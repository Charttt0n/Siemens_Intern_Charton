set pagination off
set confirm off
target remote 127.0.0.1:55088
python
import gdb,struct
inf=gdb.selected_inferior()
def word(a): return struct.unpack('<I',bytes(inf.read_memory(a,4)))[0]
d=word(0x40004010); c=word(0x40004008)
print('BAUDDIV[0x40004010] = 0x%08x (%d)' % (d,d))
print('CTRL[0x40004008] = 0x%08x' % c)
assert d == 217
assert c == 0x0b
print('UART_BAUD_READBACK_PASS')
end
detach
quit
