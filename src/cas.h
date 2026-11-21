/* RetroDesk - RDCALC: symbolischer Kern (CAS)
 * Rechnet exakt mit Bruechen und Ausdruecken: simplify, expand, factor,
 * diff, integrate, solve, subst, approx.  Reines C ohne DOS-Abhaengigkeit,
 * damit der Kern auch auf dem PC getestet werden kann. */
#ifndef CAS_H
#define CAS_H

/* Rueckgabe: 1 = Ergebnis in out, -1 = Fehlertext in out, 0 = kein CAS-Befehl */
int cas_eval(const char *in, char *out, int outsz, double ans);

/* Winkelmodus fuer approx(): 1 = Grad, 0 = Bogenmass */
void cas_set_deg(int d);

#endif
