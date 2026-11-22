; RD2GETDS.ASM - __GETDS fuer die Watcom-Bibliothek ohne cstart (nur im Abbruchpfad gebraucht)
.8086
DGROUP group _DATA
_DATA segment word public 'DATA'
_DATA ends
_TEXT segment word public 'CODE'
assume cs:_TEXT
public __GETDS
__GETDS proc near
    push ax
    mov  ax, DGROUP
    mov  ds, ax
    pop  ax
    ret
__GETDS endp
_TEXT ends
end
