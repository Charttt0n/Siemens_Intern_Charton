set pagination off
set confirm off
target remote 127.0.0.1:52812
python
import gdb
inf = gdb.selected_inferior()
assert int(gdb.parse_and_eval('uart_rx_count')) == 200
assert int(gdb.parse_and_eval('uart_rx_dropped')) == 73
assert int(gdb.parse_and_eval('uart_rx_overruns')) == 0
assert bytes(inf.read_memory(int(gdb.parse_and_eval('(unsigned int)&rx')), 127)) == bytes(range(127))
end
set var tail = head
set var test_pause_rx = 0

python print("GDB_ASSERTIONS_PASS")
detach
quit
