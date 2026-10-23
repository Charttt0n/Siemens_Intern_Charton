set pagination off
set confirm off
set remotetimeout 5
target remote 127.0.0.1:56567
python
import gdb
import struct
inf = gdb.selected_inferior()
def addr(name):
    return int(gdb.parse_and_eval('(unsigned int)&' + name))
def word(where):
    return struct.unpack('<I', bytes(inf.read_memory(where, 4)))[0]
def require(condition, message):
    if not condition:
        raise gdb.GdbError(message)
stack = addr('_stack_top')
reset = addr('Reset_Handler') & ~1
require(int(gdb.parse_and_eval('$sp')) == stack, 'Wrong initial MSP')
require(int(gdb.parse_and_eval('$pc')) == reset, 'Wrong reset PC')
require(word(0) == stack and word(4) == (reset | 1), 'Wrong reset vectors')
print('RESET_MSP=0x%x RESET_PC=0x%x RESET_VECTOR=0x%x' % (stack, reset, word(4)))
ds, de = addr('_sdata'), addr('_edata')
bs, be = addr('_sbss'), addr('_ebss')
require(ds < de <= bs < be, 'Unexpected section layout')
expected = bytes(inf.read_memory(addr('_sidata'), de-ds))
inf.write_memory(ds, b'\xCC' * (de-ds))
inf.write_memory(bs, b'\xA5' * (be-bs))
inf.write_memory(be, b'\x78\x56\x34\x12')
require(bytes(inf.read_memory(ds,de-ds)) == b'\xCC'*(de-ds), 'Data injection failed')
require(bytes(inf.read_memory(bs,be-bs)) == b'\xA5'*(be-bs), 'BSS injection failed')
print('INJECTED_DATA=' + bytes(inf.read_memory(ds,de-ds)).hex())
print('INJECTED_BSS=' + bytes(inf.read_memory(bs,be-bs)).hex())
end
break main
continue
python
require(int(gdb.parse_and_eval('$pc')) == (addr('main') & ~1), 'main breakpoint not reached')
actual = bytes(inf.read_memory(ds,de-ds))
zeros = bytes(inf.read_memory(bs,be-bs))
require(actual == expected, '.data copy failed')
require(zeros == bytes(be-bs), '.bss clear failed')
require(word(be) == 0x12345678, 'Upper-bound sentinel overwritten')
print('RESTORED_DATA=' + actual.hex())
print('CLEARED_BSS=' + zeros.hex())
print('UPPER_SENTINEL=0x%x' % word(be))
end
stepi
python
require(int(gdb.parse_and_eval('$pc')) != (addr('main') & ~1), 'Single step did not advance PC')
print('STEP_PC=0x%x' % int(gdb.parse_and_eval('$pc')))
print('CHECK_PASS:poisoned-startup')
end

detach
quit
