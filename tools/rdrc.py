#!/usr/bin/env python3
"""rdrc.py QUELLE.RC AUSGABE.RES      Ressourcen uebersetzen (SPEC.md Abschnitt 6)
   rdrc.py --lng QUELLE.RC AUS.LNG     Systemtexte: nur TEXTE, Kopf "RDL1" + WORD Sprache

Quelltext (CP437, ; leitet Kommentare ein, "..." mit \\t \\n \\" \\\\):
  SPRACHE DE|EN|ALLE
  MENU n           POPUP "Text" ... ENDE   ITEM "Text" id [GRAU] [HAKEN]   TRENNER   ENDE
  DIALOG n "Titel" x y w h [Stil...]   dann je Zeile:  KLASSE ["Text"] id x y w h [Stil...]   ENDE
  TEXTE            id "Text" ...  ENDE      (wird RT_STRING Nummer 1)
  ACCEL n          Taste Umschalt id ... ENDE
  SYMBOL|BITMAP|CURSOR|SCHRIFT|DATEN n "datei"   (Datei wird unveraendert uebernommen)
  BEDARF [CPU 8088|8086|186|286|386|486|586] [FPU] [FARBEN 4|16|256] [RAM kb] [XMS kb]
         [KLANG LAUTSPRECHER|OPL2|OPL3|PCM|MIDI ...] [NETZ] [3D SOFTWARE|BESCHLEUNIGER]   (eine Zeile, API 2.3)

Stile: Namen aus sdk/RD2.H (WS_*, ES_*), STANDARD = ES_DEFAULT, AN = ES_CHECKED, GRAU = WS_DISABLED,
oder Zahlen. Klassen: BUTTON CHECK RADIO STATIC EDIT TEXT LIST COMBO SCROLL PROGRESS GROUP.

.RES (nur fuer rd2link.py): "RDR1", WORD anzahl, je Eintrag WORD typ, nr, sprache, flags, DWORD laenge, Daten."""
import sys, os, re, struct

RT = dict(MENU=1, DIALOG=2, STRING=3, SYMBOL=4, ICON=4, BITMAP=5, CURSOR=6, SCHRIFT=7, FONT=7, DATEN=8, DATA=8, ACCEL=9)
RT_BEDARF = 10
B_CPU = {"8088": 0, "8086": 1, "186": 3, "286": 4, "386": 5, "486": 6, "586": 7, "PENTIUM": 7}
B_FARBEN = {"4": 1, "16": 2, "256": 3}
B_KLANG = dict(LAUTSPRECHER=1, OPL2=2, ADLIB=2, OPL3=4, PCM=8, MIDI=16)
B_3D = dict(SOFTWARE=1, BESCHLEUNIGER=2)
LANG = dict(DE=0x0407, EN=0x0409, ALLE=0)
MF_POPUP, MF_SEP, MF_END, MF_GRAY, MF_CHECK = 1, 2, 4, 8, 16
KLASSEN = "BUTTON CHECK RADIO STATIC EDIT TEXT LIST COMBO SCROLL PROGRESS GROUP".split()
OHNE_TEXT = ("EDIT", "TEXT", "LIST", "COMBO", "SCROLL", "PROGRESS")

def defines():
    d = {}
    h = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "SDK", "RD2.H")
    for l in open(h, encoding="latin-1"):
        m = re.match(r"#define\s+((?:WS|ES)_\w+)\s+(0x[0-9A-Fa-f]+|\d+)UL?", l)
        if m: d[m[1]] = int(m[2], 0)
    d.update(STANDARD=d.get("ES_DEFAULT", 0x10000), AN=d.get("ES_CHECKED", 0x20000), GRAU=d["WS_DISABLED"])
    return d

class Fehler(Exception): pass

def tokens(zeile):
    out, i = [], 0
    while i < len(zeile):
        c = zeile[i]
        if c in " \t\r\n": i += 1; continue
        if c == ";": break
        if c == '"':
            s, i = "", i + 1
            while i < len(zeile) and zeile[i] != '"':
                if zeile[i] == "\\" and i + 1 < len(zeile):
                    i += 1; s += {"t": "\t", "n": "\n", '"': '"', "\\": "\\"}.get(zeile[i], zeile[i])
                else: s += zeile[i]
                i += 1
            if i >= len(zeile): raise Fehler("Anfuehrungszeichen fehlt")
            out.append(("s", s)); i += 1; continue
        j = i
        while j < len(zeile) and zeile[j] not in " \t\r\n;\"": j += 1
        out.append(("w", zeile[i:j])); i = j
    return out

