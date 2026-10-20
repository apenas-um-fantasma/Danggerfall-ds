#include "dungeon.h"
#include <string.h>

static unsigned rs;
static int rnd(int n) { rs = rs * 1664525u + 1013904223u; return (int)((rs >> 12) % (unsigned)n); }

void dungeon_generate(Dungeon* d, unsigned seed) {
    memset(d, 0, sizeof *d);
    rs = seed * 2654435761u + 12345u;
    int rx[8], rz[8], rw[8], rh[8], n = 0;
    for (int tries = 0; tries < 60 && n < 7; tries++) {
        int w = 3 + rnd(4), h = 3 + rnd(4);
        int x = 1 + rnd(DG_W - w - 2), z = 1 + rnd(DG_H - h - 2);
        int ok = 1;
        for (int i = 0; i < n; i++)
            if (x < rx[i] + rw[i] + 2 && x + w + 2 > rx[i] && z < rz[i] + rh[i] + 2 && z + h + 2 > rz[i]) { ok = 0; break; }
        if (!ok) continue;
        rx[n] = x; rz[n] = z; rw[n] = w; rh[n] = h; n++;
        for (int j = z; j < z + h; j++) for (int i = x; i < x + w; i++) d->cell[j][i] = 1;
    }
    if (n < 2) { // garantia: duas salas fixas
        n = 2; rx[0] = 3; rz[0] = 3; rw[0] = 4; rh[0] = 4; rx[1] = 20; rz[1] = 18; rw[1] = 5; rh[1] = 5;
        for (int k = 0; k < 2; k++) for (int j = rz[k]; j < rz[k] + rh[k]; j++) for (int i = rx[k]; i < rx[k] + rw[k]; i++) d->cell[j][i] = 1;
    }
    for (int k = 1; k < n; k++) {      // corredores em L
        int ax = rx[k - 1] + rw[k - 1] / 2, az = rz[k - 1] + rh[k - 1] / 2;
        int bx = rx[k] + rw[k] / 2,         bz = rz[k] + rh[k] / 2;
        int x = ax, z = az;
        while (x != bx) { d->cell[z][x] = 1; x += (bx > x) ? 1 : -1; }
        while (z != bz) { d->cell[z][x] = 1; z += (bz > z) ? 1 : -1; }
        d->cell[z][x] = 1;
    }
    d->sx = rx[0] + rw[0] / 2; d->sz = rz[0] + rh[0] / 2;
    for (int k = 1; k < n; k++) {
        int cnt = 1 + rnd(2);
        for (int e = 0; e < cnt && d->nen < DG_MAX_EN - 1; e++) {
            d->en[d->nen].cx = rx[k] + rnd(rw[k]); d->en[d->nen].cz = rz[k] + rnd(rh[k]); d->en[d->nen].boss = 0; d->nen++;
        }
        if ((k == n - 1 || rnd(100) < 35) && d->nch < DG_MAX_CH) {
            d->ch[d->nch].cx = rx[k] + rnd(rw[k]); d->ch[d->nch].cz = rz[k] + rnd(rh[k]); d->nch++;
        }
    }
    // chefe na ultima sala
    d->en[d->nen].cx = rx[n - 1] + rw[n - 1] / 2; d->en[d->nen].cz = rz[n - 1] + rh[n - 1] / 2; d->en[d->nen].boss = 1; d->nen++;
}
