#!/usr/bin/env python3
"""Fake RFB server for viewer render and latency benchmarks (FarSightVNC).

Listens on 127.0.0.1 only, speaks RFB 3.8 (3.3 and 3.7 clients also work) with
security type None and serves an animated framebuffer using Raw encoding only.
Raw keeps the server trivial and puts the whole load on the viewer's
receive, copy, scale and paint path, which is what the rendering work changes.

Content: a scrolling colour pattern plus a 64x64 marker square in the top-left
corner whose colour changes every --marker-interval milliseconds. The server
stamps (perf_counter_ns) the moment it starts sending the first update that
carries a new marker colour, so a probe on the same machine can measure
send -> visible latency. PointerEvent and KeyEvent arrivals are stamped by a
dedicated reader thread, so a slow update send never delays the stamp.

The measurement window starts --warmup seconds after the first
FramebufferUpdateRequest and lasts --duration seconds; then the server writes
the JSON report (--out) and exits. Ctrl+C or Ctrl+Break also write the report.

Timestamps are time.perf_counter_ns(), which is QueryPerformanceCounter on
Windows and therefore comparable between processes on the same machine.

Example:
  python tools/fake-rfb-server.py --port 5931 --size 3840x2160 --duration 15 --out build-logs/server.json
"""
import argparse
import json
import random
import signal
import socket
import statistics
import struct
import sys
import threading
import time

MARKER_SIZE = 64
MARKER_COLORS = [(255, 0, 0), (0, 255, 0), (0, 0, 255), (255, 255, 255), (255, 255, 0), (0, 0, 0)]
ENC_RAW = 0


