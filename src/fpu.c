/* 8087-Erkennung ueber FNINIT/FNSTCW (No-Wait-Befehle, haengt ohne FPU nicht) */
#include "fpu.h"

int fpu_present = 0;
static unsigned short cw;

int fpu_detect(void)
{
    int i;
    cw = 0x5A5A;
    _asm {
        fninit
    }
    for (i = 0; i < 100; i++) ;          /* 8087 braucht nach FNINIT etwas Zeit */
    _asm {
        fnstcw cw
    }
    for (i = 0; i < 100; i++) ;
    /* Nach FNINIT ist das Kontrollwort 037Fh; ohne FPU bleibt 5A5Ah stehen */
    fpu_present = (cw & 0x103F) == 0x003F;
    return fpu_present;
}

/* Prozessor erkennen: 0 8088, 1 8086, 2 V20, 3 V30, 4 80188/186, 5 286, 6 386, 7 486+ */
int cpu_detect(void)
{
    int t = 0, q = 0, a = 0;
    _asm {
        pushf
        pop  ax
        and  ax, 0FFFh
        push ax
        popf
        pushf
        pop  ax
        and  ax, 0F000h
        cmp  ax, 0F000h
        je   c86                    ; 8086-Klasse: Bits 12-15 immer gesetzt
        mov  t, 5
        pushf
        mov  ax, 7000h
        push ax
        popf
        pushf
        pop  ax
        popf
        and  ax, 7000h
        jz   cend                   ; 286: Bits 12-14 im Realmodus immer 0
        mov  t, 6
        db 66h, 9Ch                 ; pushfd
        db 66h, 58h                 ; pop eax
        db 66h, 8Bh, 0C8h           ; mov ecx, eax
        db 66h, 35h, 0, 0, 4, 0     ; xor eax, 40000h (AC)
        db 66h, 50h                 ; push eax
        db 66h, 9Dh                 ; popfd
        db 66h, 9Ch                 ; pushfd
        db 66h, 58h                 ; pop eax
        db 66h, 51h                 ; push ecx
        db 66h, 9Dh                 ; popfd
        db 66h, 33h, 0C1h           ; xor eax, ecx
        db 66h, 0C1h, 0E8h, 18      ; shr eax, 18
        and  ax, 1
        jz   cend
        mov  t, 7
        jmp  cend
    c86:
        mov  ax, 1
        mov  cl, 32
        shl  ax, cl                 ; 8086/8088: 0, ab 186 und NEC: Zaehler & 31
        mov  a, ax
        mov  ax, 0101h
        db 0D5h, 10h                ; AAD 16: NEC rechnet immer mit 10
        mov  q, ax
        cli                         ; Prefetch-Queue: 8088/V20 4 Byte, 8086/V30 6 Byte
        xor  dx, dx
        db 0E8h, 0, 0               ; call $+3
        db 5Bh                      ; pop bx (BX = hier)
        db 2Eh, 0C6h, 47h, 10, 42h  ; mov byte ptr cs:[bx+10], 42h: INC DX, 5 Byte weiter
        db 90h, 90h, 90h, 90h
        db 90h                      ; Ziel (bx+10)
        mov  byte ptr cs:[bx+10], 90h
        sti
        mov  t, dx                  ; 1: ausgefuehrt (kurze Queue)
    cend:
    }
    if (t >= 5) return t;
    t = t ? 0 : 1;                                  /* 8088 / 8086 */
    if ((q & 0xFF) == 11) return 2 + t;             /* NEC V20 / V30 */
    if (a) return 4;
    return t;
}

/* Takt in 1/10 MHz: LOOP-Schleifen ueber 4 Timer-Ticks (ca. 0,22 s) zaehlen */
int cpu_mhz10(int cpu)
{
    static const unsigned char cyc[8] = { 17, 17, 13, 13, 16, 10, 13, 7 };
    volatile unsigned short __far *tk = (unsigned short __far *)0x0040006CL;
    unsigned short t;
    unsigned long n = 0;
    t = *tk; while (*tk == t) ;
    t = *tk;
    while ((unsigned short)(*tk - t) < 4) {
        _asm {
            mov  cx, 256
        cl1: loop cl1
        }
        n++;
    }
    t = 256 * cyc[cpu] + (cpu < 5 ? 60 : 20);                    /* Takte je Durchlauf */
    n = n < 1000000UL ? n * t / 21970UL : (n >> 4) * t / 1373UL; /* 4 Ticks = 0,2197 s */
    return n > 30000UL ? 30000 : (int)n;
}
