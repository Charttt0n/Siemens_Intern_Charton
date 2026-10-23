set pagination off
set confirm off
set remotetimeout 5
target remote 127.0.0.1:53144
python
import gdb, struct
inf = gdb.selected_inferior()
def addr(name):
    return int(gdb.parse_and_eval('(unsigned int)&' + name))
def word(where):
    return struct.unpack('<I', bytes(inf.read_memory(where, 4)))[0]
def require(ok, message):
    if not ok:
        raise gdb.GdbError(message)
end
python
require(int(gdb.parse_and_eval('$sp')) == addr('_stack_top'), 'reset MSP')
require(int(gdb.parse_and_eval('$pc')) == (addr('Reset_Handler') & ~1), 'reset PC')
require(word(0) == addr('_stack_top'), 'vector MSP')
require(word(4) == (addr('Reset_Handler') | 1), 'vector entry Thumb bit')
reserved = {7,8,9,10,13}
for i in range(2,48):
    require(word(i*4) == (0 if i in reserved else addr('Default_Handler') | 1), 'vector %d' % i)
ds,de,bs,be = [addr(s) for s in ('_sdata','_edata','_sbss','_ebss')]
require(ds < de <= bs < be < addr('_stack_bottom'), 'section boundaries')
expected = bytes(inf.read_memory(addr('_sidata'), de-ds))
inf.write_memory(ds,b'\xcc'*(de-ds))
inf.write_memory(bs,b'\xa5'*(be-bs))
inf.write_memory(be,b'\x78\x56\x34\x12')
print('RESET: MSP=%#x PC=%#x VECTOR=%#x; data=%d BSS=%d; vectors=48' % (word(0),int(gdb.parse_and_eval('$pc')),word(4),de-ds,be-bs))
require(bytes(inf.read_memory(ds,de-ds)) == b'\xcc'*(de-ds), 'data injection')
require(bytes(inf.read_memory(bs,be-bs)) == b'\xa5'*(be-bs), 'BSS injection')
end
break *main
continue
python
require(bytes(inf.read_memory(ds,de-ds)) == expected, 'data copy')
require(bytes(inf.read_memory(bs,be-bs)) == bytes(be-bs), 'BSS clear')
require(word(be) == 0x12345678, 'upper sentinel')
require(int(gdb.parse_and_eval('$sp')) % 8 == 0, 'C-entry stack alignment')
print('DATA_RESTORED=' + expected.hex())
print('BSS_ZERO_BYTES=%d UPPER_SENTINEL=%#x' % (be-bs,word(be)))
end
delete breakpoints
break boot_idle
continue
python
require(int(gdb.parse_and_eval('boot_status')) == 1, 'success state')
require(word(0xE000ED08) == addr('vectors'), 'VTOR')
require(word(0x40004008) == 1, 'UART TX only')
require(word(0x40004010) == 217, 'UART divisor')
require(word(0x40004004) & 5 == 0, 'UART drained, no TX overrun')
print('BOOT_STATUS=1 VTOR=%#x UART_CTRL=%#x BAUDDIV=%d' % (word(0xE000ED08),word(0x40004008),word(0x40004010)))
end

python print("CHECK_PASS:poisoned-startup-and-registers")
detach
quit
