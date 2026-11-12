/* RetroDesk - Sound: PC-Speaker + AdLib (OPL2), Systemklaenge */
#ifndef SND_H
#define SND_H

#define SND_OFF     0
#define SND_SPEAKER 1
#define SND_ADLIB   2
#define SND_AUTO    3

#define SND_START  0
#define SND_END    1
#define SND_ERROR  2
#define SND_ASK    3
#define SND_CLICK  4
#define SND_CONN   5   /* FTP/IRC verbunden: aufsteigend */

extern int snd_mode;
extern int snd_on;              /* Optionen: Systemklaenge an */

int  snd_init(int mode);       /* liefert tatsaechlichen Modus */
void snd_quick(int mode);      /* Modus aus RDESK.STA, ohne Suche */
void snd_close(void);
extern int snd_soft;            /* 1 = beim Schliessen nur stumm schalten (RDESK <-> RDEDIT/RDNET) */
void snd_play(int id);        /* blockierend, max. ca. 0,5 s */

void spk_tone(unsigned hz);
void spk_off(void);
int  adlib_detect(void);
void adlib_note(unsigned hz); /* 0 = aus */

#endif
