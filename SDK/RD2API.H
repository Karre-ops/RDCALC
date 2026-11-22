/* RD2API.H - Liste aller RD2-Funktionen mit fester Nummer (API 2.3)
 * SPDX-License-Identifier: GPL-2.0-or-later  (c) 2026 Karsten, siehe COPYING
 *
 * Diese Datei ist die einzige Quelle fuer Nummern und Signaturen.
 * Sie wird mit verschiedenen Definitionen von RDF() mehrfach eingebunden:
 *   - RD2.H:     Prototypen fuer Programme
 *   - RD2.LIB:   Aufrufstuecke (call far [api + nummer*4])
 *   - Kern:      Sprungtabelle
 *   - INT 7Eh:   AX = Nummer, Parameter auf dem Stack (pascal)
 *
 * API 2.0 (30.10.2026): Nummern dicht neu vergeben, Tabelle im Kern 1,4 statt 4 KB.
 *   Programme fuer API 1.x muessen neu gebaut werden.
 *
 * REGELN ab API 2.0 (nie brechen, sonst laufen alte Programme nicht mehr):
 *   1. Eine vergebene Nummer wird nie neu belegt und nie entfernt.
 *   2. Signaturen werden nie geaendert. Neue Faehigkeit = neue Funktion.
 *   3. Neue Funktionen bekommen die naechste freie Nummer ihrer Gruppe
 *      (je Gruppe 8 frei, Grenzen in tools/apicheck.py) und erhoehen RD2_API_MINOR.
 *      Systemmodule belegen Bloecke ab der ersten Funktion (RD.C k_sysmod).
 *   4. Nicht (mehr) unterstuetzte Funktionen liefern RD_ERR_NOTSUP.
 *
 * Gruppen:  000-07F Kern   080-0BF Speicher   0C0-0FF Datei
 *           100-17F Grafik 180-1FF reserviert 200-2BF Oberflaeche
 *           2C0-2FF Dialoge 300-33F Sound     340-37F Eingabe
 *           380-3BF Netz   3C0-3DF Drucker    3E0-3FF Seriell (spaeter)
 *
 * RDF(nummer, rueckgabe, name, (parameter))
 */

/* ---- Kern: Version, System, Tasks, Module ---------------------------- */
RDF(0x000, WORD,    KernVersion,    (void))                       /* hi=major lo=minor */
RDF(0x001, WORD,    KernInfo,       (SYSINFO FAR *si))            /* 0 = ok */
RDF(0x002, HTASK,   TaskSelf,       (void))
RDF(0x003, void,    TaskYield,      (void))
RDF(0x004, HTASK,   TaskExec,       (LPCSTR app, LPCSTR cmd, WORD show))
RDF(0x005, WORD,    TaskKill,       (HTASK t, WORD code))
RDF(0x006, HTASK,   TaskFirst,      (TASKINFO FAR *ti))
RDF(0x007, HTASK,   TaskNext,       (HTASK t, TASKINFO FAR *ti))
RDF(0x008, HMOD,    ModLoad,        (LPCSTR name))                /* .MOD/.DRV nachladen */
RDF(0x009, void,    ModFree,        (HMOD m))
RDF(0x00A, FARPROC, ModProc,        (HMOD m, WORD nummer))
RDF(0x00B, WORD,    RunDos,         (LPCSTR prog, LPCSTR cmd, WORD flags)) /* RD_DOS_* */
RDF(0x00C, WORD,    LastError,      (void))                       /* RD_ERR_* */
RDF(0x00D, void,    SetLastError,   (WORD e))
RDF(0x00E, void,    ExitRD,         (WORD flags))                 /* Shell: RetroDesk beenden */
RDF(0x031, void,    TaskHalt,       (HTASK t, BOOL an))           /* Vollbild geparkt: keine Zeitscheiben, Klang still (API 2.3) */
RDF(0x00F, DWORD,   InstRes,        (HINST i, LPSTR pfad, WORD len)) /* Datei und RD2-Block eines Programms (fuer Ressourcen), 0 = keine */

