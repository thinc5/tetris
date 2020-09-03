# Tetris

Quick imperative implementation of Tetris in C using SDL2.

## Compilation

First get a font you like such as the included ssp-regular in the otf/ttf format.

Run the following commands after ensuring you have access to the `xxd` tool or an analogue.

`xxd -i tiles.png > tiles.h`

`xdd -i ssp-regular.otf > font.h`

Ensuring that you have gcc and the SDL development tools installed, run the following command to compile the tetris executable.

`gcc -O3 -Wall --std=c11 -lSDL2 -lSDL2_ttf -lSDL2_image tetris.c font.h tiles.h -o tetris`

Don't forget to add .exe to the output if you're on windows!

`gcc -O3 -Wall --std=c11 -lSDL2 -lSDL2_ttf -lSDL2_image -lSDL2_mixer tetris.c font.h tiles.h -DMUSIC theme.h over.h fall.h clear.h level.h -o tetris`
