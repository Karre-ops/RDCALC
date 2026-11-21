/* RetroDesk - RDCALC: symbolischer Kern (CAS).
 *
 * Ausdruecke als Baum in einem festen Speicher (Pool), Zahlen exakt als Bruch
 * (long/long) mit Rueckfall auf double bei Ueberlauf.  Kanonische Form:
 * ADD/MUL flach, Faktoren und Summanden sortiert, Koeffizient vorn.
 *
 * Befehle: simplify(expr), expand(expr), factor(expr), diff(expr[,x]),
 *          integrate(expr[,x[,a,b]]), solve(gleichung[,x]),
 *          subst(expr,x,wert), approx(expr)
 *
 * Reines C ohne DOS-Abhaengigkeit; der Kern laesst sich auf dem PC testen.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <math.h>
#include "cas.h"

#define NIL     (-1)
#define MAXN    1600            /* Knoten je Befehl */
#define MAXF    64              /* Summanden/Faktoren je Knoten */
#define MAXDEG  8
#define PI      3.14159265358979323846

enum { OP_NUM, OP_VAR, OP_CONST, OP_FUN, OP_POW, OP_MUL, OP_ADD, OP_EQ };
enum { K_PI = 1, K_E };
enum { F_SIN = 1, F_COS, F_TAN, F_ASIN, F_ACOS, F_ATAN, F_LN, F_LOG, F_EXP, F_ABS };
#define NFUN 10

static const char *fnames[NFUN] = {
    "sin", "cos", "tan", "asin", "acos", "atan", "ln", "log", "exp", "abs"
};

typedef struct {
    unsigned char op;
    unsigned char fn;           /* Funktion, Konstante oder Variablenbuchstabe */
    short a, b;                 /* Kinder, NIL = keins */
    long n, d;                  /* exakter Bruch, d > 0; d == 0: double in r */
    double r;
} Node;

typedef struct { int real; long n, d; double r; } Num;

static Node pool[MAXN];
static int ntop;
static char cas_err[80];
static int cas_deg = 1;
static double cas_ans;

static void seterr(const char *s)
{
    strncpy(cas_err, s, sizeof cas_err - 1);
    cas_err[sizeof cas_err - 1] = 0;
}

void cas_set_deg(int d) { cas_deg = d; }

/* ---------------- exakte Zahlen ---------------- */

static long lgcd(long a, long b)
{
    if (a < 0) a = -a;
    if (b < 0) b = -b;
    while (b) { long t = a % b; a = b; b = t; }
    return a ? a : 1;
}

static int lmulck(long a, long b, long *r)
{
    double p = (double)a * (double)b;
    if (p > 2147483000.0 || p < -2147483000.0) return 0;
    *r = a * b;
    return 1;
}

static int laddck(long a, long b, long *r)
{
    double s = (double)a + (double)b;
    if (s > 2147483000.0 || s < -2147483000.0) return 0;
    *r = a + b;
    return 1;
}

static Num num_rat(long n, long d)
{
    Num v; long g;
    v.real = 0; v.r = 0.0; v.n = 0; v.d = 1;
    if (d == 0) { v.real = 1; v.r = 0.0; return v; }
    if (d < 0) { n = -n; d = -d; }
    if (n == 0) return v;
    g = lgcd(n, d);
    v.n = n / g; v.d = d / g;
    return v;
}

static Num num_int(long n) { return num_rat(n, 1); }

static Num num_real(double r)
{
    Num v; v.real = 1; v.r = r; v.n = 0; v.d = 1; return v;
}

static double num_d(Num a) { return a.real ? a.r : (double)a.n / (double)a.d; }

static int num_zero(Num a) { return a.real ? a.r == 0.0 : a.n == 0; }
static int num_one(Num a) { return a.real ? a.r == 1.0 : (a.n == 1 && a.d == 1); }

static int num_neg(Num a)
{
    if (a.real) return a.r < 0.0;
    return a.n < 0;
}

static Num num_negv(Num a)
{
    if (a.real) return num_real(-a.r);
    return num_rat(-a.n, a.d);
}

static Num num_abs(Num a)
{
    if (a.real) return num_real(fabs(a.r));
    return num_rat(a.n < 0 ? -a.n : a.n, a.d);
}

static Num num_add(Num a, Num b)
{
    long t1, t2, rn, rd;
    if (a.real || b.real) return num_real(num_d(a) + num_d(b));
    if (!lmulck(a.n, b.d, &t1) || !lmulck(b.n, a.d, &t2) || !laddck(t1, t2, &rn))
        return num_real(num_d(a) + num_d(b));
    if (!lmulck(a.d, b.d, &rd)) return num_real(num_d(a) + num_d(b));
    return num_rat(rn, rd);
}

static Num num_sub(Num a, Num b) { return num_add(a, num_negv(b)); }

static Num num_mul(Num a, Num b)
{
    long rn, rd;
    if (a.real || b.real) return num_real(num_d(a) * num_d(b));
    if (!lmulck(a.n, b.n, &rn) || !lmulck(a.d, b.d, &rd))
        return num_real(num_d(a) * num_d(b));
    return num_rat(rn, rd);
}

static Num num_div(Num a, Num b)
{
    long rn, rd;
    if (num_zero(b)) { seterr("Division by 0."); return num_real(0.0); }
    if (a.real || b.real) return num_real(num_d(a) / num_d(b));
    if (!lmulck(a.n, b.d, &rn) || !lmulck(a.d, b.n, &rd))
        return num_real(num_d(a) / num_d(b));
    return num_rat(rn, rd);
}

static Num num_pow(Num a, long k)
{
    Num r = num_int(1), base = a;
    long e = k < 0 ? -k : k;
    while (e) {
        if (e & 1) r = num_mul(r, base);
        e >>= 1;
        if (e) base = num_mul(base, base);
    }
    if (k < 0) return num_div(num_int(1), r);
    return r;
}

static int num_cmp(Num a, Num b)
{
    double x, y;
    if (!a.real && !b.real) {
        x = (double)a.n * (double)b.d;
        y = (double)b.n * (double)a.d;
        return x < y ? -1 : x > y ? 1 : 0;
    }
    x = num_d(a); y = num_d(b);
    return x < y ? -1 : x > y ? 1 : 0;
}

static int num_isint(Num a, long *v)
{
    if (a.real) {
        double d = a.r;
        if (d != floor(d) || fabs(d) > 2000000000.0) return 0;
        *v = (long)d;
        return 1;
    }
    if (a.d != 1) return 0;
    *v = a.n;
    return 1;
}

static long isqrtl(long n)
{
    long x, y;
    if (n < 0) return -1;
    if (n < 2) return n;
    x = n; y = (x + 1) / 2;
    while (y < x) { x = y; y = (x + n / x) / 2; }
    return x;
}

static int num_sqrt(Num a, Num *out)
{
    long rn, rd;
    if (a.real) {
        double v;
        if (a.r < 0.0) return 0;
        v = sqrt(a.r);
        if (v != floor(v)) return 0;
        *out = num_real(v);
        return 1;
    }
    if (a.n < 0) return 0;
    rn = isqrtl(a.n);
    rd = isqrtl(a.d);
    if ((double)rn * (double)rn != (double)a.n) return 0;
    if ((double)rd * (double)rd != (double)a.d) return 0;
    *out = num_rat(rn, rd);
    return 1;
}

/* ---------------- Baum ---------------- */

static int newnode(unsigned char op)
{
    int i;
    if (ntop >= MAXN) { seterr("Expression too complex."); return NIL; }
    i = ntop++;
    pool[i].op = op;
    pool[i].fn = 0;
    pool[i].a = pool[i].b = (short)NIL;
    pool[i].n = 0; pool[i].d = 1; pool[i].r = 0.0;
    return i;
}

static int mknum(Num v)
{
    int i = newnode(OP_NUM);
    if (i == NIL) return NIL;
    if (v.real) { pool[i].d = 0; pool[i].r = v.r; }
    else { pool[i].n = v.n; pool[i].d = v.d; }
    return i;
}

static int mkvar(char c)
{
    int i = newnode(OP_VAR);
    if (i == NIL) return NIL;
    pool[i].fn = (unsigned char)c;
    return i;
}

static int mkconst(int k)
{
    int i = newnode(OP_CONST);
    if (i == NIL) return NIL;
    pool[i].fn = (unsigned char)k;
    return i;
}

static int mkfun(int f, int a)
{
    int i;
    if (a == NIL) return NIL;
    i = newnode(OP_FUN);
    if (i == NIL) return NIL;
    pool[i].fn = (unsigned char)f;
    pool[i].a = (short)a;
    return i;
}

static int mkeq(int a, int b)
{
    int i;
    if (a == NIL || b == NIL) return NIL;
    i = newnode(OP_EQ);
    if (i == NIL) return NIL;
    pool[i].a = (short)a;
    pool[i].b = (short)b;
    return i;
}

static int isnum(int n) { return n != NIL && pool[n].op == OP_NUM; }
static int iszero(int n)
{
    if (!isnum(n)) return 0;
    if (pool[n].d == 0) return pool[n].r == 0.0;
    return pool[n].n == 0;
}
static int isone(int n)
{
    if (!isnum(n)) return 0;
    if (pool[n].d == 0) return pool[n].r == 1.0;
    return pool[n].n == 1 && pool[n].d == 1;
}

