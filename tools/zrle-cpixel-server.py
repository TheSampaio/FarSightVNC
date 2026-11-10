#!/usr/bin/env python3
# Copyright (C) 2026 FarSightVNC contributors. GPL-2.0-or-later.
"""Fake RFB server for the ZRLE CPIXEL layouts of the FarSightVNC viewer.

Listens on 127.0.0.1 only, completes an RFB 3.8 handshake with security type
None, announces a 32 bpp true colour format in ServerInit (see --case) and
sends one ZRLE FramebufferUpdate that covers the whole 200x200 frame buffer.
The tiles are solid, packed palette (2, 4 and 16 colours), plain RLE, palette
RLE and raw, with colours that are known, so the result can be checked.

CPIXEL (RFC 6143 section 7.7.5): a 32 bpp true colour pixel whose colour bits
all fit in the 3 least or the 3 most significant bytes is sent as 3 bytes (the
empty byte is dropped). The cases:

  shifted   shifts 24/16/8 (empty least significant byte; the CPIXEL is bytes
            1..3 of the pixel in memory) - the layout of ZrleDecoder's
            "m_numberFirstByte = 1" case
  noshift   shifts 16/8/0 (empty most significant byte; the viewer's own
            standard 32 bpp format)
  bgr       shifts 0/8/16 (empty most significant byte, channels swapped)
  cpixel4   shifts 24/8/0 (no empty byte: 4 byte CPIXEL)

--phase after   waits for the viewer's first FramebufferUpdateRequest (the
                viewer sends its SetPixelFormat before it) and encodes in the
                format set by the client: what a conforming server does.
--phase before  sends the update right after ServerInit, in the server's own
                format, before the client's SetPixelFormat can have been read:
                what a server that is early or that ignores SetPixelFormat
                does. The viewer decodes with its own format.

One connection is served. It prints "LISTENING 127.0.0.1:<port> ..." when ready
and one "DONE ..." line.

Viewer driver (Windows): --run-viewer <fsvncviewer.exe> starts, for every case
and phase, a server on its own port and the viewer against it, then classifies
the run as CRASH (exception exit code, CRT dialog), ERROR (handled error
dialog) or OK, and compares the rendered colours: the viewer window is moved to
a fixed topmost position and the screen pixels are read with GetPixel, so do
not use the machine while it runs. Nothing is injected into the desktop.

Examples:
  python -I tools/zrle-cpixel-server.py --port 5985 --case shifted --phase after
  python -I tools/zrle-cpixel-server.py --run-viewer build\\bin\\x64\\Debug\\fsvncviewer.exe
  python -I tools/zrle-cpixel-server.py --run-viewer build\\bin\\x64\\Debug\\fsvncviewer.exe --use-8bit
"""
import argparse
import ctypes
import os
import socket
import struct
import subprocess
import sys
import tempfile
import threading
import time
import zlib

FB_W, FB_H = 200, 200
TILE = 64
ZRLE = 16

# 16 well separated colours (channels 0/85/170/255); quantisation to 3/3/2 bits
# (viewer --use-8bit) keeps them apart.
PAL16 = [(r, g, b) for r in (0, 255) for g in (0, 255) for b in (0, 255)] + \
        [(85, 170, 0), (170, 85, 255), (0, 85, 170), (255, 170, 85),
         (170, 0, 85), (85, 255, 170), (255, 85, 0), (0, 170, 255)]


