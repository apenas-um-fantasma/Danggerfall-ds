// GERADO por tools/gen_ui.py
#ifndef UI_DATA_H
#define UI_DATA_H
typedef struct { const unsigned char* px; int w, h; } UiIcon;
enum { ICON_ENEMY, ICON_BOSS, ICON_CHEST, ICON_HOUSE, ICON_DUNGEON, ICON_COUNT };
extern const unsigned char ui_bg[256 * 192];
extern const unsigned short ui_palette[256];
extern const UiIcon ui_icons[ICON_COUNT];
extern const unsigned char ui_font[96][8];
#define UI_INK 240
#define UI_RED 241
#define UI_REDD 242
#define UI_BG 243
#define UI_WHITE 244
#define UI_YELLOW 245
#define UI_FLOOR 246
#define UI_BOX 247
#define UI_GRAY 248
#define UI_GREEN 249
#define UI_LINE_Y0 11
static const int UI_TEXT_Y[5] = {5,12,20,28,35};
#define UI_TEXT_X 26
#define UI_TEXT_R 238
#define UI_MAP_X0 21
#define UI_MAP_Y0 59
#define UI_MAP_X1 233
#define UI_MAP_Y1 175
#define UI_CMP_X0 197
#define UI_CMP_Y0 141
#endif
