/* Schnelle Textausgabe fuer RDEDIT: ganze Zeilen am Stueck, nur auf Byte-Grenzen.
 * Die Schrift liegt einmal invertiert (schwarz auf weiss) in einer Tabelle,
 * jede Pixelzeile wird mit LODSB/XLAT/STOSB uebersetzt (3 Befehle pro Byte).
 * Ausserdem: Scrollen und Bildausschnitte sichern. Nur in RDEDIT gelinkt, haelt RDESK klein. */
#include <string.h>
#include <i86.h>
#include "gfx.h"

static struct {                          /* Tabelle und Puffer im selben Segment (XLAT) */
    unsigned char tab[16][256];
    unsigned char ch[100];
} F;
static int ready = -1;

/* Ganze Pixelzeilen kopieren (Scrollen), ueberlappend erlaubt */
void gfx_copyrows(int dst, int src, int n)
{
    int i;
    if (gfx_type == GFX_VGA) {                     /* linear: am Stueck */
        _fmemmove(gfx_vram + gfx_rowofs[dst], gfx_vram + gfx_rowofs[src], (unsigned)n * 80);   /* nur VGA */
        return;
    }
    if (dst < src) for (i = 0; i < n; i++)
        _fmemcpy(gfx_vram + gfx_rowofs[dst + i], gfx_vram + gfx_rowofs[src + i], gfx_bpr);
    else for (i = n - 1; i >= 0; i--)
        _fmemcpy(gfx_vram + gfx_rowofs[dst + i], gfx_vram + gfx_rowofs[src + i], gfx_bpr);
}

static void build(void)
{
    int c, r;
    unsigned char g[16];
    for (c = 0; c < 256; c++) {
        gfx_glyphrows((unsigned char)c, g);
        for (r = 0; r < gfx_fh; r++) F.tab[r][c] = (unsigned char)~g[r];
    }
    ready = gfx_type;
}

void gfx_textrow(int bx, int y, const char *s, int n, int w)
{
    unsigned char far *p;
    unsigned dseg, soff, toff, pseg, poff;
    int r, cnt;

    if (ready != gfx_type) build();
    if (w > gfx_bpr - bx) w = gfx_bpr - bx;
    if (n > w) n = w;
    if (n < 0) n = 0;
    _fmemcpy(F.ch, s, n);
    dseg = FP_SEG(&F);
    soff = FP_OFF(F.ch);
    for (r = 0; r < gfx_fh; r++) {
        p = gfx_vram + gfx_rowofs[y + r] + bx;
        toff = FP_OFF(F.tab[r]);
        pseg = FP_SEG(p); poff = FP_OFF(p);
        cnt = n;
        _asm {
            push ds
            mov  ax, dseg
            mov  si, soff
            mov  bx, toff
            mov  es, pseg
            mov  di, poff
            mov  cx, cnt
            mov  ds, ax
            cld
            jcxz fertig
        schleife:
            lodsb
            xlat
            stosb
            loop schleife
        fertig:
            pop  ds
        }
        if (w > n) _fmemset(p + n, 0xFF, w - n);
    }
}
