/* RetroDesk - RDCALC: wissenschaftlicher Rechner mit Funktionsplotter und Druck.
 * Eigene EXE (Large Model, -fpi): mit 8087 rechnet der Koprozessor, ohne ihn der
 * Emulator der Laufzeitbibliothek (gleiches Ergebnis, nur langsamer).
 * Formeln wie "sin(30)+2^3", "2x^2-1" (x nur fuer die Kurve), ans = letztes Ergebnis.
 * Kurve: F5, Drucken auf LPT1 im Epson-Grafikmodus (ESC L, 9 Nadeln, 120x72 dpi). */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <dos.h>
#include <ctype.h>
#include "gfx.h"
#include "mouse.h"
#include "rdui.h"
#include "rdmenu.h"
#include "snd.h"
#include "rdver.h"

#include "fpu.h"                          /* fpu_detect: 8087 da? sonst rechnet der Emulator */

/* ---------------- Formel -> Postfix ---------------- */
enum { T_NUM, T_X, T_ANS, T_ADD, T_SUB, T_MUL, T_DIV, T_POW, T_NEG,
       T_SIN, T_COS, T_TAN, T_ASIN, T_ACOS, T_ATAN, T_SQRT, T_LN, T_LOG, T_ABS, T_EXP, T_LP };
static const char *fname[] = { "sin", "cos", "tan", "asin", "acos", "atan", "sqrt", "ln", "log", "abs", "exp" };
#define NF 11
#define MAXT 64
typedef struct { unsigned char t; double v; } TOK;
static TOK prog[MAXT];
static int nprog, has_x;
static double ans, xval;
static int deg = 1;                       /* Winkel in Grad */
static const char *perr;                  /* Fehlertext oder 0 */

static int prec(int t) { return t == T_ADD || t == T_SUB ? 1 : t == T_MUL || t == T_DIV ? 2 : t == T_NEG ? 3 : t == T_POW ? 4 : 0; }

static int compile(const char *s)
{
    TOK st[MAXT];
    int ns = 0, val = 0, i, t, n;
    nprog = 0; has_x = 0; perr = 0;
#define EMIT(tt, vv) do { if (nprog >= MAXT) goto lang; prog[nprog].t = (tt); prog[nprog++].v = (vv); } while (0)
#define PUSH(tt) do { if (ns >= MAXT) goto lang; st[ns++].t = (tt); } while (0)
    while (*s) {
        if (*s == ' ') { s++; continue; }
        /* Wert, Funktion oder Klammer nach einem Wert: Malzeichen ergaenzen (2x, 2pi, 3(4+1)) */
        if (val && (isalnum((unsigned char)*s) || *s == '.' || *s == '(')) {
            while (ns && st[ns - 1].t != T_LP && prec(st[ns - 1].t) >= 2 && st[ns - 1].t != T_POW && st[ns - 1].t != T_NEG)
                EMIT(st[--ns].t, 0);
            PUSH(T_MUL); val = 0;
        }
        if (isdigit((unsigned char)*s) || *s == '.') {
            char *e;
            double v = strtod(s, &e);
            if (e == s) goto syn;
            EMIT(T_NUM, v); s = e; val = 1; continue;
        }
        if (isalpha((unsigned char)*s)) {
            char w[8]; n = 0;
            while (isalpha((unsigned char)*s) && n < 7) w[n++] = (char)tolower(*s++);
            w[n] = 0;
            if (!strcmp(w, "x")) { EMIT(T_X, 0); has_x = 1; val = 1; continue; }
            if (!strcmp(w, "pi")) { EMIT(T_NUM, 3.14159265358979323846); val = 1; continue; }
            if (!strcmp(w, "e")) { EMIT(T_NUM, 2.71828182845904523536); val = 1; continue; }
            if (!strcmp(w, "ans")) { EMIT(T_ANS, 0); val = 1; continue; }
            for (i = 0; i < NF && strcmp(w, fname[i]); i++) ;
            if (i == NF) { perr = "Unknown name in formula."; return 0; }
            PUSH(T_SIN + i);
            while (*s == ' ') s++;
            if (*s != '(') { perr = "'(' missing after function."; return 0; }
            continue;
        }
        switch (*s++) {
        case '(': PUSH(T_LP); val = 0; continue;
        case ')':
            while (ns && st[ns - 1].t != T_LP) EMIT(st[--ns].t, 0);
            if (!ns || !val) goto syn;
            ns--;
            if (ns && st[ns - 1].t >= T_SIN && st[ns - 1].t < T_LP) EMIT(st[--ns].t, 0);
            val = 1; continue;
        case '+': if (!val) continue; t = T_ADD; break;    /* Vorzeichen + ueberspringen */
        case '-': t = val ? T_SUB : T_NEG; break;
        case '*': t = T_MUL; break;
        case '/': case ':': t = T_DIV; break;
        case '^': t = T_POW; break;
        default: goto syn;
        }
        if (t != T_NEG && !val) goto syn;
        while (ns && st[ns - 1].t != T_LP && t != T_NEG &&
               (prec(st[ns - 1].t) > prec(t) || (prec(st[ns - 1].t) == prec(t) && t != T_POW)))
            EMIT(st[--ns].t, 0);
        PUSH(t); val = 0;
    }
    if (!val) goto syn;
    while (ns) { if (st[--ns].t == T_LP) { perr = "')' missing."; return 0; } EMIT(st[ns].t, 0); }
    return 1;
syn: perr = "Formula incomplete or invalid character."; return 0;
lang: perr = "Formula too long."; return 0;
}

