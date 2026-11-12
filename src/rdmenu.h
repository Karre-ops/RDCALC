/* RetroDesk - Menueleiste und Dialoge mit Hintergrundsicherung (fuer RDEDIT u.a.) */
#ifndef RDMENU_H
#define RDMENU_H

typedef struct {
    const char *name;
    const char **items;     /* "Kopieren\tStrg+C": Text links, Kurztaste rechts */
    int n, x;
} UIMENU;

#define UI_MBAR (gfx_fh + 3)

void ui_menubar(UIMENU *m, int n);
int  ui_menu_hit(UIMENU *m, int n, int mx);     /* Menue unter x oder -1 */
int  ui_menu(UIMENU *m);                        /* Eintrag oder -1 */
/* Menueleiste bedienen, wechselt zwischen Menues. kb: 0 = Maustaste gehalten (Loslassen
 * auf dem Titel laesst es offen), 1 = Tastatur. Rueckgabe Menue*256+Eintrag oder -1 */
int  ui_menu_run(UIMENU *m, int n, int start, int kb);
extern int ui_menu_key, ui_menu_mx;   /* intern: 1 Tastatur, 2 offen; -2/-3/-4 = links/rechts/Leiste */

/* Dialog mit bis zu 3 Knoepfen (b2/b3 duerfen NULL sein).
 * Enter = Knopf mit Fokus (Tab/Pfeile wechseln), Esc = letzter Knopf. Hintergrund wird wiederhergestellt. */
int  ui_dlg(const char *title, const char **lines, int n,
            const char *b1, const char *b2, const char *b3);
/* Eingabedialog, 1 = OK */
int  ui_inputbox(const char *title, const char *prompt, char *buf, int max);

/* Liefert gedrueckte Taste: ASCII, 0x100+Scancode fuer Sondertasten, -1 = keine */
int  ui_pollkey(void);

#endif
