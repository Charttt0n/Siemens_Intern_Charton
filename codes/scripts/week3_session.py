"""QEMU SRAM-backed NOR lab. Checkpoints are explicit host operations.

No semihosting or hardware Flash persistence is implied. A checkpoint is taken
only at a complete UART response; SIGKILL loses changes since that checkpoint.
"""
import fcntl
import json
import os
from pathlib import Path
import shutil
import socket
import subprocess
import tempfile
import time

FLASH_BASE = 0x20300000
FLASH_SIZE = 0x100000


def connect(path, process, timeout=5):
    deadline = time.monotonic() + timeout
    while time.monotonic() < deadline:
        sock = socket.socket(socket.AF_UNIX)
        try:
            sock.connect(str(path))
            sock.settimeout(timeout)
            return sock
        except (FileNotFoundError, ConnectionRefusedError):
            sock.close()
            if process.poll() is not None:
                raise RuntimeError('QEMU exited before opening sockets')
            time.sleep(0.02)
    raise TimeoutError('QEMU socket startup timeout')


class Session:
    def __init__(self, elf, image, evidence=None, gdb_port=None):
        self.elf = Path(elf).resolve()
        self.image = Path(image).resolve()
        self.evidence = Path(evidence) if evidence else None
        self.gdb_port = gdb_port
        self.process = self.serial = self.qmp = self.qfile = self.lock = None
        self.temp = self.diag = None
        self.quiescent = False
        self.transcript = bytearray()
        self.events = []

    def __enter__(self):
        try:
            if not self.elf.is_file():
                raise ValueError(f'ELF not found: {self.elf}')
            self.image.parent.mkdir(parents=True, exist_ok=True)
            self.lock = self.image.with_name(self.image.name + '.lock').open('a+b')
            try:
                fcntl.flock(self.lock, fcntl.LOCK_EX | fcntl.LOCK_NB)
            except BlockingIOError as exc:
                raise RuntimeError('storage image is already in use') from exc
            if self.image.exists() and self.image.stat().st_size != FLASH_SIZE:
                raise ValueError(f'storage image must be exactly {FLASH_SIZE} bytes')
            self.temp = tempfile.TemporaryDirectory(prefix='w3-', dir='/tmp')
            work = Path(self.temp.name)
            seed = work / 'seed.bin'
            if self.image.exists():
                shutil.copyfile(self.image, seed)
            else:
                seed.write_bytes(b'\xff' * FLASH_SIZE)
            self.diag = (work / 'qemu.log').open('w+b')
            args = ['qemu-system-arm', '-M', 'mps2-an500', '-cpu', 'cortex-m7',
                    '-display', 'none', '-monitor', 'none', '-S',
                    '-qmp', f'unix:{work}/qmp.sock,server=on,wait=off',
                    '-chardev', f'socket,id=uart,path={work}/uart.sock,server=on,wait=off',
                    '-serial', 'chardev:uart', '-d', 'guest_errors',
                    '-kernel', str(self.elf), '-device',
                    f'loader,file={seed},addr={FLASH_BASE},force-raw=on']
            if self.gdb_port is not None:
                args += ['-gdb', f'tcp:127.0.0.1:{self.gdb_port}']
            self.process = subprocess.Popen(args, stdout=self.diag, stderr=subprocess.STDOUT)
            self.qmp = connect(work / 'qmp.sock', self.process)
            self.qfile = self.qmp.makefile('rwb')
            greeting = json.loads(self.qfile.readline())
            if 'QMP' not in greeting:
                raise RuntimeError('invalid QMP greeting')
            self.execute('qmp_capabilities')
            self.serial = connect(work / 'uart.sock', self.process)
            self.execute('cont')
            self.banner = self.response()
            return self
        except BaseException:
            self.close()
            raise

    def execute(self, name, arguments=None):
        request = {'execute': name}
        if arguments is not None:
            request['arguments'] = arguments
        self.events.append({'direction': 'request', 'monotonic_ns': time.monotonic_ns(), **request})
        self.qfile.write(json.dumps(request).encode() + b'\n')
        self.qfile.flush()
        while True:
            line = self.qfile.readline()
            if not line:
                raise RuntimeError('QMP disconnected')
            reply = json.loads(line)
            self.events.append(reply)
            if 'error' in reply:
                raise RuntimeError(f'QMP {name}: {reply["error"]}')
            if 'return' in reply:
                return reply['return']

    def response(self, timeout=5):
        data = bytearray()
        deadline = time.monotonic() + timeout
        while not data.endswith(b'> '):
            left = deadline - time.monotonic()
            if left <= 0:
                raise TimeoutError(f'UART response timeout: {bytes(data)!r}')
            self.serial.settimeout(left)
            block = self.serial.recv(4096)
            if not block:
                raise RuntimeError('UART disconnected')
            data.extend(block)
            self.transcript.extend(block)
        self.quiescent = True
        return bytes(data)

    def command(self, byte):
        if not self.quiescent or len(byte) != 1 or byte in (b'\r', b'\n'):
            raise ValueError('command requires one byte and a completed previous response')
        self.quiescent = False
        self.serial.sendall(byte)
        return self.response()

    def checkpoint(self):
        if not self.quiescent:
            raise RuntimeError('refusing snapshot with an incomplete UART response')
        self.execute('stop')
        stage = None
        try:
            dump = Path(self.temp.name) / 'flash.bin'
            self.execute('pmemsave', {'val': FLASH_BASE, 'size': FLASH_SIZE,
                                     'filename': str(dump)})
            if dump.stat().st_size != FLASH_SIZE:
                raise RuntimeError('incomplete memory snapshot')
            # Same-directory replacement protects the old checkpoint if writing
            # the new one fails; fsync both contents and directory entry.
            with tempfile.NamedTemporaryFile(dir=self.image.parent,
                                             prefix=self.image.name + '.', delete=False) as out:
                stage = Path(out.name)
                with dump.open('rb') as source:
                    shutil.copyfileobj(source, out)
                out.flush()
                os.fsync(out.fileno())
            os.replace(stage, self.image)
            stage = None
            fd = os.open(self.image.parent, os.O_RDONLY)
            try:
                os.fsync(fd)
            finally:
                os.close(fd)
        finally:
            if stage is not None:
                stage.unlink(missing_ok=True)
            self.execute('cont')

    def close(self):
        if self.process is not None and self.process.poll() is None:
            self.process.terminate()
            try:
                self.process.wait(timeout=3)
            except subprocess.TimeoutExpired:
                self.process.kill()
                self.process.wait(timeout=3)
        for stream in (self.qfile, self.qmp, self.serial):
            if stream is not None:
                stream.close()
        if self.diag is not None:
            self.diag.flush()
            self.diag.seek(0)
            diagnostics = self.diag.read()
            if self.evidence:
                self.evidence.mkdir(parents=True, exist_ok=True)
                (self.evidence / 'uart.log').write_bytes(self.transcript)
                (self.evidence / 'qemu.log').write_bytes(diagnostics)
                (self.evidence / 'qmp.json').write_text(json.dumps(self.events, indent=2) + '\n')
            self.diag.close()
        if self.temp is not None:
            self.temp.cleanup()
        if self.lock is not None:
            self.lock.close()

    def __exit__(self, *exc):
        # Never implicitly save on exceptions or process termination.
        self.close()