static Num numof(int n)
{
    if (pool[n].d == 0) return num_real(pool[n].r);
    return num_rat(pool[n].n, pool[n].d);
}

static int mkadd(int x, int y)
{
    int i;
    if (x == NIL || y == NIL) return NIL;
    if (isnum(x) && isnum(y)) return mknum(num_add(numof(x), numof(y)));
    if (iszero(x)) return y;
    if (iszero(y)) return x;
    i = newnode(OP_ADD);
    if (i == NIL) return NIL;
    pool[i].a = (short)x; pool[i].b = (short)y;
    return i;
}

static int mkmul(int x, int y)
{
    int i;
    if (x == NIL || y == NIL) return NIL;
    if (isnum(x) && isnum(y)) return mknum(num_mul(numof(x), numof(y)));
    if (iszero(x) || iszero(y)) return mknum(num_int(0));
    if (isone(x)) return y;
    if (isone(y)) return x;
    i = newnode(OP_MUL);
    if (i == NIL) return NIL;
    pool[i].a = (short)x; pool[i].b = (short)y;
    return i;
}

static int mkpow(int a, int b)
{
    int i;
    Num e;
    long k;
    if (a == NIL || b == NIL) return NIL;
    if (isnum(b)) {
        e = numof(b);
        if (num_zero(e)) return mknum(num_int(1));
        if (num_one(e)) return a;
        if (num_isint(e, &k)) {
            if (iszero(a) && k < 0) { seterr("Division by 0."); return NIL; }
            if (isnum(a)) return mknum(num_pow(numof(a), k));
            if (k == 0) return mknum(num_int(1));
            if (isone(a)) return mknum(num_int(1));
            if (iszero(a) && k > 0) return mknum(num_int(0));
        }
    }
    i = newnode(OP_POW);
    if (i == NIL) return NIL;
    pool[i].a = (short)a; pool[i].b = (short)b;
    return i;
}

/* Wurzel aus einer Zahl: exakt, sonst Quadratfaktoren herausziehen
 * (sqrt(8) = 2*sqrt(2)). */
static int sqrt_num_node(Num a)
{
    Num r;
    long n, d, k = 1, i;
    if (num_sqrt(a, &r)) return mknum(r);
    if (a.real) {
        if (a.r < 0.0) { seterr("Square root of negative number."); return NIL; }
        return mkpow(mknum(a), mknum(num_rat(1, 2)));
    }
    if (a.n < 0) { seterr("Square root of negative number."); return NIL; }
    if (a.n == 0) return mknum(num_int(0));
    if ((double)a.n * (double)a.d > 2000000000.0)
        return mkpow(mknum(a), mknum(num_rat(1, 2)));
    n = a.n * a.d;
    d = a.d;
    for (i = 2; i * i <= n; i++)
        while (n % (i * i) == 0) { n /= i * i; k *= i; }
    if (n == 1) return mknum(num_rat(k, d));
    return mkmul(mknum(num_rat(k, d)), mkpow(mknum(num_int(n)), mknum(num_rat(1, 2))));
}

/* ---------------- flach, Vergleich ---------------- */

static void flatrec(int n, int op, int *out, int *cnt, int max)
{
    if (n == NIL) return;
    if (pool[n].op == op) {
        flatrec(pool[n].a, op, out, cnt, max);
        flatrec(pool[n].b, op, out, cnt, max);
    } else if (*cnt < max) {
        out[(*cnt)++] = n;
    } else {
        seterr("Expression too complex.");
    }
}

static int rankof(int n)
{
    switch (pool[n].op) {
    case OP_NUM: return 0;
    case OP_CONST: return 1;
    case OP_VAR: return 2;
    case OP_FUN: return 3;
    case OP_POW: return 4;
    case OP_MUL: return 5;
    default: return 6;
    }
}

/* Gewicht eines Terms: Grad einer Potenz, sonst Anzahl der Faktoren.
 * Damit steht in Summen die hoechste Potenz vorn und die Zahl am Ende. */
static int weight(int n)
{
    int f[MAXF], nf = 0, i, w = 0;
    long k;
    switch (pool[n].op) {
    case OP_NUM: return 0;
    case OP_VAR:
    case OP_CONST: return 1;
    case OP_FUN: return 1 + weight(pool[n].a);
    case OP_POW:
        if (isnum(pool[n].b) && num_isint(numof(pool[n].b), &k) && k > 0 && k < 100)
            return (int)k * weight(pool[n].a);
        return 1 + weight(pool[n].a);
    default:
        flatrec(n, pool[n].op, f, &nf, MAXF);
        for (i = 0; i < nf; i++) w += weight(f[i]);
        return w;
    }
}

static int cmp(int x, int y);
static int cmp_add_term(int a, int b);

static int cmp_add_term(int a, int b)
{
    int wa = weight(a), wb = weight(b);
    if (wa != wb) return wa > wb ? -1 : 1;
    return cmp(a, b);
}

static int cmp(int x, int y)
{
    int rx, ry, i, na, nb, fa[MAXF], fb[MAXF], c;
    if (x == y) return 0;
    if (x == NIL) return -1;
    if (y == NIL) return 1;
    rx = rankof(x); ry = rankof(y);
    if (rx != ry) return rx < ry ? -1 : 1;
    switch (pool[x].op) {
    case OP_NUM: return num_cmp(numof(x), numof(y));
    case OP_VAR:
    case OP_CONST:
        return pool[x].fn < pool[y].fn ? -1 : pool[x].fn > pool[y].fn ? 1 : 0;
    case OP_FUN:
        if (pool[x].fn != pool[y].fn) return pool[x].fn < pool[y].fn ? -1 : 1;
        return cmp(pool[x].a, pool[y].a);
    case OP_POW:
        c = cmp(pool[x].a, pool[y].a);
        if (c) return c;
        return cmp(pool[x].b, pool[y].b);
    default:
        na = nb = 0;
        flatrec(x, pool[x].op, fa, &na, MAXF);
        flatrec(y, pool[y].op, fb, &nb, MAXF);
        for (i = 0; i < na && i < nb; i++) {
            c = (pool[x].op == OP_ADD) ? cmp_add_term(fa[i], fb[i]) : cmp(fa[i], fb[i]);
            if (c) return c;
        }
        return na < nb ? -1 : na > nb ? 1 : 0;
    }
}

static int eq(int x, int y)
{
    if (x == y) return 1;
    if (x == NIL || y == NIL) return 0;
    if (pool[x].op != pool[y].op) return 0;
    switch (pool[x].op) {
    case OP_NUM: return num_cmp(numof(x), numof(y)) == 0;
    case OP_VAR:
    case OP_CONST: return pool[x].fn == pool[y].fn;
    case OP_FUN: return pool[x].fn == pool[y].fn && eq(pool[x].a, pool[y].a);
    default: return eq(pool[x].a, pool[y].a) && eq(pool[x].b, pool[y].b);
    }
}

/* Vorzeichen eines Summanden (-1/0/1) */
static int signof(int n)
{
    int f[MAXF], nf = 0;
    Num v;
    if (isnum(n)) {
        v = numof(n);
        if (num_zero(v)) return 0;
        return num_neg(v) ? -1 : 1;
    }
    if (pool[n].op == OP_MUL) {
        flatrec(n, OP_MUL, f, &nf, MAXF);
        if (nf > 0 && isnum(f[0])) {
            v = numof(f[0]);
            if (num_zero(v)) return 0;
            return num_neg(v) ? -1 : 1;
        }
    }
    return 1;
}

/* ---------------- Ausgabe ---------------- */

static char *rbuf;
static int rlen, rmax, rpos;

static void rput(const char *s)
{
    while (*s && rlen < rmax - 1) rbuf[rlen++] = *s++;
    rbuf[rlen] = 0;
}

static void rch(char c)
{
    if (rlen < rmax - 1) { rbuf[rlen++] = c; rbuf[rlen] = 0; }
}

static int plevel(int n)
{
    switch (pool[n].op) {
    case OP_ADD: return 0;
    case OP_MUL: return 1;
    case OP_POW: return 2;
    default: return 3;
    }
}

static void rnd(int n, int lvl);

static void rnd_num(Num v)
{
    char t[40];
    if (rpos) v = num_abs(v);
    if (v.real) {
        double d = v.r;
        if (fabs(d) < 1e-12) d = 0.0;
        sprintf(t, "%.10g", d);
    } else if (v.d == 1) {
        sprintf(t, "%ld", v.n);
    } else {
        sprintf(t, "%ld/%ld", v.n, v.d);
    }
    rput(t);
}

static void rnd_add(int n)
{
    int f[MAXF], nf = 0, i, s;
    flatrec(n, OP_ADD, f, &nf, MAXF);
    for (i = 0; i < nf; i++) {
        s = signof(f[i]);
        if (i == 0) {
            if (s < 0) rch('-');
        } else {
            rch(s < 0 ? '-' : '+');
        }
        rpos = 1;
        rnd(f[i], 1);
        rpos = 0;
    }
}

