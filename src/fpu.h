#ifndef FPU_H
#define FPU_H
int fpu_detect(void);   /* 1 = 8087 vorhanden */
extern int fpu_present;
int cpu_detect(void);   /* 0 8088, 1 8086, 2 V20, 3 V30, 4 80188/186, 5 286, 6 386, 7 486+ */
int cpu_mhz10(int cpu); /* gemessener Takt in 1/10 MHz */
#endif
