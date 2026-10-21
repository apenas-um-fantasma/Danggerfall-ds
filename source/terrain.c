#include "terrain.h"
#include <string.h>

static unsigned char perm[512];
static unsigned world_seed;

unsigned hash3(int x, int z, unsigned salt) {
    unsigned h = (unsigned)x * 374761393u + (unsigned)z * 668265263u + salt * 2246822519u;
    h = (h ^ (h >> 13)) * 1274126177u;
    return h ^ (h >> 16);
}
int floordiv(int a, int b) { int q = a / b; if ((a % b) != 0 && ((a < 0) != (b < 0))) q--; return q; }
int isqrt(unsigned v) {
    unsigned r = 0, bit = 1u << 30;
    while (bit > v) bit >>= 2;
    while (bit) { if (v >= r + bit) { v -= r + bit; r = (r >> 1) + bit; } else r >>= 1; bit >>= 2; }
    return (int)r;
}
static int clampi(int v, int lo, int hi) { return v < lo ? lo : (v > hi ? hi : v); }
static int absi(int v) { return v < 0 ? -v : v; }

// ---------------------------------------------------------------- Perlin 2D (4.12)
static int fade(int t) {                       // 6t^5 - 15t^4 + 10t^3
    int t2 = (t * t) >> 12;
    int t3 = (t2 * t) >> 12;
    int inner = (((t * 6 - 15 * 4096) * t) >> 12) + 10 * 4096;
    return (t3 * inner) >> 12;
}
static int lerpi(int a, int b, int t) { return a + (((b - a) * t) >> 12); }
static int grad(int h, int x, int z) {
    switch (h & 7) {
        case 0: return  x + z;  case 1: return -x + z;
        case 2: return  x - z;  case 3: return -x - z;
        case 4: return  x;      case 5: return -x;
        case 6: return  z;      default: return -z;
    }
}
unsigned long g_noise_calls = 0;
static int noise2(int x, int z) {
    g_noise_calls++;              // x,z em 4.12; resultado ~[-4096,4096]
    int xi = (x >> 12) & 255, zi = (z >> 12) & 255;
    int xf = x & 4095, zf = z & 4095;
    int u = fade(xf), v = fade(zf);
    int aa = perm[perm[xi] + zi],     ab = perm[perm[xi] + zi + 1];
    int ba = perm[perm[xi + 1] + zi], bb = perm[perm[xi + 1] + zi + 1];
    int x1 = lerpi(grad(aa, xf, zf),        grad(ba, xf - 4096, zf),        u);
    int x2 = lerpi(grad(ab, xf, zf - 4096), grad(bb, xf - 4096, zf - 4096), u);
    return lerpi(x1, x2, v);
}
static int fbm(int x, int z, int oct) {        // normalizado ~[-4096,4096]
    int sum = 0, amp = 4096, norm = 0;
    for (int o = 0; o < oct; o++) {
        sum += (noise2(x + o * (37 << 12), z + o * (91 << 12)) * amp) >> 12;
        norm += amp; amp >>= 1; x <<= 1; z <<= 1;
    }
    return (sum * 4096) / norm;
}

void world_init(unsigned seed) {
    world_seed = seed;
    unsigned char p[256];
    for (int i = 0; i < 256; i++) p[i] = (unsigned char)i;
    unsigned s = seed * 1664525u + 1013904223u;
    for (int i = 255; i > 0; i--) {
        s = s * 1664525u + 1013904223u;
        int j = (s >> 16) % (i + 1);
        unsigned char t = p[i]; p[i] = p[j]; p[j] = t;
    }
    for (int i = 0; i < 512; i++) perm[i] = p[i & 255];
    terrain_cache_clear();
}

// ---------------------------------------------------------------- altura bruta
// ganho para levar o Perlin de ~[-0.9,0.9] para ~[-1,1]
#define NGAIN(v) (((v) * 5) >> 2)
static int raw_height(int tx, int tz) {
    int hills = NGAIN(fbm(tx << 6, tz << 6, 5));                          // periodo ~64 tiles
    int m     = NGAIN(fbm((tx << 5) + 90000, (tz << 5) + 30000, 3));      // periodo ~128 tiles
    int mt    = clampi((m - 1000) * 3, 0, 4096);                           // mascara de montanhas
    int rn    = 4096 - absi(NGAIN(fbm((tx << 6) + 50000, (tz << 6) + 77000, 4)));
    rn = clampi(rn, 0, 4096);
    int ridge = (rn * rn) >> 12;
    ridge = (ridge * ridge) >> 12;                                         // picos mais afiados
    ridge = (ridge + ((rn * rn) >> 12)) >> 1;
    int h = hills * 4 + (((mt * ridge) >> 12) * 30) + 3300;
    return h;
}

// ---------------------------------------------------------------- features
static int start_valid = 0, start_x, start_z;
typedef struct { int valid, rx, rz; Feature f; } FCache;
static FCache fcache[256];            // indexado por (rx&15, rz&15): vizinhos 3x3 nunca colidem

