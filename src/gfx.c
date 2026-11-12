#include <dos.h>
#include <i86.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <conio.h>
#include <fcntl.h>
#include <process.h>
#include "gfx.h"

int gfx_w, gfx_h, gfx_fh, gfx_type, gfx_bpr = 80;
static unsigned fcell = 16;          /* Bytes je Zeichen in font16 (16 VGA, 14 Hercules) */
static unsigned f14seg;              /* RDFONT.F14 im DOS-Speicher, 0 = keine */

unsigned char far *gfx_vram;
#define vram gfx_vram
static unsigned char far *font;      /* 8x8 ROM-Font, Zeichen 0-127 */
static unsigned char far *hifont;    /* 8x8 Zeichen 128-255 (GRAFTABL, INT 1Fh) oder NULL */
static unsigned char far *font16;    /* VGA: 8x16-Font aus dem BIOS, alle 256 Zeichen */

/* Eingebaute Umlaute (Codepage 437), falls auf CGA kein GRAFTABL geladen ist */
static const unsigned char uml_code[] = { 0x84, 0x94, 0x81, 0x8E, 0x99, 0x9A, 0xE1, 0x82, 0xF8 };
static const unsigned char uml_glyph[][8] = {
    { 0xCC, 0x00, 0x78, 0x0C, 0x7C, 0xCC, 0x7E, 0x00 },   /* ae */
    { 0xCC, 0x00, 0x78, 0xCC, 0xCC, 0xCC, 0x78, 0x00 },   /* oe */
    { 0xCC, 0x00, 0xCC, 0xCC, 0xCC, 0xCC, 0x7E, 0x00 },   /* ue */
    { 0xC6, 0x38, 0x6C, 0xC6, 0xFE, 0xC6, 0xC6, 0x00 },   /* AE */
    { 0xC6, 0x38, 0x6C, 0xC6, 0xC6, 0x6C, 0x38, 0x00 },   /* OE */
    { 0xC6, 0x00, 0xC6, 0xC6, 0xC6, 0xC6, 0x7C, 0x00 },   /* UE */
    { 0x78, 0xCC, 0xCC, 0xD8, 0xCC, 0xCC, 0xD8, 0xC0 },   /* sz */
    { 0x1C, 0x00, 0x78, 0xCC, 0xFC, 0xC0, 0x78, 0x00 },   /* e acute */
    { 0x38, 0x6C, 0x6C, 0x38, 0x00, 0x00, 0x00, 0x00 }    /* Grad */
};
unsigned gfx_rowofs[480];            /* Zeilen-Offsets (CGA interlaced!) */
#define rowofs gfx_rowofs
static int fsh;                      /* Font-Zeilen-Shift: 0 oder 1 */
static unsigned char oldmode;

static void setmode(unsigned char m)
{
    union REGS r;
    r.h.ah = 0x00; r.h.al = m;
    int86(0x10, &r, &r);
}

static int detect_vga(void)
{
    union REGS r;
    r.x.ax = 0x1A00;
    int86(0x10, &r, &r);
    return r.h.al == 0x1A;
}

/* Hercules: Bit 7 von 3BAh (Vertikal-Sync) wechselt, bei MDA nicht */
static int detect_hgc(void)
{
    unsigned i;
    unsigned char a = inp(0x3BA) & 0x80;
    for (i = 0; i < 0x8000; i++) if ((inp(0x3BA) & 0x80) != a) return 1;
    return 0;
}

/* 8x14-Schrift einmal aus RDFONT.F14 neben der EXE laden */
static void load_f14(void)
{
    static int tried;
    char fn[96], *p;
    int h;
    unsigned n;
    if (tried++) return;
    _cmdname(fn);
    p = strrchr(fn, '\\'); p = p ? p + 1 : fn;
    strcpy(p, "RDFONT.F14");
    if (_dos_open(fn, O_RDONLY, &h)) return;
    if (!_dos_allocmem(224, &f14seg)) {
        if (_dos_read(h, MK_FP(f14seg, 0), 3584, &n) || n != 3584) { _dos_freemem(f14seg); f14seg = 0; }
    }
    _dos_close(h);
}

