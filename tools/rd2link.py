#!/usr/bin/env python3
"""rd2link.py EXE Name [--api 0x0200] [--flags 0] [--stack 4096] [--kb 0] [--ver 0x0100] [--type P|M|D]
Haengt den RD2-Block und den 16-Byte-Abschluss an eine MZ-EXE (SPEC.md Abschnitt 2).
Ein vorhandener Anhang wird ersetzt. Ressourcen kommen spaeter mit RDRC dazu."""
import sys, struct, argparse
a = argparse.ArgumentParser()
a.add_argument("exe"); a.add_argument("name")
for o, d in (("api", "0x0200"), ("flags", "0"), ("stack", "4096"), ("kb", "0"), ("ver", "0x0100")):
    a.add_argument("--" + o, default=d)
a.add_argument("--type", default="P", choices="PMD")
a.add_argument("--res", default=None, help=".RES von rdrc.py")
a.add_argument("--pas", default=None, help="Pascal-Programm: Vorspann RD2PAS.BIN vor das Bild setzen")
o = a.parse_args()

def pascal(d, stub):
    """Vorspann vor das Bild, Bild um N Paragraphen verschieben (sdk/RD2PAS.ASM). Schon vorgespannt: unveraendert."""
    hdr = struct.unpack_from("<H", d, 8)[0] * 16
    if d[hdr + 0x112:hdr + 0x118] == b"RD2PAS": return d
    if len(stub) % 16 or stub[0x112:0x118] != b"RD2PAS": sys.exit("RD2PAS.BIN ungueltig")
    n = len(stub) // 16
    crlc, _, minalloc, maxalloc, ss, sp, _, ip, cs, lfarlc = struct.unpack_from("<HHHHHHHHHH", d, 6)
    img = bytearray(d[hdr:])
    for k in range(crlc):
        off, seg = struct.unpack_from("<HH", d, lfarlc + 4 * k)
        p = seg * 16 + off
        if p + 2 > len(img): sys.exit("Verweis ausserhalb des Bildes")
        struct.pack_into("<H", img, p, (struct.unpack_from("<H", img, p)[0] + n) & 0xFFFF)
        struct.pack_into("<HH", d, lfarlc + 4 * k, off, seg + n)
    if maxalloc != 0xFFFF and maxalloc > minalloc: minalloc = maxalloc    # {$M}: Heap bis HeapMax
    top = (len(stub) + len(img) + 15) // 16 + minalloc
    if top > 0xA000: sys.exit("Pascal-Programm braucht zu viel Speicher ({$M} verkleinern)")
    stub = bytearray(stub)
    struct.pack_into("<HHHHH", stub, 0x11A, ip, cs + n, sp, ss + n, top)
    ende = len(stub) + len(img)
    struct.pack_into("<HH", stub, 0x13E, ende // 16, ende % 16)   # o_endp, o_endo
    d = bytearray(d[:hdr]) + stub + img
    struct.pack_into("<HH", d, 2, len(d) % 512, (len(d) + 511) // 512)
    struct.pack_into("<H", d, 0x0A, minalloc)
    struct.pack_into("<HH", d, 0x0E, ss + n, sp)
    struct.pack_into("<HH", d, 0x14, 0x110, 0)
    return d
d = bytearray(open(o.exe, "rb").read())
if d[:2] != b"MZ": sys.exit("keine MZ-EXE")
if d[-16:-12] == b"RD2!":
    blk = struct.unpack_from("<I", d, len(d) - 12)[0]; d = d[:blk]
cblp, cp = struct.unpack_from("<HH", d, 2)
img_end = cp * 512 - ((512 - cblp) if cblp else 0)
if len(d) < img_end: sys.exit("Datei kuerzer als im MZ-Kopf angegeben")
d = d[:img_end]
if o.pas: d = pascal(d, open(o.pas, "rb").read())
i = lambda s: int(s, 0)
name = o.name.encode("cp437")[:15].ljust(16, b"\0")
res = []
if o.res:
    r = open(o.res, "rb").read(); p = 6
    if r[:4] != b"RDR1": sys.exit("keine .RES von rdrc.py")
    for _ in range(struct.unpack_from("<H", r, 4)[0]):
        typ, nr, sp, fl, ln = struct.unpack_from("<HHHHI", r, p); p += 12
        res.append((typ, nr, sp, fl, r[p:p + ln])); p += ln
blk = struct.pack("<4sHHHH16sHHHHH10s", ("RD2" + o.type).encode(), i(o.api), i(o.flags), i(o.stack), i(o.kb),
                  name, i(o.ver), 0, len(res), 48, 48, b"\0" * 10)
assert len(blk) == 48
verz, daten, pos = b"", b"", 48 + 16 * len(res)       # Verzeichnis gleich hinter dem Block, Offsets ab Blockanfang
for typ, nr, sp, fl, x in res:
    verz += struct.pack("<HHHHII", typ, nr, sp, fl, pos + len(daten), len(x)); daten += x
    if len(daten) & 1: daten += b"\0"
off = len(d)
d += blk + verz + daten + struct.pack("<4sIIHH", b"RD2!", off, len(blk) + len(verz) + len(daten), sum(blk) & 0xFFFF, 0)
open(o.exe, "wb").write(d)
print(f"{o.exe}: RD2-Anhang bei {off}, Datei {len(d)} Bytes")