/* Postfix rechnen. 0 = Fehler (perr), Definitionsbereich vorher pruefen: keine Meldungen der Bibliothek */
static int run(double x, double *out)
{
    double s[MAXT], a, b, k = deg ? 3.14159265358979323846 / 180 : 1;
    int n = 0, i;
    for (i = 0; i < nprog; i++) {
        int t = prog[i].t;
        if (t == T_NUM) { s[n++] = prog[i].v; continue; }
        if (t == T_X) { s[n++] = x; continue; }
        if (t == T_ANS) { s[n++] = ans; continue; }
        a = s[n - 1];
        if (t >= T_ADD && t <= T_POW) {
            b = a; a = s[n - 2]; n--;
            switch (t) {
            case T_ADD: a += b; break;
            case T_SUB: a -= b; break;
            case T_MUL: a *= b; break;
            case T_DIV: if (b == 0) { perr = "Division by 0."; return 0; } a /= b; break;
            case T_POW:
                if (a == 0 && b <= 0) { perr = "0 to the power of 0 or negative."; return 0; }
                if (a < 0 && b != floor(b)) { perr = "Negative number to a fractional power."; return 0; }
                if (a != 0 && b * log10(fabs(a)) > 300) { perr = "Result too large."; return 0; }
                a = pow(a, b); break;
            }
        } else switch (t) {
            case T_NEG: a = -a; break;
            case T_SIN: a = sin(a * k); break;
            case T_COS: a = cos(a * k); break;
            case T_TAN: if (fabs(cos(a * k)) < 1e-12) { perr = "tan undefined there."; return 0; } a = tan(a * k); break;
            case T_ASIN: if (fabs(a) > 1) { perr = "asin only from -1 to 1."; return 0; } a = asin(a) / k; break;
            case T_ACOS: if (fabs(a) > 1) { perr = "acos only from -1 to 1."; return 0; } a = acos(a) / k; break;
            case T_ATAN: a = atan(a) / k; break;
            case T_SQRT: if (a < 0) { perr = "Square root of negative number."; return 0; } a = sqrt(a); break;
            case T_LN: case T_LOG: if (a <= 0) { perr = "Logarithm only of numbers > 0."; return 0; }
                a = t == T_LN ? log(a) : log10(a); break;
            case T_ABS: a = fabs(a); break;
            case T_EXP: if (a > 690) { perr = "Result too large."; return 0; } a = exp(a); break;
        }
        s[n - 1] = a;
        if (a != a || a - a != 0 || fabs(a) > 1e300) { perr = "Result too large."; return 0; }
    }
    *out = s[0];
    return 1;
}

