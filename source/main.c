// ============================================================================
//  DAGGERFALL DS (teste tecnico) - homebrew 3D para Nintendo DS (libnds)
//  - Mundo procedural infinito (Ruido Perlin em ponto fixo), so ao redor do jogador
//  - Vilas, dungeons, arvores; de longe = sprites 2D, de perto = 3D
//  - Combate em primeira pessoa (espada / punho), inimigos como sprites animados
//  - Tela de baixo: menu, HUD e minimapa em texto
// ============================================================================
#include <nds.h>
#if defined(__has_include)
#  if __has_include(<nds/arm9/exceptions.h>)
#    include <nds/arm9/exceptions.h>
#    define HAVE_EXC_HANDLER 1
#  endif
#endif
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "terrain.h"
#include "dungeon.h"
#include "gfx_data.h"

// ------------------------------------------------------------------ configuracao (ajuste aqui)
#define R_MAX           20// maximo da distancia de renderizacao (tiles)
#define GEO_SHIFT       2           // vertices guardados em escala 1/4 (cabe ate 30 tiles no v16 do DS)
#define PITCH_SIGN      1           // se olhar para cima/baixo sair invertido, troque para -1
#define PITCH_MAX       7200        // limite de inclinacao da camera (~40 graus)
static int gRView = 16;             // distancia de renderizacao ATUAL (muda em Configuracoes)
#define R_VIEW          gRView
#define FADE_TILES      (R_VIEW / 4)  // largura do anel que "dissolve" no ceu
#define HOUSE_3D_DIST   U(8.0)      // abaixo disso a casa vira 3D; acima disso e sprite 2D
#define DG_3D_DIST      U(9.0)
#define EYE             U(0.8)
#define WALK_SPEED      U(0.050)
#define RUN_SPEED       U(0.090)
#define TURN_SPEED      560         // unidades de 1/32768 de volta por tick
#define VIEW_ROT_SIGN   1           // se a camera girar ao contrario, troque para -1
#define DAY_LEN         14400       // ticks (60/s) -> 4 minutos por dia
#define HW              (2 * R_MAX + 4)
#define DG_C            U(1.5)      // tamanho de uma celula de dungeon
#define DG_WALL_H       U(1.6)
#define DG_RADIUS       8
#define MAX_EN          24
#define SEED            1337u


// ---- espera de VBlank por polling do VCOUNT: funciona mesmo se a IRQ de VBlank nao estiver ativa (libnds 2.x/calico)
#ifdef HOST_TEST
static void wait_vbl(void) { swiWaitForVBlank(); }
#endif
static int fpsShown = 0;
#ifdef HOST_TEST
static void fps_init(void) {}
static void fps_tick(void) { fpsShown = 60; }
#else
#define MY_T3D (*(volatile u16*)0x0400010C)
#define MY_T3C (*(volatile u16*)0x0400010E)
static void fps_init(void) { MY_T3C = 0; MY_T3D = 0; MY_T3C = 0x0083; }     // timer 3 livre a ~32,7 kHz
static void fps_tick(void) {
    static u16 last = 0; static int acc = 0, frames = 0;
    u16 now = MY_T3D; acc += (u16)(now - last); last = now; frames++;
    if (acc >= 32728) { fpsShown = (frames * 32728 + acc / 2) / acc; acc = 0; frames = 0; }
}
#endif
#ifndef HOST_TEST
#define MY_VCOUNT (*(volatile u16*)0x04000006)
#define MY_POWCNT (*(volatile u16*)0x04000304)
static void wait_vbl(void) { while (MY_VCOUNT >= 192) {} while (MY_VCOUNT < 192) {} }
#endif

// ------------------------------------------------------------------ utilidades
static inline int absi(int v) { return v < 0 ? -v : v; }
static inline int clampi(int v, int lo, int hi) { return v < lo ? lo : (v > hi ? hi : v); }
static inline int mini(int a, int b) { return a < b ? a : b; }
static inline int maxi(int a, int b) { return a > b ? a : b; }
static unsigned rngs = 12345;
static int rnd(int n) { rngs = rngs * 1664525u + 1013904223u; return (int)((rngs >> 12) % (unsigned)n); }
static int dist2d(int dx, int dz) { dx >>= 4; dz >>= 4; return isqrt((unsigned)(dx * dx + dz * dz)) << 4; }
static int adist(int dx, int dz) { dx = absi(dx); dz = absi(dz); return dx > dz ? dx + (dz * 3 >> 3) : dz + (dx * 3 >> 3); }

// ------------------------------------------------------------------ estado
enum { ST_TITLE, ST_PLAY, ST_DEAD };
static int gstate = ST_TITLE;
static int paused = 0;
static int helpPage = 0;
static int menuSel = 0;

typedef struct {
    int x, z, y, ground, yaw, pitch;
    int hp, maxhp, xp, level, gold, potions, kills;
    int weapon;                 // 0 = punho, 1 = espada
    int atk_t, hurt_t, bob;
} Player;
static Player P;

typedef struct { int active, type, boss, x, z, hp, maxhp, cd, atk_t, dead_t, anim, hurt_t; } Enemy;
static Enemy en[MAX_EN];

typedef struct { int fps, trees, monsters, clouds, sens, invertY; } Settings;
static Settings cfg = { 0, 1, 1, 1, 3, 0 };     // FPS off, arvores on, monstros on, nuvens on, sensibilidade 3, Y normal

static int inDungeon = 0;
static Dungeon dg;
static unsigned char dgOpened[DG_MAX_CH];
static int ow_x, ow_z, ow_yaw;          // posicao no mundo antes de entrar na dungeon
static unsigned curDgSeed;

static int worldTime = DAY_LEN / 8;
static int frameNo = 0;
static int dayf = 4096;                 // 0..4096 (noite..dia)
static int ambR = 256, ambG = 256, ambB = 256;
static int spawnTimer = 120;

static char msg[120]; static int msgT = 0;
static void say(const char* s) { strncpy(msg, s, 119); msg[119] = 0; msgT = 220; }

// ------------------------------------------------------------------ texturas
static int texId[TEX_COUNT];
static int tsz(int n) { int s = 0, v = 8; while (v < n) { v <<= 1; s++; } return s; }
static void load_textures(void) {
    for (int i = 0; i < TEX_COUNT; i++) {
        const TexDef* t = &gTexDefs[i];
        glGenTextures(1, &texId[i]);
        glBindTexture(0, texId[i]);
        glTexImage2D(0, 0, GL_RGBA, tsz(t->w), tsz(t->h), 0,
                     TEXGEN_TEXCOORD | (t->wrap ? (GL_TEXTURE_WRAP_S | GL_TEXTURE_WRAP_T) : 0), t->data);
    }
}
#define TC(u, v) glTexCoord2t16(inttot16(u), inttot16(v))

// ------------------------------------------------------------------ camera / vertices
static int gox, goy, goz;               // origem da vista (f32): vertices sao relativos a ela
static int gRX, gRZ, gFX, gFZ;          // vetor "direita" e "frente" (4.12)
#ifdef HOST_TEST
static int g_clamps = 0;
#endif
static inline short v16c(int f) {
    int v = f >> GEO_SHIFT;
    if (v > 32767) { v = 32767;
#ifdef HOST_TEST
        g_clamps++;
#endif
    }
    if (v < -32767) { v = -32767;
#ifdef HOST_TEST
        g_clamps++;
#endif
    }
    return (short)v;
}
static inline void vtx(int wx, int wy, int wz) { glVertex3v16(v16c(wx - gox), v16c(wy - goy), v16c(wz - goz)); }

static void set_modelview(void) {
    glMatrixMode(GL_MODELVIEW); glLoadIdentity();
    if (P.pitch) glRotateXi(-PITCH_SIGN * P.pitch);
    glRotateYi(VIEW_ROT_SIGN * (P.yaw & 0x7FFF));
    glTranslate3f32(-(P.x - gox), -(P.y - goy), -(P.z - goz));
    glScalef32(inttof32(1 << GEO_SHIFT), inttof32(1 << GEO_SHIFT), inttof32(1 << GEO_SHIFT));
}
static void setup_camera(void) {
    int yaw = P.yaw & 0x7FFF;
    gFX = sinLerp(yaw); gFZ = -cosLerp(yaw);
    gRX = cosLerp(yaw); gRZ = sinLerp(yaw);
    gox = (P.x >> 12) << 12; goy = (P.y >> 12) << 12; goz = (P.z >> 12) << 12;
    glMatrixMode(GL_PROJECTION); glLoadIdentity();
    gluPerspective(70, 256.0 / 192.0, 0.25, R_VIEW + 10);
    set_modelview();
}

static unsigned short lit(int sh, int tint) {          // sh,tint 0..256 -> cor 15 bits
    int k = (sh * tint) >> 8;
    int r = (31 * k * ambR) >> 16, g = (31 * k * ambG) >> 16, b = (31 * k * ambB) >> 16;
    return RGB15(mini(r, 31), mini(g, 31), mini(b, 31));
}
static unsigned short litc(int sh) { return lit(sh, 256); }
static unsigned short litrgb(int sh, int tr, int tg, int tb) {       // sombreamento * cor do bioma * luz ambiente
    int r = (31 * sh * tr * ambR) >> 24, g = (31 * sh * tg * ambG) >> 24, b = (31 * sh * tb * ambB) >> 24;
    return RGB15(mini(r, 31), mini(g, 31), mini(b, 31));
}

static void update_daylight(void) {
    if (inDungeon) { dayf = 0; return; }
    int ang = (worldTime * 32768 / DAY_LEN) & 0x7FFF;
    int s = sinLerp(ang);
    dayf = clampi((s + 800) * 4096 / 2400, 0, 4096);
    ambR = 66 + (dayf * 190 >> 12);
    ambG = 74 + (dayf * 182 >> 12);
    ambB = 108 + (dayf * 148 >> 12);
}

