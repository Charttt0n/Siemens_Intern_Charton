set pagination off
set confirm off
set remotetimeout 5
target remote 127.0.0.1:60028
python
import gdb, struct
inf = gdb.selected_inferior()
def addr(name):
    return int(gdb.parse_and_eval('(unsigned int)&' + name))
def word(where):
    return struct.unpack('<I', bytes(inf.read_memory(where, 4)))[0]
def require(ok, message):
    if not ok:
        raise gdb.GdbError(message)
end
break *main
continue
python
require(int(gdb.parse_and_eval('uart_putc(65)')) == -1, 'disabled TX must reject writes')
require(int(gdb.parse_and_eval('uart_init(25000000,115200)')) == 0, 'valid configuration')
require(word(0x40004010) == 217, 'rounded baud divisor')
for clk,baud in [(0,115200),(25000000,0),(25000000,2000000),(25000000,1)]:
    before = (word(0x40004008),word(0x40004010))
    require(int(gdb.parse_and_eval('uart_init(%d,%d)' % (clk,baud))) == -1, 'invalid baud accepted')
    require((word(0x40004008),word(0x40004010)) == before, 'invalid init changed registers')
    print('REJECTED: clock=%d baud=%d; registers unchanged' % (clk,baud))
require(int(gdb.parse_and_eval('uart_write((const char *)0)')) == -1, 'NULL string accepted')
print('VALID: divisor=217; DISABLED_TX/NULL rejected')
end
delete breakpoints
break boot_idle
continue
python
require(int(gdb.parse_and_eval('boot_status')) == 1, 'normal boot after parameter tests')
end

python print("CHECK_PASS:uart-parameter-boundaries")
detach
quit
