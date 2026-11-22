/* RD2.H - RetroDesk 2 Programmierschnittstelle, API 1.0
 * SPDX-License-Identifier: GPL-2.0-or-later  (c) 2026 Karsten, siehe COPYING
 *
 * Fuer Programme, die unter RetroDesk 2 laufen. Open Watcom C:
 *   16 Bit (8088 bis Pentium): wcl -0 -mm  ... rd2.lib rd2start.obj
 *   32 Bit (nur 386+, Modus P): wcl386     ... rd2_32.lib
 *
 * Grundsaetze
 *   - Alle API-Funktionen: Aufrufkonvention RDAPI (far pascal), Zeiger FAR.
 *   - Handles sind in beiden Modi 16 Bit gross (einfache Uebergaenge 16/32).
 *   - Koordinaten: short, Ursprung oben links, Pixel des Geraets.
 *   - Texte: Codepage 437, mit 0 abgeschlossen.
 *   - Wer GetMessage nicht regelmaessig aufruft, haelt alle Programme an.
 *   - Fensterfunktionen laufen immer auf dem Stack und mit DS des eigenen
 *     Programms; der Kern stellt das vor jedem Aufruf ein (kein Export noetig).
 */
#ifndef RD2_H
#define RD2_H

#define RD2_API_MAJOR 2
#define RD2_API_MINOR 3
#define RD2_API_VERSION ((RD2_API_MAJOR << 8) | RD2_API_MINOR)

#if defined(__386__) || defined(__FLAT__)
#  define RD2_32 1
#  define FAR
#else
#  define RD2_16 1
#  define FAR __far
#endif
#define RDAPI FAR __pascal
#define RDCALLBACK FAR __pascal

/* ---- Grundtypen ---------------------------------------------------------- */
typedef unsigned char  BYTE;
typedef unsigned short WORD;
typedef unsigned long  DWORD;
typedef short          BOOL;
#ifndef TRUE
#  define TRUE  1
#  define FALSE 0
#endif
typedef char FAR       *LPSTR;
typedef const char FAR *LPCSTR;
typedef DWORD          COLOR;                     /* 0x00BBGGRR */
#define RGB(r,g,b)     ((COLOR)(BYTE)(r) | ((COLOR)(BYTE)(g) << 8) | ((COLOR)(BYTE)(b) << 16))

typedef WORD HMEM, HWND, HDC, HFONT, HBMP, HMOD, HTASK, HINST, HMENU, HSOCK;

typedef void  (RDAPI *FARPROC)(void);
typedef DWORD (RDCALLBACK *WNDPROC)(HWND w, WORD msg, WORD p1, DWORD p2);
typedef BOOL  (RDCALLBACK *DLGPROC)(HWND dlg, WORD msg, WORD p1, DWORD p2);

#define LOWORD(l)  ((WORD)(DWORD)(l))
#define HIWORD(l)  ((WORD)((DWORD)(l) >> 16))
#define MAKELONG(lo,hi) ((DWORD)(WORD)(lo) | ((DWORD)(WORD)(hi) << 16))

/* ---- Strukturen ---------------------------------------------------------- */
#pragma pack(push, 1)
typedef struct { short x, y; } POINT;
typedef struct { short l, t, r, b; } RECT;        /* r, b gehoeren nicht mehr dazu */

typedef struct {
    HWND  w;
    WORD  msg;
    WORD  p1;
    DWORD p2;
    DWORD time;                                   /* TickCount beim Eintreffen */
    POINT pt;                                     /* Mausposition (Bildschirm) */
} MSG;

typedef struct {
    LPCSTR  name;                                 /* Klassenname, max. 15 Zeichen */
    WNDPROC proc;
    WORD    stil;                                 /* CS_* */
    WORD    daten;                                /* Bytes eigene Fensterdaten (Get/SetWindowData 0..) */
    HINST   inst;
    WORD    symbol;                               /* Ressourcen-Nr. Symbol (minimiert) */
    COLOR   hinter;                               /* Hintergrund, RD_DEFAULT = Systemfarbe */
} WNDCLASS;

typedef struct {
    HDC  dc;
    RECT neu;                                     /* ungueltiger Bereich (Client-Koordinaten) */
    BOOL loeschen;                                /* Hintergrund wurde schon geloescht */
} PAINT;