static void hgc_mode(int gr)
{
    static const unsigned char crt[2][12] = {
        { 0x61, 0x50, 0x52, 0x0F, 0x19, 0x06, 0x19, 0x19, 0x02, 0x0D, 0x0B, 0x0C },   /* Text */
        { 0x35, 0x2D, 0x2E, 0x07, 0x5B, 0x02, 0x57, 0x57, 0x02, 0x03, 0x00, 0x00 } }; /* 720x348 */
    int i;
    outp(0x3BF, gr);                               /* Grafik erlauben, nur Seite 0 (B000h) */
    outp(0x3B8, gr ? 0x02 : 0x20);                 /* Bild aus beim Umprogrammieren */
    for (i = 0; i < 12; i++) { outp(0x3B4, i); outp(0x3B5, crt[gr][i]); }
    outp(0x3B8, gr ? 0x0A : 0x28);
    *(unsigned char far *)MK_FP(0x40, 0x49) = gr ? 6 : 7;   /* Maustreiber: 6 = Hercules-Grafik Seite 0 */
}

int gfx_arg(const char *a)
{
    return !stricmp(a, "/CGA") ? GFX_CGA : !stricmp(a, "/VGA") ? GFX_VGA : !stricmp(a, "/HGC") ? GFX_HGC : -1;
}

const char *gfx_flag(void)
{
    return gfx_type == GFX_VGA ? "/VGA" : gfx_type == GFX_HGC ? "/HGC" : "/CGA";
}

void gfx_dump(const char *fn)
{
    FILE *f = fopen(fn, "wb");
    unsigned i, n = gfx_type == GFX_VGA ? 38400U : gfx_type == GFX_HGC ? 0x8000 : 0x4000;
    if (!f) return;
    for (i = 0; i < n; i++) fputc(vram[i], f);
    fclose(f);
}

int gfx_keep;                                  /* 1 = gfx_close laesst die Grafik stehen */

/* Modus nur umschalten, wenn er nicht schon steht (Hinweis des Vorgaengers bleibt bis zum Zeichnen) */
static void setmode2(unsigned char m, unsigned char cur)
{
    if (m != cur) setmode(m);
}

int gfx_init(int type)
{
    union REGS r;
    unsigned y;
    unsigned char cur;

    r.h.ah = 0x0F;
    int86(0x10, &r, &r);
    cur = r.h.al & 0x7F;
    oldmode = r.h.al > 3 && r.h.al != 7 ? 3 : r.h.al;   /* nach Netz-Neustart steht noch Grafik */

    if (type == GFX_AUTO)
        type = detect_vga() ? GFX_VGA : cur == 7 && detect_hgc() ? GFX_HGC : GFX_CGA;
    gfx_type = type;
    gfx_bpr = 80; fcell = 16; font16 = 0;

    if (type == GFX_HGC) {
        oldmode = 7;
        load_f14();
        hgc_mode(1);
        gfx_w = 720; gfx_h = 348; gfx_bpr = 90; fsh = 0;
        gfx_fh = f14seg ? 14 : 8;
        vram = (unsigned char far *)MK_FP(0xB000, 0);
        for (y = 0; y < 348; y++) rowofs[y] = (y & 3) * 0x2000 + (y >> 2) * 90;
        _fmemset(vram, 0, 0x8000);
    } else if (type == GFX_VGA) {
        setmode2(0x11, cur);                       /* 640x480 mono, 38 KB */
        gfx_w = 640; gfx_h = 480; gfx_fh = 16; fsh = 1;
        vram = (unsigned char far *)MK_FP(0xA000, 0);
        for (y = 0; y < 480; y++) rowofs[y] = y * 80;
    } else {
        setmode2(0x06, cur);                       /* 640x200 mono, 16 KB */
        gfx_w = 640; gfx_h = 200; gfx_fh = 8; fsh = 0;
        vram = (unsigned char far *)MK_FP(0xB800, 0);
        for (y = 0; y < 200; y++)
            rowofs[y] = (y >> 1) * 80 + ((y & 1) ? 0x2000 : 0);
    }
    font = (unsigned char far *)MK_FP(0xF000, 0xFA6E);
    if (type == GFX_HGC && f14seg) { font16 = (unsigned char far *)MK_FP(f14seg, 0); fcell = 14; }
    hifont = (unsigned char far *)_dos_getvect(0x1F);     /* GRAFTABL? */
    if (FP_SEG(hifont) == 0 || FP_SEG(hifont) == 0xF000) hifont = 0;
    if (type != GFX_HGC && cur == (type == GFX_VGA ? 0x11 : 0x06))   /* nicht umgeschaltet: selbst loeschen */
        _fmemset(vram, 0, type == GFX_VGA ? 38400U : 0x4000);
    if (type == GFX_VGA) {
        union REGPACK rp;
        memset(&rp, 0, sizeof rp);
        rp.w.ax = 0x1130; rp.h.bh = 6;                    /* VGA 8x16-Font */
        intr(0x10, &rp);
        font16 = (unsigned char far *)MK_FP(rp.w.es, rp.w.bp);
    }
    return type;
}