static void fmt(char *b, double v)
{
    if (fabs(v) < 1e-12) v = 0;          /* sin(180) = 0 statt 1.2e-16 */
    sprintf(b, "%.10g", v);
}

/* ---------------- Tastatur (wie RDNET, mit Testskript) ---------------- */
static unsigned short *script;
static int script_n, script_i, script_mode;

static int kb_hit(void)
{
    union REGPACK rp;
    if (script_mode) return script_i < script_n;
    memset(&rp, 0, sizeof rp);
    rp.h.ah = 1;
    intr(0x16, &rp);
    return !(rp.w.flags & INTR_ZF);
}

static unsigned kb_get(void)
{
    union REGS r;
    if (script_mode) {
        unsigned k;
        for (;;) {
            if (script_i >= script_n) return 0x011B;
            k = script[script_i++];
            if ((k & 0xFF00) == 0xFF00) continue;      /* Shift an/aus: hier egal */
            return k;
        }
    }
    r.h.ah = 0;
    int86(0x16, &r, &r);
    return r.x.ax;
}

static int dlg_key(void)
{
    unsigned k;
    if (script_i >= script_n) return -1;
    k = kb_get();
    if ((k & 0xFF) == 0 || (k & 0xFF) == 0xE0) return 0x100 + (k >> 8);
    return (int)(k & 0xFF);
}

static int load_script(const char *fn)
{
    FILE *f = fopen(fn, "rb");
    long n;
    if (!f) return 0;
    fseek(f, 0, SEEK_END); n = ftell(f); fseek(f, 0, SEEK_SET);
    script = malloc((unsigned)n + 2);
    if (!script) { fclose(f); return 0; }
    script_n = (int)fread(script, 2, (unsigned)(n / 2), f);
    fclose(f);
    script_mode = 1;
    ui_keyfn = dlg_key;
    return 1;
}
#define SCRIPT_END() (script_mode && script_i >= script_n)   /* Test: Bild so lassen und beenden */

/* ---------------- Rechner-Fenster ---------------- */
#define NB 40
#define NCOL 8
static const char *blbl[NB] = {
    "7", "8", "9", "/", "(", ")", "sin", "asin",
    "4", "5", "6", "*", "^", "sqrt", "cos", "acos",
    "1", "2", "3", "-", "pi", "e", "tan", "atan",
    "0", ".", "=", "+", "ans", "x", "ln", "log",
    "C", "<-", "Deg", "abs", "exp", "x^2", "Plot", "Quit" };
#define B_EQ 26
#define B_C 32
#define B_BS 33
#define B_DEG 34
#define B_X2 37
#define B_PLOT 38
#define B_END 39
#define NH 6
static char hist[NH][80], line[64], status[80], expr[64];
static char lastin[64];
static int nh, lh, dx, dy, dw, dh, bx0, by0, bw, bstep, quit, shot;
static double xlo = -10, xhi = 10, ylo, yhi; static int yfix;

static int bx(int i) { return bx0 + (i % NCOL) * bstep; }
static int by(int i) { return by0 + (i / NCOL) * (UI_BTN_H + (gfx_type == GFX_CGA ? 3 : 6)); }
static const char *lbl(int i) { return i == B_DEG ? (deg ? "Deg" : "Rad") : blbl[i]; }

