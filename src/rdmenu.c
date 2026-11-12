#include <string.h>
#include <stdlib.h>
#include <conio.h>
#include <malloc.h>
#include "gfx.h"
#include "mouse.h"
#include "rdui.h"
#include "rdmenu.h"

int ui_pollkey(void)
{
    int c;
    if (ui_keyfn) return ui_keyfn();
    if (!kbhit()) return -1;
    c = getch();
    if (c == 0) return 0x100 + getch();
    return c;
}

/* ---------- Hintergrund sichern ---------- */
static unsigned char far *save_bg(int x, int y, int w, int h)
{
    unsigned char far *b = (unsigned char far *)_fmalloc(gfx_blocksize(x, w, h));
    if (b) { mouse_hide(); gfx_getblock(x, y, w, h, b); mouse_show(); }
    return b;
}

static void restore_bg(int x, int y, int w, int h, unsigned char far *b)
{
    if (!b) return;
    mouse_hide(); gfx_putblock(x, y, w, h, b); mouse_show();
    _ffree(b);
}

/* ---------- Menues ---------- */
void ui_menubar(UIMENU *m, int n)
{
    int i, x = 8;
    gfx_fill(0, 0, gfx_w, UI_MBAR, 1);
    gfx_fill(0, UI_MBAR, gfx_w, 1, 0);
    for (i = 0; i < n; i++) {
        m[i].x = x;
        gfx_text(x, 2, m[i].name, 0);
        x += gfx_textw(m[i].name) + 16;
    }
}

int ui_menu_hit(UIMENU *m, int n, int mx)
{
    int i;
    for (i = 0; i < n; i++)
        if (mx >= m[i].x - 4 && mx < m[i].x + gfx_textw(m[i].name) + 4) return i;
    return -1;
}

static void split(const char *s, char *l, char *r)
{
    const char *t = strchr(s, '\t');
    if (t) { memcpy(l, s, t - s); l[t - s] = 0; strcpy(r, t + 1); }
    else { strcpy(l, s); r[0] = 0; }
}

int ui_menu_key, ui_menu_mx;

int ui_menu(UIMENU *m)
{
    char l[40], r[24];
    int i, x, y, w, wl = 0, wr = 0, h, ih = gfx_fh + 4;
    int mx, my, b, sel = -1, ns, tw = gfx_textw(m->name) + 8;
    unsigned char far *bg;

    for (i = 0; i < m->n; i++) {
        split(m->items[i], l, r);
        if (gfx_textw(l) > wl) wl = gfx_textw(l);
        if (gfx_textw(r) > wr) wr = gfx_textw(r);
    }
    w = wl + (wr ? wr + 24 : 0) + 24;
    x = m->x - 4; y = UI_MBAR + 1; h = m->n * ih + 4;
    if (x + w + 2 > gfx_w) x = gfx_w - w - 2;

    bg = save_bg(x, y, w + 2, h + 2);
    mouse_hide();
    gfx_xorfill(m->x - 4, 0, tw, UI_MBAR);
    gfx_fill(x + 2, y + 2, w, h, 0);
    gfx_fill(x, y, w, h, 1);
    gfx_rect(x, y, w, h, 0);
    for (i = 0; i < m->n; i++) {
        split(m->items[i], l, r);
        gfx_text(x + 8, y + 4 + i * ih, l, 0);
        if (r[0]) gfx_text(x + w - 8 - gfx_textw(r), y + 4 + i * ih, r, 0);
    }
    mouse_show();
    if (ui_nowait) { if (bg) _ffree(bg); return -1; }

    if (!ui_menu_key) {                      /* Maustaste gehalten */
        int held = 0;
        do {
            mouse_read(&mx, &my, &b);
            held |= b & 1;
            ns = -1;
            if (mx > x && mx < x + w - 1 && my >= y + 2 && my < y + 2 + m->n * ih)
                ns = (my - y - 2) / ih;
            if (ns != sel) {
                mouse_hide();
                if (sel >= 0) gfx_xorfill(x + 1, y + 2 + sel * ih, w - 2, ih);
                if (ns  >= 0) gfx_xorfill(x + 1, y + 2 + ns  * ih, w - 2, ih);
                sel = ns;
                mouse_show();
            }
        } while (b & 1);
        /* auf dem Titel losgelassen, oder schon waehrend des Zeichnens: offen lassen, nichts ausfuehren */
        if (!ui_keyfn && (!held || (sel < 0 && my < UI_MBAR))) ui_menu_key = 2;
    }
    if (ui_menu_key) {                       /* offen: Pfeile, Enter, Esc, Maus */
        int c = 0, lx = -1, ly = -1, px, py;
        ns = ui_menu_key == 1 ? 0 : -1;
        mouse_pressed(0, &px, &py);
        for (;;) {
            if (ns != sel) {
                mouse_hide();
                if (sel >= 0) gfx_xorfill(x + 1, y + 2 + sel * ih, w - 2, ih);
                if (ns >= 0) gfx_xorfill(x + 1, y + 2 + ns * ih, w - 2, ih);
                sel = ns;
                mouse_show();
            }
            c = ui_pollkey();
            if (c == 0x148) ns = sel <= 0 ? m->n - 1 : sel - 1;
            else if (c == 0x150) ns = (sel + 1) % m->n;
            else if (c == 13) { if (sel >= 0) break; }
            else if (c == 27 || c == 0x14B || c == 0x14D || (c < 0 && ui_keyfn)) {
                sel = c == 0x14B ? -2 : c == 0x14D ? -3 : -1;
                break;
            }
            mouse_read(&mx, &my, &b);
            if ((mx != lx || my != ly) && mx > x && mx < x + w - 1 && my >= y + 2 && my < y + 2 + m->n * ih)
                ns = (my - y - 2) / ih;
            lx = mx; ly = my;
            if (mouse_pressed(0, &px, &py)) {    /* Klick: Eintrag, anderes Menue oder schliessen */
                ui_wait_release();
                if (px > x && px < x + w - 1 && py >= y + 2 && py < y + 2 + m->n * ih) {
                    sel = (py - y - 2) / ih;
                    break;
                }
                sel = py < UI_MBAR ? -4 : -1;
                ui_menu_mx = px;
                break;
            }
        }
    }
    restore_bg(x, y, w + 2, h + 2, bg);
    mouse_hide(); gfx_xorfill(m->x - 4, 0, tw, UI_MBAR); mouse_show();
    return sel;
}