typedef struct {
    WORD  api;                                    /* API-Version des Kerns */
    WORD  modus;                                  /* RD_MODE_R / RD_MODE_P */
    WORD  cpu;                                    /* CPU_* */
    WORD  fpu;                                    /* 0 kein, 87, 287, 387 */
    WORD  mhz10;                                  /* Takt in 0,1 MHz (geschaetzt) */
    WORD  dos;                                    /* hi.lo, z. B. 0x0500 */
    WORD  kb_konv;                                /* konventioneller Speicher gesamt */
    WORD  kb_ems;                                 /* EMS gesamt, 0 = keins */
    DWORD kb_ext;                                 /* XMS/DPMI gesamt */
    WORD  grafik;                                 /* GFX_* */
    WORD  sound;                                  /* SC_* */
    WORD  maus;                                   /* Tasten (Niederbyte), 0 = keine Maus; Hochbyte: Tempo 1..5, Bit 15 Tasten getauscht (RD2.INI [Maus]) */
    BYTE  bootlw;                                 /* 1 = A: */
    BYTE  festplatte;                             /* 0 = Diskettenbetrieb */
    WORD  sprache;                                /* RD_LANG_* */
    char  home[66];                               /* RD2-Verzeichnis, z. B. "A:\\" */
} SYSINFO;

/* SysInfo (API 2.3): alles, was ein Programm ueber den Rechner wissen will. Vor dem Aufruf groesse
   = sizeof(RDSYSDATA) setzen; spaetere Fassungen haengen Felder nur hinten an. */
typedef struct {
    WORD  groesse;                                /* vom Programm: sizeof(RDSYSDATA) */
    WORD  cpu;                                    /* CPU_* wie KernInfo */
    WORD  fpu;                                    /* 0 kein, 87 */
    WORD  dos;                                    /* hi.lo */
    WORD  kb_konv;                                /* konventioneller Speicher gesamt */
    WORD  kb_frei;                                /* davon jetzt frei fuer Programme (groesster Block) */
    WORD  kb_ems;                                 /* EMS frei, 0 = keins */
    WORD  kb_xms;                                 /* XMS frei (bis 64 MB), 0 = keins */
    WORD  grafik;                                 /* laufende Grafikart GFX_* */
    WORD  sound;                                  /* SC_* laut RD2.INI */
    WORD  netz;                                   /* 1 = Paketreiber eingetragen */
    WORD  maus;                                   /* wie KernInfo */
    WORD  festplatte;                             /* 1 = RD2 laeuft von Festplatte */
    WORD  d3;                                     /* D3_* (RD2SETUP erkennt, RD2.INI [System] 3D=) */
    WORD  d3_hersteller, d3_geraet;               /* PCI-Kennung der 3D-Karte, 0 = keine */
} RDSYSDATA;
#define D3_KEINE   0                              /* nur Software (RD3D) */
#define D3_VOODOO1 1                              /* 3dfx Voodoo Graphics (Zusatzkarte, nur Vollbild) */
#define D3_VOODOO2 2                              /* 3dfx Voodoo 2 (Zusatzkarte, nur Vollbild) */
#define D3_BANSHEE 3                              /* 3dfx Voodoo Banshee */
#define D3_VOODOO3 4                              /* 3dfx Voodoo 3 */
#define D3_VOODOO4 5                              /* 3dfx Voodoo 4/5 (VSA-100) */
#define D3_RAGE    6                              /* ATI Rage (Mach64-Familie) */
#define D3_RAGE128 7                              /* ATI Rage 128 */
#define D3_ATI     8                              /* neuere ATI (Radeon) */
#define D3_RIVA128 9                              /* nVidia Riva 128 */
#define D3_TNT     10                             /* nVidia Riva TNT/TNT2/Vanta */
#define D3_NVIDIA  11                             /* neuere nVidia (GeForce) */

typedef struct {
    HTASK t;
    HINST i;
    char  name[16];
    WORD  kb;                                     /* belegter Speicher */
    WORD  flags;                                  /* TI_* */
} TASKINFO;

/* MemStats: aktuelle Werte; Bytes, nicht KB. RAM = RD2-Arena einschliesslich UMB,
 * ohne DOS/Kern ausserhalb der Arena; swap_used = gueltige Nutzdaten (Datei + XMS).
 * swap_file = Dateilaenge. swap_cached = ungenutzter Dateipuffer, keine gueltigen Daten.
 * swap_free = Puffer + moegliches Dateiwachstum nach Limit/Reserve + freies XMS.
 * Kapazitaet ohne Fragmentierungs-/E/A-Garantie; 0 bei 32 belegten Swapplaetzen.
 * Ermittelt freien Platz ueber DOS 36h; 0-Limit = unbegrenzt.
 * Ohne geladenes RDAUSL sind flags/max/reserve/Swapwerte 0. Keine INI-Abfrage. */
typedef struct {
    DWORD ram_total, ram_free, swap_used, swap_file, swap_cached, swap_free;
    WORD swap_maxkb, swap_reservekb, flags;
} MEMSTATS;
#define MS_SWAP 1

typedef struct { WORD jahr; BYTE monat, tag, wtag, std, min, sek, hsek; } RDTIME;

typedef struct {
    BYTE  res[21];                                /* intern (DOS-DTA) */
    BYTE  attr;
    WORD  zeit, datum;
    DWORD groesse;
    char  name[13];
} FINDDATA;

