// Mock da libnds usado SO no PC (tools/host_test.sh): checa sintaxe e simula teclas/frames.
#pragma once
#include <stdio.h>
#include <stdbool.h>
#include <stdlib.h>
#include <math.h>
typedef unsigned short u16; typedef short s16; typedef unsigned char u8;
typedef struct { int px, py; } touchPosition;
enum { GL_QUADS = 1, GL_TRIANGLES = 2, GL_PROJECTION, GL_MODELVIEW, GL_RGBA, GL_TEXTURE_2D = 1, GL_ANTIALIAS = 2, GL_BLEND = 4,
       GL_TEXTURE_WRAP_S = 1 << 16, GL_TEXTURE_WRAP_T = 1 << 17, TEXGEN_TEXCOORD = 1 << 30 };
enum { KEY_A = 1, KEY_B = 2, KEY_X = 4, KEY_Y = 8, KEY_L = 16, KEY_R = 32, KEY_UP = 64, KEY_DOWN = 128, KEY_LEFT = 256, KEY_RIGHT = 512, KEY_START = 1024, KEY_SELECT = 2048, KEY_TOUCH = 4096 };
enum { MODE_0_3D, MODE_0_2D, VRAM_A_TEXTURE, VRAM_B_TEXTURE, VRAM_C_TEXTURE, VRAM_D_TEXTURE, VRAM_H_SUB_BG, BgType_Text4bpp, BgSize_T_256x256 };
#define RGB15(r, g, b) ((r) | ((g) << 5) | ((b) << 10))
#define POLY_ALPHA(n) ((n) << 16)
#define POLY_CULL_NONE (0xC0)
#define POLY_ID(n) ((n) << 24)
#define inttof32(n) ((n) << 12)
#define inttot16(n) ((n) << 4)
static u16 BG_PALETTE_SUB[256];
static int mock_frame = 0, mock_mode = 0, mock_nv = 0, mock_polys = 0, mock_maxpolys = 0, mock_overflow = 0, mock_binds = 0, mock_maxbinds = 0;
static long mock_totpolys = 0; static int mock_maxframes = 6000, mock_minpolys = 1 << 30;
static inline int glGenTextures(int n, int* names) { static int c = 1; *names = c++; return 1; }
static inline void glBindTexture(int a, int b) { mock_binds++; }
static inline int glTexImage2D(int a, int b, int c, int d, int e, int f, int g, const void* h) { return 1; }
static inline void glTexCoord2t16(int u, int v) { if (u < -32768 || u > 32767 || v < -32768 || v > 32767) mock_overflow++; }
static inline void glVertex3v16(int x, int y, int z) { if (x < -32768 || x > 32767 || y < -32768 || y > 32767 || z < -32768 || z > 32767) mock_overflow++; mock_nv++; }
static inline void glColor(unsigned c) {}
static inline void glPolyFmt(unsigned p) {}
static inline void glBegin(int m) { mock_mode = m; mock_nv = 0; }
static inline void glEnd(void) { mock_polys += mock_mode == GL_QUADS ? mock_nv / 4 : mock_nv / 3; mock_nv = 0; }
static inline void glMatrixMode(int m) {} static inline void glLoadIdentity(void) {}
static inline void gluPerspective(float a, float b, float c, float d) {}
static inline void glRotateYi(int a) {} static inline void glTranslate3f32(int x, int y, int z) {} static inline void glScalef32(int x, int y, int z) {}
static inline void glOrtho(float a, float b, float c, float d, float e, float f) {}
static inline void glClearColor(int r, int g, int b, int a) {} static inline void glClearPolyID(int a) {} static inline void glClearDepth(int a) {}
static inline void glViewport(int a, int b, int c, int d) {} static inline void glInit(void) {} static inline void glEnable(int a) {}
static inline void glFlush(int a) { if (mock_frame > 20) { if (mock_polys > mock_maxpolys) mock_maxpolys = mock_polys; if (mock_polys < mock_minpolys) mock_minpolys = mock_polys; mock_totpolys += mock_polys; } if (mock_binds > mock_maxbinds) mock_maxbinds = mock_binds; mock_polys = 0; mock_binds = 0; }
static inline int sinLerp(int a) { return (int)lround(sin((a & 0x7FFF) * 2.0 * M_PI / 32768.0) * 4096.0); }
static inline int cosLerp(int a) { return (int)lround(cos((a & 0x7FFF) * 2.0 * M_PI / 32768.0) * 4096.0); }
static inline void scanKeys(void) {}
static inline int keysHeld(void) {
    int k = 0, f = mock_frame;
    if (f > 5) k |= KEY_UP;
    if ((f / 90) % 4 == 1) k |= KEY_RIGHT;
    if ((f / 90) % 4 == 3) k |= KEY_LEFT;
    if ((f / 300) % 3 == 1) k |= KEY_B;
    if ((f / 130) % 5 == 2) k |= KEY_L;
    return k;
}
static inline int keysDown(void) {
    int f = mock_frame, k = 0;
    if (f == 5) k |= KEY_A;
    if (f > 5 && f % 37 == 0) k |= KEY_A;
    if (f > 5 && f % 53 == 0) k |= KEY_Y;
    if (f > 5 && f % 211 == 0) k |= KEY_X;
    if (f > 5 && f % 333 == 0) k |= KEY_SELECT;
    return k;
}
static inline void touchRead(touchPosition* t) { t->px = t->py = 0; }
static inline void videoSetMode(int m) {} static inline void videoSetModeSub(int m) {}
static inline void vramSetBankA(int m) {} static inline void vramSetBankB(int m) {} static inline void vramSetBankC(int m) {} static inline void vramSetBankD(int m) {} static inline void vramSetBankH(int m) {}
static inline void consoleInit(void* a, int b, int c, int d, int e, int f, int g, int h) {} static inline void consoleClear(void) {}
static inline void swiWaitForVBlank(void) {
    mock_frame++;
    if (mock_frame >= mock_maxframes) {
        printf("\nFRAMES=%d  polys/frame: min=%d max=%d avg=%ld  max_texbinds=%d  OVERFLOW_v16=%d\n", mock_frame, mock_minpolys, mock_maxpolys, mock_totpolys / (mock_frame - 20), mock_maxbinds, mock_overflow);
        exit(mock_overflow ? 2 : 0);
    }
}
static inline int mock_iprintf(const char* fmt, ...) { return 0; }
#define iprintf mock_iprintf
