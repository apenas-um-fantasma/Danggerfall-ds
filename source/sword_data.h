// GERADO por tools/gen_swords.py - nao edite a mao.
#ifndef SWORD_DATA_H
#define SWORD_DATA_H
#define SWORD_N 4
typedef struct { const unsigned short* data; int w, h, cw, ch; int x0, y0, x1, y1; } SwordDef;   // textura w x h; so cw x ch tem desenho; x/y no overlay (v16): 1 px de tela = 256
extern const SwordDef sword_def[SWORD_N];
#endif