// ------------------------------------------------------------------ billboards
static void bb(int tex, int wx, int wy, int wz, int w, int h, int alpha, unsigned short col) {
    int hw = w >> 1, ox = (gRX * hw) >> 12, oz = (gRZ * hw) >> 12;
    int tw = gTexDefs[tex].w, th = gTexDefs[tex].h;
    glPolyFmt(POLY_ALPHA(alpha) | POLY_CULL_NONE | POLY_ID(alpha < 31 ? 5 : 2));
    glBindTexture(0, texId[tex]);
    glColor(col);
    glBegin(GL_QUADS);
    TC(0, th);  vtx(wx - ox, wy,     wz - oz);
    TC(tw, th); vtx(wx + ox, wy,     wz + oz);
    TC(tw, 0);  vtx(wx + ox, wy + h, wz + oz);
    TC(0, 0);   vtx(wx - ox, wy + h, wz - oz);
    glEnd();
}

// quad generico (4 cantos em ordem), com cores por vertice
static void quad4(int tex, int alpha, int pid,
                  int x0, int y0, int z0, unsigned short c0, int u0, int v0,
                  int x1, int y1, int z1, unsigned short c1, int u1, int v1,
                  int x2, int y2, int z2, unsigned short c2, int u2, int v2,
                  int x3, int y3, int z3, unsigned short c3, int u3, int v3) {
    glPolyFmt(POLY_ALPHA(alpha) | POLY_CULL_NONE | POLY_ID(pid));
    glBindTexture(0, texId[tex]);
    glBegin(GL_QUADS);
    glColor(c0); TC(u0, v0); vtx(x0, y0, z0);
    glColor(c1); TC(u1, v1); vtx(x1, y1, z1);
    glColor(c2); TC(u2, v2); vtx(x2, y2, z2);
    glColor(c3); TC(u3, v3); vtx(x3, y3, z3);
    glEnd();
}

// ------------------------------------------------------------------ listas "perto do jogador"
static Feature nearF[9]; static int nnearF;
static House   nearH[24]; static int nearHx[24], nearHz[24], nnearH;
static int     npcX[12], npcZ[12], npcVar[12], nnpc;
static int     visTx[2000], visTz[2000], visD2[2000], nvis, g_nwater;

static void build_near_lists(void) {
    nnearF = nnearH = nnpc = 0;
    int rx0 = floordiv(P.x >> 12, REGION_SIZE), rz0 = floordiv(P.z >> 12, REGION_SIZE);
    for (int rz = rz0 - 1; rz <= rz0 + 1; rz++)
        for (int rx = rx0 - 1; rx <= rx0 + 1; rx++) {
            Feature f = world_feature(rx, rz);
            if (!f.type) continue;
            nearF[nnearF++] = f;
            if (f.type == FEAT_VILLAGE) {
                House hs[8]; int n = village_houses(&f, hs);
                for (int k = 0; k < n && nnearH < 24; k++) { nearH[nnearH] = hs[k]; nearHx[nnearH] = (hs[k].tx << 12) + 2048; nearHz[nnearH] = (hs[k].tz << 12) + 2048; nnearH++; }
                int xs[2], zs[2]; int m = village_npcs(&f, xs, zs);
                for (int k = 0; k < m && nnpc < 12; k++) { npcX[nnpc] = xs[k]; npcZ[nnpc] = zs[k]; npcVar[nnpc] = (int)((f.seed >> k) & 1); nnpc++; }
            }
        }
}

static int alphaTab[R_MAX * R_MAX + 1], alphaTabOk = 0;
static int fade_alpha(int d2) {
    if (d2 >= 0 && d2 <= R_VIEW * R_VIEW) {
        if (alphaTabOk != gRView) { for (int i = 0; i <= R_VIEW * R_VIEW; i++) { int dd = isqrt((unsigned)(i << 8)); int lim = (R_VIEW - FADE_TILES) * 16; alphaTab[i] = dd <= lim ? 31 : clampi(31 - ((dd - lim) * 28) / (FADE_TILES * 16), 3, 31); } alphaTabOk = gRView; }
        return alphaTab[d2];
    }
    int dd = isqrt((unsigned)(d2 << 8));            // distancia * 16
    int lim = (R_VIEW - FADE_TILES) * 16;
    if (dd <= lim) return 31;
    return clampi(31 - ((dd - lim) * 28) / (FADE_TILES * 16), 3, 31);
}

// ------------------------------------------------------------------ terreno
static int Hc[HW][HW];
static unsigned char SHc[HW][HW];
#define LX 2160
#define LY 3230
#define LZ 1500

static int feat_dist16(int tx, int tz, int type) {   // distancia*16 ate a feature mais proxima do tipo
    int best = 99999;
    for (int i = 0; i < nnearF; i++) {
        if (nearF[i].type != type) continue;
        int dx = tx - nearF[i].cx, dz = tz - nearF[i].cz;
        if (dx > 10 || dx < -10 || dz > 10 || dz < -10) continue;
        int d = isqrt((unsigned)((dx * dx + dz * dz) << 8));
        if (d < best) best = d;
    }
    return best;
}

static int pick_tex(int tx, int tz, int h00, int h10, int h01, int h11, unsigned hv) {
    int avg = (h00 + h10 + h01 + h11) >> 2;
    int mn = mini(mini(h00, h10), mini(h01, h11)), mx = maxi(maxi(h00, h10), maxi(h01, h11));
    int slope = mx - mn;
    avg += (((int)((hv >> 8) & 255)) - 128) * U(1.6) / 128;         // limiares tremidos -> fronteiras em faixa, nao em linha
    slope += (((int)((hv >> 16) & 255)) - 128) * U(0.45) / 128; if (slope < 0) slope = 0;
    int dv = feat_dist16(tx, tz, FEAT_VILLAGE) + (int)((hv >> 24) & 31) - 16;
    if (dv < 3 * 16) return TEX_COBBLE;
    if (dv < 9 * 16 && (hv % 5) < 2) return TEX_DIRT;
    if (feat_dist16(tx, tz, FEAT_DUNGEON) < 3 * 16) return TEX_DIRT;
    if (avg < U(0.9) && slope < U(0.9)) return TEX_SAND;
    if (avg > U(15)) return slope > U(2.2) ? TEX_ROCK : TEX_SNOW;
    if (slope > U(1.2)) return TEX_ROCK;
    if (avg > U(11)) return (hv & 1) ? TEX_ROCK : TEX_SNOW;
    if (avg > U(8)) return (hv & 3) ? TEX_ROCK : TEX_GRASS2;
    if (avg > U(5.5)) return (hv & 1) ? TEX_GRASS2 : TEX_GRASS;
    return ((hv >> 3) % 6 == 0) ? TEX_GRASS2 : TEX_GRASS;
}

static unsigned char Tr[HW][HW], Tg[HW][HW], Tb[HW][HW];
static int smoothi(int x, int a, int b) {                 // smoothstep 0..256
    if (a == b) return x >= a ? 256 : 0;
    int t = ((x - a) * 256) / (b - a); t = clampi(t, 0, 256);
    return (t * t * (768 - 2 * t)) >> 16;
}
static void mixc(int* c, int r, int g, int b, int w) { c[0] += ((r - c[0]) * w) >> 8; c[1] += ((g - c[1]) * w) >> 8; c[2] += ((b - c[2]) * w) >> 8; }