static void rnd_mul(int n)
{
    int f[MAXF], nf = 0, i, first = 1, dcount;
    int numf[MAXF], nnum = 0, denf[MAXF], nden = 0;
    Num cf = num_int(1);

    flatrec(n, OP_MUL, f, &nf, MAXF);
    for (i = 0; i < nf; i++) {
        if (isnum(f[i])) { cf = num_mul(cf, numof(f[i])); continue; }
        if (pool[f[i]].op == OP_POW && isnum(pool[f[i]].b) &&
            num_neg(numof(pool[f[i]].b)))
            denf[nden++] = f[i];
        else
            numf[nnum++] = f[i];
    }
    if (rpos) cf = num_abs(cf);
    if (num_neg(cf)) { rch('-'); cf = num_negv(cf); }
    cf = num_abs(cf);

    if (cf.real) {
        if (fabs(cf.r - 1.0) > 1e-12) { rnd_num(cf); first = 0; }
    } else if (cf.n != 1 || (nnum == 0 && nden == 0)) {
        rnd_num(num_int(cf.n)); first = 0;
    }
    for (i = 0; i < nnum; i++) {
        if (!first) rch('*');
        rnd(numf[i], 1);
        first = 0;
    }

    dcount = nden + ((!cf.real && cf.d != 1) ? 1 : 0);
    if (!dcount) return;
    if (first) { rch('1'); }
    first = 1;
    rch('/');
    if (dcount > 1) rch('(');
    if (!cf.real && cf.d != 1) { rnd_num(num_int(cf.d)); first = 0; }
    for (i = 0; i < nden; i++) {
        int p = denf[i];
        Num e = num_abs(numof(pool[p].b));
        if (!first) rch('*');
        if (plevel(pool[p].a) < 2) { rch('('); rnd(pool[p].a, 0); rch(')'); }
        else rnd(pool[p].a, 3);
        if (!(e.d == 1 && e.n == 1)) {
            rch('^');
            if (!e.real && e.d != 1) { rch('('); rnd_num(e); rch(')'); }
            else rnd_num(e);
        }
        first = 0;
    }
    if (dcount > 1) rch(')');
}

static void rnd_pow(int n)
{
    int a = pool[n].a, b = pool[n].b;
    Num e;
    if (isnum(b)) {
        e = numof(b);
        if (!e.real && e.d == 2 && e.n == 1) {
            rput("sqrt("); rnd(a, 0); rch(')');
            return;
        }
        if (!e.real && e.n < 0) {
            rput("1/");
            if (plevel(a) < 2) { rch('('); rnd(a, 0); rch(')'); }
            else rnd(a, 3);
            if (!(e.d == 1 && e.n == -1)) {
                Num pe = num_negv(e);
                rch('^');
                if (!pe.real && pe.d != 1) { rch('('); rnd_num(pe); rch(')'); }
                else rnd_num(pe);
            }
            return;
        }
    }
    if (plevel(a) < 2) { rch('('); rnd(a, 0); rch(')'); }
    else rnd(a, 3);
    rch('^');
    if (plevel(b) <= 2 || (isnum(b) && (numof(b).real || numof(b).d != 1))) {
        rch('('); rnd(b, 0); rch(')');
    } else {
        rnd(b, 2);
    }
}

static void rnd(int n, int lvl)
{
    int need;
    if (n == NIL) { rput("?"); return; }
    need = plevel(n) < lvl;
    if (need) rch('(');
    switch (pool[n].op) {
    case OP_NUM: rnd_num(numof(n)); break;
    case OP_VAR: rch((char)pool[n].fn); break;
    case OP_CONST: rput(pool[n].fn == K_PI ? "pi" : "e"); break;
    case OP_FUN:
        rput(fnames[pool[n].fn - 1]);
        rch('(');
        rnd(pool[n].a, 0);
        rch(')');
        break;
    case OP_POW: rnd_pow(n); break;
    case OP_MUL: rnd_mul(n); break;
    case OP_ADD: rnd_add(n); break;
    case OP_EQ:
        rnd(pool[n].a, 0); rput(" = "); rnd(pool[n].b, 0);
        break;
    }
    if (need) rch(')');
}

static void render_to(int n, char *dst, int sz)
{
    rbuf = dst; rlen = 0; rmax = sz; rpos = 0;
    if (sz > 0) dst[0] = 0;
    rnd(n, 0);
}

/* ---------------- Vereinfachen ---------------- */

static int simp(int n);

static int has_anyvar(int n)
{
    if (n == NIL) return 0;
    if (pool[n].op == OP_VAR) return 1;
    return has_anyvar(pool[n].a) || has_anyvar(pool[n].b);
}

static void split_coeff(int t, Num *c, int *rest)
{
    int f[MAXF], nf = 0, i, r = NIL;
    if (isnum(t)) { *c = numof(t); *rest = NIL; return; }
    if (pool[t].op == OP_MUL) {
        flatrec(t, OP_MUL, f, &nf, MAXF);
        if (nf > 0 && isnum(f[0])) {
            *c = numof(f[0]);
            for (i = 1; i < nf; i++)
                r = (r == NIL) ? f[i] : mkmul(r, f[i]);
            *rest = r;
            return;
        }
    }
    *c = num_int(1);
    *rest = t;
}

/* k*pi erkennen */
static int pi_mult(int a, Num *k)
{
    int l, r;
    if (pool[a].op == OP_CONST && pool[a].fn == K_PI) { *k = num_int(1); return 1; }
    if (isnum(a) && num_zero(numof(a))) { *k = num_int(0); return 1; }
    if (pool[a].op == OP_MUL) {
        l = pool[a].a; r = pool[a].b;
        if (isnum(l) && pool[r].op == OP_CONST && pool[r].fn == K_PI) {
            *k = numof(l);
            return 1;
        }
    }
    return 0;
}

static int simp_fun(int f, int a)
{
    Num v, k;
    long m;
    if (isnum(a)) {
        v = numof(a);
        switch (f) {
        case F_SIN:
            if (num_zero(v)) return mknum(num_int(0));
            break;
        case F_COS:
            if (num_zero(v)) return mknum(num_int(1));
            break;
        case F_TAN:
            if (num_zero(v)) return mknum(num_int(0));
            break;
        case F_ASIN:
            if (num_zero(v)) return mknum(num_int(0));
            break;
        case F_ACOS:
            if (num_one(v)) return mknum(num_int(0));
            break;
        case F_ATAN:
            if (num_zero(v)) return mknum(num_int(0));
            break;
        case F_LN:
            if (num_one(v)) return mknum(num_int(0));
            if (num_neg(v) || num_zero(v)) { seterr("ln needs a positive argument."); return NIL; }
            break;
        case F_LOG:
            if (num_one(v)) return mknum(num_int(0));
            if (num_neg(v) || num_zero(v)) { seterr("log needs a positive argument."); return NIL; }
            break;
        case F_EXP:
            if (num_zero(v)) return mknum(num_int(1));
            break;
        case F_ABS:
            return mknum(num_abs(v));
        }
    }
    if (f == F_EXP && pool[a].op == OP_FUN && pool[a].fn == F_LN) return pool[a].a;
    if (f == F_LN && pool[a].op == OP_FUN && pool[a].fn == F_EXP) return pool[a].a;
    if (f == F_LN && pool[a].op == OP_CONST && pool[a].fn == K_E) return mknum(num_int(1));
    if (f == F_ABS && pool[a].op == OP_FUN && pool[a].fn == F_ABS) return a;
    if (f == F_ABS && pool[a].op == OP_CONST) return a;   /* e und pi sind positiv */

    /* sin/cos an Vielfachen von pi */
    if ((f == F_SIN || f == F_COS || f == F_TAN) && pi_mult(a, &k)) {
        if (num_isint(k, &m)) {
            if (f == F_COS) return mknum(num_int((m & 1) ? -1 : 1));
            return mknum(num_int(0));
        }
        /* k = m + 1/2 */
        if (!k.real && k.d == 2) {
            m = (k.n - 1) / 2;
            if (f == F_SIN) return mknum(num_int((m & 1) ? -1 : 1));
            if (f == F_COS) return mknum(num_int(0));
        }
    }
    /* sin(-x) = -sin(x), cos(-x) = cos(x) */
    if (f == F_SIN || f == F_TAN || f == F_COS) {
        Num c;
        int rest;
        split_coeff(a, &c, &rest);
        if (rest != NIL && num_neg(c)) {
            int inner = simp_fun(f, rest);
            if (inner == NIL) return NIL;
            if (f == F_COS) return inner;
            return mkmul(mknum(num_int(-1)), inner);
        }
    }
    return mkfun(f, a);
}

static int simp_pow(int a, int b)
{
    Num e;
    long k;
    if (isnum(b)) {
        e = numof(b);
        if (num_zero(e)) return mknum(num_int(1));
        if (num_one(e)) return a;
        if (isnum(a)) {
            if (num_isint(e, &k)) return mknum(num_pow(numof(a), k));
            if (!e.real && e.d == 2 && e.n == 1)
                return sqrt_num_node(numof(a));
        }
    }
    if (iszero(a)) {
        if (isnum(b) && num_neg(numof(b))) { seterr("Division by 0."); return NIL; }
        return mknum(num_int(0));
    }
    if (isone(a)) return mknum(num_int(1));
    if (pool[a].op == OP_CONST && pool[a].fn == K_E &&
        pool[b].op == OP_FUN && pool[b].fn == F_LN)
        return pool[b].a;
    if (pool[a].op == OP_POW && isnum(pool[a].b) && isnum(b))
        return mkpow(pool[a].a, mknum(num_mul(numof(pool[a].b), numof(b))));
    if (pool[a].op == OP_MUL && isnum(b)) {
        if (num_isint(numof(b), &k) && k >= -6 && k <= 6) {
            int f[MAXF], nf = 0, i, r = mknum(num_int(1));
            flatrec(a, OP_MUL, f, &nf, MAXF);
            for (i = 0; i < nf; i++) {
                int p = simp_pow(f[i], b);
                if (p == NIL) return NIL;
                r = mkmul(r, p);
                if (r == NIL) return NIL;
            }
            return r;
        }
    }
    return mkpow(a, b);
}