def zahl(t, defs=None):
    k, v = t
    if k != "w": raise Fehler(f"Zahl erwartet statt \"{v}\"")
    try: return int(v, 0)
    except ValueError: pass
    if defs is not None and v.upper() in defs: return defs[v.upper()]
    raise Fehler(f"Zahl erwartet statt {v}")

def text(t):
    if t[0] != "s": raise Fehler(f"Text in Anfuehrungszeichen erwartet statt {t[1]}")
    return t[1].encode("cp437") + b"\0"

def stil(ts, defs):
    v = 0
    for t in ts:
        for teil in t[1].split("|"): v |= zahl(("w", teil), defs)
    return v

def uebersetze(pfad):
    defs = defines()
    zeilen = open(pfad, encoding="cp437").read().split("\n")
    for i, z in enumerate(zeilen, 1):                     # 30.10.: Umlaute muessen CP437 sein, Latin-1-Bytes ergeben Sigma, hoch-n usw.
        if re.search(r"[A-Za-z][\u03a3\u207f\u00f7\u2500\u2553\u2584\u2580]|[\u03a3\u207f\u00f7\u2553\u2584\u2580][A-Za-z]|\"\u2500[a-z]", z):
            sys.exit("%s:%d: Latin-1-Umlaut statt CP437: %s" % (pfad, i, z.strip()[:60]))
    basis = os.path.dirname(os.path.abspath(pfad))
    res, sprache, n = [], 0, 0
    def weiter():
        nonlocal n
        while n < len(zeilen):
            t = tokens(zeilen[n]); n += 1
            if t: return t
        raise Fehler("ENDE fehlt")
    while n < len(zeilen):
        nr_zeile = n + 1
        try:
            t = tokens(zeilen[n]); n += 1
            if not t: continue
            w = t[0][1].upper()
            if w == "SPRACHE":
                if t[1][1].upper() not in LANG: raise Fehler("SPRACHE DE, EN oder ALLE")
                sprache = LANG[t[1][1].upper()]
            elif w == "MENU":
                nr = zahl(t[1]); ebene = [[]]
                while True:
                    z = weiter(); k = z[0][1].upper()
                    if k == "ENDE":
                        if len(ebene) == 1: break
                        kinder = ebene.pop(); ebene[-1][-1][3] = kinder
                    elif k == "POPUP": ebene[-1].append([MF_POPUP, 0, text(z[1]), None]); ebene.append([])
                    elif k == "ITEM":
                        f = 0
                        for x in z[3:]:
                            if x[1].upper() == "GRAU": f |= MF_GRAY
                            elif x[1].upper() == "HAKEN": f |= MF_CHECK
                            else: raise Fehler(f"unbekannt: {x[1]}")
                        ebene[-1].append([f, zahl(z[2]), text(z[1]), None])
                    elif k == "TRENNER": ebene[-1].append([MF_SEP, 0, b"\0", None])
                    else: raise Fehler(f"POPUP, ITEM, TRENNER oder ENDE erwartet statt {z[0][1]}")
                def flach(liste):
                    b = b""
                    for i, (f, id_, s, kinder) in enumerate(liste):
                        b += struct.pack("<HH", f | (MF_END if i == len(liste) - 1 else 0), id_) + s
                        if kinder is not None:
                            if not kinder: raise Fehler("leeres POPUP")
                            b += flach(kinder)
                    return b
                if not ebene[0]: raise Fehler("leeres MENU")
                res.append((RT["MENU"], nr, sprache, flach(ebene[0])))
            elif w == "DIALOG":
                nr = zahl(t[1]); titel = text(t[2]); x, y, b_, h = (zahl(v) for v in t[3:7])
                st = stil(t[7:], defs) | defs["WS_DIALOG"] | defs["WS_TITLE"]
                el = []
                while True:
                    z = weiter(); k = z[0][1].upper()
                    if k == "ENDE": break
                    if k not in KLASSEN: raise Fehler(f"unbekannte Klasse {z[0][1]}")
                    i = 1
                    if z[1][0] == "s": s = text(z[1]); i = 2
                    elif k in OHNE_TEXT: s = b"\0"
                    else: raise Fehler(f"{k} braucht einen Text")
                    if len(z) < i + 5: raise Fehler("Element: id x y w h")
                    id_ = zahl(z[i]); ex, ey, ew, eh = (zahl(v) for v in z[i + 1:i + 5])
                    es = stil(z[i + 5:], defs) | defs["WS_CHILD"] | defs["WS_VISIBLE"]
                    el.append(k.encode() + b"\0" + s + struct.pack("<HhhhhI", id_ & 0xFFFF, ex, ey, ew, eh, es))
                res.append((RT["DIALOG"], nr, sprache, struct.pack("<IhhhhH", st, x, y, b_, h, len(el)) + titel + b"".join(el)))
            elif w == "TEXTE":
                tx = {}
                while True:
                    z = weiter()
                    if z[0][1].upper() == "ENDE": break
                    id_ = zahl(z[0])
                    if id_ in tx: raise Fehler(f"Text {id_} doppelt")
                    tx[id_] = text(z[1])
                ids = sorted(tx); kopf = 2 + 4 * len(ids); b = b""; tab = struct.pack("<H", len(ids))
                for id_ in ids: tab += struct.pack("<HH", id_, kopf + len(b)); b += tx[id_]
                if kopf + len(b) > 0xFFF0: raise Fehler("TEXTE zu gross (64 KB)")
                res.append((RT["STRING"], 1, sprache, tab + b))
            elif w == "BEDARF":
                cpu = fpu = farben = netz = d3 = ram = xms = klang = 0; i = 1
                def wert(tab, name):
                    if i + 1 >= len(t): raise Fehler(f"{name}: Wert fehlt")
                    v = t[i + 1][1].upper()
                    if v not in tab: raise Fehler(f"{name}: {', '.join(tab)}")
                    return tab[v]
                while i < len(t):
                    k = t[i][1].upper()
                    if k == "CPU": cpu = wert(B_CPU, "CPU"); i += 2
                    elif k == "FPU": fpu = 1; i += 1
                    elif k == "FARBEN": farben = wert(B_FARBEN, "FARBEN"); i += 2
                    elif k == "NETZ": netz = 1; i += 1
                    elif k == "3D": d3 = wert(B_3D, "3D"); i += 2
                    elif k == "KLANG": klang |= wert(B_KLANG, "KLANG"); i += 2
                    elif k in ("RAM", "XMS"):
                        if i + 1 >= len(t): raise Fehler(f"{k}: KB fehlen")
                        v = zahl(t[i + 1])
                        if k == "RAM": ram = v
                        else: xms = v
                        i += 2
                    else: raise Fehler(f"BEDARF: unbekannt: {t[i][1]}")
                res.append((RT_BEDARF, 1, 0, struct.pack("<HBBBBBBHHH", 1, cpu, fpu, farben, netz, d3, 0, ram, xms, klang)))
            elif w == "ACCEL":
                nr = zahl(t[1]); b = b""
                while True:
                    z = weiter()
                    if z[0][1].upper() == "ENDE": break
                    b += struct.pack("<HHH", zahl(z[0]), zahl(z[1]), zahl(z[2]))
                res.append((RT["ACCEL"], nr, sprache, b))
            elif w in RT:
                nr = zahl(t[1]); f = os.path.join(basis, t[2][1])
                if not os.path.exists(f): raise Fehler(f"Datei fehlt: {t[2][1]}")
                res.append((RT[w], nr, sprache, open(f, "rb").read()))
            else: raise Fehler(f"unbekannt: {t[0][1]}")
        except (Fehler, IndexError) as e:
            sys.exit(f"{pfad}({nr_zeile}): {e if isinstance(e, Fehler) else 'zu wenige Angaben'}")
    seen = set()
    for typ, nr, sp, _ in res:
        if (typ, nr, sp) in seen: sys.exit(f"{pfad}: Ressource Typ {typ} Nr. {nr} doppelt")
        seen.add((typ, nr, sp))
    return res

if __name__ == "__main__":
    a = [x for x in sys.argv[1:] if x != "--lng"]
    if len(a) != 2: sys.exit(__doc__)
    res = uebersetze(a[0])
    if "--lng" in sys.argv:
        s = [r for r in res if r[0] == 3]
        if len(s) != 1: sys.exit("--lng: genau ein TEXTE-Block")
        open(a[1], "wb").write(b"RDL1" + struct.pack("<H", s[0][2]) + s[0][3])
    else:
        out = b"RDR1" + struct.pack("<H", len(res))
        for typ, nr, sp, d in res: out += struct.pack("<HHHHI", typ, nr, sp, 2, len(d)) + d
        open(a[1], "wb").write(out)
    print(f"{a[1]}: {len(res)} Ressourcen")