/* ---- Kern: Nachrichten ------------------------------------------------ */
RDF(0x010, BOOL,    GetMessage,     (MSG FAR *m))                 /* FALSE bei M_QUIT */
RDF(0x011, BOOL,    PeekMessage,    (MSG FAR *m, WORD remove))
RDF(0x012, BOOL,    PostMessage,    (HWND w, WORD msg, WORD p1, DWORD p2))
RDF(0x013, DWORD,   SendMessage,    (HWND w, WORD msg, WORD p1, DWORD p2))
RDF(0x014, DWORD,   DispatchMessage,(const MSG FAR *m))
RDF(0x015, void,    PostQuit,       (WORD code))
RDF(0x016, void,    WaitMessage,    (void))
RDF(0x017, BOOL,    PostTask,       (HTASK t, WORD msg, WORD p1, DWORD p2))
RDF(0x018, WORD,    MsgRegister,    (LPCSTR name))                /* eigene Nachricht fuer Programme untereinander */

/* ---- Kern: Zeit ---------------------------------------------------------- */
RDF(0x019, WORD,    TimerSet,       (HWND w, WORD id, WORD ms))
RDF(0x01A, void,    TimerKill,      (HWND w, WORD id))
RDF(0x01B, DWORD,   TickCount,      (void))                       /* ms seit Start */
RDF(0x01C, void,    TimeGet,        (RDTIME FAR *t))
RDF(0x01D, WORD,    TimeSet,        (const RDTIME FAR *t, WORD bios))
RDF(0x01E, DWORD,   InputIdle,      (void))                       /* ms seit der letzten Taste/Mausaktion (Bildschirmschoner) */

/* ---- Kern: Profil, Zwischenablage, Ressourcen, Sprache ----------------- */
RDF(0x01F, WORD,    ProfileGetStr,  (LPCSTR file, LPCSTR sec, LPCSTR key, LPCSTR def, LPSTR buf, WORD len))
RDF(0x020, short,   ProfileGetInt,  (LPCSTR file, LPCSTR sec, LPCSTR key, short def))
RDF(0x021, WORD,    ProfileSetStr,  (LPCSTR file, LPCSTR sec, LPCSTR key, LPCSTR val))
RDF(0x022, BOOL,    ClipOpen,       (HWND owner))
RDF(0x023, BOOL,    ClipSet,        (WORD format, HMEM data))     /* Block gehoert dann der Ablage */
RDF(0x024, HMEM,    ClipGet,        (WORD format))
RDF(0x025, void,    ClipClose,      (void))
RDF(0x026, void,    ClipEmpty,      (void))
RDF(0x027, HMEM,    ResLoad,        (HINST i, WORD typ, WORD id))
RDF(0x028, void,    ResFree,        (HMEM r))
RDF(0x029, WORD,    StrLoad,        (HINST i, WORD id, LPSTR buf, WORD len)) /* aus .LNG bzw. Ressource */
RDF(0x02A, WORD,    LangGet,        (void))                       /* RD_LANG_* */
RDF(0x02B, LPCSTR,  StrGet,         (HINST i, WORD id))           /* Zeiger gilt bis zum naechsten GetMessage */

/* ---- Kern: Fehlersuche ------------------------------------------------- */
RDF(0x02C, void,    DbgPrint,       (LPCSTR text))                /* COM1 oder RD2DBG.TXT */
RDF(0x02D, void,    DbgHex,         (LPCSTR text, DWORD wert))
RDF(0x02E, void,    DbgBreak,       (void))                       /* INT 3 wenn Debugger da */

/* ---- Kern: Rechner (API 2.3) --------------------------------------------- */
RDF(0x02F, WORD,    SysInfo,        (RDSYSDATA FAR *sd))          /* 0 = ok; vorher sd->groesse setzen */
RDF(0x030, WORD,    BedarfCheck,    (LPCSTR datei))               /* BD_* was fehlt, 0 = passt oder kein BEDARF */

/* ---- Speicher ------------------------------------------------------------ */
RDF(0x037, HMEM,    MemAlloc,       (DWORD size, WORD flags))     /* MEM_* */
RDF(0x038, HMEM,    MemFree,        (HMEM h))                     /* 0 = ok */
RDF(0x039, void FAR *, MemLock,     (HMEM h))                     /* 0 = verworfen/Fehler */
RDF(0x03A, BOOL,    MemUnlock,      (HMEM h))
RDF(0x03B, HMEM,    MemRealloc,     (HMEM h, DWORD size, WORD flags))
RDF(0x03C, DWORD,   MemSize,        (HMEM h))
RDF(0x03D, WORD,    MemFlags,       (HMEM h))
RDF(0x03E, DWORD,   MemAvail,       (WORD art))                   /* MEMA_* in Bytes */
RDF(0x03F, DWORD,   MemCompact,     (DWORD wunsch))               /* liefert groessten freien Block */
RDF(0x040, void FAR *, MemFixed,    (WORD size))                  /* kleiner fester Block, wie malloc */
RDF(0x041, void,    MemFixedFree,   (void FAR *p))
RDF(0x042, WORD,    MemError,       (void))                       /* MEMERR_* der letzten Speicheroperation, API 2.1 */

