#!/usr/bin/env python3
# Copyright (C) 2026 FarSightVNC contributors. GPL-2.0-or-later.
"""Fuzzing RFB server for the FarSightVNC viewer decoders.

Listens on 127.0.0.1 only, completes an RFB 3.8 handshake (3.3 and 3.7 clients
also work) with security type None and then sends a sequence of
FramebufferUpdate messages that is deterministic for a given --encoding and
--seed. The sequence mixes valid rectangles (so the decoder state, the zlib
streams, the cursor and the frame buffer size evolve), structured mutations
aimed at the parsing paths of viewer-core (bad lengths, empty, huge and
overflowing rectangles, out-of-bounds CopyRect sources, bad subencodings,
truncated or garbage zlib data, bad palettes, oversized cursors) and pure
random bytes.

Encodings: tight, zrle, hextile, rre, copyrect, richcursor, desktopsize.
--scenario sends a fixed, hand-written sequence instead (regression cases for
defects found by fuzzing).

One connection is served. The server stops when the client disconnects, after
--messages updates plus --linger seconds, or after --duration seconds. It
prints "LISTENING 127.0.0.1:<port>" when ready and one "DONE ..." line with the
number of updates sent and the descriptions of the last rectangles, so a seed
that breaks the viewer can be triaged and replayed.

The bytes depend only on the seed and on the pixel format the client asks for
(SetPixelFormat), so a given viewer build replays the same session.

Examples:
  python -I tools/fuzz-rfb-server.py --port 5933 --encoding tight --seed 7
  python -I tools/fuzz-rfb-server.py --port 5933 --scenario fillrect-rre-bottom-edge
"""
import argparse
import random
import socket
import struct
import sys
import threading
import time
import zlib

ENCODINGS = {
    'raw': 0, 'copyrect': 1, 'rre': 2, 'hextile': 5, 'tight': 7, 'zrle': 16,
    'richcursor': -239, 'desktopsize': -223, 'lastrect': -224, 'pointerpos': -232,
}
FUZZ_ENCODINGS = ('tight', 'zrle', 'hextile', 'rre', 'copyrect', 'richcursor', 'desktopsize')

# 16x16 baseline JPEG (4:2:0, quality 50) generated for this tool; its SOF0
# segment starts at JPEG_SOF_OFFSET (height and width follow the precision byte).
JPEG_16X16 = bytes.fromhex(''.join((
    'ffd8ffe000104a46494600010101006000600000ffdb004300100b0c0e0c0a100e0d0e1211101318281a181616183123',
    '251d283a333d3c3933383740485c4e404457453738506d51575f626768673e4d71797064785c656763ffdb0043011112',
    '121815182f1a1a2f63423842636363636363636363636363636363636363636363636363636363636363636363636363',
    '6363636363636363636363636363ffc00011080010001003012200021101031101ffc4001f0000010501010101010100',
    '000000000000000102030405060708090a0bffc400b5100002010303020403050504040000017d010203000411051221',
    '31410613516107227114328191a1082342b1c11552d1f02433627282090a161718191a25262728292a3435363738393a',
    '434445464748494a535455565758595a636465666768696a737475767778797a838485868788898a9293949596979899',
    '9aa2a3a4a5a6a7a8a9aab2b3b4b5b6b7b8b9bac2c3c4c5c6c7c8c9cad2d3d4d5d6d7d8d9dae1e2e3e4e5e6e7e8e9eaf1',
    'f2f3f4f5f6f7f8f9faffc4001f0100030101010101010101010000000000000102030405060708090a0bffc400b51100',
    '020102040403040705040400010277000102031104052131061241510761711322328108144291a1b1c109233352f015',
    '6272d10a162434e125f11718191a262728292a35363738393a434445464748494a535455565758595a63646566676869',
    '6a737475767778797a82838485868788898a92939495969798999aa2a3a4a5a6a7a8a9aab2b3b4b5b6b7b8b9bac2c3c4',
    'c5c6c7c8c9cad2d3d4d5d6d7d8d9dae2e3e4e5e6e7e8e9eaf2f3f4f5f6f7f8f9faffda000c03010002110311003f00c6',
    'd374ee9f2d5886c025c4a48fe33fceba4b0b008a0914915897bc9091fc6703f1a72c6470c9a5ac993431d7853d7a9fff',
    'd9')))