void gfx_close(void)
{
    if (gfx_type == GFX_HGC) { hgc_mode(0); setmode(7); }   /* Hercules immer zurueck in Text */
    else if (!gfx_keep) setmode(oldmode);
}

#ifdef PERF
/* Horizontale Spanne x0..x1 (inklusive), entweder mit Muster setzen oder XOR */
static void span(int x0, int x1, int y, unsigned char pat, int xr)
{
    unsigned char far *p;
    int b0, b1;
    unsigned char lm, rm;

    if (y < 0 || y >= gfx_h) return;
    if (x0 < 0) x0 = 0;
    if (x1 >= gfx_w) x1 = gfx_w - 1;
    if (x0 > x1) return;

    p  = vram + rowofs[y] + (x0 >> 3);
    b0 = x0 >> 3; b1 = x1 >> 3;
    lm = (unsigned char)(0xFF >> (x0 & 7));
    rm = (unsigned char)(0xFF << (7 - (x1 & 7)));
    if (b0 == b1) lm &= rm;

    if (xr) *p ^= lm; else *p = (unsigned char)((*p & ~lm) | (pat & lm));
    if (b0 == b1) return;
    p++;
    if (!xr) { _fmemset(p, pat, b1 - b0 - 1); p += b1 - b0 - 1; }   /* REP STOSB */
    else for (b0++; b0 < b1; b0++, p++) *p ^= 0xFF;
    if (xr) *p ^= rm; else *p = (unsigned char)((*p & ~rm) | (pat & rm));
}
#endif

/* Rechteck zeilenweise in Assembler: Raender maskiert, Mitte mit REP STOSB.
 * pe/po: Muster fuer gerade/ungerade Bildschirmzeilen, xr: XOR statt setzen */
