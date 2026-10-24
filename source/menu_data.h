// GERADO por tools/gen_menu.py - nao edite a mao.
#ifndef MENU_DATA_H
#define MENU_DATA_H
extern const unsigned char menu_bg[49152];            // fundo do menu (indices 0..239 da paleta do menu)
extern const unsigned short menu_palette[240];       // so os primeiros 240; 240..255 sao as cores fixas da UI
extern const unsigned char menu_arrow[154];       // seta de selecao (255 = transparente)
#define MENU_ARROW_W 11
#define MENU_ARROW_H 14
#define MENU_ARROW_X 57
#define MENU_ARROW_DY 0
static const int MENU_SLOT_CY[4] = {76,94,111,129};   // centro vertical de cada faixa
#define MENU_SLOT_X0 71
#define MENU_SLOT_X1 199
#endif
