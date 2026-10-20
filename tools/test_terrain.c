#include <stdio.h>
#include <stdlib.h>
#include "../source/terrain.h"
// gera um PPM do mapa (altura + features) e estatisticas. Uso: ./test_terrain [seed]
int main(int argc, char** argv) {
    unsigned seed = argc > 1 ? atoi(argv[1]) : 1337;
    world_init(seed);
    int W = 512, H = 512, ox = -256, oz = -256;
    FILE* f = fopen("map.ppm", "wb"); fprintf(f, "P6\n%d %d\n255\n", W, H);
    long water = 0, land = 0; int hmin = 1 << 30, hmax = -(1 << 30); long steep = 0;
    for (int z = 0; z < H; z++) for (int x = 0; x < W; x++) {
        int tx = x + ox, tz = z + oz;
        int h = world_height(tx, tz);
        if (h < hmin) hmin = h;
        if (h > hmax) hmax = h;
        int s = abs(world_height(tx + 1, tz) - h) + abs(world_height(tx, tz + 1) - h);
        unsigned char c[3];
        if (h < 0) { c[0] = 30; c[1] = 70; c[2] = 160; water++; }
        else {
            land++; if (s > U(1.15)) steep++;
            int v = 60 + (h >> 7); if (v > 255) v = 255;
            if (h > U(14)) { c[0] = c[1] = c[2] = 240; }
            else if (h > U(7)) { c[0] = v; c[1] = v; c[2] = v; }
            else { c[0] = 40; c[1] = v + 60 > 255 ? 255 : v + 60; c[2] = 40; }
            TreeInfo t; if (tree_at(tx, tz, &t)) { c[0] = 10; c[1] = 70; c[2] = 20; }
        }
        int rx = floordiv(tx, REGION_SIZE), rz = floordiv(tz, REGION_SIZE);
        Feature fe = world_feature(rx, rz);
        if (fe.type && abs(tx - fe.cx) <= 1 && abs(tz - fe.cz) <= 1) {
            if (fe.type == 1) { c[0] = 255; c[1] = 60; c[2] = 60; } else { c[0] = 255; c[1] = 220; c[2] = 0; }
        }
        fwrite(c, 1, 3, f);
    }
    fclose(f);
    printf("agua %.1f%%  terra %.1f%%  (steep %.1f%% da terra)\n", 100.0 * water / (water + land), 100.0 * land / (water + land), 100.0 * steep / land);
    printf("h min %.2f max %.2f\n", hmin / 4096.0, hmax / 4096.0);
    int sx, sz; world_find_start(&sx, &sz);
    printf("start %.1f %.1f  h=%.2f\n", sx / 4096.0, sz / 4096.0, world_height_at(sx, sz) / 4096.0);
    int nv = 0, nd = 0;
    for (int rz = -5; rz <= 5; rz++) for (int rx = -5; rx <= 5; rx++) { Feature fe = world_feature(rx, rz); nv += fe.type == 1; nd += fe.type == 2; }
    printf("em 11x11 regioes: %d vilas, %d dungeons\n", nv, nd);
    return 0;
}
