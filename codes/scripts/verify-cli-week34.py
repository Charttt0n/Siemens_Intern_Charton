#!/usr/bin/env python3
"""Exercise the delivered Week 3/4 CLI in isolated copies, including cleanup."""
import fcntl
import json
import os
from pathlib import Path
import shutil
import signal
import subprocess
import sys
import tempfile
import time
from datetime import datetime

ROOT = Path(__file__).resolve().parents[1]
os.environ['PATH'] = '/opt/homebrew/opt/make/libexec/gnubin:/opt/homebrew/bin:' + os.environ.get('PATH', '')
OUT = ROOT / 'logs' / datetime.now().strftime('cli-week34-%Y%m%d-%H%M%S')


def alive(pid):
    try:
        os.kill(pid, 0)
        return True
    except ProcessLookupError:
        return False


def main():
    OUT.mkdir(parents=True)
    records = []
    status = 'FAIL'
    try:
        with tempfile.TemporaryDirectory(prefix='cli-week34-') as temp:
            base = Path(temp)
            real_qemu = shutil.which('qemu-system-arm')
            assert real_qemu
            for stage in (3, 4):
                work = base / f'w{stage}'
                shutil.copytree(ROOT, work, ignore=shutil.ignore_patterns('build', 'logs', '__pycache__', '.DS_Store'))
                assert not (work / 'build').exists()
                audit = base / f'audit{stage}.json'
                wrapper = base / f'qemu-audit{stage}.py'
                wrapper.write_text('''#!''' + sys.executable + '''
import json, os, signal, sys, time
from pathlib import Path
args=sys.argv[1:]
Path(os.environ['CLI_AUDIT']).write_text(json.dumps({'pid':os.getpid(),'args':args}))
mode=os.environ.get('CLI_MODE','real')
if mode=='fail': sys.exit(42)
if mode=='stall':
 signal.signal(signal.SIGTERM,signal.SIG_IGN)
 while True: time.sleep(1)
os.execv(os.environ['CLI_REAL_QEMU'],[os.environ['CLI_REAL_QEMU']]+args)
''')
                wrapper.chmod(0o755)
                # Only substitute executable lookup in the isolated test copy.
                # The wrapper execs real QEMU with exactly the original arguments.
                session = work / 'scripts/week3_session.py'
                session.write_text(session.read_text().replace("args = ['qemu-system-arm',", 'args = [' + repr(str(wrapper)) + ','))
                env = dict(os.environ, CLI_AUDIT=str(audit), CLI_REAL_QEMU=real_qemu)
                runner = ['python3', f'scripts/run-week{stage}.py']
                builder = ['bash', f'scripts/build-week{stage}.sh']
                image = work / ('build/week3-flash.bin' if stage == 3 else 'build/week4/demo-flash.bin')
                elf = work / f'build/week{stage}/bootloader-week{stage}.elf'

                def case(name, args, steps, inputs, expected, code, marker='', mode='real', stdin=None, qemu=False):
                    audit.unlink(missing_ok=True)
                    started = time.monotonic()
                    p = subprocess.Popen(args, cwd=work, env=dict(env, CLI_MODE=mode), stdin=subprocess.PIPE,
                                         stdout=subprocess.PIPE, stderr=subprocess.STDOUT, start_new_session=True)
                    watchdog = False
                    try:
                        raw, _ = p.communicate(stdin, timeout=120 if name == 'BUILD' else 20)
                    except subprocess.TimeoutExpired:
                        watchdog = True
                        os.killpg(p.pid, signal.SIGKILL)
                        raw, _ = p.communicate(timeout=5)
                    text = raw.decode(errors='replace')
                    child = json.loads(audit.read_text()) if audit.exists() else None
                    clean = True
                    cleanup = {'qemu_started': child is not None, 'pid': child['pid'] if child else None}
                    if child:
                        cleanup['process_gone'] = not alive(child['pid'])
                        dirs = [Path(a.split('unix:', 1)[1].split('/qmp.sock')[0]) for a in child['args'] if a.startswith('unix:')]
                        cleanup['temporary_dirs_removed'] = all(not d.exists() for d in dirs)
                        cleanup['temporary_dirs'] = list(map(str, dirs))
                        clean = cleanup['process_gone'] and cleanup['temporary_dirs_removed']
                    lockpath = image.with_name(image.name + '.lock')
                    if lockpath.exists():
                        with lockpath.open('a+b') as lock:
                            try:
                                fcntl.flock(lock, fcntl.LOCK_EX | fcntl.LOCK_NB)
                                cleanup['lock_released'] = True
                            except BlockingIOError:
                                cleanup['lock_released'] = False
                                clean = False
                    ok = p.returncode == code and marker in text and not watchdog and clean and (child is not None) == qemu
                    ident = f'CLI34-W{stage}-{name}'
                    record = dict(id=ident, command=args, cwd='isolated codes copy', steps=steps, input=inputs,
                                  expected=expected, expected_exit=code, actual_exit=p.returncode,
                                  seconds=round(time.monotonic()-started, 3), watchdog=watchdog, cleanup=cleanup,
                                  actual=text[-1600:], status='PASS' if ok else 'FAIL', evidence=ident+'.log')
                    records.append(record)
                    (OUT / record['evidence']).write_text('$ '+' '.join(args)+'\n'+text+'\n'+json.dumps(record, ensure_ascii=False, indent=2)+'\n')
                    print(record['status'], ident, 'exit', p.returncode, flush=True)
                    if not clean and child and alive(child['pid']):
                        os.kill(child['pid'], signal.SIGKILL)
                    assert ok, ident

                case('MISSING', runner, '复制工程时排除 build；直接运行默认命令', 'ELF 和镜像均不存在', '退出1；不启动QEMU', 1)
                case('BUILD', builder, '确认不存在 build；执行交付构建命令', '原始工程，无构建缓存', '退出0，生成该阶段ELF及所需镜像', 0)
                assert elf.is_file()
                if stage == 4: assert image.stat().st_size == 0x100000
                case('RUN', runner, '构建后执行默认运行命令；检查输出及清理', 'p、save、quit' if stage==3 else '默认有效镜像', '退出0；固件成功标记；QEMU退出、临时目录删除、文件锁释放', 0, 'PONG' if stage==3 else 'APP: RUNNING', stdin=b'p\nsave\nquit\n' if stage==3 else None, qemu=True)
                saved_elf = elf.read_bytes()
                elf.unlink()
                case('MISSING-ELF', runner, '保留镜像；仅移走ELF；直接运行', 'ELF不存在', '退出1；ELF not found；不启动QEMU', 1, 'ELF not found')
                elf.write_bytes(saved_elf)
                saved_image = image.read_bytes()
                image.unlink()
                if stage == 4:
                    case('MISSING-IMAGE', runner, '保留ELF；移走镜像；直接运行', '默认镜像不存在', '退出1；image missing；不启动QEMU', 1, 'image missing')
                else:
                    case('NEW-IMAGE', runner, '保留ELF；移走镜像；执行并输入quit', '默认镜像不存在；quit', 'Week3允许首次创建；退出0；保存1MiB镜像且清理QEMU', 0, 'Saved:', stdin=b'quit\n', qemu=True)
                    assert image.stat().st_size == 0x100000
                image.write_bytes(saved_image)
                for mode,name,marker in [('fail','QEMU-FAIL','QEMU exited'),('stall','START-TIMEOUT','startup timeout')]:
                    case(name, runner, '只在副本替换QEMU启动依赖；执行原运行命令', '子命令返回42' if mode=='fail' else '子进程不建socket且忽略SIGTERM', '入口退出1；无成功标记；清理子进程、临时目录和文件锁', 1, marker, mode=mode, qemu=True)
                # Real emulator runs an infinite loop with no UART output: exercise
                # the Session UART deadline, not an external watchdog timeout.
                loop = base/f'loop{stage}.S'
                loop.write_text('.syntax unified\n.cpu cortex-m7\n.thumb\n.section .isr_vector,"a"\n.word 0x20200000\n.word reset\n.text\n.thumb_func\n.global reset\nreset: b reset\n')
                linker = base/f'loop{stage}.ld'
                linker.write_text('SECTIONS { . = 0; .vectors : { *(.isr_vector) } .text : { *(.text*) } }')
                subprocess.run(['arm-none-eabi-gcc','-mcpu=cortex-m7','-mthumb','-nostdlib','-T',str(linker),str(loop),'-o',str(elf)],check=True,stdout=subprocess.PIPE,stderr=subprocess.PIPE)
                case('UART-TIMEOUT', runner, '仅将副本ELF换成无限循环固件；运行真实QEMU', '有效ELF，无UART输出', '约5秒内部UART超时；入口退出1；QEMU和资源清理完成', 1, 'timed out', qemu=True)
                elf.write_bytes(saved_elf)
                envfile = work/'scripts/env.sh'
                original = envfile.read_text()
                for mode in ['configure','build']:
                    envfile.write_text(original + '\ncmake() { '+('return 42;' if mode=='configure' else 'if [[ "${1:-}" == "--build" ]]; then return 42; fi; command cmake "$@";')+' }\n')
                    case('FAIL-'+mode.upper(), builder, '保留旧产物；注入cmake失败；运行交付构建命令', f'cmake {mode} 返回42', '退出42；不被旧产物掩盖；不启动QEMU', 42)
                envfile.write_text(original)
                case('RECOVERY', runner, '恢复依赖和ELF后再次运行，检验无残留锁/进程干扰', '原有效ELF和镜像；Week3输入quit', '退出0并完成清理', 0, 'Saved:' if stage==3 else 'APP: RUNNING', stdin=b'quit\n' if stage==3 else None, qemu=True)
            status = 'PASS'
    finally:
        (OUT/'summary.json').write_text(json.dumps(dict(status=status, checks=records), ensure_ascii=False, indent=2)+'\n')
        (ROOT/'logs/cli-week34-latest.txt').write_text(str(OUT)+'\n')
        print('Evidence:', OUT)
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
