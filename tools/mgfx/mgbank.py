"""Graphics banks of MONOPIXEL (.mgf), in Python: the same format as sdk/mgfx.c (see sdk/MGFX.md).

Any change of the format is made here and in sdk/mgfx.c / sdk/mgfx.h together.
"""
import struct

MAGIC = b'MGF1'
PAGES, SHEET_W, PAGE_CELLS, CELL_BYTES = 4, 16, 256, 32
PAGE_BYTES = PAGE_CELLS * CELL_BYTES
PALS, SPRITES, MAPS, MAP_CELLS, FRAMES = 16, 64, 4, 4096, 16
SF_PINGPONG = 1
OBJECTS = 64
HF_ATTRS, HF_OBJECTS = 1, 2
# tile attributes (one byte per cell; a 16-pixel tile has those of its top-left cell)
SOLID, PLATFORM, LADDER, WATER, DANGER, USER_A, USER_B, USER_C = 1, 2, 4, 8, 16, 32, 64, 128
ATTR_NAMES = {SOLID: 'solide', PLATFORM: 'plateforme', LADDER: 'echelle', WATER: 'eau', DANGER: 'danger',
              USER_A: 'A', USER_B: 'B', USER_C: 'C'}


def pack(src):
    """PackBits-like: c < 128: c + 1 literal bytes; c >= 128: the next byte, c - 125 times (3-130)"""
    src = bytes(src)
    out, i, lit, n = bytearray(), 0, 0, len(src)

    def flush(upto):
        nonlocal lit
        while lit < upto:
            k = min(128, upto - lit)
            out.append(k - 1); out.extend(src[lit:lit + k]); lit += k
    while i < n:
        r = 1
        while i + r < n and r < 130 and src[i + r] == src[i]:
            r += 1
        if r >= 3:
            flush(i)
            out.append(r + 125); out.append(src[i])
            i += r; lit = i
        else:
            i += r
            if i - lit >= 128:
                out.append(127); out.extend(src[lit:lit + 128]); lit += 128
    flush(n)
    return bytes(out)


def unpack(src, n):
    out, i = bytearray(), 0
    while i < len(src) and len(out) < n:
        c = src[i]; i += 1
        if c < 128:
            out.extend(src[i:i + c + 1]); i += c + 1
        else:
            out.extend(bytes([src[i]]) * (c - 125)); i += 1
    if len(out) != n or i != len(src):
        raise ValueError('packed data of the wrong size')
    return out


# the default palette of a new bank (palette 0; MONOPIXEL fills every palette with it)
DEFAULT_PALETTE = [(0, 0, 0), (16, 14, 24), (72, 52, 40), (140, 96, 60), (220, 170, 110), (34, 70, 52), (60, 140, 70),
                   (140, 210, 90), (36, 44, 100), (60, 110, 200), (120, 200, 240), (245, 245, 240), (150, 150, 165),
                   (85, 85, 105), (250, 210, 70), (220, 70, 60)]


def rgb555(r, g, b):
    return (r >> 3) << 10 | (g >> 3) << 5 | (b >> 3)


def rgb888(c):
    r, g, b = (c >> 10) & 31, (c >> 5) & 31, c & 31
    return (r << 3 | r >> 2, g << 3 | g >> 2, b << 3 | b >> 2)


class Sprite:
    FMT = '<8B8s'

    def __init__(self, x=0, y=0, w=1, h=1, pal=0, frames=1, speed=0, flags=0, name=''):
        self.x, self.y, self.w, self.h, self.pal = x, y, w, h, pal
        self.frames, self.speed, self.flags, self.name = frames, speed, flags, name

    def pack(self):
        return struct.pack(self.FMT, self.x, self.y, self.w, self.h, self.pal, self.frames, self.speed,
                           self.flags, self.name.encode('ascii', 'replace')[:8])

    @classmethod
    def unpack(cls, b):
        x, y, w, h, pal, fr, sp, fl, nm = struct.unpack(cls.FMT, b)
        return cls(x, y, w, h, pal, fr, sp, fl, nm.split(b'\0')[0].decode('ascii', 'replace'))

    def frame_pos(self, f):
        """the image f: to the right of the previous one, else at the start of the next band"""
        x, y = self.x, self.y
        for _ in range(f):
            x += self.w
            if x + self.w > SHEET_W:
                x, y = 0, y + self.h
        return x, y