static int simp_mul(int n)
{
    int f[MAXF], nf = 0, i, j, ng = 0, base[MAXF], terms[MAXF], nt = 0, t, r;
    Num cf = num_int(1), ex[MAXF], e;
    flatrec(n, OP_MUL, f, &nf, MAXF);
    for (i = 0; i < nf; i++) {
        f[i] = simp(f[i]);
        if (f[i] == NIL) return NIL;
    }
    for (i = 0; i < nf; i++) {
        int b;
        if (isnum(f[i])) { cf = num_mul(cf, numof(f[i])); continue; }
        if (pool[f[i]].op == OP_POW && isnum(pool[f[i]].a) && isnum(pool[f[i]].b)) {
            long k;
            if (num_isint(numof(pool[f[i]].b), &k)) {
                cf = num_mul(cf, num_pow(numof(pool[f[i]].a), k));
                continue;
            }
        }
        if (pool[f[i]].op == OP_POW && isnum(pool[f[i]].b)) {
            b = pool[f[i]].a;
            e = numof(pool[f[i]].b);
        } else {
            b = f[i];
            e = num_int(1);
        }
        for (j = 0; j < ng; j++)
            if (eq(base[j], b)) { ex[j] = num_add(ex[j], e); break; }
        if (j == ng) {
            if (ng >= MAXF) { seterr("Expression too complex."); return NIL; }
            base[ng] = b; ex[ng] = e; ng++;
        }
    }
    if (num_zero(cf)) return mknum(num_int(0));
    for (j = 0; j < ng; j++) {
        if (num_zero(ex[j])) continue;
        if (num_one(ex[j])) t = base[j];
        else t = mkpow(base[j], mknum(ex[j]));
        if (t == NIL) return NIL;
        terms[nt++] = t;
    }
    for (i = 1; i < nt; i++) {
        int v = terms[i];
        for (j = i; j > 0 && cmp(terms[j - 1], v) > 0; j--) terms[j] = terms[j - 1];
        terms[j] = v;
    }
    if (nt == 1 && !num_one(cf) && pool[terms[0]].op == OP_ADD &&
        !has_anyvar(terms[0])) {
        int s = terms[0];
        return simp(mkadd(mkmul(mknum(cf), pool[s].a), mkmul(mknum(cf), pool[s].b)));
    }
    r = num_one(cf) ? NIL : mknum(cf);
    for (i = 0; i < nt; i++)
        r = (r == NIL) ? terms[i] : mkmul(r, terms[i]);
    if (r == NIL) r = mknum(num_int(1));
    return r;
}

static int simp_add(int n)
{
    int f[MAXF], nf = 0, i, j, nr = 0, rest[MAXF], terms[MAXF], nt = 0, t;
    Num cf[MAXF], c;
    flatrec(n, OP_ADD, f, &nf, MAXF);
    for (i = 0; i < nf; i++) {
        f[i] = simp(f[i]);
        if (f[i] == NIL) return NIL;
    }
    for (i = 0; i < nf; i++) {
        split_coeff(f[i], &c, &t);
        for (j = 0; j < nr; j++)
            if (eq(rest[j], t)) { cf[j] = num_add(cf[j], c); break; }
        if (j == nr) {
            if (nr >= MAXF) { seterr("Expression too complex."); return NIL; }
            rest[nr] = t; cf[nr] = c; nr++;
        }
    }
    for (j = 0; j < nr; j++) {
        if (num_zero(cf[j])) continue;
        if (rest[j] == NIL) t = mknum(cf[j]);
        else t = mkmul(mknum(cf[j]), rest[j]);
        if (t == NIL) return NIL;
        terms[nt++] = t;
    }
    if (!nt) return mknum(num_int(0));
    for (i = 1; i < nt; i++) {
        int v = terms[i];
        for (j = i; j > 0 && cmp_add_term(terms[j - 1], v) > 0; j--) terms[j] = terms[j - 1];
        terms[j] = v;
    }
    t = terms[0];
    for (i = 1; i < nt; i++) t = mkadd(t, terms[i]);
    return t;
}

static int simp(int n)
{
    int a, b;
    if (n == NIL) return NIL;
    switch (pool[n].op) {
    case OP_NUM:
    case OP_VAR:
    case OP_CONST:
        return n;
    case OP_FUN:
        a = simp(pool[n].a);
        if (a == NIL) return NIL;
        return simp_fun(pool[n].fn, a);
    case OP_POW:
        a = simp(pool[n].a);
        b = simp(pool[n].b);
        if (a == NIL || b == NIL) return NIL;
        return simp_pow(a, b);
    case OP_MUL: return simp_mul(n);
    case OP_ADD: return simp_add(n);
    case OP_EQ:
        a = simp(pool[n].a);
        b = simp(pool[n].b);
        return mkeq(a, b);
    }
    return n;
}

/* ---------------- Ausmultiplizieren ---------------- */

static int dist(int a, int b)
{
    if (pool[a].op == OP_ADD)
        return mkadd(dist(pool[a].a, b), dist(pool[a].b, b));
    if (pool[b].op == OP_ADD)
        return mkadd(dist(a, pool[b].a), dist(a, pool[b].b));
    return mkmul(a, b);
}

static int expand(int n)
{
    int a, b, i, r;
    long k;
    if (n == NIL) return NIL;
    switch (pool[n].op) {
    case OP_ADD:
        a = expand(pool[n].a);
        b = expand(pool[n].b);
        return mkadd(a, b);
    case OP_MUL:
        a = expand(pool[n].a);
        b = expand(pool[n].b);
        return simp(dist(a, b));
    case OP_POW:
        a = expand(pool[n].a);
        b = pool[n].b;
        if (isnum(b) && num_isint(numof(b), &k) && k >= 0 && k <= 10) {
            r = mknum(num_int(1));
            for (i = 0; i < k; i++) {
                r = simp(dist(r, a));
                if (r == NIL) return NIL;
            }
            return r;
        }
        return mkpow(a, expand(b));
    case OP_FUN:
        return mkfun(pool[n].fn, expand(pool[n].a));
    case OP_EQ:
        return mkeq(expand(pool[n].a), expand(pool[n].b));
    }
    return n;
}

/* ---------------- Variablen und Polynome ---------------- */

static int hasvar(int n, char v)
{
    if (n == NIL) return 0;
    switch (pool[n].op) {
    case OP_VAR: return pool[n].fn == (unsigned char)v;
    case OP_NUM:
    case OP_CONST: return 0;
    default: return hasvar(pool[n].a, v) || hasvar(pool[n].b, v);
    }
}

static int getpoly(int n, char v, Num *c, int *deg)
{
    Num t[MAXDEG + 1], u[MAXDEG + 1];
    int dt, du, i, j;
    long k;
    for (i = 0; i <= MAXDEG; i++) c[i] = num_int(0);
    *deg = 0;
    if (n == NIL) return 0;
    switch (pool[n].op) {
    case OP_NUM:
        c[0] = numof(n);
        return 1;
    case OP_CONST:
        return 0;
    case OP_VAR:
        if (pool[n].fn != (unsigned char)v) return 0;
        c[1] = num_int(1);
        *deg = 1;
        return 1;
    case OP_ADD:
        if (!getpoly(pool[n].a, v, t, &dt)) return 0;
        if (!getpoly(pool[n].b, v, u, &du)) return 0;
        if (du > dt) dt = du;
        for (i = 0; i <= dt; i++) c[i] = num_add(t[i], u[i]);
        *deg = dt;
        return 1;
    case OP_MUL:
        if (!getpoly(pool[n].a, v, t, &dt)) return 0;
        if (!getpoly(pool[n].b, v, u, &du)) return 0;
        if (dt + du > MAXDEG) return 0;
        for (i = 0; i <= dt + du; i++) c[i] = num_int(0);
        for (i = 0; i <= dt; i++)
            for (j = 0; j <= du; j++)
                c[i + j] = num_add(c[i + j], num_mul(t[i], u[j]));
        *deg = dt + du;
        return 1;
    case OP_POW:
        if (!isnum(pool[n].b) || !num_isint(numof(pool[n].b), &k)) return 0;
        if (k < 0 || k > MAXDEG) return 0;
        if (pool[pool[n].a].op == OP_VAR && pool[pool[n].a].fn == (unsigned char)v) {
            c[k] = num_int(1);
            *deg = (int)k;
            return 1;
        }
        return 0;
    }
    return 0;
}

static void poly_trim(Num *c, int *deg)
{
    while (*deg > 0 && num_zero(c[*deg])) (*deg)--;
}

static int poly_node(Num *c, int deg, char v)
{
    int r = mknum(num_int(0)), p = mknum(num_int(1)), i, t;
    for (i = 0; i <= deg; i++) {
        t = mkmul(mknum(c[i]), p);
        r = mkadd(r, t);
        p = mkmul(p, mkvar(v));
    }
    return r;
}

/* ---------------- Ableiten ---------------- */

