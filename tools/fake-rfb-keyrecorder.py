#!/usr/bin/env python3
# Minimal fake RFB 3.8 server that records KeyEvent messages (loopback only).
#
# Completes the handshake with security type None, serves a small Raw
# framebuffer and writes one JSON object per line to --log:
#   {"event":"listening|connected|disconnected|stopped","port":N}
#   {"msg":"KeyEvent","down":true,"keysym":63}
# Other client messages are parsed and ignored. It binds 127.0.0.1 only, never
# touches the registry and stops after --duration seconds, when --stop-file
# appears, or when the process is killed. The idea follows the fake server of
# tools/fake-rfb-server.ps1 (Phase 2); this file is kept separate on purpose.
import argparse
import json
import os
import socket
import struct
import threading
import time


def read_exact(sock, count):
    data = b''
    while len(data) < count:
        chunk = sock.recv(count - len(data))
        if not chunk:
            raise EOFError()
        data += chunk
    return data


class Server:
    def __init__(self, args):
        self.args = args
        self.log_lock = threading.Lock()
        self.log_file = open(args.log, 'w', encoding='utf-8', buffering=1)
        self.stop = False

    def log(self, obj):
        with self.log_lock:
            self.log_file.write(json.dumps(obj) + '\n')
            self.log_file.flush()

    def pixels(self, fmt):
        # fmt: (bpp, big_endian, rmax, gmax, bmax, rshift, gshift, bshift)
        bpp, big, rmax, gmax, bmax, rsh, gsh, bsh = fmt
        width, height = self.args.width, self.args.height
        out = bytearray()
        order = 'big' if big else 'little'
        nbytes = bpp // 8
        for y in range(height):
            for x in range(width):
                r = (x * rmax) // max(width - 1, 1)
                g = (y * gmax) // max(height - 1, 1)
                b = bmax // 2
                value = (r << rsh) | (g << gsh) | (b << bsh)
                out += value.to_bytes(nbytes, order)
        return bytes(out)

    def serve(self, sock):
        sock.settimeout(None)
        self.log({'event': 'connected'})
        fmt = (32, False, 255, 255, 255, 16, 8, 0)
        sock.sendall(b'RFB 003.008\n')
        read_exact(sock, 12)
        sock.sendall(bytes([1, 1]))          # one security type: None
        read_exact(sock, 1)
        sock.sendall(struct.pack('>I', 0))   # SecurityResult OK
        read_exact(sock, 1)                  # ClientInit
        name = self.args.name.encode('utf-8')
        sock.sendall(struct.pack('>HHBBBBHHHBBBxxx', self.args.width, self.args.height,
                                 32, 24, 0, 1, 255, 255, 255, 16, 8, 0) +
                     struct.pack('>I', len(name)) + name)
        sent_full = False
        while not self.stop:
            msg_type = read_exact(sock, 1)[0]
            if msg_type == 0:                # SetPixelFormat
                body = read_exact(sock, 19)
                bpp, _depth, big, true_color, rmax, gmax, bmax, rsh, gsh, bsh = \
                    struct.unpack('>BBBBHHHBBB', body[3:16])
                if true_color and bpp in (8, 16, 32):
                    fmt = (bpp, bool(big), rmax, gmax, bmax, rsh, gsh, bsh)
                sent_full = False
            elif msg_type == 2:              # SetEncodings
                count = struct.unpack('>xH', read_exact(sock, 3))[0]
                read_exact(sock, 4 * count)
            elif msg_type == 3:              # FramebufferUpdateRequest
                body = read_exact(sock, 9)
                incremental = body[0]
                if not incremental or not sent_full:
                    data = self.pixels(fmt)
                    sock.sendall(struct.pack('>BxHHHHHi', 0, 1, 0, 0, self.args.width,
                                             self.args.height, 0) + data)
                    sent_full = True
            elif msg_type == 4:              # KeyEvent
                down, _pad, keysym = struct.unpack('>BHI', read_exact(sock, 7))
                self.log({'msg': 'KeyEvent', 'down': bool(down), 'keysym': keysym})
            elif msg_type == 5:              # PointerEvent
                read_exact(sock, 5)
            elif msg_type == 6:              # ClientCutText
                length = struct.unpack('>xxxI', read_exact(sock, 7))[0]
                read_exact(sock, length)
            else:
                self.log({'event': 'error', 'unknown_message': msg_type})
                break

    def run(self):
        listener = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        listener.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
        listener.bind(('127.0.0.1', self.args.port))
        listener.listen(1)
        listener.settimeout(0.2)
        self.log({'event': 'listening', 'port': self.args.port})
        end = time.time() + self.args.duration if self.args.duration > 0 else None
        client = None
        try:
            while not self.stop:
                if end and time.time() > end:
                    break
                if self.args.stop_file and os.path.exists(self.args.stop_file):
                    break
                try:
                    client, _ = listener.accept()
                except socket.timeout:
                    continue
                try:
                    self.serve(client)
                except (EOFError, ConnectionError, OSError):
                    pass
                finally:
                    client.close()
                    self.log({'event': 'disconnected'})
        finally:
            listener.close()
            self.log({'event': 'stopped'})
            self.log_file.close()


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--port', type=int, default=5921)
    parser.add_argument('--width', type=int, default=320)
    parser.add_argument('--height', type=int, default=200)
    parser.add_argument('--name', default='FarSightVNC key recorder')
    parser.add_argument('--log', required=True)
    parser.add_argument('--duration', type=int, default=0)
    parser.add_argument('--stop-file')
    args = parser.parse_args()
    Server(args).run()


if __name__ == '__main__':
    main()
