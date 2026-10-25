set pagination off
set confirm off
set remotetimeout 5
target remote 127.0.0.1:55034
break main
continue
set variable initialized_data = 0xdeadbeef
python
import gdb
if int(gdb.parse_and_eval('initialized_data')) != 0xdeadbeef:
    raise gdb.GdbError('Fault injection failed')
print('INJECTED_initialized_data=0x%x' % int(gdb.parse_and_eval('initialized_data')))
end
break semihost
continue
python
if int(gdb.parse_and_eval('demo_result')) != 0xdead:
    raise gdb.GdbError('Firmware did not detect injected corruption')
print('DEMO_RESULT=0x%x' % int(gdb.parse_and_eval('demo_result')))
print('CHECK_PASS:corrupt-data')
end

detach
quit
