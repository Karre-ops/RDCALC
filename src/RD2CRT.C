/* RD2CRT.C - Ersatz fuer die Teile des Watcom-Startcodes (cstart), die Gleitkomma-Fehlerpfade der
 * Bibliothek hereinziehen. RD2-Programme starten ueber RD2START.ASM; ein Abbruch beendet nur das Programm. */
#include "RD2.H"
void __exit(unsigned rc) { TaskKill(TaskSelf(), (WORD)rc); for (;;) WaitMessage(); }
void __do_exit_with_msg(char __far *msg, unsigned rc) { (void)msg; DbgPrint("RDCALC: Laufzeitfehler"); __exit(rc); }
