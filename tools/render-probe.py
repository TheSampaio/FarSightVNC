#!/usr/bin/env python3
"""Input and display latency probe for a tvnviewer window (FarSightVNC).

Drives one viewer process connected to tools/fake-rfb-server.py and writes a
JSON result with input latency, display latency and the server's frame rate.

Input latency: posts WM_MOUSEMOVE messages (PostMessage) to the viewer's
remote-desktop child window at known times. Nothing is injected into the real
desktop: no SendInput, no SetCursorPos, no keyboard events, so the real mouse
and keyboard are never involved. The fake server stamps the matching
PointerEvent on arrival; latency = server arrival - post time. This covers the
viewer's message handling, coordinate transform and socket send, plus
scheduling noise. It does not include the OS input path of a real mouse.

Display latency: the server changes the colour of a 64x64 marker square in the
top-left corner of the framebuffer and stamps the moment it starts sending the
update that carries the new colour. This probe polls that screen pixel
(GetPixel on the screen DC) and records when each colour first appears.
latency = first matching pixel observation - server send start. It includes the
frame transfer, the viewer's decode/scale/paint and the desktop compositor, with
a resolution of about 1 ms (the polling period). It does not include the
monitor's scan-out and panel latency. The viewer window is moved to a fixed
topmost position so that nothing covers the sampled pixel; do not use the
machine while a measurement runs.

Both clocks are time.perf_counter_ns() (QueryPerformanceCounter), which is
shared between processes on the same machine.

Example (the server must be running with matching --size):
  python tools/render-probe.py --pid 1234 --server-json build-logs/server.json --size 3840x2160 --scale fit
"""
import argparse
import ctypes
import json
import math
import os
import statistics
import sys
import time
from ctypes import wintypes

user32 = ctypes.WinDLL('user32', use_last_error=True)
gdi32 = ctypes.WinDLL('gdi32', use_last_error=True)
HWND_TOPMOST = wintypes.HWND(-1)
SWP_SHOWWINDOW = 0x0040
SW_RESTORE = 9
WM_MOUSEMOVE = 0x0200
MARKER_CENTER = 32          # remote pixel inside the 64x64 marker square
VIEWER_CLASS = 'TvnWindowClass'
WNDENUMPROC = ctypes.WINFUNCTYPE(wintypes.BOOL, wintypes.HWND, wintypes.LPARAM)

user32.GetClassNameW.argtypes = [wintypes.HWND, wintypes.LPWSTR, ctypes.c_int]
user32.GetWindowThreadProcessId.argtypes = [wintypes.HWND, ctypes.POINTER(wintypes.DWORD)]
user32.EnumWindows.argtypes = [WNDENUMPROC, wintypes.LPARAM]
user32.EnumChildWindows.argtypes = [wintypes.HWND, WNDENUMPROC, wintypes.LPARAM]
user32.IsWindowVisible.argtypes = [wintypes.HWND]
user32.ShowWindow.argtypes = [wintypes.HWND, ctypes.c_int]
user32.SetWindowPos.argtypes = [wintypes.HWND, wintypes.HWND, ctypes.c_int, ctypes.c_int, ctypes.c_int,
                                ctypes.c_int, wintypes.UINT]
user32.GetClientRect.argtypes = [wintypes.HWND, ctypes.POINTER(wintypes.RECT)]
user32.ClientToScreen.argtypes = [wintypes.HWND, ctypes.POINTER(wintypes.POINT)]
user32.PostMessageW.argtypes = [wintypes.HWND, wintypes.UINT, wintypes.WPARAM, wintypes.LPARAM]
user32.GetDC.argtypes = [wintypes.HWND]
user32.GetDC.restype = wintypes.HDC
user32.ReleaseDC.argtypes = [wintypes.HWND, wintypes.HDC]
gdi32.GetPixel.argtypes = [wintypes.HDC, ctypes.c_int, ctypes.c_int]
gdi32.GetPixel.restype = wintypes.DWORD


