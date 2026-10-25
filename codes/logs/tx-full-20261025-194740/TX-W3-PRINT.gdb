set pagination off
set confirm off
set remotetimeout 3
target remote 127.0.0.1:55049
hbreak fault_idle
continue
python
import gdb
reads=int(gdb.parse_and_eval('uart_tx_test_reads')); data=int(gdb.parse_and_eval('uart_tx_test_data'))
print('TX_STATE_READS=%d DATA=0x%x' % (reads,data))
assert reads==1000000 and data==0x55
end
python
status=int(gdb.parse_and_eval('boot_status')); print('boot_status=0x%x' % status); assert status==3
end

python print("TX_FAILURE_PATH_PASS")
detach
quit