typedef struct {
    WORD  typ;                                    /* DT_* */
    WORD  flags;                                  /* DI_* */
    DWORD gesamt, frei;                           /* Bytes */
    DWORD serial;                                 /* Datentraeger-Seriennummer */
    char  label[12];
    WORD  format_kb;                              /* 360, 720, 1200, 1440, 2880; 0 = Festplatte */
} DRIVEINFO;

typedef struct {
    WORD  hoehe, oberlaenge, breite_max, breite_mittel;
    WORD  stil;                                   /* FS_* */
    BYTE  fest;                                   /* 1 = feste Breite */
    BYTE  erstes, letztes;                        /* Zeichenbereich */
    char  name[13];
} FONTINFO;

typedef struct {
    WORD   groesse;                               /* sizeof(FILEDLG) */
    WORD   flags;                                 /* FD_* */
    LPCSTR titel;
    LPCSTR filter;                                /* "Texte|*.TXT|Alle|*.*|" */
    LPSTR  datei;                                 /* Ein/Aus, voller Pfad */
    WORD   len;
} FILEDLG;

typedef struct {
    BYTE mod[5], car[5];                          /* OPL: 20h,40h,60h,80h,E0h */
    BYTE feedback;                                /* C0h */
    BYTE res;
} INSTR;

typedef struct { short x, y; WORD tasten; } JOYSTATE;

typedef struct {
    DWORD ip, maske, gateway, dns;
    BYTE  mac[6];
    WORD  flags;                                  /* NI_* */
} NETINFO;

typedef struct {
    WORD   groesse;
    WORD   flags;                                 /* PJ_* */
    LPCSTR name;                                  /* Dokumentname */
    WORD   kopien, von, bis;
    WORD   dpi_x, dpi_y;                          /* Aus: Aufloesung des Druckers */
    short  breite, hoehe;                         /* Aus: Druckbereich in Punkten */
} PRNJOB;
#pragma pack(pop)

/* ---- Konstanten ------------------------------------------------------------ */
#define RD_DEFAULT      0xFFFFFFFFUL
#define RD_NOBG         0xFFFFFFFEUL              /* WNDCLASS.hinter: Fenster fuellt selbst, RDUSER loescht nicht */

#define RD_MODE_R       1                         /* Real Mode, 8088+ */
#define RD_MODE_KOOP    0x0100                    /* Bit in SYSINFO.modus: RD2 /KOOP, keine Zeitscheiben (auch nicht fuer 32 Bit) */
#define RD_MODE_P       2                         /* Protected Mode, 386+ */
#define CPU_8088 0
#define CPU_8086 1
#define CPU_V20  2
#define CPU_186  3
#define CPU_286  4
#define CPU_386  5
#define CPU_486  6
#define CPU_586  7
#define GFX_CGA 1
#define GFX_HGC 2
#define GFX_VGAMONO 3
#define GFX_EGA 4
#define BMP_ROH 0x80                              /* BmpCreate: Hintergrund roh (nur BitBlt Schirm <-> Bitmap an dieselbe Stelle) */
#define BMP_NURVR 0x100                           /* BmpCreate mit BMP_ROH: nur im unsichtbaren Bildspeicher, sonst 0 (kein RAM) */
#define BMP_MASKE 0x200                           /* BmpCreate 4/8 Bit | BMP_MASKE: Maske (1 Bit) hinter den Punkten, BitBlt setzt nur deckende Punkte (Symbole); faellt weg, wenn der Treiber es nicht kann */
#define GFX_VGA16 5
#define GFX_VESA 6
#define GFX_EGA320 7                              /* 320x200 16 Farben (EGA/VGA Modus 0Dh), schmale Schrift SYSK.FNT */
#define GFX_EGAMONO 8                             /* EGA 640x350 1 Bit (Modus 10h) */
#define GFX_EGA4 9                                /* EGA 640x350 4 Farben (2 Ebenen) */
#define GFX_VGA4 10                               /* VGA 640x480 4 Farben (2 Ebenen) */
#define GFX_EGA200 11                             /* EGA 640x200 16 Farben (Modus 0Eh, 64 KB) */
#define GFX_SVGA16 12                             /* VESA 800x600 16 Farben (Modus 102h) */
#define GFX_MCGA 13                               /* MCGA/VGA 320x200 256 Farben (Modus 13h) */
#define GFX_VESA800 14                            /* VESA 800x600 256 Farben (Modus 103h) */
#define GFX_VESA1024 15                           /* VESA 1024x768 256 Farben (Modus 105h) */
#define GFX_CGA4 16                               /* CGA 320x200 4 Farben (Modus 4) */

