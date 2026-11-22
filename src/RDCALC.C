/* RDCALC.C - RDCALC als RD2-Programm: wissenschaftlicher Rechner, Funktionsplotter und CAS, ab dem 8088.
 * SPDX-License-Identifier: GPL-2.0-or-later  (c) 2026 Karsten, siehe COPYING
 * Rechenwerk in src/formel.c (Zahlen und Kurve), Computer-Algebra in src/cas.c.
 * Gleitkomma: -fpc (Bibliotheksaufrufe). Meldet der Kern einen 8087, wird __real87 gesetzt; dann rechnen
 * die Bibliotheksroutinen auf dem Koprozessor, sonst in Software. Es braucht keinen Emulator im Kern.
 * Befehlszeile "T": Testmodus, meldet jedes Ergebnis ueber DbgPrint ("RDCALC: ...", test/rdcalc_rd2.sh). */
#include "RD2.H"
#include <string.h>
#include <math.h>
#include <ctype.h>
#include <stdlib.h>
#include <stdio.h>
#include "cas.h"
#include "formel.c"                               /* compile, run, fmt, ans, deg, perr */

/* Gleitkomma-Ausgabe von sprintf (%g) einhaengen; das macht sonst __setEFGfmt im cstart (dort near gerufen) */
#pragma aux efg_p "___EFG_printf"
#pragma aux efg_s "___EFG_scanf"
#pragma aux efg_fmt "_EFG_Format_"
#pragma aux efg_cnv "__cnvs2d_"
extern void (__far *efg_p)(void), (__far *efg_s)(void);
extern void __far efg_fmt(void), __far efg_cnv(void);
#pragma aux real87 "__real87"
#pragma aux m8087 "__8087"
extern unsigned char real87, m8087;               /* Watcom-Bibliothek: 8087 benutzen (sonst setzt das cstart) */

#define LN 160
#define NH 6
#define NB 54
#define NCOL 9
static const char * const blbl[NB] = {
    "7", "8", "9", "/", "(", ")", "sin", "asin", "^",
    "4", "5", "6", "*", "pi", "e", "cos", "acos", "sqrt",
    "1", "2", "3", "-", "ans", "x^2", "tan", "atan", "ln",
    "0", ".", "=", "+", "C", "<-", "log", "abs", "exp",
    "simpl", "expand", "factor", "diff", "integ", "solve", "subst", "help", "Deg",
    "det", "inv", "trans", "i", "[", "]", "Plot", "Quit", "mtx" };
#define B_EQ 29
#define B_C 31
#define B_BS 32
#define B_X2 23
#define B_DEG 44
#define B_HELP 43
#define B_PLOT 51
#define B_END 52
#define B_MTX 53
static const char * const bins[NB] = {            /* was die Taste einsetzt (0 = Beschriftung) */
    0, 0, 0, 0, 0, 0, "sin(", "asin(", 0,   0, 0, 0, 0, 0, 0, "cos(", "acos(", "sqrt(",
    0, 0, 0, 0, 0, "^2", "tan(", "atan(", "ln(",   0, 0, 0, 0, 0, 0, "log(", "abs(", "exp(",
    "simplify(", "expand(", "factor(", "diff(", "integrate(", "solve(", "subst(", 0, 0,
    "det(", "inv(", "trans(", 0, 0, 0, 0, 0, "[1,2;3,4]" };

static HINST hi; static HWND fe, kw, btn[NB]; static BYTE test, fpu;
static char hist[NH][160], line[LN], lastin[LN], status[80], expr[LN], stafn[80];
static short nh, lh, th, bh, bw, bg, dyh, dh;     /* Verlauf, Zeilen, Text-/Tastenhoehe, Anzeige */

/* ---------------- Stand (RDCALC2.STA neben der EXE) ---------------- */
#define STA_FELDER(F) F(ans) F(deg) F(nh) F(hist) F(lastin) F(xlo) F(xhi) F(ylo) F(yhi) F(yfix)
static double xlo = -10, xhi = 10, ylo, yhi; static short yfix;
#define STA_W(v) FileWrite(f, &v, sizeof v);
#define STA_R(v) FileRead(f, &v, sizeof v);
static void sta_save(void)
{
    short f = FileCreate(stafn, 0);
    if (f < 0) return;
    FileWrite(f, "RDR1", 4); STA_FELDER(STA_W) FileClose(f);
}
static void sta_load(void)
{
    char m[4]; short f = FileOpen(stafn, FO_READ), i;
    if (f < 0) return;
    if (FileRead(f, m, 4) == 4 && !memcmp(m, "RDR1", 4)) { STA_FELDER(STA_R) }
    FileClose(f);
    if (nh < 0 || xhi <= xlo || deg < 0 || deg > 1) { nh = 0; deg = 1; xlo = -10; xhi = 10; yfix = 0; }
    lastin[LN - 1] = 0;
    for (i = 0; i < NH; i++) hist[i][159] = 0;
}

