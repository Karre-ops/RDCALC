#include <ctype.h>
#include <string.h>
#include <conio.h>
#include <dos.h>
#include <i86.h>
#include "gfx.h"
#include "mouse.h"
#include "rdui.h"

int ui_nowait = 0;
int ui_restored;

int ui_save(UISAVE *s, int x, int y, int w, int h)
{
    unsigned sz;
    s->seg = 0; ui_restored = 0;
    if (ui_nowait) return 0;
    if (y + h > gfx_h) h = gfx_h - y;
    sz = gfx_blocksize(x, w, h);
    if (_dos_allocmem((sz + 15) >> 4, &s->seg)) { s->seg = 0; return 0; }
    s->x = x; s->y = y; s->w = w; s->h = h;
    mouse_hide();
    gfx_getblock(x, y, w, h, (unsigned char far *)MK_FP(s->seg, 0));
    mouse_show();
    return 1;
}

void ui_restore(UISAVE *s)
{
    ui_restored = s->seg != 0;
    if (!s->seg) return;
    mouse_hide();
    gfx_putblock(s->x, s->y, s->w, s->h, (unsigned char far *)MK_FP(s->seg, 0));
    mouse_show();
    _dos_freemem(s->seg);
    s->seg = 0;
}

/* Taste da? Ueber BIOS INT 16h/01h statt DOS (kbhit kostet ueber INT 21h ca. 10x so viel) */
int ui_kbhit(void)
{
    unsigned char z;
    _asm {
        push ax
        push bx
        push cx
        push dx
        push es
        mov  ah, 1
        int  16h
        mov  al, 0
        jz   keine
        mov  al, 1
    keine:
        mov  z, al
        pop  es
        pop  dx
        pop  cx
        pop  bx
        pop  ax
    }
    return z;
}   /* Testmodus: Dialoge nur zeichnen */
int (*ui_keyfn)(void) = 0;
int ui_input_append = 0;     /* 1: Vorgabe nicht markieren, hinten weitertippen */   /* Testmodus: Tasten aus Skript statt Tastatur */

static volatile unsigned long far *ticks =
    (volatile unsigned long far *)MK_FP(0x40, 0x6C);

int ui_in(int mx, int my, int x, int y, int w, int h)
{
    return mx >= x && mx < x + w && my >= y && my < y + h;
}

void ui_wait_release(void)
{
    int x, y, b;
    do mouse_read(&x, &y, &b); while (b & 1);
}

/* ---------- Button ---------- */
void ui_button_draw(int x, int y, int w, const char *label, int def)
{
    int h = UI_BTN_H;
    gfx_fill(x, y, w, h, 1);
    gfx_rect(x, y, w, h, 0);
    if (def) gfx_rect(x + 1, y + 1, w - 2, h - 2, 0);   /* Standard-Button */
    gfx_text(x + (w - gfx_textw(label)) / 2, y + 3, label, 0);
}

int ui_button_track(int x, int y, int w)
{
    int mx, my, b, in, was = 0, h = UI_BTN_H;
    do {
        mouse_read(&mx, &my, &b);
        in = ui_in(mx, my, x, y, w, h);
        if (in != was) {
            mouse_hide(); gfx_xorfill(x + 2, y + 2, w - 4, h - 4); mouse_show();
            was = in;
        }
    } while (b & 1);
    if (was) { mouse_hide(); gfx_xorfill(x + 2, y + 2, w - 4, h - 4); mouse_show(); }
    return was;
}

/* ---------- Liste ---------- */
#define LI_H UI_LI_H

static unsigned long last_t;
static int last_i = -1;

void ui_list_reset(void) { last_i = -1; }

/* Schieber im Scrollbalken: Hoehe *th, Rueckgabe obere Kante */
static int list_thumb(UILIST *l, int *th)
{
    int h = l->rows * LI_H + 2;
    *th = (int)((long)h * l->rows / l->count);
    if (*th < 6) *th = 6;                        /* erst begrenzen, dann Lage rechnen */
    return l->y + 1 + (int)((long)(h - *th) * l->top / (l->count - l->rows));
}

static void list_bar(UILIST *l)
{
    int h = l->rows * LI_H + 4, sbx = l->x + l->w - 12;
    gfx_pattern(sbx + 1, l->y + 1, 10, h - 2);
    if (l->count > l->rows) {
        int th, ty = list_thumb(l, &th);
        gfx_fill(sbx + 1, ty, 10, th, 1);
        gfx_rect(sbx + 1, ty, 10, th, 0);
    }
}

