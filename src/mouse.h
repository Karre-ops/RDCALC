#ifndef MOUSE_H
#define MOUSE_H
int  mouse_present(void);
int  mouse_init(void);
void mouse_show(void);
void mouse_hide(void);
void mouse_read(int *x, int *y, int *b);
void mouse_range(int w, int h);
int  mouse_pressed(int btn, int *x, int *y);   /* 0 links, 1 rechts: Anzahl Druecke */
void mouse_moveto(int x, int y);
#endif
