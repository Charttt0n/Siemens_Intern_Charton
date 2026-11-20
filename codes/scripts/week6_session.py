"""Week-6 QEMU session. Reuses the Week-5 binary UART / YMODEM / snapshot path."""
from week5_session import (FLASH_BASE, FLASH_SIZE, PeerCancelled, Session as Week5Session,
                           sender)


class Session(Week5Session):
    def response(self, timeout=8):
        return self.read_until(b'BOOT6: command window\r\n', timeout)

    def command(self, text, expect=None, timeout=20):
        """Send a CLI line and optionally wait for an output marker."""
        payload = text if isinstance(text, bytes) else text.encode()
        if not payload.endswith(b'\r'):
            payload += b'\r'
        self.quiescent = False
        start = len(self.transcript)
        self.write(payload)
        if expect is None:
            return b''
        marker = expect if isinstance(expect, bytes) else expect.encode()
        end_deadline = __import__('time').monotonic() + timeout
        while marker not in self.transcript[start:]:
            left = end_deadline - __import__('time').monotonic()
            if left <= 0:
                raise TimeoutError(f'UART missing {marker!r}: {bytes(self.transcript[start:][-400:])!r}')
            self._recv(4096, left)
        return bytes(self.transcript[start:])