static void draw_disp(void)
{
    int i, y, n = nh < lh ? nh : lh;
    char b[90];
    mouse_hide();
    gfx_fill(dx + 1, dy + 1, dw - 2, dh - 2, 1);
    for (i = 0; i < n; i++) {                 /* aelteste oben */
        const char *h = hist[(nh - n + i) % NH], *eq = strchr(h, '\t');
        y = dy + 3 + i * (gfx_fh + 2);
        sprintf(b, "%.*s", (int)(eq - h), h);
        gfx_text(dx + 6, y, b, 0);
        gfx_text(dx + dw - 6 - gfx_textw(eq + 1), y, eq + 1, 0);
    }
    y = dy + dh - gfx_fh - 4;
    gfx_fill(dx + 1, y - 2, dw - 2, 1, 0);
    sprintf(b, "> %s_", line);
    gfx_text(dx + 6, y, b, 0);
    mouse_show();
}

static void draw_status(void)
{
    int y = gfx_h - 2 - gfx_fh - 4;
    mouse_hide();
    gfx_fill(4, y, gfx_w - 10, gfx_fh + 2, 1);
    gfx_text(8, y + 1, status[0] ? status : "Enter = calculate   F5 = plot   Up = last formula   Esc = quit", 0);
    mouse_show();
}

static void draw_all(void)
{
    int i;
    char t[48];
    mouse_hide();
    gfx_fill(0, 0, gfx_w, gfx_h, 0);
    sprintf(t, "Calculator (%s)", fpu_present ? "8087" : "no 8087");
    ui_frame(0, 0, gfx_w - 3, gfx_h - 3, t);
    gfx_rect(dx, dy, dw, dh, 0);
    for (i = 0; i < NB; i++) ui_button_draw(bx(i), by(i), bw, lbl(i), i == B_EQ);
    gfx_fill(4, gfx_h - 2 - gfx_fh - 6, gfx_w - 10, 1, 0);
    mouse_show();
    draw_disp();
    draw_status();
}

static void add_hist(const char *e, const char *r)
{
    sprintf(hist[nh % NH], "%.40s\t= %.24s", e, r);
    nh++;
}

static void calc(void)
{
    double v;
    char r[32];
    status[0] = 0;
    if (!line[0]) return;
    if (!compile(line) || (!has_x && !run(0, &v))) { strcpy(status, perr); snd_play(SND_ERROR); return; }
    strcpy(lastin, line);
    if (has_x) { strcpy(status, "Formula with x: F5 or Plot draws it."); return; }
    ans = v;
    fmt(r, v);
    add_hist(line, r);
    line[0] = 0;
}

static void insert(const char *s)
{
    if (strlen(line) + strlen(s) < 56) strcat(line, s);
}

/* ---------------- Kurve ---------------- */
static int px0, py0, pw, ph;
static double *vals; static char *ok;

static void axis_text(double v, char *b) { fmt(b, v); if (strlen(b) > 9) sprintf(b, "%.3g", v); }

static double nice(double r)               /* Schrittweite 1, 2, 5 * 10^n fuer ca. 8 Teile */
{
    double s = pow(10, floor(log10(r / 8))), m = r / 8 / s;
    return s * (m < 1.5 ? 1 : m < 3.5 ? 2 : m < 7.5 ? 5 : 10);
}

static int ymap(double v) { return py0 + (int)((yhi - v) / (yhi - ylo) * (ph - 1) + 0.5); }

static void plot_status(const char *s)
{
    int y = gfx_h - gfx_fh - 3;
    mouse_hide();
    gfx_fill(0, y - 1, gfx_w, gfx_fh + 3, 1);
    gfx_text(4, y, s, 0);
    mouse_show();
}

