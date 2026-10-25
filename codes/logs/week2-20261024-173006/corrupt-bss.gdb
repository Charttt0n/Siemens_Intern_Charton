set pagination off
set confirm off
set remotetimeout 5
target remote 127.0.0.1:54841
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
set variable runtime_bss[6] = 0xdeadbeef
python
require(int(gdb.parse_and_eval('runtime_bss[6]')) == 0xdeadbeef, 'injection failed')
print('INJECTED: runtime_bss[6]=0xdeadbeef')
end
delete breakpoints
break boot_idle
continue
python
require(int(gdb.parse_and_eval('boot_status')) == 2, 'must reject corrupt runtime')
print('BOOT_STATUS=2 (expected failure)')
end

python print("CHECK_PASS:corrupt-bss")
detach
quit