RDF(0x043, WORD,    MemStats,       (MEMSTATS FAR *info))          /* 0 = ok, aktuelle RAM-/Swapwerte, API 2.1 */

/* ---- Dateien und Laufwerke ---------------------------------------------- */
RDF(0x04A, short,   FileOpen,       (LPCSTR name, WORD mode))     /* FO_*, -1 = Fehler */
RDF(0x04B, short,   FileCreate,     (LPCSTR name, WORD attr))
RDF(0x04C, WORD,    FileRead,       (short f, void FAR *buf, WORD n))
RDF(0x04D, WORD,    FileWrite,      (short f, const void FAR *buf, WORD n))
RDF(0x04E, long,    FileSeek,       (short f, long pos, WORD art))
RDF(0x04F, WORD,    FileClose,      (short f))
RDF(0x050, WORD,    FileDelete,     (LPCSTR name))
RDF(0x051, WORD,    FileRename,     (LPCSTR alt, LPCSTR neu))
RDF(0x052, WORD,    FileFindFirst,  (LPCSTR muster, WORD attr, FINDDATA FAR *fd))
RDF(0x053, WORD,    FileFindNext,   (FINDDATA FAR *fd))
RDF(0x054, WORD,    FileAttr,       (LPCSTR name, WORD neu, WORD setzen))
RDF(0x055, WORD,    DirMake,        (LPCSTR name))
RDF(0x056, WORD,    DirRemove,      (LPCSTR name))
RDF(0x057, WORD,    DirChange,      (LPCSTR name))
RDF(0x058, WORD,    DirGet,         (WORD drive, LPSTR buf))      /* drive 0 = aktuell, 1 = A: */
RDF(0x059, WORD,    DriveInfo,      (WORD drive, DRIVEINFO FAR *di))
RDF(0x05A, WORD,    DiskRequest,    (WORD drive, DWORD serial, LPCSTR label)) /* "Bitte Diskette ... einlegen" */
RDF(0x05B, WORD,    FileSaveSafe,   (LPCSTR name, const void FAR *buf, DWORD n)) /* erst .$$$, dann umbenennen */