/* ---------------- Anzeige ---------------- */
static void neu_anzeige(void) { RECT r; GetClientRect(fe, &r); r.t = 0; r.b = (short)(dyh + bg); InvalidateRect(fe, &r, FALSE); }
static void neu_status(void) { RECT r; GetClientRect(fe, &r); r.t = (short)(r.b - th - 4); InvalidateRect(fe, &r, FALSE); }
static void meld(LPCSTR s) { _fstrncpy(status, s, sizeof status - 1); neu_status(); }
static void textmax(HDC d, short x, short y, const char *s, short w, BYTE ende)   /* gekuerzt auf w Pixel */
{
    short n = (short)strlen(s);
    if (ende) { while (n && TextWidth(d, s, -1) > w) { s++; n--; } }       /* Eingabe: das Ende bleibt sichtbar */
    else while (n && TextWidth(d, s, n) > w) n--;
    TextOut(d, x, y, s, n);
}
static void male(HWND w)
{
    PAINT ps; HDC d; RECT r, a; short i, n = nh < lh ? nh : lh, y, wi;
    char b[LN + 4];
    d = BeginPaint(w, &ps); GetClientRect(w, &r);
    a.l = bg; a.r = (short)(r.r - bg); a.t = bg; a.b = dyh;
    SetBrush(d, C_WHITE, PAT_SOLID); FillRect(d, &a); SetPen(d, C_BLACK); Frame(d, &a);
    SetBkMode(d, BK_TRANSPARENT); SetTextColor(d, C_BLACK);
    wi = (short)(a.r - a.l - 10);
    for (i = 0; i < n; i++) {                     /* aelteste oben, Eingabe und Ergebnis je eine Zeile */
        const char *h = hist[(nh - n + i) % NH], *tab = strchr(h, '\t');
        short len = tab ? (short)(tab - h) : (short)strlen(h);
        y = (short)(a.t + 3 + i * 2 * (th + 2));
        b[0] = '>'; b[1] = ' '; memcpy(b + 2, h, len); b[len + 2] = 0;
        textmax(d, (short)(a.l + 5), y, b, wi, 0);
        if (tab && tab[1]) { b[0] = '='; strcpy(b + 2, tab + 1); textmax(d, (short)(a.l + 5), (short)(y + th + 2), b, wi, 0); }
    }
    y = (short)(a.b - th - 4);
    MoveTo(d, (short)(a.l + 1), (short)(y - 2)); LineTo(d, (short)(a.r - 2), (short)(y - 2));
    b[0] = '>'; b[1] = ' '; strcpy(b + 2, line); strcat(b, "_");
    textmax(d, (short)(a.l + 5), y, TextWidth(d, b, -1) > wi ? b + 2 : b, wi, 1);
    a.t = (short)(r.b - th - 4); a.b = r.b; a.l = 0; a.r = r.r;          /* Statuszeile */
    SetBrush(d, C_FENSTER, PAT_SOLID); FillRect(d, &a);
    SetTextColor(d, C_TEXT);
    if (!status[0]) { char t[80]; StrLoad(hi, 10, t, sizeof t); textmax(d, bg, (short)(a.t + 2), t, (short)(r.r - 2 * bg), 0); }
    else textmax(d, bg, (short)(a.t + 2), status, (short)(r.r - 2 * bg), 0);
    EndPaint(w, &ps);
}

