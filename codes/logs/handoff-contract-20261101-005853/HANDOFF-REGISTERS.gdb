set pagination off
set confirm off
set remotetimeout 5
target remote 127.0.0.1:58877

break *1156
continue
python

import gdb, json, struct
inf = gdb.selected_inferior()
def word(addr): return struct.unpack('<I', bytes(inf.read_memory(addr, 4)))[0]
def reg(name): return int(gdb.parse_and_eval('$' + name)) & 0xffffffff
def snapshot():
    data = {name: reg(name) for name in ('sp','msp','psp','control','primask','basepri','faultmask','xpsr','lr')}
    for name, addr in {'vtor':0xe000ed08,'aircr':0xe000ed0c,'shpr1':0xe000ed18,
                       'shpr2':0xe000ed1c,'shpr3':0xe000ed20,'shcsr':0xe000ed24,
                       'cfsr':0xe000ed28,'hfsr':0xe000ed2c,'cpacr':0xe000ed88,
                       'fpccr':0xe000ef34,'ccr':0xe000ed14}.items(): data[name] = word(addr)
    data['ipsr'] = data['xpsr'] & 0x1ff
    return data

before = snapshot()
assert before['ipsr'] == 0 and not (before['control'] & 3)
print('BEFORE ' + json.dumps(before, sort_keys=True))
for _ in range(8):
    if reg('primask') == 1: break
    gdb.execute('stepi')
assert reg('primask') == 1
end
set {unsigned int}0xe000e100 = 0x80
set {unsigned int}0xe000e200 = 0x80
set {unsigned int}0xe000e014 = 999999
set {unsigned int}0xe000e010 = 7
set {unsigned int}0xe000ed04 = 0x14000000
set {unsigned int}0xe000ed18 = 0x00604020
set {unsigned int}0xe000ed1c = 0x80000000
python
assert word(0xe000e100) & 0x80 and word(0xe000e200) & 0x80
assert word(0xe000ed04) & 0x14000000 == 0x14000000
assert word(0xe000e010) & 7 == 7
assert word(0xe000e300) == 0
print('DIRTY pending and enable state confirmed')
end
delete breakpoints
break *65728
continue
python
entry = snapshot()
assert entry['ipsr'] == 0 and entry['control'] == 0
assert entry['msp'] == 0x20200000 and entry['psp'] == 0 and entry['sp'] == entry['msp']
assert entry['primask'] == 1 and entry['basepri'] == entry['faultmask'] == 0
assert entry['vtor'] == 0x10000
assert entry['shpr1'] == 0x00604020 and entry['shpr2'] == 0x80000000
assert ((entry['aircr'] >> 8) & 7) == ((before['aircr'] >> 8) & 7)
assert entry['shcsr'] == before['shcsr'] == 0
assert entry['cpacr'] & 0x00f00000 == 0 and entry['fpccr'] & 1 == 0
assert word(0xe000e100) == word(0xe000e200) == word(0xe000e300) == 0
assert word(0xe000e010) & 7 == 0 and word(0xe000ed04) & 0x14000000 == 0
print('ENTRY ' + json.dumps(entry, sort_keys=True))
end
delete breakpoints
break *65832
continue
python
svc = snapshot()
assert svc['ipsr'] == 11 and svc['sp'] == svc['msp'] and svc['psp'] == 0
assert svc['primask'] == svc['basepri'] == svc['faultmask'] == 0
assert svc['lr'] == 0xfffffff9
print('SVC ' + json.dumps(svc, sort_keys=True))
end
delete breakpoints
break *65804
continue
python
final = snapshot()
assert final['ipsr'] == 0 and final['control'] == 0 and final['primask'] == 0
assert word(536936472) == word(536936460) == word(536936468) == 1
assert word(536936464) == 0
print('FINAL ' + json.dumps(final, sort_keys=True))
end

python print("DEBUG_PASS")
detach
quit