/* GfxList: eine Zeile je moeglicher Grafikart (Menue "Grafik") */
typedef struct {
    WORD  typ;                                    /* GFX_* */
    WORD  w, h, farben;
    WORD  flags;                                  /* GI_* */
    char  name[14];                               /* Anzeigename, z. B. "VGA mono" */
} GFXINFO;
#define GI_AKTIV    0x0001                        /* laeuft gerade */
#define GI_GEWAEHLT 0x0002                        /* in RD2.INI fest eingestellt */
#define GI_STANDARD 0x0004                        /* waere die automatische Wahl */
#define RD_LANG_DE 0x0407
#define RD_LANG_EN 0x0409

/* Fehler (LastError) */
#define RD_OK           0
#define RD_ERR_NOMEM    1
#define RD_ERR_HANDLE   2
#define RD_ERR_NOTFOUND 3
#define RD_ERR_DISK     4                         /* Lese-/Schreibfehler */
#define RD_ERR_NODISK   5                         /* Diskette fehlt / abgebrochen */
#define RD_ERR_WPROT    6                         /* schreibgeschuetzt */
#define RD_ERR_FULL     7
#define RD_ERR_NOTSUP   8                         /* Funktion/Treiber nicht vorhanden */
#define RD_ERR_FORMAT   9
#define RD_ERR_VERSION  10                        /* Programm braucht neuere API */
#define RD_ERR_NET      11
#define RD_ERR_TIMEOUT  12
#define RD_ERR_ABORT    13                        /* Benutzer hat abgebrochen */
#define RD_ERR_BUSY     14

/* Speicher */
#define MEM_FIXED       0x0000
#define MEM_MOVABLE     0x0002
#define MEM_ZEROINIT    0x0040
#define MEM_DISCARDABLE 0x0100
#define MEM_SHARE       0x2000                    /* gehoert keinem Task (Ablage, Nachrichten) */
#define MEM_NOEMS       0x4000                    /* nie nach EMS auslagern (DMA-Puffer) */
#define MEMA_KONV  0                              /* frei konventionell */
#define MEMA_GROESSTER 1
#define MEMA_EMS   2
#define MEMA_EXT   3

/* MemError: Zusatzgrund nach fehlgeschlagenem MemAlloc/MemLock/MemRealloc/MemFixed.
 * LastError bleibt RD_ERR_NOMEM; nach erfolgreichem Speicherzugriff MEMERR_NONE.
 * Vor weiteren Speicheraufrufen abfragen. Je Task getrennt (API 2.1). */
#define MEMERR_NONE       0
#define MEMERR_RAM        1                       /* kein geeigneter RAM-Bereich / Handle mehr frei */
#define MEMERR_SWAPLIMIT  2                       /* eingestellte maximale Dateigroesse erreicht */
#define MEMERR_RESERVE    3                       /* Freiplatzreserve auf dem Laufwerk */
#define MEMERR_DISK       4                       /* Swapdatei nicht lesbar/schreibbar oder Laufwerk unbekannt */
#define MEMERR_SLOTS      5                       /* alle 32 Auslagerplaetze belegt */

/* Dateien */
#define FO_READ   0x00
#define FO_WRITE  0x01
#define FO_RW     0x02
#define FO_DENYNONE 0x40
#define FS_SET 0
#define FS_CUR 1
#define FS_END 2
/* Dateiattribute (FINDDATA.attr, FileAttr) */
#define FA_RDONLY 0x01
#define FA_HIDDEN 0x02
#define FA_SYSTEM 0x04
#define FA_LABEL  0x08
#define FA_DIR    0x10
#define FA_ARCH   0x20
#define DT_NONE 0
#define DT_FLOPPY 1
#define DT_HARD 2
#define DT_NET 3
#define DT_RAM 4
#define DT_CDROM 5
#define DI_WPROT  0x0001
#define DI_CHANGE 0x0002                          /* Wechsel erkannt seit letzter Abfrage */
#define DI_PHANTOM 0x0004                         /* B: auf dem Laufwerk von A: */
#define DI_NURTYP 0x8000                          /* DriveInfo(laufwerk | DI_NURTYP): nur Typ und Phantom, ohne Zugriff */
#define RD_DOS_KEEP   0x0001                      /* Zustand sichern und danach weitermachen */
#define RD_DOS_PAUSE  0x0002                      /* "Taste druecken" nach Programmende */
#define RD_DOS_GFX    0x0004                      /* Programm stellt Grafik selbst ein */
#define RD_DOS_NEU    0x0008                      /* prog "": alle Programme enden, RD2 startet mit derselben Befehlszeile neu */