/* ---------------- Rechnen ---------------- */
static void insert(const char *s) { if (strlen(line) + strlen(s) < LN - 1) strcat(line, s); }
static void add_hist(const char *e, const char *r) { sprintf(hist[nh % NH], "%.76s\t%.76s", e, r); nh++; }
static void fehler(LPCSTR s)
{
    meld(s); SndBeep(440, 120);
    if (test) { char t[100] = "RDCALC: FEHLER "; _fstrncat(t, s, 80); DbgPrint(t); }
}
static void ergebnis(const char *r)
{
    if (test) { char t[100] = "RDCALC: "; strncat(t, r, 90); DbgPrint(t); }
}
static void calc(void)
{
    double v; char r[200]; short cr; char *p;
    status[0] = 0;
    if (!line[0]) return;
    cas_set_deg(deg);
    cr = (short)cas_eval(line, r, sizeof r, ans);
    if (cr < 0) { fehler(r); return; }
    if (cr > 0) { strcpy(lastin, line); add_hist(line, r); line[0] = 0; ergebnis(r); return; }
    for (p = line; *p; p++) if (*p == ',') *p = '.';   /* kein CAS-Befehl: Komma als Dezimaltrennzeichen */
    if (strchr(line, '=')) { fehler(StrGet(hi, 11)); return; }
    if (!compile(line) || (!has_x && !run(0, &v))) { fehler(perr); return; }
    strcpy(lastin, line);
    if (has_x) { meld(StrGet(hi, 12)); if (test) DbgPrint("RDCALC: Formel mit x"); return; }
    ans = v; fmt(r, v); add_hist(line, r); line[0] = 0; ergebnis(r);
}

