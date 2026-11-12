/* RetroDesk - Bedienelemente (RDLIB) */
#ifndef RDUI_H
#define RDUI_H
#include <conio.h>
int ui_kbhit(void);                /* schnelles kbhit ueber BIOS */
#define kbhit ui_kbhit

#define UI_BTN_H   (gfx_fh + 6)
#define UI_LI_H    (gfx_fh + 2)
#define UI_TITLE_H (gfx_fh + 3)

typedef struct {
    int x, y, w, rows;          /* sichtbare Zeilen */
    const char **items;
    int count, top, sel;
} UILIST;

typedef struct { int x, y, w, h; unsigned seg; } UISAVE;
int  ui_save(UISAVE *s, int x, int y, int w, int h);   /* Hintergrund sichern, 0 = kein Speicher */
void ui_restore(UISAVE *s);                            /* zurueckschreiben, setzt ui_restored */
extern int ui_restored;                                /* 1 = letzter Dialog hat aufgeraeumt */
extern int ui_nowait;
extern int ui_input_append;
extern int ui_input_mask;               /* Passwortfeld: Sternchen */
extern int (*ui_keyfn)(void);   /* Testmodus: ASCII oder 0x100+Scancode, -1 = Ende */

int  ui_in(int mx, int my, int x, int y, int w, int h);
void ui_wait_release(void);

void ui_button_draw(int x, int y, int w, const char *label, int def);
int  ui_button_track(int x, int y, int w);     /* 1 = innen losgelassen */

void ui_list_draw(UILIST *l);
int  ui_list_click(UILIST *l, int mx, int my); /* 1 = Auswahl geaendert, 2 = Doppelklick */
void ui_list_reset(void);                      /* Doppelklick-Gedaechtnis loeschen */
void ui_list_select(UILIST *l, int s);         /* Auswahl setzen, schnell umfaerben */
int  ui_list_key(UILIST *l, int c);            /* Pfeile/Bild/Pos1/Ende/Buchstabe, 1 = bewegt */

/* Fenster mit Schatten, Titelleiste und Schliessbox (links, 16 px) */
void ui_frame(int x, int y, int w, int h, const char *title);

int  ui_input(int x, int y, int w, char *buf, int max); /* 1 = Enter, 0 = Esc */

/* Modaler Dialog. b2 darf NULL sein. Rueckgabe 0 = b1, 1 = b2 */
void ui_busy(const char *title, const char *s);
int  ui_msgbox(const char *title, const char **lines, int n,
               const char *b1, const char *b2);

/* Testbau (-dMTEST): wartet eine Tastaturschleife und der Tastenpuffer ist leer,
 * Bild speichern und beenden (nur wenn RDESK mit /KT gestartet wurde) */
#ifdef MTEST
void test_idle(void);
#define KB_IDLE() test_idle()
#else
#define KB_IDLE()
#endif

#endif