static void draw_terrain(void) {
    int cx = P.x >> 12, cz = P.z >> 12;
    int bx = cx - R_VIEW - 1, bz = cz - R_VIEW - 1;
    for (int j = 0; j < HW; j++) for (int i = 0; i < HW; i++) Hc[j][i] = world_height(bx + i, bz + j);
    for (int j = 1; j < HW - 1; j++) for (int i = 1; i < HW - 1; i++) {
        int sx = (Hc[j][i + 1] - Hc[j][i - 1]) >> 1, sz = (Hc[j + 1][i] - Hc[j - 1][i]) >> 1;
        int dot = ((-sx * LX - sz * LZ) >> 12) + LY;
        int len = 4096 + (((sx * sx) >> 12) + ((sz * sz) >> 12)) / 2;
        int sh = clampi((dot * 4096) / len, 0, 4096);
        SHc[j][i] = (unsigned char)(56 + ((sh * 199) >> 12));
        // cor do bioma por vertice: varia de forma CONTINUA com altura/inclinacao -> transicoes suaves (Gouraud)
        int h = Hc[j][i], slope = maxi(absi(sx), absi(sz));
        int vx = bx + i, vz = bz + j;
        int c[3] = { 98, 170, 66 };                                                   // grama
        mixc(c, 176, 164, 84, (smoothi(h, U(3.5), U(7.0)) * 150) >> 8);               // grama seca em terreno alto
        mixc(c, 238, 218, 166, 256 - smoothi(h, U(0.3), U(1.4)));                     // areia perto da agua
        mixc(c, 120, 142, 132, smoothi(-h, 0, U(1.4)));                               // fundo do lago
        mixc(c, 150, 146, 140, maxi(smoothi(slope, U(0.75), U(1.5)), smoothi(h, U(7.5), U(12))));   // rocha
        mixc(c, 250, 252, 255, (smoothi(h, U(12), U(16)) * (256 - smoothi(slope, U(1.4), U(2.6)))) >> 8);  // neve
        int dv = feat_dist16(vx, vz, FEAT_VILLAGE);
        if (dv < 11 * 16) {
            mixc(c, 162, 124, 86, (256 - smoothi(dv, 5 * 16, 11 * 16)) * 80 >> 8);    // terra batida ao redor da vila
            mixc(c, 172, 166, 152, 256 - smoothi(dv, 2 * 16, 4 * 16));                // praca de pedra
        }
        int dd = feat_dist16(vx, vz, FEAT_DUNGEON);
        if (dd < 6 * 16) mixc(c, 150, 118, 84, 256 - smoothi(dd, 2 * 16, 6 * 16));
        int jit = 246 + (int)(hash3(vx, vz, 7) & 15);                                 // variacao sutil por vertice
        Tr[j][i] = (unsigned char)mini(255, c[0] * jit >> 8);
        Tg[j][i] = (unsigned char)mini(255, c[1] * jit >> 8);
        Tb[j][i] = (unsigned char)mini(255, c[2] * jit >> 8);
    }
    nvis = 0;
    static int wlist[1400], nw; nw = 0;
    int lastTex = -1, lastAlpha = -1;
    for (int tz = cz - R_VIEW; tz <= cz + R_VIEW; tz++)
        for (int tx = cx - R_VIEW; tx <= cx + R_VIEW; tx++) {
            int dx = tx - cx, dz = tz - cz, d2 = dx * dx + dz * dz;
            if (d2 > R_VIEW * R_VIEW) continue;
            int rx = (tx << 12) + 2048 - P.x, rz = (tz << 12) + 2048 - P.z;
            int fd = (rx * gFX + rz * gFZ) >> 12;
            int sd = (rx * gRX + rz * gRZ) >> 12;
            if (fd < -U(2.5) || absi(sd) > fd + U(4.5)) continue;
            if (nvis < 2000) { visTx[nvis] = tx; visTz[nvis] = tz; visD2[nvis] = d2; nvis++; }
            int a = tx - bx, b = tz - bz;
            int h00 = Hc[b][a], h10 = Hc[b][a + 1], h01 = Hc[b + 1][a], h11 = Hc[b + 1][a + 1];
            unsigned hv = hash3(tx, tz, 99);
            int tex = pick_tex(tx, tz, h00, h10, h01, h11, hv);
            int alpha = fade_alpha(d2);
            if (mini(mini(h00, h10), mini(h01, h11)) < SEA_LEVEL && nw < 1400) wlist[nw++] = (nvis - 1);
            if (tex != lastTex) { glBindTexture(0, texId[tex]); lastTex = tex; }
            if (alpha != lastAlpha) { glPolyFmt(POLY_ALPHA(alpha) | POLY_CULL_NONE | POLY_ID(alpha < 31 ? 4 : 1)); lastAlpha = alpha; }
            int uA = (hv & 2) ? 64 : 0, uB = (hv & 2) ? 0 : 64;
            int vA = (hv & 4) ? 64 : 0, vB = (hv & 4) ? 0 : 64;
            glBegin(GL_QUADS);
            glColor(litrgb(SHc[b][a], Tr[b][a], Tg[b][a], Tb[b][a])); TC(uA, vA); vtx(tx << 12,       h00, tz << 12);
            glColor(litrgb(SHc[b][a + 1], Tr[b][a + 1], Tg[b][a + 1], Tb[b][a + 1])); TC(uB, vA); vtx((tx + 1) << 12, h10, tz << 12);
            glColor(litrgb(SHc[b + 1][a + 1], Tr[b + 1][a + 1], Tg[b + 1][a + 1], Tb[b + 1][a + 1])); TC(uB, vB); vtx((tx + 1) << 12, h11, (tz + 1) << 12);
            glColor(litrgb(SHc[b + 1][a], Tr[b + 1][a], Tg[b + 1][a], Tb[b + 1][a])); TC(uA, vB); vtx(tx << 12,       h01, (tz + 1) << 12);
            glEnd();
        }
    g_nwater = nw;
    // agua (translucida, animada)
    if (nw) {
        glBindTexture(0, texId[TEX_WATER]);
        int anim = (frameNo >> 1) & 31;
        int lastA = -1;
        for (int k = 0; k < nw; k++) {
            int idx = wlist[k]; int tx = visTx[idx], tz = visTz[idx];
            int alpha = mini(18, fade_alpha(visD2[idx]));
            if (alpha != lastA) { glPolyFmt(POLY_ALPHA(alpha) | POLY_CULL_NONE | POLY_ID(3)); lastA = alpha; }
            unsigned short c = litc(230);
            glBegin(GL_QUADS);
            glColor(c); TC(anim, 0);       vtx(tx << 12,       SEA_LEVEL, tz << 12);
            glColor(c); TC(anim + 32, 0);  vtx((tx + 1) << 12, SEA_LEVEL, tz << 12);
            glColor(c); TC(anim + 32, 32); vtx((tx + 1) << 12, SEA_LEVEL, (tz + 1) << 12);
            glColor(c); TC(anim, 32);      vtx(tx << 12,       SEA_LEVEL, (tz + 1) << 12);
            glEnd();
        }
    }
}

// ------------------------------------------------------------------ casas e entradas (3D perto / sprite longe)
static void draw_house3d(int wx, int wy, int wz, int rot) {
    int hf = U(1.2), wh = U(1.5), rh = U(1.15);
    int x0 = wx - hf, x1 = wx + hf, z0 = wz - hf, z1 = wz + hf;
    int y0 = wy - U(0.15), y1 = wy + wh;
    unsigned short cw = litc(236), cs = litc(190);
    int fx[5] = { x0, x1, x1, x0, x0 }, fz[5] = { z0, z0, z1, z1, z0 };
    for (int f = 0; f < 4; f++) {
        int ax = fx[f], az = fz[f], bx = fx[f + 1], bz = fz[f + 1];
        unsigned short c = (f & 1) ? cs : cw;
        quad4(TEX_WALL, 31, 6,
              ax, y0, az, c, 0, 40,  bx, y0, bz, c, 64, 40,  bx, y1, bz, c, 64, 0,  ax, y1, az, c, 0, 0);
        if (f == rot) {   // porta
            int nx = (f == 1) ? 90 : (f == 3 ? -90 : 0), nz = (f == 0) ? -90 : (f == 2 ? 90 : 0);
            int dxa = ax + ((bx - ax) * 35 / 100), dza = az + ((bz - az) * 35 / 100);
            int dxb = ax + ((bx - ax) * 65 / 100), dzb = az + ((bz - az) * 65 / 100);
            int dh = U(1.05);
            quad4(TEX_DOOR, 31, 6,
                  dxa + nx, y0 + U(0.15), dza + nz, c, 0, 64,  dxb + nx, y0 + U(0.15), dzb + nz, c, 32, 64,
                  dxb + nx, y0 + dh, dzb + nz, c, 32, 0,       dxa + nx, y0 + dh, dza + nz, c, 0, 0);
        }
    }
    // telhado (piramide)
    int ov = U(0.3); int rx0 = x0 - ov, rx1 = x1 + ov, rz0 = z0 - ov, rz1 = z1 + ov, yb = y1 - U(0.05), ya = y1 + rh;
    int ex[5] = { rx0, rx1, rx1, rx0, rx0 }, ez[5] = { rz0, rz0, rz1, rz1, rz0 };
    glPolyFmt(POLY_ALPHA(31) | POLY_CULL_NONE | POLY_ID(6));
    glBindTexture(0, texId[TEX_ROOF]);
    for (int f = 0; f < 4; f++) {
        unsigned short c = (f & 1) ? litc(200) : litc(250);
        glBegin(GL_TRIANGLES);
        glColor(c); TC(0, 64);  vtx(ex[f], yb, ez[f]);
        glColor(c); TC(64, 64); vtx(ex[f + 1], yb, ez[f + 1]);
        glColor(c); TC(32, 0);  vtx(wx, ya, wz);
        glEnd();
    }
}

static void draw_entrance3d(int wx, int wy, int wz) {
    int hf = U(1.4), hh = U(2.5);
    int x0 = wx - hf, x1 = wx + hf, z0 = wz - hf, z1 = wz + hf, y0 = wy - U(0.2), y1 = wy + hh;
    unsigned short cw = litc(220), cs = litc(170);
    int fx[5] = { x0, x1, x1, x0, x0 }, fz[5] = { z0, z0, z1, z1, z0 };
    for (int f = 0; f < 4; f++) {
        unsigned short c = (f & 1) ? cs : cw;
        quad4(TEX_DG_WALL, 31, 6, fx[f], y0, fz[f], c, 0, 64, fx[f + 1], y0, fz[f + 1], c, 64, 64, fx[f + 1], y1, fz[f + 1], c, 64, 0, fx[f], y1, fz[f], c, 0, 0);
    }
    quad4(TEX_ROCK, 31, 6, x0 - U(0.2), y1, z0 - U(0.2), litc(250), 0, 0, x1 + U(0.2), y1, z0 - U(0.2), litc(250), 64, 0,
          x1 + U(0.2), y1, z1 + U(0.2), litc(250), 64, 64, x0 - U(0.2), y1, z1 + U(0.2), litc(250), 0, 64);
    quad4(TEX_DG_DOOR, 31, 6, wx - U(0.55), y0 + U(0.2), z1 + 90, litc(256), 0, 64, wx + U(0.55), y0 + U(0.2), z1 + 90, litc(256), 32, 64,
          wx + U(0.55), y0 + U(1.9), z1 + 90, litc(256), 32, 0, wx - U(0.55), y0 + U(1.9), z1 + 90, litc(256), 0, 0);
}