static void plot_draw(void)
{
    int i, y, ly = 0, have = 0, a, b, xa;
    double st, t;
    char s[96], n1[16], n2[16], n3[16], n4[16];
    mouse_hide();
    gfx_fill(0, 0, gfx_w, gfx_h, 1);
    axis_text(xlo, n1); axis_text(xhi, n2); axis_text(ylo, n3); axis_text(yhi, n4);
    sprintf(s, "f(x) = %.40s   x: %s .. %s   y: %s .. %s", expr, n1, n2, n3, n4);
    gfx_text(4, 2, s, 0);
    gfx_rect(px0 - 1, py0 - 1, pw + 2, ph + 2, 0);
    /* Gitterpunkte und Achsen */
    st = nice(xhi - xlo);
    for (t = ceil(xlo / st) * st; t <= xhi; t += st) {
        xa = px0 + (int)((t - xlo) / (xhi - xlo) * (pw - 1) + 0.5);
        for (y = py0; y < py0 + ph; y += 4) gfx_fill(xa, y, 1, 1, 0);
    }
    st = nice(yhi - ylo);
    for (t = ceil(ylo / st) * st; t <= yhi; t += st) {
        y = ymap(t);
        for (xa = px0; xa < px0 + pw; xa += 6) gfx_fill(xa, y, 1, 1, 0);
    }
    if (ylo < 0 && yhi > 0) gfx_fill(px0, ymap(0), pw, 1, 0);
    if (xlo < 0 && xhi > 0) gfx_fill(px0 + (int)(-xlo / (xhi - xlo) * (pw - 1) + 0.5), py0, 1, ph, 0);
    /* Kurve: je Spalte senkrechte Strecke zum vorigen Punkt, Spruenge (Polstellen) nicht verbinden */
    for (i = 0; i < pw; i++) {
        if (!ok[i]) { have = 0; continue; }
        y = ymap(vals[i]);
        a = b = y;
        if (have && abs(y - ly) < ph) { if (y > ly) a = ly + 1; else if (y < ly) b = ly - 1; }
        if (a < py0) a = py0;
        if (b > py0 + ph - 1) b = py0 + ph - 1;
        if (a <= b) gfx_fill(px0 + i, a, 1, b - a + 1, 0);
        ly = y; have = 1;
    }
    gfx_fill(0, gfx_h - gfx_fh - 5, gfx_w, 1, 0);
    mouse_show();
    plot_status("P = print   R = range   Esc = back   (mouse shows x and f(x))");
}

static int sample(void)
{
    int i, n = 0;
    double v, mn = 0, mx = 0;
    char s[60];
    for (i = 0; i < pw; i++) {
        double x = xlo + (xhi - xlo) * i / (pw - 1);
        ok[i] = (char)run(x, &v);
        vals[i] = v;
        if (ok[i]) { if (!n || v < mn) mn = v; if (!n || v > mx) mx = v; n++; }
        if ((i & 63) == 63) { sprintf(s, "Calculating ... %d %%", i * 100 / pw); plot_status(s); }
    }
    perr = 0;
    if (!n) return 0;
    if (!yfix) {
        if (mx - mn < 1e-9) { mn -= 1; mx += 1; }
        ylo = mn - (mx - mn) * 0.05; yhi = mx + (mx - mn) * 0.05;
    }
    return 1;
}

static int ask_range(void)
{
    char b[60], n1[16], n2[16], n3[16], n4[16];
    double a, c, d, e;
    int k;
    fmt(n1, xlo); fmt(n2, xhi);
    if (yfix) { fmt(n3, ylo); fmt(n4, yhi); sprintf(b, "%s %s %s %s", n1, n2, n3, n4); }
    else sprintf(b, "%s %s", n1, n2);
    for (;;) {
        if (!ui_inputbox("Range", "x from to, optionally y from to:", b, 50)) return 0;
        k = sscanf(b, "%lf %lf %lf %lf", &a, &c, &d, &e);
        if ((k == 2 || k == 4) && c > a && (k == 2 || e > d)) break;
        snd_play(SND_ERROR);
        if (SCRIPT_END()) return 0;
    }
    xlo = a; xhi = c; yfix = k == 4;
    if (yfix) { ylo = d; yhi = e; }
    return 1;
}

static int prn_err;
static FILE *prn_file;                    /* Test: Druckdaten in Datei statt LPT1 */
static void pout(unsigned char c)
{
    union REGS r;
    if (prn_err) return;
    if (prn_file) { fputc(c, prn_file); return; }
    r.h.ah = 0; r.h.al = c; r.x.dx = 0; int86(0x17, &r, &r);
    if (r.h.ah & 0x29) prn_err = 1;
}
static void pstr(const char *s) { while (*s) pout((unsigned char)*s++); }