/* Nachrichten */
#define M_NULL        0x0000
#define M_CREATE      0x0001                      /* p2 = Parameter aus CreateWindow */
#define M_DESTROY     0x0002
#define M_MOVE        0x0003
#define M_SIZE        0x0005                      /* p1 = SIZE_*, p2 = MAKELONG(b, h) */
#define M_ACTIVATE    0x0006                      /* p1 = 1 aktiv */
#define M_SETFOCUS    0x0007
#define M_KILLFOCUS   0x0008
#define M_ENABLE      0x000A
#define M_PAINT       0x000F
#define M_CLOSE       0x0010
#define M_QUIT        0x0012
#define M_ERASEBKGND  0x0014
#define M_SHOW        0x0018
#define M_KEY         0x0100                      /* p1 = Taste (K_*), p2 lo = Wiederholung, hi = KS_* */
#define M_KEYUP       0x0101
#define M_CHAR        0x0102                      /* p1 = Zeichen (CP437) */
#define M_COMMAND     0x0111                      /* p1 = ID, p2 lo = HWND Steuerelement, hi = Meldung */
#define M_TIMER       0x0113                      /* p1 = ID */
#define M_SCROLL      0x0114                      /* p1 = SB_LINEUP.., p2 lo = Position, hi = Balken */
#define M_MENUOPEN    0x0116                      /* vor dem Aufklappen: Haekchen setzen */
#define M_MOUSEMOVE   0x0200                      /* p1 = Tasten, p2 = MAKELONG(x, y) Client */
#define M_BUTTONDOWN  0x0201                      /* p1 = Taste 1/2/3 | KS_* << 8 */
#define M_BUTTONUP    0x0202
#define M_DBLCLK      0x0203
#define M_DROPFILES   0x0233                      /* p2 = HMEM mit Dateiliste (0-getrennt, 00 am Ende) */
#define M_SOUNDDONE   0x0300                      /* p1 = 1 Musik, 2 Sample zu Ende (nicht nach ...Stop), 3 Haelfte p2 (0/1) gespielt (SM_HALBE) */
#define M_NETSTATE    0x0310                      /* p1 = NS_* des Netzes */
#define M_NETRESOLVED 0x0311                      /* p1 = 0 ok, p2 = IP */
#define M_NETCONNECT  0x0312                      /* p1 = HSOCK, p2 = Fehler */
#define M_NETDATA     0x0313                      /* p1 = HSOCK, p2 = Bytes bereit */
#define M_NETCLOSED   0x0314
#define M_NETACCEPT   0x0315                      /* p1 = neuer HSOCK */
#define M_DISKCHANGE  0x0320                      /* p1 = Laufwerk 1 = A: */
#define M_SETTINGS    0x0321                      /* Einstellungen geaendert (Farben, Sprache) */
#define M_INITDIALOG  0x0330
#define M_SYSMENU     0x0331                      /* an den Schreibtisch: Menue "RD" angeklickt, p1 = 1 Maus */
#define M_SCREENLOST  0x0322                      /* Vollbild (API 2.3): Alt+Tab, der Schirm gehoert RD2; nicht zeichnen, Klang still */
#define M_SCREENBACK  0x0323                      /* Vollbild zurueck: ScreenRestore() und Bild neu zeichnen */
#define M_USER        0x0400                      /* ab hier eigene */

/* Steuerelement-Meldungen (M_COMMAND hi-Wort von p2) */
#define N_CLICKED 0
#define N_CHANGE  1
#define N_SELECT  2
#define N_DBLCLK  3
#define N_ENTER   4
#define N_RCLICK  5                               /* LIST: rechte Maustaste (Eintrag schon gewaehlt) */
/* Nachrichten an Steuerelemente (DlgSetText usw. benutzen sie) */
#define C_SETTEXT  0x0340                         /* p2 = Text */
#define C_GETTEXT  0x0341                         /* p1 = Puffergroesse, p2 = Puffer; liefert Laenge */
#define C_SETCHECK 0x0342                         /* p1 = 0/1 */
#define C_GETCHECK 0x0343
#define C_SETLIMIT 0x0344                         /* EDIT: hoechstens p1 Zeichen (1..80); TEXT: p1 bis 8192, p2 = MAKELONG(Spalten, Zeilen) */
#define C_CLICK    0x0345                         /* wie ein Mausklick */
#define C_MNEMO    0x0346                         /* liefert den Buchstaben hinter & (gross) */
#define C_SETPOS   0x0347                         /* PROGRESS: p2 = Stand */
#define C_SETRANGE 0x0348                         /* PROGRESS/SCROLL: p2 = Hoechstwert (Vorgabe 100) */
#define C_GETPOS   0x0349                         /* SCROLL: liefert den Stand */
#define L_ADD      0x0350                         /* LIST: p2 = Text, liefert Index oder 0xFFFF */
#define L_CLEAR    0x0351
#define L_COUNT    0x0352
#define L_GETSEL   0x0353                         /* 0xFFFF = keine Auswahl */
#define L_SETSEL   0x0354                         /* p1 = Index (0xFFFF = keine) */
#define L_GETTEXT  0x0355                         /* p1 = Index, p2 = Puffer (33 Zeichen); C_GETTEXT = Auswahl */
#define L_DELETE   0x0356                         /* p1 = Index */

