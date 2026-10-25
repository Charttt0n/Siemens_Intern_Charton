set pagination off
set confirm off
set remotetimeout 5
target remote 127.0.0.1:54275
break main
continue
set {unsigned int}0xe000ed24 = 0
delete breakpoints
break boot_idle
set $pc = 0xfffffffe
continue
python
import gdb
status=int(gdb.parse_and_eval('boot_status'));number=int(gdb.parse_and_eval('exception_number'))
print('boot_status=0x%x exception_number=%d' % (status,number))
assert status==0x100 and number==3
end

python print("DEBUG_PASS")
detach
quit