static void print_plot(void)
{
    union REGS r;
    int band, i, k, y, x0 = px0 - 1, w = pw + 2, y0 = py0 - 1, h = ph + 2;
    char s[100], n1[16], n2[16], n3[16], n4[16];
    const char *l[2];
    prn_err = 0;
    if (!prn_file) {
        r.h.ah = 2; r.x.dx = 0; int86(0x17, &r, &r);
        if ((r.h.ah & 0x29) || !(r.h.ah & 0x10)) {
            l[0] = "Printer on LPT1 not ready."; l[1] = "Switched on, online, paper?";
            snd_play(SND_ERROR); ui_dlg("Print", l, 2, "OK", NULL, NULL); return;
        }
    }
    plot_status("Printing ... (Esc = cancel)");
    mouse_hide();                         /* Zeiger nicht mitdrucken */
    axis_text(xlo, n1); axis_text(xhi, n2); axis_text(ylo, n3); axis_text(yhi, n4);
    sprintf(s, "f(x) = %.50s   x: %s .. %s   y: %s .. %s\r\n\r\n", expr, n1, n2, n3, n4);
    pstr(s);
    pstr("\x1B" "3\x18");                 /* Zeilenabstand 24/216" = 8 Nadeln */
    for (band = 0; band < h && !prn_err; band += 8) {
        pout(27); pout('L'); pout((unsigned char)(w & 255)); pout((unsigned char)(w >> 8));
        for (i = 0; i < w; i++) {
            unsigned char b = 0, m = 0x80 >> ((x0 + i) & 7);
            unsigned o = (x0 + i) >> 3;
            for (k = 0; k < 8; k++) {
                y = y0 + band + k;
                if (y < y0 + h && !(gfx_vram[gfx_rowofs[y] + o] & m)) b |= 0x80 >> k;
            }
            pout(b);
        }
        pstr("\r\n");
        if (kb_hit() && (kb_get() & 0xFF) == 27) { prn_err = 2; break; }
    }
    pstr("\x1B" "2\r\n");
    mouse_show();                 /* Zeilenabstand zurueck auf 1/6" */
    plot_status(prn_err == 1 ? "Printer error." : prn_err == 2 ? "Printing cancelled." :
                "Printed.   P = print   R = range   Esc = back");
    if (prn_err == 1) snd_play(SND_ERROR);
}

static void plot(void)
{
    int c, mx, my, b, lx = -1;
    char s[80], n1[20], n2[20];
    status[0] = 0;
    if (!line[0] && lastin[0]) strcpy(line, lastin);
    if (!line[0]) { strcpy(status, "First enter a formula with x, e.g. sin(x)."); snd_play(SND_ERROR); draw_status(); return; }
    if (!compile(line)) { strcpy(status, perr); snd_play(SND_ERROR); draw_status(); return; }
    strcpy(expr, line); strcpy(lastin, line);
    if (!has_x) { strcpy(status, "The formula needs x, e.g. sin(x) or x^2-2."); snd_play(SND_ERROR); draw_status(); return; }
    if (!ask_range()) return;
    px0 = 4; py0 = gfx_fh + 6; pw = gfx_w - 8; ph = gfx_h - py0 - gfx_fh - 9;
    for (;;) {
        mouse_hide(); gfx_fill(0, 0, gfx_w, gfx_h, 1); mouse_show();
        if (!sample()) { strcpy(status, "No point in range can be calculated."); snd_play(SND_ERROR); break; }
        plot_draw();
        mouse_read(&lx, &my, &b);            /* Anzeige erst nach Mausbewegung */
        for (c = 0; !c; ) {
            if (SCRIPT_END()) { quit = 1; return; }
            if (kb_hit()) {
                c = (int)(kb_get() & 0xFF);
                c = toupper(c);
                if (c == 'R') c = 'B';        /* englisch: R = Range */
                if (c == 'D' || c == 'P') { print_plot(); c = 0; }
                else if (c != 27 && c != 'B' && c != 13) c = 0;
            }
            mouse_read(&mx, &my, &b);
            if (b & 1) { ui_wait_release(); c = 27; }
            if (mx != lx && mx >= px0 && mx < px0 + pw && my >= py0 && my < py0 + ph) {
                int i = mx - px0;
                lx = mx;
                fmt(n1, xlo + (xhi - xlo) * i / (pw - 1));
                if (ok[i]) fmt(n2, vals[i]); else strcpy(n2, "-");
                sprintf(s, "x = %s   f(x) = %s      P = print  R = range  Esc", n1, n2);
                plot_status(s);
            }
        }
        if (c != 'B' || !ask_range()) break;
    }
    draw_all();
}

