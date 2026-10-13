set pagination off
set confirm off
set remotetimeout 5
target remote 127.0.0.1:55115
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
break *main
continue
python
# UDF #0 forces a synchronous undefined-instruction exception. UsageFault
# is disabled after reset, so this must escalate to HardFault (IPSR=3).
inf.write_memory(addr('main') & ~1, b'\x00\xde')
print('INJECTED: UDF #0 at main')
end
delete breakpoints
break fault_idle
continue
python
require(int(gdb.parse_and_eval('exception_number')) == 3, 'HardFault not recorded')
require(int(gdb.parse_and_eval('$xpsr')) & 0x1ff == 3, 'not in HardFault context')
print('EXCEPTION_NUMBER=3 (HardFault); xPSR=%#x' % int(gdb.parse_and_eval('$xpsr')))
end

python print("CHECK_PASS:undefined-instruction")
detach
quit