/* Fensterstile */
#define WS_TITLE    0x00000001UL
#define WS_CLOSE    0x00000002UL
#define WS_SIZE     0x00000004UL
#define WS_MIN      0x00000008UL
#define WS_VSCROLL  0x00000010UL
#define WS_HSCROLL  0x00000020UL
#define WS_BORDER   0x00000040UL
#define WS_CHILD    0x00000080UL
#define WS_POPUP    0x00000100UL
#define WS_VISIBLE  0x00000200UL
#define WS_DISABLED 0x00000400UL
#define WS_DIALOG   0x00000800UL
#define WS_TOOL     0x00001000UL                  /* Zubehoer: kein eigenes Menue */
#define WS_DESKTOP  0x00002000UL                  /* Schreibtisch der Shell: bleibt unten, bekommt M_SYSMENU */
#define WS_NORMAL   (WS_TITLE | WS_CLOSE | WS_SIZE | WS_MIN | WS_BORDER)
#define ES_DEFAULT  0x00010000UL                  /* BUTTON: Standardknopf (Eingabetaste) */
#define ES_CHECKED  0x00020000UL                  /* CHECK/RADIO: vorbelegt angehakt */
#define ES_PASSWORD 0x00040000UL                  /* EDIT: Sternchen */
#define ES_NUMBER   0x00080000UL                  /* EDIT: nur Ziffern */
#define ES_READONLY 0x00100000UL                  /* EDIT/TEXT: nur lesen */
/* Menue-Ressource: Eintraege WORD flags, WORD id, Text\0; MF_END beim letzten Eintrag jeder Ebene */
#define MF_POPUP 0x0001
#define MF_SEP   0x0002
#define MF_END   0x0004
#define MF_GRAY  0x0008
#define MF_CHECK 0x0010
#define MF_HIDE  0x0020                           /* ausgeblendet: MenuText(m, id, 0) */
#define CS_DBLCLKS  0x0001
#define CS_SAVEBITS 0x0002                        /* Hintergrund sichern (kleine Popups) */
#define CS_SYSTEM   0x8000                        /* Systemklasse: bleibt nach dem Programmende (RDCTL) */
#define CW_DEFAULT  ((short)0x8000)
#define SW_HIDE 0
#define SW_SHOW 1
#define SW_MIN  2
#define SW_MAX  3
#define SW_RESTORE 4
#define SIZE_NORMAL 0
#define SIZE_MIN 1
#define SIZE_MAX 2
#define WD_PROC   (-1)
#define WD_INST   (-2)
#define WD_ID     (-3)
#define WD_STIL   (-4)
#define WD_ZORDER (-5)   /* nur Setzen, Kindfenster: direkt unter Geschwister v (0 = ganz oben); liefert das Geschwister, das vorher darueber lag */
#define SB_HORZ 0
#define SB_VERT 1
#define SB_LINEUP 0
#define SB_LINEDOWN 1
#define SB_PAGEUP 2
#define SB_PAGEDOWN 3
#define SB_THUMB 4
#define SB_TOP 5
#define SB_BOTTOM 6

/* Standard-Steuerelementklassen */
#define CL_BUTTON   "BUTTON"
#define CL_CHECK    "CHECK"
#define CL_RADIO    "RADIO"
#define CL_STATIC   "STATIC"
#define CL_EDIT     "EDIT"
#define CL_TEXT     "TEXT"                        /* mehrzeilig */
#define CL_LIST     "LIST"
#define CL_COMBO    "COMBO"
#define CL_SCROLL   "SCROLL"
#define CL_PROGRESS "PROGRESS"
#define CL_GROUP    "GROUP"

/* MsgBox */
#define MB_OK       0x0000
#define MB_OKCANCEL 0x0001
#define MB_YESNO    0x0004
#define MB_YESALLNOCANCEL 0x0005                  /* Ja, Alle, Nein, Abbruch (Ersetzen beim Kopieren, 01.11.2026) */
#define MB_YESALLNO 0x0006                          /* Ja, Alle, Nein */
#define MB_YESNOCANCEL 0x0003
#define MB_INFO     0x0040
#define MB_WARN     0x0030
#define MB_ERROR    0x0010
#define MB_ASK      0x0020
#define MB_DEFNO    0x0100                          /* Nein ist vorgewaehlt (Eingabe = Nein), fuer Rueckfragen mit Folgen */
#define ID_OK     1
#define ID_CANCEL 2
#define ID_YES    6
#define ID_NO     7
#define ID_ALLE   8                               /* MsgBox: Ja fuer alle */
#define FD_OPEN   0x0000
#define FD_SAVE   0x0001
#define FD_MUSTEXIST 0x0002
#define FD_ASKOVER   0x0004

