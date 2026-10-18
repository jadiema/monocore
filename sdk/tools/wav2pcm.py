#!/usr/bin/env python3
"""Convert a WAV file into a MONOCORE-1 sample.

usage: wav2pcm.py in.wav out.raw [s8|s16|adpcm]

Mixes to mono and keeps the file's sample rate. Prints the sample count and the STEP value to
put in the PCM voice (0x10000 = 48 kHz). ADPCM is the OKI MSM6295 format (4 bits, high nibble first).
"""
import struct, sys, wave

src, dst = sys.argv[1], sys.argv[2]
fmt = sys.argv[3] if len(sys.argv) > 3 else 's16'
w = wave.open(src)
ch, width, rate, n = w.getnchannels(), w.getsampwidth(), w.getframerate(), w.getnframes()
raw = w.readframes(n)
vals = []
for i in range(n):
    acc = 0
    for c in range(ch):
        o = (i * ch + c) * width
        if width == 1: v = (raw[o] - 128) / 128
        elif width == 2: v = struct.unpack_from('<h', raw, o)[0] / 32768
        elif width == 3: v = (int.from_bytes(raw[o:o + 3], 'little', signed=True)) / 8388608
        else: v = struct.unpack_from('<i', raw, o)[0] / 2147483648
        acc += v
    vals.append(acc / ch)

if fmt == 's8':
    data = bytes(max(-128, min(127, round(v * 127))) & 0xff for v in vals); count = n
elif fmt == 's16':
    data = b''.join(struct.pack('<h', max(-32768, min(32767, round(v * 32767)))) for v in vals); count = n
elif fmt == 'adpcm':
    steps = [int(16.0 * (1.1 ** i)) for i in range(49)]
    adj = [-1, -1, -1, -1, 2, 4, 6, 8]
    sig, idx, nibs = 0, 0, []
    for v in vals:
        step = steps[idx]; diff = int(v * 2047) - sig
        neg = diff < 0
        q = min(7, max(0, round((abs(diff) * 8 / step - 1) / 2)))
        d = ((2 * q + 1) * step) >> 3
        sig = max(-2048, min(2047, sig - d if neg else sig + d))
        idx = max(0, min(48, idx + adj[q]))
        nibs.append(q | (8 if neg else 0))
    count = len(nibs)
    if len(nibs) % 2: nibs.append(0)
    data = bytes((nibs[i] << 4) | nibs[i + 1] for i in range(0, len(nibs), 2))
else:
    sys.exit('format: s8, s16 or adpcm')
open(dst, 'wb').write(data)
print('%s: %d samples at %d Hz, %s, %d bytes, STEP = 0x%X' % (dst, count, rate, fmt, len(data), (rate << 16) // 48000))
