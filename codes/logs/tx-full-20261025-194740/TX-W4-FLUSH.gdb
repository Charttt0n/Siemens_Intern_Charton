set pagination off
set confirm off
set remotetimeout 3
target remote 127.0.0.1:55055
hbreak main
continue
set var uart_tx_test_full=0
hbreak src/week4/main.c:55
continue
python
import gdb
before=int(gdb.parse_and_eval('uart_tx_test_reads'))
end
set var uart_tx_test_full=1
hbreak boot_idle
continue
python
import gdb
status=int(gdb.parse_and_eval('boot_status')); delta=int(gdb.parse_and_eval('uart_tx_test_reads'))-before
print('boot_status=0x%x final_flush_reads=%d' % (status,delta))
assert status==0x101 and delta==1000000
end

python print("TX_FAILURE_PATH_PASS")
detach
quit