/* ---------------- Kurve (eigenes Fenster) ---------------- */
static double FAR *vals; static BYTE FAR *ok; static short nval;   /* je Spalte ein Wert */
static short pw, ph, px0, py0, kdirty = 1;
static void axis_text(double v, char *b) { fmt(b, v); if (strlen(b) > 9) sprintf(b, "%.3g", v); }
static double nice(double r)
{
    double s = pow(10, floor(log10(r / 8))), m = r / 8 / s;
    return s * (m < 1.5 ? 1 : m < 3.5 ? 2 : m < 7.5 ? 5 : 10);
}
static short ymap(double v, short y0, short h) { return (short)(y0 + (short)((yhi - v) / (yhi - ylo) * (h - 1) + 0.5)); }
static short sample(short n)
{
    short i, k = 0; double v, mn = 0, mx = 0;
    if (n > 720) n = 720;
    for (i = 0; i < n; i++) {
        double x = xlo + (xhi - xlo) * i / (n - 1);
        ok[i] = (BYTE)run(x, &v); vals[i] = v;
        if (ok[i]) { if (!k || v < mn) mn = v; if (!k || v > mx) mx = v; k++; }
    }
    perr = 0; nval = n;
    if (!k) return 0;
    if (!yfix) { if (mx - mn < 1e-9) { mn -= 1; mx += 1; } ylo = mn - (mx - mn) * 0.05; yhi = mx + (mx - mn) * 0.05; }
    return 1;
}
/* Kurve in d zeichnen: Rahmen x0,y0,w,h; je Spalte i Wert vals[i * nval / w] */
static void kurve(HDC d, short x0, short y0, short w, short h, short dot)
{
    RECT r; double st, t; short i, y, ly = 0, have = 0, xa;
    r.l = (short)(x0 - 1); r.t = (short)(y0 - 1); r.r = (short)(x0 + w + 1); r.b = (short)(y0 + h + 1);
    SetPen(d, C_BLACK); Frame(d, &r);
    st = nice(xhi - xlo);
    for (t = ceil(xlo / st) * st; t <= xhi; t += st) {
        xa = (short)(x0 + (short)((t - xlo) / (xhi - xlo) * (w - 1) + 0.5));
        for (y = y0; y < y0 + h; y += (short)(4 * dot)) Pixel(d, xa, y, C_BLACK);
    }
    st = nice(yhi - ylo);
    for (t = ceil(ylo / st) * st; t <= yhi; t += st) {
        y = ymap(t, y0, h);
        for (xa = x0; xa < x0 + w; xa += (short)(6 * dot)) Pixel(d, xa, y, C_BLACK);
    }
    if (ylo < 0 && yhi > 0) { y = ymap(0, y0, h); MoveTo(d, x0, y); LineTo(d, (short)(x0 + w - 1), y); }
    if (xlo < 0 && xhi > 0) { xa = (short)(x0 + (short)(-xlo / (xhi - xlo) * (w - 1) + 0.5)); MoveTo(d, xa, y0); LineTo(d, xa, (short)(y0 + h - 1)); }
    SetClip(d, &r);
    for (i = 0; i < w; i++) {                     /* Spruenge (Polstellen) nicht verbinden */
        short k = (short)((long)i * nval / w);
        if (!ok[k]) { have = 0; continue; }
        y = ymap(vals[k], y0, h);
        if (have && abs(y - ly) < h) LineTo(d, (short)(x0 + i), y); else { MoveTo(d, (short)(x0 + i), y); Pixel(d, (short)(x0 + i), y, C_BLACK); }
        ly = y; have = 1;
    }
    SetClip(d, 0);
}
static void kopf(char *s)
{
    char n1[16], n2[16], n3[16], n4[16];
    axis_text(xlo, n1); axis_text(xhi, n2); axis_text(ylo, n3); axis_text(yhi, n4);
    sprintf(s, "f(x) = %.40s   x: %s .. %s   y: %s .. %s", expr, n1, n2, n3, n4);
}
static void drucken(void)
{
    PRNJOB j; HDC d; short m; char s[100];
    memset(&j, 0, sizeof j); j.groesse = sizeof j; j.flags = PJ_DIALOG; j.name = "RDCALC";
    if (!(d = PrnOpen(kw, &j))) return;
    m = (short)(j.breite / 20); kopf(s);
    do {
        SetBkMode(d, BK_TRANSPARENT); SetTextColor(d, C_BLACK);
        TextOut(d, m, m, s, -1);
        kurve(d, m, (short)(m + 3 * th), (short)(j.breite - 2 * m), (short)((j.breite - 2 * m) * 2 / 3), 2);
    } while (PrnPage(d) == PRN_NOCHMAL);
    PrnClose(d);
}
static DWORD RDAPI kproc(HWND w, WORD msg, WORD p1, DWORD p2)
{
    PAINT ps; HDC d; RECT r; char s[100]; double v, dx;
    switch (msg) {
    case M_PAINT:
        d = BeginPaint(w, &ps); GetClientRect(w, &r);
        px0 = 6; py0 = (short)(th + 8); pw = (short)(r.r - 12); ph = (short)(r.b - py0 - th - 10);
        if (pw < 16 || ph < 16) { EndPaint(w, &ps); return 0; }
        if (kdirty) { SetTextColor(d, C_TEXT); SetBkMode(d, BK_TRANSPARENT); TextOut(d, 6, 3, StrGet(hi, 13), -1);
                      if (!sample(pw)) { EndPaint(w, &ps); DestroyWindow(w); fehler(StrGet(hi, 14)); return 0; }
                      kdirty = 0; if (test) DbgPrint("RDCALC: Kurve fertig"); }
        SetBrush(d, C_WHITE, PAT_SOLID); FillRect(d, &r);
        SetBkMode(d, BK_TRANSPARENT); SetTextColor(d, C_BLACK);
        kopf(s); TextOut(d, 6, 3, s, -1);
        kurve(d, px0, py0, pw, ph, 1);
        TextOut(d, 6, (short)(r.b - th - 3), StrGet(hi, 15), -1);
        EndPaint(w, &ps);
        return 0;
    case M_BUTTONDOWN:                            /* x und f(x) an der Mausstelle */
        if (pw > 1 && (short)LOWORD(p2) >= px0 && (short)LOWORD(p2) < px0 + pw) {
            char a[16], b[16];
            v = xlo + (xhi - xlo) * ((short)LOWORD(p2) - px0) / (pw - 1);
            fmt(a, v); if (run(v, &dx)) fmt(b, dx); else strcpy(b, "-");
            sprintf(s, "x = %s   f(x) = %s", a, b); SetWindowText(w, s);
        }
        return 0;
    case M_KEY:
        dx = (xhi - xlo) / 4;
        if (p1 == K_ESC) DestroyWindow(w);
        else if (p1 == K_LEFT) { xlo -= dx; xhi -= dx; }
        else if (p1 == K_RIGHT) { xlo += dx; xhi += dx; }
        else if (p1 == K_UP && yfix) { v = (yhi - ylo) / 4; ylo += v; yhi += v; }
        else if (p1 == K_DOWN && yfix) { v = (yhi - ylo) / 4; ylo -= v; yhi -= v; }
        else break;
        kdirty = 1; InvalidateRect(w, 0, TRUE);
        return 0;
    case M_CHAR:
        dx = (xhi - xlo) / 4;
        if (p1 == '+') { xlo += dx; xhi -= dx; }
        else if (p1 == '-') { xlo -= 2 * dx; xhi += 2 * dx; }
        else if (p1 == '0') { xlo = -10; xhi = 10; yfix = 0; }
        else if (p1 == 'a' || p1 == 'A') yfix = 0;
        else if (p1 == 'y' || p1 == 'Y') { if (!yfix) yfix = 1; v = (yhi - ylo) / 4; ylo += v; yhi -= v; }
        else if (p1 == 'p' || p1 == 'P') { drucken(); return 0; }
        else return 0;
        kdirty = 1; InvalidateRect(w, 0, TRUE);
        return 0;
    case M_DESTROY: kw = 0; SetFocus(fe); return 0;
    }
    return DefWindowProc(w, msg, p1, p2);
}
static void plot(void)
{
    short W, H; HDC d;
    if (!line[0] && lastin[0]) strcpy(line, lastin);
    if (!line[0]) { fehler(StrGet(hi, 16)); return; }
    { char *p; for (p = line; *p; p++) if (*p == ',') *p = '.'; }
    if (!compile(line)) { fehler(perr); return; }
    strcpy(expr, line); strcpy(lastin, line); line[0] = 0; neu_anzeige();
    if (!vals) { vals = (double FAR *)MemFixed(720 * sizeof(double)); ok = (BYTE FAR *)MemFixed(720); }
    if (!vals || !ok) { fehler(StrGet(hi, 17)); return; }
    kdirty = 1;
    if (kw) { InvalidateRect(kw, 0, TRUE); SetFocus(kw); return; }
    d = GetDC(0); W = GetCaps(d, CAPS_W); H = GetCaps(d, CAPS_H); ReleaseDC(0, d);
    kw = CreateWindow("RdCalcKurve", StrGet(hi, 18), WS_NORMAL | WS_VISIBLE,
                      (short)(W / 10), (short)(H / 12), (short)(W * 4 / 5), (short)(H * 5 / 6), 0, 0, hi);
    if (kw) SetFocus(kw);
}

