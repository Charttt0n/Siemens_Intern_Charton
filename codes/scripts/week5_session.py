"""Binary Week-5 QEMU session. NOR persistence is an explicit host snapshot.

Startup has a fixed command-window marker. During YMODEM all reads/writes are
binary; neither '>' nor newlines delimit protocol frames. Fault snapshots may
be taken at a latched test hook without waiting for a complete UART response.
"""
import os
from pathlib import Path
import socket
import sys
import tempfile
import time
from week3_session import Session as QemuSession, FLASH_BASE, FLASH_SIZE

VENDOR = Path(__file__).resolve().parents[1] / 'tests/third_party'


class PeerCancelled(BaseException):
    """Control-flow cancellation bypassing the vendor's IO retry handler.

    The pinned sender ignores CAN while waiting for some ACKs and catches
    Exception from callbacks. Two consecutive CAN bytes end this upload,
    instead of retrying into application UART text for tens of seconds.
    """


def sender(channel, packet_size=1024):
    if str(VENDOR) not in sys.path:
        sys.path.insert(0, str(VENDOR))
    import logging
    logging.getLogger('ModemSocket').setLevel(logging.CRITICAL)
    from ymodem.Protocol import ProtocolType
    from ymodem.Socket import ModemSocket
    return ModemSocket(channel.read, channel.write,
                       protocol_type=ProtocolType.YMODEM,
                       packet_size=packet_size, style_id='UNIX_RZ_SZ')


class Session(QemuSession):
    def __init__(self, *args, **kwargs):
        super().__init__(*args, **kwargs)
        self.pending = bytearray()
        self.sending = False
        self.cancel_tail = b''
        self.cancel_sender = False

    def _recv(self, size, timeout):
        self.serial.settimeout(timeout)
        data = self.serial.recv(size)
        if not data:
            raise ConnectionError('UART disconnected')
        self.transcript.extend(data)
        return data

    def read(self, size, timeout=1):
        if self.cancel_sender:
            raise PeerCancelled()
        if self.pending:
            data = bytes(self.pending[:size])
            del self.pending[:size]
        else:
            try:
                data = self._recv(size, timeout)
            except socket.timeout:
                return b''
        if self.sending:
            combined = self.cancel_tail + data
            if b'\x18\x18' in combined:
                raise PeerCancelled()
            self.cancel_tail = combined[-1:]
        return data

    def write(self, data, timeout=1):
        if self.cancel_sender:
            raise PeerCancelled()
        self.serial.settimeout(timeout)
        self.serial.sendall(bytes(data))
        return len(data)  # the independent Python sender expects a byte count

    def read_until(self, marker, timeout=20):
        end = time.monotonic() + timeout
        while marker not in self.pending:
            left = end - time.monotonic()
            if left <= 0:
                raise TimeoutError(f'UART missing {marker!r}: {bytes(self.pending[-400:])!r}')
            try:
                self.pending.extend(self._recv(4096, left))
            except socket.timeout as error:
                raise TimeoutError(f'UART missing {marker!r}: {bytes(self.pending[-400:])!r}') from error
        boundary = self.pending.index(marker) + len(marker)
        data = bytes(self.pending[:boundary])
        del self.pending[:boundary]
        return data

    def response(self, timeout=5):
        # Called by the shared QMP launcher, only to establish the initial UART
        # boundary. It cannot mistake a byte inside a firmware frame for text.
        return self.read_until(b'BOOT5: command window\r\n', timeout)

    def boot(self, timeout=20, transcript_start=None):
        marker = b'APP: RUNNING\r\n> '
        if transcript_start is None:
            data = self.read_until(marker, timeout)
        else:
            # A sender may read the application's first text while reacting to
            # an early CAN/rejection. The byte log is authoritative even then;
            # do not wait for a second banner that the guest will never emit.
            end = time.monotonic() + timeout
            while marker not in self.transcript[transcript_start:]:
                left = end - time.monotonic()
                if left <= 0:
                    raise TimeoutError(f'no application after upload: {bytes(self.transcript[transcript_start:][-400:])!r}')
                self._recv(4096, left)
            data = bytes(self.transcript[transcript_start:])
            self.pending.clear()
        self.quiescent = True
        return data

    def upload(self, path, command=b'u', packet_size=1024, channel=None):
        self.quiescent = False
        start = len(self.transcript)
        self.write(command)
        self.sending = True
        self.cancel_tail = b''
        try:
            sent = sender(channel or self, packet_size).send([str(Path(path).resolve())])
        except PeerCancelled:
            sent = False
        finally:
            self.sending = False
        text = self.boot(transcript_start=start)
        return sent, text

    def fault_snapshot(self, output):
        """Pause and export current NOR, even in the middle of an operation.

        Caller must have established its controlled fault point. No resume:
        restart a fresh process from this image, never the old seed file.
        """
        self.execute('stop')
        output = Path(output).resolve()
        output.parent.mkdir(parents=True, exist_ok=True)
        stage = None
        try:
            with tempfile.NamedTemporaryFile(dir=output.parent, delete=False) as stream:
                stage = Path(stream.name)
            self.execute('pmemsave', {'val': FLASH_BASE, 'size': FLASH_SIZE,
                                     'filename': str(stage)})
            if stage.stat().st_size != FLASH_SIZE:
                raise RuntimeError('incomplete NOR fault snapshot')
            with stage.open('rb') as stream:
                os.fsync(stream.fileno())
            os.replace(stage, output)
            stage = None
            fd = os.open(output.parent, os.O_RDONLY)
            try:
                os.fsync(fd)
            finally:
                os.close(fd)
        finally:
            if stage is not None:
                stage.unlink(missing_ok=True)
        self.events.append({'fault_snapshot': str(output), 'bytes': FLASH_SIZE})
        return output

    def close(self):
        self.cancel_sender = True
        super().close()
