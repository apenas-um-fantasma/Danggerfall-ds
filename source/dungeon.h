// dungeon.h - gerador de dungeons pequenas (salas + corredores). C puro, sem libnds.
#ifndef DUNGEON_H
#define DUNGEON_H
#define DG_W 32
#define DG_H 32
#define DG_MAX_EN 14
#define DG_MAX_CH 8

typedef struct { int cx, cz, boss; } DgEnemy;
typedef struct { int cx, cz; } DgChest;
typedef struct {
    unsigned char cell[DG_H][DG_W];       // 0 = parede, 1 = chao
    int sx, sz;                            // celula de entrada/saida
    int nen; DgEnemy en[DG_MAX_EN];
    int nch; DgChest ch[DG_MAX_CH];
} Dungeon;

void dungeon_generate(Dungeon* d, unsigned seed);
static inline int dg_floor(const Dungeon* d, int cx, int cz) {
    return cx >= 0 && cz >= 0 && cx < DG_W && cz < DG_H && d->cell[cz][cx];
}
#endif
