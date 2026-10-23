set pagination off
set confirm off
set remotetimeout 5
target remote 127.0.0.1:56590
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
break uart_write
continue
set {unsigned int}0x40004008 = 0
delete breakpoints
break boot_idle
continue
python
require(int(gdb.parse_and_eval('boot_status')) == 3, 'disabled UART not detected')
print('BOOT_STATUS=3 (expected UART failure)')
end

python print("CHECK_PASS:disabled-uart")
detach
quit
