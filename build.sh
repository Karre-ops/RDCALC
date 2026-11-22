#!/bin/sh
# RDCALC fuer RetroDesk 2 bauen (Linux, Open Watcom 2.0): Ergebnis BIN/RDCALC.EXE
# Medium Model, -fpc: Gleitkomma ueber Bibliotheksaufrufe, die den 8087 nehmen, wenn der Kern einen meldet.
# Stapel 24 KB (CAS rekursiv), CAS-Knoten in einem eigenen Block (CAS_FARPOOL).
set -e
cd "$(dirname "$0")"
WATCOM=${WATCOM:-/opt/ow}; export WATCOM
PATH=$WATCOM/binl64:$WATCOM/binl:$PATH; export PATH
INCLUDE=$WATCOM/h; export INCLUDE
STK=${STK:-24576}
mkdir -p obj BIN
F="-0 -mm -fpc -os -s -zq -w4 -bt=dos -zl -iSDK -isrc -DCAS_FARPOOL"
wcc $F -fo=obj/rdcalc.obj src/RDCALC.C
wcc $F -fo=obj/cas.obj src/cas.c
wcc $F -fo=obj/rd2crt.obj src/RD2CRT.C
wasm -q -0 -fo=obj/rd2getds.obj src/RD2GETDS.ASM
wasm -q -0 -dSTK=$STK -fo=obj/rd2start.obj SDK/RD2START.ASM
wlink option quiet format dos name BIN/RDCALC.EXE file obj/rd2start.obj,obj/rdcalc.obj,obj/cas.obj,obj/rd2crt.obj,obj/rd2getds.obj \
  library SDK/RD2.LIB,$WATCOM/lib286/dos/clibm.lib,$WATCOM/lib286/mathm.lib option nodefaultlibs,map=obj/rdcalc.map
python3 tools/rdrc.py src/RDCALC.RC obj/rdcalc.res >/dev/null
python3 tools/rd2link.py BIN/RDCALC.EXE "RDCALC" --res obj/rdcalc.res >/dev/null
ls -l BIN/RDCALC.EXE