/* ---- Grafik (RDGDI) --------------------------------------------------- */
RDF(0x064, short,   GetCaps,        (HDC dc, WORD index))         /* CAPS_* */
RDF(0x065, COLOR,   SetPen,         (HDC dc, COLOR c))
RDF(0x066, COLOR,   SetBrush,       (HDC dc, COLOR c, WORD muster)) /* PAT_* */
RDF(0x067, WORD,    SetRop,         (HDC dc, WORD rop))           /* ROP_* */
RDF(0x068, HFONT,   SetFont,        (HDC dc, HFONT f))
RDF(0x069, COLOR,   SetTextColor,   (HDC dc, COLOR c))
RDF(0x06A, COLOR,   SetBkColor,     (HDC dc, COLOR c))
RDF(0x06B, WORD,    SetBkMode,      (HDC dc, WORD mode))          /* BK_* */
RDF(0x06C, void,    SetOrigin,      (HDC dc, short x, short y))
RDF(0x06D, void,    SetClip,        (HDC dc, const RECT FAR *r))  /* zusaetzlich zum Fenster-Clipping */
RDF(0x074, void,    Pixel,          (HDC dc, short x, short y, COLOR c))
RDF(0x075, COLOR,   GetPixel,       (HDC dc, short x, short y))
RDF(0x076, void,    MoveTo,         (HDC dc, short x, short y))
RDF(0x077, void,    LineTo,         (HDC dc, short x, short y))
RDF(0x078, void,    Line,           (HDC dc, short x0, short y0, short x1, short y1))
RDF(0x079, void,    Frame,          (HDC dc, const RECT FAR *r))
RDF(0x07A, void,    FillRect,       (HDC dc, const RECT FAR *r))  /* mit aktuellem Pinsel */
RDF(0x07B, void,    InvertRect,     (HDC dc, const RECT FAR *r))
RDF(0x07C, void,    Ellipse,        (HDC dc, const RECT FAR *r, WORD fill))
RDF(0x07D, void,    Polygon,        (HDC dc, const POINT FAR *p, WORD n, WORD fill))
RDF(0x07E, void,    FloodFill,      (HDC dc, short x, short y))
RDF(0x07F, void,    RoundRect,      (HDC dc, const RECT FAR *r, WORD radius, WORD fill))
RDF(0x084, void,    TextOut,        (HDC dc, short x, short y, LPCSTR s, short len)) /* len -1 = bis 0 */
RDF(0x085, short,   TextWidth,      (HDC dc, LPCSTR s, short len))
RDF(0x086, void,    TextBox,        (HDC dc, const RECT FAR *r, LPCSTR s, WORD flags)) /* TB_* umbrechen/zentrieren */
RDF(0x08C, HFONT,   FontLoad,       (LPCSTR name, WORD hoehe, WORD stil)) /* FS_* */
RDF(0x08D, void,    FontFree,       (HFONT f))
RDF(0x08E, WORD,    FontInfo,       (HFONT f, FONTINFO FAR *fi))
RDF(0x094, HBMP,    BmpCreate,      (WORD w, WORD h, WORD bpp))
RDF(0x095, void,    BmpFree,        (HBMP b))
RDF(0x096, HDC,     BmpDC,          (HBMP b))                     /* Zeichnen in die Bitmap */
RDF(0x097, void,    DCFree,         (HDC dc))
RDF(0x098, void,    BitBlt,         (HDC d, short x, short y, short w, short h, HDC s, short sx, short sy, WORD rop))
RDF(0x099, void,    StretchBlt,     (HDC d, const RECT FAR *dr, HDC s, const RECT FAR *sr, WORD rop))
RDF(0x09A, HBMP,    BmpLoad,        (HINST i, WORD id))           /* Ressource oder 0 + Dateiname per BmpLoadFile */
RDF(0x09B, HBMP,    BmpLoadFile,    (LPCSTR name))                /* .RDB, spaeter .BMP */
RDF(0x09C, void,    IconDraw,       (HDC dc, short x, short y, HINST i, WORD id))
RDF(0x09D, COLOR,   ColorNearest,   (HDC dc, COLOR c))
RDF(0x09E, WORD,    PaletteSet,     (HDC dc, WORD first, WORD n, const COLOR FAR *c)) /* nur 16/256 Farben */
RDF(0x09F, WORD,    GfxList,        (GFXINFO FAR *l, WORD max))   /* Grafikarten, die diese Hardware kann */
RDF(0x0A0, WORD,    GfxSelect,      (WORD typ))                   /* GFX_*, 0 = automatisch; gilt ab naechstem Start */
RDF(0x0A1, WORD,    IconDrawFile,   (HDC dc, short x, short y, LPCSTR datei, WORD id)) /* Symbol aus einer RD2-Programmdatei, 0 = keins, API 2.2 */

