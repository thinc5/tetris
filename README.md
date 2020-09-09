# Tetris

Quick imperative implementation of Tetris in C using SDL2.

Play a recent version in the browser here: [tetris](https://tetris.thinc5.xyz/)

## Compilation

First get a font you like such as the included ssp-regular in the otf/ttf format.

Run the following commands after ensuring you have access to the `xxd` tool or an analogue.

`xxd -i tiles.png > tiles.h`

`xdd -i ssp-regular.otf > font.h`

Ensuring that you have gcc and the SDL development tools installed, run the following command to compile the tetris executable.

`gcc -O3 -Wall --std=c11 -lSDL2 -lSDL2_ttf -lSDL2_image tetris.c font.h tiles.h -o tetris`

Don't forget to add .exe to the output if you're on windows!

`gcc -O3 -Wall --std=c11 -lSDL2 -lSDL2_ttf -lSDL2_image -lSDL2_mixer tetris.c font.h tiles.h -DMUSIC theme.h over.h fall.h clear.h level.h -o tetris`

## WASM

`emcc -s WASM=1 -s USE_SDL=2 -s USE_SDL_IMAGE=2 -s USE_SDL_TTF=2 -s SDL2_IMAGE_FORMATS='["png"]' -s USE_SDL_MIXER=2 -s SDL2_MIXER_FORMATS='["mp3"]' --shell-file template.html --std=c11 -DMUSIC tetris.c -o tetris.html`