static Feature compute_feature(int rx, int rz) {
    Feature f; memset(&f, 0, sizeof f);
    unsigned h = hash3(rx, rz, world_seed ^ 0xA5u);
    int v = h % 100;
    if (v >= 72) return f;
    f.cx = rx * REGION_SIZE + 14 + ((h >> 8) % 20);
    f.cz = rz * REGION_SIZE + 14 + ((h >> 16) % 20);
    f.type = (v < 42) ? FEAT_VILLAGE : FEAT_DUNGEON;
    f.seed = hash3(rx, rz, world_seed ^ 0x77u);
    int rr = (f.type == FEAT_VILLAGE) ? 9 : 5;
    int c = raw_height(f.cx, f.cz);
    f.base_h = c;
    int ok = (c > U(0.9) && c < U(7.0));
    static const int dx[4] = { 1, -1, 0, 0 }, dz[4] = { 0, 0, 1, -1 };
    for (int i = 0; i < 4 && ok; i++) {
        int hh = raw_height(f.cx + dx[i] * rr, f.cz + dz[i] * rr);
        if (hh < U(0.5) || hh > U(9.0)) ok = 0;
    }
    if (!ok) { f.type = FEAT_NONE; }
    return f;
}
Feature world_feature(int rx, int rz) {
    FCache* c = &fcache[(rx & 15) + (rz & 15) * 16];
    if (!c->valid || c->rx != rx || c->rz != rz) { c->valid = 1; c->rx = rx; c->rz = rz; c->f = compute_feature(rx, rz); }
    return c->f;
}

// ---------------------------------------------------------------- altura final (com cache)
#define CS 128
static int cacheH[CS * CS], cacheX[CS * CS], cacheZ[CS * CS];
static unsigned char cacheV[CS * CS];
static void tree_cache_clear(void);
void terrain_cache_clear(void) { memset(cacheV, 0, sizeof cacheV); memset(fcache, 0, sizeof fcache); tree_cache_clear(); start_valid = 0; }

static int smooth01(int t) { t = clampi(t, 0, 4096); return ((t * t >> 12) * (3 * 4096 - 2 * t)) >> 12; }

static int final_height(int tx, int tz) {
    int h = raw_height(tx, tz);
    int rx0 = floordiv(tx, REGION_SIZE), rz0 = floordiv(tz, REGION_SIZE);
    for (int rz = rz0 - 1; rz <= rz0 + 1; rz++)
        for (int rx = rx0 - 1; rx <= rx0 + 1; rx++) {
            Feature f = world_feature(rx, rz);
            if (!f.type) continue;
            int rin = (f.type == FEAT_VILLAGE) ? 8 : 4, rout = (f.type == FEAT_VILLAGE) ? 15 : 9;
            int dx = tx - f.cx, dz = tz - f.cz;
            if (dx > rout || dx < -rout || dz > rout || dz < -rout) continue;
            int d2 = dx * dx + dz * dz;
            if (d2 >= rout * rout) continue;
            int d = isqrt(d2 << 8);                    // distancia * 16
            int t = ((rout * 16 - d) * 4096) / ((rout - rin) * 16);
            int w = smooth01(t);
            h = lerpi(h, f.base_h, w);
        }
    return h;
}
int world_height(int tx, int tz) {
    int i = (tx & (CS - 1)) + (tz & (CS - 1)) * CS;
    if (!cacheV[i] || cacheX[i] != tx || cacheZ[i] != tz) {
        cacheH[i] = final_height(tx, tz); cacheX[i] = tx; cacheZ[i] = tz; cacheV[i] = 1;
    }
    return cacheH[i];
}
int world_height_at(int fx, int fz) {
    int tx = fx >> 12, tz = fz >> 12, ax = fx & 4095, az = fz & 4095;
    int h00 = world_height(tx, tz), h10 = world_height(tx + 1, tz);
    int h01 = world_height(tx, tz + 1), h11 = world_height(tx + 1, tz + 1);
    return lerpi(lerpi(h00, h10, ax), lerpi(h01, h11, ax), az);
}

// ---------------------------------------------------------------- vilas
static const int DIRX[16] = { 4096, 3784, 2896, 1567, 0, -1567, -2896, -3784, -4096, -3784, -2896, -1567, 0, 1567, 2896, 3784 };
static const int DIRZ[16] = { 0, 1567, 2896, 3784, 4096, 3784, 2896, 1567, 0, -1567, -2896, -3784, -4096, -3784, -2896, -1567 };

