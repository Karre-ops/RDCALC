/* RetroDesk - RDCALC: Formel -> Postfix, Postfix rechnen, Zahl ausgeben.
 * In src/RDCALC.C mit #include eingebunden.
 * Braucht vorher string.h, math.h, ctype.h, stdlib.h und stdio.h (sprintf). Texte englisch wie die CAS-Meldungen. */
/* ---------------- Formel -> Postfix ---------------- */
enum { T_NUM, T_X, T_ANS, T_ADD, T_SUB, T_MUL, T_DIV, T_POW, T_NEG,
       T_SIN, T_COS, T_TAN, T_ASIN, T_ACOS, T_ATAN, T_SQRT, T_LN, T_LOG, T_ABS, T_EXP, T_LP };
static const char *fname[] = { "sin", "cos", "tan", "asin", "acos", "atan", "sqrt", "ln", "log", "abs", "exp" };
#define NF 11
#define MAXT 64
typedef struct { unsigned char t; double v; } TOK;
static TOK prog[MAXT];
static int nprog, has_x;
static double ans;
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