class Object:
    """a sprite of the bank placed in a map (pixels from the top left of the map)"""
    def __init__(self, x, y, sprite, param=0, flags=0):
        self.x, self.y, self.sprite, self.param, self.flags = x, y, sprite, param, flags


class Map:
    def __init__(self, w=32, h=32, tile16=False, cells=None):
        assert w in (32, 64, 128) and h in (32, 64, 128) and w * h <= MAP_CELLS
        self.w, self.h, self.tile16 = w, h, tile16
        self.cells = list(cells) if cells else [0] * (w * h)
        self.objects = []

    @staticmethod
    def entry(tile, pal=0, hflip=False, vflip=False):
        return tile | pal << 10 | (0x4000 if hflip else 0) | (0x8000 if vflip else 0)

    def set(self, x, y, e):
        self.cells[(y % self.h) * self.w + x % self.w] = e

    def get(self, x, y):
        return self.cells[(y % self.h) * self.w + x % self.w]


class Bank:
    def __init__(self, name='SANS NOM', pages=1):
        self.name = name
        self.sheet = bytearray(PAGE_BYTES * pages)
        self.pals = [[rgb555(*c) if i else 0 for i, c in enumerate(DEFAULT_PALETTE)]]
        self.sprites = []
        self.maps = []
        self.attrs = bytearray(PAGE_CELLS * pages)

    def tile_attr(self, tile, tile16=True):
        cell = (tile % 8) * 2 + (tile // 8) * 32 if tile16 else tile
        return self.attrs[cell] if cell < len(self.attrs) else 0

    def set_tile_attr(self, tile, a, tile16=True):
        cell = (tile % 8) * 2 + (tile // 8) * 32 if tile16 else tile
        self.attrs[cell] = a

    def sprite_number(self, name):
        for i, s in enumerate(self.sprites):
            if s.name == name:
                return i
        raise KeyError(name)

    # ------------------------------------------------------------ pixels of the sheet
    @property
    def pages(self):
        return len(self.sheet) // PAGE_BYTES

    def set_pages(self, n):
        n = max(1, min(PAGES, n))
        self.sheet = (self.sheet + bytes(PAGE_BYTES * n))[:PAGE_BYTES * n]
        self.attrs = (self.attrs + bytes(PAGE_CELLS * n))[:PAGE_CELLS * n]

    def _addr(self, x, y):
        cell = (y >> 3) * SHEET_W + (x >> 3)
        return cell * CELL_BYTES + (y & 7) * 4 + ((x & 7) >> 1)

    def pixel(self, x, y):
        b = self.sheet[self._addr(x, y)]
        return (b >> 4) if x & 1 else b & 15

    def set_pixel(self, x, y, c):
        if y >= self.pages * 128:
            self.set_pages(y // 128 + 1)
        a = self._addr(x, y)
        b = self.sheet[a]
        self.sheet[a] = (b & 0x0F) | (c << 4) if x & 1 else (b & 0xF0) | c

    # ------------------------------------------------------------ file
    def to_bytes(self):
        while len(self.pals) < 1:
            self.pals.append([0] * 16)
        body = bytearray()
        for p in self.pals:
            body += struct.pack('<16H', *p)
        for s in self.sprites:
            body += s.pack()
        for m in self.maps:
            body += struct.pack('<3B5x', {32: 0, 64: 1, 128: 2}[m.w], {32: 0, 64: 1, 128: 2}[m.h], 1 if m.tile16 else 0)
        sh = pack(self.sheet)
        body += struct.pack('<I', len(sh)) + sh
        for m in self.maps:
            d = pack(struct.pack('<%dH' % len(m.cells), *m.cells))
            body += struct.pack('<I', len(d)) + d
        flags = 0
        if any(self.attrs):
            flags |= HF_ATTRS
            d = pack(self.attrs)
            body += struct.pack('<I', len(d)) + d
        if any(m.objects for m in self.maps):
            flags |= HF_OBJECTS
            body += bytes((-(32 + len(body))) % 4)          # from a multiple of 4
            for m in self.maps:
                body += struct.pack('<HH', len(m.objects), 0)
                for o in m.objects[:OBJECTS]:
                    body += struct.pack('<HHBBBx', o.x, o.y, o.sprite, o.param, o.flags)
        size = 32 + len(body)
        hdr = MAGIC + struct.pack('<I4BBBH', size, self.pages, len(self.pals), len(self.sprites), len(self.maps),
                                  flags, 2 if flags else 1, 0)
        hdr += self.name.encode('ascii', 'replace')[:16].ljust(16, b'\0')
        return bytes(hdr + body)

    @classmethod
    def from_bytes(cls, b):
        if b[:4] != MAGIC:
            raise ValueError('not a MONOPIXEL bank (MGF1)')
        size, npg, npal, nspr, nmap, flags, _ver, _r = struct.unpack_from('<I4BBBH', b, 4)
        if not (1 <= npg <= PAGES and 1 <= npal <= PALS and nspr <= SPRITES and nmap <= MAPS):
            raise ValueError('bank header out of range')
        bank = cls(b[16:32].split(b'\0')[0].decode('ascii', 'replace'), npg)
        o = 32
        bank.pals = [list(struct.unpack_from('<16H', b, o + 32 * i)) for i in range(npal)]; o += 32 * npal
        bank.sprites = [Sprite.unpack(b[o + 16 * i:o + 16 * i + 16]) for i in range(nspr)]; o += 16 * nspr
        heads = [struct.unpack_from('<3B', b, o + 8 * i) for i in range(nmap)]; o += 8 * nmap
        n = struct.unpack_from('<I', b, o)[0]
        bank.sheet = unpack(b[o + 4:o + 4 + n], npg * PAGE_BYTES); o += 4 + n
        for wc, hc, t16 in heads:
            w, h = 32 << wc, 32 << hc
            n = struct.unpack_from('<I', b, o)[0]
            d = unpack(b[o + 4:o + 4 + n], w * h * 2); o += 4 + n
            bank.maps.append(Map(w, h, bool(t16), struct.unpack('<%dH' % (w * h), d)))
        if flags & HF_ATTRS:
            n = struct.unpack_from('<I', b, o)[0]
            bank.attrs = unpack(b[o + 4:o + 4 + n], npg * PAGE_CELLS); o += 4 + n
        if flags & HF_OBJECTS:
            o += (-o) % 4
            for m in bank.maps:
                n = struct.unpack_from('<H', b, o)[0]; o += 4
                for i in range(n):
                    x, y, sp, pa, fl = struct.unpack_from('<HHBBB', b, o); o += 8
                    m.objects.append(Object(x, y, sp, pa, fl))
        if o != size:
            raise ValueError('bank size mismatch (%d, header says %d)' % (o, size))
        return bank

    # ------------------------------------------------------------ pictures (Pillow)
    def colour(self, pal, i):
        p = self.pals[pal] if pal < len(self.pals) else self.pals[0]
        return rgb888(p[i])

    def sheet_image(self, pal=0, scale=1):
        from PIL import Image
        im = Image.new('RGBA', (128, 128 * self.pages), (0, 0, 0, 0))
        px = im.load()
        for y in range(im.height):
            for x in range(128):
                c = self.pixel(x, y)
                if c:
                    px[x, y] = self.colour(pal, c) + (255,)
        return im.resize((im.width * scale, im.height * scale), Image.NEAREST) if scale > 1 else im

    def sprite_image(self, s, frame=0, scale=1):
        from PIL import Image
        sp = self.sprites[s]
        im = Image.new('RGBA', (sp.w * 8, sp.h * 8), (0, 0, 0, 0))
        fx, fy = sp.frame_pos(frame)
        px = im.load()
        for y in range(sp.h * 8):
            for x in range(sp.w * 8):
                sy = fy * 8 + y
                c = self.pixel(fx * 8 + x, sy) if sy < self.pages * 128 else 0
                if c:
                    px[x, y] = self.colour(sp.pal, c) + (255,)
        return im.resize((im.width * scale, im.height * scale), Image.NEAREST) if scale > 1 else im

    def map_image(self, m, bg=(0, 0, 0)):
        from PIL import Image
        mp = self.maps[m]
        t = 16 if mp.tile16 else 8
        im = Image.new('RGB', (mp.w * t, mp.h * t), bg)
        px = im.load()
        for ty in range(mp.h):
            for tx in range(mp.w):
                e = mp.get(tx, ty)
                tile, pal, hf, vf = e & 0x3FF, (e >> 10) & 15, e & 0x4000, e & 0x8000
                ox, oy = ((tile % 8) * 16, (tile // 8) * 16) if mp.tile16 else ((tile % 16) * 8, (tile // 16) * 8)
                for y in range(t):
                    for x in range(t):
                        sx, sy = (t - 1 - x if hf else x), (t - 1 - y if vf else y)
                        if oy + sy >= self.pages * 128:
                            continue
                        c = self.pixel(ox + sx, oy + sy)
                        if c:
                            px[tx * t + x, ty * t + y] = self.colour(pal, c)
        for o in mp.objects:
            if o.sprite < len(self.sprites):
                spr = self.sprite_image(o.sprite)
                if o.flags & 1:
                    from PIL import ImageOps
                    spr = ImageOps.mirror(spr)
                im.paste(spr, (o.x, o.y), spr)
        return im

    def import_image(self, path, x0=0, y0=0, pal=0):
        """draws a picture into the sheet at pixel (x0, y0): its colours become palette pal (16 at
        most; transparent pixels and the first colour of an indexed picture become colour 0)"""
        from PIL import Image
        im = Image.open(path)
        colours = [None]
        if im.mode == 'P' and 'transparency' not in im.info:
            p = im.getpalette()
            colours = [tuple(p[3 * i:3 * i + 3]) for i in range(min(16, len(p) // 3))]
            idx = lambda x, y: im.getpixel((x, y)) & 15
        else:
            im = im.convert('RGBA')
            lut = {}

            def idx(x, y):
                r, g, b, a = im.getpixel((x, y))
                if a < 128:
                    return 0
                k = (r >> 3, g >> 3, b >> 3)
                if k not in lut:
                    if len(colours) >= 16:
                        raise ValueError('%s: more than 15 colours' % path)
                    lut[k] = len(colours); colours.append((r, g, b))
                return lut[k]
        for y in range(im.height):
            for x in range(im.width):
                if x0 + x < 128:
                    self.set_pixel(x0 + x, y0 + y, idx(x, y))
        while len(self.pals) <= pal:
            self.pals.append(list(self.pals[0]))
        for i, c in enumerate(colours):
            if c is not None and i:
                self.pals[pal][i] = rgb555(*c)


# ---------------------------------------------------------------- NVRAM of MONOPIXEL
# "MGNV", u8 keyboard layout, u8 last slot, 10 bytes, then 8 x {u32 offset, u32 size} from 16,
# banks from 80 (the same layout as the songs of MONOTRACKER)
NV_SIZE, NV_SLOTS, NV_DATA = 0x8000, 8, 80


def read_nvram(data):
    """the banks of a .sav, as a list of (slot 0-7, bytes) for the slots in use"""
    if data[:4] != b'MGNV':
        raise ValueError('this save is not the NVRAM of MONOPIXEL (MGNV)')
    out = []
    for k in range(NV_SLOTS):
        off, size = struct.unpack_from('<II', data, 16 + 8 * k)
        if size and off + size <= len(data) and data[off:off + 4] == MAGIC:
            out.append((k, bytes(data[off:off + size])))
    return out


def write_nvram(data, slot, bank_bytes):
    """puts a bank into a slot of a MONOPIXEL NVRAM (a new one if data is None); returns the NVRAM"""
    nv = bytearray(data) if data else bytearray(NV_SIZE)
    nv = (nv + bytes(NV_SIZE))[:NV_SIZE]
    if nv[:4] != b'MGNV':
        nv[:NV_DATA] = bytes(NV_DATA)
        nv[0:4] = b'MGNV'; nv[5] = 0xFF
    banks = {k: b for k, b in read_nvram(nv)}
    banks[slot] = bytes(bank_bytes)
    o = NV_DATA
    body = bytearray()
    table = bytearray(8 * NV_SLOTS)
    for k in sorted(banks):
        b = banks[k]
        struct.pack_into('<II', table, 8 * k, o, len(b))
        body += b + bytes((-len(b)) % 4)
        o += len(b) + (-len(b)) % 4
    if o > NV_SIZE:
        raise ValueError('the NVRAM is full (%d bytes needed, %d)' % (o, NV_SIZE))
    nv[16:16 + 8 * NV_SLOTS] = table
    nv[NV_DATA:] = bytes(NV_SIZE - NV_DATA)
    nv[NV_DATA:NV_DATA + len(body)] = body
    nv[5] = slot
    return bytes(nv)