/* ---------------- Ablauf ---------------- */
static int hitbtn(int mx, int my)
{
    int i;
    for (i = 0; i < NB; i++) if (ui_in(mx, my, bx(i), by(i), bw, UI_BTN_H)) return i;
    return -1;
}

static void press(int i)
{
    status[0] = 0;
    if (i == B_EQ) calc();
    else if (i == B_C) line[0] = 0;
    else if (i == B_BS) { if (*line) line[strlen(line) - 1] = 0; }
    else if (i == B_DEG) { deg = !deg; mouse_hide(); ui_button_draw(bx(i), by(i), bw, lbl(i), 0); mouse_show();
                           strcpy(status, deg ? "Angles in degrees." : "Angles in radians."); }
    else if (i == B_X2) insert("^2");
    else if (i == B_PLOT) { plot(); return; }
    else if (i == B_END) { quit = 1; return; }
    else if (i >= 0) {
        int f;
        insert(blbl[i]);
        for (f = 0; f < NF; f++) if (!strcmp(blbl[i], fname[f])) insert("(");
    }
    draw_disp(); draw_status();
}

static void key(unsigned k)
{
    int c = k & 0xFF, sc = k >> 8;
    char s[2];
    if (c == 0 || c == 0xE0) {
        if (sc == 0x3F) press(B_PLOT);                         /* F5 */
        else if (sc == 0x48 && lastin[0]) { strcpy(line, lastin); draw_disp(); }
        return;
    }
    if (c == 13 || c == '=') { press(B_EQ); return; }
    if (c == 27) { if (line[0]) press(B_C); else quit = 1; return; }
    if (c == 8) { press(B_BS); return; }
    if (c == ',') c = '.';
    if (c >= ' ' && c < 127 && strchr("0123456789.+-*/:^() abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ", c)) {
        s[0] = (char)c; s[1] = 0; insert(s); status[0] = 0; draw_disp(); draw_status();
    }
}

/* Stand in RDCALC.STA neben der EXE: Verlauf, ans, Eingabe, Grad/Rad, Kurvenbereich */
static char stafn[80];
#define STA_FELDER(F) F(ans) F(deg) F(nh) F(hist) F(line) F(lastin) F(xlo) F(xhi) F(ylo) F(yhi) F(yfix)
#define STA_W(v) fwrite(&v, sizeof v, 1, f);
#define STA_R(v) fread(&v, sizeof v, 1, f);
static void sta_name(const char *exe)
{
    char *p;
    strncpy(stafn, exe, 66); stafn[66] = 0;
    p = strrchr(stafn, '\\'); if (!p) p = strrchr(stafn, ':');
    strcpy(p ? p + 1 : stafn, "RDCALC.STA");
}
static void sta_save(void)
{
    FILE *f = fopen(stafn, "wb");
    if (!f) return;
    fputs("RDC1", f); STA_FELDER(STA_W) fclose(f);
}
static void sta_load(void)
{
    char m[4];
    FILE *f = fopen(stafn, "rb");
    if (!f) return;
    if (fread(m, 4, 1, f) == 1 && !memcmp(m, "RDC1", 4)) { STA_FELDER(STA_R) }
    fclose(f);
    if (nh < 0 || xhi <= xlo) { nh = 0; xlo = -10; xhi = 10; yfix = 0; }   /* kaputte Datei */
    line[63] = lastin[63] = 0;
    for (m[0] = 0; m[0] < NH; m[0]++) hist[m[0]][79] = 0;
}