static void draw_world_objects(void) {
    // arvores (sprites 2D)
    int budget = 1400 - nvis - g_nwater - 150;           // o DS aguenta ~1536 quads por frame
    for (int pass = 0; pass < 2; pass++)                  // arvores proximas primeiro
    for (int k = 0; k < nvis && budget > 0 && cfg.trees; k++) {
        TreeInfo ti;
        if ((visD2[k] < (R_VIEW * R_VIEW) / 4) != (pass == 0)) continue;
        if (!tree_at(visTx[k], visTz[k], &ti)) continue;
        int wx = (visTx[k] << 12) + ti.ox, wz = (visTz[k] << 12) + ti.oz;
        int wy = world_height_at(wx, wz) - U(0.05);
        int alpha = fade_alpha(visD2[k]);
        int tex = ti.type == TREE_PINE ? TEX_TREE_PINE : (ti.type == TREE_OAK ? TEX_TREE_OAK : TEX_TREE_DEAD);
        int w = ti.type == TREE_PINE ? U(1.8) : U(2.2), h = ti.type == TREE_PINE ? U(2.9) : U(2.5);
        bb(tex, wx, wy, wz, w, h, alpha, litc(220 + (int)((ti.ox >> 4) & 31)));
        budget--;
    }
    // vilas
    for (int i = 0; i < nnearH; i++) {
        int dx = nearHx[i] - P.x, dz = nearHz[i] - P.z;
        int d = adist(dx, dz);
        if (d > U(R_VIEW - 1)) continue;
        int fd = (dx * gFX + dz * gFZ) >> 12;
        if (fd < -U(3)) continue;
        int wy = world_height_at(nearHx[i], nearHz[i]);
        if (d < HOUSE_3D_DIST) draw_house3d(nearHx[i], wy, nearHz[i], nearH[i].rot);
        else bb(TEX_HOUSE_FAR, nearHx[i], wy - U(0.1), nearHz[i], U(3.2), U(3.2), fade_alpha((d >> 12) * (d >> 12)), litc(240));
    }
    // dungeons
    for (int i = 0; i < nnearF; i++) {
        if (nearF[i].type != FEAT_DUNGEON) continue;
        int wx = (nearF[i].cx << 12) + 2048, wz = (nearF[i].cz << 12) + 2048;
        int dx = wx - P.x, dz = wz - P.z, d = adist(dx, dz);
        if (d > U(R_VIEW - 1) || ((dx * gFX + dz * gFZ) >> 12) < -U(3)) continue;
        int wy = world_height_at(wx, wz);
        if (d < DG_3D_DIST) draw_entrance3d(wx, wy, wz);
        else bb(TEX_DUNGEON_FAR, wx, wy - U(0.1), wz, U(3.4), U(3.4), fade_alpha((d >> 12) * (d >> 12)), litc(240));
    }
    // aldeoes
    for (int i = 0; i < nnpc; i++) {
        int dx = npcX[i] - P.x, dz = npcZ[i] - P.z;
        if (adist(dx, dz) > U(R_VIEW - 2) || ((dx * gFX + dz * gFZ) >> 12) < -U(2)) continue;
        bb(TEX_NPC0 + npcVar[i], npcX[i], world_height_at(npcX[i], npcZ[i]) - U(0.05), npcZ[i], U(1.4), U(1.4), 31, litc(240));
    }
}

// ------------------------------------------------------------------ dungeon (render)
static unsigned short dg_light(int wx, int wz) {
    int d = adist(wx - P.x, wz - P.z);
    int fl = (frameNo & 8) ? 1 : 0;
    int v = 31 - (d * 31) / U(9.5) + fl;
    v = clampi(v, 1, 31);
    return RGB15(v, (v * 28) >> 5, (v * 20) >> 5);
}

static void draw_dungeon(void) {
    int pcx = floordiv(P.x, DG_C), pcz = floordiv(P.z, DG_C);
    for (int cz = pcz - DG_RADIUS; cz <= pcz + DG_RADIUS; cz++)
        for (int cx = pcx - DG_RADIUS; cx <= pcx + DG_RADIUS; cx++) {
            if (!dg_floor(&dg, cx, cz)) continue;
            int x0 = cx * DG_C, x1 = x0 + DG_C, z0 = cz * DG_C, z1 = z0 + DG_C, H = DG_WALL_H;
            unsigned short c00 = dg_light(x0, z0), c10 = dg_light(x1, z0), c11 = dg_light(x1, z1), c01 = dg_light(x0, z1);
            quad4(TEX_DG_FLOOR, 31, 1, x0, 0, z0, c00, 0, 0, x1, 0, z0, c10, 64, 0, x1, 0, z1, c11, 64, 64, x0, 0, z1, c01, 0, 64);
            quad4(TEX_DG_CEIL, 31, 1, x0, H, z0, c00, 0, 0, x1, H, z0, c10, 64, 0, x1, H, z1, c11, 64, 64, x0, H, z1, c01, 0, 64);
            if (!dg_floor(&dg, cx, cz - 1)) quad4(TEX_DG_WALL, 31, 1, x0, 0, z0, c00, 0, 64, x1, 0, z0, c10, 64, 64, x1, H, z0, c10, 64, 0, x0, H, z0, c00, 0, 0);
            if (!dg_floor(&dg, cx, cz + 1)) quad4(TEX_DG_WALL, 31, 1, x0, 0, z1, c01, 0, 64, x1, 0, z1, c11, 64, 64, x1, H, z1, c11, 64, 0, x0, H, z1, c01, 0, 0);
            if (!dg_floor(&dg, cx - 1, cz)) quad4(TEX_DG_WALL, 31, 1, x0, 0, z0, c00, 0, 64, x0, 0, z1, c01, 64, 64, x0, H, z1, c01, 64, 0, x0, H, z0, c00, 0, 0);
            if (!dg_floor(&dg, cx + 1, cz)) quad4(TEX_DG_WALL, 31, 1, x1, 0, z0, c10, 0, 64, x1, 0, z1, c11, 64, 64, x1, H, z1, c11, 64, 0, x1, H, z0, c10, 0, 0);
        }
    // saida
    int ex = dg.sx * DG_C + DG_C / 2, ez = dg.sz * DG_C + DG_C / 2;
    if (adist(ex - P.x, ez - P.z) < U(10)) bb(TEX_EXIT, ex, 0, ez, U(1.1), U(1.3), 31, RGB15(31, 31, 31));
    // baus
    for (int i = 0; i < dg.nch; i++) {
        int wx = dg.ch[i].cx * DG_C + DG_C / 2, wz = dg.ch[i].cz * DG_C + DG_C / 2;
        if (adist(wx - P.x, wz - P.z) < U(10)) bb(TEX_CHEST0 + (dgOpened[i] ? 1 : 0), wx, 0, wz, U(0.8), U(0.8), 31, dg_light(wx, wz));
    }
}

// ------------------------------------------------------------------ inimigos (render)
static void draw_enemies(void) {
    for (int i = 0; i < MAX_EN; i++) {
        Enemy* e = &en[i]; if (!e->active) continue;
        int dx = e->x - P.x, dz = e->z - P.z;
        int fd = (dx * gFX + dz * gFZ) >> 12;
        if (fd < -U(1) || adist(dx, dz) > U(R_VIEW)) continue;
        int base = e->type == 0 ? TEX_GOBLIN0 : TEX_SKEL0;
        int fr = e->dead_t ? 3 : (e->atk_t ? 2 : ((e->anim >> 3) & 1));
        int gy = inDungeon ? 0 : world_height_at(e->x, e->z);
        int sz = e->boss ? U(2.1) : (e->type == 0 ? U(1.25) : U(1.4));
        unsigned short col;
        if (e->hurt_t > 0) col = RGB15(31, 12, 12);
        else if (inDungeon) col = dg_light(e->x, e->z);
        else col = litc(240);
        int alpha = inDungeon ? 31 : fade_alpha((adist(dx, dz) >> 12) * (adist(dx, dz) >> 12));
        bb(base + fr, e->x, gy - U(0.04), e->z, sz, sz, alpha, col);
    }
}

// ------------------------------------------------------------------ overlay 2D (arma, tela vermelha, logo)
#define ATK_LEN_SWORD 21
#define ATK_LEN_FIST  15
static void draw_overlay_quad(int tex, int x0, int y0, int x1, int y1, int alpha, unsigned short col, int pid) {
    int tw = tex >= 0 ? gTexDefs[tex].w : 0, th = tex >= 0 ? gTexDefs[tex].h : 0;
    glPolyFmt(POLY_ALPHA(alpha) | POLY_CULL_NONE | POLY_ID(pid));
    if (tex >= 0) glBindTexture(0, texId[tex]); else glBindTexture(0, 0);
    glColor(col);
    glBegin(GL_QUADS);
    TC(0, th);  glVertex3v16(x0, y0, 4000);
    TC(tw, th); glVertex3v16(x1, y0, 4000);
    TC(tw, 0);  glVertex3v16(x1, y1, 4000);
    TC(0, 0);   glVertex3v16(x0, y1, 4000);
    glEnd();
}
static void begin_overlay(void) {
    glMatrixMode(GL_PROJECTION); glLoadIdentity(); glOrtho(-8, 8, -6, 6, -1, 1);
    glMatrixMode(GL_MODELVIEW); glLoadIdentity();
}
static void draw_weapon(void) {
    begin_overlay();
    int tex, len = P.weapon ? ATK_LEN_SWORD : ATK_LEN_FIST;
    int idx = 0;
    if (P.atk_t > 0) idx = 1 + ((P.atk_t - 1) * (P.weapon ? 3 : 2)) / len;
    tex = P.weapon ? TEX_SWORD0 + mini(idx, 3) : TEX_FIST0 + mini(idx, 2);
    int bobx = (sinLerp((P.bob * 700) & 0x7FFF) * 6) >> 12;           // pixels*... em 1/4096 de unidade
    int boby = (absi(cosLerp((P.bob * 700) & 0x7FFF)) * 5) >> 12;
    int hurt = P.hurt_t > 0;
    int x0 = -2048 + bobx * 40, y0 = -24576 + boby * 40 - 180;        // canto inferior esquerdo (v16)
    int sz = 8 * 4096;                                                 // 128 px
    draw_overlay_quad(tex, x0, y0, x0 + sz, y0 + sz, 31, hurt ? RGB15(31, 16, 16) : litc(250), 10);
    if (hurt) draw_overlay_quad(-1, -32000, -24500, 32000, 24500, 9, RGB15(31, 0, 0), 11);
}
static void draw_logo(void) {
    begin_overlay();
    draw_overlay_quad(TEX_LOGO, -32500, 6000, 32500, 22000, 31, RGB15(31, 31, 31), 10);
}

