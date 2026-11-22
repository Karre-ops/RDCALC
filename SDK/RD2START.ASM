; RD2START.ASM - Startcode fuer RD2-Programme (16 Bit, Modell Medium/Large)
; SPDX-License-Identifier: GPL-2.0-or-later  (c) 2026 Karsten, siehe COPYING
; Der Kern ruft den Einsprung (CS:IP aus dem MZ-Kopf) per FAR CALL auf mit
;   AX = 5232h ('R2'), ES:BX = API-Tabelle, CX = HINST, DX = show, DI:SI = Befehlszeile
; Startet DOS die EXE direkt, steht in AX nie 5232h: Meldung, Fehlercode 1.
.8086
RD2START_TEXT segment word public 'CODE'     ; Code zuerst: Klasse CODE vor DATA im Bild
RD2START_TEXT ends
DGROUP group _DATA, CONST, CONST2, _BSS, STACK

_DATA segment word public 'DATA'
public __rd2api
__rd2api dd 0
kss      dw 0
ksp      dw 0
nomsg    db 'Dieses Programm ben', 94h, 'tigt RetroDesk 2.', 13, 10, '$'
_DATA ends
CONST  segment word public 'DATA'
CONST  ends
CONST2 segment word public 'DATA'
CONST2 ends
_BSS   segment word public 'BSS'
bss_anf label byte                  ; erstes Objekt: Anfang von _BSS
_BSS   ends
ifndef STK
STK equ 4096
endif
STACK  segment para stack 'STACK'
bss_end label byte                  ; _BSS endet vor STACK
public stkbot
stkbot label byte                   ; unterstes Stapelbyte (Programme messen damit den freien Stapel, 31.10.)
       db STK dup(?)                ; Stackgroesse: wasm -dSTK=3072 (build.sh APPSTACK), sonst 4 KB
stacktop label byte
STACK  ends

RD2START_TEXT segment word public 'CODE'
assume cs:RD2START_TEXT, ds:DGROUP
extrn RDMAIN:far
_start:
    cmp  ax, 5232h
    jne  dosrun
    mov  ax, DGROUP
    mov  ds, ax
    push es                          ; _BSS loeschen (DOS laedt nur das Dateibild)
    push di
    push cx
    mov  es, ax
    mov  di, offset DGROUP:bss_anf
    mov  cx, offset DGROUP:stacktop      ; _BSS und Stapel loeschen: unberuehrte Nullen zeigen den nie benutzten Stapel
    sub  cx, di
    xor  al, al
    cld
    rep  stosb
    pop  cx
    pop  di
    pop  es
    mov  ax, ds
    mov  word ptr __rd2api, bx
    mov  word ptr __rd2api+2, es
    mov  kss, ss
    mov  ksp, sp
    cli
    mov  ss, ax                      ; eigener Stack in DGROUP (SS = DS, wie Watcom erwartet)
    mov  sp, offset DGROUP:stacktop
    sti
    push cx                          ; RdMain(HINST inst, LPCSTR cmd, WORD show), pascal
    push di
    push si
    push dx
    call RDMAIN
    mov  bx, DGROUP
    mov  ds, bx
    cli
    mov  ss, kss                     ; zurueck auf den Stack des Kerns
    mov  sp, ksp
    sti
    retf
dosrun:
    mov  ax, DGROUP
    mov  ds, ax
    mov  dx, offset DGROUP:nomsg
    mov  ah, 9
    int  21h
    mov  ax, 4C01h
    int  21h
RD2START_TEXT ends
end _start