static int deriv(int n, char v)
{
    int a, b, da, db, t;
    if (n == NIL) return NIL;
    switch (pool[n].op) {
    case OP_NUM:
    case OP_CONST:
        return mknum(num_int(0));
    case OP_VAR:
        return mknum(num_int(pool[n].fn == (unsigned char)v ? 1 : 0));
    case OP_ADD:
        return mkadd(deriv(pool[n].a, v), deriv(pool[n].b, v));
    case OP_MUL:
        a = pool[n].a; b = pool[n].b;
        da = deriv(a, v); db = deriv(b, v);
        return mkadd(mkmul(da, b), mkmul(a, db));
    case OP_POW:
        a = pool[n].a; b = pool[n].b;
        if (!hasvar(b, v)) {
            int bm1 = mkadd(b, mknum(num_int(-1)));
            t = mkpow(a, bm1);
            return mkmul(mkmul(b, t), deriv(a, v));
        } else {
            int lna = mkfun(F_LN, a);
            int t1 = mkmul(deriv(b, v), lna);
            int t2 = mkmul(b, mkmul(deriv(a, v), mkpow(a, mknum(num_int(-1)))));
            return mkmul(n, mkadd(t1, t2));
        }
    case OP_FUN:
        a = pool[n].a;
        da = deriv(a, v);
        switch (pool[n].fn) {
        case F_SIN: t = mkfun(F_COS, a); break;
        case F_COS: t = mkmul(mknum(num_int(-1)), mkfun(F_SIN, a)); break;
        case F_TAN: t = mkpow(mkfun(F_COS, a), mknum(num_int(-2))); break;
        case F_LN: t = mkpow(a, mknum(num_int(-1))); break;
        case F_LOG:
            t = mkpow(mkmul(a, mkfun(F_LN, mknum(num_int(10)))), mknum(num_int(-1)));
            break;
        case F_EXP: t = mkfun(F_EXP, a); break;
        case F_ASIN:
            t = mkpow(mkadd(mknum(num_int(1)), mkmul(mknum(num_int(-1)),
                    mkpow(a, mknum(num_int(2))))), mknum(num_rat(-1, 2)));
            break;
        case F_ACOS:
            t = mkmul(mknum(num_int(-1)),
                    mkpow(mkadd(mknum(num_int(1)), mkmul(mknum(num_int(-1)),
                    mkpow(a, mknum(num_int(2))))), mknum(num_rat(-1, 2))));
            break;
        case F_ATAN:
            t = mkpow(mkadd(mknum(num_int(1)), mkpow(a, mknum(num_int(2)))),
                    mknum(num_int(-1)));
            break;
        default: /* abs */
            t = mkmul(a, mkpow(mkfun(F_ABS, a), mknum(num_int(-1))));
            break;
        }
        return mkmul(t, da);
    }
    return mknum(num_int(0));
}

/* ---------------- Integrieren ---------------- */

static int lin_in(int n, char v, int *a, int *b)
{
    int f[MAXF], nf = 0, i, dep = -1, A = NIL, a1, b1, a2, b2;
    if (n == NIL) return 0;
    if (!hasvar(n, v)) {
        *a = mknum(num_int(0));
        *b = n;
        return 1;
    }
    if (pool[n].op == OP_VAR) {
        if (pool[n].fn != (unsigned char)v) return 0;
        *a = mknum(num_int(1));
        *b = mknum(num_int(0));
        return 1;
    }
    if (pool[n].op == OP_ADD) {
        if (!lin_in(pool[n].a, v, &a1, &b1)) return 0;
        if (!lin_in(pool[n].b, v, &a2, &b2)) return 0;
        if (!iszero(a1) && !iszero(a2)) return 0;
        *a = mkadd(a1, a2);
        *b = mkadd(b1, b2);
        return 1;
    }
    if (pool[n].op == OP_MUL) {
        flatrec(n, OP_MUL, f, &nf, MAXF);
        for (i = 0; i < nf; i++) {
            if (hasvar(f[i], v)) {
                if (dep >= 0) return 0;
                dep = i;
            } else {
                A = (A == NIL) ? f[i] : mkmul(A, f[i]);
            }
        }
        if (dep < 0) return 0;
        if (!lin_in(f[dep], v, &a1, &b1)) return 0;
        if (iszero(a1)) return 0;
        *a = (A == NIL) ? a1 : mkmul(A, a1);
        *b = (A == NIL) ? b1 : mkmul(A, b1);
        return 1;
    }
    return 0;
}

static int integ(int n, char v)
{
    int a, b, f[MAXF], nf = 0, i, dep = -1, A = NIL, D, la, lb;
    if (n == NIL) return NIL;
    if (!hasvar(n, v)) return mkmul(n, mkvar(v));
    switch (pool[n].op) {
    case OP_VAR:
        return mkmul(mknum(num_rat(1, 2)), mkpow(mkvar(v), mknum(num_int(2))));
    case OP_ADD:
        a = integ(pool[n].a, v);
        if (a == NIL) return NIL;
        b = integ(pool[n].b, v);
        if (b == NIL) return NIL;
        return mkadd(a, b);
    case OP_MUL:
        flatrec(n, OP_MUL, f, &nf, MAXF);
        for (i = 0; i < nf; i++) {
            if (hasvar(f[i], v)) {
                if (dep >= 0) return NIL;
                dep = i;
            } else {
                A = (A == NIL) ? f[i] : mkmul(A, f[i]);
            }
        }
        if (dep < 0) return mkmul(n, mkvar(v));
        D = integ(f[dep], v);
        if (D == NIL) return NIL;
        return (A == NIL) ? D : mkmul(A, D);
    case OP_POW:
        if (!hasvar(pool[n].b, v) && lin_in(pool[n].a, v, &la, &lb) && !iszero(la)) {
            if (isnum(pool[n].b) && num_one(num_negv(numof(pool[n].b)))) {
                /* 1/u -> ln|u| */
                return mkmul(mkfun(F_LN, mkfun(F_ABS, pool[n].a)),
                             mkpow(la, mknum(num_int(-1))));
            }
            a = mkpow(pool[n].a, mkadd(pool[n].b, mknum(num_int(1))));
            b = mkmul(la, mkadd(pool[n].b, mknum(num_int(1))));
            return mkmul(a, mkpow(b, mknum(num_int(-1))));
        }
        if (pool[pool[n].a].op == OP_CONST && pool[pool[n].a].fn == K_E &&
            lin_in(pool[n].b, v, &la, &lb) && !iszero(la))
            return mkmul(mkpow(pool[n].a, pool[n].b),
                         mkpow(la, mknum(num_int(-1))));
        return NIL;
    case OP_FUN:
        if (lin_in(pool[n].a, v, &la, &lb) && !iszero(la)) {
            switch (pool[n].fn) {
            case F_SIN: a = mkmul(mknum(num_int(-1)), mkfun(F_COS, pool[n].a)); break;
            case F_COS: a = mkfun(F_SIN, pool[n].a); break;
            case F_EXP: a = mkfun(F_EXP, pool[n].a); break;
            case F_LN:
                a = mkadd(mkmul(pool[n].a, mkfun(F_LN, pool[n].a)),
                          mkmul(mknum(num_int(-1)), pool[n].a));
                break;
            case F_TAN:
                a = mkmul(mknum(num_int(-1)),
                          mkfun(F_LN, mkfun(F_ABS, mkfun(F_COS, pool[n].a))));
                break;
            default:
                return NIL;
            }
            return mkmul(a, mkpow(la, mknum(num_int(-1))));
        }
        return NIL;
    }
    return NIL;
}

/* ---------------- Einsetzen ---------------- */

static int subst(int n, char v, int val)
{
    if (n == NIL) return NIL;
    if (pool[n].op == OP_VAR) {
        if (pool[n].fn == (unsigned char)v) return val;
        return n;
    }
    if (pool[n].op == OP_NUM || pool[n].op == OP_CONST) return n;
    if (pool[n].op == OP_FUN)
        return mkfun(pool[n].fn, subst(pool[n].a, v, val));
    if (pool[n].op == OP_POW)
        return mkpow(subst(pool[n].a, v, val), subst(pool[n].b, v, val));
    if (pool[n].op == OP_MUL)
        return mkmul(subst(pool[n].a, v, val), subst(pool[n].b, v, val));
    if (pool[n].op == OP_ADD)
        return mkadd(subst(pool[n].a, v, val), subst(pool[n].b, v, val));
    return mkeq(subst(pool[n].a, v, val), subst(pool[n].b, v, val));
}

/* ---------------- Zahlenwert ---------------- */

