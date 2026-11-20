"""Week 7 CLI marker with the existing binary UART/QMP transport."""
from week6_session import Session as PreviousSession
class Session(PreviousSession):
    def response(self, timeout=8):
        return self.read_until(b'BOOT7: command window\r\n',timeout)