/* Tasten (M_KEY p1): ASCII bzw. 0x100 + Scancode wie bei RDESK 1.x */
#define K_ESC    27
#define K_ENTER  13
#define K_TAB    9
#define K_BACK   8
#define K_F1     0x13B
#define K_F2     0x13C
#define K_F3     0x13D
#define K_F4     0x13E
#define K_F5     0x13F
#define K_F6     0x140
#define K_F7     0x141
#define K_F8     0x142
#define K_F9     0x143
#define K_F10    0x144
#define K_UP     0x148
#define K_DOWN   0x150
#define K_LEFT   0x14B
#define K_RIGHT  0x14D
#define K_HOME   0x147
#define K_END    0x14F
#define K_PGUP   0x149
#define K_PGDN   0x151
#define K_INS    0x152
#define K_DEL    0x153
#define KS_SHIFT 0x01
#define KS_CTRL  0x04
#define KS_ALT   0x08

/* Grafik */
#define CAPS_W      0                             /* Breite in Pixeln */
#define CAPS_H      1
#define CAPS_BPP    2                             /* Bit pro Pixel: 1, 4, 8 */
#define CAPS_FARBEN 3
#define CAPS_ASPX   4                             /* Seitenverhaeltnis Pixel x:y (100 = quadratisch) */
#define CAPS_ASPY   5
#define CAPS_DPI    6
#define CAPS_TEXTH  7                             /* Systemschrift Hoehe */
#define CAPS_TECH   8                             /* 0 Schirm, 1 Speicher, 2 Drucker */
#define CAPS_GFX    9                             /* laufende Grafikart GFX_* (31.10.2026) */
#define C_BLACK  RGB(0,0,0)
#define C_WHITE  RGB(255,255,255)
#define C_GRAY   RGB(128,128,128)
#define C_LTGRAY RGB(192,192,192)
#define C_FENSTER    0xFF000001UL                 /* Systemfarben, vom Schema abhaengig */
#define C_TEXT       0xFF000002UL
#define C_MARKIERT   0xFF000003UL
#define C_TITEL      0xFF000004UL
#define C_DESKTOP    0xFF000005UL
#define PAT_SOLID 0
#define PAT_GRAY50 1
#define PAT_GRAY25 2
#define PAT_GRAY75 3
#define PAT_HATCH 4
#define PAT_NONE  0xFFFF
#define ROP_COPY 0
#define ROP_OR   1
#define ROP_AND  2
#define ROP_XOR  3
#define ROP_NOT  4                                /* Ziel invertieren */
#define ROP_MASK 5                                /* Quelle als Maske (Symbole) */
#define BK_TRANSPARENT 0
#define BK_OPAQUE 1
#define TB_LEFT 0
#define TB_CENTER 1
#define TB_RIGHT 2
#define TB_WRAP 4
#define TB_VCENTER 8
#define FS_NORMAL 0
#define FS_BOLD   1
#define FS_ITALIC 2
#define FS_UNDER  4
#define CUR_ARROW 0
#define CUR_WAIT  1
#define CUR_TEXT  2
#define CUR_CROSS 3
#define CUR_HAND  4

/* Sound */
#define SC_SPEAKER 0x0001
#define SC_OPL2    0x0002
#define SC_OPL3    0x0004
#define SC_PCM     0x0008
#define SC_MIDI    0x0010
#define SM_LOOP    0x0001                         /* SndMusicPlay: endlos wiederholen */
#define SM_IMF700  0x0002                         /* IMF mit 700 Hz (Wolfenstein 3D), sonst 560 Hz (Keen) */
#define SM_IMF280  0x0004                         /* IMF mit 280 Hz (Duke Nukem II) */
#define SM_HALBE   0x0008                         /* SndSamplePlay: jede Blockhaelfte melden (M_SOUNDDONE p1 = 3), mit SM_LOOP Ringpuffer */

/* Netz */
#define NO_TCP 0
#define NO_UDP 1
#define NO_GROSS 2                                /* TCP mit 8 KB Empfangsfenster (Datenkanal), sonst 4 KB */
#define NS_CLOSED 0
#define NS_CONNECTING 1
#define NS_OPEN 2
#define NS_CLOSING 3
#define NS_ERROR 4
#define NI_DHCP 0x0001
#define NI_UP   0x0002

/* Drucker */
#define PJ_DIALOG 0x0001
#define PJ_QUER   0x0002
#define PJ_ENTWURF 0x0004
#define PRN_NOCHMAL 1                             /* PrnPage: Seite noch einmal zeichnen (naechstes Band oder Kopie) */

/* Zwischenablage */
#define CF_TEXT 1
#define CF_BITMAP 2
#define CF_FILES 3