static void rows(int x, int y, int w, int h, unsigned char pe, unsigned char po, int xr)
{
    int x1 = x + w - 1, b0, b1, single;
    unsigned roff, vseg, col, mid, n;
    unsigned char lm, rm, p0, p1;

    if (x < 0) x = 0;
    if (x1 >= gfx_w) x1 = gfx_w - 1;
    if (y < 0) { h += y; y = 0; }
    if (y + h > gfx_h) h = gfx_h - y;
    if (x > x1 || h <= 0) return;
    b0 = x >> 3; b1 = x1 >> 3;
    lm = (unsigned char)(0xFF >> (x & 7));
    rm = (unsigned char)(0xFF << (7 - (x1 & 7)));
    single = b0 == b1;
    if (single) lm &= rm;
    mid = single ? 0 : b1 - b0 - 1;
    roff = FP_OFF((void far *)(rowofs + y));
    vseg = FP_SEG(vram);
    col = b0;
    n = h;
    p0 = (y & 1) ? po : pe; p1 = (y & 1) ? pe : po;
    if (!xr) { _asm {
        push ax
        push bx
        push cx
        push dx
        push es
        push si
        push di
        cld
        mov  es, vseg
        mov  si, roff
        mov  al, p0
        mov  ah, p1
    fz:
        mov  di, ss:[si]
        add  di, col
        mov  bl, es:[di]
        xor  bl, al
        and  bl, lm
        xor  es:[di], bl
        cmp  word ptr single, 0
        jne  fn
        inc  di
        mov  cx, mid
        rep  stosb
        mov  bl, es:[di]
        xor  bl, al
        and  bl, rm
        xor  es:[di], bl
    fn:
        add  si, 2
        xchg al, ah
        dec  word ptr n
        jnz  fz
        pop  di
        pop  si
        pop  es
        pop  dx
        pop  cx
        pop  bx
        pop  ax
    } } else { _asm {
        push ax
        push bx
        push cx
        push dx
        push es
        push si
        push di
        mov  es, vseg
        mov  si, roff
    xz:
        mov  di, ss:[si]
        add  di, col
        mov  al, lm
        xor  es:[di], al
        cmp  word ptr single, 0
        jne  xn
        inc  di
        mov  cx, mid
        jcxz xr_
    xm:
        not  byte ptr es:[di]
        inc  di
        loop xm
    xr_:
        mov  al, rm
        xor  es:[di], al
    xn:
        add  si, 2
        dec  word ptr n
        jnz  xz
        pop  di
        pop  si
        pop  es
        pop  dx
        pop  cx
        pop  bx
        pop  ax
    } }
}

void gfx_fill(int x, int y, int w, int h, int color)
{
    unsigned char pat = color ? 0xFF : 0x00;
    rows(x, y, w, h, pat, pat, 0);
}

void gfx_pattern(int x, int y, int w, int h)
{
    rows(x, y, w, h, 0x55, 0xAA, 0);       /* gerade Zeile 0x55, ungerade 0xAA wie bisher */
}

void gfx_xorfill(int x, int y, int w, int h)
{
    rows(x, y, w, h, 0, 0, 1);
}

void gfx_rect(int x, int y, int w, int h, int color)
{
    unsigned char pat = color ? 0xFF : 0x00;
    rows(x, y, w, 1, pat, pat, 0);
    rows(x, y + h - 1, w, 1, pat, pat, 0);
    rows(x, y + 1, 1, h - 2, pat, pat, 0);
    rows(x + w - 1, y + 1, 1, h - 2, pat, pat, 0);
}

void gfx_xorrect(int x, int y, int w, int h)
{
    rows(x, y, w, 1, 0, 0, 1);
    rows(x, y + h - 1, w, 1, 0, 0, 1);
    rows(x, y + 1, 1, h - 2, 0, 0, 1);
    rows(x + w - 1, y + 1, 1, h - 2, 0, 0, 1);
}

static const unsigned char far *glyph(unsigned char c, int *stride)
{
    int i;
    *stride = 1;
    if (font16) return font16 + c * fcell;
    *stride = 0;                                  /* 8x8: Zeile r >> fsh */
    if (c < 128) return font + c * 8;
    if (hifont) return hifont + (c - 128) * 8;
    for (i = 0; i < (int)sizeof uml_code; i++)
        if (uml_code[i] == c) return (const unsigned char far *)uml_glyph[i];
    return font + '?' * 8;
}

#ifdef PERF
void gfx_text_c(int x, int y, const char *s, int color)   /* alte C-Fassung zum Vergleich */
{
    int r, sh, yy, st;
    unsigned char g;
    unsigned char far *p;
    const unsigned char far *gl;

    for (; *s; s++, x += 8) {
        if (x < 0 || x + 8 > gfx_w) continue;
        gl = glyph((unsigned char)*s, &st);
        sh = x & 7;
        for (r = 0; r < gfx_fh; r++) {
            yy = y + r;
            if (yy < 0 || yy >= gfx_h) continue;
            g = st ? gl[r] : gl[r >> fsh];
            if (!g) continue;
            p = vram + rowofs[yy] + (x >> 3);
            if (color) {
                p[0] |= (unsigned char)(g >> sh);
                if (sh) p[1] |= (unsigned char)(g << (8 - sh));
            } else {
                p[0] &= (unsigned char)~(g >> sh);
                if (sh) p[1] &= (unsigned char)~(unsigned char)(g << (8 - sh));
            }
        }
    }
}
#endif

