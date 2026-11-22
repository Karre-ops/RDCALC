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
| `integrate(ausdruck[,x[,a,b]])` | Stammfunktion (auch partiell), mit Grenzen bestimmtes Integral | `integrate(x^2,x)` → `x^3/3` |
| `solve(gleichung[,x])` | Gleichung lösen | `solve(x^2-5x+6=0,x)` → `x = 2 or x = 3` |
| `solve([g1;g2],[x,y])` | lineares Gleichungssystem | `solve([x+y=3;x-y=1],[x,y])` → `x = 2 and y = 1` |
| `subst(ausdruck,x,wert)` | einsetzen | `subst(x^2+1,x,3)` → `10` |
| `approx(ausdruck)` | Zahlenwert (Winkelmodus beachten) | `approx(pi)` → `3.141592654` |
| `rref(A)` / `rank(A)` | Zeilenstufenform / Rang | `rref([1,2;3,4])` → `[1,0;0,1]` |
| `lsolve(A,b)` | lineares Gleichungssystem A·x=b | `lsolve([1,1;1,-1],[3,1])` → `[2;1]` |
| `charpoly(A[,x])` | charakteristisches Polynom det(A−x·I) | `charpoly([1,2;3,4])` → `x^2-5*x-2` |
| `eigenvals(A[,x])` | Eigenwerte | `eigenvals([2,1;1,2])` → `x = 1 or x = 3` |
| `eigenvect(A[,x])` | Eigenvektoren | `eigenvect([1,1;0,2])` → `[1;0] or [1;1]` |
| `taylor(expr,x,a,n)` | Taylorpolynom um a bis Ordnung n | `taylor(exp(x),x,0,4)` → `x^4/24+x^3/6+x^2/2+x+1` |
| `limit(expr,x,a)` | Grenzwert an einer Stelle (0/0 über l'Hospital) | `limit(sin(x)/x,x,0)` → `1` |
| `nsolve(gleichung,x,start)` | numerisch lösen (Newton) | `nsolve(x^2-2=0,x,1)` → `x = 1.414213562` |
| `zeros(expr[,x])` | Nullstellen | `zeros(x^2-4)` → `x = -2 or x = 2` |
| `deg(expr[,x])` / `coeff(expr,x,n)` | Grad und Koeffizient | `coeff(x^3+2x,x,1)` → `2` |
| `gcd` `lcm` `mod` `ncr` `npr` `fac` | Zahlentheorie, `n!` als Nachsatz | `gcd(12,18)` → `6`, `5!` → `120` |
| `sum(expr,x,a,b)` / `prod(expr,x,a,b)` | Summe / Produkt über ganze Grenzen | `sum(x^2,x,1,10)` → `385` |
| `dot(u,v)` / `cross(u,v)` | Skalar- und Kreuzprodukt | `dot([1,2,3],[4,5,6])` → `32` |
| `laplace(expr[,t[,s]])` | Laplace-Transformation | `laplace(exp(-t)*sin(2t))` → `2/(s^2+2*s+5)` |
| `ilaplace(expr[,s[,t]])` | inverse Laplace-Transformation | `ilaplace(1/(s^2+1))` → `sin(t)` |

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
| `eigenvals([2,1;1,2])` | `x = 1 or x = 3` |
| `eigenvect([1,1;0,2])` | `[1;0] or [1;1]` |

Die Tasten `det`, `inv`, `trans`, `i`, `[` und `]` stehen in der fünften Reihe; `mtx` setzt `[1,2;3,4]` als Vorlage ein.

### Laplace-Transformation

`laplace(ausdruck[,t[,s]])` bildet f(t) → F(s); Standardvariablen sind `t` und `s`. Abgedeckt sind Konstanten, `t^n`, `exp(a*t)`, `sin(w*t)`, `cos(w*t)` und Produkte daraus (Ableitungssatz für `t^n`, Verschiebungssatz für `exp`).

| Beispiel | Ergebnis |
| --- | --- |
| `laplace(t)` | `1/s^2` |
| `laplace(exp(2t))` | `1/(s-2)` |
| `laplace(sin(3t))` | `3/(s^2+9)` |
| `laplace(t*exp(-2t))` | `1/(s+2)^2` |
| `laplace(exp(-t)*sin(2t))` | `2/(s^2+2*s+5)` |
| `laplace(t*sin(t))` | `2*s/(s^2+1)^2` |

### Inverse Laplace-Transformation

`ilaplace(ausdruck[,s[,t]])` bildet die Rücktransformierte F(s) → f(t); Standardvariablen sind `s` und `t`. Echt gebrochene rationale Funktionen werden exakt in Partialbrüche zerlegt (rationale Polstellen, mehrfache Polstellen, ein quadratischer Rest) und daraus die Grundformen zusammengesetzt. Polynomanteile ergeben Delta-Impulse, Faktoren `exp(-a*s)` eine Zeitverschiebung mit `step`.

| Beispiel | Ergebnis |
| --- | --- |
| `ilaplace(1/s^2)` | `t` |
| `ilaplace(1/(s-3))` | `exp(3*t)` |
| `ilaplace(1/(s+1)^2)` | `t*exp(-t)` |
| `ilaplace(1/(s^2+1))` | `sin(t)` |
| `ilaplace(s/(s^2+1))` | `cos(t)` |
| `ilaplace(1/(s^2+2s+5))` | `sin(2*t)*exp(-t)/2` |
| `ilaplace(1/(s*(s+1)))` | `-exp(-t)+1` |
| `ilaplace(1/(s^2-1))` | `-exp(-t)/2+exp(t)/2` |
| `ilaplace(1/((s+1)*(s+2)*(s+3)))` | `-exp(-2*t)+exp(-3*t)/2+exp(-t)/2` |
| `ilaplace(1)` | `delta(t)` |
| `ilaplace(s/(s+1))` | `delta(t)-exp(-t)` |
| `ilaplace(s^2/(s+1))` | `exp(-t)+delta'(t)-delta(t)` |
| `ilaplace(exp(-2s)/s)` | `step(t-2)` |
| `ilaplace(exp(-s)/(s+1))` | `exp(-t+1)*step(t-1)` |

Nicht abgedeckt: nicht rationale Anteile (etwa Wurzeln in `s`), Delta-Ableitungen über `delta''` hinaus und mehrere Verschiebungen im selben Summanden.

Hinweise zur Bedienung:

- Das Komma trennt CAS-Argumente; Dezimaltrennzeichen ist der Punkt.
- Die Taste `=` schreibt jetzt ein Gleichheitszeichen in die Eingabe (für `solve`). Gerechnet wird mit Enter oder der Taste `=` im Tastenfeld.
- Jedes Ergebnis steht in einer eigenen Zeile unter der Eingabe. Sehr lange Ergebnisse werden auf die Anzeigebreite gekürzt.
- Die Eingabezeile fasst 159 Zeichen, das reicht auch für 6×6-Matrizen. Ist die Eingabe breiter als die Anzeige, bleibt ihr Ende sichtbar (links steht dann `<`). `!` (Fakultät) und `'` (`delta'`) lassen sich direkt tippen.
- Die exakte Bruchrechnung kommt ohne Gleitkomma aus und ist deshalb ohne 8087 fast genauso schnell wie mit. Nur Zahlenwerte (`approx`, `nsolve`, Winkelfunktionen) profitieren vom Koprozessor.

Grenzen der jetzigen Fassung: `solve` löst lineare und quadratische Gleichungen (auch komplex), Polynome mit rationalen Nullstellen, lineare Gleichungen mit symbolischen Koeffizienten (`solve(a*x+b=0,x)`) und lineare Systeme über `solve([g1;g2],[x,y])`; nichtlineare Systeme lehnt es ab. `factor` für eine Variable; Matrizen bis 6×6 mit `+`, `-`, `*`, `^`, `det`, `inv`, `trans`, `rref`, `rank`, `lsolve`, `charpoly`, `eigenvals` und `eigenvect` (Eigenwerte exakt bei rationalen bzw. quadratischen Polynomen). Integration mit partieller Integration für Polynom mal `sin`/`cos`/`exp`/`ln` und für `atan`/`asin`/`acos`, aber ohne Substitutionsverfahren wie `exp(x^2)`. `laplace` deckt Konstanten, `t^n`, `exp`, `sin`/`cos` und Produkte daraus ab; `ilaplace` rationale Funktionen mit rationalen Polen und einem quadratischen Rest, dazu Polynomanteile (Delta-Impulse bis `delta''`) und Zeitverschiebungen `exp(-a*s)` (Sprungfunktion `step`).

## Stand

Das Programm stammt aus RetroDesk 1 und läuft als eigenständiges DOS-Programm (eigene Grafik, Maus und Menüs in `src/`). Der symbolische Kern liegt in `src/cas.c` und ist reines C ohne DOS-Abhängigkeiten, damit er sich auch auf dem PC testen lässt. RDCALC wird getrennt von RetroDesk 2 weiterentwickelt. Eine Fassung als RD2-Programm ist geplant.

## Symbol

In `symbol/` liegt das Programmsymbol (32x32 S/W, CGA 32x16, farbig in `symbol/farbe/`): Achsen mit roter Kurve, passend zu den Symbolen von RetroDesk 2. RDCALC ist noch ein RD1-Programm, deshalb steckt das Symbol noch nicht in der EXE. Für die RD2-Fassung wird es mit `tools/mkrdb.py ... --cga` aus RetroDesk 2 zur `.ICO` und über `SYMBOL 1` in der `.RC` eingebaut.

## Bauen

Mit Open Watcom: `./build.sh` (erzeugt `RDCALC.EXE`).

## Testen

Neben dem normalen Betrieb gibt es einen Testmodus: `RDCALC /KEYS datei /SHOT` liest Tastencodes aus `datei` (je 2 Byte, Little Endian; 13 = Enter) und schreibt am Ende das letzte Ergebnis nach `CALC.TXT` und den Bildschirm nach `SHOT.BIN` (roher Grafikspeicher). Damit lassen sich Rechnungen und CAS-Befehle ohne Maus automatisch prüfen. Der symbolische Kern ist reines C und lässt sich zusätzlich mit einem beliebigen C-Compiler und einem kurzen Treiber testen.

Lizenz: GPL v2 oder später (siehe COPYING).