// ------------------------------------------------------------------ mundo: colisao
static int tree_blocks(int nx, int nz) {
    if (!cfg.trees) return 0;
    int tx0 = nx >> 12, tz0 = nz >> 12;
    for (int tz = tz0 - 1; tz <= tz0 + 1; tz++)
        for (int tx = tx0 - 1; tx <= tx0 + 1; tx++) {
            TreeInfo ti; if (!tree_at(tx, tz, &ti)) continue;
            if (dist2d(nx - ((tx << 12) + ti.ox), nz - ((tz << 12) + ti.oz)) < U(0.42)) return 1;
        }
    return 0;
}
static int ow_walkable(int nx, int nz, int cur_ground) {
    int g = world_height_at(nx, nz);
    if (g < SEA_LEVEL - U(0.12)) return 0;
    if (g - cur_ground > U(0.10)) return 0;
    for (int i = 0; i < nnearH; i++) if (absi(nx - nearHx[i]) < U(1.55) && absi(nz - nearHz[i]) < U(1.55)) return 0;
    for (int i = 0; i < nnearF; i++) if (nearF[i].type == FEAT_DUNGEON) {
        int wx = (nearF[i].cx << 12) + 2048, wz = (nearF[i].cz << 12) + 2048;
        if (absi(nx - wx) < U(1.7) && absi(nz - wz) < U(1.7)) return 0;
    }
    for (int i = 0; i < nnpc; i++) if (dist2d(nx - npcX[i], nz - npcZ[i]) < U(0.5)) return 0;
    if (tree_blocks(nx, nz)) return 0;
    return 1;
}
static int dg_walkable(int nx, int nz) {
    int r = U(0.3);
    return dg_floor(&dg, floordiv(nx - r, DG_C), floordiv(nz - r, DG_C)) && dg_floor(&dg, floordiv(nx + r, DG_C), floordiv(nz - r, DG_C)) &&
           dg_floor(&dg, floordiv(nx - r, DG_C), floordiv(nz + r, DG_C)) && dg_floor(&dg, floordiv(nx + r, DG_C), floordiv(nz + r, DG_C));
}

// ------------------------------------------------------------------ inimigos (logica)
static int stat_hp(int type, int boss) { return boss ? 90 : (type == 0 ? 20 : 28); }
static void spawn_enemy(int type, int boss, int x, int z) {
    for (int i = 0; i < MAX_EN; i++) if (!en[i].active) {
        memset(&en[i], 0, sizeof(Enemy));
        en[i].active = 1; en[i].type = type; en[i].boss = boss; en[i].x = x; en[i].z = z;
        en[i].hp = en[i].maxhp = stat_hp(type, boss) + P.level * 2; en[i].cd = 30; return;
    }
}
static void player_hurt(int dmg) {
    P.hp -= dmg; P.hurt_t = 14;
    if (P.hp <= 0) { P.hp = 0; gstate = ST_DEAD; }
}
static void give_xp(int xp) {
    P.xp += xp;
    while (P.xp >= 40 * P.level) { P.xp -= 40 * P.level; P.level++; P.maxhp += 10; P.hp = P.maxhp; say("SUBIU DE NIVEL!"); }
}
static void update_enemies(void) {
    for (int i = 0; i < MAX_EN; i++) {
        Enemy* e = &en[i]; if (!e->active) continue;
        if (e->hurt_t > 0) e->hurt_t--;
        if (e->dead_t) { if (++e->dead_t > 420) e->active = 0; continue; }
        int dx = P.x - e->x, dz = P.z - e->z, d = dist2d(dx, dz);
        if (!inDungeon && d > U(24)) { e->active = 0; continue; }
        int spd = e->boss ? U(0.016) : (e->type == 0 ? U(0.021) : U(0.018));
        int chase = inDungeon ? U(8) : U(10);
        if (e->atk_t > 0) {
            e->atk_t++;
            if (e->atk_t == 14 && d < U(1.7)) player_hurt(e->boss ? 8 + rnd(7) : (e->type == 0 ? 3 + rnd(4) : 4 + rnd(5)));
            if (e->atk_t > 28) { e->atk_t = 0; e->cd = 40; }
        } else if (d < chase) {
            if (d > U(1.15)) {
                int sx = (dx * spd) / (d ? d : 1), sz = (dz * spd) / (d ? d : 1);
                int nx = e->x + sx, nz = e->z + sz;
                if (inDungeon) { if (dg_walkable(nx, e->z)) e->x = nx; if (dg_walkable(e->x, nz)) e->z = nz; }
                else {
                    int g = world_height_at(e->x, e->z);
                    if (ow_walkable(nx, nz, g + U(0.1))) { e->x = nx; e->z = nz; }
                }
                e->anim++;
            } else if (e->cd > 0) e->cd--; else e->atk_t = 1;
            if (e->cd > 0 && d > U(1.15)) e->cd--;
        }
    }
}
static void player_attack_hit(void) {
    int dmg_lo = P.weapon ? 8 : 2, dmg_hi = P.weapon ? 14 : 5;
    int range = P.weapon ? U(1.9) : U(1.4);
    int hit = 0;
    for (int i = 0; i < MAX_EN; i++) {
        Enemy* e = &en[i]; if (!e->active || e->dead_t) continue;
        int dx = e->x - P.x, dz = e->z - P.z, d = dist2d(dx, dz);
        if (d > range) continue;
        if (d > U(0.5) && ((dx * gFX + dz * gFZ) >> 12) * 10 < d * 6) continue;       // cone frontal
        int dmg = dmg_lo + rnd(dmg_hi - dmg_lo + 1) + P.level - 1;
        e->hp -= dmg; e->hurt_t = 8; e->atk_t = 0; e->cd = 20; hit = 1;
        if (e->hp <= 0) {
            e->dead_t = 1; P.kills++;
            give_xp(e->boss ? 120 : (e->type == 0 ? 14 : 20));
            int g = 3 + rnd(9) + (e->boss ? 60 : 0); P.gold += g;
            if (rnd(100) < 25) { P.potions++; say("Voce achou uma pocao!"); }
        }
    }
    if (!hit) { /* errou */ }
}

// ------------------------------------------------------------------ dialogos
static const char* const TALK[] = {
    "Bem-vindo, viajante!\nCuidado com os goblins\nao anoitecer.",
    "Dizem que ha uma dungeon\ncheia de esqueletos\nlogo ao norte...",
    "Um bom aventureiro leva\npocoes. Aperte SELECT\npara beber uma.",
    "O chefe da dungeon\nguarda o melhor tesouro.",
    "Nao ha lobos por aqui,\nso goblins. E mosquitos!"
};

// ------------------------------------------------------------------ entrar / sair da dungeon
static void enter_dungeon(const Feature* f) {
    ow_x = P.x; ow_z = P.z; ow_yaw = P.yaw;
    curDgSeed = f->seed;
    dungeon_generate(&dg, f->seed);
    memset(dgOpened, 0, sizeof dgOpened);
    memset(en, 0, sizeof en);
    inDungeon = 1;
    P.x = dg.sx * DG_C + DG_C / 2; P.z = dg.sz * DG_C + DG_C / 2; P.y = EYE;
    int best = -1, bestdir = 0; static const int ddx[4] = { 0, 1, 0, -1 }, ddz[4] = { -1, 0, 1, 0 };
    for (int k = 0; k < 4; k++) { int n = 0; while (n < 8 && dg_floor(&dg, dg.sx + ddx[k] * (n + 1), dg.sz + ddz[k] * (n + 1))) n++; if (n > best) { best = n; bestdir = k; } }
    P.yaw = bestdir * 8192;
    for (int i = 0; i < dg.nen; i++) spawn_enemy(1, dg.en[i].boss, dg.en[i].cx * DG_C + DG_C / 2, dg.en[i].cz * DG_C + DG_C / 2);
    say("Voce entrou na dungeon...");
}
static void leave_dungeon(void) {
    inDungeon = 0; memset(en, 0, sizeof en);
    P.x = ow_x; P.z = ow_z; P.yaw = (ow_yaw + 16384) & 0x7FFF; P.ground = world_height_at(P.x, P.z); P.y = P.ground + EYE;
    spawnTimer = 300;
    say("De volta ao mundo!");
}