static int evaln(int n, char v, double val, double *out)
{
    double a, b, k = cas_deg ? PI / 180.0 : 1.0;
    if (n == NIL) return 0;
    switch (pool[n].op) {
    case OP_NUM: *out = num_d(numof(n)); return 1;
    case OP_CONST: *out = (pool[n].fn == K_PI) ? PI : 2.71828182845904523536; return 1;
    case OP_VAR:
        if (pool[n].fn == (unsigned char)v) { *out = val; return 1; }
        return 0;
    case OP_ADD:
        if (!evaln(pool[n].a, v, val, &a) || !evaln(pool[n].b, v, val, &b)) return 0;
        *out = a + b;
        return 1;
    case OP_MUL:
        if (!evaln(pool[n].a, v, val, &a) || !evaln(pool[n].b, v, val, &b)) return 0;
        *out = a * b;
        return 1;
    case OP_POW:
        if (!evaln(pool[n].a, v, val, &a) || !evaln(pool[n].b, v, val, &b)) return 0;
        if (a == 0.0 && b < 0.0) { seterr("Division by 0."); return 0; }
        if (a < 0.0 && b != floor(b)) { seterr("Negative number to a fractional power."); return 0; }
        *out = pow(a, b);
        return 1;
    case OP_FUN:
        if (!evaln(pool[n].a, v, val, &a)) return 0;
        switch (pool[n].fn) {
        case F_SIN: *out = sin(a * k); return 1;
        case F_COS: *out = cos(a * k); return 1;
        case F_TAN: *out = tan(a * k); return 1;
        case F_ASIN:
            if (a < -1.0 || a > 1.0) { seterr("asin only from -1 to 1."); return 0; }
            *out = asin(a) / k; return 1;
        case F_ACOS:
            if (a < -1.0 || a > 1.0) { seterr("acos only from -1 to 1."); return 0; }
            *out = acos(a) / k; return 1;
        case F_ATAN: *out = atan(a) / k; return 1;
        case F_LN:
            if (a <= 0.0) { seterr("ln needs a positive argument."); return 0; }
            *out = log(a); return 1;
        case F_LOG:
            if (a <= 0.0) { seterr("log needs a positive argument."); return 0; }
            *out = log10(a); return 1;
        case F_EXP:
            if (a > 690.0) { seterr("Result too large."); return 0; }
            *out = exp(a); return 1;
        default:
            *out = fabs(a); return 1;
        }
    }
    return 0;
}

/* ---------------- Faktorisieren ---------------- */

static int sqrt_expr(int n)
{
    long k;
    Num v;
    if (isnum(n)) {
        if (num_sqrt(numof(n), &v)) return mknum(v);
        return NIL;
    }
    if (pool[n].op == OP_POW) {
        if (isnum(pool[n].b) && num_isint(numof(pool[n].b), &k) && k > 0 && (k % 2) == 0)
            return mkpow(pool[n].a, mknum(num_int(k / 2)));
        return NIL;
    }
    if (pool[n].op == OP_MUL) {
        int a = sqrt_expr(pool[n].a);
        int b = sqrt_expr(pool[n].b);
        if (a == NIL || b == NIL) return NIL;
        return mkmul(a, b);
    }
    return NIL;
}

static int try_squares(int n)
{
    int p, q, sp, sq;
    if (pool[n].op != OP_ADD) return NIL;
    p = pool[n].a; q = pool[n].b;
    if (signof(p) < 0 && signof(q) > 0) { int t = p; p = q; q = t; }
    if (signof(q) >= 0) return NIL;
    q = simp(mkmul(mknum(num_int(-1)), q));
    if (q == NIL) return NIL;
    sp = sqrt_expr(p);
    sq = sqrt_expr(q);
    if (sp == NIL || sq == NIL) return NIL;
    return mkmul(mkadd(sp, mkmul(mknum(num_int(-1)), sq)), mkadd(sp, sq));
}

static int divisors(long n, long *out, int max)
{
    long i;
    int k = 0;
    if (n < 0) n = -n;
    if (n == 0) { out[0] = 0; return 1; }
    for (i = 1; i * i <= n && k < max; i++)
        if (n % i == 0) {
            out[k++] = i;
            if (k < max && i != n / i) out[k++] = n / i;
        }
    return k;
}

static int to_int_coeffs(Num *c, int deg, long *ic)
{
    long L = 1, g, d, n;
    int i;
    for (i = 0; i <= deg; i++) {
        d = c[i].real ? 1 : c[i].d;
        g = lgcd(L, d);
        if (!lmulck(L / g, d, &L)) return 0;
        if (L > 1000000L) return 0;
    }
    for (i = 0; i <= deg; i++) {
        double val;
        if (c[i].real) val = c[i].r * (double)L;
        else val = (double)c[i].n * (double)(L / c[i].d);
        if (val > 1000000.0 || val < -1000000.0) return 0;
        if (c[i].real) n = (long)(val + (val >= 0 ? 0.5 : -0.5));
        else n = c[i].n * (L / c[i].d);
        ic[i] = n;
    }
    return 1;
}

static Num poly_eval(Num *c, int deg, Num x)
{
    Num r = num_int(0);
    int i;
    for (i = deg; i >= 0; i--) r = num_add(num_mul(r, x), c[i]);
    return r;
}

static int find_rat_root(Num *c, int deg, Num *root)
{
    long ic[MAXDEG + 1], ps[32], qs[32];
    int np, nq, i, j;
    Num cand;
    if (deg < 1) return 0;
    if (!to_int_coeffs(c, deg, ic)) return 0;
    if (ic[0] == 0) { *root = num_int(0); return 1; }
    np = divisors(ic[0], ps, 32);
    nq = divisors(ic[deg], qs, 32);
    for (i = 0; i < np; i++)
        for (j = 0; j < nq; j++) {
            if (qs[j] == 0) continue;
            cand = num_rat(ps[i], qs[j]);
            if (num_zero(poly_eval(c, deg, cand))) { *root = cand; return 1; }
            cand = num_negv(cand);
            if (num_zero(poly_eval(c, deg, cand))) { *root = cand; return 1; }
        }
    return 0;
}

static void deflate(Num *c, int deg, Num r)
{
    Num b[MAXDEG + 1];
    int k;
    b[deg - 1] = c[deg];
    for (k = deg - 1; k >= 1; k--)
        b[k - 1] = num_add(c[k], num_mul(r, b[k]));
    for (k = 0; k < deg; k++) c[k] = b[k];
}

/* Differenz zweier Quadrate, auch verschachtelt: x^4-1 -> (x^2-1)(x^2+1)
 * -> (x-1)(x+1)(x^2+1) */
static int factor_squares(int n)
{
    int f, l, r;
    if (n == NIL) return NIL;
    f = try_squares(n);
    if (f == NIL) return n;
    l = factor_squares(pool[f].a);
    r = factor_squares(pool[f].b);
    if (l == NIL || r == NIL) return f;
    return simp(mkmul(l, r));
}

static int factor_expr(int e)
{
    int s, f, deg, i, nvar, k, r, fac[16], nf = 0;
    char vc = 0, v;
    Num c[MAXDEG + 1], content, root;
    long ic[MAXDEG + 1], g = 0, L;

    s = simp(expand(e));
    if (s == NIL || cas_err[0]) return NIL;

    f = factor_squares(s);
    if (f != s) {
        int r2 = simp(f);
        if (r2 != NIL) return r2;
    }

    nvar = 0;
    for (v = 'a'; v <= 'z'; v++)
        if (hasvar(s, v)) { if (!nvar) vc = v; nvar++; }
    if (nvar != 1) return s;
    v = vc;

    if (!getpoly(s, v, c, &deg)) return s;
    poly_trim(c, &deg);
    if (deg < 1) return s;
    if (!to_int_coeffs(c, deg, ic)) return s;

    for (i = 0; i <= deg; i++) if (ic[i] != 0) g = lgcd(g, ic[i]);
    if (g < 1) g = 1;
    L = 1;
    for (i = 0; i <= deg; i++) {
        long d = c[i].real ? 1 : c[i].d;
        long gg = lgcd(L, d);
        if (!lmulck(L / gg, d, &L)) return s;
    }
    content = num_rat(g, L);
    for (i = 0; i <= deg; i++) c[i] = num_rat(ic[i] / g, 1);

    k = 0;
    while (k < deg && num_zero(c[0])) {
        for (i = 0; i < deg; i++) c[i] = c[i + 1];
        deg--;
        k++;
    }
    if (k > 0)
        fac[nf++] = mkpow(mkvar(v), mknum(num_int(k)));
    if (!num_one(content)) fac[nf++] = mknum(content);

    while (deg > 2) {
        if (!find_rat_root(c, deg, &root)) break;
        fac[nf++] = mkadd(mkvar(v), mkmul(mknum(num_negv(root)), mknum(num_int(1))));
        deflate(c, deg, root);
        deg--;
        poly_trim(c, &deg);
    }
    if (deg == 2) {
        Num D = num_sub(num_mul(c[1], c[1]), num_mul(num_int(4), num_mul(c[2], c[0])));
        Num sq;
        if (num_sqrt(D, &sq)) {
            Num r1 = num_div(num_sub(num_negv(c[1]), sq), num_mul(num_int(2), c[2]));
            Num r2 = num_div(num_add(num_negv(c[1]), sq), num_mul(num_int(2), c[2]));
            fac[nf++] = mknum(c[2]);
            fac[nf++] = mkadd(mkvar(v), mkmul(mknum(num_negv(r1)), mknum(num_int(1))));
            fac[nf++] = mkadd(mkvar(v), mkmul(mknum(num_negv(r2)), mknum(num_int(1))));
            deg = -1;
        }
    }
    if (deg >= 1) fac[nf++] = poly_node(c, deg, v);
    else if (deg == 0 && !num_one(c[0])) fac[nf++] = mknum(c[0]);

    r = mknum(num_int(1));
    for (i = 0; i < nf; i++) r = mkmul(r, fac[i]);
    r = simp(r);
    if (r == NIL) return NIL;
    if (eq(r, s)) return s;
    return r;
}

/* ---------------- Loesen ---------------- */

