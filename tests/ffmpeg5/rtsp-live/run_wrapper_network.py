#!/usr/bin/env python3
"""Bounded loopback wrapper tests: effective options, SETUP transport, reconnect."""
import argparse
import subprocess
from rtsp_fixture import RTSPFixture

class ObservedFixture(RTSPFixture):
    def __init__(self, vod=False):
        super().__init__(duration=0.7)
        self.vod = vod
        self.transports = []
    def sdp(self):
        result = super().sdp()
        if self.vod:
            result = result.replace(b"a=control:*\r\n", b"a=control:*\r\na=range:npt=0-60\r\n")
        return result
    def response_override(self, method, uri, headers):
        if method == "SETUP":
            self.transports.append(headers["transport"])
        return None

def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("binary")
    args = parser.parse_args()
    for transport, vod in (("tcp", False), ("udp", False), ("tcp", True)):
        fixture = ObservedFixture(vod).start()
        try:
            result = subprocess.run([args.binary, fixture.url, transport, str(int(vod))],
                                    text=True, capture_output=True, timeout=12)
            print(result.stdout, end="", flush=True)
            if result.returncode:
                raise RuntimeError(result.stderr)
            assert len(fixture.transports) >= 2, fixture.transports
            assert all(("TCP" in header.upper()) == (transport == "tcp")
                       for header in fixture.transports), fixture.transports
            print(f"PASS outgoing SETUP {transport}, {len(fixture.transports)} opens, VOD={vod}", flush=True)
        finally:
            fixture.close()

if __name__ == "__main__":
    main()