int main(int argc, char **argv)
{
    int i, mode = GFX_AUTO, smode = SND_AUTO, keep = 0, mx, my, b, last = 0, pn;
    char *e;

    for (i = 1; i < argc; i++) {
        if (gfx_arg(argv[i]) > 0) mode = gfx_arg(argv[i]);
        else if (!stricmp(argv[i], "/NOSND")) smode = SND_OFF;
        else if (!stricmp(argv[i], "/SHOT")) shot = 1;
        else if (!stricmp(argv[i], "/K")) keep = 1;
        else if (!stricmp(argv[i], "/ADL")) smode = SND_ADLIB;
        else if (!stricmp(argv[i], "/SPK")) smode = SND_SPEAKER;
        else if (!stricmp(argv[i], "/KEYS") && i + 1 < argc) load_script(argv[++i]);
        else if (!stricmp(argv[i], "/PRN") && i + 1 < argc) prn_file = fopen(argv[++i], "wb");
    }
    if (!script_mode && (e = getenv("RDCALC_KEYS")) != NULL) {   /* Testmodus aus RDESK heraus */
        if (!load_script(e)) { script_mode = 1; ui_keyfn = dlg_key; }
        else remove(e);
        shot = 1;
    }
    fpu_detect();
    sta_name(argv[0]); sta_load();
    if (!mouse_present() && !script_mode) { puts("RDCALC: No mouse driver loaded (INT 33h)."); return 1; }
    vals = malloc(720 * sizeof(double)); ok = malloc(720);
    if (!vals || !ok) { puts("RDCALC: not enough memory."); return 1; }

    if (keep && smode != SND_AUTO) snd_quick(smode); else snd_init(smode);
    snd_soft = keep;
    gfx_init(mode);
    mouse_init();
    mouse_range(gfx_w, gfx_h);
    mouse_show();

    pn = gfx_type == GFX_CGA ? 3 : 6;         /* Zeilen im Verlauf */
    lh = pn;
    bstep = (gfx_w - 24) / NCOL; bw = bstep - 6; bx0 = 12;
    dx = 12; dy = UI_TITLE_H + 6; dw = gfx_w - 27;
    dh = (pn + 1) * (gfx_fh + 2) + 8;
    by0 = dy + dh + (gfx_type == GFX_CGA ? 5 : 12);
    draw_all();

    while (!quit) {
        if (kb_hit()) key(kb_get());
        else if (script_mode) break;
        mouse_read(&mx, &my, &b);
        if ((b & 1) && !(last & 1)) {
            i = hitbtn(mx, my);
            if (i < 0 && ui_in(mx, my, 0, 0, 18, UI_TITLE_H)) { ui_wait_release(); quit = 1; }
            else if (i >= 0 && ui_button_track(bx(i), by(i), bw)) press(i);
            mouse_read(&mx, &my, &b);
        }
        last = b;
    }
    if (shot) {                              /* Test: Bild und letztes Ergebnis */
        FILE *f = nh ? fopen("CALC.TXT", "w") : NULL;
        if (f) { fputs(strchr(hist[(nh - 1) % NH], '=') + 2, f); fclose(f); }
        mouse_hide(); gfx_dump("SHOT.BIN");
    }
    if (prn_file) fclose(prn_file);
    sta_save();
    mouse_hide();
    if (keep) ui_busy("Calculator", "Loading desktop ...");
    gfx_close();
    snd_close();
    return 0;
}