static void list_row(UILIST *l, int i)          /* sichtbare Zeile i neu */
{
    int yy = l->y + 2 + i * LI_H;
    gfx_fill(l->x + 1, yy, l->w - 13, LI_H, 1);
    if (l->top + i < l->count) gfx_text((l->x + 11) & ~7, yy + 1, l->items[l->top + i], 0);
    if (l->top + i == l->sel) gfx_xorfill(l->x + 1, yy, l->w - 14, LI_H);
}

/* Liste stand bei Anfang t, Auswahl o: bei kleinem Sprung Zeilen verschieben, nur neue zeichnen */
static void list_scroll(UILIST *l, int t, int o)
{
    int d = l->top - t, n = l->rows, i, bx = l->x >> 3, nb = ((l->x + l->w - 12) >> 3) - bx;
    int y0 = l->y + 2;
    if (d == 0) return;
    if (d >= n || -d >= n) { ui_list_draw(l); return; }
    mouse_hide();
    if (o >= t && o < t + n) gfx_xorfill(l->x + 1, y0 + (o - t) * LI_H, l->w - 14, LI_H);  /* alte Markierung weg */
    if (d > 0) {
        gfx_moverows(bx, nb, y0, y0 + d * LI_H, (n - d) * LI_H);
        for (i = n - d; i < n; i++) list_row(l, i);
    } else {
        gfx_moverows(bx, nb, y0 - d * LI_H, y0, (n + d) * LI_H);
        for (i = 0; i < -d; i++) list_row(l, i);
    }
    i = l->sel - l->top;                            /* Auswahl in einer verschobenen Zeile */
    if (l->sel >= 0 && i >= 0 && i < n && (d > 0 ? i < n - d : i >= -d))
        gfx_xorfill(l->x + 1, y0 + i * LI_H, l->w - 14, LI_H);
    list_bar(l);
    mouse_show();
}

void ui_list_draw(UILIST *l)
{
    int i, yy, h = l->rows * LI_H + 4;
    int sbx = l->x + l->w - 12;          /* Scrollbalken */
    mouse_hide();
    gfx_fill(l->x, l->y, l->w, h, 1);
    gfx_rect(l->x, l->y, l->w, h, 0);
    gfx_fill(sbx, l->y, 1, h, 0);
    list_bar(l);
    for (i = 0; i < l->rows && l->top + i < l->count; i++) {
        yy = l->y + 2 + i * LI_H;
        gfx_text((l->x + 11) & ~7, yy + 1, l->items[l->top + i], 0);   /* Byte-Grenze = schnell */
        if (l->top + i == l->sel) gfx_xorfill(l->x + 1, yy, l->w - 14, LI_H);
    }
    mouse_show();
}

int ui_list_click(UILIST *l, int mx, int my)
{
    int h = l->rows * LI_H + 4, i, sbx = l->x + l->w - 12;

    if (!ui_in(mx, my, l->x, l->y, l->w, h)) return 0;
    if (mx >= sbx) {                      /* Scrollbalken: ueber/unter dem Schieber blaettern */
        int th, ty;
        if (l->count <= l->rows) return 0;
        ty = list_thumb(l, &th);
        i = l->top;
        if (my < ty) l->top -= l->rows - 1; else if (my >= ty + th) l->top += l->rows - 1;
        if (l->top > l->count - l->rows) l->top = l->count - l->rows;
        if (l->top < 0) l->top = 0;
        list_scroll(l, i, l->sel);
        ui_wait_release();
        return 0;
    }
    i = l->top + (my - l->y - 2) / LI_H;
    if (i < 0 || i >= l->count) return 0;
    ui_wait_release();
    if (i == last_i && *ticks - last_t < 12) {  /* ca. 0,65 s nach dem ersten Klick */
        last_i = -1;
        return 2;
    }
    ui_list_select(l, i);
    last_i = i; last_t = *ticks;                 /* erst nach dem Zeichnen: langsamer XT */
    return 1;
}

/* Auswahl setzen; ohne Blaettern nur zwei Zeilen umfaerben (schnell auf dem XT) */
void ui_list_select(UILIST *l, int s)
{
    int o = l->sel, t = l->top;
    l->sel = s;
    if (s < t) l->top = s;
    if (s >= t + l->rows) l->top = s - l->rows + 1;
    if (l->top != t) { list_scroll(l, t, o); return; }
    if (o == s) return;
    mouse_hide();
    if (o >= t && o < t + l->rows) gfx_xorfill(l->x + 1, l->y + 2 + (o - t) * LI_H, l->w - 14, LI_H);
    if (s >= 0) gfx_xorfill(l->x + 1, l->y + 2 + (s - t) * LI_H, l->w - 14, LI_H);
    mouse_show();
}

/* Tastatur in Listen. c = Zeichen oder 0x100 + Scancode (Pfeile, Bild, Pos1, Ende).
 * Buchstaben/Ziffern springen zum naechsten Eintrag mit diesem Anfang. 1 = Auswahl neu */
