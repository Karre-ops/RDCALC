/* RetroDesk - Grafiktreiber (1 Bit pro Pixel, CGA Modus 6 / VGA Modus 11h) */
#ifndef GFX_H
#define GFX_H

#define GFX_AUTO 0
#define GFX_CGA  1
#define GFX_VGA  2
#define GFX_HGC  3   /* Hercules 720x348, 8x14-Schrift aus RDFONT.F14 */
/* Wert je Karte: CGA, Hercules, VGA */
#define GFX_SEL(c, h, v) (gfx_type == GFX_VGA ? (v) : gfx_type == GFX_HGC ? (h) : (c))

extern int gfx_w, gfx_h;   /* Aufloesung */
extern int gfx_fh;         /* Fonthoehe: 8 (CGA) oder 16 (VGA, verdoppelt) */
extern int gfx_type;
extern int gfx_keep;
extern int gfx_bpr;        /* Bytes pro Pixelzeile: 80 oder 90 */
int  gfx_arg(const char *a);   /* "/CGA" "/VGA" "/HGC" -> Typ, sonst -1 */
const char *gfx_flag(void);    /* Schalter fuer Kindprogramme */
void gfx_dump(const char *fn); /* Bildspeicher in Datei (Tests) */

int  gfx_init(int type);
void gfx_close(void);
void gfx_fill(int x, int y, int w, int h, int color);   /* 1=weiss, 0=schwarz */
void gfx_pattern(int x, int y, int w, int h);           /* 50%-Raster Desktop */
void gfx_xorfill(int x, int y, int w, int h);
void gfx_rect(int x, int y, int w, int h, int color);
void gfx_xorrect(int x, int y, int w, int h);
void gfx_text(int x, int y, const char *s, int color);
int  gfx_textw(const char *s);
void gfx_bigtext(int x, int y, const char *s, int sx, int sy, int color);
void gfx_copyrows(int dst, int src, int n);
unsigned char gfx_glyphrow(unsigned char c, int r);
void gfx_glyphrows(unsigned char c, unsigned char *out);
extern unsigned char far *gfx_vram;
extern unsigned gfx_rowofs[];
/* gfxfast.c: schnelle Textzeile, schwarz auf weiss, Byte-Spalte bx, n Zeichen, auf w aufgefuellt */
void gfx_textrow(int bx, int y, const char *s, int n, int w);
unsigned gfx_blocksize(int x, int w, int h);
void gfx_moverows(int bx, int nb, int dst, int src, int h);
void gfx_getblock(int x, int y, int w, int h, unsigned char far *buf);
void gfx_putblock(int x, int y, int w, int h, const unsigned char far *buf);

#endif