/* Ressourcentypen */
#define RT_MENU 1
#define RT_DIALOG 2
#define RT_STRING 3
#define RT_ICON 4
#define RT_BITMAP 5
#define RT_CURSOR 6
#define RT_FONT 7
#define RT_DATA 8
#define RT_ACCEL 9
/* Vollbild (API 2.3, PLAN_32BIT Schritt 2): ScreenAcquire(fenster), dann ScreenMode; Alt+Tab schickt M_SCREENLOST,
   zurueck M_SCREENBACK (dann ScreenRestore). ScreenMode/ScreenPalette/ScreenRestore/ScreenBank liegen in RD2.LIB (VOLL.C). */
#define SM_TEXT   0                               /* 80x25 Text, B800 */
#define SM_CGA4   1                               /* 320x200 4 Farben (BIOS 04h), B800, Zeilen verschraenkt */
#define SM_EGA    2                               /* 320x200 16 Farben (0Dh), A000, 4 Ebenen */
#define SM_VGA16  3                               /* 640x480 16 Farben (12h), A000, 4 Ebenen */
#define SM_13H    4                               /* 320x200 256 Farben (13h), A000, linear */
#define SM_X      5                               /* 320x240 256 Farben (Mode X), A000, 4 Ebenen */
#define SM_VESA   6                               /* 640x480 256 Farben (VESA 101h), A000, 64-KB-Fenster (ScreenBank) */
typedef struct { WORD modus, w, h, bpp, zeilenbytes, seg, ebenen; } SCREENINFO;
WORD RDAPI ScreenMode(WORD modus, SCREENINFO FAR *si);   /* 1 = ok; merkt den Modus fuer ScreenRestore */
void RDAPI ScreenPalette(WORD erste, WORD n, const BYTE FAR *rgb);   /* je 3 Bytes 0..63 (VGA-DAC), gemerkt */
void RDAPI ScreenRestore(void);                   /* nach M_SCREENBACK: Modus und Palette wie zuletzt */
void RDAPI ScreenBank(WORD bank);                 /* SM_VESA: 64-KB-Fenster waehlen */
#define RT_BEDARF 10                              /* BEDARF-Zeile in der .RC (API 2.3), Nr. 1, RDBEDARF */
#pragma pack(push, 1)
typedef struct {
    WORD  version;                                /* 1 */
    BYTE  cpu;                                    /* mindestens CPU_* */
    BYTE  fpu;                                    /* 1 = 8087 noetig */
    BYTE  farben;                                 /* 0 egal, 1 = 4, 2 = 16, 3 = 256 Farben */
    BYTE  netz;                                   /* 1 = Paketreiber noetig */
    BYTE  d3;                                     /* 0 egal, 1 = Software-3D, 2 = 3D-Karte */
    BYTE  frei;
    WORD  kb_ram;                                 /* freier Speicher in KB */
    WORD  kb_xms;                                 /* freies XMS in KB */
    WORD  klang;                                  /* SC_*, alle noetig */
} RDBEDARF;
#pragma pack(pop)
#define BD_CPU    0x0001                          /* BedarfCheck: was fehlt */
#define BD_FPU    0x0002
#define BD_FARBEN 0x0004
#define BD_RAM    0x0008
#define BD_XMS    0x0010
#define BD_KLANG  0x0020
#define BD_NETZ   0x0040
#define BD_3D     0x0080

/* ---- Funktionen ------------------------------------------------------------ */
#ifndef RD2_KERNEL                                /* der Kern deklariert selbst (mit __loadds) */
#define RDF(n, ret, name, args) ret RDAPI name args;
#include "RD2API.H"

/* Hilfen aus RD2.LIB (laufen im Programm, kein API-Aufruf) */
HMEM RDAPI SndWavLoad(LPCSTR name, WORD FAR *rate);   /* WAV (PCM 8 Bit mono) -> Block nur mit den Samples fuer SndSamplePlay */
#undef RDF
#endif

/* Einsprung des Programms (statt main). cmd = Befehlszeile, show = SW_* */
short RDAPI RdMain(HINST inst, LPCSTR cmd, WORD show);

/* Module (.MOD/.DRV): Einsprung liefert die Exporttabelle, Exporte mit RDEXPORT.
   Module mit -zu bauen (sie laufen mit dem Stack des Aufrufers). */
#define RDEXPORT FAR __pascal __loadds
void FAR * RDAPI RdModInit(void);                 /* -> { WORD anzahl; FARPROC f[anzahl]; } */
#define PF_32       0x0001
#define PF_TOOL     0x0002
#define PF_SINGLE   0x0004
#define PF_FPU      0x0008
#define PF_NET      0x0010
#define PF_EMS      0x0020
#define PF_NOEMS    0x0040
#define PF_DISCARD  0x0080                        /* Modul: darf verworfen und nachgeladen werden */

/* Kopf, den RD2START.OBJ ins Programm legt; Werte per #define vor dem
   Einbinden von RD2.H aenderbar, siehe SPEC.md Abschnitt 2. */
#ifndef RD2_STACK
#  define RD2_STACK 4096
#endif

#endif /* RD2_H */