static void ui_button_draw_m(int x, int y, int w, const char *s, int d)
{
    mouse_hide(); ui_button_draw(x, y, w, s, d); mouse_show();
}

/* Menueleiste per Tastatur (F10/Alt+Buchstabe): Pfeile, Enter, Esc.
 * Rueckgabe Menue * 256 + Eintrag oder -1 */
int ui_menu_run(UIMENU *m, int n, int start, int kb)
{
    int r, o;
    for (;;) {
        ui_menu_key = kb;
        r = ui_menu(&m[start]);
        ui_menu_key = 0;
        kb = 1;
        if (r == -2) start = (start + n - 1) % n;
        else if (r == -3) start = (start + 1) % n;
        else if (r == -4) {                      /* Klick in die Leiste */
            o = start;
            start = ui_menu_hit(m, n, ui_menu_mx);
            if (start < 0 || start == o) return -1;
            kb = 2;
        }
        else return r < 0 ? -1 : start * 256 + r;
    }
}

/* ---------- Dialoge ---------- */
int ui_dlg(const char *title, const char **lines, int n,
           const char *b1, const char *b2, const char *b3)
{
    const char *bl[3];
    int i, nb, w = 200, h, x, y, bw = 88, bx[3], by, th = UI_TITLE_H;
    int mx, my, b, c, res = -1, foc = 0;
    unsigned char far *bg;

    bl[0] = b1; bl[1] = b2; bl[2] = b3;
    nb = b3 ? 3 : b2 ? 2 : 1;
    for (i = 0; i < n; i++)
        if (gfx_textw(lines[i]) + 32 > w) w = gfx_textw(lines[i]) + 32;
    if (nb * (bw + 12) + 20 > w) w = nb * (bw + 12) + 20;
    h = th + 8 + n * (gfx_fh + 2) + 8 + UI_BTN_H + 8;
    x = ((gfx_w - w) / 2) & ~7;
    y = (gfx_h - h) / 2;
    by = y + h - UI_BTN_H - 8;
    for (i = 0; i < nb; i++) bx[i] = x + (w - nb * bw - (nb - 1) * 12) / 2 + i * (bw + 12);

    bg = save_bg(x, y, w + 3, h + 3);
    mouse_hide();
    ui_frame(x, y, w, h, title);
    gfx_fill(x + 4, y + 2, 10, th - 4, 0);             /* keine Schliessbox */
    for (i = 0; i < n; i++) gfx_text(x + 16, y + th + 8 + i * (gfx_fh + 2), lines[i], 0);
    for (i = 0; i < nb; i++) ui_button_draw(bx[i], by, bw, bl[i], i == 0);
    mouse_show();
    if (ui_nowait) { if (bg) _ffree(bg); return 0; }

    while (res < 0) {
        c = ui_pollkey();
        if (c == 13) res = foc;
        else if (c == 27) res = nb - 1;
        else if (nb > 1 && (c == 9 || c == 0x10F || c == 0x14B || c == 0x14D)) {   /* Knopf wechseln */
            ui_button_draw_m(bx[foc], by, bw, bl[foc], 0);
            foc = (foc + (c == 0x10F || c == 0x14B ? nb - 1 : 1)) % nb;
            ui_button_draw_m(bx[foc], by, bw, bl[foc], 1);
        }
        mouse_read(&mx, &my, &b);
        if (b & 1) {
            for (i = 0; i < nb; i++)
                if (ui_in(mx, my, bx[i], by, bw, UI_BTN_H) && ui_button_track(bx[i], by, bw)) res = i;
            if (res < 0) ui_wait_release();
        }
    }
    restore_bg(x, y, w + 3, h + 3, bg);
    return res;
}

int ui_inputbox(const char *title, const char *prompt, char *buf, int max)
{
    int w = 400, h, x, y, r, th = UI_TITLE_H;
    unsigned char far *bg;

    h = th + 8 + gfx_fh + 6 + gfx_fh + 4 + 8 + gfx_fh + 8;
    x = ((gfx_w - w) / 2) & ~7;
    y = (gfx_h - h) / 2;
    bg = save_bg(x, y, w + 3, h + 3);
    mouse_hide();
    ui_frame(x, y, w, h, title);
    gfx_fill(x + 4, y + 2, 10, th - 4, 0);
    gfx_text(x + 12, y + th + 8, prompt, 0);
    gfx_text(x + 12, y + h - gfx_fh - 6, "Enter = OK, Esc = cancel", 0);
    mouse_show();
    r = ui_input(x + 12, y + th + 8 + gfx_fh + 6, w - 24, buf, max);
    restore_bg(x, y, w + 3, h + 3, bg);
    return r;
}