/* Schnelle Fassung: Pixelzeilen direkt aus dem Font (DS:SI) in Assembler setzen, Zeilen-
 * Offsets aus der Tabelle ueber SS. Pro Zeile: Glyphe um x&7 schieben, 2 Bytes AND/OR,
 * leere Zeilen werden uebersprungen. Beliebige x-Position (nicht nur Byte-Grenze). */
void gfx_text(int x, int y, const char *s, int color)
{
    int st;
    unsigned goff, gseg, roff, vseg, col, sh, n;
    const unsigned char far *gl;

    /* Annahme (Watcom small/large): SS = DGROUP, Zeilentabelle ueber SS erreichbar */
    if (y < 0 || y + gfx_fh > gfx_h) return;     /* nur ganz sichtbare Zeilen */
    roff = FP_OFF((void far *)(rowofs + y));
    vseg = FP_SEG(vram);
    for (; *s; s++, x += 8) {
        if (x < 0 || x + 8 > gfx_w) continue;
        if (font16) gl = font16 + (fcell == 16 ? (unsigned)(unsigned char)*s << 4 : (unsigned)(unsigned char)*s * 14);
        else if (!(*s & 0x80)) gl = font + ((unsigned)*s << 3);
        else gl = glyph((unsigned char)*s, &st);
        gseg = FP_SEG(gl); goff = FP_OFF(gl);
        sh = x & 7;
        col = (unsigned)(x >> 3);
        n = gfx_fh;
        if (!sh) {
            /* Byte-Grenze: Zeichenzelle ist genau 1 Byte breit -> nur schreiben, nicht lesen
             * (Lesen ueber den 8-Bit-Bus ist teuer). Hintergrund der Zelle wird mit gesetzt. */
            unsigned char inv = color ? 0 : 0xFF;
            unsigned start = rowofs[y] + col;
            if (gfx_type == GFX_VGA) { _asm {
                push ax
                push bx
                push cx
                push dx
                push es
                push ds
                push si
                push di
                mov  es, vseg
                mov  di, start
                mov  si, goff
                mov  cx, n
                mov  bl, inv
                mov  ax, gseg
                mov  ds, ax
                mov  ah, bl
                cld
            va:
                lodsb
                xor  al, ah
                stosb
                add  di, 79
                loop va
                pop  di
                pop  si
                pop  ds
                pop  es
                pop  dx
                pop  cx
                pop  bx
                pop  ax
            } } else { _asm {
                push ax
                push bx
                push cx
                push dx
                push es
                push ds
                push si
                push di
                mov  es, vseg
                mov  si, goff
                mov  bx, roff
                mov  dx, col
                mov  cx, n
                mov  ax, gseg
                mov  ds, ax
                mov  ah, inv
            ca:
                lodsb
                xor  al, ah
                mov  di, ss:[bx]
                add  di, dx
                mov  es:[di], al
                add  bx, 2
                loop ca
                pop  di
                pop  si
                pop  ds
                pop  es
                pop  dx
                pop  cx
                pop  bx
                pop  ax
            } }
            continue;
        }
        if (color) { _asm {
            push ax
            push bx
            push cx
            push dx
            push es
            push ds
            push si
            push di
            mov  es, vseg
            mov  si, goff
            mov  bx, roff
            mov  dx, col
            mov  cx, sh
            mov  ax, gseg
            mov  ds, ax
        zeile1:
            lodsb
            or   al, al
            jz   leer1
            mov  ah, al
            xor  al, al
            shr  ax, cl
            mov  di, ss:[bx]
            add  di, dx
            or   es:[di], ah
            or   es:[di+1], al
        leer1:
            add  bx, 2
            dec  word ptr n
            jnz  zeile1
            pop  di
            pop  si
            pop  ds
            pop  es
            pop  dx
            pop  cx
            pop  bx
            pop  ax
        } } else { _asm {
            push ax
            push bx
            push cx
            push dx
            push es
            push ds
            push si
            push di
            mov  es, vseg
            mov  si, goff
            mov  bx, roff
            mov  dx, col
            mov  cx, sh
            mov  ax, gseg
            mov  ds, ax
        zeile0:
            lodsb
            or   al, al
            jz   leer0
            mov  ah, al
            xor  al, al
            shr  ax, cl
            not  ax
            mov  di, ss:[bx]
            add  di, dx
            and  es:[di], ah
            and  es:[di+1], al
        leer0:
            add  bx, 2
            dec  word ptr n
            jnz  zeile0
            pop  di
            pop  si
            pop  ds
            pop  es
            pop  dx
            pop  cx
            pop  bx
            pop  ax
        } }
    }
}