// ------------------------------------------------------------------ jogador (update)
static const char* prompt = "";
static void update_player(void) {
    int held = keysHeld(), down = keysDown();
    // giro
    if (held & KEY_LEFT)  P.yaw -= TURN_SPEED;
    if (held & KEY_RIGHT) P.yaw += TURN_SPEED;
    // camera livre pela tela de toque: arraste para girar e olhar para cima/baixo; toque rapido = centralizar
    {
        static int tActive = 0, tPx = 0, tPy = 0, tFrames = 0, tMove = 0;
        if (held & KEY_TOUCH) {
            touchPosition t; touchRead(&t);
            if (!tActive) { tActive = 1; tFrames = 0; tMove = 0; }
            else {
                int dx = t.px - tPx, dy = t.py - tPy;
                if (absi(dx) < 90 && absi(dy) < 90) {
                    P.yaw += dx * 30 * cfg.sens;
                    P.pitch += (cfg.invertY ? dy : -dy) * 23 * cfg.sens;
                    tMove += absi(dx) + absi(dy);
                }
            }
            tPx = t.px; tPy = t.py; tFrames++;
        } else {
            if (tActive && tFrames < 14 && tMove < 8) P.pitch = 0;
            tActive = 0;
        }
        P.pitch = clampi(P.pitch, -PITCH_MAX, PITCH_MAX);
    }
    P.yaw &= 0x7FFF;
    int fx = sinLerp(P.yaw), fz = -cosLerp(P.yaw), rx = cosLerp(P.yaw), rz = sinLerp(P.yaw);
    int spd = (held & KEY_B) ? RUN_SPEED : WALK_SPEED;
    int mx = 0, mz = 0, moving = 0;
    if (held & KEY_UP)   { mx += fx; mz += fz; moving = 1; }
    if (held & KEY_DOWN) { mx -= fx; mz -= fz; moving = 1; }
    if (held & KEY_R)    { mx += rx; mz += rz; moving = 1; }
    if (held & KEY_L)    { mx -= rx; mz -= rz; moving = 1; }
    int stepx = (mx * spd) >> 12, stepz = (mz * spd) >> 12;
    if (moving) {
        int nx = P.x + stepx, nz = P.z + stepz;
        if (inDungeon) { if (dg_walkable(nx, P.z)) P.x = nx; if (dg_walkable(P.x, nz)) P.z = nz; }
        else { if (ow_walkable(nx, P.z, P.ground)) P.x = nx; if (ow_walkable(P.x, nz, P.ground)) P.z = nz; }
        P.bob += (held & KEY_B) ? 3 : 2;
    }
    if (inDungeon) { P.ground = 0; P.y = EYE; }
    else { P.ground = world_height_at(P.x, P.z); int target = P.ground + EYE; P.y += (target - P.y) >> 2; }
    // trocar arma / pocao
    if (down & KEY_X) { P.weapon ^= 1; say(P.weapon ? "Espada equipada" : "Punhos livres"); }
    if ((down & KEY_SELECT) && P.potions > 0 && P.hp < P.maxhp) { P.potions--; P.hp = mini(P.maxhp, P.hp + 40); say("Voce bebeu uma pocao (+40)"); }
    // ataque
    int len = P.weapon ? ATK_LEN_SWORD : ATK_LEN_FIST;
    if ((down & KEY_A) && P.atk_t == 0) P.atk_t = 1;
    else if (P.atk_t > 0) {
        P.atk_t++;
        if (P.atk_t == (len * 2) / 3) player_attack_hit();
        if (P.atk_t > len) P.atk_t = 0;
    }
    if (P.hurt_t > 0) P.hurt_t--;
    // interacao (Y)
    prompt = "";
    if (inDungeon) {
        int ex = dg.sx * DG_C + DG_C / 2, ez = dg.sz * DG_C + DG_C / 2;
        if (dist2d(P.x - ex, P.z - ez) < U(1.8)) { prompt = "Y: sair da dungeon"; if (down & KEY_Y) { leave_dungeon(); return; } }
        for (int i = 0; i < dg.nch; i++) {
            int wx = dg.ch[i].cx * DG_C + DG_C / 2, wz = dg.ch[i].cz * DG_C + DG_C / 2;
            if (!dgOpened[i] && dist2d(P.x - wx, P.z - wz) < U(1.5)) {
                prompt = "Y: abrir bau";
                if (down & KEY_Y) { dgOpened[i] = 1; int g = 15 + rnd(30) + P.level * 5; P.gold += g; char b[40]; snprintf(b, 40, "Bau: +%d de ouro!", g); say(b); if (rnd(100) < 50) P.potions++; }
            }
        }
    } else {
        for (int i = 0; i < nnpc; i++) if (dist2d(P.x - npcX[i], P.z - npcZ[i]) < U(1.9)) {
            prompt = "Y: conversar";
            if (down & KEY_Y) { unsigned idx = (unsigned)((npcX[i] >> 12) + (npcZ[i] >> 12) + P.kills + (frameNo >> 8)) % 5u; say(TALK[idx]); msgT = 420; }
        }
        for (int i = 0; i < nnearF; i++) if (nearF[i].type == FEAT_DUNGEON) {
            int wx = (nearF[i].cx << 12) + 2048, wz = (nearF[i].cz << 12) + 2048;
            if (dist2d(P.x - wx, P.z - wz) < U(2.6)) { prompt = "Y: entrar na dungeon"; if (down & KEY_Y) { enter_dungeon(&nearF[i]); return; } }
        }
    }
}

static void update_spawns(void) {
    if (inDungeon || !cfg.monsters) return;
    if (--spawnTimer > 0) return;
    spawnTimer = (dayf < 1200) ? 140 : 300;
    int cnt = 0; for (int i = 0; i < MAX_EN; i++) if (en[i].active && !en[i].dead_t) cnt++;
    if (cnt >= ((dayf < 1200) ? 8 : 4)) return;
    for (int tries = 0; tries < 8; tries++) {
        int ang = rnd(32768), r = U(9) + rnd(U(3));
        int x = P.x + ((sinLerp(ang) * r) >> 12), z = P.z - ((cosLerp(ang) * r) >> 12);
        int g = world_height_at(x, z);
        if (g < U(0.9) || g > U(12)) continue;
        int safe = 1; for (int i = 0; i < nnearF; i++) if (nearF[i].type == FEAT_VILLAGE && dist2d(x - (nearF[i].cx << 12), z - (nearF[i].cz << 12)) < U(14)) safe = 0;
        if (!safe || !ow_walkable(x, z, g + U(0.1))) continue;
        spawn_enemy((dayf < 1200 && rnd(100) < 55) ? 1 : 0, 0, x, z);
        break;
    }
}

// ------------------------------------------------------------------ texto (tela de baixo)
static void put(int row, const char* s) { iprintf("\x1b[%d;0H%-32s", row, s); }

static void hud_minimap(void) {
    char line[34];
    if (inDungeon) {
        int pcx = floordiv(P.x, DG_C), pcz = floordiv(P.z, DG_C);
        for (int r = 0; r < 11; r++) {
            for (int c = 0; c < 31; c++) {
                int cx = pcx + c - 15, cz = pcz + r - 5;
                char ch = ' ';
                if (cx >= 0 && cz >= 0 && cx < DG_W && cz < DG_H) ch = dg.cell[cz][cx] ? '.' : '#';
                if (cx == dg.sx && cz == dg.sz) ch = '<';
                line[c] = ch;
            }
            line[31] = 0;
            for (int i = 0; i < dg.nch; i++) { int c = dg.ch[i].cx - pcx + 15, rr = dg.ch[i].cz - pcz + 5; if (rr == r && c >= 0 && c < 31 && !dgOpened[i]) line[c] = '$'; }
            for (int i = 0; i < MAX_EN; i++) if (en[i].active && !en[i].dead_t) { int c = floordiv(en[i].x, DG_C) - pcx + 15, rr = floordiv(en[i].z, DG_C) - pcz + 5; if (rr == r && c >= 0 && c < 31) line[c] = en[i].boss ? 'B' : 'e'; }
            if (r == 5) line[15] = '@';
            put(6 + r, line);
        }
        return;
    }
    int cx = P.x >> 12, cz = P.z >> 12;
    for (int r = 0; r < 11; r++) {
        for (int c = 0; c < 31; c++) { char ch = (char)world_biome_char(cx + (c - 15) * 3, cz + (r - 5) * 3); if (ch == 'f' && !cfg.trees) ch = '.'; line[c] = ch; }
        line[31] = 0;
        for (int i = 0; i < nnearF; i++) {
            int c = (nearF[i].cx - cx + (nearF[i].cx - cx >= 0 ? 1 : -1)) / 3 + 15, rr = (nearF[i].cz - cz + (nearF[i].cz - cz >= 0 ? 1 : -1)) / 3 + 5;
            if (rr == r && c >= 0 && c < 31) line[c] = nearF[i].type == FEAT_VILLAGE ? 'V' : 'D';
        }
        for (int i = 0; i < MAX_EN; i++) if (en[i].active && !en[i].dead_t) {
            int c = ((en[i].x >> 12) - cx) / 3 + 15, rr = ((en[i].z >> 12) - cz) / 3 + 5;
            if (rr == r && c >= 0 && c < 31) line[c] = 'e';
        }
        if (r == 5) line[15] = '@';
        put(6 + r, line);
    }
}

static const char* compass(void) {
    int a = ((P.yaw & 0x7FFF) + 2048) >> 12;      // 0..7
    static const char* const N[8] = { "N ", "NE", "L ", "SE", "S ", "SO", "O ", "NO" };
    return N[a & 7];
}

static void hud_update(void) {
    char b[48];
    if (cfg.fps) snprintf(b, sizeof b, "DAGGERFALL DS  Nv %d  FPS %d", P.level, fpsShown); else snprintf(b, sizeof b, "DAGGERFALL DS     Nv %d", P.level);
    put(0, b);
    int bars = (P.hp * 12) / P.maxhp; if (bars < 0) bars = 0;
    char bar[13]; for (int i = 0; i < 12; i++) bar[i] = i < bars ? '#' : '-'; bar[12] = 0;
    snprintf(b, sizeof b, "PV [%s] %d/%d", bar, P.hp, P.maxhp); put(1, b);
    snprintf(b, sizeof b, "XP %d/%d  Ouro %d  Pocoes %d", P.xp, 40 * P.level, P.gold, P.potions); put(2, b);
    snprintf(b, sizeof b, "%s  Dir %s  Abates %d", P.weapon ? "Espada" : "Punhos", compass(), P.kills); put(3, b);
    put(4, prompt[0] ? prompt : (inDungeon ? "Dungeon" : ((dayf > 2000) ? "Dia" : "Noite")));
    int multi = (msgT > 0 && strchr(msg, '\n'));
    if (!multi) put(5, msgT > 0 ? msg : "");
    hud_minimap();
    put(17, "~agua f mata V vila D dung e");
    put(18, "A atacar  B correr  X arma");
    put(19, "L/R lado  Y agir  SELECT pocao");
    put(20, "START pausa  Tela: olhar");
    if (multi) {   // mensagens multilinha (dialogos)
        char tmp[120]; strncpy(tmp, msg, 119); tmp[119] = 0; char* l = tmp; int row = 21;
        while (l && row < 24) { char* nl = strchr(l, '\n'); if (nl) *nl = 0; put(row++, l); l = nl ? nl + 1 : 0; }
        put(5, "");
    } else { put(21, ""); put(22, ""); put(23, ""); }
}

