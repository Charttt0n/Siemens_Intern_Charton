set pagination off
set confirm off
set remotetimeout 5
target remote 127.0.0.1:52386

break *1156
continue
python
import gdb
# QEMU exposes these registers but this backend ignores GDB writes to PRIMASK.
# Execute the actual CPSID I before injecting pending interrupts.
for _ in range(8):
    if int(gdb.parse_and_eval('$primask')) == 1: break
    gdb.execute('stepi')
assert int(gdb.parse_and_eval('$primask')) == 1
end
set {unsigned int}0xe000e100 = 0x80
set {unsigned int}0xe000e200 = 0x80
set {unsigned int}0xe000e014 = 999999
set {unsigned int}0xe000e010 = 7
set {unsigned int}0xe000ed04 = 0x14000000
delete breakpoints
break *65728
continue
python
import gdb, struct
inf=gdb.selected_inferior()
def word(addr): return struct.unpack('<I',bytes(inf.read_memory(addr,4)))[0]
assert int(gdb.parse_and_eval('$sp'))==0x20200000
assert int(gdb.parse_and_eval('$primask'))==1
for reg in ('basepri','faultmask','control'): assert int(gdb.parse_and_eval('$'+reg))==0
assert word(0xe000ed08)==0x10000
assert word(0xe000e100)==0 and word(0xe000e200)==0
assert word(0xe000e010)&7==0
assert word(0xe000ed04)&0x14000000==0
assert word(0x40004008)==0
inf.write_memory(536936448,b'\xcc'*12)
inf.write_memory(536936460,b'\xa5'*36)
inf.write_memory(536936496,b'\x78\x56\x34\x12')
end
delete breakpoints
break *65804
continue
python
assert word(536936472)==1
assert word(536936460)==1
assert word(536936468)==1
assert word(536936464)==0
assert bytes(inf.read_memory(536936448,12))==struct.pack('<3I',0x12345678,0xa55aa55a,0x11223344)
assert bytes(inf.read_memory(536936476,20))==bytes(20)
assert word(536936496)==0x12345678
print('APP MSP VTOR masks NVIC SysTick CRT sentinel SVC: PASS')
end

python print("DEBUG_PASS")
detach
quit