/* Bildausschnitt sichern/zurueckschreiben (byteweise, fuer Menues und Dialoge) */
unsigned gfx_blocksize(int x, int w, int h)
{
    return (unsigned)(((x + w - 1) >> 3) - (x >> 3) + 1) * (unsigned)h;
}

void gfx_getblock(int x, int y, int w, int h, unsigned char far *buf)
{
    int j, n = ((x + w - 1) >> 3) - (x >> 3) + 1;
    for (j = 0; j < h; j++, buf += n)
        if (y + j < gfx_h) _fmemcpy(buf, gfx_vram + gfx_rowofs[y + j] + (x >> 3), n);
}

void gfx_putblock(int x, int y, int w, int h, const unsigned char far *buf)
{
    int j, n = ((x + w - 1) >> 3) - (x >> 3) + 1;
    for (j = 0; j < h; j++, buf += n)
        if (y + j < gfx_h) _fmemcpy(gfx_vram + gfx_rowofs[y + j] + (x >> 3), buf, n);
}

/* Ausschnitt (Bytespalten bx..bx+nb-1) um Zeilen verschieben: Liste scrollen ohne neu zu zeichnen */
void gfx_moverows(int bx, int nb, int dst, int src, int h)
{
    int j;
    if (dst < src) for (j = 0; j < h; j++)
        _fmemcpy(gfx_vram + gfx_rowofs[dst + j] + bx, gfx_vram + gfx_rowofs[src + j] + bx, nb);
    else for (j = h - 1; j >= 0; j--)
        _fmemcpy(gfx_vram + gfx_rowofs[dst + j] + bx, gfx_vram + gfx_rowofs[src + j] + bx, nb);
}

int gfx_textw(const char *s)
{
    return (int)strlen(s) * 8;
}

/* Vergroesserter Text: jedes Font-Pixel wird ein sx*sy-Block (fuer die Uhr) */
void gfx_bigtext(int x, int y, const char *s, int sx, int sy, int color)
{
    int r, c, run;
    unsigned char g, ch;
    for (; *s; s++, x += 8 * sx) {
        ch = (unsigned char)*s;
        if (ch > 127) ch = '?';
        for (r = 0; r < 8; r++) {
            g = font[ch * 8 + r];
            for (c = 0; c < 8; c++) {
                if (!(g & (0x80 >> c))) continue;
                run = 1;                      /* zusammenhaengende Pixel am Stueck */
                while (c + run < 8 && (g & (0x80 >> (c + run)))) run++;
                gfx_fill(x + c * sx, y + r * sy, run * sx, sy, color);
                c += run - 1;
            }
        }
    }
}

/* Eine Pixelzeile r (0..gfx_fh-1) eines Zeichens */
unsigned char gfx_glyphrow(unsigned char c, int r)
{
    int st;
    const unsigned char far *gl = glyph(c, &st);
    return st ? gl[r] : gl[r >> fsh];
}

#ifdef __LARGE__
/* Alle gfx_fh Zeilen eines Zeichens (RDEDIT-Tabelle: einmal suchen statt pro Zeile) */
void gfx_glyphrows(unsigned char c, unsigned char *out)
{
    int st, r;
    const unsigned char far *gl = glyph(c, &st);
    for (r = 0; r < gfx_fh; r++) out[r] = st ? gl[r] : gl[r >> fsh];
}
#endif
