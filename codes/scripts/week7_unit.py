"""Run official Unity against real C modules; ASan/UBSan on the host.
Legacy contract functions are individually adapted, not reimplemented.
"""
from pathlib import Path
import json, re, subprocess
from lab_signing_key import KEY_DIR, prepare_lab_key
ROOT=Path(__file__).resolve().parents[1]

def run_units(out, package):
    prepare_lab_key()
    out=Path(out);out.mkdir(parents=True,exist_ok=True)
    flags=['-std=c99','-g','-O1','-fsanitize=address,undefined','-fno-omit-frame-pointer',
           '-Itests/third_party/unity/src','-Isrc/week7','-Isrc/week6','-Isrc/week5','-Isrc/week4','-Isrc/week3','-Isrc/week2']
    common=['tests/third_party/unity/src/unity.c','src/week7/crc32.c','src/week3/flash.c']
    suites={'flash':[], 'image':['src/week4/image.c'],
            'boot_meta':['src/week5/boot_meta.c'],
            'ymodem':['src/week5/ymodem.c','src/week5/crc16.c'],
            'update':['src/week5/update.c','src/week5/boot_meta.c','src/week4/image.c','src/week5/ymodem.c','src/week5/crc16.c']}
    # Use exactly the same pinned crypto source list as the firmware target.
    cmake=(ROOT/'cmake/week6.cmake').read_text().split('set_source_files_properties')[0]
    crypto=['tests/third_party/mbedtls/library/'+n for n in re.findall(r'library/([^\s)]+\.c)',cmake)]
    suites['core']=['src/week4/image.c','src/week5/boot_meta.c','src/week6/boot_config.c','src/week6/secure_image.c',
                    'src/week6/update.c','src/week6/watchdog.c','src/week7/control.c']+crypto
    results=[]
    for name,sources in suites.items():
        source=f'tests/week7/test_{name}_unity.c' if name!='core' else 'tests/week7/test_core.c'
        extra=[] if name!='core' else ['-DWEEK6_HOST_TEST','-DMBEDTLS_CONFIG_FILE="week6_mbedtls_config.h"',
                f'-I{KEY_DIR}',
                '-Itests/third_party/mbedtls/include','-Itests/third_party/mbedtls/library']
        exe=out/name
        cmd=['cc']+flags+extra+[source]+common+sources+['-o',str(exe)]
        p=subprocess.run(cmd,cwd=ROOT,capture_output=True,text=True)
        (out/f'{name}-build.txt').write_text(' '.join(cmd)+'\n'+p.stdout+p.stderr)
        if p.returncode: raise RuntimeError(f'Unity {name} build: {p.stderr[-2000:]}')
        p=subprocess.run([str(exe)]+([str(package)] if name=='core' else []),cwd=ROOT,capture_output=True,text=True,timeout=60)
        log=p.stdout+p.stderr;(out/f'{name}.txt').write_text(log)
        match=re.search(r'(\d+) Tests (\d+) Failures (\d+) Ignored',log)
        if p.returncode or not match or int(match[2]) or int(match[3]): raise RuntimeError(f'Unity {name}: {log[-2500:]}')
        results.append({'suite':name,'tests':int(match[1]),'failures':0,'ignored':0,'log':str(out/f'{name}.txt')})
        print(f'PASS Unity {name}: {match[1]}',flush=True)
    (out/'summary.json').write_text(json.dumps(results,indent=2)+'\n')
    return results

if __name__=='__main__':
    import argparse
    p=argparse.ArgumentParser();p.add_argument('--out',type=Path,default=ROOT/'build/week7/unity');p.add_argument('--package',type=Path,default=ROOT/'build/week7/application-week7-v1.pkg')
    a=p.parse_args();run_units(a.out.resolve(),a.package.resolve())