// ------------------------------------------------------------------ cenas
// ------------------------------------------------------------------ ceu (sol, lua, nuvens) - sincronizado com worldTime
static short sclamp(int v) { return (short)(v > 32767 ? 32767 : (v < -32767 ? -32767 : v)); }
static inline void vtxs(int x, int y, int z) { glVertex3v16(sclamp(x >> 3), sclamp(y >> 3), sclamp(z >> 3)); }

// quad voltado para a camera, na direcao d (vetor unitario 4.12) a distancia D; vertices relativos a camera (ceu = no infinito)
static void sky_quad(int tex, int dx, int dy, int dz, int D, int hw, int hh, int alpha, unsigned short col, int pid) {
    int hyp = isqrt((unsigned)(dx * dx + dz * dz)), rx, rz;
    if (hyp < 300) { rx = gRX; rz = gRZ; } else { rx = (-dz * 4096) / hyp; rz = (dx * 4096) / hyp; }
    int ux = (-rz * dy) >> 12, uy = (rz * dx - rx * dz) >> 12, uz = (rx * dy) >> 12;      // u = r x d
    int cx = (dx * D) >> 12, cy = (dy * D) >> 12, cz = (dz * D) >> 12;
    int rwx = (rx * hw) >> 12, rwz = (rz * hw) >> 12;
    int uhx = (ux * hh) >> 12, uhy = (uy * hh) >> 12, uhz = (uz * hh) >> 12;
    int tw = gTexDefs[tex].w, th = gTexDefs[tex].h;
    glPolyFmt(POLY_ALPHA(alpha) | POLY_CULL_NONE | POLY_ID(pid));
    glBindTexture(0, texId[tex]);
    glColor(col);
    glBegin(GL_QUADS);
    TC(0, th);  vtxs(cx - rwx - uhx, cy - uhy, cz - rwz - uhz);
    TC(tw, th); vtxs(cx + rwx - uhx, cy - uhy, cz + rwz - uhz);
    TC(tw, 0);  vtxs(cx + rwx + uhx, cy + uhy, cz + rwz + uhz);
    TC(0, 0);   vtxs(cx - rwx + uhx, cy + uhy, cz - rwz + uhz);
    glEnd();
}

static void draw_sky(void) {
    if (inDungeon) return;
    int D = (R_VIEW + 7) << 12;
    glMatrixMode(GL_MODELVIEW); glLoadIdentity();                 // so rotacao (o ceu nao tem paralaxe)
    if (P.pitch) glRotateXi(-PITCH_SIGN * P.pitch);
    glRotateYi(VIEW_ROT_SIGN * (P.yaw & 0x7FFF));
    glScalef32(inttof32(8), inttof32(8), inttof32(8));
    int ang = (worldTime * 32768 / DAY_LEN) & 0x7FFF;             // mesma fase do ciclo dia/noite
    int ca = cosLerp(ang), sa = sinLerp(ang);
    int sdx = (ca * 3900) >> 12, sdy = (sa * 3900) >> 12, sdz = 1250;   // sol: nasce a leste (+x), poe a oeste, inclinado p/ sul
    if (sdy > -250) sky_quad(TEX_SUN, sdx, sdy, sdz, D, (D * 300) >> 12, (D * 300) >> 12, 31, RGB15(31, 31, 31), 7);
    if (-sdy > -250) sky_quad(TEX_MOON, -sdx, -sdy, -sdz, D, (D * 240) >> 12, (D * 240) >> 12, 31, RGB15(31, 31, 31), 8);
    if (cfg.clouds) {
        static int init = 0, caz[18], cel[18], csz[18];
        if (!init) {
            for (int i = 0; i < 18; i++) {
                unsigned h = hash3(i, 77, 5);
                caz[i] = (int)(h & 0x7FFF); int r = (int)((h >> 15) % 100);
                cel[i] = 900 + (r * r * 5500) / 10000;               // mais nuvens perto do horizonte
                csz[i] = 650 + (int)((h >> 22) % 350);
            }
            init = 1;
        }
        unsigned short col = RGB15(mini(31, (31 * ambR) >> 8), mini(31, (31 * ambG) >> 8), mini(31, (31 * ambB) >> 8));
        int drift = frameNo >> 2;                                    // vento lento
        for (int i = 0; i < 18; i++) {
            int az = (caz[i] + drift) & 0x7FFF, ce = cosLerp(cel[i]);
            int dx = (sinLerp(az) * ce) >> 12, dz = -((cosLerp(az) * ce) >> 12), dy = sinLerp(cel[i]);
            int hw = (D * csz[i]) >> 12;
            sky_quad(TEX_CLOUD0 + (i % 3), dx, dy, dz, D, hw, hw >> 1, 22, col, 9);
        }
    }
    set_modelview();                                                // restaura a matriz do mundo
}

static void set_sky(void) {
    if (inDungeon) { glClearColor(0, 0, 0, 31); return; }
    int ang = (worldTime * 32768 / DAY_LEN) & 0x7FFF;
    int warm = clampi(4096 - absi(sinLerp(ang)) * 3, 0, 4096);       // forte quando o sol esta no horizonte
    int r = 3 + ((dayf * 13) >> 12) + ((warm * 10) >> 12), g = 4 + ((dayf * 18) >> 12) + ((warm * 4) >> 12), b = 9 + ((dayf * 20) >> 12) - ((warm * 6) >> 12);
    glClearColor(mini(31, r + 2), mini(31, g + 3), clampi(b + 2, 2, 31), 31);
}

static void render_scene(void) {
    setup_camera();
    set_sky();
    if (inDungeon) { draw_dungeon(); draw_enemies(); }
    else { draw_sky(); build_near_lists(); draw_terrain(); draw_world_objects(); draw_enemies(); }
}

static void new_game(void) {
    memset(&P, 0, sizeof P); memset(en, 0, sizeof en);
    P.hp = P.maxhp = 100; P.level = 1; P.weapon = 1; P.potions = 1;
    world_find_start(&P.x, &P.z);
    P.ground = world_height_at(P.x, P.z); P.y = P.ground + EYE; P.yaw = 0;
    inDungeon = 0; worldTime = DAY_LEN / 8; spawnTimer = 200; msgT = 0;
    gstate = ST_PLAY; paused = 0;
    consoleClear();
    say("Procure uma dungeon (D) !");
}

// ------------------------------------------------------------------ titulo
// ------------------------------------------------------------------ configuracoes
static int inSettings = 0, settingsFromTitle = 0, setSel = 0;
#define NSET 8
static void title_draw_menu(void);
static void pause_draw(void) {
    consoleClear();
    put(8, "       -- PAUSA --"); put(10, "START: continuar"); put(12, "A: configuracoes"); put(14, "SELECT: voltar ao titulo");
}
static void settings_draw(void) {
    char b[40];
    put(0, "        CONFIGURACOES");
    snprintf(b, sizeof b, "%c Distancia render   %2d", setSel == 0 ? '>' : ' ', gRView); put(3, b);
    snprintf(b, sizeof b, "%c Mostrar FPS        %s", setSel == 1 ? '>' : ' ', cfg.fps ? "SIM" : "NAO"); put(5, b);
    snprintf(b, sizeof b, "%c Arvores            %s", setSel == 2 ? '>' : ' ', cfg.trees ? "SIM" : "NAO"); put(7, b);
    snprintf(b, sizeof b, "%c Monstros (mundo)   %s", setSel == 3 ? '>' : ' ', cfg.monsters ? "SIM" : "NAO"); put(9, b);
    snprintf(b, sizeof b, "%c Nuvens             %s", setSel == 4 ? '>' : ' ', cfg.clouds ? "SIM" : "NAO"); put(11, b);
    snprintf(b, sizeof b, "%c Sensib. camera     %d", setSel == 5 ? '>' : ' ', cfg.sens); put(13, b);
    snprintf(b, sizeof b, "%c Inverter eixo Y    %s", setSel == 6 ? '>' : ' ', cfg.invertY ? "SIM" : "NAO"); put(15, b);
    snprintf(b, sizeof b, "%c Voltar", setSel == 7 ? '>' : ' '); put(17, b);
    put(20, "Cima/Baixo: escolher   B: sair"); put(21, "Esq/Dir ou A: mudar  (dungeon");
    put(22, "sempre tem monstros)");
}
static void settings_change(int i, int act) {            // act: -1 esquerda, +1 direita, 2 = A
    switch (i) {
    case 0: gRView += (act == -1) ? -2 : 2; if (gRView > R_MAX) gRView = (act == 2) ? 10 : R_MAX; if (gRView < 10) gRView = 10; break;
    case 1: cfg.fps ^= 1; break;
    case 2: cfg.trees ^= 1; break;
    case 3: cfg.monsters ^= 1; if (!cfg.monsters && !inDungeon) memset(en, 0, sizeof en); break;
    case 4: cfg.clouds ^= 1; break;
    case 5: cfg.sens += (act == -1) ? -1 : 1; if (cfg.sens > 5) cfg.sens = (act == 2) ? 1 : 5; if (cfg.sens < 1) cfg.sens = 1; break;
    case 6: cfg.invertY ^= 1; break;
    }
}
static void settings_exit(void) {
    inSettings = 0; consoleClear();
    if (settingsFromTitle) title_draw_menu(); else pause_draw();
}
static void settings_update(void) {
    int down = keysDown(), act = 0;
    if (down & KEY_UP)   setSel = (setSel + NSET - 1) % NSET;
    if (down & KEY_DOWN) setSel = (setSel + 1) % NSET;
    if (down & KEY_LEFT) act = -1;
    if (down & KEY_RIGHT) act = 1;
    if (down & KEY_A) act = 2;
    if (down & KEY_TOUCH) {
        touchPosition t; touchRead(&t); int row = t.py / 8;
        if (row >= 3 && row <= 18) { setSel = (row - 3) / 2; act = 2; }
    }
    if (down & (KEY_B | KEY_START)) { settings_exit(); return; }
    if (act) { if (setSel == 7) { settings_exit(); return; } settings_change(setSel, act); }
    if (down) settings_draw();
}

