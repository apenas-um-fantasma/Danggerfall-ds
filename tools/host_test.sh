#!/bin/sh
# Roda a logica do jogo no PC com uma "libnds falsa": simula teclas, entra na dungeon, conta poligonos
# e detecta estouro de coordenadas/arrays (AddressSanitizer + UBSan).
cd "$(dirname "$0")/.." || exit 1
mkdir -p /tmp/ndsmock && cp tools/mock_nds.h /tmp/ndsmock/nds.h
gcc -g -O1 -fsanitize=address,undefined -fno-sanitize-recover=undefined -fno-sanitize=shift -DHOST_TEST -Wall -Isource -I/tmp/ndsmock \
    source/main.c source/terrain.c source/dungeon.c source/gfx_data.c -lm -o /tmp/ndsmock/game_host && /tmp/ndsmock/game_host
