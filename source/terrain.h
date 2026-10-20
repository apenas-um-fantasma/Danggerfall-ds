// terrain.h - mundo procedural (Ruido Perlin em ponto fixo), vilas, dungeons, arvores.
// Codigo C puro (sem libnds) para poder ser testado no PC.
#ifndef TERRAIN_H
#define TERRAIN_H

#define F_ONE        4096          // 1.0 em ponto fixo 20.12 (1 tile = 1.0)
#define U(x)         ((int)((x) * 4096))
#define SEA_LEVEL    0
#define REGION_SIZE  48            // tamanho (em tiles) de cada regiao de "features"

enum { FEAT_NONE = 0, FEAT_VILLAGE = 1, FEAT_DUNGEON = 2 };
typedef struct { int type; int cx, cz; int base_h; unsigned seed; } Feature;

typedef struct { int tx, tz; int rot; } House;      // centro (tile) e lado da porta 0..3
enum { TREE_NONE = 0, TREE_PINE, TREE_OAK, TREE_DEAD };
typedef struct { int type; int ox, oz; } TreeInfo;  // deslocamento dentro do tile (f32)

void     world_init(unsigned seed);
void     terrain_cache_clear(void);
unsigned hash3(int x, int z, unsigned salt);
int      floordiv(int a, int b);
int      isqrt(unsigned v);
int      world_height(int tx, int tz);              // altura do canto (tx,tz), f32, com cache
int      world_height_at(int fx, int fz);           // altura interpolada em posicao f32
Feature  world_feature(int rx, int rz);
int      village_houses(const Feature* f, House* out);       // devolve quantidade
int      village_npcs(const Feature* f, int* xs, int* zs);   // posicoes f32
int      tree_at(int tx, int tz, TreeInfo* out);
void     world_find_start(int* fx, int* fz);
int      world_biome_char(int tx, int tz);          // caractere p/ minimapa

#endif