// ------------------------------------------------------------------ titulo
static void title_draw_menu(void) {
    put(1, "");
    put(3, "   D A G G E R F A L L   D S");
    put(4, "     (teste tecnico - NDS)");
    if (helpPage == 0) {
        put(8,  menuSel == 0 ? "      > NOVO JOGO" : "        NOVO JOGO");
        put(10, menuSel == 1 ? "      > CONFIGURACOES" : "        CONFIGURACOES");
        put(12, menuSel == 2 ? "      > COMO JOGAR" : "        COMO JOGAR");
        put(14, menuSel == 3 ? "      > SOBRE" : "        SOBRE");
        put(18, "  Cima/Baixo + A, ou toque");
        for (int r = 19; r < 24; r++) put(r, "");
    } else if (helpPage == 1) {
        put(6, "Cima/Baixo  andar");  put(7, "Esq/Dir     girar");   put(8, "L / R       andar de lado");
        put(9, "B           correr");  put(10, "A           atacar");  put(11, "X           espada/punhos");
        put(12, "Y           falar/abrir/entrar"); put(13, "SELECT      beber pocao"); put(14, "START       pausa");
        put(16, "Arrastar na tela: olhar em"); put(17, "volta (cima/baixo tambem)");
        put(18, "Toque rapido: centralizar");
        put(21, "       (A para voltar)");
    } else {
        put(7, "Mundo procedural (Perlin),");  put(8, "vilas, dungeons e arvores.");
        put(9, "Sprites 2D de longe, 3D de");   put(10, "perto. Feito com libnds.");
        put(12, "Texturas geradas por codigo"); put(13, "(sem licenca necessaria).");
        put(21, "       (A para voltar)");
    }
}

static void title_update(void) {
    int down = keysDown();
    if (helpPage) { if (down & (KEY_A | KEY_B | KEY_TOUCH)) { helpPage = 0; consoleClear(); title_draw_menu(); } return; }
    if (down & KEY_UP)   menuSel = (menuSel + 3) % 4;
    if (down & KEY_DOWN) menuSel = (menuSel + 1) % 4;
    int pick = -1;
    if (down & KEY_A) pick = menuSel;
    if (down & KEY_TOUCH) {
        touchPosition t; touchRead(&t); int row = t.py / 8;
        if (row >= 8 && row <= 15) { pick = (row - 8) / 2; menuSel = pick; }
    }
    if (down & (KEY_UP | KEY_DOWN | KEY_TOUCH)) title_draw_menu();
    if (pick == 0) new_game();
    else if (pick == 1) { inSettings = 1; settingsFromTitle = 1; setSel = 0; consoleClear(); settings_draw(); }
    else if (pick == 2) { helpPage = 1; consoleClear(); title_draw_menu(); }
    else if (pick == 3) { helpPage = 2; consoleClear(); title_draw_menu(); }
}

static void title_render(void) {
    static int tt = 0; tt++;
    // camera orbitando devagar sobre o terreno
    static int cx, cz, have = 0; if (!have) { world_find_start(&cx, &cz); have = 1; }
    P.x = cx + ((sinLerp((tt * 6) & 0x7FFF) * U(3)) >> 12); P.z = cz - ((cosLerp((tt * 6) & 0x7FFF) * U(3)) >> 12);
    P.yaw = (tt * 20) & 0x7FFF; P.ground = world_height_at(P.x, P.z); P.y = P.ground + U(1.5);
    inDungeon = 0; dayf = 4096; ambR = ambG = ambB = 256; frameNo++;
    build_near_lists();
    P.pitch = 1500;
    setup_camera(); set_sky(); draw_sky();
    draw_terrain(); draw_world_objects();
    draw_logo();
}


#ifdef HOST_TEST
// gancho usado apenas pelo teste no PC (tools/host_test.sh): teleporta p/ uma dungeon e volta
static void host_test_hook(void) {
    { extern unsigned long g_noise_calls; static unsigned long last; if (frameNo <= 8 || frameNo % 500 == 0) printf("  [f=%d] noise2/frame=%lu\n", frameNo, g_noise_calls - last); last = g_noise_calls; }
    if (frameNo % 1000 == 0) printf("  clamps(v16)=%d nvis=%d\n", g_clamps, nvis);
    if (frameNo % 500 == 0) { int na = 0; for (int i = 0; i < MAX_EN; i++) na += en[i].active; printf("f=%d state=%d dg=%d pos=(%.1f,%.1f) h=%.2f hp=%d lv=%d kills=%d gold=%d enemies=%d day=%d\n", frameNo, gstate, inDungeon, P.x / 4096.0, P.z / 4096.0, P.ground / 4096.0, P.hp, P.level, P.kills, P.gold, na, dayf); }
    if (gstate != ST_PLAY) return;
    if (frameNo == 400) {
        for (int ring = 0; ring < 4; ring++) for (int rz = -ring; rz <= ring; rz++) for (int rx = -ring; rx <= ring; rx++) {
            Feature f = world_feature(rx, rz);
            if (f.type == FEAT_DUNGEON && !inDungeon) { P.x = (f.cx << 12) + 2048; P.z = (f.cz << 12) + 2048 + U(2.2); enter_dungeon(&f); }
        }
    }
    if (frameNo == 1800 && inDungeon) leave_dungeon();
    if (frameNo == 2000) { P.hp = P.maxhp; }
    if (frameNo == 3302 || frameNo == 3338 || frameNo == 3342 || frameNo == 3350) printf("  [menu f=%d] paused=%d inSettings=%d sel=%d  R=%d fps=%d trees=%d monst=%d nuvens=%d sens=%d invY=%d\n", frameNo, paused, inSettings, setSel, gRView, cfg.fps, cfg.trees, cfg.monsters, cfg.clouds, cfg.sens, cfg.invertY);
    if (frameNo == 1000) gRView = 30;
    if (frameNo == 1500) { settings_change(2, 2); settings_change(3, 2); settings_change(4, 2); }
    if (frameNo == 2600) { settings_change(2, 2); settings_change(3, 2); settings_change(4, 2); gRView = 14; }
    if (frameNo == 3200) gRView = 10;
    if (frameNo == 4000) gRView = 22;
}
#endif

// ------------------------------------------------------------------ main
int main(void) {
#ifndef HOST_TEST
    MY_POWCNT |= 0x820F;            // LCD, 2D A, 3D render+geometria, 2D B, engine A na tela de cima
#endif
    videoSetMode(MODE_0_3D);
    videoSetModeSub(MODE_0_2D);
    vramSetBankA(VRAM_A_TEXTURE); vramSetBankB(VRAM_B_TEXTURE); vramSetBankC(VRAM_C_TEXTURE); vramSetBankD(VRAM_D_TEXTURE);
    vramSetBankH(VRAM_H_SUB_BG);
    consoleInit(NULL, 0, BgType_Text4bpp, BgSize_T_256x256, 15, 0, false, true);
    BG_PALETTE_SUB[0] = RGB15(2, 3, 8);
    BG_PALETTE_SUB[255] = RGB15(31, 31, 28);
#ifdef HAVE_EXC_HANDLER
    defaultExceptionHandler();      // em caso de crash mostra uma tela com o erro
#endif
    put(0, "Daggerfall DS - iniciando...");
    put(1, "1/4 video ok");

    glInit();
    put(2, "2/4 3D ok");
    glEnable(GL_TEXTURE_2D | GL_ANTIALIAS | GL_BLEND);
    glClearColor(10, 16, 26, 31);
    glClearPolyID(63);
    glClearDepth(0x7FFF);
    glViewport(0, 0, 255, 191);
    load_textures();
    put(3, "3/4 texturas ok");
    world_init(SEED);
    put(4, "4/4 mundo ok");
    for (int i = 0; i < 20; i++) wait_vbl();    // mostra as etapas por um instante
    consoleClear();
    rngs ^= (unsigned)(SEED * 7919u);

    fps_init();
    title_draw_menu();
    int hudTick = 0;
    while (1) {
        scanKeys();
        frameNo++;
#ifdef HOST_TEST
        host_test_hook();
#endif
        if (msgT > 0) msgT--;
        switch (gstate) {
        case ST_TITLE:
            if (inSettings) settings_update(); else title_update();
            title_render();
            break;
        case ST_PLAY:
            if (inSettings) { settings_update(); }
            else {
            if (keysDown() & KEY_START) { paused ^= 1; if (paused) pause_draw(); else consoleClear(); }
            if (paused) {
                if (keysDown() & KEY_A) { inSettings = 1; settingsFromTitle = 0; setSel = 0; consoleClear(); settings_draw(); }
                else if (keysDown() & KEY_SELECT) { gstate = ST_TITLE; paused = 0; inDungeon = 0; consoleClear(); title_draw_menu(); }
            } else {
                worldTime = (worldTime + 1) % DAY_LEN;
                update_daylight();
                if (!inDungeon) build_near_lists();
                update_player();
                if (gstate == ST_PLAY) { update_enemies(); update_spawns(); }
                if (++hudTick >= 6) { hudTick = 0; if (gstate == ST_PLAY) hud_update(); }
            }
            }
            update_daylight();
            render_scene();
            draw_weapon();
            if (gstate == ST_DEAD) { consoleClear(); put(8, "      VOCE MORREU"); put(10, "  Pressione A para renascer"); }
            break;
        case ST_DEAD:
            if (keysDown() & KEY_A) {
                int g = P.gold / 2, lv = P.level, mh = P.maxhp, k = P.kills, xp = P.xp, po = P.potions;
                new_game(); P.gold = g; P.level = lv; P.maxhp = mh; P.hp = mh; P.kills = k; P.xp = xp; P.potions = po;
            }
            update_daylight();
            render_scene();
            break;
        }
        glFlush(0);
        fps_tick();
        wait_vbl();
    }
    return 0;
}