class PixelFormat:
    """True-colour pixel format of 8, 16 or 32 bits per pixel."""

    def __init__(self, bpp, depth, big_endian, red_max, green_max, blue_max, red_shift, green_shift, blue_shift):
        self.bpp, self.depth, self.big_endian = bpp, depth, big_endian
        self.max = (red_max, green_max, blue_max)
        self.shift = (red_shift, green_shift, blue_shift)

    @classmethod
    def default(cls):
        return cls(32, 24, 0, 255, 255, 255, 16, 8, 0)

    @classmethod
    def parse(cls, data):
        bpp, depth, big_endian, true_color, rmax, gmax, bmax, rsh, gsh, bsh = struct.unpack('>BBBBHHHBBB3x', data)
        if not true_color or bpp not in (8, 16, 32):
            raise ValueError('unsupported pixel format: bpp=%d true_colour=%d' % (bpp, true_color))
        return cls(bpp, depth, big_endian, rmax, gmax, bmax, rsh, gsh, bsh)

    def pack(self):
        return struct.pack('>BBBBHHHBBB3x', self.bpp, self.depth, self.big_endian, 1, *self.max, *self.shift)

    def pixel(self, red, green, blue):
        value = 0
        for channel, maximum, shift in zip((red, green, blue), self.max, self.shift):
            value |= (channel * maximum // 255) << shift
        return value.to_bytes(self.bpp // 8, 'big' if self.big_endian else 'little')


class Framebuffer:
    """Scrolling pattern for one pixel format; frame n is a slice of a doubled image, so it costs no copy."""

    def __init__(self, width, height, pixel_format):
        self.width, self.height, self.pf = width, height, pixel_format
        self.bytes_per_pixel = pixel_format.bpp // 8
        self.stride = width * self.bytes_per_pixel
        # One gradient row, doubled so every row of the image is a shifted slice of it.
        gradient = b''.join(pixel_format.pixel(x * 255 // width, (x * 7) % 256, 255 - x * 255 // width)
                            for x in range(width))
        doubled = gradient * 2
        rows = []
        for y in range(height):
            shift = (y * 3 % width) * self.bytes_per_pixel
            rows.append(doubled[shift:shift + self.stride])
        image = b''.join(rows)
        self.image = memoryview(image + image)

    def frame(self, step):
        """Whole frame for scroll step `step` (contiguous, Raw order)."""
        offset = (step * 8 % self.height) * self.stride
        return self.image[offset:offset + self.height * self.stride]

    def rows(self, frame, y, count):
        return frame[y * self.stride:(y + count) * self.stride]

    def rect(self, frame, x, y, width, height):
        """Raw data of a sub-rectangle (rows gathered because it is not contiguous)."""
        start = x * self.bytes_per_pixel
        end = (x + width) * self.bytes_per_pixel
        return b''.join(frame[(y + r) * self.stride + start:(y + r) * self.stride + end] for r in range(height))

    def marker_strip(self, frame, color):
        """First MARKER_SIZE rows of the frame with the marker square painted in."""
        strip = bytearray(self.rows(frame, 0, MARKER_SIZE))
        marker_row = self.pf.pixel(*color) * MARKER_SIZE
        for r in range(MARKER_SIZE):
            strip[r * self.stride:r * self.stride + len(marker_row)] = marker_row
        return strip

    def marker_rect(self, color):
        return self.pf.pixel(*color) * (MARKER_SIZE * MARKER_SIZE)


def recv_exact(sock, count):
    chunks = []
    while count:
        chunk = sock.recv(min(count, 1 << 20))
        if not chunk:
            raise ConnectionError('client closed the connection')
        chunks.append(chunk)
        count -= len(chunk)
    return b''.join(chunks)


class Server:
    def __init__(self, args):
        self.args = args
        self.width, self.height = args.width, args.height
        self.lock = threading.Lock()
        self.done = threading.Event()
        self.t_first_request = None
        self.window_start = None
        self.window_end = None
        self.requests = []      # arrival timestamps of FramebufferUpdateRequest
        self.updates = []       # (send start ns, send end ns, rects, bytes)
        self.pointer = []       # (ns, x, y, buttons)
        self.keys = []          # (ns, down, keysym)
        self.markers = []       # (ns, color index, (r, g, b))
        self.pixel_formats = []
        self.connections = 0
        self.marker_index = 0
        self.marker_changed_ns = time.perf_counter_ns()
        self.marker_stamped = True
        self.rng = random.Random(1)

    # ---- connection handling -------------------------------------------------
    def serve(self):
        listener = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        listener.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
        listener.bind(('127.0.0.1', self.args.port))   # loopback only, never 0.0.0.0
        listener.listen(2)
        listener.settimeout(0.2)
        print('LISTENING 127.0.0.1:%d %dx%d' % (self.args.port, self.width, self.height), flush=True)
        started = time.monotonic()
        while not self.done.is_set():
            if self.window_end and time.perf_counter_ns() >= self.window_end:
                break
            if not self.t_first_request and time.monotonic() - started > self.args.idle_timeout:
                print('no client request within %d s' % self.args.idle_timeout, flush=True)
                break
            try:
                client, _ = listener.accept()
            except socket.timeout:
                continue
            self.connections += 1
            threading.Thread(target=self.handle, args=(client,), daemon=True).start()
        listener.close()
        self.write_report()

    def handle(self, sock):
        sock.setsockopt(socket.IPPROTO_TCP, socket.TCP_NODELAY, 1)
        try:
            self.handshake(sock)
        except (ConnectionError, OSError, ValueError) as error:
            print('handshake failed: %s' % error, flush=True)
            sock.close()
            return
        state = {'pf': PixelFormat.default(), 'fb': None, 'pending': threading.Event(), 'closed': False,
                 'request': (0, 0, self.width, self.height, False)}
        state['fb'] = Framebuffer(self.width, self.height, state['pf'])
        sender = threading.Thread(target=self.send_loop, args=(sock, state), daemon=True)
        sender.start()
        try:
            self.read_loop(sock, state)
        except (ConnectionError, OSError, ValueError) as error:
            print('client ended: %s' % error, flush=True)
        state['closed'] = True
        state['pending'].set()
        sock.close()

    def handshake(self, sock):
        sock.sendall(b'RFB 003.008\n')
        version = recv_exact(sock, 12)
        if version[:4] != b'RFB ':
            raise ValueError('not an RFB client')
        minor = int(version[8:11])
        if minor >= 7:
            sock.sendall(bytes([1, 1]))               # one security type: None
            if recv_exact(sock, 1) != b'\x01':
                raise ValueError('client rejected security type None')
            if minor >= 8:
                sock.sendall(struct.pack('>I', 0))    # SecurityResult OK
        else:
            sock.sendall(struct.pack('>I', 1))        # RFB 3.3: server picks None
        recv_exact(sock, 1)                           # ClientInit (shared flag)
        name = b'FarSightVNC fake RFB server'
        sock.sendall(struct.pack('>HH', self.width, self.height) + PixelFormat.default().pack()
                     + struct.pack('>I', len(name)) + name)

    def read_loop(self, sock, state):
        while not self.done.is_set():
            kind = recv_exact(sock, 1)[0]
            now = time.perf_counter_ns()
            if kind == 0:                                           # SetPixelFormat
                pixel_format = PixelFormat.parse(recv_exact(sock, 19)[3:])
                state['fb'] = Framebuffer(self.width, self.height, pixel_format)
                state['pf'] = pixel_format
                self.pixel_formats.append(pixel_format.pack().hex())
            elif kind == 2:                                         # SetEncodings
                count = struct.unpack('>xH', recv_exact(sock, 3))[0]
                recv_exact(sock, 4 * count)
            elif kind == 3:                                         # FramebufferUpdateRequest
                incremental, x, y, w, h = struct.unpack('>BHHHH', recv_exact(sock, 9))
                self.on_request(now, state, (x, y, w, h, bool(incremental)))
            elif kind == 4:                                         # KeyEvent
                down, keysym = struct.unpack('>B2xI', recv_exact(sock, 7))
                self.keys.append((now, down, keysym))
            elif kind == 5:                                         # PointerEvent
                buttons, x, y = struct.unpack('>BHH', recv_exact(sock, 5))
                self.pointer.append((now, x, y, buttons))
            elif kind == 6:                                         # ClientCutText
                length = struct.unpack('>3xI', recv_exact(sock, 7))[0]
                recv_exact(sock, length)
            else:
                raise ValueError('unknown client message %d' % kind)

    def on_request(self, now, state, request):
        with self.lock:
            if self.t_first_request is None:
                self.t_first_request = now
                self.window_start = now + int(self.args.warmup * 1e9)
                self.window_end = self.window_start + int(self.args.duration * 1e9)
                print('ACTIVE', flush=True)
            self.requests.append(now)
        state['request'] = request
        state['pending'].set()

    # ---- update generation ---------------------------------------------------
    def current_marker(self, now):
        """Advance the marker colour on schedule; return (index, color, newly_changed)."""
        interval = int(self.args.marker_interval * 1e6)
        if interval and now - self.marker_changed_ns >= interval:
            self.marker_index = (self.marker_index + 1) % len(MARKER_COLORS)
            self.marker_changed_ns = now
            self.marker_stamped = False
        changed = not self.marker_stamped
        self.marker_stamped = True
        return self.marker_index, MARKER_COLORS[self.marker_index], changed

    def build_update(self, state, step, color):
        """Return (list of buffers, rect count) for one incremental update."""
        fb = state['fb']
        frame = fb.frame(step)
        buffers, rects = [], 0

        def add_rect(x, y, w, h, data):
            nonlocal rects
            buffers.append(struct.pack('>HHHHi', x, y, w, h, ENC_RAW))
            buffers.append(data)
            rects += 1

        if self.args.rects == 0:
            add_rect(0, 0, fb.width, MARKER_SIZE, fb.marker_strip(frame, color))
            add_rect(0, MARKER_SIZE, fb.width, fb.height - MARKER_SIZE, fb.rows(frame, MARKER_SIZE, fb.height - MARKER_SIZE))
        else:
            add_rect(0, 0, MARKER_SIZE, MARKER_SIZE, fb.marker_rect(color))
            size = self.args.rect_size
            for _ in range(self.args.rects):
                w, h = min(size, fb.width), min(size, fb.height)
                x = self.rng.randrange(0, fb.width - w + 1)
                y = self.rng.randrange(MARKER_SIZE, max(MARKER_SIZE + 1, fb.height - h + 1))
                add_rect(x, y, w, h, fb.rect(frame, x, y, w, h))
        return buffers, rects

    def send_loop(self, sock, state):
        step = 0
        interval_ns = int(1e9 / self.args.max_fps) if self.args.max_fps else 0
        next_slot = time.perf_counter_ns()
        try:
            while not self.done.is_set() and not state['closed']:
                state['pending'].wait(0.2)
                if state['closed']:
                    break
                if not state['pending'].is_set():
                    continue
                state['pending'].clear()
                if interval_ns:
                    wait = next_slot - time.perf_counter_ns()
                    if wait > 0:
                        time.sleep(wait / 1e9)
                    next_slot = max(next_slot, time.perf_counter_ns() - interval_ns) + interval_ns
                x, y, w, h, incremental = state['request']
                start = time.perf_counter_ns()
                if incremental:
                    index, color, changed = self.current_marker(start)
                    step += 1
                    buffers, rects = self.build_update(state, step, color)
                else:
                    # Full refresh of the requested area, as one Raw rectangle.
                    fb = state['fb']
                    changed = False
                    x, y = min(x, fb.width), min(y, fb.height)
                    w, h = min(w, fb.width - x), min(h, fb.height - y)
                    buffers = [struct.pack('>HHHHi', x, y, w, h, ENC_RAW), fb.rect(fb.frame(step), x, y, w, h)]
                    rects = 1
                header = struct.pack('>BxH', 0, rects)
                if changed:
                    self.markers.append((start, index, color))
                total = len(header) + sum(len(b) for b in buffers)
                sock.sendall(header)
                for buffer in buffers:
                    sock.sendall(buffer)
                self.updates.append((start, time.perf_counter_ns(), rects, total))
        except (ConnectionError, OSError):
            pass

    # ---- report --------------------------------------------------------------
    def write_report(self):
        start = self.window_start or 0
        end = self.window_end or time.perf_counter_ns()
        inside = lambda t: start <= t <= end
        updates = [u for u in self.updates if inside(u[0])]
        requests = [t for t in self.requests if inside(t)]
        seconds = (end - start) / 1e9 if self.window_start else 0
        gaps = [(b[0] - a[0]) / 1e6 for a, b in zip(updates, updates[1:])]
        gaps.sort()
        report = {
            'args': vars(self.args),
            'pixel_formats': self.pixel_formats,
            'connections': self.connections,
            'window_start_ns': start, 'window_end_ns': end, 'window_seconds': round(seconds, 3),
            'requests_total': len(self.requests), 'updates_total': len(self.updates),
            'requests_in_window': len(requests), 'updates_in_window': len(updates),
            'effective_fps': round(len(updates) / seconds, 2) if seconds else 0,
            'update_interval_ms_median': round(statistics.median(gaps), 2) if gaps else None,
            'update_interval_ms_p95': round(gaps[int(0.95 * (len(gaps) - 1))], 2) if gaps else None,
            'bytes_in_window': sum(u[3] for u in updates),
            'update_starts_ns': [u[0] for u in self.updates],
            'pointer_events': [{'ns': t, 'x': x, 'y': y, 'buttons': b} for t, x, y, b in self.pointer],
            'key_events': [{'ns': t, 'down': d, 'keysym': k} for t, d, k in self.keys],
            'marker_events': [{'ns': t, 'index': i, 'rgb': list(c)} for t, i, c in self.markers],
        }
        with open(self.args.out, 'w', encoding='utf-8') as handle:
            json.dump(report, handle)
        print('REPORT %s fps=%s updates=%d' % (self.args.out, report['effective_fps'], len(updates)), flush=True)


def parse_args():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument('--port', type=int, default=5931, help='TCP port on 127.0.0.1 (default 5931)')
    parser.add_argument('--size', default='3840x2160', help='framebuffer WxH (default 3840x2160)')
    parser.add_argument('--rects', type=int, default=0,
                        help='0 = full-frame updates; N = N small random rectangles per update')
    parser.add_argument('--rect-size', type=int, default=256, help='side of each rectangle with --rects')
    parser.add_argument('--max-fps', type=float, default=0, help='cap on updates per second (0 = as fast as requested)')
    parser.add_argument('--marker-interval', type=float, default=500, help='ms between marker colour changes (0 = off)')
    parser.add_argument('--warmup', type=float, default=3, help='seconds between first request and window start')
    parser.add_argument('--duration', type=float, default=15, help='measurement window length in seconds')
    parser.add_argument('--idle-timeout', type=float, default=60, help='exit if no client requests an update')
    parser.add_argument('--out', default='fake-rfb-server.json', help='JSON report path')
    args = parser.parse_args()
    try:
        args.width, args.height = (int(v) for v in args.size.lower().split('x'))
    except ValueError:
        parser.error('--size must look like 3840x2160')
    if args.width < MARKER_SIZE or args.height <= 2 * MARKER_SIZE:
        parser.error('framebuffer too small')
    return args


def main():
    server = Server(parse_args())

    def stop(*_):
        server.done.set()
    signal.signal(signal.SIGINT, stop)
    if hasattr(signal, 'SIGBREAK'):
        signal.signal(signal.SIGBREAK, stop)
    server.serve()


if __name__ == '__main__':
    sys.exit(main())