def class_name(hwnd):
    buffer = ctypes.create_unicode_buffer(256)
    user32.GetClassNameW(hwnd, buffer, 256)
    return buffer.value


def find_session_windows(pid):
    """Return (top-level window, remote-desktop child) of the viewer process, or None."""
    found = []

    def on_top(hwnd, _):
        owner = wintypes.DWORD()
        user32.GetWindowThreadProcessId(hwnd, ctypes.byref(owner))
        if owner.value == pid and class_name(hwnd) == VIEWER_CLASS and user32.IsWindowVisible(hwnd):
            found.append(hwnd)
        return True
    user32.EnumWindows(WNDENUMPROC(on_top), 0)
    for top in found:
        children = []

        def on_child(hwnd, _):
            if class_name(hwnd) == VIEWER_CLASS:
                children.append(hwnd)
            return True
        user32.EnumChildWindows(top, WNDENUMPROC(on_child), 0)
        if children:
            return top, children[0]
    return None


def percentile(sorted_values, fraction):
    return sorted_values[int(fraction * (len(sorted_values) - 1))]


def summarize(latencies_ms, attempted):
    if not latencies_ms:
        return {'samples': 0, 'attempted': attempted, 'missed': attempted}
    ordered = sorted(latencies_ms)
    return {'samples': len(ordered), 'attempted': attempted, 'missed': attempted - len(ordered),
            'median_ms': round(statistics.median(ordered), 2), 'p95_ms': round(percentile(ordered, 0.95), 2),
            'max_ms': round(ordered[-1], 2)}


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument('--pid', type=int, required=True, help='viewer process id')
    parser.add_argument('--server-json', required=True, help='report written by fake-rfb-server.py')
    parser.add_argument('--size', default='3840x2160', help='remote framebuffer WxH (as given to the server)')
    parser.add_argument('--scale', choices=('fit', 'none'), default='fit',
                        help='fit = viewer scales to the window; none = 100 percent')
    parser.add_argument('--window', default='40,40,1280,800', help='viewer window x,y,w,h (default 40,40,1280,800)')
    parser.add_argument('--warmup', type=float, default=3, help='seconds before sampling starts')
    parser.add_argument('--seconds', type=float, default=15, help='sampling length')
    parser.add_argument('--move-interval', type=float, default=50, help='ms between posted mouse moves')
    parser.add_argument('--no-input', action='store_true', help='skip the input latency test')
    parser.add_argument('--no-display', action='store_true', help='skip the display latency test')
    parser.add_argument('--out', required=True, help='result JSON path')
    args = parser.parse_args()
    remote_w, remote_h = (int(v) for v in args.size.lower().split('x'))
    x, y, w, h = (int(v) for v in args.window.split(','))

    deadline = time.monotonic() + 30
    windows = None
    while time.monotonic() < deadline and not windows:
        windows = find_session_windows(args.pid)
        time.sleep(0.2)
    if not windows:
        raise SystemExit('viewer session window not found for pid %d' % args.pid)
    top, child = windows
    user32.ShowWindow(top, SW_RESTORE)
    user32.SetWindowPos(top, HWND_TOPMOST, x, y, w, h, SWP_SHOWWINDOW)
    time.sleep(args.warmup)

    # Geometry of the remote image inside the child window (see ScaleManager.cpp:
    # fit keeps the aspect ratio and centres; 100 percent starts at the top-left).
    client = wintypes.RECT()
    user32.GetClientRect(child, ctypes.byref(client))
    cw, ch = client.right, client.bottom
    scale = min(cw / remote_w, ch / remote_h) if args.scale == 'fit' else 1.0
    shown_w, shown_h = min(cw, math.ceil(remote_w * scale)), min(ch, math.ceil(remote_h * scale))
    off_x, off_y = (cw - shown_w) // 2, (ch - shown_h) // 2
    origin = wintypes.POINT(0, 0)
    user32.ClientToScreen(child, ctypes.byref(origin))
    pixel_x = origin.x + off_x + int(MARKER_CENTER * scale)
    pixel_y = origin.y + off_y + int(MARKER_CENTER * scale)
    tolerance = 2 / scale + 2

    screen_dc = user32.GetDC(None)
    posts, observations = [], []      # (ns, remote x, remote y) / (ns, (r, g, b))
    last_color = None
    interval_ns = int(args.move_interval * 1e6)
    start = time.perf_counter_ns()
    end = start + int(args.seconds * 1e9)
    next_post = start
    index = 0
    while True:
        now = time.perf_counter_ns()
        if now >= end:
            break
        if not args.no_input and now >= next_post:
            cx = off_x + 10 + (index * 97) % max(1, shown_w - 20)
            cy = off_y + 10 + (index * 53) % max(1, shown_h - 20)
            stamp = time.perf_counter_ns()
            user32.PostMessageW(child, WM_MOUSEMOVE, 0, (cy << 16) | cx)
            posts.append((stamp, (cx - off_x) / scale, (cy - off_y) / scale))
            next_post += interval_ns
            index += 1
        if not args.no_display:
            value = gdi32.GetPixel(screen_dc, pixel_x, pixel_y)
            stamp = time.perf_counter_ns()
            color = (value & 0xFF, (value >> 8) & 0xFF, (value >> 16) & 0xFF)
            if color != last_color:
                observations.append((stamp, color))
                last_color = color
        time.sleep(0.0005)
    user32.ReleaseDC(None, screen_dc)

    server_wait = time.monotonic() + args.warmup + 30
    while not os.path.exists(args.server_json) and time.monotonic() < server_wait:
        time.sleep(0.2)
    time.sleep(0.3)  # let the server finish writing
    with open(args.server_json, encoding='utf-8') as handle:
        server = json.load(handle)

    # Input: pair each post with the first later PointerEvent at the expected remote position.
    input_ms, cursor = [], 0
    arrivals = server['pointer_events']
    for stamp, ex, ey in posts:
        for i in range(cursor, len(arrivals)):
            event = arrivals[i]
            if event['ns'] > stamp + 2_000_000_000:
                break
            if event['ns'] >= stamp and abs(event['x'] - ex) <= tolerance and abs(event['y'] - ey) <= tolerance:
                input_ms.append((event['ns'] - stamp) / 1e6)
                cursor = i + 1
                break

    # Display: first observation of the expected colour after the server started sending it.
    display_ms, attempted = [], 0
    markers = [m for m in server['marker_events'] if start <= m['ns'] <= end]
    all_markers = server['marker_events']
    for number, marker in enumerate(markers):
        attempted += 1
        later = next((m['ns'] for m in all_markers if m['ns'] > marker['ns']), None)
        for stamp, color in observations:
            if stamp < marker['ns']:
                continue
            if later is not None and stamp > later:
                break
            if all(abs(a - b) <= 8 for a, b in zip(color, marker['rgb'])):
                display_ms.append((stamp - marker['ns']) / 1e6)
                break

    # Frame rate over the probe's own sampling window, from the server's send timestamps.
    updates = [t for t in server['update_starts_ns'] if start <= t <= end]
    gaps = [(b - a) / 1e6 for a, b in zip(updates, updates[1:])]

    result = {
        'window_client': [cw, ch], 'image_offset': [off_x, off_y], 'image_shown': [shown_w, shown_h],
        'scale': round(scale, 4), 'sampled_pixel': [pixel_x, pixel_y],
        'input_latency': None if args.no_input else summarize(input_ms, len(posts)),
        'display_latency': None if args.no_display else summarize(display_ms, attempted),
        'server': {
            'effective_fps': round(len(updates) / args.seconds, 2),
            'update_interval_ms_median': round(statistics.median(gaps), 2) if gaps else None,
            'update_interval_ms_p95': round(percentile(sorted(gaps), 0.95), 2) if gaps else None,
            'requests_total': server['requests_total'], 'connections': server['connections'],
        },
    }
    with open(args.out, 'w', encoding='utf-8') as handle:
        json.dump(result, handle, indent=1)
    print(json.dumps(result))


if __name__ == '__main__':
    sys.exit(main())
