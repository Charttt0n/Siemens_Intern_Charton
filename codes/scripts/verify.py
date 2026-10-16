#!/usr/bin/env python3
"""End-to-end checks; any missing dependency or failed check exits nonzero."""
import os
import pathlib
import re
import shutil
import socket
import subprocess
import sys
import time

root = pathlib.Path(__file__).resolve().parents[1]
os.chdir(root)
os.environ['PATH'] = '/opt/homebrew/opt/make/libexec/gnubin:/opt/homebrew/bin:' + os.environ.get('PATH', '')
(root / 'logs').mkdir(exist_ok=True)

def run(args, log, timeout=120):
    result = subprocess.run(args, text=True, stdout=subprocess.PIPE,
                            stderr=subprocess.STDOUT, timeout=timeout)
    (root / 'logs' / log).write_text(result.stdout)
    print(result.stdout, end='')
    if result.returncode:
        raise RuntimeError(f'{args[0]} exited with {result.returncode}')
    return result.stdout

try:
    tools = [('qemu-system-arm', (7, 0)), ('arm-none-eabi-gcc', (12, 3)),
             ('arm-none-eabi-gdb', None), ('cmake', (3, 20)),
             ('make', None), ('git', None)]
    versions = []
    for name, minimum in tools:
        if not shutil.which(name):
            raise RuntimeError(f'Missing tool: {name}; run scripts/install-tools.sh')
        output = run([name, '--version'], f'{name}-version.txt')
        versions.append(output.splitlines()[0])
        match = re.search(r'(\d+)\.(\d+)', output.splitlines()[0])
        if minimum and (not match or tuple(map(int, match.groups())) < minimum):
            raise RuntimeError(f'{name} must be >= {minimum}')
    (root / 'logs/versions.txt').write_text('\n'.join(versions) + '\n')
    machines = run(['qemu-system-arm', '-machine', 'help'], 'qemu-machines.txt')
    if 'mps2-an500' not in machines:
        raise RuntimeError('QEMU lacks the mps2-an500 platform')
    run(['bash', 'scripts/build.sh'], 'build.txt')
    output = run(['bash', 'scripts/run.sh'], 'qemu-run.txt', timeout=15)
    if 'PASS: Cortex-M7; .data initialized; .bss zero; bare-metal C99' not in output:
        raise RuntimeError('Missing firmware PASS marker')
    with socket.socket() as sock:
        sock.bind(('127.0.0.1', 0))
        port = sock.getsockname()[1]
    with open(root / 'logs/qemu-debug.txt', 'w') as log:
        qemu = subprocess.Popen(['bash', 'scripts/run.sh', '-S', '-gdb',
                                 f'tcp:127.0.0.1:{port}'], stdout=log, stderr=log)
        try:
            time.sleep(1)
            commands = ['set pagination off', f'target remote 127.0.0.1:{port}',
                        'break main', 'continue', 'stepi', 'break semihost',
                        'continue', 'printf "DEMO_RESULT=0x%x\\n", demo_result',
                        'info registers pc sp', 'detach']
            args = ['arm-none-eabi-gdb', '--batch', '-q', 'build/demo.elf']
            for command in commands:
                args.extend(['-ex', command])
            output = run(args, 'gdb.txt', timeout=20)
            if 'DEMO_RESULT=0x600d' not in output:
                raise RuntimeError('GDB did not observe successful validation')
        finally:
            if qemu.poll() is None:
                qemu.terminate()
            qemu.wait(timeout=5)
    (root / 'logs/STATUS.txt').write_text('PASS: versions, build, Cortex-M7 execution, C runtime, GDB breakpoints and single step\n')
except Exception as error:
    (root / 'logs/STATUS.txt').write_text(f'NOT COMPLETE: {error}\n')
    print(f'ERROR: {error}', file=sys.stderr)
    sys.exit(1)
