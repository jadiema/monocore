"""The NVRAM of the atelier ("ATNV", sdk/nvslots.h), in Python: 32 slots, 8 for each editor.

    "ATNV", byte 4 keyboard layout, bytes 5-7 last slot of MONOTRACKER, MONOPIXEL, MONOSFX,
    8-11 settings of the editors, 12-14 slots played by the game (0xFF: the last ones),
    32 x {u32 offset, u32 size} from 16 (editor e: slots 8e to 8e + 7), items from 272,
    the last 256 bytes: the save of the game (mj_save).
"""
import struct

MAGIC, SLOTS, TABLE, DATA, SIZE, GAME = b'ATNV', 32, 16, 16 + 8 * 32, 0x8000, 256
TRACKER, PIXEL, SFX = 0, 1, 2
APP_NAMES = {TRACKER: 'MONOTRACKER', PIXEL: 'MONOPIXEL', SFX: 'MONOSFX'}
ITEM_MAGIC = {TRACKER: b'MTS1', PIXEL: b'MGF1', SFX: b'MFX1'}
OWN_MAGIC = {TRACKER: b'MTNV', PIXEL: b'MGNV', SFX: b'MSNV'}   # the NVRAM of each editor alone


def is_atelier(data):
    return data[:4] == MAGIC


def new():
    nv = bytearray(SIZE)
    nv[0:4] = MAGIC
    nv[5:8] = b'\xff\xff\xff'
    nv[12:15] = b'\xff\xff\xff'
    return nv


def read(data, app):
    """the 8 items of an editor (bytes, or None for an empty slot)"""
    out = []
    for k in range(8):
        off, size = struct.unpack_from('<II', data, TABLE + 8 * (app * 8 + k))
        ok = size and DATA <= off and off + size <= SIZE - GAME and data[off:off + 4] == ITEM_MAGIC[app]
        out.append(bytes(data[off:off + size]) if ok else None)
    return out


def read_own(data, app):
    """the 8 items of the NVRAM of an editor alone (monopixel.sav...)"""
    if data[:4] != OWN_MAGIC[app]:
        raise ValueError('not the NVRAM of %s' % APP_NAMES[app])
    out = []
    for k in range(8):
        off, size = struct.unpack_from('<II', data, 16 + 8 * k)
        out.append(bytes(data[off:off + size]) if size and data[off:off + 4] == ITEM_MAGIC[app] else None)
    return out


def write(data, items):
    """a new atelier NVRAM from data (or None) with items = {(app, slot): bytes or None}; settings and
    the save of the game are kept"""
    nv = bytearray(data) if data and is_atelier(data) else new()
    nv = (nv + bytes(SIZE))[:SIZE]
    every = {}
    for app in (TRACKER, PIXEL, SFX):
        for k, it in enumerate(read(nv, app)):
            if it:
                every[(app, k)] = it
    every.update(items)
    table, body, o = bytearray(8 * SLOTS), bytearray(), DATA
    for (app, k) in sorted(every):
        it = every[(app, k)]
        if not it:
            continue
        struct.pack_into('<II', table, 8 * (app * 8 + k), o, len(it))
        pad = (-len(it)) % 4
        body += it + bytes(pad); o += len(it) + pad
    if o > SIZE - GAME:
        raise ValueError('the NVRAM of the atelier is full (%d bytes for %d)' % (o, SIZE - GAME))
    nv[TABLE:TABLE + 8 * SLOTS] = table
    nv[DATA:SIZE - GAME] = bytes(SIZE - GAME - DATA)
    nv[DATA:DATA + len(body)] = body
    return bytes(nv)
