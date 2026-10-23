set pagination off
set confirm off
set remotetimeout 5
target remote 127.0.0.1:56627

break *1924
continue
python
import gdb
inf=gdb.selected_inferior()
byte=bytes(inf.read_memory(0x100c8,1))[0]
inf.write_memory(0x100c8,bytes([byte^1]))
end
delete breakpoints
break *956
continue
python
import gdb
assert int(gdb.parse_and_eval('boot_status'))==8
end

python print("DEBUG_PASS")
detach
quit