JPEG_SOF_OFFSET = JPEG_16X16.index(b'\xff\xc0')


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
        return max(1, self.bpp // 8)

    def value(self, rgb):
        v = 0
        for c, m, s in zip(rgb, self.max, self.shift):
            v |= (c * m // 255) << s
        return v & ((1 << (8 * self.bypp)) - 1)

    def pixel(self, rgb):
        return self.value(rgb).to_bytes(self.bypp, 'big' if self.big_endian else 'little')

    def tight_cpixel_size(self):
        # Same rule as TightDecoder::decode (m_isCPixel).
        if self.depth == 24 and self.bpp == 32 and self.max == (255, 255, 255):
            return 3
        return self.bypp

    def tight_cpixel(self, rgb):
        if self.tight_cpixel_size() == 3:
            v = self.value(rgb)
            return bytes(((v >> 16) & 0xFF, (v >> 8) & 0xFF, v & 0xFF))
        return self.pixel(rgb)

    def zrle_cpixel_layout(self):
        # Same rule as ZrleDecoder::decode: (bytes per cpixel, first byte).
        if self.bpp != 32:
            return self.bypp, 0
        cmv = 0
        for m, s in zip(self.max, self.shift):
            cmv |= (m << s)
        cmv &= 0xFFFFFFFF
        if cmv & 0xFF000000 == 0:
            return 3, 0
        if cmv & 0xFF == 0:
            return 3, 1
        return 4, 0

    def zrle_cpixel(self, rgb):
        size, first = self.zrle_cpixel_layout()
        return self.pixel(rgb).ljust(4, b'\0')[first:first + size]

    def describe(self):
        return '%d/%d%s/%s' % (self.bpp, self.depth, 'be' if self.big_endian else '',
                               ','.join(str(s) for s in self.shift))


SERVER_PIXEL_FORMATS = [
    PixelFormat(32, 24, 0, (255, 255, 255), (16, 8, 0)),
    PixelFormat(32, 24, 0, (255, 255, 255), (0, 8, 16)),
    PixelFormat(32, 24, 0, (255, 255, 255), (24, 16, 8)),
    PixelFormat(32, 24, 1, (255, 255, 255), (16, 8, 0)),
    PixelFormat(16, 16, 0, (31, 63, 31), (11, 5, 0)),
    PixelFormat(8, 8, 0, (7, 7, 3), (0, 3, 6)),
]


def compact_size(n):
    """Tight compact length (1 to 3 bytes, up to 2^22 - 1)."""
    out = bytearray([n & 0x7F])
    if n > 0x7F:
        out[0] |= 0x80
        out.append((n >> 7) & 0x7F)
        if n > 0x3FFF:
            out[1] |= 0x80
            out.append((n >> 14) & 0xFF)
    return bytes(out)


def run_length(n):
    """ZRLE run length (n >= 1) as 255-continued bytes of n - 1."""
    n -= 1
    out = bytearray()
    while n >= 255:
        out.append(255)
        n -= 255
    out.append(n)
    return bytes(out)


def header(x, y, w, h, encoding):
    return struct.pack('>HHHHi', x & 0xFFFF, y & 0xFFFF, w & 0xFFFF, h & 0xFFFF, encoding)


class Fuzzer:
    """Generates the update sequence for one encoding and seed."""

    def __init__(self, encoding, seed, width, height, pf):
        self.encoding = encoding
        self.rng = random.Random('%s:%d' % (encoding, seed))
        self.w, self.h = width, height
        self.pf = pf
        self.tight_z = [None] * 4
        self.zrle_z = None
        self.descs = []
        self.extra_rects = 0     # rectangles packed into one generated chunk
        # Per-seed mutation pressure: low values let the session go deeper.
        self.p_valid = self.rng.choice((0.9, 0.75, 0.55, 0.35))

    # ---- helpers -------------------------------------------------------------
    def randbytes(self, n):
        return self.rng.randbytes(n)

    def colors(self, k):
        return [(self.rng.randrange(256), self.rng.randrange(256), self.rng.randrange(256)) for _ in range(k)]

    def indices(self, count, k):
        """Pixel indices into a k-colour palette, with runs so RLE paths are used."""
        out = []
        while len(out) < count:
            out.extend([self.rng.randrange(k)] * self.rng.choice((1, 1, 2, 3, 7, 20, 64)))
        return out[:count]

    def geom(self, maxw=256, maxh=128):
        if self.w <= 0 or self.h <= 0:
            return 0, 0, 0, 0
        w = self.rng.randint(1, min(self.w, maxw))
        h = self.rng.randint(1, min(self.h, maxh))
        if self.rng.random() < 0.04:
            w, h = self.rng.choice(((0, h), (w, 0), (0, 0)))
        return self.rng.randint(0, self.w - w), self.rng.randint(0, self.h - h), w, h

    def bad_geom(self):
        w, h, r = self.w, self.h, self.rng
        return r.choice((
            (max(w - 1, 0), 0, 2, 1),                 # one pixel past the right edge
            (0, max(h - 1, 0), 1, 2),                 # one pixel past the bottom edge
            (65535, 65535, 65535, 65535),             # overflowing coordinates
            (r.randrange(65536), r.randrange(65536), r.randrange(65536), r.randrange(65536)),
            (0, 0, w + 1, h),
            (w, h, 0, 0),
            (1, 1, 65535, 1),
        ))

    def rect(self, x, y, w, h, payload, enc=None):
        return header(x, y, w, h, ENCODINGS[self.encoding] if enc is None else enc) + payload

    # ---- messages --------------------------------------------------------------
    def next_message(self, index):
        r = self.rng
        if r.random() < 0.04:
            self.descs.append('m%d:chaos' % index)
            return b'\x00' + self.randbytes(r.randint(1, 512))
        count = r.choice((1, 1, 1, 2, 3, 5))
        rects = []
        self.extra_rects = 0
        for i in range(count):
            data, desc = self.make_rect()
            rects.append(data)
            self.descs.append('m%d.%d:%s' % (index, i, desc))
        count += self.extra_rects
        declared = count
        if r.random() < 0.03:
            declared = r.choice((0, count + 1, count + 3, 0xFFFF))
            self.descs.append('m%d:nrects=%d/%d' % (index, declared, count))
        if r.random() < 0.05:
            rects.append(header(0, 0, 0, 0, ENCODINGS['lastrect']))
            declared += 1 if declared < 0xFFFF else 0
        return b'\x00\x00' + struct.pack('>H', declared) + b''.join(rects)

    def make_rect(self):
        r = self.rng
        gen = getattr(self, 'rect_' + self.encoding)
        roll = r.random()
        if roll < self.p_valid:
            return gen(False)
        roll = (roll - self.p_valid) / (1.0 - self.p_valid)
        if roll < 0.6:
            return gen(True)
        if roll < 0.85:
            data, desc = gen(False)
            return self.generic_mutation(data, desc)
        x, y, w, h = self.geom() if r.random() < 0.7 else self.bad_geom()
        return self.rect(x, y, w, h, self.randbytes(r.choice((0, 1, 4, 16, 200, 2048)))), 'random'

    def generic_mutation(self, data, desc):
        r = self.rng
        head, payload = bytearray(data[:12]), bytearray(data[12:])
        kind = r.choice(('flip', 'flip', 'trunc', 'append', 'geom', 'enc'))
        if kind == 'flip' and payload:
            for _ in range(r.randint(1, 8)):
                pos = r.randrange(len(payload))
                payload[pos] ^= 1 << r.randrange(8)
        elif kind == 'trunc' and payload:
            del payload[r.randrange(len(payload)):]
        elif kind == 'append':
            payload += self.randbytes(r.randint(1, 64))
        elif kind == 'geom':
            head[0:8] = struct.pack('>HHHH', *[v & 0xFFFF for v in self.bad_geom()])
        else:
            enc = r.choice((0, 1, 2, 5, 7, 16, -239, -223, -224, -232, 6, 0x12345, -1))
            head[8:12] = struct.pack('>i', enc)
        return bytes(head + payload), desc + '+' + kind

    # ---- raw (filler) ------------------------------------------------------------
    def raw_rect(self):
        x, y, w, h = self.geom(128, 64)
        palette = [self.pf.pixel(c) for c in self.colors(4)]
        data = b''.join(palette[i] for i in self.indices(w * h, 4))
        return header(x, y, w, h, ENCODINGS['raw']) + data, 'raw'

    # ---- Tight -------------------------------------------------------------------
    def tight_compress(self, sid, data, level=None):
        if self.tight_z[sid] is None:
            self.tight_z[sid] = zlib.compressobj(level or self.rng.randint(1, 9))
        z = self.tight_z[sid]
        return z.compress(data) + z.flush(zlib.Z_SYNC_FLUSH)

    def tight_data(self, sid, data):
        if len(data) < 12:
            return data
        comp = self.tight_compress(sid, data)
        return compact_size(len(comp)) + comp

    def rect_tight(self, mutate):
        r, pf = self.rng, self.pf
        x, y, w, h = self.geom(256, 96)
        reset = r.choice((0, 0, 0, 1, 2, 4, 8, 15)) if r.random() < 0.2 else 0
        for i in range(4):
            if reset & (1 << i):
                self.tight_z[i] = None
        sid = r.randrange(4)
        cps = pf.tight_cpixel_size()
        kind = r.choice(('fill', 'jpeg', 'copy', 'copy', 'palette', 'palette', 'mono', 'gradient'))
        mut = r.choice(('badtype', 'badfilter', 'zerolen', 'hugelen', 'garbage', 'trunczlib', 'short',
                        'long', 'desync', 'palindex', 'palsize', 'mono-bytes', 'jpegdims', 'jpegflip',
                        'jpegtrunc', 'jpegzero', 'jpegbig', 'geom')) if mutate else None
        if mut == 'geom':
            x, y, w, h = self.bad_geom()
            w, h = min(w, 512), min(h, 512)
        if mut == 'badtype':
            return self.rect(x, y, w, h, bytes([(r.randint(10, 15) << 4) | r.randrange(16)])
                             + self.randbytes(r.randint(0, 64))), 'tight/badtype'
        if mut == 'badfilter':
            return self.rect(x, y, w, h, bytes([0x40 | (sid << 4) | reset, r.randint(3, 255)])
                             + self.randbytes(r.randint(0, 64))), 'tight/badfilter'
        if mut and mut.startswith('jpeg'):
            kind = 'jpeg'
        elif mut in ('palindex', 'palsize', 'mono-bytes'):
            kind = 'palette' if mut != 'mono-bytes' else 'mono'
        elif mut in ('zerolen', 'hugelen', 'garbage', 'trunczlib', 'short', 'long', 'desync') and kind in ('fill', 'jpeg'):
            kind = 'copy'

        if kind == 'fill':
            return self.rect(x, y, w, h, bytes([0x80 | reset]) + pf.tight_cpixel(self.colors(1)[0])), \
                'tight/fill' + ('+' + mut if mut else '')
        if kind == 'jpeg':
            jpeg = bytearray(JPEG_16X16)
            if not mutate and r.random() < 0.7 and self.w >= 16 and self.h >= 16:
                x, y, w, h = r.randint(0, self.w - 16), r.randint(0, self.h - 16), 16, 16
            jh, jw = h, w
            if mut == 'jpegdims':
                jh, jw = r.choice(((r.randint(0, 300), r.randint(0, 300)), (h, w + 1), (0, 0), (65535, 65535)))
            if mut == 'jpegbig' and self.w > 0 and self.h > 0:
                w, h = r.randint(1, min(self.w, 2048)), r.randint(1, min(self.h, 512))
                x, y = r.randint(0, self.w - w), r.randint(0, self.h - h)
                jh, jw = h, w
            struct.pack_into('>HH', jpeg, JPEG_SOF_OFFSET + 5, jh & 0xFFFF, jw & 0xFFFF)
            if mut == 'jpegflip':
                for _ in range(r.randint(1, 16)):
                    jpeg[r.randrange(2, len(jpeg))] ^= 1 << r.randrange(8)
            elif mut == 'jpegtrunc':
                del jpeg[r.randint(2, len(jpeg) - 1):]
            if mut == 'jpegzero':
                body = compact_size(0)
            else:
                body = compact_size(len(jpeg)) + bytes(jpeg)
            return self.rect(x, y, w, h, bytes([0x90 | reset]) + body), 'tight/jpeg' + ('+' + mut if mut else '')

        filter_byte = b''
        control = (sid << 4) | reset
        if kind == 'copy':
            if r.random() < 0.5:
                control |= 0x40
                filter_byte = b'\x00'
            k = r.choice((1, 3, 30, 300))
            cpx = [pf.tight_cpixel(c) for c in self.colors(min(k, 64))]
            data = b''.join(cpx[i] for i in self.indices(w * h, len(cpx)))
        elif kind == 'gradient':
            control |= 0x40
            filter_byte = b'\x02'
            data = self.randbytes(w * h * cps)
        else:   # palette / mono
            control |= 0x40
            size = 2 if kind == 'mono' else r.choice((1, 3, 4, 16, 200, 256))
            if mut == 'palsize':
                declared, size = size, r.choice((1, 2, 3))
                declared = r.choice((256, 128, declared + 5)) if declared <= size else declared
            else:
                declared = size
            pal = b''.join(pf.tight_cpixel(c) for c in self.colors(size))
            filter_byte = bytes([0x01, (declared - 1) & 0xFF]) + pal
            if declared == 2 and mut != 'mono-bytes':
                bits = self.indices(w * h, 2)
                rows = []
                for row in range(h):
                    acc = bytearray((w + 7) // 8)
                    for col in range(w):
                        if bits[row * w + col]:
                            acc[col // 8] |= 0x80 >> (col % 8)
                    rows.append(bytes(acc))
                data = b''.join(rows)
            else:
                limit = 256 if mut == 'palindex' else max(size, 1)
                data = bytes(self.indices(w * h, limit))
        # zlib level mutations
        if mut == 'zerolen' and len(data) >= 12:
            body = compact_size(0)
        elif mut == 'hugelen' and len(data) >= 12:
            comp = self.tight_compress(sid, data)
            body = compact_size(r.randint(len(comp) + 1, (1 << 22) - 1)) + comp[:r.randint(0, len(comp))]
        elif mut == 'garbage' and len(data) >= 12:
            junk = self.randbytes(r.randint(1, 300))
            body = compact_size(len(junk)) + junk
        elif mut == 'trunczlib' and len(data) >= 12:
            comp = self.tight_compress(sid, data)
            comp = comp[:r.randint(1, max(1, len(comp) - 1))]
            body = compact_size(len(comp)) + comp
        elif mut == 'short' and len(data) >= 13:
            comp = self.tight_compress(sid, data[:r.randint(0, len(data) - 1)])
            body = compact_size(len(comp)) + comp
        elif mut == 'long':
            comp = self.tight_compress(sid, data + self.randbytes(r.choice((1, 100, 5000, 70000))))
            body = compact_size(len(comp)) + comp
        elif mut == 'desync':
            if r.random() < 0.5:
                self.tight_z[sid] = None          # server restarts its stream silently
                body = self.tight_data(sid, data)
            else:
                body = self.tight_data(sid, data)  # client is told to reset, server is not
                control |= 1 << sid
        else:
            body = self.tight_data(sid, data)
        return self.rect(x, y, w, h, bytes([control]) + filter_byte + body), \
            'tight/%s%s' % (kind, '+' + mut if mut else '')

    # ---- ZRLE --------------------------------------------------------------------
    def zrle_tile(self, tw, th, mut):
        r, pf = self.rng, self.pf
        n = tw * th
        cp = pf.zrle_cpixel
        kind = r.choice(('raw', 'solid', 'packed', 'packed', 'plainrle', 'palrle', 'palrle'))
        if mut == 'type129':
            return b'\x81' + self.randbytes(r.randint(0, 32))
        if mut == 'typeunused':
            return bytes([r.randint(17, 127)]) + self.randbytes(r.randint(0, 32))
        if mut in ('packidx',):
            kind = 'packed'
        if mut in ('palrleidx', 'palrlerun'):
            kind = 'palrle'
        if mut == 'plainrun':
            kind = 'plainrle'
        if kind == 'raw':
            cols = [cp(c) for c in self.colors(8)]
            return b'\x00' + b''.join(cols[i] for i in self.indices(n, 8))
        if kind == 'solid':
            return b'\x01' + cp(self.colors(1)[0])
        if kind == 'packed':
            size = r.randint(2, 16)
            bits = 1 if size == 2 else 2 if size <= 4 else 4
            limit = (1 << bits) if mut == 'packidx' else size
            idx = self.indices(n, limit)
            out = bytearray([size]) + b''.join(cp(c) for c in self.colors(size))
            for row in range(th):
                acc, nbits = 0, 0
                line = bytearray()
                for col in range(tw):
                    acc = (acc << bits) | idx[row * tw + col]
                    nbits += bits
                    if nbits == 8:
                        line.append(acc)
                        acc, nbits = 0, 0
                if nbits:
                    line.append(acc << (8 - nbits))
                out += line
            return bytes(out)
        if kind == 'plainrle':
            out = bytearray([128])
            left = n
            while left > 0:
                run = min(left, r.choice((1, 2, 5, 64, 300, 4096)))
                if mut == 'plainrun' and r.random() < 0.3:
                    run = left + r.randint(1, 5000)
                out += cp(self.colors(1)[0]) + run_length(run)
                left -= run
            return bytes(out)
        size = r.randint(2, 127)
        out = bytearray([128 + size]) + b''.join(cp(c) for c in self.colors(size))
        left = n
        while left > 0:
            color = r.randrange(size)
            if mut == 'palrleidx' and r.random() < 0.3:
                color = r.randint(size, 127)
            run = min(left, r.choice((1, 1, 2, 9, 255, 600)))
            if mut == 'palrlerun' and r.random() < 0.3:
                run = left + r.randint(1, 3000)
            if run == 1:
                out.append(color)
            else:
                out.append(color | 0x80)
                out += run_length(run)
            left -= min(run, left)
        return bytes(out)

    def zrle_compress(self, data):
        if self.zrle_z is None:
            self.zrle_z = zlib.compressobj(self.rng.randint(1, 9))
        return self.zrle_z.compress(data) + self.zrle_z.flush(zlib.Z_SYNC_FLUSH)

    def rect_zrle(self, mutate):
        r = self.rng
        x, y, w, h = self.geom(200, 140)
        mut = r.choice(('type129', 'typeunused', 'packidx', 'palrleidx', 'palrlerun', 'plainrun',
                        'trunctiles', 'extra', 'zerolen', 'hugelen', 'garbage', 'trunczlib',
                        'reset', 'toomuch', 'geom')) if mutate else None
        if mut == 'geom':
            x, y, w, h = self.bad_geom()
            w, h = min(w, 300), min(h, 300)
        tiles = []
        bad_tile = r.randrange(max(1, ((w + 63) // 64) * ((h + 63) // 64)))
        t = 0
        for ty in range(0, h, 64):
            for tx in range(0, w, 64):
                tiles.append(self.zrle_tile(min(64, w - tx), min(64, h - ty), mut if t == bad_tile else None))
                t += 1
        payload = b''.join(tiles)
        if mut == 'trunctiles' and payload:
            payload = payload[:r.randrange(len(payload))]
        elif mut == 'extra':
            payload += self.randbytes(r.randint(1, 100))
        elif mut == 'toomuch':
            payload += bytes(r.choice((30000, 200000)))
        if mut == 'zerolen':
            body = struct.pack('>I', 0)
        elif mut == 'hugelen':
            comp = self.zrle_compress(payload)
            body = struct.pack('>I', r.choice((len(comp) + 1, 0x00FFFFFF, r.randint(len(comp) + 1, 0x00400000)))) + comp
        elif mut == 'garbage':
            junk = self.randbytes(r.randint(1, 400))
            body = struct.pack('>I', len(junk)) + junk
        elif mut == 'trunczlib':
            comp = self.zrle_compress(payload)
            comp = comp[:r.randint(1, max(1, len(comp) - 1))]
            body = struct.pack('>I', len(comp)) + comp
        elif mut == 'reset':
            self.zrle_z = None
            comp = self.zrle_compress(payload)
            body = struct.pack('>I', len(comp)) + comp
        else:
            comp = self.zrle_compress(payload)
            body = struct.pack('>I', len(comp)) + comp
        return self.rect(x, y, w, h, body), 'zrle' + ('/' + mut if mut else '')

    # ---- Hextile -----------------------------------------------------------------
    def rect_hextile(self, mutate):
        r, pf = self.rng, self.pf
        x, y, w, h = self.geom(200, 140)
        mut = r.choice(('nobg', 'subout', 'count', 'rawtrunc', 'highflags', 'geom')) if mutate else None
        if mut == 'geom':
            x, y, w, h = self.bad_geom()
            w, h = min(w, 300), min(h, 300)
        out = bytearray()
        first = True
        tiles = [(tx, ty) for ty in range(0, h, 16) for tx in range(0, w, 16)]
        bad_tile = r.randrange(max(1, len(tiles)))
        for t, (tx, ty) in enumerate(tiles):
            tw, th = min(16, w - tx), min(16, h - ty)
            here = mut if t == bad_tile else None
            if here == 'nobg' and first:
                out.append(0)
                first = False
                continue
            kind = r.choice(('raw', 'bg', 'sub', 'subcol', 'reuse')) if not first else r.choice(('raw', 'bg', 'sub', 'subcol'))
            flags = {'raw': 1, 'bg': 2, 'sub': 2 | 4 | 8, 'subcol': 2 | 8 | 16, 'reuse': 0}[kind]
            if here == 'highflags':
                flags |= r.choice((0x20, 0x40, 0x80, 0xE0))
            if here == 'rawtrunc':
                flags = 1
            out.append(flags)
            if flags & 1:
                data = b''.join(pf.pixel(c) for c in self.colors(tw * th))
                if here == 'rawtrunc':
                    data = data[:r.randrange(max(1, len(data)))]
                out += data
                first = False
                continue
            if flags & 2:
                out += pf.pixel(self.colors(1)[0])
            if flags & 4:
                out += pf.pixel(self.colors(1)[0])
            if flags & 8:
                count = r.randint(0, 20)
                declared = 255 if here == 'count' else count
                out.append(declared)
                for _ in range(count):
                    if flags & 16:
                        out += pf.pixel(self.colors(1)[0])
                    if here == 'subout':
                        sx, sy, sw, sh = r.randrange(16), r.randrange(16), r.randint(1, 16), r.randint(1, 16)
                    else:
                        sx, sy = r.randrange(tw), r.randrange(th)
                        sw, sh = r.randint(1, tw - sx), r.randint(1, th - sy)
                    out += bytes(((sx << 4) | sy, ((sw - 1) << 4) | (sh - 1)))
            first = False
        return self.rect(x, y, w, h, bytes(out)), 'hextile' + ('/' + mut if mut else '')

    # ---- RRE ---------------------------------------------------------------------
    def rect_rre(self, mutate):
        r, pf = self.rng, self.pf
        x, y, w, h = self.geom(400, 300)
        mut = r.choice(('subout', 'countbig', 'countshort', 'geom')) if mutate else None
        if mut == 'geom':
            x, y, w, h = self.bad_geom()
        count = r.randint(0, 40)
        subs = bytearray()
        for _ in range(count):
            subs += pf.pixel(self.colors(1)[0])
            if mut == 'subout' and r.random() < 0.5:
                sx, sy, sw, sh = (r.randrange(65536) for _ in range(4))
            elif w and h:
                sx, sy = r.randrange(w), r.randrange(h)
                sw, sh = r.randint(0, w - sx), r.randint(0, h - sy)
            else:
                sx = sy = sw = sh = 0
            subs += struct.pack('>HHHH', sx, sy, sw, sh)
        declared = count
        if mut == 'countbig':
            declared = r.choice((0xFFFFFFFF, 0x7FFFFFFF, count + 1, count + 1000))
        elif mut == 'countshort' and count:
            declared = r.randrange(count)
        body = struct.pack('>I', declared) + pf.pixel(self.colors(1)[0]) + bytes(subs)
        return self.rect(x, y, w, h, body), 'rre' + ('/' + mut if mut else '')

    # ---- CopyRect ----------------------------------------------------------------
    def rect_copyrect(self, mutate):
        r = self.rng
        if not mutate and r.random() < 0.35:
            return self.raw_rect()
        x, y, w, h = self.geom(400, 300)
        mut = r.choice(('srcoob', 'srcedge', 'overlap', 'geom', 'trunc')) if mutate else None
        if mut == 'geom':
            x, y, w, h = self.bad_geom()
        sx, sy = (r.randint(0, max(0, self.w - w)), r.randint(0, max(0, self.h - h)))
        if mut == 'srcoob':
            sx, sy = r.choice(((-1, -1), (-32768, -32768), (32767, 32767), (self.w, self.h),
                               (r.randint(-32768, 32767), r.randint(-32768, 32767))))
        elif mut == 'srcedge':
            sx, sy = self.w - r.randint(0, max(1, w)), self.h - r.randint(0, max(1, h))
        elif mut == 'overlap':
            sx, sy = x + r.randint(-3, 3), y + r.randint(-3, 3)
        body = struct.pack('>hh', max(-32768, min(32767, sx)), max(-32768, min(32767, sy)))
        if mut == 'trunc':
            body = body[:r.randint(0, 3)]
        return self.rect(x, y, w, h, body), 'copyrect' + ('/' + mut if mut else '')

    # ---- RichCursor --------------------------------------------------------------
    def rect_richcursor(self, mutate):
        r, pf = self.rng, self.pf
        roll = r.random()
        if not mutate and roll < 0.25:
            return self.raw_rect()
        if roll < 0.45:
            if mutate:
                px, py = r.choice(((65535, 65535), (self.w, self.h), (r.randrange(65536), r.randrange(65536))))
            else:
                px, py = r.randint(0, max(0, self.w - 1)), r.randint(0, max(0, self.h - 1))
            return header(px, py, 0, 0, ENCODINGS['pointerpos']), 'pointerpos' + ('/oob' if mutate else '')
        w, h = r.randint(1, 64), r.randint(1, 64)
        hx, hy = r.randrange(w), r.randrange(h)
        mut = r.choice(('hotoob', 'zerow', 'zeroh', 'large', 'toolarge', 'trunc', 'maskshort')) if mutate else None
        if mut == 'hotoob':
            hx, hy = r.choice(((65535, 65535), (w, h), (r.randrange(65536), 0)))
        elif mut == 'zerow':
            w = 0
        elif mut == 'zeroh':
            h = 0
        elif mut == 'large':
            w, h = r.choice(((256, 256), (1024, 512), (2048, 1), (1, 2048), (4096, 1024)))
            hx, hy = r.randrange(w), r.randrange(h)
        elif mut == 'toolarge':
            w, h = r.choice(((65535, 65535), (46341, 46341), (32768, 16385)))
            return header(0, 0, w, h, ENCODINGS['richcursor']), 'richcursor/toolarge'
        if w * h > 65536:
            pixels = self.randbytes(w * h * pf.bypp)
        elif w * h:
            pal = [pf.pixel(c) for c in self.colors(4)]
            pixels = b''.join(pal[i] for i in self.indices(w * h, 4))
        else:
            pixels = b''
        mask = self.randbytes(((w + 7) // 8) * h) if w * h else b''
        if mut == 'trunc':
            pixels = pixels[:r.randrange(max(1, len(pixels)))]
            mask = b''
        elif mut == 'maskshort':
            mask = mask[:r.randrange(max(1, len(mask)))]
        return header(hx, hy, w, h, ENCODINGS['richcursor']) + pixels + mask, \
            'richcursor %dx%d' % (w, h) + ('/' + mut if mut else '')

    # ---- DesktopSize -------------------------------------------------------------
    def rect_desktopsize(self, mutate):
        r = self.rng
        roll = r.random()
        if not mutate and roll < 0.45:
            return self.raw_rect()
        if not mutate and roll < 0.6:
            data, desc = self.rect_copyrect(False)
            return data, desc
        mut = r.choice(('zero', 'zerodim', 'toolarge', 'large', 'stale', 'many', 'xy')) if mutate else None
        nw, nh = r.randint(1, 1024), r.randint(1, 768)
        if mut == 'zero':
            nw = nh = 0
        elif mut == 'zerodim':
            nw, nh = r.choice(((0, nh), (nw, 0)))
        elif mut == 'toolarge':
            nw, nh = r.choice(((65535, 65535), (46341, 46341), (32768, 16384)))
            return header(0, 0, nw, nh, ENCODINGS['desktopsize']), 'desktopsize/toolarge'
        elif mut == 'large':
            nw, nh = r.choice(((4096, 2160), (8192, 4096), (16383, 1), (1, 16383), (7680, 4320)))
        elif mut == 'many':
            out = bytearray()
            for _ in range(r.randint(3, 8)):
                nw, nh = r.randint(0, 2048), r.randint(0, 1536)
                out += header(0, 0, nw, nh, ENCODINGS['desktopsize'])
            self.w, self.h = nw, nh
            self.extra_rects += len(out) // 12 - 1
            return bytes(out), 'desktopsize/many'
        x = y = 0
        if mut == 'xy':
            x, y = r.randrange(65536), r.randrange(65536)
        if mut != 'stale':
            self.w, self.h = nw, nh
        return header(x, y, nw, nh, ENCODINGS['desktopsize']), 'desktopsize %dx%d' % (nw, nh) + \
            ('/' + mut if mut else '')


class Scenario:
    """Hand-written regression sequence with the Fuzzer interface.

    Each builder returns the list of updates for the current pixel format and
    frame buffer size; the last update is repeated up to --messages.
    """

    BUILDERS = {}

    def __init__(self, name, width, height, pf):
        self.name, self.w, self.h, self.pf = name, width, height, pf
        self.descs = []

    def next_message(self, index):
        messages = self.BUILDERS[self.name](self)
        self.descs.append('m%d:%s' % (index, self.name))
        return messages[min(index, len(messages) - 1)]

    @staticmethod
    def update(*rects):
        return b'\x00\x00' + struct.pack('>H', len(rects)) + b''.join(rects)

    def fillrect_rre_bottom_edge(self):
        # RRE subrectangle of height 0 starting on the bottom edge of the frame buffer.
        sub = self.pf.pixel((255, 0, 0)) + struct.pack('>HHHH', 0, self.h, self.w, 0)
        body = struct.pack('>I', 1) + self.pf.pixel((0, 0, 255)) + sub
        return [self.update(header(0, 0, self.w, self.h, ENCODINGS['rre']) + body)]

    def fillrect_tight_bottom_edge(self):
        # Tight fill rectangle of height 0 on the bottom edge (it passes the bounds check).
        body = b'\x80' + self.pf.tight_cpixel((0, 255, 0))
        return [self.update(header(0, self.h, self.w, 0, ENCODINGS['tight']) + body)]


Scenario.BUILDERS = {
    'fillrect-rre-bottom-edge': Scenario.fillrect_rre_bottom_edge,
    'fillrect-tight-bottom-edge': Scenario.fillrect_tight_bottom_edge,
}
SCENARIO_FB = (64, 48)


def recv_exact(sock, count):
    chunks = []
    while count:
        chunk = sock.recv(min(count, 1 << 16))
        if not chunk:
            raise ConnectionError('client closed the connection')
        chunks.append(chunk)
        count -= len(chunk)
    return b''.join(chunks)


class Session:
    def __init__(self, sock, fuzzer):
        self.sock, self.fuzzer = sock, fuzzer
        self.requests = threading.Semaphore(0)
        self.closed = threading.Event()

    def read_loop(self):
        sock = self.sock
        try:
            while True:
                kind = recv_exact(sock, 1)[0]
                if kind == 0:
                    self.fuzzer.pf = PixelFormat.parse(recv_exact(sock, 19)[3:])
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
                    while sock.recv(65536):     # unknown message: just drain
                        pass
                    break
        except (ConnectionError, OSError, ValueError, struct.error):
            pass
        self.closed.set()


def handshake(sock, width, height, pf):
    sock.sendall(b'RFB 003.008\n')
    version = recv_exact(sock, 12)
    if version[:4] != b'RFB ':
        raise ValueError('not an RFB client')
    minor = int(version[8:11])
    if minor >= 7:
        sock.sendall(bytes([1, 1]))
        if recv_exact(sock, 1) != b'\x01':
            raise ValueError('client rejected security type None')
        if minor >= 8:
            sock.sendall(struct.pack('>I', 0))
    else:
        sock.sendall(struct.pack('>I', 1))
    recv_exact(sock, 1)
    name = b'FarSightVNC fuzz server'
    sock.sendall(struct.pack('>HH', width, height) + pf.pack() + struct.pack('>I', len(name)) + name)


def main():
    parser = argparse.ArgumentParser(description=__doc__.split('\n')[0])
    parser.add_argument('--port', type=int, required=True)
    parser.add_argument('--encoding', choices=FUZZ_ENCODINGS)
    parser.add_argument('--seed', type=int)
    parser.add_argument('--scenario', choices=sorted(Scenario.BUILDERS),
                        help='send a fixed regression sequence instead of --encoding/--seed')
    parser.add_argument('--messages', type=int, default=25, help='updates to send (default 25)')
    parser.add_argument('--wait', type=float, default=0.3, help='max seconds to wait for each update request')
    parser.add_argument('--linger', type=float, default=0.8, help='seconds to keep the socket open at the end')
    parser.add_argument('--duration', type=float, default=20.0, help='hard limit for the whole run in seconds')
    args = parser.parse_args()
    if not 1024 <= args.port <= 65535:
        parser.error('invalid port')

    if args.scenario:
        width, height = SCENARIO_FB
        pf = SERVER_PIXEL_FORMATS[0]
        fuzzer = Scenario(args.scenario, width, height, pf)
        label = 'scenario=%s' % args.scenario
    else:
        if args.encoding is None or args.seed is None:
            parser.error('--encoding and --seed are required without --scenario')
        setup = random.Random('setup:%s:%d' % (args.encoding, args.seed))
        width, height = setup.choice(((320, 240), (320, 240), (640, 480), (64, 64), (100, 37), (13, 7),
                                      (1, 1), (2048, 16), (17, 300)))
        pf = SERVER_PIXEL_FORMATS[0] if setup.random() < 0.7 else setup.choice(SERVER_PIXEL_FORMATS)
        fuzzer = Fuzzer(args.encoding, args.seed, width, height, pf)
        label = '%s seed=%d' % (args.encoding, args.seed)
    deadline = time.monotonic() + args.duration

    listener = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    listener.bind(('127.0.0.1', args.port))   # loopback only, never 0.0.0.0
    listener.listen(1)
    listener.settimeout(0.2)
    print('LISTENING 127.0.0.1:%d %s fb=%dx%d pf=%s' % (args.port, label, width, height, pf.describe()),
          flush=True)
    sock = None
    while sock is None and time.monotonic() < deadline:
        try:
            sock, _ = listener.accept()
        except socket.timeout:
            pass
    listener.close()
    if sock is None:
        print('DONE no client', flush=True)
        return 2
    sock.setsockopt(socket.IPPROTO_TCP, socket.TCP_NODELAY, 1)
    sent, end = 0, 'completed'
    try:
        handshake(sock, width, height, pf)
        session = Session(sock, fuzzer)
        threading.Thread(target=session.read_loop, daemon=True).start()
        session.requests.acquire(timeout=3.0)      # first FramebufferUpdateRequest
        for index in range(args.messages):
            if session.closed.is_set() or time.monotonic() > deadline:
                end = 'client closed' if session.closed.is_set() else 'deadline'
                break
            sock.sendall(fuzzer.next_message(index))
            sent += 1
            session.requests.acquire(timeout=args.wait)
        else:
            session.closed.wait(min(args.linger, max(0.0, deadline - time.monotonic())))
    except (ConnectionError, OSError, ValueError) as error:
        end = 'socket: %s' % error.__class__.__name__
    finally:
        try:
            sock.shutdown(socket.SHUT_RDWR)
        except OSError:
            pass
        sock.close()
    print('DONE sent=%d/%d end=%s pf=%s last=%s' % (sent, args.messages, end, fuzzer.pf.describe(),
                                                     ' | '.join(fuzzer.descs[-4:])), flush=True)
    return 0


if __name__ == '__main__':
    sys.exit(main())
