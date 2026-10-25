set pagination off
set confirm off
set remotetimeout 5
target remote 127.0.0.1:54273
break image_validate
continue
set var dev->memory = 0
delete breakpoints
break boot_idle
continue
python
import gdb
assert int(gdb.parse_and_eval('boot_status'))==1
end

python print("DEBUG_PASS")
detach
quit