int ui_list_key(UILIST *l, int c)
{
    int s = l->sel, n = l->count, i, k;
    if (!n) return 0;
    switch (c) {
    case 0x148: s--; break;
    case 0x150: s++; break;
    case 0x149: s -= l->rows - 1; break;
    case 0x151: s += l->rows - 1; break;
    case 0x147: s = 0; break;
    case 0x14F: s = n - 1; break;
    default:
        if (c > 255 || !isalnum(c)) return 0;
        for (i = 1; i <= n; i++) {
            k = (s + i + n) % n;
            if (s < 0) k = i - 1;
            if (toupper(l->items[k][l->items[k][0] == '[']) == toupper(c)) { s = k; break; }
        }
        if (i > n) return 0;
    }
    if (l->sel < 0 && c >= 0x100) s = 0;
    if (s < 0) s = 0;
    if (s >= n) s = n - 1;
    ui_list_select(l, s);
    return 1;
}

/* ---------- Texteingabe ---------- */
int ui_input_mask;                      /* 1 = Sternchen statt Text (Passwort) */
int ui_input(int x, int y, int w, char *buf, int max)
{
    int len = (int)strlen(buf), c, h = gfx_fh + 4, vis = (w - 8) / 8, off;
    int fresh = len > 0 && !ui_input_append;   /* Vorgabe markiert: erste Taste ersetzt sie */
    for (;;) {
        off = len > vis - 1 ? len - (vis - 1) : 0;
        mouse_hide();
        gfx_fill(x, y, w, h, 1);
        gfx_rect(x, y, w, h, 0);
        if (ui_input_mask) { int i; for (i = 0; i < len - off; i++) gfx_text(x + 4 + i * 8, y + 2, "*", 0); }
        else gfx_text(x + 4, y + 2, buf + off, 0);
        if (fresh) gfx_xorfill(x + 3, y + 1, (len - off) * 8 + 2, gfx_fh + 2);
        else gfx_fill(x + 4 + (len - off) * 8, y + 2, 2, gfx_fh, 0);  /* Cursor */
        mouse_show();
        if (ui_keyfn) { c = ui_keyfn(); if (c < 0) return 0; }
        else { while (!kbhit()) KB_IDLE(); c = getch(); if (c == 0) { getch(); fresh = 0; continue; } }
        if (c == 0 || c > 255) { fresh = 0; continue; }
        if (c == 13) return 1;
        if (c == 27) return 0;
        if (c == 8) { if (fresh) len = 0, buf[0] = 0; else if (len) buf[--len] = 0; }
        else if (c >= 32 && c != 127 && c < 256) {
            if (fresh) { len = 0; buf[0] = 0; }
            if (len < max - 1) { buf[len++] = (char)c; buf[len] = 0; }
        }
        fresh = 0;
    }
}

/* ---------- Dialog ---------- */
/* Hinweis ohne Knopf, bleibt stehen, waehrend DOS das naechste Programm laedt */
void ui_busy(const char *title, const char *s)
{
    const char *l[2];
    l[0] = s; l[1] = "Please wait ...";
    ui_nowait++;
    ui_msgbox(title, l, 2, NULL, NULL);
    ui_nowait--;
    mouse_hide();
    gfx_keep = 1;
}

/* Zeilen, die mit "[ ]" oder "[X]" beginnen, sind Haekchen: Klick oder Leertaste schaltet um,
 * Pfeil rauf/runter waehlt. Der Aufrufer liest das Ergebnis an lines[i][1] ab. */
static int mb_x, mb_y0, mb_w;
static void mb_line(const char **lines, int i, int cur)
{
    int ly = mb_y0 + i * (gfx_fh + 2);
    gfx_fill(mb_x + 12, ly - 1, mb_w - 24, gfx_fh + 2, 1);
    gfx_text(mb_x + 16, ly, lines[i], 0);
    if (i == cur) gfx_xorfill(mb_x + 12, ly - 1, mb_w - 24, gfx_fh + 2);
}
#define IS_CB(i) (lines[i][0] == '[' && lines[i][2] == ']')