static int solve_cmd(int node, char v, char *out, int outsz)
{
    int e, roots[MAXDEG + 2], nr = 0, i, j, deg;
    Num c[MAXDEG + 1], D, root;
    char line[1024], tmp[120];

    if (pool[node].op == OP_EQ)
        e = mkadd(pool[node].a, mkmul(mknum(num_int(-1)), pool[node].b));
    else
        e = node;
    e = simp(expand(e));
    if (e == NIL || cas_err[0]) { strncpy(out, cas_err, outsz - 1); out[outsz - 1] = 0; return -1; }
    if (!getpoly(e, v, c, &deg)) {
        strncpy(out, "Coefficients must be numeric.", outsz - 1);
        out[outsz - 1] = 0;
        return -1;
    }
    poly_trim(c, &deg);
    if (deg == 0) {
        if (num_zero(c[0])) sprintf(out, "true for all %c", v);
        else sprintf(out, "no solution");
        return 1;
    }
    if (deg == 1) {
        roots[nr++] = mknum(num_div(num_negv(c[0]), c[1]));
    } else if (deg == 2) {
        D = num_sub(num_mul(c[1], c[1]), num_mul(num_int(4), num_mul(c[2], c[0])));
        if (num_neg(D)) { strcpy(out, "no real solution"); return 1; }
        roots[nr++] = sqrt_num_node(D);
        if (roots[0] == NIL) { strcpy(out, "Cannot solve that exactly."); return -1; }
        {
            int s1 = mkadd(mknum(num_negv(c[1])), roots[0]);
            int s2 = mkadd(mknum(num_negv(c[1])), mkmul(mknum(num_int(-1)), roots[0]));
            int den = mknum(num_mul(num_int(2), c[2]));
            roots[0] = simp(mkmul(s1, mkpow(den, mknum(num_int(-1)))));
            if (!num_zero(D)) roots[nr++] = simp(mkmul(s2, mkpow(den, mknum(num_int(-1)))));
        }
    } else {
        while (deg > 2) {
            if (!find_rat_root(c, deg, &root)) break;
            roots[nr++] = mknum(root);
            deflate(c, deg, root);
            deg--;
            poly_trim(c, &deg);
        }
        if (deg == 1) roots[nr++] = mknum(num_div(num_negv(c[0]), c[1]));
        else if (deg == 2) {
            D = num_sub(num_mul(c[1], c[1]), num_mul(num_int(4), num_mul(c[2], c[0])));
            if (!num_neg(D)) {
                roots[nr++] = sqrt_num_node(D);
                if (roots[nr - 1] == NIL) { strcpy(out, "Cannot solve that exactly."); return -1; }
                {
                    int s1 = mkadd(mknum(num_negv(c[1])), roots[nr - 1]);
                    int s2 = mkadd(mknum(num_negv(c[1])), mkmul(mknum(num_int(-1)), roots[nr - 1]));
                    int den = mknum(num_mul(num_int(2), c[2]));
                    roots[nr - 1] = simp(mkmul(s1, mkpow(den, mknum(num_int(-1)))));
                    if (!num_zero(D)) roots[nr++] = simp(mkmul(s2, mkpow(den, mknum(num_int(-1)))));
                }
            }
        } else if (deg > 2) {
            strcpy(out, "Cannot solve that exactly.");
            return -1;
        }
    }
    if (nr == 0) { strcpy(out, "no real solution"); return 1; }
    {
        char saved[80];
        memcpy(saved, cas_err, sizeof saved);
        for (i = 1; i < nr; i++) {
            int x = roots[i], okx;
            double xv = 0.0, yv;
            okx = evaln(x, 0, 0.0, &xv);
            for (j = i; j > 0; j--) {
                int oky = evaln(roots[j - 1], 0, 0.0, &yv);
                if (!okx || !oky || yv <= xv) break;
                roots[j] = roots[j - 1];
            }
            roots[j] = x;
        }
        memcpy(cas_err, saved, sizeof cas_err);
    }
    line[0] = 0;
    for (i = 0; i < nr; i++) {
        render_to(roots[i], tmp, sizeof tmp);
        if (i) strcat(line, " or ");
        if (strlen(line) + strlen(tmp) + 8 < sizeof line)
            sprintf(line + strlen(line), "%c = %s", v, tmp);
    }
    strncpy(out, line, outsz - 1);
    out[outsz - 1] = 0;
    return 1;
}

/* ---------------- Parser ---------------- */

typedef struct { const char *s; } P;

static void pskip(P *p)
{
    while (*p->s == ' ' || *p->s == '\t') p->s++;
}

static int cimatch(const char *s, const char *w)
{
    while (*w) {
        if (tolower((unsigned char)*s) != *w) return 0;
        s++; w++;
    }
    return 1;
}

static int ans_node(void)
{
    long v;
    if (fabs(cas_ans) < 2000000000.0 && fabs(cas_ans - floor(cas_ans)) < 1e-9) {
        v = (long)(cas_ans >= 0 ? cas_ans + 0.5 : cas_ans - 0.5);
        return mknum(num_int(v));
    }
    return mknum(num_real(cas_ans));
}

static int parse_rel(P *p);

static int parse_num(P *p)
{
    long ip = 0, fp = 0, sc = 1;
    int any = 0;
    while (isdigit((unsigned char)*p->s)) {
        if (ip > 200000000L) { seterr("Number too large."); return NIL; }
        ip = ip * 10 + (*p->s - '0');
        p->s++; any = 1;
    }
    if (*p->s == '.') {
        p->s++;
        while (isdigit((unsigned char)*p->s)) {
            if (sc > 100000000L) { seterr("Number too large."); return NIL; }
            fp = fp * 10 + (*p->s - '0');
            sc *= 10;
            p->s++; any = 1;
        }
    }
    if (!any) { seterr("Number expected."); return NIL; }
    return mknum(num_add(num_int(ip), num_rat(fp, sc)));
}

static int parse_prim(P *p)
{
    int i, a, c;
    pskip(p);
    if (*p->s == '(') {
        p->s++;
        a = parse_rel(p);
        if (a == NIL) return NIL;
        pskip(p);
        if (*p->s != ')') { seterr("')' missing."); return NIL; }
        p->s++;
        return a;
    }
    if (isdigit((unsigned char)*p->s) || *p->s == '.') return parse_num(p);
    if (isalpha((unsigned char)*p->s)) {
        for (i = 0; i < NFUN; i++) {
            int len = (int)strlen(fnames[i]);
            if (cimatch(p->s, fnames[i]) && !isalnum((unsigned char)p->s[len])) {
                const char *save = p->s;
                p->s += len;
                pskip(p);
                if (*p->s == '(') {
                    p->s++;
                    a = parse_rel(p);
                    if (a == NIL) return NIL;
                    pskip(p);
                    if (*p->s != ')') { seterr("')' missing."); return NIL; }
                    p->s++;
                    return mkfun(i + 1, a);
                }
                p->s = save;
            }
        }
        if (cimatch(p->s, "sqrt") && !isalnum((unsigned char)p->s[4])) {
            p->s += 4;
            pskip(p);
            if (*p->s != '(') { seterr("'(' missing after sqrt."); return NIL; }
            p->s++;
            a = parse_rel(p);
            if (a == NIL) return NIL;
            pskip(p);
            if (*p->s != ')') { seterr("')' missing."); return NIL; }
            p->s++;
            return mkpow(a, mknum(num_rat(1, 2)));
        }
        if (cimatch(p->s, "pi") && !isalnum((unsigned char)p->s[2])) {
            p->s += 2;
            return mkconst(K_PI);
        }
        if (cimatch(p->s, "ans") && !isalnum((unsigned char)p->s[3])) {
            p->s += 3;
            return ans_node();
        }
        if (tolower((unsigned char)*p->s) == 'e') {
            p->s++;
            return mkconst(K_E);
        }
        c = tolower((unsigned char)*p->s++);
        return mkvar((char)c);
    }
    seterr("Formula incomplete or invalid character.");
    return NIL;
}

static int parse_pow(P *p);

static int parse_unary(P *p)
{
    int a;
    pskip(p);
    if (*p->s == '+') { p->s++; return parse_unary(p); }
    if (*p->s == '-') {
        p->s++;
        a = parse_unary(p);
        if (a == NIL) return NIL;
        return mkmul(mknum(num_int(-1)), a);
    }
    return parse_pow(p);
}

static int parse_pow(P *p)
{
    int a, b;
    a = parse_prim(p);
    if (a == NIL) return NIL;
    pskip(p);
    if (*p->s == '^') {
        p->s++;
        b = parse_unary(p);
        if (b == NIL) return NIL;
        return mkpow(a, b);
    }
    return a;
}

static int parse_mul(P *p)
{
    int a, b;
    char c;
    a = parse_unary(p);
    if (a == NIL) return NIL;
    for (;;) {
        pskip(p);
        c = *p->s;
        if (c == '*') {
            p->s++;
            b = parse_unary(p);
            if (b == NIL) return NIL;
            a = mkmul(a, b);
        } else if (c == '/' || c == ':') {
            p->s++;
            b = parse_unary(p);
            if (b == NIL) return NIL;
            a = mkmul(a, mkpow(b, mknum(num_int(-1))));
        } else if (isdigit((unsigned char)c) || c == '.' || c == '(' ||
                   isalpha((unsigned char)c)) {
            b = parse_unary(p);
            if (b == NIL) return NIL;
            a = mkmul(a, b);
        } else {
            break;
        }
        if (a == NIL) return NIL;
    }
    return a;
}

