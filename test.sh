#!/bin/bash
# RD2-Fassung in QEMU testen: RDCALC.EXE T unter RD2 (VGA, dann CGA, dann ohne 8087), Eingaben per QEMU-Monitor,
# Ergebnisse ueber DbgPrint auf COM1.  Aufruf: RD2=/pfad/zu/RetroDesk2 ./test.sh   (braucht dort out/ und tools/mkdisk.sh)
cd "$(dirname "$0")"; RD2=${RD2:?RD2=Pfad zum RetroDesk2-Repo}; H=$PWD
mkdir -p obj; IMG=$H/obj/rd2test.img; SER=$H/obj/com1.txt; SOCK=/tmp/qmon_rdcalc_$$.sock
LAUF=${LAUF:-VGA}   # VGA, CGA oder N (VGA ohne 8087); je Lauf ein QEMU-Start
case $LAUF in CGA) Z='A:\RD2\RD2.EXE /NOMAUS /CGA A:\RD2\RDCALC.EXE T';; N) Z='A:\RD2\RD2.EXE /VGA A:\RD2\RDCALC.EXE TN';; *) Z='A:\RD2\RD2.EXE /VGA A:\RD2\RDCALC.EXE T';; esac
printf '@echo off\nA:\\RDMAUS.COM\n%s\nA:\\FDAPM.COM POWEROFF\n' "$Z" > obj/ae.txt
(cd $RD2 && FMT=1440 bash tools/mkdisk.sh $IMG $H/obj/ae.txt >/dev/null 2>&1)
mcopy -o -i $IMG $RD2/out/RDMAUS.COM ::/; mcopy -o -i $IMG BIN/RDCALC.EXE ::/RD2/
rm -f $SER $SOCK obj/shot_*.ppm
timeout 900 qemu-system-i386 -m 1 -display none -no-reboot -drive file=$IMG,format=raw,if=floppy,index=0 -boot a \
  -serial file:$SER -monitor unix:$SOCK,server,nowait >/dev/null 2>&1 &
QP=$!
python3 -u - "$SOCK" "$SER" "$H/obj" "$LAUF" <<'PY'
import socket, time, sys, re
sock, ser, od = sys.argv[1:4]
def mon(c):
    for _ in range(50):
        try: s = socket.socket(socket.AF_UNIX); s.connect(sock); break
        except OSError: time.sleep(0.2)
    b = b''
    while not b.endswith(b'(qemu) '): b += s.recv(4096)
    s.send((c + '\n').encode()); b = b''
    while b'(qemu) ' not in b: b += s.recv(4096)
    s.close()
def zeilen(): 
    try: return [l for l in open(ser, encoding='latin-1').read().replace('\r', '').split('\n') if l.startswith('RDCALC: ')]
    except FileNotFoundError: return []
def warte(muster, n=1, t=120):
    for _ in range(int(t * 5)):
        if sum(1 for l in zeilen() if muster in l) >= n: return True
        time.sleep(0.2)
    return False
K = {'(':'shift-9',')':'shift-0','^':'shift-6','+':'shift-equal','*':'shift-8','=':'equal',',':'comma',';':'semicolon',
     '[':'bracket_left',']':'bracket_right','-':'minus','/':'slash',' ':'spc','.':'dot','!':'shift-1',"'":'apostrophe'}
def tippe(s):
    for c in s: mon('sendkey ' + (('shift-' + c.lower()) if c.isupper() else K.get(c, c))); time.sleep(0.12)
FAELLE = [("1+1", "2"), ("sin(30)", "0.5"), ("2^0.5", "1.414213562"), ("simplify(2x+3x)", "5*x"),
  ("factor(x^2-5x+6)", "(x-3)*(x-2)"), ("solve(x^2+1=0,x)", "x = i or x = -i"), ("5!", "120"),
  ("laplace(exp(-t)*sin(2t))", "2/(s^2+2*s+5)"), ("ilaplace(1/((s+1)*(s+2)*(s+3)))", "-exp(-2*t)+exp(-3*t)/2+exp(-t)/2"),
  ("eigenvals([2,1,0;1,2,1;0,1,2])", "x = -sqrt(2)+2 or x = 2 or x = sqrt(2)+2"), ("expand((x+1)^8)", "x^8+8*x^7+28*x^6+56*x^5+70*x^4+56*x^3+28*x^2+8*x+1"),
  ("det([1,2,3,4,5,6;2,1,3,5,4,6;3,5,1,2,4,6;4,3,5,1,2,6;5,4,2,3,1,6;6,6,6,6,6,1])", "-2640"),
  ("nsolve(x^3-2x-5=0,x,2)", "x = 2.094551482"), ("approx(sqrt(2)*pi+exp(1)/3)", "5.348976881"), ("7/0", "FEHLER Division by 0.")]
KURZ = FAELLE[:7]
fehler = 0
lauf = 0; name = sys.argv[4]; faelle = KURZ if name == "CGA" else FAELLE
if True:
    if not warte("bereit", 1, 240): print("FEHLER", name, ": startet nicht"); fehler += 1; sys.exit(1)
    print("--", name, [l for l in zeilen() if "bereit" in l][-1]); time.sleep(2)
    for e, soll in faelle:
        n = len(zeilen()); t0 = time.time(); tippe(e); mon('sendkey ret')
        for _ in range(300):
            if len(zeilen()) > n: break
            time.sleep(0.1)
        ist = zeilen()[n][8:] if len(zeilen()) > n else "(keine Antwort)"
        ok = ist == soll; fehler += not ok
        print(("ok    " if ok else "FEHLER"), e, "->", ist, "" if ok else "(erwartet " + soll + ")")
    mon('sendkey delete'); time.sleep(0.5); tippe("sin(x)*x"); mon('sendkey f5')
    k = warte("Kurve fertig", 1, 120); fehler += not k
    print(("ok    " if k else "FEHLER"), "Kurve sin(x)*x")
    time.sleep(1.5); mon('screendump %s/shot_%s_kurve.ppm' % (od, name)); mon('sendkey esc'); time.sleep(1.5)
    mon('sendkey f1'); time.sleep(1.5); mon('screendump %s/shot_%s_hilfe.ppm' % (od, name)); mon('sendkey ret'); time.sleep(1)
    mon('screendump %s/shot_%s_rechner.ppm' % (od, name)); mon('sendkey esc'); time.sleep(0.5); mon('sendkey esc')
    if not warte("Stapel frei", 1, 60): print("FEHLER", name, ": endet nicht"); fehler += 1
    else: print("      ", [l for l in zeilen() if "Stapel" in l][-1])
print("Fehler:", fehler)
PY
wait $QP 2>/dev/null
tr -d '\r' < $SER | grep -E "RD2: (Speicher|SPEICHER|Fehler)" | head -5
for f in obj/shot_*.ppm; do python3 -c "from PIL import Image; Image.open('$f').save('${f%.ppm}.png')" 2>/dev/null; done
exit 0
