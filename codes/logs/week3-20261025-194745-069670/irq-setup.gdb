set pagination off
set confirm off
target remote 127.0.0.1:55082
python
import gdb, struct
inf = gdb.selected_inferior()
def word(addr): return struct.unpack('<I', bytes(inf.read_memory(addr,4)))[0]
assert word(0x40004008) == 0xb
assert word(0x40004010) == 217
assert word(0xe000e100) & 1
assert word(64) == (int(gdb.parse_and_eval('(unsigned int)&UART0_RX_Handler')) | 1)
assert word(0) == 0x20300000
assert int(gdb.parse_and_eval('boot_status')) == 1
assert int(gdb.parse_and_eval('uart_rx_count')) == 0
end
set var test_pause_rx = 1

python print("GDB_ASSERTIONS_PASS")
detach
quit
