#!/usr/bin/env bash
# Run the four acceptance scenarios with a disposable, caller-owned NOR.
set -euo pipefail
cd "$(dirname "$0")/.."
source scripts/env.sh
dest=${1:-build/demo-$(date +%Y%m%d-%H%M%S)}
mkdir -p "$(dirname "$dest")"
mkdir "$dest"  # refuse to mix transcripts from different runs
bash scripts/build-week7.sh
build=build/week7
python3 scripts/seed-week6.py --slot-a "$build/application-week7-v1.pkg" --output "$dest/seed.bin"
run() { python3 scripts/send-week7.py --image "$1" --evidence "$2" "${@:3}"; }
run "$dest/seed.bin" "$dest/boot"
run "$dest/boot/nor.bin" "$dest/upgrade" --package "$build/application-week7-v2.pkg"
python3 scripts/pack-week5.py --app "$build/application-week7-v3.bin" --version 3 --output "$dest/unsigned.pkg"
if run "$dest/upgrade/nor.bin" "$dest/refusal" --package "$dest/unsigned.pkg"; then
    echo "Unsigned package unexpectedly committed" >&2; exit 1
fi
# Corrupt only the disposable active B slot. A still contains signed v1.
python3 - "$dest/upgrade/nor.bin" "$dest/damaged.bin" <<'PY'
from pathlib import Path
import sys
raw = bytearray(Path(sys.argv[1]).read_bytes())
raw[0x80000 + 32 + 192] ^= 1
Path(sys.argv[2]).write_bytes(raw)
PY
run "$dest/damaged.bin" "$dest/recovery"
python3 - "$dest" <<'PY'
from pathlib import Path
import sys
root = Path(sys.argv[1])
for name, markers in {
    'boot': ['APP: VERSION v1'],
    'upgrade': ['UPGRADE: COMMITTED', 'TRIAL: LAUNCH', 'APP: VERSION v2'],
    'refusal': ['UPGRADE: FAIL', 'APP: VERSION v2'],
    'recovery': ['ROLLBACK: SELECT A', 'APP: VERSION v1'],
}.items():
    log = (root/name/'uart.log').read_bytes()
    for marker in [*markers, 'APP: BOOT CONFIRMED']:
        if marker.encode() not in log:
            raise SystemExit(f'{name}: missing {marker}')
    if name == 'refusal' and b'UPGRADE: COMMITTED' in log:
        raise SystemExit('unsigned package committed')
    print(f'PASS {name}')
PY