/* ---- Oberflaeche (RDUSER) ---------------------------------------------- */
RDF(0x0A9, WORD,    RegisterClass,  (const WNDCLASS FAR *wc))
RDF(0x0AA, HWND,    CreateWindow,   (LPCSTR klasse, LPCSTR titel, DWORD stil, short x, short y, short w, short h, HWND eltern, WORD id, HINST i))
RDF(0x0AB, BOOL,    DestroyWindow,  (HWND w))
RDF(0x0AC, void,    ShowWindow,     (HWND w, WORD art))           /* SW_* */
RDF(0x0AD, void,    MoveWindow,     (HWND w, short x, short y, short cx, short cy, BOOL neu))
RDF(0x0AE, void,    SetWindowText,  (HWND w, LPCSTR s))
RDF(0x0AF, WORD,    GetWindowText,  (HWND w, LPSTR buf, WORD len))
RDF(0x0B0, void,    GetClientRect,  (HWND w, RECT FAR *r))
RDF(0x0B1, void,    GetWindowRect,  (HWND w, RECT FAR *r))        /* Bildschirmkoordinaten */
RDF(0x0B2, void,    InvalidateRect, (HWND w, const RECT FAR *r, BOOL loeschen)) /* r 0 = alles */
RDF(0x0B3, void,    ValidateRect,   (HWND w, const RECT FAR *r))
RDF(0x0B4, void,    UpdateWindow,   (HWND w))
RDF(0x0B5, HDC,     BeginPaint,     (HWND w, PAINT FAR *ps))
RDF(0x0B6, void,    EndPaint,       (HWND w, PAINT FAR *ps))
RDF(0x0B7, HDC,     GetDC,          (HWND w))                     /* 0 = ganzer Schirm */
RDF(0x0B8, void,    ReleaseDC,      (HWND w, HDC dc))
RDF(0x0B9, HWND,    SetFocus,       (HWND w))
RDF(0x0BA, HWND,    GetFocus,       (void))
RDF(0x0BB, HWND,    SetActive,      (HWND w))
RDF(0x0BC, HWND,    GetActive,      (void))
RDF(0x0BD, DWORD,   DefWindowProc,  (HWND w, WORD msg, WORD p1, DWORD p2))
RDF(0x0BE, DWORD,   SetWindowData,  (HWND w, short index, DWORD wert)) /* WD_* oder 0..n eigene */
RDF(0x0BF, DWORD,   GetWindowData,  (HWND w, short index))
RDF(0x0C0, HWND,    GetParent,      (HWND w))
RDF(0x0C1, HWND,    WindowFromPoint,(short x, short y))
RDF(0x0C2, void,    ClientToScreen, (HWND w, POINT FAR *p))
RDF(0x0C3, void,    ScreenToClient, (HWND w, POINT FAR *p))
RDF(0x0C4, BOOL,    IsWindow,       (HWND w))
RDF(0x0C5, void,    EnableWindow,   (HWND w, BOOL an))
RDF(0x0C6, void,    ScrollWindow,   (HWND w, short dx, short dy, const RECT FAR *r))
RDF(0x0C7, HWND,    GetChild,       (HWND eltern, HWND nach))     /* naechstes Kind (oberstes zuerst), nach 0 = erstes */
RDF(0x0C9, void,    ScrollSet,      (HWND w, WORD balken, short pos, short max, short seite)) /* SB_* */
RDF(0x0CA, short,   ScrollGet,      (HWND w, WORD balken))
RDF(0x0D1, HWND,    CaptureMouse,   (HWND w))
RDF(0x0D2, void,    ReleaseMouse,   (void))
RDF(0x0D3, void,    CursorSet,      (HINST i, WORD id))           /* 0,CUR_* = Standardzeiger */
RDF(0x0D4, void,    CursorBusy,     (BOOL an))                    /* Sanduhr */
RDF(0x0D5, void,    CursorPos,      (POINT FAR *p))
RDF(0x0D9, HMENU,   MenuLoad,       (HINST i, WORD id))
RDF(0x0DA, void,    MenuSet,        (HWND w, HMENU m))            /* global: Leiste beim Aktivieren */
RDF(0x0DB, void,    MenuCheck,      (HMENU m, WORD id, BOOL an))
RDF(0x0DC, void,    MenuEnable,     (HMENU m, WORD id, BOOL an))
RDF(0x0DD, void,    MenuFree,       (HMENU m))
RDF(0x0DE, WORD,    MenuPopup,      (HWND w, HMENU m, WORD sub, short x, short y)) /* Rechtsklickmenue */
RDF(0x0DF, WORD,    MenuText,       (HMENU m, WORD id, LPCSTR s))
RDF(0x0E0, WORD,    ScreenAcquire,  (HWND w))                     /* Vollbild: Schirm fuer das Programm, 1 = ok (API 2.3) */
RDF(0x0E1, void,    ScreenRelease,  (HWND w))                     /* Vollbild beenden, Schreibtisch neu */
RDF(0x0E9, WORD,    KeyState,       (WORD taste))                 /* Shift/Strg/Alt, KS_* */

/* ---- Dialoge ------------------------------------------------------------ */
RDF(0x0F3, short,   DialogRun,      (HINST i, WORD id, HWND owner, DLGPROC proc, DWORD param))
RDF(0x0F4, void,    DialogEnd,      (HWND dlg, short ergebnis))
RDF(0x0F5, HWND,    DlgItem,        (HWND dlg, WORD id))
RDF(0x0F6, void,    DlgSetText,     (HWND dlg, WORD id, LPCSTR s))
RDF(0x0F7, WORD,    DlgGetText,     (HWND dlg, WORD id, LPSTR buf, WORD len))
RDF(0x0F8, void,    DlgSetInt,      (HWND dlg, WORD id, long v))
RDF(0x0F9, long,    DlgGetInt,      (HWND dlg, WORD id, BOOL FAR *ok))
RDF(0x0FA, void,    DlgCheck,       (HWND dlg, WORD id, WORD an))
RDF(0x0FB, WORD,    DlgChecked,     (HWND dlg, WORD id))
RDF(0x0FC, short,   DialogIndirect, (HINST i, const void FAR *vorlage, HWND owner, DLGPROC proc, DWORD param)) /* Vorlage wie RT_DIALOG im Speicher */
RDF(0x103, short,   MsgBox,         (HWND owner, LPCSTR text, LPCSTR titel, WORD flags)) /* MB_* */
RDF(0x104, BOOL,    FileDialog,     (HWND owner, FILEDLG FAR *fd))
RDF(0x105, BOOL,    InputBox,       (HWND owner, LPCSTR frage, LPCSTR titel, LPSTR buf, WORD len))
RDF(0x106, BOOL,    HelpShow,       (HWND owner, LPCSTR datei, WORD thema)) /* F1-Hilfe */
RDF(0x112, void,    CtlInit,        (LPCSTR klasse))                       /* intern: Standardklassen anmelden (RDUSER) */

