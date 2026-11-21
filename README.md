# RDCALC – wissenschaftlicher Rechner mit Funktionsplotter und CAS

RDCALC ist ein Rechner für DOS-PCs ab dem 8088: Formeln wie `sin(30)+2^3`, `ans` für das letzte Ergebnis, Kurven wie `2x^2-1` (F5) und Druck der Kurve auf LPT1 im Epson-Grafikmodus (9 Nadeln). Dazu kommt ein symbolischer Kern (Computer-Algebra) mit exakten Brüchen.

Mit 8087 rechnet der Koprozessor, ohne ihn der Emulator der Laufzeitbibliothek (gleiches Ergebnis, nur langsamer).

## Computer-Algebra (CAS)

Die vierte Tastenreihe setzt die Befehle in die Eingabezeile, die fünfte bedient komplexe Zahlen und Matrizen; `help` zeigt eine Übersicht. Man kann die Befehle auch direkt tippen. Ohne Variablenangabe rechnet das CAS mit `x`.

| Befehl | Wirkung | Beispiel |
| --- | --- | --- |
| `simplify(ausdruck)` | zusammenfassen, kürzen | `simplify(2x+3x)` → `5*x` |
| `expand(ausdruck)` | ausmultiplizieren | `expand((x+1)^2)` → `x^2+2*x+1` |
| `factor(ausdruck)` | faktorisieren | `factor(x^2-5x+6)` → `(x-3)*(x-2)` |
| `diff(ausdruck[,x])` | ableiten | `diff(x^2*sin(x),x)` → `cos(x)*x^2+2*x*sin(x)` |
| `integrate(ausdruck[,x[,a,b]])` | Stammfunktion, mit Grenzen bestimmtes Integral | `integrate(x^2,x)` → `x^3/3` |
| `solve(gleichung[,x])` | Gleichung lösen | `solve(x^2-5x+6=0,x)` → `x = 2 or x = 3` |
| `subst(ausdruck,x,wert)` | einsetzen | `subst(x^2+1,x,3)` → `10` |
| `approx(ausdruck)` | Zahlenwert (Winkelmodus beachten) | `approx(pi)` → `3.141592654` |

Gerechnet wird exakt: `1/3` bleibt ein Bruch, `sqrt(8)` wird zu `2*sqrt(2)`, `sin(pi)` zu `0`. `pi` und `e` sind Konstanten. Die Eingabe versteht `2x`, `3(x+1)`, `x^2`, `sqrt(...)`, `ln`, `log`, `exp`, `abs` und die Winkelfunktionen.

### Komplexe Zahlen

`i` ist die imaginäre Einheit, `i^2` wird zu `-1`. `simplify` und `expand` rechnen damit exakt, `solve` liefert bei negativer Diskriminante komplexe Lösungen, `approx` gibt `a+b*i` aus.

| Beispiel | Ergebnis |
| --- | --- |
| `simplify((1+i)^2)` | `2*i` |
| `expand((x+i)*(x-i))` | `x^2+1` |
| `solve(x^2+1=0,x)` | `x = i or x = -i` |
| `simplify(1/(1+i))` | `-i/2+1/2` |
| `approx(1/(1+i))` | `0.5-0.5*i` |
| `abs(3+4i)` | `5` |
| `simplify(sqrt(-8))` | `2*sqrt(2)*i` |

### Matrizen

Matrizen schreibt man als `[1,2;3,4]`: Zeilen durch `;`, Elemente durch `,`. `+`, `-`, `*` und `^` rechnen wie in der linearen Algebra, eine Zahl davor oder dahinter skaliert alle Elemente. `det`, `inv` und `trans` arbeiten exakt, auch mit Brüchen und `i`.

| Beispiel | Ergebnis |
| --- | --- |
| `det([1,2;3,4])` | `-2` |
| `inv([1,2;3,4])` | `[-2,1;3/2,-1/2]` |
| `trans([1,2;3,4])` | `[1,3;2,4]` |
| `[1,2;3,4]*[5,6;7,8]` | `[19,22;43,50]` |
| `2*[1,2;3,4]` | `[2,4;6,8]` |
| `[1,2;3,4]^2` | `[7,10;15,22]` |
| `det([x,1;1,x])` | `x^2-1` |

Die Tasten `det`, `inv`, `trans`, `i`, `[` und `]` stehen in der fünften Reihe; `mtx` setzt `[1,2;3,4]` als Vorlage ein.

Hinweise zur Bedienung:

- Das Komma trennt CAS-Argumente; Dezimaltrennzeichen ist der Punkt.
- Die Taste `=` schreibt jetzt ein Gleichheitszeichen in die Eingabe (für `solve`). Gerechnet wird mit Enter oder der Taste `=` im Tastenfeld.
- Jedes Ergebnis steht in einer eigenen Zeile unter der Eingabe. Sehr lange Ergebnisse werden auf die Anzeigebreite gekürzt.

Grenzen der jetzigen Fassung: `solve` löst lineare und quadratische Gleichungen (auch komplex) sowie Polynome mit rationalen Nullstellen, aber nur mit reellen Koeffizienten; `factor` für eine Variable; Matrizen bis 6×6 mit `+`, `-`, `*`, `^`, `det`, `inv` und `trans` (kein `rref`, keine Eigenwerte); Integration ohne partielle Integration (Polynome, `sin`/`cos`/`tan`/`exp`/`ln`, lineare Substitution und bestimmte Integrale).

## Stand

Das Programm stammt aus RetroDesk 1 und läuft als eigenständiges DOS-Programm (eigene Grafik, Maus und Menüs in `src/`). Der symbolische Kern liegt in `src/cas.c` und ist reines C ohne DOS-Abhängigkeiten, damit er sich auch auf dem PC testen lässt. RDCALC wird getrennt von RetroDesk 2 weiterentwickelt. Eine Fassung als RD2-Programm ist geplant.

## Symbol

In `symbol/` liegt das Programmsymbol (32x32 S/W, CGA 32x16, farbig in `symbol/farbe/`): Achsen mit roter Kurve, passend zu den Symbolen von RetroDesk 2. RDCALC ist noch ein RD1-Programm, deshalb steckt das Symbol noch nicht in der EXE. Für die RD2-Fassung wird es mit `tools/mkrdb.py ... --cga` aus RetroDesk 2 zur `.ICO` und über `SYMBOL 1` in der `.RC` eingebaut.

## Bauen

Mit Open Watcom: `./build.sh` (erzeugt `RDCALC.EXE`).

## Testen

Neben dem normalen Betrieb gibt es einen Testmodus: `RDCALC /KEYS datei /SHOT` liest Tastencodes aus `datei` (je 2 Byte, Little Endian; 13 = Enter) und schreibt am Ende das letzte Ergebnis nach `CALC.TXT` und den Bildschirm nach `SHOT.BIN` (roher Grafikspeicher). Damit lassen sich Rechnungen und CAS-Befehle ohne Maus automatisch prüfen. Der symbolische Kern ist reines C und lässt sich zusätzlich mit einem beliebigen C-Compiler und einem kurzen Treiber testen.

Lizenz: GPL v2 oder später (siehe COPYING).
