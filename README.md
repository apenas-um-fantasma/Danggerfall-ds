# Daggerfall DS (teste técnico) — homebrew 3D para Nintendo DS

Mini-versão inspirada em *The Elder Scrolls II: Daggerfall*, escrita em C com **libnds** (devkitARM).
Foco em emuladores (Drastic, melonDS, DeSmuME); não foi otimizada para hardware real.

## Status honesto
- **Escrito e testado só a nível de lógica** (no PC, com uma "libnds falsa" + AddressSanitizer: 6000 frames
  simulando andar, atacar, subir de nível, entrar/sair de dungeon, ciclo dia/noite — sem crash e sem estouro de coordenadas).
- **A **ROM `.nds` precisa ser compilada por você** (instruções abaixo; o jeito mais fácil é o GitHub Actions).
- Pode haver pequenos erros de compilação/API na 1ª tentativa (a libnds mudou na versão 2.x). Se der erro, me mande a mensagem.

## O que tem
- **Mundo procedural infinito** com Ruído Perlin (ponto fixo): colinas, lagos, montanhas com neve, só ao redor do jogador (raio de 13 tiles, com dissolução suave no horizonte).
- **Terreno bonito**: texturas por altura/inclinação (grama, grama seca, areia, rocha, neve, terra, pedra de vila), iluminação por vértice (sol), água translúcida animada, ciclo **dia/noite**.
- **Árvores** (pinheiro, carvalho, árvore morta) como **sprites 2D**.
- **Vilas** (5–6 casas, plaza de pedra, 2 aldeões que conversam) e **dungeons pequenas** (salas+corredores, esqueletos, chefe, baús).
  Casas e entradas de dungeon são **sprites 2D de longe e viram modelos 3D quando você chega perto**.
- **Combate em 1ª pessoa**: espada ou punhos (X troca), inimigos (goblins/esqueletos) como sprites animados, XP/nível, ouro, poções.
- **Tela de baixo**: menu (D-pad ou toque), HUD, bússola, **minimapa**, mensagens/diálogos.

## Controles
| Tecla | Ação |
|---|---|
| Cima/Baixo | andar / recuar |
| Esquerda/Direita | girar |
| L / R | andar de lado |
| B (segurar) | correr |
| A | atacar |
| X | trocar espada ⇄ punhos |
| Y | falar / abrir baú / entrar e sair da dungeon |
| SELECT | beber poção |
| START | pausa (SELECT na pausa volta ao título) |

## Como gerar a ROM
### A) Pelo celular/navegador (GitHub Actions) — sem instalar nada
1. Crie um repositório no GitHub e suba **todo o conteúdo desta pasta** (inclusive `.github/workflows/build.yml`).
2. Aba **Actions → "Compilar ROM (NDS)" → Run workflow** (ou o push já dispara).
3. Quando terminar, abra a execução e baixe o artefato **daggerfall-ds-rom** (contém o `.nds`).
4. Abra o `.nds` no Drastic DS.

### B) No PC
1. Instale o devkitPro (https://devkitpro.org) e o grupo `nds-dev` (`dkp-pacman -S nds-dev`).
2. `export DEVKITPRO=/opt/devkitpro` (no Windows use o "MSYS2" do devkitPro) e rode `make`.
3. Sai `daggerfall_ds.nds`. Se o Makefile falhar na sua versão da libnds, copie o `Makefile` de
   `$DEVKITPRO/examples/nds/templates/arm9/` para esta pasta e rode `make` de novo (ele já compila `source/*.c`).

### C) Docker
`docker run --rm -v "$PWD":/work -w /work devkitpro/devkitarm sh -c "dkp-pacman -Sy --noconfirm nds-dev && make"`

## Estrutura
- `source/main.c` — loop, renderização 3D, combate, menus, HUD.
- `source/terrain.c/.h` — Perlin, altura do mundo (com cache), vilas, árvores. **C puro**, testável no PC.
- `source/dungeon.c/.h` — gerador de dungeons.
- `source/gfx_data.c/.h` — **gerado** por `tools/gen_assets.py` (texturas A1RGB5 prontas para `glTexImage2D`).
- `gfx/*.png` — todas as texturas/sprites (editáveis). `gfx/_preview.png` mostra tudo numa folha.
  Para trocar uma arte: edite o PNG e rode `python3 tools/gen_assets.py --from-png`.
- `tools/host_test.sh` — simulação no PC; `tools/test_terrain.c` — gera `map.ppm` do mundo.

## Ajustes rápidos (topo de `main.c`)
- `VIEW_ROT_SIGN`: se a câmera girar ao contrário, troque `1` por `-1`.
- `R_VIEW` (máx. 14), `HOUSE_3D_DIST`/`DG_3D_DIST` (distância em que sprite vira 3D), velocidades, `DAY_LEN`, `SEED`.
- Os sprites foram **desenhados por código** (sem arquivos externos), então não há licença a respeitar.

## Limitações conhecidas
Sem save, sem som, sem inventário de verdade, câmera sem olhar para cima/baixo, minimapa em texto.