/* ---------------- Tasten ---------------- */
static void press(short i)
{
    status[0] = 0;
    if (i == B_EQ) calc();
    else if (i == B_C) line[0] = 0;
    else if (i == B_BS) { if (*line) line[strlen(line) - 1] = 0; }
    else if (i == B_DEG) { deg = !deg; cas_set_deg(deg); SetWindowText(btn[B_DEG], deg ? "Deg" : "Rad"); _fstrncpy(status, StrGet(hi, deg ? 19 : 20), sizeof status - 1); }
    else if (i == B_PLOT) { plot(); neu_status(); return; }
    else if (i == B_END) { PostMessage(fe, M_CLOSE, 0, 0); return; }
    else if (i == B_HELP) { char t[700], k[40]; StrLoad(hi, 30, t, sizeof t); StrLoad(hi, 31, k, sizeof k);
                            MsgBox(fe, t, k, MB_OK | MB_INFO); return; }
    else if (bins[i]) insert(bins[i]);
    else insert(blbl[i]);
    neu_anzeige(); neu_status();
}
static DWORD RDAPI proc(HWND w, WORD msg, WORD p1, DWORD p2)
{
    char s[2];
    switch (msg) {
    case M_PAINT: male(w); return 0;
    case M_COMMAND:
        if (p1 >= 100 && p1 < 100 + NB) press((short)(p1 - 100));
        if (!kw) SetFocus(w);                     /* Tastatur bleibt beim Rechner */
        return 0;
    case M_KEY:
        if (p1 == K_ESC) { if (line[0]) press(B_C); else PostMessage(w, M_CLOSE, 0, 0); }
        else if (p1 == K_F1) press(B_HELP);
        else if (p1 == K_F5) press(B_PLOT);
        else if (p1 == K_ENTER) press(B_EQ);
        else if (p1 == K_BACK) press(B_BS);
        else if (p1 == K_DEL) press(B_C);
        else if (p1 == K_UP && lastin[0]) { strcpy(line, lastin); neu_anzeige(); }
        else break;
        return 0;
    case M_CHAR:
        if (p1 >= ' ' && p1 < 127 && strchr("0123456789.+-*/:^(),=;[]!' abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ", (char)p1)) {
            s[0] = (char)p1; s[1] = 0; insert(s); status[0] = 0; neu_anzeige(); neu_status();
        }
        return 0;
    case M_CLOSE: sta_save(); if (kw) DestroyWindow(kw); DestroyWindow(w); return 0;
    case M_DESTROY: PostQuit(0); return 0;
    }
    return DefWindowProc(w, msg, p1, p2);
}