int village_houses(const Feature* f, House* out) {
    int n = 5 + (f->seed % 2);
    int step = 16 / n;
    for (int k = 0; k < n; k++) {
        unsigned h = hash3(k, (int)(f->seed & 0xFFFF), 31);
        int di = (k * step + (h % 2)) & 15;
        int r = 5 + ((h >> 4) % 2);
        out[k].tx = f->cx + ((DIRX[di] * r + 2048) >> 12);
        out[k].tz = f->cz + ((DIRZ[di] * r + 2048) >> 12);
        int dx = -DIRX[di], dz = -DIRZ[di];            // aponta para o centro
        if (absi(dx) > absi(dz)) out[k].rot = dx > 0 ? 1 : 3; else out[k].rot = dz > 0 ? 2 : 0;
    }
    return n;
}
int village_npcs(const Feature* f, int* xs, int* zs) {
    xs[0] = (f->cx << 12) + U(1.9);  zs[0] = (f->cz << 12) + U(0.9);
    xs[1] = (f->cx << 12) + U(-0.3); zs[1] = (f->cz << 12) + U(-1.4);
    return 2;
}

// ---------------------------------------------------------------- arvores
static int near_feature_dist(int tx, int tz, int type_mask) {      // distancia em tiles (aprox) da feature mais proxima
    int best = 9999;
    int rx0 = floordiv(tx, REGION_SIZE), rz0 = floordiv(tz, REGION_SIZE);
    for (int rz = rz0 - 1; rz <= rz0 + 1; rz++)
        for (int rx = rx0 - 1; rx <= rx0 + 1; rx++) {
            Feature f = world_feature(rx, rz);
            if (!f.type || !((1 << f.type) & type_mask)) continue;
            int dx = absi(tx - f.cx), dz = absi(tz - f.cz);
            int d = dx > dz ? dx + (dz >> 1) : dz + (dx >> 1);
            if (d < best) best = d;
        }
    return best;
}
static int tc_x[64 * 64], tc_z[64 * 64]; static unsigned char tc_v[64 * 64]; static TreeInfo tc_t[64 * 64];
static int tree_at_raw(int tx, int tz, TreeInfo* out);
int tree_at(int tx, int tz, TreeInfo* out) {
    int i = (tx & 63) + (tz & 63) * 64;
    if (!tc_v[i] || tc_x[i] != tx || tc_z[i] != tz) {
        TreeInfo ti; int r = tree_at_raw(tx, tz, &ti);
        tc_x[i] = tx; tc_z[i] = tz; tc_t[i] = ti; tc_t[i].type = r ? ti.type : 0; tc_v[i] = 1;
    }
    if (!tc_t[i].type) return 0;
    *out = tc_t[i]; return 1;
}
static void tree_cache_clear(void) { memset(tc_v, 0, sizeof tc_v); }
static int tree_at_raw(int tx, int tz, TreeInfo* out) {
    unsigned h = hash3(tx, tz, world_seed + 77u);
    int fm = NGAIN(fbm((tx << 6) + 12345, (tz << 6) + 54321, 2));
    int dens = 2 + (fm > 0 ? (fm * 30) / 4096 : 0);                 // % por tile
    if ((int)(h % 100) >= dens) return 0;
    int hh = world_height(tx, tz);
    if (hh < U(0.8) || hh > U(14.0)) return 0;
    int s = absi(world_height(tx + 1, tz) - hh) + absi(world_height(tx, tz + 1) - hh);
    if (s > U(1.0)) return 0;
    if (near_feature_dist(tx, tz, (1 << FEAT_VILLAGE)) < 12) return 0;
    if (near_feature_dist(tx, tz, (1 << FEAT_DUNGEON)) < 7) return 0;
    int t;
    if (hh > U(8.5)) t = TREE_PINE;
    else if (((h >> 12) % 22) == 0) t = TREE_DEAD;
    else if (hh > U(5.0) || ((h >> 8) % 3) == 0) t = TREE_PINE;
    else t = TREE_OAK;
    out->type = t;
    out->ox = 600 + ((h >> 16) & 0xFF) * 11;
    out->oz = 600 + ((h >> 24) & 0xFF) * 11;
    return 1;
}

static void find_start_raw(int* fx, int* fz);
void world_find_start(int* fx, int* fz) {
    if (!start_valid) { find_start_raw(&start_x, &start_z); start_valid = 1; }
    *fx = start_x; *fz = start_z;
}
static void find_start_raw(int* fx, int* fz) {
    for (int ring = 0; ring <= 3; ring++)
        for (int rz = -ring; rz <= ring; rz++)
            for (int rx = -ring; rx <= ring; rx++) {
                if (absi(rx) != ring && absi(rz) != ring) continue;
                Feature f = world_feature(rx, rz);
                if (f.type == FEAT_VILLAGE) { *fx = (f.cx << 12) + U(0.5); *fz = (f.cz << 12) + U(2.6); return; }
            }
    *fx = U(0.5); *fz = U(0.5);
}

int world_biome_char(int tx, int tz) {
    int h = world_height(tx, tz);
    if (h < SEA_LEVEL) return '~';
    if (h < U(0.8)) return ',';
    if (h > U(14)) return '^';
    if (h > U(7)) return 'n';
    TreeInfo t;
    if (tree_at(tx, tz, &t)) return 'f';
    return '.';
}
