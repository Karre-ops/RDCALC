#!/bin/sh
# RDCALC.EXE mit Open Watcom bauen (Large Model, -fpi: 8087 wenn vorhanden, sonst Emulator)
export WATCOM=${WATCOM:-/opt/ow} PATH=${WATCOM}/binl64:${WATCOM}/binl:$PATH INCLUDE=${WATCOM}/h
cd src && wcl -0 -ml -os -s -fpi -bt=dos -fe=../RDCALC.EXE rdcalc.c fpu.c rdmenu.c gfx.c gfxfast.c mouse.c rdui.c snd.c && rm -f *.o *.obj
