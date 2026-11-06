set pagination off
set confirm off
set remotetimeout 5
target remote 127.0.0.1:58893

break *1236
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

assert reg('xpsr') & 0x1ff == 0
inf.write_memory(0x70000, bytes.fromhex('62b6002080f3118880f31388bff36f8f044880f30988022080f31488bff36f8f00dffee700f01f20'))
end
set {unsigned int}0xe000ed24 = 0
set {unsigned int}0xe000ed28 = 0xffffffff
set {unsigned int}0xe000ed2c = 0xffffffff
delete breakpoints
break *256
set $pc = 0x70000
continue
python
state = snapshot()
assert state['ipsr'] == 11, state
assert state['sp'] == state['msp']
assert state['lr'] == 4294967293
assert state['cfsr'] & 0 == 0, state
if 11 == 3: assert state['hfsr'] & (1<<30)
if True:
    assert state['msp'] == 0x20010000 and state['psp'] == 0x201fefe0
    assert word(state['psp']+28) & 0x1ff == 0
print('FAULT ' + json.dumps(state, sort_keys=True))
end
delete breakpoints
break *960
continue
python
assert word(536871092) == 0x100
assert word(536871088) == 11
end

python print("DEBUG_PASS")
detach
quit
