"""Sound effect banks of MONOSFX (.mfx), in Python: the same format and the same steps as sdk/msfx.c
(see sdk/MSFX.md). Any change is made here and in sdk/msfx.c / sdk/msfx.h together.
"""
import math, random, struct, wave

MAGIC = b'MFX1'
EFFECTS = 32
SQUARE, TRIANGLE, SAW, NOISE = 0, 1, 2, 3
WAVE_NAMES = ['carre', 'triangle', 'dents', 'bruit']
FIELDS = ['wave', 'volume', 'attack', 'sustain', 'decay', 'duty', 'duty_sweep', 'repeat', 'freq', 'slide', 'accel',
          'vib_depth', 'vib_speed', 'arp', 'arp_time', 'flags']
FMT = '<8s6BbBHhhBBbBB5x'
SEMITONE = [65536, 69433, 73562, 77936, 82570, 87480, 92682, 98193, 104032, 110218, 116772, 123715]


class Effect:
    def __init__(self, name='', **kw):
        self.name = name
        self.wave, self.volume, self.attack, self.sustain, self.decay, self.duty = SQUARE, 160, 0, 6, 12, 128
        self.duty_sweep = self.repeat = self.slide = self.accel = self.vib_depth = self.vib_speed = 0
        self.arp = self.arp_time = self.flags = 0
        self.freq = 440
        for k, v in kw.items():
            if k not in FIELDS:
                raise KeyError(k)
            setattr(self, k, v)

    def pack(self):
        return struct.pack(FMT, self.name.encode('ascii', 'replace')[:8], self.wave, self.volume, self.attack, self.sustain,
                           self.decay, self.duty, self.duty_sweep, self.repeat, self.freq, self.slide, self.accel,
                           self.vib_depth, self.vib_speed, self.arp, self.arp_time, self.flags)

    @classmethod
    def unpack(cls, b):
        v = struct.unpack(FMT, b)
        e = cls(v[0].split(b'\0')[0].decode('ascii', 'replace'))
        for k, x in zip(FIELDS, v[1:]):
            setattr(e, k, x)
        return e

    def length(self):
        return max(1, self.attack + self.sustain + self.decay)

    def steps(self):
        """(frequency in 1/16 Hz, volume, duty) frame by frame: exactly ms_voice_step"""
        f, sl, duty, out = self.freq * 16, self.slide, self.duty, []
        a, s, d, n = self.attack, self.sustain, self.decay, self.length()
        for t in range(n):
            if t < a: vl = self.volume * (t + 1) // (a + 1)
            elif t < a + s: vl = self.volume
            else: vl = self.volume * (n - t) // (d + 1)
            if self.repeat and t and t % self.repeat == 0:
                f, sl = self.freq * 16, self.slide
            if self.arp_time and t == self.arp_time:
                k, octv = self.arp, 0
                while k < 0: k += 12; octv -= 1
                while k >= 12: k -= 12; octv += 1
                r = f * SEMITONE[k] >> 16
                f = r << octv if octv >= 0 else r >> -octv
            fo = f
            if self.vib_speed and self.vib_depth:
                p, half = t % self.vib_speed, self.vib_speed
                tri = (p * 4 if p * 2 < half else (half - p) * 4) - half
                fo += int(self.vib_depth * 16 * tri / half)   # C: truncates towards zero
            fo = min(max(fo, 16 * 20), 16 * 20000)
            out.append((fo, max(0, min(255, vl)), min(max(duty, 8), 248)))
            f += sl; sl += self.accel; duty += self.duty_sweep
            f = max(f, 16 * 20)
        return out

    def samples(self, rate=48000):
        """an approximation of the PSG, for listening on a computer"""
        out, phase, lfsr, noise = [], 0.0, 0x7FFF, 1.0
        for f16, vl, duty in self.steps():
            f = f16 / 16
            for _ in range(rate // 60):
                phase += f / rate
                if phase >= 1:
                    phase -= 1
                    if self.wave == NOISE:
                        bit = (lfsr ^ (lfsr >> 1)) & 1
                        lfsr = (lfsr >> 1) | (bit << 14)
                        noise = 1.0 if lfsr & 1 else -1.0
                if self.wave == SQUARE: v = 1.0 if phase < duty / 256 else -1.0
                elif self.wave == TRIANGLE: v = 4 * phase - 1 if phase < 0.5 else 3 - 4 * phase
                elif self.wave == SAW: v = 2 * phase - 1
                else: v = noise
                out.append(v * vl / 255 * 0.6)
        return out

    def write_wav(self, path, rate=48000):
        s = self.samples(rate)
        w = wave.open(path, 'wb')
        w.setnchannels(1); w.setsampwidth(2); w.setframerate(rate)
        w.writeframes(b''.join(struct.pack('<h', int(max(-1, min(1, x)) * 32000)) for x in s))
        w.close()


class Bank:
    def __init__(self, name='SANS NOM', effects=None):
        self.name, self.effects = name, list(effects or [])

    def to_bytes(self):
        body = b''.join(e.pack() for e in self.effects[:EFFECTS])
        hdr = MAGIC + struct.pack('<IBBH', 32 + len(body), len(self.effects[:EFFECTS]), 1, 0)
        hdr += self.name.encode('ascii', 'replace')[:16].ljust(16, b'\0') + bytes(4)
        return hdr + body

    @classmethod
    def from_bytes(cls, b):
        if b[:4] != MAGIC:
            raise ValueError('not a MONOSFX bank (MFX1)')
        size, n, _v, _r = struct.unpack_from('<IBBH', b, 4)
        if n > EFFECTS or size < 32 + 32 * n:
            raise ValueError('bank header out of range')
        return cls(b[12:28].split(b'\0')[0].decode('ascii', 'replace'),
                   [Effect.unpack(b[32 + 32 * i:64 + 32 * i]) for i in range(n)])

    def find(self, name):
        for i, e in enumerate(self.effects):
            if e.name == name:
                return i
        return -1


# ---------------------------------------------------------------- NVRAM of MONOSFX
# "MSNV", u8 keyboard layout, u8 last slot, then 8 x {u32 offset, u32 size} from 16, banks from 80
NV_SIZE, NV_SLOTS, NV_DATA = 0x8000, 8, 80


def read_nvram(data):
    if data[:4] != b'MSNV':
        raise ValueError('this save is not the NVRAM of MONOSFX (MSNV)')
    out = []
    for k in range(NV_SLOTS):
        off, size = struct.unpack_from('<II', data, 16 + 8 * k)
        if size and off + size <= len(data) and data[off:off + 4] == MAGIC:
            out.append((k, bytes(data[off:off + size])))
    return out


def write_nvram(data, slot, bank_bytes):
    nv = (bytearray(data) if data else bytearray(NV_SIZE)) + bytes(NV_SIZE)
    nv = nv[:NV_SIZE]
    if nv[:4] != b'MSNV':
        nv[:NV_DATA] = bytes(NV_DATA); nv[0:4] = b'MSNV'; nv[5] = 0xFF
    banks = dict(read_nvram(nv)); banks[slot] = bytes(bank_bytes)
    o, body, table = NV_DATA, bytearray(), bytearray(8 * NV_SLOTS)
    for k in sorted(banks):
        b = banks[k]
        struct.pack_into('<II', table, 8 * k, o, len(b))
        body += b + bytes((-len(b)) % 4); o += len(b) + (-len(b)) % 4
    nv[16:16 + 8 * NV_SLOTS] = table
    nv[NV_DATA:] = bytes(NV_SIZE - NV_DATA)
    nv[NV_DATA:NV_DATA + len(body)] = body
    nv[5] = slot
    return bytes(nv)
