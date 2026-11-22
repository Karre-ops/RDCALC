/* RetroDesk - RDCALC: symbolischer Kern (CAS).
 *
 * Ausdruecke als Baum in einem festen Speicher (Pool), Zahlen exakt als Bruch
 * (long/long) mit Rueckfall auf double bei Ueberlauf.  Kanonische Form:
 * ADD/MUL flach, Faktoren und Summanden sortiert, Koeffizient vorn.
 *
 * Befehle: simplify(expr), expand(expr), factor(expr), diff(expr[,x]),
 *          integrate(expr[,x[,a,b]]), solve(gleichung[,x]),
 *          solve([g1;g2],[x,y]), subst(expr,x,wert), approx(expr),
 *          laplace/ilaplace, eigenvals/eigenvect, rref/rank/lsolve/charpoly
 *
 * Komplex: i ist eine Konstante mit i^2 = -1. Quadratische Gleichungen mit
 * negativer Diskriminante liefern komplexe Loesungen.
 * Matrizen: [1,2;3,4], Rechnen mit +, -, *, ^ sowie det(), inv(), trans().
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
#define MAXM    16              /* Matrizen je Befehl */
#define MAXME   36              /* Elemente je Matrix (6x6) */
#define MAXDIM  6               /* Zeilen/Spalten je Matrix */
#define PI      3.14159265358979323846

enum { OP_NUM, OP_VAR, OP_CONST, OP_FUN, OP_POW, OP_MUL, OP_ADD, OP_EQ, OP_MAT };
enum { K_PI = 1, K_E, K_I };
enum { F_SIN = 1, F_COS, F_TAN, F_ASIN, F_ACOS, F_ATAN, F_LN, F_LOG, F_EXP, F_ABS,
       F_DET, F_INV, F_TRANS, F_DELTA, F_DDELTA, F_DDELTA2, F_STEP };
#define NFUN 17

static const char *fnames[NFUN] = {
    "sin", "cos", "tan", "asin", "acos", "atan", "ln", "log", "exp", "abs",
    "det", "inv", "trans", "delta", "delta'", "delta''", "step"
};

typedef struct {
    unsigned char op;
    unsigned char fn;           /* Funktion, Konstante oder Variablenbuchstabe */
    short a, b;                 /* Kinder, NIL = keins */
    long n, d;                  /* exakter Bruch, d > 0; d == 0: double in r */
    double r;
} Node;

typedef struct { int real; long n, d; double r; } Num;

typedef struct { int rows, cols; int e[MAXME]; } Mat;   /* Elemente als Knoten */

#ifdef CAS_FARPOOL                      /* RD2: Knoten in einem eigenen Block (MemFixed), DGROUP bleibt klein */
static Node __far *pool;
void cas_setpool(void __far *p) { pool = (Node __far *)p; }
unsigned cas_poolsize(void) { return (unsigned)(MAXN * sizeof(Node)); }
#else
static Node pool[MAXN];
#endif
static int ntop;
static Mat mats[MAXM];
static int nmats;
static char cas_err[80];
static int cas_deg = 1;
static double cas_ans;

static int cas_ok(int node);
static void setout(char *out, int outsz, const char *s);

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

/* Ueberlaufpruefung rein ganzzahlig: ohne 8087 kostet jede Gleitkommaoperation einen Emulatoraufruf */
#define LLIM 2147483000UL
static unsigned long luabs(long a) { return a < 0 ? 0UL - (unsigned long)a : (unsigned long)a; }

static int lmulck(long a, long b, long *r)
{
    unsigned long ua = luabs(a), ub = luabs(b);
    if ((ua | ub) <= 0xFFFFUL) { if (ua * ub > LLIM) return 0; }   /* Normalfall: beide 16 Bit */
    else if (ua && ub > LLIM / ua) return 0;
    *r = a * b;
    return 1;
}

