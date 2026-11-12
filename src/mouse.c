#include <dos.h>
#include <i86.h>
#include "mouse.h"

/* Verhindert Absturz, wenn kein Treiber geladen ist (Vektor 0 oder IRET) */
int mouse_present(void)
{
    unsigned char far *v = (unsigned char far *)_dos_getvect(0x33);
    if (v == 0 || *v == 0xCF) return 0;
    return mouse_init();
}

int mouse_init(void)
{
    union REGS r;
    r.x.ax = 0x0000;
    int86(0x33, &r, &r);
    return r.x.ax == 0xFFFF;
}

void mouse_show(void) { union REGS r; r.x.ax = 1; int86(0x33, &r, &r); }
void mouse_hide(void) { union REGS r; r.x.ax = 2; int86(0x33, &r, &r); }

#ifdef MTEST
/* Testbau: Mausereignisse aus MOUSE.SCR abspielen. Zeile "x y Knoepfe n" = Zustand fuer n
 * Abfragen, "K hex" = Taste (Scancode*256+ASCII) in den BIOS-Puffer. Datei wird nach dem
 * Laden geloescht, damit ein Neustart von RDESK sie nicht noch einmal abspielt. */
#include <stdio.h>
static int ev[80][4], nev = -1, cur, cnt, lastb, pc[2], cx, cy;
static void ms_load(void)
{
    FILE *f = fopen("\\MOUSE.SCR", "r");
    char l[40];
    nev = 0;
    if (!f) return;
    while (nev < 80 && fgets(l, sizeof l, f)) {          /* ohne sscanf: spart im Testbau Platz */
        char *p = l + (l[0] == 'K' || l[0] == 'S');
        int k, v, b = p > l ? 16 : 10, nk = p > l ? 1 : 4;
        for (k = 0; k < nk; k++) {
            while (*p == ' ') p++;
            for (v = 0; (*p >= '0' && *p <= '9') || (b == 16 && ((*p | 32) >= 'a' && (*p | 32) <= 'f')); p++)
                v = v * b + (*p <= '9' ? *p - '0' : (*p | 32) - 'a' + 10);
            ev[nev][k] = v;
        }
        if (p > l + 1 || l[0] == 'K' || l[0] == 'S') {
            if (l[0] == 'K') ev[nev][3] = -1; else if (l[0] == 'S') ev[nev][3] = -2;
            nev++;
        }
    }
    fclose(f);
    remove("\\MOUSE.SCR");
}
int ms_done(void) { if (nev < 0) ms_load(); return cur >= nev; }
static int ms_read(int *x, int *y, int *b)
{
    union REGS r;
    if (ms_done()) return 0;
    while (ev[cur][3] < 0) {                    /* Tasten / Shift-Status */
        if (ev[cur][3] == -2) *(unsigned char far *)MK_FP(0x40, 0x17) = (unsigned char)ev[cur][0];
        else { r.h.ah = 5; r.x.cx = ev[cur][0]; int86(0x16, &r, &r); }
        cur++;
        if (ms_done()) { *x = cx; *y = cy; *b = 0; return 1; }
    }
    cx = *x = ev[cur][0]; cy = *y = ev[cur][1]; *b = ev[cur][2];
    if ((*b & 1) && !(lastb & 1)) pc[0]++;
    if ((*b & 2) && !(lastb & 2)) pc[1]++;
    lastb = *b;
    if (++cnt >= ev[cur][3]) { cur++; cnt = 0; }
    return 1;
}
#endif

void mouse_read(int *x, int *y, int *b)
{
    unsigned bb, xx, yy;
#ifdef MTEST
    if (ms_read(x, y, b)) return;
#endif
    _asm {                                      /* ohne int86: wird staendig aufgerufen */
        push ax
        push bx
        push cx
        push dx
        push es
        mov  ax, 3
        int  33h
        mov  bb, bx
        mov  xx, cx
        mov  yy, dx
        pop  es
        pop  dx
        pop  cx
        pop  bx
        pop  ax
    }
    *b = bb; *x = xx; *y = yy;
}

/* Druecke seit der letzten Abfrage (INT 33h/5) und Position des letzten Drucks.
 * Faengt auch Klicks, die waehrend langsamer Bildaufbauten passiert sind. */
int mouse_pressed(int btn, int *x, int *y)
{
    unsigned n, xx, yy;
#ifdef MTEST
    if (nev > 0) { n = pc[btn]; pc[btn] = 0; *x = cx; *y = cy; return n; }
#endif
    _asm {
        push ax
        push bx
        push cx
        push dx
        push es
        mov  ax, 5
        mov  bx, btn
        int  33h
        mov  n, bx
        mov  xx, cx
        mov  yy, dx
        pop  es
        pop  dx
        pop  cx
        pop  bx
        pop  ax
    }
    *x = xx; *y = yy;
    return n;
}

void mouse_range(int w, int h)
{
    union REGS r;
    r.x.ax = 7; r.x.cx = 0; r.x.dx = w - 1; int86(0x33, &r, &r);
    r.x.ax = 8; r.x.cx = 0; r.x.dx = h - 1; int86(0x33, &r, &r);
}

void mouse_moveto(int x, int y)
{
    union REGS r;
    r.x.ax = 4; r.x.cx = x; r.x.dx = y;
    int86(0x33, &r, &r);
}