class PixelFormat:
    def __init__(self, bpp, depth, big_endian, maxes, shifts):
        self.bpp, self.depth, self.big_endian = bpp, depth, big_endian
        self.max, self.shift = tuple(maxes), tuple(shifts)

    @classmethod
    def parse(cls, data):
        bpp, depth, big, _true, rmax, gmax, bmax, rsh, gsh, bsh = struct.unpack('>BBBBHHHBBB3x', data)
        return cls(bpp, depth, big, (rmax, gmax, bmax), (rsh, gsh, bsh))

    def pack(self):
        return struct.pack('>BBBBHHHBBB3x', self.bpp, self.depth, self.big_endian, 1, *self.max, *self.shift)

    @property
    def bypp(self):
        return self.bpp // 8

    def value(self, rgb):
        v = 0
        for c, m, s in zip(rgb, self.max, self.shift):
            v |= (c * m // 255) << s
        return v & ((1 << self.bpp) - 1)

    def pixel(self, rgb):
        """The pixel as it is in memory / in a PIXEL on the wire."""
        return self.value(rgb).to_bytes(self.bypp, 'big' if self.big_endian else 'little')

    def cpixel_size(self):
        if self.bpp != 32:
            return self.bypp
        cmv = 0
        for m, s in zip(self.max, self.shift):
            cmv |= (m << s)
        cmv &= 0xFFFFFFFF
        return 3 if (cmv & 0xFF000000 == 0 or cmv & 0xFF == 0) else 4

    def cpixel(self, rgb):
        """CPIXEL written from the spec: the PIXEL without its empty byte."""
        data = self.pixel(rgb)
        if self.bpp != 32:
            return data
        cmv = 0
        for m, s in zip(self.max, self.shift):
            cmv |= (m << s)
        cmv &= 0xFFFFFFFF
        if cmv & 0xFF000000 == 0:
            drop_value_byte = 3
        elif cmv & 0xFF == 0:
            drop_value_byte = 0
        else:
            return data
        drop_in_memory = 3 - drop_value_byte if self.big_endian else drop_value_byte
        return data[:drop_in_memory] + data[drop_in_memory + 1:]

    def rgb_of(self, value):
        return tuple(((value >> s) & m) * 255 // m for m, s in zip(self.max, self.shift))

    def describe(self):
        return '%d/%d%s/%s' % (self.bpp, self.depth, 'be' if self.big_endian else '',
                               ','.join(str(s) for s in self.shift))


CASES = {
    'shifted': PixelFormat(32, 24, 0, (255, 255, 255), (24, 16, 8)),
    'noshift': PixelFormat(32, 24, 0, (255, 255, 255), (16, 8, 0)),
    'bgr': PixelFormat(32, 24, 0, (255, 255, 255), (0, 8, 16)),
    'cpixel4': PixelFormat(32, 32, 0, (255, 255, 255), (24, 8, 0)),
}
# The viewer's standard formats (StandardPixelFormatFactory, little endian host).
VIEWER_32 = PixelFormat(32, 24, 0, (255, 255, 255), (16, 8, 0))


def run_length(n):
    n -= 1
    out = bytearray()
    while n >= 255:
        out.append(255)
        n -= 255
    out.append(n)
    return bytes(out)


KINDS = ('solid', 'pack2', 'pack4', 'pack16', 'rle', 'palrle', 'raw')


def make_tile(kind, tw, th, number, pf):
    """Returns (zrle bytes, tile image as row-major list of rgb)."""
    cp = pf.cpixel
    n = tw * th
    img = [(0, 0, 0)] * n
    out = bytearray()
    if kind == 'solid':
        color = PAL16[number % 16]
        out.append(1)
        out += cp(color)
        img = [color] * n
    elif kind in ('pack2', 'pack4', 'pack16'):
        size = int(kind[4:])
        pal = [PAL16[(number + i) % 16] for i in range(size)]
        out.append(size)
        for c in pal:
            out += cp(c)
        bits = 1 if size == 2 else (2 if size <= 4 else 4)
        for y in range(th):
            acc, used = 0, 0
            for x in range(tw):
                idx = ((x // 4) + (y // 16)) % size
                img[y * tw + x] = pal[idx]
                acc = (acc << bits) | idx
                used += bits
                if used == 8:
                    out.append(acc)
                    acc, used = 0, 0
            if used:
                out.append(acc << (8 - used))
    elif kind == 'rle':
        out.append(128)
        runs = [1, 100, 3, 700, 2, 255, 256, 511, 4096]
        i, r = 0, 0
        while i < n:
            run = min(runs[r % len(runs)], n - i)
            color = PAL16[(number + r) % 16]
            out += cp(color) + run_length(run)
            img[i:i + run] = [color] * run
            i += run
            r += 1
    elif kind == 'palrle':
        size = 5
        pal = [PAL16[(number + 3 * i) % 16] for i in range(size)]
        out.append(128 + size)
        for c in pal:
            out += cp(c)
        runs = [1, 70, 1, 1, 300, 2, 129, 1, 1000]
        i, r = 0, 0
        while i < n:
            run = min(runs[r % len(runs)], n - i)
            idx = r % size
            if run == 1:
                out.append(idx)
            else:
                out.append(idx | 128)
                out += run_length(run)
            img[i:i + run] = [pal[idx]] * run
            i += run
            r += 1
    elif kind == 'raw':
        out.append(0)
        for y in range(th):
            for x in range(tw):
                color = PAL16[((x // 4) + (y // 4) + number) % 16]
                img[y * tw + x] = color
                out += cp(color)
    else:
        raise ValueError(kind)
    return bytes(out), img


def build_update(pf):
    """ZRLE data (uncompressed) for the whole frame buffer and the expected image."""
    data = bytearray()
    image = [[(0, 0, 0)] * FB_W for _ in range(FB_H)]
    number = 0
    for y in range(0, FB_H, TILE):
        for x in range(0, FB_W, TILE):
            tw, th = min(TILE, FB_W - x), min(TILE, FB_H - y)
            tile, img = make_tile(KINDS[number % len(KINDS)], tw, th, number, pf)
            data += tile
            for ty in range(th):
                image[y + ty][x:x + tw] = img[ty * tw:(ty + 1) * tw]
            number += 1
    return bytes(data), image


def interpret(pf_server, pf_viewer, image):
    """The image a viewer with pf_viewer (a 3 byte little endian CPIXEL format
    with the empty byte last, i.e. the viewer's standard 32 bpp) shows when it
    reads CPIXELs sent in pf_server's layout. None when the CPIXEL sizes differ
    (the stream is read out of step)."""
    if pf_server.cpixel_size() != pf_viewer.cpixel_size() or pf_viewer.cpixel_size() != 3:
        return None
    out = []
    for row in image:
        line = []
        for rgb in row:
            value = int.from_bytes(pf_server.cpixel(rgb) + bytes(1), 'little')
            line.append(pf_viewer.rgb_of(value))
        out.append(line)
    return out


def zrle_message(pf, zstream):
    data, image = build_update(pf)
    packed = zstream.compress(data) + zstream.flush(zlib.Z_SYNC_FLUSH)
    header = struct.pack('>BxH', 0, 1) + struct.pack('>HHHHi', 0, 0, FB_W, FB_H, ZRLE)
    return header + struct.pack('>I', len(packed)) + packed, image


def recv_exact(sock, count):
    chunks = []
    while count:
        chunk = sock.recv(min(count, 1 << 16))
        if not chunk:
            raise ConnectionError('client closed the connection')
        chunks.append(chunk)
        count -= len(chunk)
    return b''.join(chunks)


class Server:
    def __init__(self, port, case, phase, duration=25.0):
        self.port, self.case, self.phase, self.duration = port, case, phase, duration
        self.server_pf = CASES[case]
        self.client_pf = None
        self.image = None             # image the update encodes (before interpretation)
        self.sent_pf = None
        self.requests = threading.Semaphore(0)
        self.closed = threading.Event()
        self.sent = threading.Event()
        self.ready = threading.Event()
        self.result = 'not started'
        self.listener = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        self.listener.bind(('127.0.0.1', port))     # loopback only, never 0.0.0.0
        self.listener.listen(1)
        self.listener.settimeout(0.2)

    def read_loop(self, sock):
        try:
            while True:
                kind = recv_exact(sock, 1)[0]
                if kind == 0:
                    self.client_pf = PixelFormat.parse(recv_exact(sock, 19)[3:])
                elif kind == 2:
                    count = struct.unpack('>xH', recv_exact(sock, 3))[0]
                    recv_exact(sock, 4 * count)
                elif kind == 3:
                    recv_exact(sock, 9)
                    self.requests.release()
                elif kind == 4:
                    recv_exact(sock, 7)
                elif kind == 5:
                    recv_exact(sock, 5)
                elif kind == 6:
                    recv_exact(sock, struct.unpack('>3xI', recv_exact(sock, 7))[0])
                else:
                    break
        except (ConnectionError, OSError, ValueError, struct.error):
            pass
        self.closed.set()

    def serve(self):
        deadline = time.monotonic() + self.duration
        sock = None
        self.ready.set()
        while sock is None and time.monotonic() < deadline:
            try:
                sock, _ = self.listener.accept()
            except socket.timeout:
                pass
        self.listener.close()
        if sock is None:
            self.result = 'no client'
            return
        sock.setsockopt(socket.IPPROTO_TCP, socket.TCP_NODELAY, 1)
        try:
            sock.sendall(b'RFB 003.008\n')
            minor = int(recv_exact(sock, 12)[8:11])
            if minor >= 7:
                sock.sendall(bytes([1, 1]))
                recv_exact(sock, 1)
                if minor >= 8:
                    sock.sendall(struct.pack('>I', 0))
            else:
                sock.sendall(struct.pack('>I', 1))
            recv_exact(sock, 1)                     # ClientInit
            name = b'FarSightVNC ZRLE CPIXEL server'
            sock.sendall(struct.pack('>HH', FB_W, FB_H) + self.server_pf.pack() +
                         struct.pack('>I', len(name)) + name)
            threading.Thread(target=self.read_loop, args=(sock,), daemon=True).start()
            zstream = zlib.compressobj(6)
            if self.phase == 'before':
                # Before anything the client sends can have been read.
                message, self.image = zrle_message(self.server_pf, zstream)
                self.sent_pf = self.server_pf
                sock.sendall(message)
                self.sent.set()
            self.requests.acquire(timeout=5.0)      # first FramebufferUpdateRequest
            if self.phase == 'after':
                pf = self.client_pf or self.server_pf
                message, self.image = zrle_message(pf, zstream)
                self.sent_pf = pf
                sock.sendall(message)
                self.sent.set()
            while not self.closed.is_set() and time.monotonic() < deadline:
                self.closed.wait(0.2)
            self.result = 'client closed' if self.closed.is_set() else 'deadline'
        except (ConnectionError, OSError, ValueError) as error:
            self.result = 'socket: %s' % error.__class__.__name__
        finally:
            try:
                sock.shutdown(socket.SHUT_RDWR)
            except OSError:
                pass
            sock.close()


# ---------------------------------------------------------------- viewer driver

def run_driver(args):
    from ctypes import wintypes
    import winreg

    user32 = ctypes.WinDLL('user32', use_last_error=True)
    gdi32 = ctypes.WinDLL('gdi32', use_last_error=True)
    kernel32 = ctypes.WinDLL('kernel32', use_last_error=True)
    try:
        ctypes.WinDLL('shcore').SetProcessDpiAwareness(2)   # physical pixels
    except (OSError, AttributeError):
        pass
    kernel32.SetErrorMode(0x0003)                  # inherited: no WER / critical-error boxes
    wndenum = ctypes.WINFUNCTYPE(wintypes.BOOL, wintypes.HWND, wintypes.LPARAM)
    user32.GetClassNameW.argtypes = [wintypes.HWND, wintypes.LPWSTR, ctypes.c_int]
    user32.GetWindowTextW.argtypes = [wintypes.HWND, wintypes.LPWSTR, ctypes.c_int]
    user32.GetWindowThreadProcessId.argtypes = [wintypes.HWND, ctypes.POINTER(wintypes.DWORD)]
    user32.EnumWindows.argtypes = [wndenum, wintypes.LPARAM]
    user32.EnumChildWindows.argtypes = [wintypes.HWND, wndenum, wintypes.LPARAM]
    user32.IsWindowVisible.argtypes = [wintypes.HWND]
    user32.ShowWindow.argtypes = [wintypes.HWND, ctypes.c_int]
    user32.SetWindowPos.argtypes = [wintypes.HWND, wintypes.HWND] + [ctypes.c_int] * 4 + [wintypes.UINT]
    user32.GetClientRect.argtypes = [wintypes.HWND, ctypes.POINTER(wintypes.RECT)]
    user32.ClientToScreen.argtypes = [wintypes.HWND, ctypes.POINTER(wintypes.POINT)]
    user32.PostMessageW.argtypes = [wintypes.HWND, wintypes.UINT, wintypes.WPARAM, wintypes.LPARAM]
    user32.GetDC.argtypes = [wintypes.HWND]
    user32.GetDC.restype = wintypes.HDC
    user32.ReleaseDC.argtypes = [wintypes.HWND, wintypes.HDC]
    gdi32.GetPixel.argtypes = [wintypes.HDC, ctypes.c_int, ctypes.c_int]
    gdi32.GetPixel.restype = wintypes.DWORD

    def text(fn, hwnd):
        buffer = ctypes.create_unicode_buffer(512)
        fn(hwnd, buffer, 512)
        return buffer.value

    def top_windows(pid):
        found = []

        def on_top(hwnd, _):
            owner = wintypes.DWORD()
            user32.GetWindowThreadProcessId(hwnd, ctypes.byref(owner))
            if owner.value == pid and user32.IsWindowVisible(hwnd):
                found.append(hwnd)
            return True
        user32.EnumWindows(wndenum(on_top), 0)
        return found

    def dialogs(pid):
        result = []
        for hwnd in top_windows(pid):
            if text(user32.GetClassNameW, hwnd) != '#32770':
                continue
            parts = [text(user32.GetWindowTextW, hwnd)]

            def on_child(child, _):
                t = text(user32.GetWindowTextW, child)
                if t:
                    parts.append(t)
                return True
            user32.EnumChildWindows(hwnd, wndenum(on_child), 0)
            result.append((hwnd, ' | '.join(parts).replace('\r', ' ').replace('\n', ' ')))
        return result

    def session_windows(pid):
        for top in top_windows(pid):
            if text(user32.GetClassNameW, top) != 'TvnWindowClass':
                continue
            children = []

            def on_child(child, _):
                if text(user32.GetClassNameW, child) == 'TvnWindowClass':
                    children.append(child)
                return True
            user32.EnumChildWindows(top, wndenum(on_child), 0)
            if children:
                return top, children[0]
        return None

    crash_pattern = ('debug assertion', 'debug error', 'run-time check failure', 'heap corruption',
                     'abort()', 'visual c++ runtime', 'has stopped working', 'unhandled exception',
                     'stack buffer', 'assertion failed')

    def sample_points(image):
        points = []
        for ty in range(0, FB_H, TILE):
            th = min(TILE, FB_H - ty)
            for y in (ty + th // 4, ty + (3 * th) // 4):
                for x in range(2, FB_W, 4):
                    points.append((x, y))
        return points

    def screen_pixels(child, points):
        client = wintypes.RECT()
        user32.GetClientRect(child, ctypes.byref(client))
        cw, ch = client.right, client.bottom
        scale = min(cw / FB_W, ch / FB_H)
        shown_w, shown_h = min(cw, int(FB_W * scale + 0.999)), min(ch, int(FB_H * scale + 0.999))
        off_x, off_y = (cw - shown_w) // 2, (ch - shown_h) // 2
        origin = wintypes.POINT(0, 0)
        user32.ClientToScreen(child, ctypes.byref(origin))
        dc = user32.GetDC(None)
        result = []
        for x, y in points:
            value = gdi32.GetPixel(dc, origin.x + off_x + int((x + 0.5) * scale),
                                   origin.y + off_y + int((y + 0.5) * scale))
            result.append((value & 0xFF, (value >> 8) & 0xFF, (value >> 16) & 0xFF))
        user32.ReleaseDC(None, dc)
        return result

    history_root = r'Software\TightVNC\Viewer\History'

    def history_exists(host):
        try:
            winreg.CloseKey(winreg.OpenKey(winreg.HKEY_CURRENT_USER, history_root + '\\' + host))
            return True
        except OSError:
            return False

    def free_port(start):
        for port in range(start, 5999):
            probe = socket.socket()
            try:
                probe.bind(('127.0.0.1', port))
                return port
            except OSError:
                continue
            finally:
                probe.close()
        raise SystemExit('no free port in %d-5999' % start)

    cases = args.cases or list(CASES)
    phases = args.phases.split(',')
    viewer_pf = None
    window = [int(v) for v in args.window.split(',')]
    results = []
    next_port = args.port_base
    for case in cases:
        for phase in phases:
            port = free_port(next_port)
            next_port = port + 1
            host = '127.0.0.1::%d' % port
            existed = history_exists(host)
            server = Server(port, case, phase)
            thread = threading.Thread(target=server.serve, daemon=True)
            thread.start()
            server.ready.wait(5)
            command = [os.path.abspath(args.run_viewer)]
            options_file = None
            if args.use_8bit:
                handle, options_file = tempfile.mkstemp(suffix='.vnc', prefix='zrle-cpixel-')
                with os.fdopen(handle, 'w') as out:
                    out.write('[connection]\nhost=127.0.0.1\nport=%d\n[options]\n8bit=1\n' % port)
                command.append('-optionsfile=' + options_file)
            else:
                command += ['-host=127.0.0.1', '-port=%d' % port]
            command += ['-viewonly=yes', '-encoding=zrle', '-useclipboard=no']
            process = subprocess.Popen(command, cwd=os.path.dirname(os.path.abspath(args.run_viewer)))
            verdict, detail = 'OK', ''
            # A server in a CPIXEL layout the viewer does not use (before its
            # SetPixelFormat takes effect) desynchronises the stream: a handled
            # error message is then the correct outcome, a crash is not.
            expect_desync = phase == 'before' and CASES[case].cpixel_size() != args.viewer_pf.cpixel_size()
            started = time.monotonic()
            laid_out = False
            desync_since = None
            tolerance = 30 if args.use_8bit else 3
            try:
                while True:
                    code = process.poll()
                    if code is not None:
                        if code < 0 or code > 0x7FFFFFFF or code == 3:
                            verdict, detail = 'CRASH', 'viewer exit 0x%08X' % (code & 0xFFFFFFFF)
                        else:
                            verdict, detail = 'ERROR', 'viewer exited with %d' % code
                        break
                    shown = dialogs(process.pid)
                    crash = [d for _, d in shown if any(p in d.lower() for p in crash_pattern)]
                    if crash:
                        verdict, detail = 'CRASH', crash[0]
                        break
                    if shown:
                        verdict, detail = 'ERROR', shown[0][1]
                        for hwnd, _ in shown:
                            user32.PostMessageW(hwnd, 0x0010, 0, 0)
                        break
                    windows = session_windows(process.pid)
                    if windows and not laid_out:
                        user32.ShowWindow(windows[0], 9)
                        user32.SetWindowPos(windows[0], wintypes.HWND(-1), *window, 0x0040)
                        laid_out = True
                    if windows and server.sent.is_set():
                        time.sleep(0.4)
                        expected = server.image
                        if server.sent_pf is not None and server.phase == 'before':
                            # Decoded with the viewer's own format, not the server's.
                            expected = interpret(server.sent_pf, args.viewer_pf, server.image)
                        if expected is None:
                            # Give the viewer time to reject the stream (error dialog or exit).
                            if desync_since is None:
                                desync_since = time.monotonic()
                            if time.monotonic() - desync_since < 3:
                                time.sleep(0.15)
                                continue
                            verdict, detail = 'OK', 'no colour check (CPIXEL size differs from the viewer format; stream out of step), viewer still running'
                            break
                        points = sample_points(expected)
                        bad = []
                        end = time.monotonic() + 6
                        while True:
                            seen = screen_pixels(windows[1], points)
                            bad = [(p, s, expected[p[1]][p[0]]) for p, s in zip(points, seen)
                                   if any(abs(a - b) > tolerance for a, b in zip(s, expected[p[1]][p[0]]))]
                            if not bad or time.monotonic() > end:
                                break
                            time.sleep(0.3)
                        if bad:
                            verdict = 'COLORS'
                            detail = '%d of %d samples differ, first at fb%s: got %s want %s' % (
                                len(bad), len(points), bad[0][0], bad[0][1], bad[0][2])
                        else:
                            detail = 'colours match (%d samples)' % len(points)
                        break
                    if time.monotonic() - started > args.timeout:
                        verdict, detail = 'TIMEOUT', 'no session window or update within %ds' % args.timeout
                        break
                    time.sleep(0.15)
                if verdict == 'ERROR':
                    # The viewer shows its message and exits: the teardown must be clean.
                    try:
                        code = process.wait(6)
                        if code < 0 or code > 0x7FFFFFFF or code == 3:
                            verdict, detail = 'CRASH', detail + ' / exit 0x%08X' % (code & 0xFFFFFFFF)
                    except subprocess.TimeoutExpired:
                        pass
                    if verdict == 'ERROR' and expect_desync:
                        verdict, detail = 'OK', 'handled error, as expected for a CPIXEL layout the viewer does not use: ' + detail
            finally:
                if process.poll() is None:
                    process.kill()
                process.wait(10)
                thread.join(5)
                if options_file:
                    os.unlink(options_file)
                if not existed and history_exists(host):
                    winreg.DeleteKey(winreg.HKEY_CURRENT_USER, history_root + '\\' + host)
            line = '%-8s %-6s %-6s %s' % (case, phase, verdict, detail)
            print(line, flush=True)
            results.append(verdict)
    bad = [v for v in results if v not in ('OK',)]
    print('SUMMARY runs=%d ok=%d not-ok=%d' % (len(results), len(results) - len(bad), len(bad)), flush=True)
    return 1 if bad else 0


def main():
    parser = argparse.ArgumentParser(description=__doc__.split('\n')[0])
    parser.add_argument('--port', type=int, help='listen port (server mode)')
    parser.add_argument('--case', choices=sorted(CASES), default='shifted')
    parser.add_argument('--phase', choices=('before', 'after'), default='after')
    parser.add_argument('--duration', type=float, default=25.0, help='hard limit in seconds (default 25)')
    parser.add_argument('--run-viewer', metavar='EXE', help='drive this fsvncviewer.exe against every case and phase')
    parser.add_argument('--cases', nargs='*', choices=sorted(CASES), help='driver: cases to run (default all)')
    parser.add_argument('--phases', default='before,after', help='driver: comma separated (default before,after)')
    parser.add_argument('--use-8bit', action='store_true', help='driver: start the viewer with 8bit=1 (colour tolerance 30)')
    parser.add_argument('--port-base', type=int, default=5985, help='driver: first port tried (5960-5999)')
    parser.add_argument('--window', default='40,40,1280,800', help='driver: viewer window x,y,w,h')
    parser.add_argument('--timeout', type=int, default=25, help='driver: seconds per run')
    args = parser.parse_args()
    if args.run_viewer:
        if not 5960 <= args.port_base <= 5999:
            parser.error('--port-base must be within 5960-5999')
        # Format the viewer asks for with SetPixelFormat; before the first
        # update the viewer already decodes with it.
        args.viewer_pf = PixelFormat(8, 8, 0, (7, 7, 3), (0, 3, 6)) if args.use_8bit else VIEWER_32
        return run_driver(args)
    if args.port is None or not 1024 <= args.port <= 65535:
        parser.error('--port is required (1024-65535)')
    server = Server(args.port, args.case, args.phase, args.duration)
    print('LISTENING 127.0.0.1:%d case=%s phase=%s fb=%dx%d pf=%s' % (
        args.port, args.case, args.phase, FB_W, FB_H, server.server_pf.describe()), flush=True)
    server.serve()
    print('DONE end=%s sent_pf=%s client_pf=%s' % (
        server.result, server.sent_pf.describe() if server.sent_pf else '-',
        server.client_pf.describe() if server.client_pf else '-'), flush=True)
    return 0


if __name__ == '__main__':
    sys.exit(main())