/* ---- Sound (RDSND) -------------------------------------------------------- */
RDF(0x11B, WORD,    SndCaps,        (void))                       /* SC_* */
RDF(0x11C, void,    SndBeep,        (WORD hz, WORD ms))
RDF(0x11D, void,    SndSystem,      (LPCSTR name))                /* "START", "DING", "FEHLER", ... */
RDF(0x11E, WORD,    SndNote,        (WORD kanal, WORD note, WORD lautst))  /* note: MIDI 0..127 */
RDF(0x11F, void,    SndNoteOff,     (WORD kanal))
RDF(0x120, WORD,    SndInstr,       (WORD kanal, const INSTR FAR *ins))
RDF(0x121, WORD,    SndMusicPlay,   (HWND melden, LPCSTR datei, WORD flags)) /* IMF/DRO, SM_* */
RDF(0x122, void,    SndMusicStop,   (void))
RDF(0x123, WORD,    SndSamplePlay,  (HWND melden, HMEM daten, WORD rate, WORD flags)) /* 8-Bit PCM */
RDF(0x124, void,    SndSampleStop,  (void))
RDF(0x125, WORD,    SndVolume,      (WORD art, WORD wert))
RDF(0x126, BOOL,    SndFocus,       (HWND w))                     /* Musik/Samples fuer dieses Programm */

/* ---- Eingabe ------------------------------------------------------------- */
RDF(0x12F, WORD,    JoyRead,        (WORD nr, JOYSTATE FAR *js))   /* RD2.LIB (JOY.C): x/y -100..100, Tasten Bit 0/1 */
RDF(0x130, WORD,    JoyCalibrate,   (WORD nr))                     /* RD2.LIB: jetzige Lage = Mitte */

/* ---- Netz (RDNET) ---------------------------------------------------- */
RDF(0x139, WORD,    NetCaps,        (NETINFO FAR *ni))            /* 0 = Netz bereit */
RDF(0x13A, WORD,    NetStart,       (HWND melden))                /* Packet-Treiber, DHCP; M_NETSTATE */
RDF(0x13B, WORD,    NetResolve,     (HWND melden, LPCSTR host))   /* -> M_NETRESOLVED p2 = IP */
RDF(0x13C, HSOCK,   NetOpen,        (HWND melden, DWORD ip, WORD port, WORD flags)) /* NO_TCP/NO_UDP */
RDF(0x13D, HSOCK,   NetListen,      (HWND melden, WORD port, WORD flags))
RDF(0x13E, WORD,    NetSend,        (HSOCK s, const void FAR *buf, WORD n)) /* gesendete Bytes */
RDF(0x13F, WORD,    NetRecv,        (HSOCK s, void FAR *buf, WORD n))
RDF(0x140, WORD,    NetClose,       (HSOCK s))
RDF(0x141, WORD,    NetState,       (HSOCK s))                    /* NS_* */
RDF(0x142, DWORD,   NetLocalIP,     (void))

/* ---- Drucker ------------------------------------------------------------- */
RDF(0x14C, HDC,     PrnOpen,        (HWND owner, PRNJOB FAR *job)) /* zeigt Druckdialog wenn PJ_DIALOG */
RDF(0x14D, WORD,    PrnPage,        (HDC dc))                     /* Band fertig: PRN_NOCHMAL = Seite noch einmal zeichnen, 0 = Seite gedruckt */
RDF(0x14E, WORD,    PrnClose,       (HDC dc))
RDF(0x14F, void,    PrnAbort,       (HDC dc))
