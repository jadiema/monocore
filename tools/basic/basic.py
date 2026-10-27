#!/usr/bin/env python3
"""The programs of MONOBASIC, between the NVRAM save and text files (.bas).

usage:
    python3 basic.py monobasic.sav                   lists the 8 programs (also monoatelier.sav)
    python3 basic.py monobasic.sav 2 -o jeu.bas      program 2 as text
    python3 basic.py jeu.bas --to-sav monobasic.sav 3 [--name JEU]
                                                     a text file into slot 3 (created if missing; an
                                                     atelier save too: the slots of MONOBASIC)
A program in the NVRAM: "MBS1", u32 size, name (16 bytes), 4 zero bytes, then its lines as text
("10 PRINT ...", one by "\\n"). Text written on a computer is read as typed in the editor: the lines
need their numbers, and the keywords are recognised as whole words.
"""
import argparse, os, struct, sys
sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), '../atelier'))
import atnv


def own_read(data):
    out = []
    for k in range(8):
        off, size = struct.unpack_from('<II', data, 16 + 8 * k)
        out.append(bytes(data[off:off + size]) if size and data[off:off + 4] == b'MBS1' else None)
    return out


def own_write(data, slot, item):
    items = own_read(data) if data and data[:4] == b'MBNV' else [None] * 8
    nv = bytearray(data) if data and data[:4] == b'MBNV' else bytearray(0x8000)
    if nv[:4] != b'MBNV':
        nv[0:4] = b'MBNV'; nv[5] = 0xFF
    items[slot] = item
    o, body, table = 80, bytearray(), bytearray(64)
    for k, it in enumerate(items):
        if it:
            struct.pack_into('<II', table, 8 * k, o, len(it))
            pad = (-len(it)) % 4
            body += it + bytes(pad); o += len(it) + pad
    if o > 0x8000:
        sys.exit('NVRAM pleine')
    nv[16:80] = table
    nv[80:] = bytes(0x8000 - 80); nv[80:80 + len(body)] = body
    return bytes(nv)


def main():
    ap = argparse.ArgumentParser(description='MONOBASIC programs')
    ap.add_argument('file'); ap.add_argument('slot', nargs='?', type=int)
    ap.add_argument('-o', '--output'); ap.add_argument('--to-sav', nargs=2, metavar=('SAV', 'SLOT')); ap.add_argument('--name')
    a = ap.parse_args()
    data = open(a.file, 'rb').read()
    if a.to_sav:
        text = data.decode('ascii', 'replace').replace('\r', '')
        name = (a.name or os.path.splitext(os.path.basename(a.file))[0]).upper()[:16]
        body = text.encode('ascii', 'replace')
        item = b'MBS1' + struct.pack('<I', 28 + len(body)) + name.encode().ljust(16, b'\0') + bytes(4) + body
        sav, slot = a.to_sav[0], int(a.to_sav[1]) - 1
        old = open(sav, 'rb').read() if os.path.exists(sav) else None
        new = atnv.write(old, {(atnv.BASIC, slot): item}) if old and atnv.is_atelier(old) else own_write(old, slot, item)
        open(sav, 'wb').write(new)
        print('%s : %s dans l\'emplacement %d' % (sav, name, slot + 1))
        return
    items = atnv.read(data, atnv.BASIC) if atnv.is_atelier(data) else own_read(data)
    if a.slot is None:
        for k, it in enumerate(items):
            print('%d  %s' % (k + 1, '-' if not it else '%-16s %5d octets' % (it[8:24].split(b'\0')[0].decode(), len(it))))
        return
    it = items[a.slot - 1]
    if not it:
        sys.exit('emplacement %d vide' % a.slot)
    text = it[28:].decode('ascii', 'replace')
    if a.output:
        open(a.output, 'w').write(text)
        print('%s : %d lignes' % (a.output, text.count('\n')))
    else:
        sys.stdout.write(text)


if __name__ == '__main__':
    main()
