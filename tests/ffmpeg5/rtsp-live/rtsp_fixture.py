#!/usr/bin/env python3
"""Bounded loopback-only RTSP/RTP fixture: PCMU, optionally H264.

Every audio packet covers 20ms and every video frame covers 40ms. RTP timestamp
zero is the same monotonic origin for both tracks, so age is measurable without
wall-clock/NTP synchronization. Intended for controlled tests, never deployment.
"""
import argparse
import json
import re
import socket
import struct
import threading
import time
from pathlib import Path


class RTSPFixture:
    def __init__(self, bind_host="127.0.0.1", port=0, duration=90, video=None):
        if not bind_host.startswith("127."):
            raise ValueError("fixture may bind loopback only")
        self.host = bind_host
        self.duration = duration
        self.listener = socket.socket()
        self.listener.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
        self.listener.bind((bind_host, port))
        self.listener.listen(4)
        self.port = self.listener.getsockname()[1]
        self.url = f"rtsp://{bind_host}:{self.port}/live"
        self.stopped = threading.Event()
        self.events = []
        self.start_times = []
        self.video_frames = self._parse_video(video) if video else []

    @staticmethod
    def _parse_video(path):
        nals = [n for n in re.split(b"\x00\x00\x00?\x01", Path(path).read_bytes()) if n]
        frames, current = [], []
        for n in nals:
            if n[0] & 31 == 9 and current:
                frames.append(current)
                current = []
            current.append(n)
        if current:
            frames.append(current)
        return frames

    def sdp(self):
        result = ("v=0\r\no=- 1 1 IN IP4 127.0.0.1\r\ns=Loopback fixture\r\n"
                  "c=IN IP4 127.0.0.1\r\nt=0 0\r\na=control:*\r\n"
                  "m=audio 0 RTP/AVP 0\r\na=rtpmap:0 PCMU/8000/1\r\na=control:trackID=0\r\n")
        if self.video_frames:
            result += ("m=video 0 RTP/AVP 96\r\na=rtpmap:96 H264/90000\r\n"
                       "a=fmtp:96 packetization-mode=1\r\na=control:trackID=1\r\n")
        return result.encode()

    def response_override(self, method, uri, headers):
        """Subclass may return (status, headers, body), e.g. a redirect."""
        return None

    def start(self):
        self.thread = threading.Thread(target=self.serve, daemon=True)
        self.thread.start()
        return self

    def close(self):
        self.stopped.set()
        self.listener.close()

    def serve(self):
        self.listener.settimeout(.25)
        while not self.stopped.is_set():
            try:
                conn, peer = self.listener.accept()
            except socket.timeout:
                continue
            except OSError:
                break
            threading.Thread(target=self._client, args=(conn, peer), daemon=True).start()

    def _client(self, conn, peer):
        send_lock = threading.Lock()
        active = threading.Event()
        tracks = {}
        buf = b""
        conn.settimeout(.25)
        def send(data):
            with send_lock:
                conn.sendall(data)
        try:
            while not self.stopped.is_set():
                try:
                    data = conn.recv(8192)
                    if not data:
                        break
                    buf += data
                except socket.timeout:
                    continue
                while buf:
                    if buf[0] == 36:
                        if len(buf) < 4:
                            break
                        size = int.from_bytes(buf[2:4], "big") + 4
                        if len(buf) < size:
                            break
                        buf = buf[size:]
                        continue
                    end = buf.find(b"\r\n\r\n")
                    if end < 0:
                        break
                    raw = buf[:end].decode("ascii")
                    lines = raw.split("\r\n")
                    method, uri, _ = lines[0].split(" ", 2)
                    headers = dict((k.lower(), v.strip()) for k, v in
                                   (line.split(":", 1) for line in lines[1:]))
                    length = int(headers.get("content-length", 0))
                    if len(buf) < end + 4 + length:
                        break
                    buf = buf[end + 4 + length:]
                    self.events.append({"method": method, "uri": uri, "time": time.monotonic()})
                    extra = {"Session": "12345678"}
                    status, body = "200 OK", b""
                    override = self.response_override(method, uri, headers)
                    if override:
                        status, extra, body = override
                    elif method == "OPTIONS":
                        extra["Public"] = "OPTIONS, DESCRIBE, SETUP, PLAY, PAUSE, TEARDOWN, GET_PARAMETER"
                    elif method == "DESCRIBE":
                        body = self.sdp()
                        extra.update({"Content-Type": "application/sdp", "Content-Base": self.url + "/"})
                    elif method == "SETUP":
                        track = int(uri.rsplit("=", 1)[1])
                        transport = headers["transport"]
                        if "TCP" in transport.upper():
                            m = re.search(r"interleaved=(\d+)-(\d+)", transport)
                            channel = int(m.group(1)) if m else track * 2
                            tracks[track] = ("tcp", channel)
                            extra["Transport"] = f"RTP/AVP/TCP;unicast;interleaved={channel}-{channel+1}"
                        else:
                            m = re.search(r"client_port=(\d+)-(\d+)", transport)
                            if not m:
                                status = "461 Unsupported Transport"
                            else:
                                udp = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
                                udp.bind((self.host, 0))
                                target = (peer[0], int(m.group(1)))
                                tracks[track] = ("udp", udp, target)
                                extra["Transport"] = (f"RTP/AVP/UDP;unicast;client_port={m.group(1)}-{m.group(2)};"
                                                      f"server_port={udp.getsockname()[1]}-{udp.getsockname()[1]+1}")
                    elif method == "PLAY":
                        extra["Range"] = "npt=0.000-"
                        extra["RTP-Info"] = ",".join(f"url={self.url}/trackID={t};seq=0;rtptime=0" for t in tracks)
                    elif method == "TEARDOWN":
                        active.clear()
                    response = f"RTSP/1.0 {status}\r\nCSeq: {headers.get('cseq', '1')}\r\n"
                    for k, v in extra.items():
                        response += f"{k}: {v}\r\n"
                    response += f"Content-Length: {len(body)}\r\n\r\n"
                    send(response.encode() + body)
                    if method == "PLAY" and not override and not active.is_set():
                        active.set()
                        threading.Thread(target=self._stream, args=(tracks, send, active), daemon=True).start()
                    if method == "TEARDOWN":
                        return
        except (OSError, ValueError):
            pass
        finally:
            active.clear()
            conn.close()
            for spec in tracks.values():
                if spec[0] == "udp":
                    spec[1].close()

    def _stream(self, tracks, send, active):
        start = time.monotonic()
        self.start_times.append(start)
        seq = {0: 0, 1: 0}
        def packet(track, timestamp, payload, marker=False):
            pt = 0 if track == 0 else 96
            rtp = struct.pack("!BBHII", 0x80, pt | (0x80 if marker else 0), seq[track] & 65535,
                              timestamp & 0xffffffff, 0xABCD0000 + track) + payload
            seq[track] += 1
            spec = tracks[track]
            if spec[0] == "tcp":
                send(b"$" + bytes([spec[1]]) + struct.pack("!H", len(rtp)) + rtp)
            else:
                spec[1].sendto(rtp, spec[2])
        try:
            for tick in range(int(self.duration * 50)):
                if not active.is_set() or self.stopped.is_set():
                    return
                time.sleep(max(0, start + tick / 50 - time.monotonic()))
                if 0 in tracks:
                    packet(0, tick * 160, b"\xff" * 160, marker=tick == 0)
                if 1 in tracks and tick % 2 == 0:
                    nals = self.video_frames[(tick // 2) % len(self.video_frames)]
                    for ni, nal in enumerate(nals):
                        last_nal = ni == len(nals) - 1
                        if len(nal) <= 1200:
                            packet(1, tick * 1800, nal, last_nal)
                        else:
                            body = nal[1:]
                            for offset in range(0, len(body), 1198):
                                chunk = body[offset:offset + 1198]
                                last = offset + len(chunk) == len(body)
                                fua = bytes([(nal[0] & 0xE0) | 28, (nal[0] & 31) |
                                             (0x80 if offset == 0 else 0) | (0x40 if last else 0)])
                                packet(1, tick * 1800, fua + chunk, last and last_nal)
        except OSError:
            active.clear()


if __name__ == "__main__":
    parser = argparse.ArgumentParser()
    parser.add_argument("--host", default="127.0.0.1")
    parser.add_argument("--port", type=int, default=0)
    parser.add_argument("--duration", type=float, default=90)
    parser.add_argument("--video")
    args = parser.parse_args()
    fixture = RTSPFixture(args.host, args.port, args.duration, args.video).start()
    print(json.dumps({"url": fixture.url}), flush=True)
    try:
        time.sleep(args.duration + 10)
    finally:
        fixture.close()
