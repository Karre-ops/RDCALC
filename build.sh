#!/bin/sh
# RDCALC.EXE mit Open Watcom bauen (Large Model, -fpi: 8087 wenn vorhanden, sonst Emulator)
WATCOM=${WATCOM:-/opt/ow}
export WATCOM
PATH=${WATCOM}/binl64:${WATCOM}/binl:$PATH
export PATH
INCLUDE=${WATCOM}/h
export INCLUDE
cd src && wcl -0 -ml -os -s -fpi -bt=dos -k32768 -fe=../RDCALC.EXE rdcalc.c cas.c fpu.c rdmenu.c gfx.c gfxfast.c mouse.c rdui.c snd.c && rm -f *.o *.obj
