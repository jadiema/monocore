"""Songs of MONOTRACKER in Python: compact format (.mts) and NVRAM of the tracker (.sav).

The formats are described in sdk/MTRACK.md; they match sdk/mtrack.c byte for byte.
"""
import struct

ROWS, TRACKS, NINS, MAGIC = 64, 16, 32, b'MTS1'
NOTE_OFF = 97
NOTES = ['C-', 'C#', 'D-', 'D#', 'E-', 'F-', 'F#', 'G-', 'G#', 'A-', 'A#', 'B-']
INS_FIELDS = ('type wave duty duty_sweep volume pan attack decay sustain release vib_speed vib_depth '
              'vib_delay arp1 arp2 arp_speed pitch_sweep finetune transpose flags').split()
INS_FMT = '<10sBBBbBBBBBBBBBbbBbbbB2x'
SIGNED = {'duty_sweep', 'arp1', 'arp2', 'pitch_sweep', 'finetune', 'transpose'}
PSG, PCM = 0, 1
SQUARE, TRIANGLE, SAW, NOISE = 0, 1, 2, 3
BOUNCE, FIXED = 1, 2

def note(name):
    """'C-4', 'C#4', 'A#2' -> 1-96; '===' -> note off"""
    if name in ('===', 'OFF'): return NOTE_OFF
    return NOTES.index(name[:2]) + 12 * int(name[2]) + 1

def note_name(n):
    if n == 0: return '...'
    if n == NOTE_OFF: return '==='
    return NOTES[(n - 1) % 12] + str((n - 1) // 12)

def cell(note=0, inst=0, vol=None, fx=None, param=0):
    """vol 0-64 or None; fx 0-15 or None"""
    v = 0 if vol is None else vol + 1
    f = 0 if fx is None else fx + 1
    return note | (inst << 7) | (v << 12) | (f << 19) | (param << 24)

class Instrument:
    def __init__(self, name='', **kw):
        self.name = name
        d = dict(type=PSG, wave=0, duty=128, duty_sweep=0, volume=64, pan=128, attack=0, decay=0,
                 sustain=64, release=0, vib_speed=0, vib_depth=0, vib_delay=0, arp1=0, arp2=0,
                 arp_speed=0, pitch_sweep=0, finetune=0, transpose=0, flags=0)
        d.update(kw)
        self.__dict__.update(d)
    def pack(self):
        return struct.pack(INS_FMT, self.name.encode('ascii')[:10], *[getattr(self, f) for f in INS_FIELDS])
    @classmethod
    def unpack(cls, b):
        v = struct.unpack(INS_FMT, b)
        return cls(v[0].rstrip(b'\0').decode('ascii', 'replace'), **dict(zip(INS_FIELDS, v[1:])))

class Song:
    def __init__(self, name='SANS NOM'):
        self.name, self.speed, self.bpm, self.master, self.restart = name, 6, 125, 48, 0
        self.ins = [Instrument.unpack(bytes(32)) for _ in range(NINS)]   # empty slots: all zero
        self.orders = [0]
        self.patterns = []              # each: list of ROWS lists of TRACKS cells
    def pattern(self, n):
        while len(self.patterns) <= n: self.patterns.append([[0] * TRACKS for _ in range(ROWS)])
        return self.patterns[n]

    # ------------------------------------------------------------ compact format
    def encode(self):
        n_ins = max([i + 1 for i in range(NINS) if self.ins[i].pack() != bytes(32)] + [1])
        n_pat = max(len(self.patterns), max(self.orders) + 1)
        body = b''.join(self.ins[i].pack() for i in range(n_ins)) + bytes(self.orders)
        head_len = 36 + len(body) + 2 * n_pat
        pats, offs = b'', []
        for p in range(n_pat):
            offs.append(head_len + len(pats))
            pats += encode_pattern(self.pattern(p))
        size = head_len + len(pats)
        if size > 0xFFFF: raise ValueError('song too big (%d bytes)' % size)
        head = MAGIC + struct.pack('<IBBBBBBBB4x16s', size, len(self.orders), n_pat, self.speed, self.bpm,
                                   self.restart, self.master, n_ins, 0, self.name.encode('ascii')[:16])
        return head + body + b''.join(struct.pack('<H', o) for o in offs) + pats

    @classmethod
    def decode(cls, b):
        if b[:4] != MAGIC: raise ValueError('not a MONOTRACKER song')
        size, n_ord, n_pat, speed, bpm, restart, master, n_ins, flags, name = struct.unpack_from('<IBBBBBBBB4x16s', b, 4)
        s = cls(name.rstrip(b'\0').decode('ascii', 'replace'))
        s.speed, s.bpm, s.restart, s.master = speed, bpm, restart, master
        p = 36
        for i in range(n_ins): s.ins[i] = Instrument.unpack(b[p:p + 32]); p += 32
        s.orders = list(b[p:p + n_ord]); p += n_ord
        offs = struct.unpack_from('<%dH' % n_pat, b, p)
        s.patterns = [decode_pattern(b, o) for o in offs]
        return s

def encode_pattern(rows):
    out, tmask = b'', 0
    for t in range(TRACKS):
        m = 0
        for r in range(ROWS):
            if rows[r][t]: m |= 1 << r
        if not m: continue
        tmask |= 1 << t
        out += struct.pack('<Q', m) + b''.join(struct.pack('<I', rows[r][t]) for r in range(ROWS) if rows[r][t])
    return struct.pack('<H', tmask) + out

def decode_pattern(b, p):
    rows = [[0] * TRACKS for _ in range(ROWS)]
    tmask, = struct.unpack_from('<H', b, p); p += 2
    for t in range(TRACKS):
        if not tmask & (1 << t): continue
        m, = struct.unpack_from('<Q', b, p); p += 8
        for r in range(ROWS):
            if m & (1 << r): rows[r][t], = struct.unpack_from('<I', b, p); p += 4
    return rows

# ---------------------------------------------------------------- NVRAM of the tracker
NV_MAGIC, NV_SLOTS, NV_DATA, NV_SIZE = b'MTNV', 8, 80, 0x8000

def read_nvram(data):
    """returns a list of NV_SLOTS songs (bytes) or None for the empty slots"""
    if data[:4] != NV_MAGIC: raise ValueError('this .sav does not come from MONOTRACKER')
    out = []
    for k in range(NV_SLOTS):
        off, size = struct.unpack_from('<II', data, 16 + 8 * k)
        out.append(bytes(data[off:off + size]) if size else None)
    return out

def write_nvram(songs, layout=0):
    head = bytearray(NV_DATA); head[:4] = NV_MAGIC; head[4] = layout
    body, off = b'', NV_DATA
    for k in range(NV_SLOTS):
        s = songs[k] if k < len(songs) else None
        if s:
            struct.pack_into('<II', head, 16 + 8 * k, off + len(body), len(s)); body += s
            if len(body) % 4: body += bytes(4 - len(body) % 4)
    data = bytes(head) + body
    if len(data) > NV_SIZE: raise ValueError('songs too big for the NVRAM')
    return data + bytes(NV_SIZE - len(data))