#pragma aux stkbot "stkbot"
static void stapel(void)                          /* Test: nie benutzter Stapel (RD2START loescht ihn mit Nullen) */
{
    extern char stkbot[];
    char t[40]; WORD n = 0;
    while (!stkbot[n] && n < 60000U) n++;
    sprintf(t, "RDCALC: Stapel frei %u", n); DbgPrint(t);
}

short RDAPI RdMain(HINST inst, LPCSTR cmd, WORD show)
{
    WNDCLASS wc; MSG m; HDC d; SYSINFO si; RECT r, c; void FAR *pool; char *p;
    short W, H, fw, fh, i, rows = NB / NCOL, maxl, y0, x;
    (void)show; hi = inst; test = cmd && cmd[0] == 'T';

    KernInfo(&si); fpu = si.fpu != 0 && !(test && cmd[1] == 'N'); efg_p = efg_fmt; efg_s = efg_cnv;   /* Test "TN": ohne 8087 */
    real87 = m8087 = (unsigned char)(fpu ? 1 : 0);   /* Bibliothek: Koprozessor nehmen, wenn da */
    if (!(pool = MemFixed(cas_poolsize()))) { MsgBox(0, StrGet(inst, 17), "RDCALC", MB_OK | MB_ERROR); return 1; }
    cas_setpool(pool);
    InstRes(inst, stafn, sizeof stafn);
    p = strrchr(stafn, '\\'); if (!p) p = strrchr(stafn, ':');
    strcpy(p ? p + 1 : stafn, "RDCALC2.STA");
    sta_load(); cas_set_deg(deg);

    wc.name = "RdCalc"; wc.proc = proc; wc.stil = 0; wc.daten = 0; wc.inst = inst; wc.symbol = 1; wc.hinter = RD_DEFAULT;
    RegisterClass(&wc);
    wc.name = "RdCalcKurve"; wc.proc = kproc; wc.symbol = 1;
    RegisterClass(&wc);
    d = GetDC(0); W = GetCaps(d, CAPS_W); H = GetCaps(d, CAPS_H); th = GetCaps(d, CAPS_TEXTH);
    for (maxl = 0, i = 0; i < NB; i++) { short t = TextWidth(d, blbl[i], -1); if (t > maxl) maxl = t; }
    ReleaseDC(0, d);
    bg = (short)(H < 300 ? 2 : 4); bh = (short)(th + (H < 300 ? 4 : 8));
    bw = (short)(maxl + 10);
    if (NCOL * (bw + bg) + 3 * bg > W - 8) bw = (short)((W - 8 - 3 * bg) / NCOL - bg);
    fe = CreateWindow("RdCalc", StrGet(inst, fpu ? 2 : 1), WS_TITLE | WS_CLOSE | WS_MIN | WS_BORDER | WS_VISIBLE,
                      0, 0, 100, 100, 0, 1, inst);
    if (!fe) return 1;

    GetWindowRect(fe, &r); GetClientRect(fe, &c);
    fw = (short)(NCOL * (bw + bg) + bg + (r.r - r.l - c.r));
    /* Verlauf: so viele Eintraege, wie in die Schirmhoehe passen (CGA 2, VGA 5) */
    i = (short)(H - (r.b - r.t - c.b) - rows * (bh + bg) - 3 * bg - (th + 4) - (th + 12) - 8);
    lh = (short)(i / (2 * (th + 2))); if (lh > 5) lh = 5; if (lh < 1) lh = 1;
    dh = (short)((2 * lh + 1) * (th + 2) + 8); dyh = (short)(bg + dh);
    y0 = (short)(dyh + bg + 2);
    fh = (short)(y0 + rows * (bh + bg) + th + 6 + (r.b - r.t - c.b));
    MoveWindow(fe, (short)((W - fw) / 2), (short)((H - fh) / 2 > 0 ? (H - fh) / 2 : 0), fw, fh, TRUE);
    for (i = 0; i < NB; i++) {
        x = (short)(bg + i % NCOL * (bw + bg));
        btn[i] = CreateWindow(CL_BUTTON, i == B_DEG ? (deg ? "Deg" : "Rad") : blbl[i], WS_CHILD | WS_VISIBLE | (i == B_EQ ? ES_DEFAULT : 0),
                              x, (short)(y0 + i / NCOL * (bh + bg)), bw, bh, fe, (WORD)(100 + i), inst);
    }
    SetFocus(fe);
    if (test) DbgPrint(fpu ? "RDCALC: bereit 8087" : "RDCALC: bereit");
    while (GetMessage(&m)) DispatchMessage(&m);
    if (test) stapel();
    return 0;
}
