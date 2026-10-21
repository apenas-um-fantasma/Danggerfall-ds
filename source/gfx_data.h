// GERADO por tools/gen_assets.py
#ifndef GFX_DATA_H
#define GFX_DATA_H
typedef struct { const unsigned short* data; int w, h; int wrap; } TexDef;
enum {
    TEX_GRASS,
    TEX_GRASS2,
    TEX_DIRT,
    TEX_ROCK,
    TEX_SAND,
    TEX_SNOW,
    TEX_COBBLE,
    TEX_WATER,
    TEX_WALL,
    TEX_ROOF,
    TEX_DOOR,
    TEX_DG_WALL,
    TEX_DG_FLOOR,
    TEX_DG_CEIL,
    TEX_DG_DOOR,
    TEX_TREE_PINE,
    TEX_TREE_OAK,
    TEX_TREE_DEAD,
    TEX_HOUSE_FAR,
    TEX_DUNGEON_FAR,
    TEX_EXIT,
    TEX_GOBLIN0,
    TEX_GOBLIN1,
    TEX_GOBLIN2,
    TEX_GOBLIN3,
    TEX_SKEL0,
    TEX_SKEL1,
    TEX_SKEL2,
    TEX_SKEL3,
    TEX_NPC0,
    TEX_NPC1,
    TEX_CHEST0,
    TEX_CHEST1,
    TEX_SWORD0,
    TEX_SWORD1,
    TEX_SWORD2,
    TEX_SWORD3,
    TEX_FIST0,
    TEX_FIST1,
    TEX_FIST2,
    TEX_SUN,
    TEX_MOON,
    TEX_CLOUD0,
    TEX_CLOUD1,
    TEX_CLOUD2,
    TEX_LOGO,
    TEX_COUNT
};
extern const TexDef gTexDefs[TEX_COUNT];
// cores medias dos biomas (calculadas das texturas de chao)
#define TINT_GRASS_R 80
#define TINT_GRASS_G 119
#define TINT_GRASS_B 62
#define TINT_GRASS2_R 115
#define TINT_GRASS2_G 122
#define TINT_GRASS2_B 51
#define TINT_DIRT_R 142
#define TINT_DIRT_G 93
#define TINT_DIRT_B 52
#define TINT_SAND_R 255
#define TINT_SAND_G 191
#define TINT_SAND_B 97
#define TINT_SNOW_R 214
#define TINT_SNOW_G 239
#define TINT_SNOW_B 255
#endif
