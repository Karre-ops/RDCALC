#include <conio.h>
#include <dos.h>
#include <i86.h>
#include "snd.h"

int snd_mode = SND_OFF;

static volatile unsigned long far *ticks =
    (volatile unsigned long far *)MK_FP(0x40, 0x6C);   /* 18,2 Hz */

typedef struct { unsigned hz; unsigned char t; } NOTE;  /* t in Ticks */

static const NOTE s_start[] = { {523,2},{659,2},{784,2},{1047,4},{0,0} };
static const NOTE s_end[]   = { {1047,2},{784,2},{659,2},{523,4},{0,0} };
static const NOTE s_error[] = { {220,3},{1,1},{165,5},{0,0} };
static const NOTE s_ask[]   = { {880,2},{1175,3},{0,0} };
static const NOTE s_conn[]  = { {659,1},{880,1},{1175,1},{1568,3},{0,0} };
static const NOTE *sounds[] = { s_start, s_end, s_error, s_ask, 0, s_conn };
int snd_on = 1;

static void wait_ticks(unsigned n)
{
    unsigned long t = *ticks;
    while (*ticks - t < n) ;
}

static unsigned pit0(void)
{
    unsigned lo;
    _disable();
    outp(0x43, 0x00);                            /* Kanal 0 latchen */
    lo = inp(0x40);
    lo |= inp(0x40) << 8;
    _enable();
    return lo;
}

/* ---------- PC-Speaker (PIT Kanal 2) ---------- */
void spk_tone(unsigned hz)
{
    unsigned d;
    if (hz < 20) { spk_off(); return; }
    d = (unsigned)(1193182UL / hz);
    outp(0x43, 0xB6);
    outp(0x42, d & 0xFF);
    outp(0x42, d >> 8);
    outp(0x61, inp(0x61) | 3);
}

void spk_off(void)
{
    outp(0x61, inp(0x61) & 0xFC);
}

/* ---------- AdLib / OPL2 an 388h ---------- */
static void opl(unsigned char reg, unsigned char val)
{
    int i;
    outp(0x388, reg);
    for (i = 0; i < 6; i++)  inp(0x388);     /* 3,3 us */
    outp(0x389, val);
    for (i = 0; i < 35; i++) inp(0x388);     /* 23 us */
}

int adlib_detect(void)
{
    unsigned char s1, s2;
    int i;
    opl(0x04, 0x60);
    opl(0x04, 0x80);
    s1 = (unsigned char)inp(0x388);
    opl(0x02, 0xFF);
    opl(0x04, 0x21);
    for (i = 0; i < 200; i++) inp(0x388);    /* > 80 us */
    s2 = (unsigned char)inp(0x388);
    opl(0x04, 0x60);
    opl(0x04, 0x80);
    return (s1 & 0xE0) == 0x00 && (s2 & 0xE0) == 0xC0;
}

static void adlib_reset(void)
{
    int r;
    for (r = 0x01; r <= 0xF5; r++) opl((unsigned char)r, 0);
}

static void adlib_instrument(void)
{
    /* Kanal 0: Modulator-Op 00h, Carrier-Op 03h - heller "Glocken"-Klang */
    opl(0x01, 0x20);                 /* Waveform Select an */
    opl(0x20, 0x01); opl(0x23, 0x01);
    opl(0x40, 0x18); opl(0x43, 0x00);
    opl(0x60, 0xF4); opl(0x63, 0xF4);
    opl(0x80, 0x55); opl(0x83, 0x55);
    opl(0xC0, 0x06);
}

static unsigned char last_b0;

void adlib_note(unsigned hz)
{
    unsigned long fnum;
    unsigned block = 0;

    opl(0xB0, last_b0 & 0x1F);       /* alte Note loslassen */
    if (hz < 20) return;
    do {
        fnum = (unsigned long)hz * (1UL << (20 - block)) / 49716UL;
        if (fnum < 1024) break;
    } while (++block < 8);
    opl(0xA0, (unsigned char)(fnum & 0xFF));
    last_b0 = (unsigned char)(0x20 | (block << 2) | ((fnum >> 8) & 3));
    opl(0xB0, last_b0);
}

/* ---------- Systemklaenge ---------- */
int snd_init(int mode)
{
    if (mode == SND_AUTO)
        mode = adlib_detect() ? SND_ADLIB : SND_SPEAKER;
    else if (mode == SND_ADLIB && !adlib_detect())
        mode = SND_SPEAKER;
    if (mode == SND_ADLIB) { adlib_reset(); adlib_instrument(); }
    snd_mode = mode;
    return mode;
}

/* Nach Rueckkehr aus einem Programm: erkannte Hardware aus RDESK.STA uebernehmen, statt neu
 * zu suchen und alle 245 OPL-Register zu loeschen. Nur alle 9 Kanaele stumm + eigener Klang. */
void snd_quick(int mode)
{
    int r;
    if (mode == SND_ADLIB) {
        for (r = 0xB0; r <= 0xB8; r++) opl((unsigned char)r, 0);
        adlib_instrument();
    }
    snd_mode = mode;
}

int snd_soft;

void snd_close(void)
{
    int r;
    if (snd_mode == SND_ADLIB && snd_soft) for (r = 0xB0; r <= 0xB8; r++) opl((unsigned char)r, 0);
    else if (snd_mode == SND_ADLIB) adlib_reset();
    spk_off();
}

static void tone(unsigned hz)
{
    if (snd_mode == SND_ADLIB) adlib_note(hz);
    else spk_tone(hz);
}

void snd_play(int id)
{
    const NOTE *n;
    unsigned i;

    if (snd_mode == SND_OFF || !snd_on) return;
    if (id == SND_CLICK) {           /* kurzer Klick immer ueber Speaker, ca. 3 ms nach PIT-Kanal 0 (taktunabhaengig) */
        spk_tone(1800);
        i = pit0();
        while ((unsigned)(i - pit0()) < 8000) ;   /* Modus 3 zaehlt in Zweierschritten: 8000 = 3,4 ms */
        spk_off();
        return;
    }
    for (n = sounds[id]; n->t; n++) {
        tone(n->hz);
        wait_ticks(n->t);
    }
    tone(0);
}
