set pagination off
set confirm off
set remotetimeout 5
target remote 127.0.0.1:60022
break main
continue
set variable zero_data = 0xdeadbeef
python
import gdb
if int(gdb.parse_and_eval('zero_data')) != 0xdeadbeef:
    raise gdb.GdbError('Fault injection failed')
print('INJECTED_zero_data=0x%x' % int(gdb.parse_and_eval('zero_data')))
end
break semihost
continue
python
if int(gdb.parse_and_eval('demo_result')) != 0xdead:
    raise gdb.GdbError('Firmware did not detect injected corruption')
print('DEMO_RESULT=0x%x' % int(gdb.parse_and_eval('demo_result')))
print('CHECK_PASS:corrupt-bss')
end

detach
quit
