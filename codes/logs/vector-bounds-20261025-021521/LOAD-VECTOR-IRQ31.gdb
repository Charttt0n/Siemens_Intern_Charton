set pagination off
set confirm off
set remotetimeout 5
target remote 127.0.0.1:54269
break boot_verify_loaded
continue
set {unsigned int}0x100bc = 0
python
import gdb,zlib
inf=gdb.selected_inferior();size=int(gdb.parse_and_eval('boot_image.size'))
crc=zlib.crc32(bytes(inf.read_memory(0x10000,size)))
gdb.execute('set var boot_image.crc = %d' % crc)
end
delete breakpoints
break boot_idle
continue
python
assert int(gdb.parse_and_eval('boot_status'))==7
end

python print("DEBUG_PASS")
detach
quit
