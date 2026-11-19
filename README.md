# RDCALC – wissenschaftlicher Rechner mit Funktionsplotter

RDCALC ist ein Rechner für DOS-PCs ab dem 8088: Formeln wie `sin(30)+2^3`, `ans` für das letzte Ergebnis, Kurven wie `2x^2-1` (F5) und Druck der Kurve auf LPT1 im Epson-Grafikmodus (9 Nadeln).

Mit 8087 rechnet der Koprozessor, ohne ihn der Emulator der Laufzeitbibliothek (gleiches Ergebnis, nur langsamer).

## Stand
Das Programm stammt aus RetroDesk 1 und läuft als eigenständiges DOS-Programm (eigene Grafik, Maus und Menüs in `src/`). Es wird getrennt von RetroDesk 2 weiterentwickelt. Eine Fassung als RD2-Programm ist geplant.

## Symbol
In `symbol/` liegt das Programmsymbol (32x32 S/W, CGA 32x16, farbig in `symbol/farbe/`): Achsen mit roter Kurve, passend zu den Symbolen von RetroDesk 2. RDCALC ist noch ein RD1-Programm, deshalb steckt das Symbol noch nicht in der EXE. Für die RD2-Fassung wird es mit `tools/mkrdb.py ... --cga` aus RetroDesk 2 zur `.ICO` und über `SYMBOL 1` in der `.RC` eingebaut.

## Bauen
Mit Open Watcom: `./build.sh` (erzeugt `RDCALC.EXE`).

Lizenz: GPL v2 oder später (siehe COPYING).
