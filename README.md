# RDCALC – wissenschaftlicher Rechner mit Funktionsplotter und CAS für RetroDesk 2

RDCALC ist ein Programm für RetroDesk 2 und läuft ab dem 8088, mit und ohne 8087, auf CGA, Hercules, EGA, VGA und den übrigen RD2-Grafikarten. Es rechnet Formeln wie `sin(30)+2^3`, kennt `ans` für das letzte Ergebnis, zeichnet Kurven wie `2x^2-1` (F5) und druckt sie über das Druckersystem von RD2. Dazu kommt ein symbolischer Kern (Computer-Algebra) mit exakten Brüchen.

Die frühere eigenständige DOS-Fassung (aus RetroDesk 1) gibt es nicht mehr.

## Installation

`BIN/RDCALC.EXE` in den RD2-Ordner kopieren (z. B. `C:\RD2`) und in der Shell starten. Unter reinem DOS meldet die Datei nur, dass sie RetroDesk 2 braucht.

- Speicher: 116 KB Datei, rund 137 KB Programm (davon 24 KB Stapel) plus 35 KB für die Ausdrucksbäume des CAS und 6 KB für die Kurve.
- 8087: Die Gleitkommarechnung läuft über die Bibliotheksroutinen von Open Watcom. Meldet RD2 einen Koprozessor, rechnen sie darauf, sonst in Software. Es ist eine Datei für beide Fälle, im Titel steht dann „RDCALC (8087)“. Die exakte Bruchrechnung des CAS kommt ohne Gleitkomma aus.
- Der Stand (Verlauf, `ans`, Grad/Bogenmaß, Kurvenbereich) wird beim Beenden in `RDCALC2.STA` neben der EXE gespeichert.

## Bedienung

Das Fenster hat oben den Verlauf (je Eintrag Eingabe und Ergebnis, auf CGA 2, auf VGA 5 Einträge), darunter die Eingabezeile und 54 Tasten in sechs Reihen.

| Taste | Wirkung |
| --- | --- |
| Eingabe oder `=` im Tastenfeld | rechnen |
| Rückschritt / Entf | letztes Zeichen löschen / Eingabe löschen |
| Esc | Eingabe löschen, bei leerer Eingabe beenden |
| Pfeil hoch | letzte Eingabe zurückholen |
| F1 oder `help` | Übersicht der CAS-Befehle |
| F5 oder `Plot` | Formel mit `x` als Kurve zeichnen |
| `Deg`/`Rad` | Winkel in Grad oder Bogenmaß |

Die Eingabezeile fasst 159 Zeichen; ist sie breiter als die Anzeige, bleibt ihr Ende sichtbar.

### Kurve

Die Kurve öffnet ein eigenes Fenster. Ein Mausklick zeigt x und f(x) an der Stelle im Titel.

| Taste | Wirkung |
| --- | --- |
| `+` / `-` | x-Bereich verkleinern / vergrößern |
| Pfeil links / rechts | x-Bereich verschieben |
| `Y`, Pfeil hoch / runter | y-Bereich festhalten und zoomen, dann verschieben |
| `A` | y-Bereich wieder automatisch |
| `0` | Ausgangsbereich −10 bis 10 |
| `P` | drucken (Druckdialog von RD2) |
| Esc | Kurvenfenster schließen |

## Computer-Algebra (CAS)

Die fünfte Tastenreihe setzt die Befehle in die Eingabezeile, die sechste bedient Matrizen und komplexe Zahlen; `help` (F1) zeigt eine Übersicht. Man kann die Befehle auch direkt tippen. Ohne Variablenangabe rechnet das CAS mit `x`.

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

- Das Komma trennt CAS-Argumente; Dezimaltrennzeichen ist der Punkt. Bei reinen Zahlenformeln gilt das Komma auch als Dezimaltrennzeichen.
- Die Taste `=` auf der Tastatur schreibt ein Gleichheitszeichen in die Eingabe (für `solve`); gerechnet wird mit Eingabe oder der Taste `=` im Tastenfeld.
- `!` (Fakultät) und `'` (`delta'`) lassen sich direkt tippen.
- Sehr lange Ergebnisse werden auf die Anzeigebreite gekürzt.

Grenzen der jetzigen Fassung: `solve` löst lineare und quadratische Gleichungen (auch komplex), Polynome mit rationalen Nullstellen, lineare Gleichungen mit symbolischen Koeffizienten (`solve(a*x+b=0,x)`) und lineare Systeme über `solve([g1;g2],[x,y])`; nichtlineare Systeme lehnt es ab. `factor` für eine Variable; Matrizen bis 6×6 mit `+`, `-`, `*`, `^`, `det`, `inv`, `trans`, `rref`, `rank`, `lsolve`, `charpoly`, `eigenvals` und `eigenvect` (Eigenwerte exakt bei rationalen bzw. quadratischen Polynomen). Integration mit partieller Integration für Polynom mal `sin`/`cos`/`exp`/`ln` und für `atan`/`asin`/`acos`, aber ohne Substitutionsverfahren wie `exp(x^2)`. `laplace` deckt Konstanten, `t^n`, `exp`, `sin`/`cos` und Produkte daraus ab; `ilaplace` rationale Funktionen mit rationalen Polen und einem quadratischen Rest, dazu Polynomanteile (Delta-Impulse bis `delta''`) und Zeitverschiebungen `exp(-a*s)` (Sprungfunktion `step`).

## Bauen

Mit Open Watcom 2.0 unter Linux:

```
./build.sh
```

Ergebnis ist `BIN/RDCALC.EXE`. Gebaut wird im Medium-Modell mit `-fpc`. `SDK/` enthält Kopf, Bibliothek und Startcode von RetroDesk 2 (API 2.3), `tools/` die Werkzeuge für Ressourcen und den RD2-Anhang. Weil RD2-Programme ohne den Watcom-Startcode laufen, ersetzen `src/RD2CRT.C` und `src/RD2GETDS.ASM` die wenigen Teile davon, die die Gleitkomma-Bibliothek braucht.

| Datei | Inhalt |
| --- | --- |
| `src/RDCALC.C` | Fenster, Tasten, Verlauf, Kurve, Drucken |
| `src/formel.c` | Zahlenformeln: Formel nach Postfix, rechnen, ausgeben |
| `src/cas.c`, `src/cas.h` | symbolischer Kern, reines C |
| `src/RDCALC.RC` | Texte deutsch und englisch, Symbol |

## Testen

`test.sh` startet RetroDesk 2 in QEMU, tippt Rechnungen über den QEMU-Monitor und liest die Ergebnisse über COM1 (Befehlszeile `T`: jedes Ergebnis als „RDCALC: …“). Es braucht eine gebaute RetroDesk-2-Arbeitskopie:

```
RD2=/pfad/zu/RetroDesk2 LAUF=VGA ./test.sh   # VGA mit 8087
RD2=/pfad/zu/RetroDesk2 LAUF=N ./test.sh     # VGA ohne 8087 (Befehlszeile TN)
RD2=/pfad/zu/RetroDesk2 LAUF=CGA ./test.sh   # CGA, kürzerer Satz
```

Geprüft werden Zahlen, CAS (unter anderem Laplace, inverse Laplace, Eigenwerte 3×3, 6×6-Determinante, `5!`), eine Fehlermeldung, die Kurve, der freie Stapel und ob RD2 nach dem Beenden allen Speicher zurückbekommt.

## Lizenz

GPL 2 oder neuer, siehe `COPYING`.
