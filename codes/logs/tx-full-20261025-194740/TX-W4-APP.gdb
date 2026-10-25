set pagination off
set confirm off
set remotetimeout 3
target remote 127.0.0.1:55058
hbreak app_idle
continue
python
import gdb
status=int(gdb.parse_and_eval('app_status')); handoff=int(gdb.parse_and_eval('app_handoff_ok'))
reads=int(gdb.parse_and_eval('uart_tx_test_reads'));data=int(gdb.parse_and_eval('uart_tx_test_data'))
print('app_status=%d handoff_ok=%d reads=%d DATA=0x%x' % (status,handoff,reads,data))
assert status==4 and handoff==1 and reads==1000000 and data==0x55
end

python print("TX_FAILURE_PATH_PASS")
detach
quit