static int parse_sum(P *p)
{
    int a, b;
    a = parse_mul(p);
    if (a == NIL) return NIL;
    for (;;) {
        pskip(p);
        if (*p->s == '+') {
            p->s++;
            b = parse_mul(p);
            if (b == NIL) return NIL;
            a = mkadd(a, b);
        } else if (*p->s == '-') {
            p->s++;
            b = parse_mul(p);
            if (b == NIL) return NIL;
            a = mkadd(a, mkmul(mknum(num_int(-1)), b));
        } else {
            break;
        }
    }
    return a;
}

static int parse_rel(P *p)
{
    int a, b;
    a = parse_sum(p);
    if (a == NIL) return NIL;
    pskip(p);
    if (*p->s == '=') {
        p->s++;
        b = parse_sum(p);
        if (b == NIL) return NIL;
        return mkeq(a, b);
    }
    return a;
}

static int parse_full(const char *s, int allow_eq)
{
    P p;
    int n;
    p.s = s;
    n = parse_rel(&p);
    if (n == NIL) return NIL;
    pskip(&p);
    if (*p.s) {
        if (!cas_err[0]) seterr("Unexpected character.");
        return NIL;
    }
    if (!allow_eq && pool[n].op == OP_EQ) {
        seterr("Use = only in solve().");
        return NIL;
    }
    return n;
}

/* ---------------- Befehle ---------------- */

static void setout(char *out, int outsz, const char *s)
{
    strncpy(out, s, outsz - 1);
    out[outsz - 1] = 0;
}

static void trim(char *s)
{
    char *p = s, *q;
    while (*p == ' ' || *p == '\t') p++;
    if (p != s) memmove(s, p, strlen(p) + 1);
    q = s + strlen(s);
    while (q > s && (q[-1] == ' ' || q[-1] == '\t')) *--q = 0;
}

static int split_args(const char *s, char out[][200], int max)
{
    int n = 0, d = 0;
    char *o;
    if (!*s) return 0;
    o = out[0];
    *o = 0;
    while (*s) {
        if (*s == '(') d++;
        if (*s == ')') d--;
        if ((*s == ',' || *s == ';') && d == 0) {
            *o = 0;
            trim(out[n]);
            n++;
            if (n >= max) return -1;
            o = out[n];
            *o = 0;
            s++;
            continue;
        }
        if ((int)(o - out[n]) < 199) { *o++ = *s; *o = 0; }
        s++;
    }
    trim(out[n]);
    return n + 1;
}

static int cas_ok(int node) { return node != NIL && cas_err[0] == 0; }

static int argvar(char args[][200], int idx, int nargs, char *v)
{
    if (nargs <= idx) { *v = 'x'; return 1; }
    trim(args[idx]);
    if (args[idx][0] == 0 || args[idx][1] != 0 ||
        !isalpha((unsigned char)args[idx][0])) return 0;
    *v = (char)tolower((unsigned char)args[idx][0]);
    return 1;
}

int cas_eval(const char *in, char *out, int outsz, double ansv)
{
    char work[220], name[24], args[4][200], inside[200];
    const char *lp, *rp, *q;
    int cmd = 0, nargs, i, n, node, node2;
    char var, tmp[220];
    double dv;

    cas_ans = ansv;
    cas_err[0] = 0;
    if (!in || outsz < 2) return 0;
    n = (int)strlen(in);
    if (n == 0 || n > 200) return 0;
    strcpy(work, in);
    trim(work);
    lp = strchr(work, '(');
    if (!lp) return 0;
    n = (int)(lp - work);
    if (n > 20) return 0;
    memcpy(name, work, n);
    name[n] = 0;
    trim(name);
    for (i = 0; name[i]; i++) name[i] = (char)tolower((unsigned char)name[i]);
    if (!strcmp(name, "simplify") || !strcmp(name, "simpl")) cmd = 1;
    else if (!strcmp(name, "expand")) cmd = 2;
    else if (!strcmp(name, "factor")) cmd = 3;
    else if (!strcmp(name, "diff") || !strcmp(name, "deriv")) cmd = 4;
    else if (!strcmp(name, "integrate") || !strcmp(name, "integ")) cmd = 5;
    else if (!strcmp(name, "solve")) cmd = 6;
    else if (!strcmp(name, "subst") || !strcmp(name, "substitute")) cmd = 7;
    else if (!strcmp(name, "approx") || !strcmp(name, "evalf")) cmd = 8;
    if (!cmd) return 0;

    rp = work + strlen(work) - 1;
    while (rp > lp && (*rp == ' ' || *rp == '\t')) rp--;
    if (*rp != ')') { setout(out, outsz, "')' missing."); return -1; }
    for (q = rp + 1; *q; q++)
        if (*q != ' ' && *q != '\t') { setout(out, outsz, "Text after ')'."); return -1; }
    n = (int)(rp - lp - 1);
    if (n < 0 || n > 199) { setout(out, outsz, "Expression too long."); return -1; }
    memcpy(inside, lp + 1, n);
    inside[n] = 0;
    nargs = split_args(inside, args, 4);
    if (nargs < 0) { setout(out, outsz, "Too many arguments."); return -1; }

    ntop = 0;
    cas_err[0] = 0;

    switch (cmd) {
    case 1:
        if (nargs != 1) { setout(out, outsz, "simplify needs one expression."); return -1; }
        node = parse_full(args[0], 0);
        if (!cas_ok(node)) break;
        node = simp(node);
        if (!cas_ok(node)) break;
        render_to(node, out, outsz);
        return 1;
    case 2:
        if (nargs != 1) { setout(out, outsz, "expand needs one expression."); return -1; }
        node = parse_full(args[0], 0);
        if (!cas_ok(node)) break;
        node = simp(expand(node));
        if (!cas_ok(node)) break;
        render_to(node, out, outsz);
        return 1;
    case 3:
        if (nargs != 1) { setout(out, outsz, "factor needs one expression."); return -1; }
        node = parse_full(args[0], 0);
        if (!cas_ok(node)) break;
        node = factor_expr(node);
        if (!cas_ok(node)) break;
        render_to(node, out, outsz);
        return 1;
    case 4:
        if (nargs < 1 || nargs > 2) { setout(out, outsz, "diff needs expr[,x]."); return -1; }
        if (!argvar(args, 1, nargs, &var)) { setout(out, outsz, "Variable must be one letter."); return -1; }
        node = parse_full(args[0], 0);
        if (!cas_ok(node)) break;
        node = simp(deriv(node, var));
        if (!cas_ok(node)) break;
        render_to(node, out, outsz);
        return 1;
    case 5:
        if (nargs < 1 || nargs == 3 || nargs > 4) {
            setout(out, outsz, "integrate needs expr[,x[,a,b]].");
            return -1;
        }
        if (!argvar(args, 1, nargs, &var)) { setout(out, outsz, "Variable must be one letter."); return -1; }
        node = parse_full(args[0], 0);
        if (!cas_ok(node)) break;
        node = integ(node, var);
        if (node == NIL) {
            setout(out, outsz, "Cannot integrate that.");
            return -1;
        }
        node = simp(node);
        if (!cas_ok(node)) break;
        if (nargs == 4) {
            int lo = parse_full(args[2], 0);
            int hi = parse_full(args[3], 0);
            if (lo == NIL || hi == NIL) break;
            node2 = simp(mkadd(subst(node, var, hi),
                               mkmul(mknum(num_int(-1)), subst(node, var, lo))));
            if (!cas_ok(node2)) break;
            node = node2;
        }
        render_to(node, out, outsz);
        return 1;
    case 6:
        if (nargs < 1 || nargs > 2) { setout(out, outsz, "solve needs equation[,x]."); return -1; }
        if (!argvar(args, 1, nargs, &var)) { setout(out, outsz, "Variable must be one letter."); return -1; }
        node = parse_full(args[0], 1);
        if (!cas_ok(node)) break;
        return solve_cmd(node, var, out, outsz);
    case 7:
        if (nargs != 3) { setout(out, outsz, "subst needs expr,x,wert."); return -1; }
        if (!argvar(args, 1, nargs, &var)) { setout(out, outsz, "Variable must be one letter."); return -1; }
        node = parse_full(args[0], 0);
        if (!cas_ok(node)) break;
        node2 = parse_full(args[2], 0);
        if (!cas_ok(node2)) break;
        node = simp(subst(node, var, node2));
        if (!cas_ok(node)) break;
        render_to(node, out, outsz);
        return 1;
    case 8:
        if (nargs != 1) { setout(out, outsz, "approx needs one expression."); return -1; }
        node = parse_full(args[0], 0);
        if (!cas_ok(node)) break;
        node = simp(node);
        if (!cas_ok(node)) break;
        if (!evaln(node, 0, 0.0, &dv)) {
            setout(out, outsz, cas_err[0] ? cas_err : "Contains variables.");
            return -1;
        }
        if (fabs(dv) < 1e-12) dv = 0.0;
        sprintf(tmp, "%.10g", dv);
        setout(out, outsz, tmp);
        return 1;
    }

    if (cas_err[0]) setout(out, outsz, cas_err);
    else setout(out, outsz, "Syntax error.");
    return -1;
}
