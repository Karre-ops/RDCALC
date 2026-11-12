# RDCALC – wissenschaftlicher Rechner mit Funktionsplotter

RDCALC ist ein Rechner für DOS-PCs ab dem 8088: Formeln wie `sin(30)+2^3`, `ans` für das letzte Ergebnis, Kurven wie `2x^2-1` (F5) und Druck der Kurve auf LPT1 im Epson-Grafikmodus (9 Nadeln).

Mit 8087 rechnet der Koprozessor, ohne ihn der Emulator der Laufzeitbibliothek (gleiches Ergebnis, nur langsamer).

## Stand
Das Programm stammt aus RetroDesk 1 und läuft als eigenständiges DOS-Programm (eigene Grafik, Maus und Menüs in `src/`). Es wird getrennt von RetroDesk 2 weiterentwickelt. Eine Fassung als RD2-Programm ist geplant.

## Bauen
Mit Open Watcom: `./build.sh` (erzeugt `RDCALC.EXE`).

Lizenz: GPL v2 oder später (siehe COPYING).