int ui_msgbox(const char *title, const char **lines, int n,
              const char *b1, const char *b2)
{
    int i, w = 200, h, x, y, bw = 72, bx1, bx2, by, th = gfx_fh + 3;
    int mx, my, b, c, res = -1, foc = 0, cb = -1, k;
    UISAVE sv;

    for (i = 0; i < n; i++)
        if (gfx_textw(lines[i]) + 32 > w) w = gfx_textw(lines[i]) + 32;
    h = th + 8 + n * (gfx_fh + 2) + 8 + UI_BTN_H + 8;
    x = ((gfx_w - w) / 2) & ~7;
    y = (gfx_h - h) / 2;
    by = y + h - UI_BTN_H - 8;
    if (b2) { bx1 = x + w / 2 - bw - 8; bx2 = x + w / 2 + 8; }
    else    { bx1 = x + (w - bw) / 2;   bx2 = 0; }

    ui_save(&sv, x, y, w + 3, h + 3);                /* mit Schatten */
    mouse_hide();
    gfx_fill(x + 3, y + 3, w, h, 0);
    gfx_fill(x, y, w, h, 1);
    gfx_rect(x, y, w, h, 0);
    gfx_rect(x + 2, y + 2, w - 4, h - 4, 0);
    gfx_fill(x + 2, y + 2, w - 4, th, 0);
    gfx_text(x + (((w - gfx_textw(title)) / 2) & ~7), y + 3, title, 1);
    mb_x = x; mb_y0 = y + th + 8; mb_w = w;
    for (i = n - 1; i >= 0; i--) if (IS_CB(i)) cb = i;
    for (i = 0; i < n; i++) mb_line(lines, i, cb);
    if (b1) ui_button_draw(bx1, by, bw, b1, 1);       /* ohne Knopf: nur Hinweis */
    if (b2) ui_button_draw(bx2, by, bw, b2, 0);
    mouse_show();
    if (ui_nowait) return -1;

    while (res < 0) {
        if (!kbhit()) KB_IDLE();
        if (kbhit()) {
            c = getch();
            if (c == 0) {
                k = getch();
                c = k == 0x0F ? 9 : 0x4B;                    /* Pfeile/Shift+Tab: wechseln */
                if (cb >= 0 && (k == 0x48 || k == 0x50)) {   /* rauf/runter: anderes Haekchen */
                    i = cb;
                    do i = (i + (k == 0x50 ? 1 : n - 1)) % n; while (!IS_CB(i));
                    c = cb; cb = i;
                    mouse_hide(); mb_line(lines, c, cb); mb_line(lines, cb, cb); mouse_show();
                    c = 0;
                }
            }
            if (c == ' ' && cb >= 0) {
                ((char *)lines[cb])[1] ^= 'X' ^ ' ';
                mouse_hide(); mb_line(lines, cb, cb); mouse_show();
            }
            else if (c == 13) res = foc;
            else if (c == 27) res = b2 ? 1 : 0;
            else if (b2 && (c == 9 || c == 0x4B)) {          /* Tab: anderer Knopf */
                foc = !foc;
                mouse_hide();
                ui_button_draw(bx1, by, bw, b1, !foc);
                ui_button_draw(bx2, by, bw, b2, foc);
                mouse_show();
            }
        }
        mouse_read(&mx, &my, &b);
        if (b & 1) {
            if (ui_in(mx, my, bx1, by, bw, UI_BTN_H) && ui_button_track(bx1, by, bw)) res = 0;
            else if (b2 && ui_in(mx, my, bx2, by, bw, UI_BTN_H) && ui_button_track(bx2, by, bw)) res = 1;
            else {
                i = (my - mb_y0 + 1) / (gfx_fh + 2);
                if (my >= mb_y0 - 1 && i < n && ui_in(mx, my, x + 12, mb_y0 - 1, w - 24, n * (gfx_fh + 2)) && IS_CB(i)) {
                    ((char *)lines[i])[1] ^= 'X' ^ ' ';
                    c = cb; cb = i;
                    mouse_hide(); if (c >= 0 && c != i) mb_line(lines, c, cb); mb_line(lines, i, cb); mouse_show();
                }
                ui_wait_release();
            }
        }
    }
    mouse_pressed(0, &mx, &my); mouse_pressed(1, &mx, &my);   /* Klick nicht nach unten durchreichen */
    ui_restore(&sv);
    return res;
}

/* ---------- Fensterrahmen ---------- */
void ui_frame(int x, int y, int w, int h, const char *title)
{
    int th = UI_TITLE_H, tw, max = (w - 32) / 8;
    char buf[80];
    gfx_fill(x + 3, y + 3, w, h, 0);                 /* Schatten */
    gfx_fill(x, y, w, h, 1);
    gfx_rect(x, y, w, h, 0);
    gfx_fill(x, y, w, th, 0);                         /* Titelleiste */
    gfx_fill(x + 4, y + 2, 10, th - 4, 1);            /* Schliessbox */
    if ((int)strlen(title) > max && max > 3) {        /* zu lang: vorne kuerzen */
        strcpy(buf, "..");
        strcat(buf, title + strlen(title) - (max - 2));
        title = buf;
    }
    tw = gfx_textw(title);
    gfx_text(x + (((w - tw) / 2) & ~7), y + 2, title, 1);
}