static int laddck(long a, long b, long *r)
{
    long s;
    if ((a < 0) != (b < 0)) { *r = a + b; return 1; }              /* verschiedene Vorzeichen: kein Ueberlauf */
    if (luabs(a) + luabs(b) > LLIM) return 0;
    s = a + b; *r = s;
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
        long p, q;
        if (a.d == b.d) return a.n < b.n ? -1 : a.n > b.n ? 1 : 0;
        if (lmulck(a.n, b.d, &p) && lmulck(b.n, a.d, &q)) return p < q ? -1 : p > q ? 1 : 0;
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
    if (rn * rn != a.n) return 0;               /* isqrtl(n) <= 46340: Quadrat passt in long */
    if (rd * rd != a.d) return 0;
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

/* Matrix aus Zeilen x Spalten mit Elementknoten */
static int mkmat(int rows, int cols, int *el)
{
    int i, m, n;
    if (rows < 1 || cols < 1 || rows > MAXDIM || cols > MAXDIM ||
        rows * cols > MAXME) { seterr("Matrix too large."); return NIL; }
    if (nmats >= MAXM) { seterr("Too many matrices."); return NIL; }
    m = nmats++;
    mats[m].rows = rows;
    mats[m].cols = cols;
    n = rows * cols;
    for (i = 0; i < n; i++) mats[m].e[i] = el[i];
    i = newnode(OP_MAT);
    if (i == NIL) return NIL;
    pool[i].fn = (unsigned char)m;
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

static int mat_add(int x, int y);
static int mat_mul(int x, int y);
static int mat_scal(int m, int s, int left);
static int mat_pow(int m, long k);
static int mat_det(int m);
static int mat_inv(int m);
static int mat_trans(int m);
static int mkpow(int a, int b);
static int simp(int n);
static int expand(int n);

/* det/inv/trans liefern erst nach dem Vereinfachen eine Matrix */
static int matfun(int n)
{
    return pool[n].op == OP_FUN &&
           (pool[n].fn == F_DET || pool[n].fn == F_INV || pool[n].fn == F_TRANS);
}

static int mkadd(int x, int y)
{
    int i;
    if (x == NIL || y == NIL) return NIL;
    if (pool[x].op == OP_MAT || pool[y].op == OP_MAT) {
        if (!matfun(x) && !matfun(y)) {         /* det/inv/trans: erst vereinfachen */
            if (pool[x].op != OP_MAT || pool[y].op != OP_MAT) {
                seterr("Matrix and number do not add.");
                return NIL;
            }
            return mat_add(x, y);
        }
    }
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
    if (pool[x].op == OP_MAT && pool[y].op == OP_MAT) return mat_mul(x, y);
    if (pool[x].op == OP_MAT && !matfun(y)) return mat_scal(x, y, 0);
    if (pool[y].op == OP_MAT && !matfun(x)) return mat_scal(y, x, 1);
    if (iszero(x) || iszero(y)) return mknum(num_int(0));
    if (isone(x)) return y;
    if (isone(y)) return x;
    i = newnode(OP_MUL);
    if (i == NIL) return NIL;
    pool[i].a = (short)x; pool[i].b = (short)y;
    return i;
}

/* ---------------- Matrizen ---------------- */

static void flatrec(int n, int op, int *out, int *cnt, int max);

static int mat_add(int x, int y)
{
    Mat *A = &mats[pool[x].fn], *B = &mats[pool[y].fn];
    int el[MAXME], i, n;
    if (A->rows != B->rows || A->cols != B->cols) {
        seterr("Matrix sizes differ.");
        return NIL;
    }
    n = A->rows * A->cols;
    for (i = 0; i < n; i++) {
        el[i] = mkadd(A->e[i], B->e[i]);
        if (el[i] == NIL) return NIL;
    }
    return mkmat(A->rows, A->cols, el);
}

static int mat_scal(int m, int s, int left)
{
    Mat *A = &mats[pool[m].fn];
    int el[MAXME], i, n = A->rows * A->cols;
    for (i = 0; i < n; i++) {
        el[i] = left ? mkmul(s, A->e[i]) : mkmul(A->e[i], s);
        if (el[i] == NIL) return NIL;
    }
    return mkmat(A->rows, A->cols, el);
}

static int mat_mul(int x, int y)
{
    Mat *A = &mats[pool[x].fn], *B = &mats[pool[y].fn];
    int el[MAXME], i, j, k, t, s;
    if (A->cols != B->rows) {
        seterr("Matrix sizes do not match for *.");
        return NIL;
    }
    if (A->rows * B->cols > MAXME) { seterr("Matrix too large."); return NIL; }
    for (i = 0; i < A->rows; i++)
        for (j = 0; j < B->cols; j++) {
            s = mknum(num_int(0));
            for (k = 0; k < A->cols; k++) {
                t = mkmul(A->e[i * A->cols + k], B->e[k * B->cols + j]);
                if (t == NIL) return NIL;
                s = mkadd(s, t);
                if (s == NIL) return NIL;
            }
            el[i * B->cols + j] = s;
        }
    return mkmat(A->rows, B->cols, el);
}

/* Determinante durch Entwicklung nach der ersten Zeile (kleine Matrizen,
 * bleibt bei Symbolen ohne Brueche) */
static int det_rec(int *a, int n, int ld)
{
    int i, j, c, s, r, sign = 1, sub[MAXME], k;
    if (n == 1) return a[0];
    if (n == 2)
        return simp(mkadd(mkmul(a[0], a[ld + 1]),
                          mkmul(mknum(num_int(-1)), mkmul(a[1], a[ld]))));
    s = mknum(num_int(0));
    for (c = 0; c < n; c++) {
        k = 0;
        for (i = 1; i < n; i++)
            for (j = 0; j < n; j++)
                if (j != c) sub[k++] = a[i * ld + j];
        r = det_rec(sub, n - 1, n - 1);
        if (r == NIL) return NIL;
        r = simp(mkmul(a[c], r));
        if (r == NIL) return NIL;
        s = simp(mkadd(s, sign > 0 ? r : mkmul(mknum(num_int(-1)), r)));
        if (s == NIL) return NIL;
        sign = -sign;
    }
    return s;
}

/* Determinante mit Gauss-Elimination, exakt und auch mit i oder Symbolen */
static int mat_det(int m)
{
    Mat *A = &mats[pool[m].fn];
    int a[MAXME], t, f, det;
    int n = A->rows, i, j, k, piv, neg = 0;
    if (A->rows != A->cols) { seterr("det needs a square matrix."); return NIL; }
    if (n <= 4) {
        for (i = 0; i < n * n; i++) {
            a[i] = simp(A->e[i]);
            if (a[i] == NIL) return NIL;
        }
        return simp(expand(det_rec(a, n, n)));
    }
    for (i = 0; i < n * n; i++) a[i] = A->e[i];
    det = mknum(num_int(1));
    for (k = 0; k < n; k++) {
        piv = -1;
        for (i = k; i < n; i++) if (!iszero(a[i * n + k])) { piv = i; break; }
        if (piv < 0) return mknum(num_int(0));
        if (piv != k) {
            for (j = 0; j < n; j++) {
                t = a[k * n + j]; a[k * n + j] = a[piv * n + j]; a[piv * n + j] = t;
            }
            neg = !neg;
        }
        det = simp(mkmul(det, a[k * n + k]));
        if (det == NIL) return NIL;
        for (i = k + 1; i < n; i++) {
            if (iszero(a[i * n + k])) continue;
            f = simp(mkmul(a[i * n + k], mkpow(a[k * n + k], mknum(num_int(-1)))));
            if (f == NIL) return NIL;
            for (j = k; j < n; j++) {
                a[i * n + j] = simp(mkadd(a[i * n + j],
                    mkmul(mknum(num_int(-1)), mkmul(f, a[k * n + j]))));
                if (a[i * n + j] == NIL) return NIL;
            }
        }
    }
    if (neg) det = simp(mkmul(mknum(num_int(-1)), det));
    return simp(expand(det));       /* ausmultiplizieren: x*(x-1/x) -> x^2-1 */
}

/* Inverse mit Gauss-Jordan, exakt und auch mit i oder Symbolen */
static int mat_inv(int m)
{
    Mat *A = &mats[pool[m].fn];
    int a[MAXME], b[MAXME], t, f, d, el[MAXME];
    int n = A->rows, i, j, k, piv;
    if (A->rows != A->cols) { seterr("inv needs a square matrix."); return NIL; }
    for (i = 0; i < n * n; i++) {
        a[i] = A->e[i];
        b[i] = mknum(num_int(i % (n + 1) == 0 ? 1 : 0));
    }
    for (k = 0; k < n; k++) {
        piv = -1;
        for (i = k; i < n; i++) if (!iszero(a[i * n + k])) { piv = i; break; }
        if (piv < 0) { seterr("Matrix is singular."); return NIL; }
        if (piv != k) {
            for (j = 0; j < n; j++) {
                t = a[k * n + j]; a[k * n + j] = a[piv * n + j]; a[piv * n + j] = t;
                t = b[k * n + j]; b[k * n + j] = b[piv * n + j]; b[piv * n + j] = t;
            }
        }
        d = simp(mkpow(a[k * n + k], mknum(num_int(-1))));
        if (d == NIL) return NIL;
        for (j = 0; j < n; j++) {
            a[k * n + j] = simp(mkmul(a[k * n + j], d));
            b[k * n + j] = simp(mkmul(b[k * n + j], d));
            if (a[k * n + j] == NIL || b[k * n + j] == NIL) return NIL;
        }
        for (i = 0; i < n; i++) {
            if (i == k || iszero(a[i * n + k])) continue;
            f = a[i * n + k];
            for (j = 0; j < n; j++) {
                a[i * n + j] = simp(mkadd(a[i * n + j],
                    mkmul(mknum(num_int(-1)), mkmul(f, a[k * n + j]))));
                b[i * n + j] = simp(mkadd(b[i * n + j],
                    mkmul(mknum(num_int(-1)), mkmul(f, b[k * n + j]))));
                if (a[i * n + j] == NIL || b[i * n + j] == NIL) return NIL;
            }
        }
    }
    for (i = 0; i < n * n; i++) el[i] = b[i];
    return mkmat(n, n, el);
}

static int mat_trans(int m)
{
    Mat *A = &mats[pool[m].fn];
    int el[MAXME], r, c, k = 0;
    for (c = 0; c < A->cols; c++)
        for (r = 0; r < A->rows; r++)
            el[k++] = A->e[r * A->cols + c];
    return mkmat(A->cols, A->rows, el);
}

static int mat_pow(int m, long k)
{
    Mat *A = &mats[pool[m].fn];
    int el[MAXME], i, n = A->rows * A->cols, r;
    if (A->rows != A->cols) { seterr("Matrix power needs a square matrix."); return NIL; }
    if (k < 0) {
        r = mat_inv(m);
        if (r == NIL) return NIL;
        return mat_pow(r, -k);
    }
    for (i = 0; i < n; i++) el[i] = mknum(num_int(i % (A->rows + 1) == 0 ? 1 : 0));
    r = mkmat(A->rows, A->cols, el);
    for (i = 0; i < k; i++) {
        r = mat_mul(r, m);
        if (r == NIL) return NIL;
    }
    return r;
}

/* Komplexe Zahl als a + b*i zerlegen (nur exakte Zahlen); 1 = erkannt */
static int cplx_split(int n, Num *re, Num *im)
{
    int f[MAXF], nf = 0, i, got = 0;
    Num c = num_int(1), r1, i1, r2, i2;
    if (isnum(n)) { *re = numof(n); *im = num_int(0); return 1; }
    if (pool[n].op == OP_CONST && pool[n].fn == K_I) {
        *re = num_int(0); *im = num_int(1); return 1;
    }
    if (pool[n].op == OP_MUL) {
        flatrec(n, OP_MUL, f, &nf, MAXF);
        for (i = 0; i < nf; i++) {
            if (isnum(f[i])) { c = num_mul(c, numof(f[i])); continue; }
            if (pool[f[i]].op == OP_CONST && pool[f[i]].fn == K_I && !got) { got = 1; continue; }
            return 0;
        }
        if (!got) return 0;
        *re = num_int(0); *im = c; return 1;
    }
    if (pool[n].op == OP_ADD) {
        if (!cplx_split(pool[n].a, &r1, &i1)) return 0;
        if (!cplx_split(pool[n].b, &r2, &i2)) return 0;
        *re = num_add(r1, r2); *im = num_add(i1, i2); return 1;
    }
    return 0;
}

static int mkpow(int a, int b)
{
    int i;
    Num e;
    long k;
    if (a == NIL || b == NIL) return NIL;
    if (isnum(b) && num_isint(numof(b), &k)) {
        if (pool[a].op == OP_CONST && pool[a].fn == K_I) {
            k = ((k % 4) + 4) % 4;
            if (k == 0) return mknum(num_int(1));
            if (k == 1) return a;
            if (k == 2) return mknum(num_int(-1));
            return mkmul(mknum(num_int(-1)), a);
        }
        if (pool[a].op == OP_MAT) {
            if (k < -8 || k > 8) { seterr("Matrix exponent too large."); return NIL; }
            return mat_pow(a, k);
        }
    }
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
        if (a.r < 0.0) {                       /* sqrt(-x) = sqrt(x)*i */
            int s = sqrt_num_node(num_real(-a.r));
            if (s == NIL) return NIL;
            return mkmul(s, mkconst(K_I));
        }
        return mkpow(mknum(a), mknum(num_rat(1, 2)));
    }
    if (a.n < 0) {                             /* sqrt(-x) = sqrt(x)*i */
        int s = sqrt_num_node(num_negv(a));
        if (s == NIL) return NIL;
        return mkmul(s, mkconst(K_I));
    }
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
    case OP_FUN:
    case OP_MAT: return 3;
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
    case OP_CONST:
    case OP_MAT: return 1;
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
    case OP_MAT: {
        Mat *A = &mats[pool[x].fn], *B = &mats[pool[y].fn];
        int i, c2;
        if (A->rows != B->rows) return A->rows < B->rows ? -1 : 1;
        if (A->cols != B->cols) return A->cols < B->cols ? -1 : 1;
        for (i = 0; i < A->rows * A->cols; i++) {
            c2 = cmp(A->e[i], B->e[i]);
            if (c2) return c2;
        }
        return 0;
    }
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
    case OP_MAT: {
        Mat *A = &mats[pool[x].fn], *B = &mats[pool[y].fn];
        int i;
        if (A->rows != B->rows || A->cols != B->cols) return 0;
        for (i = 0; i < A->rows * A->cols; i++)
            if (!eq(A->e[i], B->e[i])) return 0;
        return 1;
    }
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
    rpos = 0;                       /* Vorzeichen des Summanden ist abgehandelt */

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
        if (!e.real && e.d == 2 && e.n == 1) {          /* Wurzel im Nenner */
            rput("sqrt("); rnd(pool[p].a, 0); rch(')');
            first = 0;
            continue;
        }
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
    rpos = 0;
    if (isnum(b) && num_one(numof(b))) {        /* ^1 nicht schreiben */
        if (pool[a].op == OP_NUM) rnd(a, 0);
        else if (plevel(a) < 2) { rch('('); rnd(a, 0); rch(')'); }
        else rnd(a, 3);
        return;
    }
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
    case OP_CONST:
        rput(pool[n].fn == K_PI ? "pi" : pool[n].fn == K_E ? "e" : "i");
        break;
    case OP_MAT: {
        Mat *A = &mats[pool[n].fn];
        int r, c;
        rpos = 0;
        rch('[');
        for (r = 0; r < A->rows; r++) {
            if (r) rch(';');
            for (c = 0; c < A->cols; c++) {
                if (c) rch(',');
                rnd(A->e[r * A->cols + c], 0);
            }
        }
        rch(']');
        break;
    }
    case OP_FUN:
        rput(fnames[pool[n].fn - 1]);
        rch('(');
        rpos = 0;
        rnd(pool[n].a, 0);
        rch(')');
        break;
    case OP_POW: rnd_pow(n); break;
    case OP_MUL: rnd_mul(n); break;
    case OP_ADD: rnd_add(n); break;
    case OP_EQ:
        rpos = 0;
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
static int dist(int a, int b);

static int has_anyvar(int n)
{
    int i, k;
    if (n == NIL) return 0;
    if (pool[n].op == OP_VAR) return 1;
    if (pool[n].op == OP_MAT) {
        Mat *A = &mats[pool[n].fn];
        for (i = 0, k = A->rows * A->cols; i < k; i++)
            if (has_anyvar(A->e[i])) return 1;
        return 0;
    }
    return has_anyvar(pool[n].a) || has_anyvar(pool[n].b);
}

static int has_mat(int n)
{
    if (n == NIL) return 0;
    if (pool[n].op == OP_MAT || matfun(n)) return 1;
    if (pool[n].op == OP_NUM || pool[n].op == OP_VAR || pool[n].op == OP_CONST) return 0;
    if (pool[n].op == OP_FUN) return has_mat(pool[n].a);
    return has_mat(pool[n].a) || has_mat(pool[n].b);
}

static int has_i(int n)
{
    int i, k;
    if (n == NIL) return 0;
    if (pool[n].op == OP_CONST) return pool[n].fn == K_I;
    if (pool[n].op == OP_NUM || pool[n].op == OP_VAR) return 0;
    if (pool[n].op == OP_MAT) {
        Mat *A = &mats[pool[n].fn];
        for (i = 0, k = A->rows * A->cols; i < k; i++)
            if (has_i(A->e[i])) return 1;
        return 0;
    }
    return has_i(pool[n].a) || has_i(pool[n].b);
}

static int mat_simp(int n)
{
    Mat *A = &mats[pool[n].fn];
    int el[MAXME], i, n2 = A->rows * A->cols, ch = 0;
    for (i = 0; i < n2; i++) {
        el[i] = simp(A->e[i]);
        if (el[i] == NIL) return NIL;
        if (el[i] != A->e[i]) ch = 1;
    }
    if (!ch) return n;
    return mkmat(A->rows, A->cols, el);
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
    if (f == F_DET || f == F_INV || f == F_TRANS) {
        if (pool[a].op != OP_MAT) {
            seterr(f == F_DET ? "det needs a matrix." :
                   f == F_INV ? "inv needs a matrix." : "trans needs a matrix.");
            return NIL;
        }
        if (f == F_DET) return mat_det(a);
        if (f == F_INV) return mat_inv(a);
        return mat_trans(a);
    }
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
    if (f == F_ABS) {                                     /* |a + b*i| = sqrt(a^2+b^2) */
        Num re, im;
        if (pool[a].op == OP_CONST && pool[a].fn == K_I) return mknum(num_int(1));
        if (cplx_split(a, &re, &im)) {
            int s = sqrt_num_node(num_add(num_mul(re, re), num_mul(im, im)));
            if (s != NIL) return s;
        }
    }
    if (f == F_ABS && pool[a].op == OP_CONST && pool[a].fn != K_I)
        return a;                                         /* e und pi sind positiv */

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
    if (isnum(b) && num_isint(numof(b), &k) && pool[a].op == OP_ADD) {
        Num re, im;
        if (cplx_split(a, &re, &im) && !num_zero(im)) {
            if (k < 0 && k >= -6) {              /* 1/z mit der Konjugierten erweitern */
                Num d = num_add(num_mul(re, re), num_mul(im, im));
                Num r2 = num_div(re, d), i2 = num_negv(num_div(im, d));
                int z = simp(mkadd(mknum(r2), mkmul(mknum(i2), mkconst(K_I))));
                if (z == NIL) return NIL;
                return simp(mkpow(z, mknum(num_int(-k))));
            }
            if (k >= 2 && k <= 6) {              /* (a+b*i)^n ausrechnen */
                int r2 = a;
                while (--k > 0) {
                    r2 = simp(dist(r2, a));
                    if (r2 == NIL) return NIL;
                }
                return r2;
            }
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
    for (i = 0; i < nf; i++)            /* Matrizen: Reihenfolge erhalten (nicht vertauschbar) */
        if (pool[f[i]].op == OP_MAT) {
            r = f[0];
            for (j = 1; j < nf; j++) {
                r = mkmul(r, f[j]);
                if (r == NIL) return NIL;
            }
            return r;
        }
    if (has_i(n) && !has_anyvar(n)) {   /* komplexe Klammern ausmultiplizieren */
        int anyadd = 0;
        for (i = 0; i < nf; i++) if (pool[f[i]].op == OP_ADD) anyadd = 1;
        if (anyadd) {
            r = f[0];
            for (j = 1; j < nf; j++) {
                r = dist(r, f[j]);
                if (r == NIL) return NIL;
            }
            return simp(r);
        }
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

/* sin(u)^2 bzw. cos(u)^2 erkennen */
static int trig_sq(int n, int f, int *u)
{
    long k;
    if (n == NIL || pool[n].op != OP_POW) return 0;
    if (!isnum(pool[n].b) || !num_isint(numof(pool[n].b), &k) || k != 2) return 0;
    if (pool[pool[n].a].op != OP_FUN || pool[pool[n].a].fn != f) return 0;
    *u = pool[pool[n].a].a;
    return 1;
}

static int simp_add(int n)
{
    int f[MAXF], nf = 0, i, j, nr = 0, rest[MAXF], terms[MAXF], nt = 0, t;
    Num cf[MAXF], c, extra;
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
    /* sin(u)^2 + cos(u)^2 = 1 (gleicher Koeffizient) */
    extra = num_int(0);
    for (j = 0; j < nr; j++) {
        int u1, u2;
        if (num_zero(cf[j]) || rest[j] == NIL) continue;
        if (!trig_sq(rest[j], F_SIN, &u1)) continue;
        for (i = 0; i < nr; i++) {
            if (i == j || num_zero(cf[i]) || rest[i] == NIL) continue;
            if (trig_sq(rest[i], F_COS, &u2) && eq(u1, u2) &&
                num_cmp(cf[j], cf[i]) == 0) {
                extra = num_add(extra, cf[j]);
                cf[j] = num_int(0);
                cf[i] = num_int(0);
                break;
            }
        }
    }
    for (j = 0; j < nr; j++) {
        if (num_zero(cf[j])) continue;
        if (rest[j] == NIL) t = mknum(cf[j]);
        else t = mkmul(mknum(cf[j]), rest[j]);
        if (t == NIL) return NIL;
        terms[nt++] = t;
    }
    if (!num_zero(extra)) terms[nt++] = mknum(extra);
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
    case OP_MAT: return mat_simp(n);
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
    case OP_MAT: {
        Mat *A = &mats[pool[n].fn];
        int i, k = A->rows * A->cols;
        for (i = 0; i < k; i++) if (hasvar(A->e[i], v)) return 1;
        return 0;
    }
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
    case OP_MAT:
        seterr("Cannot differentiate a matrix.");
        return NIL;
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
        case F_DELTA: case F_DDELTA: case F_DDELTA2: case F_STEP:
            seterr("Cannot differentiate delta or step.");
            return NIL;
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

static int integ(int n, char v);

/* Polynom in v mit Grad >= 1? */
static int is_poly_in(int n, char v)
{
    Num c[MAXDEG + 1];
    int deg;
    if (!hasvar(n, v)) return 0;
    if (!getpoly(n, v, c, &deg)) return 0;
    poly_trim(c, &deg);
    return deg >= 1;
}

/* Partielle Integration: u ist ein Polynom in v, f ein Faktor
 * sin/cos/exp (linear) oder ln(v). */
static int integ_parts(int u, int f, char v)
{
    int V, sum, term, sgn = 1, du, deg, i, cnt = 0;
    Num c[MAXDEG + 1], co;

    if (pool[f].op == OP_FUN && pool[f].fn == F_LN) {     /* u*ln(v) */
        int arg = pool[f].a;
        if (pool[arg].op != OP_VAR || pool[arg].fn != (unsigned char)v) return NIL;
        if (!getpoly(u, v, c, &deg)) return NIL;
        poly_trim(c, &deg);
        if (deg < 1) return NIL;
        sum = mknum(num_int(0));
        for (i = 0; i <= deg; i++) {
            if (num_zero(c[i])) continue;
            co = num_div(c[i], num_int(i + 1));
            term = mkmul(mknum(co),
                         mkmul(mkpow(mkvar(v), mknum(num_int(i + 1))),
                               mkfun(F_LN, mkvar(v))));
            term = mkadd(term, mkmul(mknum(num_negv(co)),
                          mkmul(mkpow(mkvar(v), mknum(num_int(i + 1))),
                                mkpow(mknum(num_int(i + 1)), mknum(num_int(-1))))));
            sum = mkadd(sum, term);
            if (sum == NIL) return NIL;
        }
        return simp(sum);
    }

    V = integ(f, v);
    if (V == NIL) return NIL;
    sum = mknum(num_int(0));
    for (;;) {
        term = simp(mkmul(u, V));
        if (term == NIL) return NIL;
        sum = simp(sgn > 0 ? mkadd(sum, term)
                           : mkadd(sum, mkmul(mknum(num_int(-1)), term)));
        if (sum == NIL) return NIL;
        du = simp(deriv(u, v));
        if (du == NIL) return NIL;
        if (iszero(du)) break;
        u = du;
        V = simp(integ(V, v));
        if (V == NIL) return NIL;
        sgn = -sgn;
        if (++cnt > MAXDEG) return NIL;
    }
    return sum;
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
    case OP_MUL: {
        int cand = -1, nfn = 0, u = NIL, r;
        flatrec(n, OP_MUL, f, &nf, MAXF);
        for (i = 0; i < nf; i++) {
            int g = f[i];
            if (pool[g].op == OP_FUN && hasvar(g, v) &&
                (pool[g].fn == F_SIN || pool[g].fn == F_COS ||
                 pool[g].fn == F_EXP || pool[g].fn == F_LN)) {
                nfn++;
                cand = i;
            }
        }
        if (nfn == 1) {                     /* Polynom mal sin/cos/exp/ln */
            for (i = 0; i < nf; i++) {
                if (i == cand) continue;
                u = (u == NIL) ? f[i] : mkmul(u, f[i]);
            }
            if (u != NIL && is_poly_in(u, v)) {
                r = integ_parts(u, f[cand], v);
                if (r != NIL) return r;
            }
        }
    }
        nf = 0;
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
            case F_ATAN:
                a = mkadd(mkmul(pool[n].a, mkfun(F_ATAN, pool[n].a)),
                          mkmul(mknum(num_rat(-1, 2)),
                                mkfun(F_LN, mkadd(mknum(num_int(1)),
                                      mkpow(pool[n].a, mknum(num_int(2)))))));
                break;
            case F_ASIN:
                a = mkadd(mkmul(pool[n].a, mkfun(F_ASIN, pool[n].a)),
                          mkpow(mkadd(mknum(num_int(1)),
                                      mkmul(mknum(num_int(-1)),
                                            mkpow(pool[n].a, mknum(num_int(2))))),
                                mknum(num_rat(1, 2))));
                break;
            case F_ACOS:
                a = mkadd(mkmul(pool[n].a, mkfun(F_ACOS, pool[n].a)),
                          mkmul(mknum(num_int(-1)),
                                mkpow(mkadd(mknum(num_int(1)),
                                            mkmul(mknum(num_int(-1)),
                                                  mkpow(pool[n].a, mknum(num_int(2))))),
                                      mknum(num_rat(1, 2)))));
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
    if (pool[n].op == OP_MAT) {
        Mat *A = &mats[pool[n].fn];
        int el[MAXME], i, k = A->rows * A->cols;
        for (i = 0; i < k; i++) el[i] = subst(A->e[i], v, val);
        return mkmat(A->rows, A->cols, el);
    }
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
    case OP_CONST:
        if (pool[n].fn == K_PI) *out = PI;
        else if (pool[n].fn == K_E) *out = 2.71828182845904523536;
        else return 0;                     /* i: nicht reell */
        return 1;
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

/* ---------------- komplexer Zahlenwert ---------------- */

typedef struct { double re, im; } Cplx;

static Cplx cx(double re, double im) { Cplx z; z.re = re; z.im = im; return z; }

static Cplx cx_mul(Cplx a, Cplx b)
{
    return cx(a.re * b.re - a.im * b.im, a.re * b.im + a.im * b.re);
}

static int evalc(int n, Cplx *out)
{
    Cplx a, b;
    long e;
    if (n == NIL) return 0;
    switch (pool[n].op) {
    case OP_NUM: *out = cx(num_d(numof(n)), 0.0); return 1;
    case OP_CONST:
        if (pool[n].fn == K_PI) *out = cx(PI, 0.0);
        else if (pool[n].fn == K_E) *out = cx(2.71828182845904523536, 0.0);
        else *out = cx(0.0, 1.0);
        return 1;
    case OP_ADD:
        if (!evalc(pool[n].a, &a) || !evalc(pool[n].b, &b)) return 0;
        *out = cx(a.re + b.re, a.im + b.im);
        return 1;
    case OP_MUL:
        if (!evalc(pool[n].a, &a) || !evalc(pool[n].b, &b)) return 0;
        *out = cx_mul(a, b);
        return 1;
    case OP_POW:
        if (!evalc(pool[n].a, &a) || !evalc(pool[n].b, &b)) return 0;
        if (b.im != 0.0 || b.re != floor(b.re) || fabs(b.re) > 8.0) return 0;
        e = (long)b.re;
        if (e < 0) {                            /* 1/z = konjugiert / |z|^2 */
            double d = a.re * a.re + a.im * a.im;
            if (d == 0.0) { seterr("Division by 0."); return 0; }
            a = cx(a.re / d, -a.im / d);
            e = -e;
        }
        *out = cx(1.0, 0.0);
        while (e > 0) { *out = cx_mul(*out, a); e--; }
        return 1;
    }
    return 0;
}

static void fmt_cplx(char *b, double re, double im)
{
    char ti[40];
    if (fabs(re) < 1e-12) re = 0.0;
    if (fabs(im) < 1e-12) im = 0.0;
    if (im == 0.0) { sprintf(b, "%.10g", re); return; }
    if (im == 1.0) strcpy(ti, "i");
    else if (im == -1.0) strcpy(ti, "-i");
    else sprintf(ti, "%.10g*i", im);
    if (re == 0.0) { strcpy(b, ti); return; }
    if (im > 0.0) sprintf(b, "%.10g+%s", re, ti);
    else sprintf(b, "%.10g%s", re, ti);          /* ti beginnt mit '-' */
}

/* Matrix Element fuer Element als Zahlenwert; 0 = nicht numerisch */
static int approx_mat(int node, char *out, int outsz)
{
    Mat *A = &mats[pool[node].fn];
    int i, n = A->rows * A->cols;
    double v;
    char t[40];
    rbuf = out; rlen = 0; rmax = outsz;
    if (outsz > 0) out[0] = 0;
    rch('[');
    for (i = 0; i < n; i++) {
        if (i && i % A->cols == 0) rch(';');
        else if (i) rch(',');
        if (evaln(A->e[i], 0, 0.0, &v)) {
            if (fabs(v) < 1e-12) v = 0.0;
            sprintf(t, "%.10g", v);
        } else {
            Cplx z;
            if (!evalc(A->e[i], &z)) return 0;
            fmt_cplx(t, z.re, z.im);
        }
        rput(t);
    }
    rch(']');
    return 1;
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

/* Ganze Zahl in Primfaktoren zerlegen: 360 -> 2^3*3^2*5 */
static int factor_int_node(long v)
{
    int fac[16], nf = 0, r, i;
    long p, e, n = v;
    if (n < 0) { fac[nf++] = mknum(num_int(-1)); n = -n; }
    for (p = 2; p <= n / p && nf < 14; p += (p == 2 ? 1 : 2)) {
        e = 0;
        while (n % p == 0) { n /= p; e++; }
        if (e) {                               /* p^e als Knoten, nicht als Zahl */
            int pw = newnode(OP_POW);
            if (pw == NIL) return NIL;
            pool[pw].a = (short)mknum(num_int(p));
            pool[pw].b = (short)mknum(num_int(e));
            fac[nf++] = pw;
        }
    }
    if (n > 1) {
        int pw = newnode(OP_POW);
        if (pw == NIL) return NIL;
        pool[pw].a = (short)mknum(num_int(n));
        pool[pw].b = (short)mknum(num_int(1));
        fac[nf++] = pw;
    }
    r = fac[0];                            /* MUL direkt bauen: Zahlen nicht zusammenrechnen */
    for (i = 1; i < nf; i++) {
        int t = newnode(OP_MUL);
        if (t == NIL) return NIL;
        pool[t].a = (short)r;
        pool[t].b = (short)fac[i];
        r = t;
    }
    return r;
}

static int factor_expr(int e)
{
    int s, f, deg, i, nvar, k, r, fac[16], nf = 0;
    char vc = 0, v;
    Num c[MAXDEG + 1], content, root;
    long ic[MAXDEG + 1], g = 0, L, iv;

    s = simp(expand(e));
    if (s == NIL || cas_err[0]) return NIL;
    if (isnum(s) && num_isint(numof(s), &iv) && (iv > 1 || iv < -1))
        return factor_int_node(iv);

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

/* Nullstellen von c[2]x^2 + c[1]x + c[0]; bei negativer Diskriminante
 * komplex. 0 = Fehler (Text in cas_err). */
static int quad_roots(Num *c, int *roots, int *nr)
{
    Num D = num_sub(num_mul(c[1], c[1]), num_mul(num_int(4), num_mul(c[2], c[0])));
    int den = mknum(num_mul(num_int(2), c[2]));
    int re, im, s;
    if (den == NIL) return 0;
    if (num_neg(D)) {
        Num nd = num_negv(D);
        re = mknum(num_div(num_negv(c[1]), num_mul(num_int(2), c[2])));
        s = sqrt_num_node(nd);
        if (s == NIL) return 0;
        im = simp(mkmul(s, mkpow(den, mknum(num_int(-1)))));
        if (im == NIL) return 0;
        roots[(*nr)++] = simp(mkadd(re, mkmul(im, mkconst(K_I))));
        roots[(*nr)++] = simp(mkadd(re, mkmul(mknum(num_int(-1)), mkmul(im, mkconst(K_I)))));
        return 1;
    }
    s = sqrt_num_node(D);
    if (s == NIL) return 0;
    roots[(*nr)++] = simp(mkmul(mkadd(mknum(num_negv(c[1])), s),
                                mkpow(den, mknum(num_int(-1)))));
    if (!num_zero(D))
        roots[(*nr)++] = simp(mkmul(mkadd(mknum(num_negv(c[1])),
                                         mkmul(mknum(num_int(-1)), s)),
                                    mkpow(den, mknum(num_int(-1)))));
    return 1;
}

/* Lineare Form a*v+b mit beliebigen (symbolischen) Koeffizienten */
static int lin_sym(int n, char v, int *a, int *b)
{
    int f[MAXF], nf = 0, i, dep = -1, A = NIL, a1, b1, a2, b2;
    long k;
    if (n == NIL) return 0;
    if (!hasvar(n, v)) { *a = mknum(num_int(0)); *b = n; return 1; }
    if (pool[n].op == OP_VAR) {
        if (pool[n].fn != (unsigned char)v) return 0;
        *a = mknum(num_int(1));
        *b = mknum(num_int(0));
        return 1;
    }
    if (pool[n].op == OP_ADD) {
        if (!lin_sym(pool[n].a, v, &a1, &b1)) return 0;
        if (!lin_sym(pool[n].b, v, &a2, &b2)) return 0;
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
        if (!lin_sym(f[dep], v, &a1, &b1)) return 0;
        *a = (A == NIL) ? a1 : mkmul(A, a1);
        *b = (A == NIL) ? b1 : mkmul(A, b1);
        return 1;
    }
    if (pool[n].op == OP_POW && isnum(pool[n].b) &&
        num_isint(numof(pool[n].b), &k) && k == 1)
        return lin_sym(pool[n].a, v, a, b);
    return 0;
}

/* Lineares Gleichungssystem: [gl1;gl2],[x,y] */
static int solve_system(int eqm, int varm, char *out, int outsz)
{
    Mat *E, *V;
    int a[MAXDIM][MAXDIM + 1], ne, nv, i, j, k, row = 0, piv;
    char vars[MAXDIM], line[1024], tmp[160];

    if (pool[eqm].op != OP_MAT || pool[varm].op != OP_MAT) {
        setout(out, outsz, "solve: needs [equations],[variables].");
        return -1;
    }
    E = &mats[pool[eqm].fn];
    V = &mats[pool[varm].fn];
    ne = E->rows * E->cols;
    nv = V->rows * V->cols;
    if (nv < 1 || nv > MAXDIM || ne > MAXDIM) {
        setout(out, outsz, "solve: too many equations or variables.");
        return -1;
    }
    for (j = 0; j < nv; j++) {
        int vn = V->e[j];
        if (pool[vn].op != OP_VAR) {
            setout(out, outsz, "solve: variables must be single letters.");
            return -1;
        }
        vars[j] = (char)pool[vn].fn;
    }
    for (i = 0; i < ne; i++) {
        int e = E->e[i], b;
        if (pool[e].op == OP_EQ)
            e = mkadd(pool[e].a, mkmul(mknum(num_int(-1)), pool[e].b));
        e = simp(expand(e));
        if (!cas_ok(e)) { setout(out, outsz, cas_err[0] ? cas_err : "Syntax error."); return -1; }
        for (j = 0; j < nv; j++) {                 /* Koeffizienten */
            int c = simp(deriv(e, vars[j]));
            if (!cas_ok(c)) { setout(out, outsz, cas_err[0] ? cas_err : "Syntax error."); return -1; }
            for (k = 0; k < nv; k++)              /* nur die Systemvariablen zaehlen */
                if (hasvar(c, vars[k])) {
                    setout(out, outsz, "solve: system must be linear.");
                    return -1;
                }
            a[i][j] = c;
        }
        b = e;                                     /* Konstante */
        for (j = 0; j < nv; j++) {
            b = subst(b, vars[j], mknum(num_int(0)));
            if (b == NIL) { setout(out, outsz, "Syntax error."); return -1; }
        }
        b = simp(b);
        if (!cas_ok(b)) { setout(out, outsz, cas_err[0] ? cas_err : "Syntax error."); return -1; }
        a[i][nv] = simp(mkmul(mknum(num_int(-1)), b));
        if (a[i][nv] == NIL) return -1;
    }
    {   /* symbolische Koeffizienten: Cramer-Regel liefert die geschlossene Form */
        int el[MAXME], i2, j2, det, detj, val, done = 0;
        for (i2 = 0; i2 < nv; i2++)
            for (j2 = 0; j2 < nv; j2++) el[i2 * nv + j2] = a[i2][j2];
        det = mkmat(nv, nv, el);
        if (det != NIL) det = simp(mat_det(det));
        if (det != NIL && cas_ok(det) && !isnum(det)) {
            line[0] = 0;
            for (j2 = 0; j2 < nv; j2++) {
                for (i2 = 0; i2 < nv; i2++)
                    for (k = 0; k < nv; k++)
                        el[i2 * nv + k] = (k == j2) ? a[i2][nv] : a[i2][k];
                detj = mkmat(nv, nv, el);
                if (detj == NIL) break;
                detj = simp(expand(mat_det(detj)));
                if (!cas_ok(detj)) break;
                val = simp(mkmul(detj, mkpow(det, mknum(num_int(-1)))));
                if (!cas_ok(val)) break;
                render_to(val, tmp, sizeof tmp);
                if (j2) strcat(line, " and ");
                if (strlen(line) + strlen(tmp) + 8 < sizeof line)
                    sprintf(line + strlen(line), "%c = %s", vars[j2], tmp);
                done = 1;
            }
            if (done && j2 == nv) { setout(out, outsz, line); return 1; }
        }
    }
    for (k = 0; k < nv && row < ne; k++) {         /* Gauss-Jordan */
        int x;
        piv = -1;
        for (i = row; i < ne; i++) if (!iszero(a[i][k])) { piv = i; break; }
        if (piv < 0) continue;
        if (piv != row)
            for (j = k; j <= nv; j++) { x = a[row][j]; a[row][j] = a[piv][j]; a[piv][j] = x; }
        x = a[row][k];
        for (j = k; j <= nv; j++) {
            a[row][j] = simp(mkmul(a[row][j], mkpow(x, mknum(num_int(-1)))));
            if (a[row][j] == NIL) return -1;
        }
        for (i = 0; i < ne; i++) {
            int f;
            if (i == row || iszero(a[i][k])) continue;
            f = a[i][k];
            for (j = k; j <= nv; j++) {
                a[i][j] = simp(mkadd(a[i][j],
                    mkmul(mknum(num_int(-1)), mkmul(f, a[row][j]))));
                if (a[i][j] == NIL) return -1;
            }
        }
        row++;
    }
    for (i = 0; i < ne; i++) {
        int allzero = 1;
        for (j = 0; j < nv; j++) if (!iszero(a[i][j])) allzero = 0;
        if (allzero && !iszero(a[i][nv])) { setout(out, outsz, "no solution"); return 1; }
    }
    if (row < nv) { setout(out, outsz, "infinitely many solutions"); return 1; }
    line[0] = 0;
    for (j = 0; j < nv; j++) {
        render_to(a[j][nv], tmp, sizeof tmp);
        if (j) strcat(line, " and ");
        if (strlen(line) + strlen(tmp) + 8 < sizeof line)
            sprintf(line + strlen(line), "%c = %s", vars[j], tmp);
    }
    setout(out, outsz, line);
    return 1;
}

static int solve_cmd(int node, char v, char *out, int outsz)
{
    int e, roots[MAXDEG + 2], nr = 0, i, j, deg, A, B;
    Num c[MAXDEG + 1], root;
    char line[1024], tmp[120];

    if (has_mat(node)) {
        strcpy(out, "solve needs an equation, not a matrix.");
        return -1;
    }
    if (pool[node].op == OP_EQ)
        e = mkadd(pool[node].a, mkmul(mknum(num_int(-1)), pool[node].b));
    else
        e = node;
    e = simp(expand(e));
    if (e == NIL || cas_err[0]) { strncpy(out, cas_err, outsz - 1); out[outsz - 1] = 0; return -1; }
    if (!getpoly(e, v, c, &deg)) {
        int sol;
        if (lin_sym(e, v, &A, &B)) {          /* a*x+b=0 -> x = -b/a */
            if (iszero(A)) {
                if (iszero(B)) sprintf(out, "true for all %c", v);
                else strcpy(out, "no solution");
                return 1;
            }
            sol = simp(mkmul(mknum(num_int(-1)),
                             mkmul(B, mkpow(A, mknum(num_int(-1))))));
            if (cas_ok(sol)) {
                render_to(sol, tmp, sizeof tmp);
                sprintf(out, "%c = %s", v, tmp);
                return 1;
            }
        }
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
        if (!quad_roots(c, roots, &nr)) {
            strcpy(out, cas_err[0] ? cas_err : "Cannot solve that exactly.");
            return -1;
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
            if (!quad_roots(c, roots, &nr)) {
                strcpy(out, cas_err[0] ? cas_err : "Cannot solve that exactly.");
                return -1;
            }
        } else if (deg > 2) {
            strcpy(out, "Cannot solve that exactly.");
            return -1;
        }
    }
    if (nr == 0) { strcpy(out, "no solution"); return 1; }
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

/* ---------------- Erweiterte Befehle ---------------- */

/* Ganzzahl aus einem Knoten; 0 = keine ganze Zahl */
static int int_of(int node, long *v)
{
    if (!cas_ok(node)) return 0;
    node = simp(node);
    if (!isnum(node)) return 0;
    return num_isint(numof(node), v);
}

/* Reduzierte Zeilenstufenform (Gauss-Jordan), exakt und mit Symbolen */
static int mat_rref(int m)
{
    Mat *A = &mats[pool[m].fn];
    int a[MAXME], el[MAXME], t, f, d, x;
    int n = A->rows, c = A->cols, i, j, row, lead, piv;
    for (i = 0; i < n * c; i++) {
        a[i] = simp(A->e[i]);
        if (a[i] == NIL) return NIL;
    }
    row = 0;
    for (lead = 0; lead < c && row < n; lead++) {
        piv = -1;
        for (i = row; i < n; i++) if (!iszero(a[i * c + lead])) { piv = i; break; }
        if (piv < 0) continue;
        if (piv != row)
            for (j = 0; j < c; j++) {
                t = a[row * c + j]; a[row * c + j] = a[piv * c + j]; a[piv * c + j] = t;
            }
        x = a[row * c + lead];
        if (!isone(x)) {
            d = simp(mkpow(x, mknum(num_int(-1))));
            if (d == NIL) return NIL;
            for (j = 0; j < c; j++) {
                a[row * c + j] = simp(mkmul(a[row * c + j], d));
                if (a[row * c + j] == NIL) return NIL;
            }
        }
        for (i = 0; i < n; i++) {
            if (i == row) continue;
            f = a[i * c + lead];
            if (iszero(f)) continue;
            for (j = 0; j < c; j++) {
                a[i * c + j] = simp(mkadd(a[i * c + j],
                    mkmul(mknum(num_int(-1)), mkmul(f, a[row * c + j]))));
                if (a[i * c + j] == NIL) return NIL;
            }
        }
        row++;
    }
    for (i = 0; i < n * c; i++) el[i] = a[i];
    return mkmat(n, c, el);
}

static int mat_rank(int m)
{
    int r = mat_rref(m), i, j, k = 0;
    Mat *R;
    if (r == NIL) return NIL;
    R = &mats[pool[r].fn];
    for (i = 0; i < R->rows; i++) {
        for (j = 0; j < R->cols; j++) if (!iszero(R->e[i * R->cols + j])) break;
        if (j < R->cols) k++;
    }
    return mknum(num_int(k));
}

/* Lineares Gleichungssystem A*x = b loesen */
static int mat_lsolve(int x, int y)
{
    Mat *A = &mats[pool[x].fn], *B = &mats[pool[y].fn];
    int el[MAXME], i, inv, r;
    if (A->rows != A->cols) { seterr("lsolve needs a square matrix."); return NIL; }
    if (B->rows == 1 && B->cols == A->rows) {      /* Zeilenvektor als Spalte */
        for (i = 0; i < B->cols; i++) el[i] = B->e[i];
        y = mkmat(B->cols, 1, el);
        if (y == NIL) return NIL;
        B = &mats[pool[y].fn];
    }
    if (B->rows != A->rows || B->cols != 1) {
        seterr("lsolve needs matrix and vector.");
        return NIL;
    }
    inv = mat_inv(x);
    if (inv == NIL) return NIL;
    r = mat_mul(inv, y);
    if (r == NIL) return NIL;
    return mat_simp(r);
}

/* Charakteristisches Polynom det(A - x*I) */
static int mat_charpoly(int m, char v)
{
    Mat *A = &mats[pool[m].fn];
    int el[MAXME], i, j, r, n = A->rows;
    if (A->rows != A->cols) { seterr("charpoly needs a square matrix."); return NIL; }
    for (i = 0; i < n; i++)
        for (j = 0; j < n; j++) {
            if (i == j)
                el[i * n + j] = mkadd(A->e[i * n + j],
                                      mkmul(mknum(num_int(-1)), mkvar(v)));
            else
                el[i * n + j] = A->e[i * n + j];
            if (el[i * n + j] == NIL) return NIL;
        }
    r = mkmat(n, n, el);
    if (r == NIL) return NIL;
    r = mat_det(r);
    if (r == NIL) return NIL;
    return simp(expand(r));
}

/* Nullstellen eines Polynoms in v: linear, quadratisch (auch komplex)
 * und rationale Wurzeln. 1 = ok. */
static int poly_roots(int e, char v, int *roots, int *nr)
{
    int deg;
    Num c[MAXDEG + 1], root;
    if (!getpoly(e, v, c, &deg)) return 0;
    poly_trim(c, &deg);
    if (deg < 1) return 0;
    if (deg == 1) {
        roots[(*nr)++] = mknum(num_div(num_negv(c[0]), c[1]));
        return 1;
    }
    if (deg == 2) return quad_roots(c, roots, nr);
    while (deg > 2) {
        if (!find_rat_root(c, deg, &root)) return 0;
        roots[(*nr)++] = mknum(root);
        deflate(c, deg, root);
        deg--;
        poly_trim(c, &deg);
    }
    if (deg == 1) roots[(*nr)++] = mknum(num_div(num_negv(c[0]), c[1]));
    else if (deg == 2) return quad_roots(c, roots, nr);
    return 1;
}

/* Kreuzprodukt zweier Zeilen; NIL, wenn es der Nullvektor ist */
static int cross_rows(int a0, int a1, int a2, int b0, int b1, int b2, int *v)
{
    v[0] = simp(expand(mkadd(mkmul(a1, b2), mkmul(mknum(num_int(-1)), mkmul(a2, b1)))));
    v[1] = simp(expand(mkadd(mkmul(a2, b0), mkmul(mknum(num_int(-1)), mkmul(a0, b2)))));
    v[2] = simp(expand(mkadd(mkmul(a0, b1), mkmul(mknum(num_int(-1)), mkmul(a1, b0)))));
    if (v[0] == NIL || v[1] == NIL || v[2] == NIL) return 0;
    if (iszero(v[0]) && iszero(v[1]) && iszero(v[2])) return 0;
    return 1;
}

/* Vorzeichen positiv drehen und gemeinsame Faktoren kuerzen */
static void vec_normalize(int *v, int n)
{
    int i;
    long g = 0, val;
    for (i = 0; i < n; i++) {
        if (iszero(v[i])) continue;
        if (isnum(v[i]) && num_neg(numof(v[i])))
            for (i = 0; i < n; i++)
                v[i] = simp(mkmul(mknum(num_int(-1)), v[i]));
        break;
    }
    for (i = 0; i < n; i++) {
        if (iszero(v[i])) continue;
        if (!isnum(v[i]) || !num_isint(numof(v[i]), &val)) return;
        g = lgcd(g, val);
    }
    if (g > 1)
        for (i = 0; i < n; i++)
            v[i] = simp(mkmul(v[i], mknum(num_rat(1, g))));
}

/* Eigenvektor zu lam: Kern von A - lam*I */
static int eigvec_for(int m, int lam, int *out)
{
    Mat *A = &mats[pool[m].fn];
    int n = A->rows, el[MAXME], i, j, R, npiv = 0, freecol = -1;
    int pcol[MAXDIM], M[MAXDIM][MAXDIM];

    for (i = 0; i < n; i++)
        for (j = 0; j < n; j++) {
            int e = A->e[i * n + j];
            if (i == j) e = mkadd(e, mkmul(mknum(num_int(-1)), lam));
            if (e == NIL) return NIL;
            M[i][j] = e;
        }
    if (n == 1) {
        el[0] = mknum(num_int(1));
        *out = mkmat(1, 1, el);
        return *out;
    }
    if (n == 2) {                               /* Kern von [[a,b],[c,d]] */
        int v0 = simp(expand(mkmul(mknum(num_int(-1)), M[0][1])));
        int v1 = simp(expand(M[0][0]));
        if (v0 == NIL || v1 == NIL) return NIL;
        if (iszero(v0) && iszero(v1)) {
            v0 = simp(expand(M[1][1]));
            v1 = simp(expand(mkmul(mknum(num_int(-1)), M[1][0])));
        }
        if (v0 == NIL || v1 == NIL) return NIL;
        if (iszero(v0) && iszero(v1)) return NIL;
        el[0] = v0; el[1] = v1;
        vec_normalize(el, 2);
        *out = mkmat(2, 1, el);
        return *out;
    }
    if (n == 3) {                               /* Kreuzprodukt zweier Zeilen */
        if (!cross_rows(M[0][0], M[0][1], M[0][2], M[1][0], M[1][1], M[1][2], el) &&
            !cross_rows(M[0][0], M[0][1], M[0][2], M[2][0], M[2][1], M[2][2], el) &&
            !cross_rows(M[1][0], M[1][1], M[1][2], M[2][0], M[2][1], M[2][2], el))
            return NIL;
        vec_normalize(el, 3);
        *out = mkmat(3, 1, el);
        return *out;
    }
    for (i = 0; i < n * n; i++) el[i] = M[i / n][i % n];
    R = mkmat(n, n, el);
    if (R == NIL) return NIL;
    R = mat_rref(R);
    if (R == NIL) return NIL;
    {
        Mat *B = &mats[pool[R].fn];
        for (i = 0; i < n; i++) {
            for (j = 0; j < n; j++) if (!iszero(B->e[i * n + j])) break;
            if (j < n) pcol[npiv++] = j;
        }
        for (j = 0; j < n; j++) {
            int k, used = 0;
            for (k = 0; k < npiv; k++) if (pcol[k] == j) used = 1;
            if (!used) { freecol = j; break; }
        }
        if (freecol < 0) return NIL;
        for (j = 0; j < n; j++) el[j] = mknum(num_int(0));
        el[freecol] = mknum(num_int(1));
        for (i = 0; i < npiv; i++)
            el[pcol[i]] = simp(mkmul(mknum(num_int(-1)),
                                     B->e[i * n + freecol]));
        for (i = 0; i < n; i++) if (el[i] == NIL) return NIL;
        vec_normalize(el, n);
        *out = mkmat(n, 1, el);
        return *out;
    }
}

static int eigenvals_cmd(int m, char v, char *out, int outsz)
{
    int cp, roots[MAXDEG + 2], nr = 0, i, j;
    char line[1024], tmp[120];

    cp = mat_charpoly(m, v);
    if (cp == NIL) return -1;
    if (!poly_roots(cp, v, roots, &nr) || nr == 0) {
        setout(out, outsz, "eigenvals: cannot solve the characteristic polynomial.");
        return -1;
    }
    for (i = 1; i < nr; i++) {                  /* numerisch sortieren */
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
    line[0] = 0;
    for (i = 0; i < nr; i++) {
        render_to(roots[i], tmp, sizeof tmp);
        if (i) strcat(line, " or ");
        if (strlen(line) + strlen(tmp) + 8 < sizeof line)
            sprintf(line + strlen(line), "%c = %s", v, tmp);
    }
    setout(out, outsz, line);
    return 1;
}

static int eigenvect_cmd(int m, char v, char *out, int outsz)
{
    int cp, roots[MAXDEG + 2], nr = 0, i, j, vecs[MAXDEG + 2];
    char line[1024], tmp[160];

    cp = mat_charpoly(m, v);
    if (cp == NIL) return -1;
    if (!poly_roots(cp, v, roots, &nr) || nr == 0) {
        setout(out, outsz, "eigenvect: cannot solve the characteristic polynomial.");
        return -1;
    }
    for (i = 0; i < nr; i++)
        if (eigvec_for(m, roots[i], &vecs[i]) == NIL) {
            setout(out, outsz, "eigenvect: cannot build the eigenvector.");
            return -1;
        }
    for (i = 1; i < nr; i++) {                  /* mit den Eigenwerten sortieren */
        int x = roots[i], xv2 = vecs[i], okx;
        double xv = 0.0, yv;
        okx = evaln(x, 0, 0.0, &xv);
        for (j = i; j > 0; j--) {
            int oky = evaln(roots[j - 1], 0, 0.0, &yv);
            if (!okx || !oky || yv <= xv) break;
            roots[j] = roots[j - 1];
            vecs[j] = vecs[j - 1];
        }
        roots[j] = x;
        vecs[j] = xv2;
    }
    line[0] = 0;
    for (i = 0; i < nr; i++) {
        render_to(vecs[i], tmp, sizeof tmp);
        if (i) strcat(line, " or ");
        if (strlen(line) + strlen(tmp) + 2 < sizeof line) strcat(line, tmp);
    }
    setout(out, outsz, line);
    return 1;
}

/* n! exakt bis 12! */
static int mkfact(int n)
{
    long r = 1, i;
    if (n < 0) { seterr("fact needs a whole number >= 0."); return NIL; }
    if (n > 12) { seterr("fact is exact only up to 12!."); return NIL; }
    for (i = 2; i <= n; i++) r *= i;
    return mknum(num_int(r));
}

/* Taylor-Reihe bis zur Ordnung n um x = a */
static int taylor_cmd(int f, char v, int at, long n, char *out, int outsz)
{
    int d, k, term, sum, pw, xm;
    Num co;
    long i;
    if (n < 0 || n > 8) { setout(out, outsz, "taylor needs order 0..8."); return -1; }
    xm = mkadd(mkvar(v), mkmul(mknum(num_int(-1)), at));     /* x - a */
    sum = mknum(num_int(0));
    co = num_int(1);
    d = f;
    for (i = 0; i <= n; i++) {
        if (i > 0) {
            d = deriv(d, v);
            if (d == NIL) { setout(out, outsz, "Cannot differentiate that."); return -1; }
            co = num_div(co, num_int(i));
        }
        term = simp(subst(d, v, at));
        if (!cas_ok(term)) {
            setout(out, outsz, cas_err[0] ? cas_err : "Syntax error.");
            return -1;
        }
        term = simp(mkmul(mknum(co), term));
        pw = mkpow(xm, mknum(num_int(i)));
        if (term == NIL || pw == NIL) break;
        k = simp(mkmul(term, pw));
        if (k == NIL) break;
        sum = simp(mkadd(sum, k));
        if (!cas_ok(sum)) break;
    }
    sum = simp(expand(sum));
    if (!cas_ok(sum)) {
        setout(out, outsz, cas_err[0] ? cas_err : "Syntax error.");
        return -1;
    }
    render_to(sum, out, outsz);
    return 1;
}

/* Ausdruck in Zaehler und Nenner zerlegen (fuer l'Hospital) */
static void split_frac(int n, int *num, int *den)
{
    int f[MAXF], nf = 0, i;
    long k;
    *num = mknum(num_int(1));
    *den = mknum(num_int(1));
    if (pool[n].op == OP_MUL) flatrec(n, OP_MUL, f, &nf, MAXF);
    else { f[0] = n; nf = 1; }
    for (i = 0; i < nf; i++) {
        int b = f[i];
        if (pool[b].op == OP_POW && isnum(pool[b].b) &&
            num_isint(numof(pool[b].b), &k) && k < 0) {
            *den = mkmul(*den, mkpow(pool[b].a, mknum(num_int(-k))));
            continue;
        }
        *num = mkmul(*num, b);
    }
}

/* Grenzwert an einer endlichen Stelle; 0/0 mit l'Hospital */
static int limit_cmd(int node, char v, int at, char *out, int outsz)
{
    int L, num, den, nv, dv, i;
    L = simp(subst(node, v, at));
    if (cas_ok(L) && !hasvar(L, v)) {
        render_to(L, out, outsz);
        return 1;
    }
    cas_err[0] = 0;
    split_frac(simp(node), &num, &den);
    for (i = 0; i < 4; i++) {
        nv = simp(subst(num, v, at));
        dv = simp(subst(den, v, at));
        if (!cas_ok(nv) || !cas_ok(dv) || hasvar(nv, v) || hasvar(dv, v)) {
            cas_err[0] = 0;
        } else if (!iszero(dv)) {
            L = simp(mkmul(nv, mkpow(dv, mknum(num_int(-1)))));
            if (cas_ok(L)) { render_to(L, out, outsz); return 1; }
            break;
        } else if (!iszero(nv)) {
            break;                            /* Zaehler endlich, Nenner 0 */
        }
        cas_err[0] = 0;
        num = deriv(num, v);
        den = deriv(den, v);
        if (num == NIL || den == NIL) break;
    }
    setout(out, outsz, "No finite limit found.");
    return -1;
}

/* Numerisch loesen mit dem Newton-Verfahren */
static int nsolve_cmd(int eq, char v, double x0, char *out, int outsz)
{
    int f, df;
    double x = x0, fx, dfx, x1;
    int i;
    if (pool[eq].op == OP_EQ)
        f = simp(expand(mkadd(pool[eq].a, mkmul(mknum(num_int(-1)), pool[eq].b))));
    else
        f = simp(expand(eq));
    if (!cas_ok(f)) { setout(out, outsz, cas_err[0] ? cas_err : "Syntax error."); return -1; }
    df = simp(deriv(f, v));
    if (df == NIL) { setout(out, outsz, "Cannot differentiate that."); return -1; }
    for (i = 0; i < 40; i++) {
        if (!evaln(f, v, x, &fx) || !evaln(df, v, x, &dfx)) {
            setout(out, outsz, cas_err[0] ? cas_err : "Cannot evaluate.");
            return -1;
        }
        if (fabs(dfx) < 1e-14) { setout(out, outsz, "Derivative is zero."); return -1; }
        x1 = x - fx / dfx;
        if (fabs(x1 - x) < 1e-10) { x = x1; break; }
        x = x1;
        if (fabs(x) > 1e12) { setout(out, outsz, "No convergence."); return -1; }
    }
    if (fabs(x) < 1e-12) x = 0.0;
    sprintf(out, "%c = %.10g", v, x);
    return 1;
}

/* Summe bzw. Produkt ueber einen ganzzahligen Bereich */
static int sumprod(int e, char v, long a, long b, int isprod, char *out, int outsz)
{
    int acc, t;
    long k;
    if (b < a) { setout(out, outsz, "Upper bound below lower bound."); return -1; }
    if (b - a > 40) { setout(out, outsz, "Too many terms (max 41)."); return -1; }
    acc = mknum(num_int(isprod ? 1 : 0));
    for (k = a; k <= b; k++) {
        t = simp(subst(e, v, mknum(num_int(k))));
        if (!cas_ok(t)) {
            setout(out, outsz, cas_err[0] ? cas_err : "Syntax error.");
            return -1;
        }
        acc = simp(isprod ? mkmul(acc, t) : mkadd(acc, t));
        if (!cas_ok(acc)) {
            setout(out, outsz, cas_err[0] ? cas_err : "Expression too complex.");
            return -1;
        }
    }
    render_to(acc, out, outsz);
    return 1;
}

/* Vektoren: Laenge, Element, Skalar- und Kreuzprodukt */
static int veclen(int m)
{
    Mat *A = &mats[pool[m].fn];
    if (A->rows == 1) return A->cols;
    if (A->cols == 1) return A->rows;
    return 0;
}

static int vecel(int m, int i)
{
    Mat *A = &mats[pool[m].fn];
    return A->rows == 1 ? A->e[i] : A->e[i * A->cols];
}

static int mat_dot(int x, int y)
{
    int n = veclen(x), i, s;
    if (n < 1 || n != veclen(y)) {
        seterr("dot needs two vectors of equal length.");
        return NIL;
    }
    s = mknum(num_int(0));
    for (i = 0; i < n; i++) {
        s = simp(mkadd(s, mkmul(vecel(x, i), vecel(y, i))));
        if (s == NIL) return NIL;
    }
    return s;
}

static int mat_cross(int x, int y)
{
    int el[3], a1, a2, a3, b1, b2, b3;
    if (veclen(x) != 3 || veclen(y) != 3) {
        seterr("cross needs two 3-vectors.");
        return NIL;
    }
    a1 = vecel(x, 0); a2 = vecel(x, 1); a3 = vecel(x, 2);
    b1 = vecel(y, 0); b2 = vecel(y, 1); b3 = vecel(y, 2);
    el[0] = simp(mkadd(mkmul(a2, b3), mkmul(mknum(num_int(-1)), mkmul(a3, b2))));
    el[1] = simp(mkadd(mkmul(a3, b1), mkmul(mknum(num_int(-1)), mkmul(a1, b3))));
    el[2] = simp(mkadd(mkmul(a1, b2), mkmul(mknum(num_int(-1)), mkmul(a2, b1))));
    if (el[0] == NIL || el[1] == NIL || el[2] == NIL) return NIL;
    return mkmat(3, 1, el);
}

/* Zahlentheorie auf ganzen Zahlen */
static int mk_gcd(long a, long b)
{
    if (!a && !b) return mknum(num_int(0));
    return mknum(num_int(lgcd(a, b)));
}

static int mk_lcm(long a, long b)
{
    long g, r;
    if (!a || !b) return mknum(num_int(0));
    if (a < 0) a = -a;
    if (b < 0) b = -b;
    g = lgcd(a, b);
    if (!lmulck(a / g, b, &r)) { seterr("Number too large."); return NIL; }
    return mknum(num_int(r));
}

static int mk_mod(long a, long b)
{
    long r;
    if (!b) { seterr("Division by 0."); return NIL; }
    r = a % b;
    if (r && ((r < 0) != (b < 0))) r += b;
    return mknum(num_int(r));
}

static int mk_ncr(long n, long k)
{
    long r = 1, i;
    if (n < 0 || k < 0 || k > n) { seterr("ncr needs 0 <= k <= n."); return NIL; }
    if (k > n - k) k = n - k;
    for (i = 1; i <= k; i++) {
        if (!lmulck(r, n - k + i, &r)) { seterr("Number too large."); return NIL; }
        r /= i;
    }
    return mknum(num_int(r));
}

static int mk_npr(long n, long k)
{
    long r = 1, i;
    if (n < 0 || k < 0 || k > n) { seterr("npr needs 0 <= k <= n."); return NIL; }
    for (i = 0; i < k; i++)
        if (!lmulck(r, n - i, &r)) { seterr("Number too large."); return NIL; }
    return mknum(num_int(r));
}

/* ---------------- Laplace-Transformation (vorwaerts) ---------------- */

/* L{f(t)} fuer einen Summanden: Konstante, t^k, exp(a*t), sin/cos(w*t)
 * und Produkte daraus (Verschiebungssatz, Ableitung nach s). */
static int lap_term(int term, char tv, char sv, int *out)
{
    int f[MAXF], nf = 0, i, C = NIL, F0, a = NIL, b = NIL, t;
    int ae = NIL, at = NIL;
    int k = 0, expo = 0, trig = 0;
    long e2;

    if (term == NIL) return 0;
    if (!hasvar(term, tv)) {                    /* Konstante -> C/s */
        t = simp(mkmul(term, mkpow(mkvar(sv), mknum(num_int(-1)))));
        if (t == NIL) return 0;
        *out = t;
        return 1;
    }
    if (pool[term].op == OP_MUL) flatrec(term, OP_MUL, f, &nf, MAXF);
    else { f[0] = term; nf = 1; }
    for (i = 0; i < nf; i++) {
        int g = f[i];
        if (!hasvar(g, tv)) { C = (C == NIL) ? g : mkmul(C, g); continue; }
        if (pool[g].op == OP_VAR) {
            if (pool[g].fn != (unsigned char)tv) return 0;
            k++;
            continue;
        }
        if (pool[g].op == OP_POW) {
            int base = pool[g].a;
            if (pool[base].op != OP_VAR || pool[base].fn != (unsigned char)tv) return 0;
            if (!isnum(pool[g].b) || !num_isint(numof(pool[g].b), &e2) || e2 < 0) return 0;
            k += (int)e2;
            continue;
        }
        if (pool[g].op == OP_FUN && (pool[g].fn == F_EXP || pool[g].fn == F_SIN ||
                                     pool[g].fn == F_COS)) {
            if (!lin_in(pool[g].a, tv, &a, &b) || !iszero(b)) return 0;
            if (pool[g].fn == F_EXP) {
                if (expo) return 0;
                expo = 1;
                ae = a;
            } else {
                if (trig) return 0;
                trig = (int)pool[g].fn;
                at = a;
            }
            continue;
        }
        return 0;
    }

    if (trig) {                                 /* w/(s^2+w^2) bzw. s/(s^2+w^2) */
        int den = mkadd(mkpow(mkvar(sv), mknum(num_int(2))),
                        mkpow(at, mknum(num_int(2))));
        F0 = (trig == F_SIN) ? mkmul(at, mkpow(den, mknum(num_int(-1))))
                             : mkmul(mkvar(sv), mkpow(den, mknum(num_int(-1))));
    } else if (expo) {                          /* 1/(s-a) */
        F0 = mkpow(mkadd(mkvar(sv), mkmul(mknum(num_int(-1)), ae)),
                   mknum(num_int(-1)));
    } else {
        F0 = mkpow(mkvar(sv), mknum(num_int(-1)));
    }
    if (F0 == NIL) return 0;
    for (i = 0; i < k; i++) {                   /* L{t^k f} = (-1)^k F^(k)(s) */
        F0 = deriv(F0, sv);
        if (F0 == NIL) return 0;
        F0 = mkmul(mknum(num_int(-1)), F0);
        if (F0 == NIL) return 0;
    }
    if (expo && trig) {                         /* Verschiebungssatz */
        F0 = subst(F0, sv, mkadd(mkvar(sv), mkmul(mknum(num_int(-1)), ae)));
        if (F0 == NIL) return 0;
    }
    if (C != NIL) F0 = mkmul(C, F0);
    if (F0 == NIL) return 0;
    F0 = simp(F0);
    if (F0 == NIL) return 0;
    *out = F0;
    return 1;
}

static int laplace_cmd(int node, char tv, char sv, char *out, int outsz)
{
    int f[MAXF], nf = 0, i, acc, t;
    node = simp(expand(node));
    if (!cas_ok(node)) {
        setout(out, outsz, cas_err[0] ? cas_err : "Syntax error.");
        return -1;
    }
    if (pool[node].op == OP_ADD) flatrec(node, OP_ADD, f, &nf, MAXF);
    else { f[0] = node; nf = 1; }
    acc = mknum(num_int(0));
    for (i = 0; i < nf; i++) {
        if (!lap_term(f[i], tv, sv, &t)) {
            setout(out, outsz, "laplace: form not supported.");
            return -1;
        }
        acc = simp(mkadd(acc, t));
        if (!cas_ok(acc)) {
            setout(out, outsz, cas_err[0] ? cas_err : "Expression too complex.");
            return -1;
        }
    }
    acc = simp(expand(acc));
    if (!cas_ok(acc)) {
        setout(out, outsz, cas_err[0] ? cas_err : "Expression too complex.");
        return -1;
    }
    render_to(acc, out, outsz);
    return 1;
}

/* ---------------- Inverse Laplace-Transformation ---------------- */

#define ILN (MAXDEG + 2)

static void ptrim(Num *c, int *d)
{
    while (*d > 0 && num_zero(c[*d])) (*d)--;
}

static void pmul(const Num *a, int da, const Num *b, int db, Num *c, int *dc)
{
    int i, j;
    for (i = 0; i <= da + db; i++) c[i] = num_int(0);
    for (i = 0; i <= da; i++)
        for (j = 0; j <= db; j++)
            c[i + j] = num_add(c[i + j], num_mul(a[i], b[j]));
    *dc = da + db;
    ptrim(c, dc);
}

static void pcopy(Num *d, const Num *s, int n)
{
    int i;
    for (i = 0; i <= n; i++) d[i] = s[i];
}

/* Produkt aller Linearfaktoren (s-a)^m, einer davon optional mit anderem Grad */
static void basis_poly(Num *roots, int *mult, int nr, int skip, int skipExp,
                       const Num *q, int qd, Num *out, int *od)
{
    Num fac[MAXDEG + 1], lin[2];
    int i, e, d;
    out[0] = num_int(1);
    *od = 0;
    for (i = 0; i < nr; i++) {
        int cnt = (i == skip) ? skipExp : mult[i];
        for (e = 0; e < cnt; e++) {
            lin[0] = num_negv(roots[i]);
            lin[1] = num_int(1);
            pmul(out, *od, lin, 1, fac, &d);
            pcopy(out, fac, d);
            *od = d;
        }
    }
    if (qd >= 0) {
        pmul(out, *od, q, qd, fac, &d);
        pcopy(out, fac, d);
        *od = d;
    }
}

/* Lineares Gleichungssystem exakt loesen (Gauss-Jordan) */
static int psolve(int n, Num m[][ILN + 1], Num *x)
{
    int i, j, k, piv;
    Num t, f;
    for (k = 0; k < n; k++) {
        piv = -1;
        for (i = k; i < n; i++) if (!num_zero(m[i][k])) { piv = i; break; }
        if (piv < 0) return 0;
        if (piv != k)
            for (j = 0; j <= n; j++) { t = m[k][j]; m[k][j] = m[piv][j]; m[piv][j] = t; }
        for (i = 0; i < n; i++) {
            if (i == k || num_zero(m[i][k])) continue;
            f = num_div(m[i][k], m[k][k]);
            for (j = k; j <= n; j++) m[i][j] = num_sub(m[i][j], num_mul(f, m[k][j]));
        }
    }
    for (i = 0; i < n; i++) {
        if (num_zero(m[i][i])) return 0;
        x[i] = num_div(m[i][n], m[i][i]);
    }
    return 1;
}

/* Ausdruck als Zaehler und Nenner in s */
static int to_ratio(int n, char v, int *nu, int *de)
{
    int f[MAXF], nf = 0, i, a, b;
    long k;
    if (n == NIL) return 0;
    if (!hasvar(n, v)) { *nu = n; *de = mknum(num_int(1)); return 1; }
    switch (pool[n].op) {
    case OP_VAR:
    case OP_ADD:                        /* Summe steht im Zaehler */
        *nu = n;
        *de = mknum(num_int(1));
        return 1;
    case OP_MUL:
        flatrec(n, OP_MUL, f, &nf, MAXF);
        *nu = mknum(num_int(1));
        *de = mknum(num_int(1));
        for (i = 0; i < nf; i++) {
            if (!to_ratio(f[i], v, &a, &b)) return 0;
            *nu = mkmul(*nu, a);
            *de = mkmul(*de, b);
            if (*nu == NIL || *de == NIL) return 0;
        }
        return 1;
    case OP_POW:
        if (!isnum(pool[n].b) || !num_isint(numof(pool[n].b), &k)) return 0;
        if (k >= 0) { *nu = n; *de = mknum(num_int(1)); return 1; }
        *nu = mknum(num_int(1));
        *de = mkpow(pool[n].a, mknum(num_int(-k)));
        return *de != NIL;
    }
    return 0;
}

/* Ruecktransformation eines Summanden c(s)/d(s) */
static int il_term(int term, char sv, char tv, int *acc)
{
    Num num[MAXDEG + 1], den[MAXDEG + 1], roots[ILN], qq[3];
    Num m[ILN][ILN + 1], x[ILN];
    int mult[ILN], nr = 0, dn, dd, i, k, u, nunk, irr = 0;
    int nu, de;
    Num root, A;

    if (!to_ratio(term, sv, &nu, &de)) { seterr("ilaplace: form not supported."); return NIL; }
    nu = simp(expand(nu));
    de = simp(expand(de));
    if (nu == NIL || de == NIL) return NIL;
    if (!getpoly(nu, sv, num, &dn) || !getpoly(de, sv, den, &dd)) {
        seterr("ilaplace: coefficients must be numeric.");
        return NIL;
    }
    ptrim(num, &dn);
    ptrim(den, &dd);
    if (dn == 0 && num_zero(num[0])) { *acc = mknum(num_int(0)); return *acc; }
    if (dd == 0 && num_zero(den[0])) { seterr("ilaplace: denominator is zero."); return NIL; }

    /* Polynomanteil abspalten: ergibt Delta-Impulse */
    {
        Num quot[MAXDEG + 1];
        int dq = -1;
        for (i = 0; i <= MAXDEG; i++) quot[i] = num_int(0);
        while (dn >= dd) {
            Num f = num_div(num[dn], den[dd]);
            int sh = dn - dd, j;
            if (num_zero(f)) break;
            quot[sh] = f;
            if (dq < sh) dq = sh;
            for (j = 0; j <= dd; j++)
                num[sh + j] = num_sub(num[sh + j], num_mul(f, den[j]));
            while (dn >= 0 && num_zero(num[dn])) dn--;
        }
        *acc = mknum(num_int(0));
        for (k = 0; k <= dq; k++) {
            int d;
            if (num_zero(quot[k])) continue;
            if (k > 2) { seterr("ilaplace: delta derivative too high."); return NIL; }
            d = mkfun(k == 0 ? F_DELTA : (k == 1 ? F_DDELTA : F_DDELTA2), mkvar(tv));
            *acc = simp(mkadd(*acc, mkmul(mknum(quot[k]), d)));
            if (*acc == NIL) return NIL;
        }
        if (dn < 0) return *acc;
    }

    while (dd > 0) {                     /* Nenner in Linearfaktoren zerlegen */
        if (!find_rat_root(den, dd, &root)) break;
        for (i = 0; i < nr; i++) if (num_cmp(roots[i], root) == 0) break;
        if (i == nr) { roots[nr] = root; mult[nr] = 1; nr++; }
        else mult[i]++;
        deflate(den, dd, root);
        dd--;
        ptrim(den, &dd);
    }
    if (dd == 2) { irr = 1; qq[0] = den[0]; qq[1] = den[1]; qq[2] = den[2]; }
    else if (dd != 0) { seterr("ilaplace: denominator does not factor."); return NIL; }

    nunk = irr ? 2 : 0;
    for (i = 0; i < nr; i++) nunk += mult[i];
    if (nunk < 1 || nunk > ILN) { seterr("ilaplace: denominator too large."); return NIL; }

    for (i = 0; i < ILN; i++)
        for (k = 0; k <= ILN; k++) m[i][k] = num_int(0);
    u = 0;
    for (i = 0; i < nr; i++)
        for (k = 1; k <= mult[i]; k++) {
            Num basis[MAXDEG + 1];
            int db, j;
            basis_poly(roots, mult, nr, i, mult[i] - k, qq, irr ? 2 : -1, basis, &db);
            for (j = 0; j <= db; j++) m[j][u] = basis[j];
            u++;
        }
    if (irr) {                           /* quadratischer Rest: B0 + B1*s */
        Num basis[MAXDEG + 1];
        int db, j;
        basis_poly(roots, mult, nr, -1, 0, qq, -1, basis, &db);
        for (j = 0; j <= db; j++) m[j][u] = basis[j];
        for (j = 0; j <= db; j++) m[j + 1][u + 1] = basis[j];
    }
    for (i = 0; i < nunk; i++) m[i][nunk] = num[i];

    if (!psolve(nunk, m, x)) { seterr("ilaplace: no partial fractions found."); return NIL; }

    u = 0;
    for (i = 0; i < nr; i++)
        for (k = 1; k <= mult[i]; k++) {
            long fct = 1, e2;
            Num co;
            A = x[u++];
            if (num_zero(A)) continue;
            for (e2 = 2; e2 <= k - 1; e2++) fct *= e2;
            co = num_div(A, num_int(fct));
            *acc = simp(mkadd(*acc, simp(mkmul(mknum(co),
                mkmul(mkpow(mkvar(tv), mknum(num_int(k - 1))),
                      mkfun(F_EXP, mkmul(mknum(roots[i]), mkvar(tv))))))));
            if (*acc == NIL) return NIL;
        }
    if (irr) {
        Num B0 = x[u], B1 = x[u + 1];
        Num p = num_div(qq[1], qq[2]);
        Num q0 = num_div(qq[0], qq[2]);
        Num h = num_div(p, num_int(2));
        Num w2 = num_sub(q0, num_mul(h, h));
        Num c0 = num_sub(B0, num_mul(B1, h));
        int ex = mkfun(F_EXP, mkmul(mknum(num_negv(h)), mkvar(tv)));
        int X = NIL, Y = NIL;
        if (num_zero(w2)) {                          /* (s+h)^2 */
            X = ex;
            Y = simp(mkmul(mkvar(tv), ex));
        } else if (!num_neg(w2)) {                   /* (s+h)^2 + w^2 */
            int w = sqrt_num_node(w2);
            int arg = mkmul(w, mkvar(tv));
            X = simp(mkmul(ex, mkfun(F_COS, arg)));
            Y = simp(mkmul(ex, mkmul(mkfun(F_SIN, arg), mkpow(w, mknum(num_int(-1))))));
        } else {                                     /* (s+h)^2 - u^2, u irrational */
            int uu = sqrt_num_node(num_negv(w2));
            int arg = mkmul(uu, mkvar(tv));
            int e1 = mkfun(F_EXP, arg);
            int e2 = mkfun(F_EXP, mkmul(mknum(num_int(-1)), arg));
            X = simp(mkmul(ex, mkmul(mknum(num_rat(1, 2)), mkadd(e1, e2))));
            Y = simp(mkmul(ex, mkmul(mknum(num_rat(1, 2)),
                     mkadd(e1, mkmul(mknum(num_int(-1)), e2)))));
            Y = simp(mkmul(Y, mkpow(uu, mknum(num_int(-1)))));
        }
        if (X == NIL || Y == NIL) return NIL;
        if (!num_zero(B1))
            *acc = simp(mkadd(*acc, simp(mkmul(mknum(B1), X))));
        if (!num_zero(c0))
            *acc = simp(mkadd(*acc, simp(mkmul(mknum(c0), Y))));
        if (*acc == NIL) return NIL;
    }
    return *acc;
}

/* Faktor exp(-a*s) abtrennen (Zeitverschiebung); *sh = Summe der a. */
static int il_strip_shift(int term, char sv, int *sh, int *rest)
{
    int f[MAXF], nf = 0, i, keep = NIL;
    *sh = mknum(num_int(0));
    *rest = term;
    if (term == NIL) return 0;
    if (pool[term].op != OP_MUL) return 1;
    flatrec(term, OP_MUL, f, &nf, MAXF);
    for (i = 0; i < nf; i++) {
        int g = f[i];
        if (pool[g].op == OP_FUN && pool[g].fn == F_EXP) {
            Num c[MAXDEG + 1];
            int deg;
            if (getpoly(pool[g].a, sv, c, &deg) && deg == 1 &&
                num_zero(c[0]) && num_neg(c[1])) {
                *sh = mkadd(*sh, mknum(num_negv(c[1])));
                continue;
            }
        }
        keep = (keep == NIL) ? g : mkmul(keep, g);
        if (keep == NIL) return 0;
    }
    *rest = (keep == NIL) ? mknum(num_int(1)) : keep;
    return 1;
}

static int ilaplace_cmd(int node, char sv, char tv, char *out, int outsz)
{
    int f[MAXF], nf = 0, i, acc, t, r;
    node = simp(expand(node));
    if (!cas_ok(node)) {
        setout(out, outsz, cas_err[0] ? cas_err : "Syntax error.");
        return -1;
    }
    if (pool[node].op == OP_ADD) flatrec(node, OP_ADD, f, &nf, MAXF);
    else { f[0] = node; nf = 1; }
    acc = mknum(num_int(0));
    for (i = 0; i < nf; i++) {
        int sh, rest, arg;
        if (!il_strip_shift(f[i], sv, &sh, &rest)) {
            setout(out, outsz, "Cannot transform that.");
            return -1;
        }
        r = il_term(rest, sv, tv, &t);
        if (r == NIL) {
            setout(out, outsz, cas_err[0] ? cas_err : "Cannot transform that.");
            return -1;
        }
        if (!iszero(sh)) {                      /* e^(-a*s) -> f(t-a)*step(t-a) */
            arg = mkadd(mkvar(tv), mkmul(mknum(num_int(-1)), sh));
            t = simp(mkmul(subst(t, tv, arg), mkfun(F_STEP, arg)));
            if (t == NIL) {
                setout(out, outsz, cas_err[0] ? cas_err : "Expression too complex.");
                return -1;
            }
        }
        acc = simp(mkadd(acc, t));
        if (!cas_ok(acc)) {
            setout(out, outsz, cas_err[0] ? cas_err : "Expression too complex.");
            return -1;
        }
    }
    acc = simp(expand(acc));
    if (!cas_ok(acc)) {
        setout(out, outsz, cas_err[0] ? cas_err : "Expression too complex.");
        return -1;
    }
    render_to(acc, out, outsz);
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

/* Matrixliteral [1,2;3,4] */
static int parse_mat(P *p)
{
    int el[MAXME], n = 0, c = 0, rows = 0, cols = 0;
    p->s++;                                    /* '[' */
    for (;;) {
        int e2;
        pskip(p);
        if (*p->s == ']') { seterr("Empty matrix."); return NIL; }
        e2 = parse_rel(p);
        if (e2 == NIL) return NIL;
        if (pool[e2].op == OP_MAT) {
            seterr("Nested matrices are not supported.");
            return NIL;
        }
        if (n >= MAXME) { seterr("Matrix too large."); return NIL; }
        el[n++] = e2;
        c++;
        pskip(p);
        if (*p->s == ',') { p->s++; continue; }
        if (*p->s == ';') {
            p->s++;
            if (rows == 0) cols = c;
            else if (c != cols) { seterr("Rows must have the same length."); return NIL; }
            rows++; c = 0;
            continue;
        }
        if (*p->s == ']') { p->s++; break; }
        seterr("']' missing.");
        return NIL;
    }
    if (rows == 0) cols = c;
    else if (c != cols) { seterr("Rows must have the same length."); return NIL; }
    rows++;
    return mkmat(rows, cols, el);
}

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
    if (*p->s == '[') return parse_mat(p);
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
        if (tolower((unsigned char)*p->s) == 'i' && !isalnum((unsigned char)p->s[1])) {
            p->s++;
            return mkconst(K_I);
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
    long k;
    a = parse_prim(p);
    if (a == NIL) return NIL;
    for (;;) {                              /* 5! */
        pskip(p);
        if (*p->s != '!') break;
        p->s++;
        if (!isnum(a) || !num_isint(numof(a), &k)) {
            seterr("! needs a whole number.");
            return NIL;
        }
        a = mkfact((int)k);
        if (a == NIL) return NIL;
    }
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
        } else if (isdigit((unsigned char)c) || c == '.' || c == '(' || c == '[' ||
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
        if (*s == '(' || *s == '[') d++;
        if (*s == ')' || *s == ']') d--;
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

/* Steht i als eigene Einheit im Text? (pi, sin usw. zaehlen nicht) */
static int has_i_unit(const char *s)
{
    const char *p;
    for (p = s; *p; p++)
        if (tolower((unsigned char)*p) == 'i') {
            int prev_alpha = p > s && isalpha((unsigned char)p[-1]);
            if (!prev_alpha && !isalnum((unsigned char)p[1])) return 1;
        }
    return 0;
}

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
    int cmd = 0, nargs, i, n, node, node2, deg;
    char var, tmp[220];
    double dv;
    Num c[MAXDEG + 1];
    long iv, ord;

    cas_ans = ansv;
    cas_err[0] = 0;
    if (!in || outsz < 2) return 0;
    n = (int)strlen(in);
    if (n == 0 || n > 200) return 0;
    strcpy(work, in);
    trim(work);
    lp = strchr(work, '(');
    if (lp) {
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
        else if (!strcmp(name, "rref")) cmd = 10;
        else if (!strcmp(name, "rank")) cmd = 11;
        else if (!strcmp(name, "lsolve")) cmd = 12;
        else if (!strcmp(name, "charpoly")) cmd = 13;
        else if (!strcmp(name, "taylor") || !strcmp(name, "series")) cmd = 14;
        else if (!strcmp(name, "limit")) cmd = 15;
        else if (!strcmp(name, "nsolve")) cmd = 16;
        else if (!strcmp(name, "zeros")) cmd = 17;
        else if (!strcmp(name, "deg")) cmd = 18;
        else if (!strcmp(name, "coeff") || !strcmp(name, "coef")) cmd = 19;
        else if (!strcmp(name, "gcd")) cmd = 20;
        else if (!strcmp(name, "lcm")) cmd = 21;
        else if (!strcmp(name, "mod")) cmd = 22;
        else if (!strcmp(name, "ncr")) cmd = 23;
        else if (!strcmp(name, "npr")) cmd = 24;
        else if (!strcmp(name, "fac")) cmd = 25;
        else if (!strcmp(name, "sum")) cmd = 26;
        else if (!strcmp(name, "prod")) cmd = 27;
        else if (!strcmp(name, "dot")) cmd = 28;
        else if (!strcmp(name, "cross")) cmd = 29;
        else if (!strcmp(name, "ilaplace") || !strcmp(name, "invlaplace")) cmd = 30;
        else if (!strcmp(name, "laplace")) cmd = 31;
        else if (!strcmp(name, "eigenvals") || !strcmp(name, "eigvals")) cmd = 32;
        else if (!strcmp(name, "eigenvect") || !strcmp(name, "eigvec")) cmd = 33;
    }
    if (!cmd) {                  /* Matrixliterale, det/inv/trans und i direkt rechnen */
        if (strchr(work, '[') || strchr(work, '!') || has_i_unit(work)) cmd = 9;
        else return 0;
    }
    if (cmd == 9) {
        ntop = 0;
        nmats = 0;
        cas_err[0] = 0;
        node = parse_full(work, 0);
        if (cas_ok(node)) node = simp(node);
        if (!cas_ok(node)) {
            setout(out, outsz, cas_err[0] ? cas_err : "Syntax error.");
            return -1;
        }
        render_to(node, out, outsz);
        return 1;
    }

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
    nmats = 0;
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
        if (nargs == 2 && args[0][0] == '[') {    /* System: [gl1;gl2],[x,y] */
            if (args[1][0] != '[') {
                setout(out, outsz, "solve: variables as [x,y].");
                return -1;
            }
            node = parse_full(args[0], 1);
            if (!cas_ok(node)) break;
            node2 = parse_full(args[1], 0);
            if (!cas_ok(node2)) break;
            return solve_system(node, node2, out, outsz);
        }
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
        if (pool[node].op == OP_MAT) {
            if (!approx_mat(node, out, outsz)) {
                setout(out, outsz, cas_err[0] ? cas_err : "Contains variables.");
                return -1;
            }
            return 1;
        }
        if (evaln(node, 0, 0.0, &dv)) {
            if (fabs(dv) < 1e-12) dv = 0.0;
            sprintf(tmp, "%.10g", dv);
            setout(out, outsz, tmp);
            return 1;
        }
        {
            Cplx z;
            if (evalc(node, &z)) {
                fmt_cplx(tmp, z.re, z.im);
                setout(out, outsz, tmp);
                return 1;
            }
        }
        setout(out, outsz, cas_err[0] ? cas_err : "Contains variables.");
        return -1;
    case 10:                                    /* rref(A) */
    case 11:                                    /* rank(A) */
        if (nargs != 1) { setout(out, outsz, "rref/rank need a matrix."); return -1; }
        node = parse_full(args[0], 0);
        if (!cas_ok(node)) break;
        node = simp(node);
        if (!cas_ok(node)) break;
        if (pool[node].op != OP_MAT) { setout(out, outsz, "rref/rank need a matrix."); return -1; }
        node = (cmd == 10) ? mat_rref(node) : mat_rank(node);
        if (!cas_ok(node)) break;
        render_to(node, out, outsz);
        return 1;
    case 12:                                    /* lsolve(A,b) */
        if (nargs != 2) { setout(out, outsz, "lsolve needs matrix and vector."); return -1; }
        node = parse_full(args[0], 0);
        if (!cas_ok(node)) break;
        node2 = parse_full(args[1], 0);
        if (!cas_ok(node2)) break;
        node = simp(node);
        node2 = simp(node2);
        if (!cas_ok(node) || !cas_ok(node2)) break;
        if (pool[node].op != OP_MAT || pool[node2].op != OP_MAT) {
            setout(out, outsz, "lsolve needs matrix and vector.");
            return -1;
        }
        node = mat_lsolve(node, node2);
        if (!cas_ok(node)) break;
        render_to(node, out, outsz);
        return 1;
    case 13:                                    /* charpoly(A[,x]) */
        if (nargs < 1 || nargs > 2) { setout(out, outsz, "charpoly needs matrix[,x]."); return -1; }
        if (!argvar(args, 1, nargs, &var)) { setout(out, outsz, "Variable must be one letter."); return -1; }
        node = parse_full(args[0], 0);
        if (!cas_ok(node)) break;
        node = simp(node);
        if (!cas_ok(node)) break;
        if (pool[node].op != OP_MAT) { setout(out, outsz, "charpoly needs a matrix."); return -1; }
        node = mat_charpoly(node, var);
        if (!cas_ok(node)) break;
        render_to(node, out, outsz);
        return 1;
    case 14:                                    /* taylor(expr,x,a,n) */
        if (nargs != 4) { setout(out, outsz, "taylor needs expr,x,a,n."); return -1; }
        if (!argvar(args, 1, nargs, &var)) { setout(out, outsz, "Variable must be one letter."); return -1; }
        node = parse_full(args[0], 0);
        if (!cas_ok(node)) break;
        node2 = parse_full(args[2], 0);
        if (!cas_ok(node2)) break;
        {
            int on = parse_full(args[3], 0);
            if (!int_of(on, &iv)) { setout(out, outsz, "Order must be a whole number."); return -1; }
        }
        return taylor_cmd(node, var, node2, iv, out, outsz);
    case 15:                                    /* limit(expr,x,a) */
        if (nargs != 3) { setout(out, outsz, "limit needs expr,x,a."); return -1; }
        if (!argvar(args, 1, nargs, &var)) { setout(out, outsz, "Variable must be one letter."); return -1; }
        node = parse_full(args[0], 0);
        if (!cas_ok(node)) break;
        node2 = parse_full(args[2], 0);
        if (!cas_ok(node2)) break;
        return limit_cmd(node, var, node2, out, outsz);
    case 16:                                    /* nsolve(eq,x,start) */
        if (nargs != 3) { setout(out, outsz, "nsolve needs equation,x,start."); return -1; }
        if (!argvar(args, 1, nargs, &var)) { setout(out, outsz, "Variable must be one letter."); return -1; }
        node = parse_full(args[0], 1);
        if (!cas_ok(node)) break;
        node2 = parse_full(args[2], 0);
        if (!cas_ok(node2)) break;
        if (!evaln(node2, 0, 0.0, &dv)) { setout(out, outsz, "Start value must be a number."); return -1; }
        return nsolve_cmd(node, var, dv, out, outsz);
    case 17:                                    /* zeros(expr[,x]) */
        if (nargs < 1 || nargs > 2) { setout(out, outsz, "zeros needs expr[,x]."); return -1; }
        if (!argvar(args, 1, nargs, &var)) { setout(out, outsz, "Variable must be one letter."); return -1; }
        node = parse_full(args[0], 1);
        if (!cas_ok(node)) break;
        return solve_cmd(node, var, out, outsz);
    case 18:                                    /* deg(expr[,x]) */
        if (nargs < 1 || nargs > 2) { setout(out, outsz, "deg needs expr[,x]."); return -1; }
        if (!argvar(args, 1, nargs, &var)) { setout(out, outsz, "Variable must be one letter."); return -1; }
        node = parse_full(args[0], 0);
        if (!cas_ok(node)) break;
        node = simp(expand(node));
        if (!cas_ok(node)) break;
        if (!getpoly(node, var, c, &deg)) {
            setout(out, outsz, "Not a polynomial in that variable.");
            return -1;
        }
        poly_trim(c, &deg);
        sprintf(tmp, "%d", deg);
        setout(out, outsz, tmp);
        return 1;
    case 19:                                    /* coeff(expr,x,n) */
        if (nargs != 3) { setout(out, outsz, "coeff needs expr,x,n."); return -1; }
        if (!argvar(args, 1, nargs, &var)) { setout(out, outsz, "Variable must be one letter."); return -1; }
        node = parse_full(args[0], 0);
        if (!cas_ok(node)) break;
        node2 = parse_full(args[2], 0);
        if (!int_of(node2, &iv) || iv < 0 || iv > MAXDEG) {
            setout(out, outsz, "Coefficient index 0..8.");
            return -1;
        }
        node = simp(expand(node));
        if (!cas_ok(node)) break;
        if (!getpoly(node, var, c, &deg)) {
            setout(out, outsz, "Not a polynomial in that variable.");
            return -1;
        }
        node = mknum(c[iv]);
        if (node == NIL) break;
        render_to(node, out, outsz);
        return 1;
    case 20: case 21: case 22: case 23: case 24:   /* gcd lcm mod ncr npr */
        if (nargs != 2) { setout(out, outsz, "This command needs two whole numbers."); return -1; }
        node = parse_full(args[0], 0);
        node2 = parse_full(args[1], 0);
        if (!int_of(node, &iv) || !int_of(node2, &ord)) {
            setout(out, outsz, "This command needs two whole numbers.");
            return -1;
        }
        if (cmd == 20) node = mk_gcd(iv, ord);
        else if (cmd == 21) node = mk_lcm(iv, ord);
        else if (cmd == 22) node = mk_mod(iv, ord);
        else if (cmd == 23) node = mk_ncr(iv, ord);
        else node = mk_npr(iv, ord);
        if (!cas_ok(node)) break;
        render_to(node, out, outsz);
        return 1;
    case 25:                                    /* fac(n) */
        if (nargs != 1) { setout(out, outsz, "fac needs one whole number."); return -1; }
        node = parse_full(args[0], 0);
        if (!int_of(node, &iv)) { setout(out, outsz, "fac needs a whole number."); return -1; }
        node = mkfact((int)iv);
        if (!cas_ok(node)) break;
        render_to(node, out, outsz);
        return 1;
    case 26: case 27:                           /* sum/prod(expr,x,a,b) */
        if (nargs != 4) { setout(out, outsz, "sum/prod needs expr,x,a,b."); return -1; }
        if (!argvar(args, 1, nargs, &var)) { setout(out, outsz, "Variable must be one letter."); return -1; }
        node = parse_full(args[0], 0);
        if (!cas_ok(node)) break;
        node2 = parse_full(args[2], 0);
        if (!int_of(node2, &iv)) { setout(out, outsz, "Lower bound must be a whole number."); return -1; }
        {
            int hi = parse_full(args[3], 0);
            if (!int_of(hi, &ord)) { setout(out, outsz, "Upper bound must be a whole number."); return -1; }
        }
        node = simp(expand(node));
        if (!cas_ok(node)) break;
        return sumprod(node, var, iv, ord, cmd == 27, out, outsz);
    case 28: case 29:                           /* dot/cross */
        if (nargs != 2) { setout(out, outsz, "dot/cross needs two vectors."); return -1; }
        node = parse_full(args[0], 0);
        if (!cas_ok(node)) break;
        node2 = parse_full(args[1], 0);
        if (!cas_ok(node2)) break;
        node = simp(node);
        node2 = simp(node2);
        if (!cas_ok(node) || !cas_ok(node2)) break;
        if (pool[node].op != OP_MAT || pool[node2].op != OP_MAT) {
            setout(out, outsz, "dot/cross needs two vectors.");
            return -1;
        }
        node = (cmd == 28) ? mat_dot(node, node2) : mat_cross(node, node2);
        if (!cas_ok(node)) break;
        render_to(node, out, outsz);
        return 1;
    case 30:                                    /* ilaplace(expr[,s[,t]]) */
        if (nargs < 1 || nargs > 3) { setout(out, outsz, "ilaplace needs expr[,s[,t]]."); return -1; }
        {
            char sv2 = 's', tv2 = 't';
            if (nargs >= 2 && !argvar(args, 1, nargs, &sv2)) {
                setout(out, outsz, "Variable must be one letter.");
                return -1;
            }
            if (nargs >= 3 && !argvar(args, 2, nargs, &tv2)) {
                setout(out, outsz, "Variable must be one letter.");
                return -1;
            }
            node = parse_full(args[0], 0);
            if (!cas_ok(node)) break;
            return ilaplace_cmd(node, sv2, tv2, out, outsz);
        }
    case 31:                                    /* laplace(expr[,t[,s]]) */
        if (nargs < 1 || nargs > 3) { setout(out, outsz, "laplace needs expr[,t[,s]]."); return -1; }
        {
            char tv2 = 't', sv2 = 's';
            if (nargs >= 2 && !argvar(args, 1, nargs, &tv2)) {
                setout(out, outsz, "Variable must be one letter.");
                return -1;
            }
            if (nargs >= 3 && !argvar(args, 2, nargs, &sv2)) {
                setout(out, outsz, "Variable must be one letter.");
                return -1;
            }
            node = parse_full(args[0], 0);
            if (!cas_ok(node)) break;
            return laplace_cmd(node, tv2, sv2, out, outsz);
        }
    case 32:                                    /* eigenvals(A[,x]) */
    case 33:                                    /* eigenvect(A[,x]) */
        if (nargs < 1 || nargs > 2) { setout(out, outsz, "eigenvals/eigenvect needs a matrix[,x]."); return -1; }
        if (!argvar(args, 1, nargs, &var)) { setout(out, outsz, "Variable must be one letter."); return -1; }
        node = parse_full(args[0], 0);
        if (!cas_ok(node)) break;
        node = simp(node);
        if (!cas_ok(node)) break;
        if (pool[node].op != OP_MAT) {
            setout(out, outsz, "eigenvals/eigenvect needs a matrix.");
            return -1;
        }
        return (cmd == 32) ? eigenvals_cmd(node, var, out, outsz)
                           : eigenvect_cmd(node, var, out, outsz);
    }

    if (cas_err[0]) setout(out, outsz, cas_err);
    else setout(out, outsz, "Syntax error.");
    return -1;
}
