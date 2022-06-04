# Tetris

Quick imperative implementation of Tetris in C using SDL2.

## Dependencies

SDL2, SDL_Image (2), SDL_TTF (2) are all required, but SDL_Mixer (2) is only required if you want sound. Consult your operating system's package manager or google in order to find out where you can download the development libraries for these dependencies.

If you wish to compile to Webassembly, emscripten is required.

The provided Makefile makes reference to some resources that are not present, and as such you will need to source on your own. You will need to provide an `.otf` font in `res/` and name it `font.otf`. This is the only required file that is not included in this repository.

If you want music and sound effects, you must provide the files referenced in the Makefile and use the flag described below.

## Compilation

This project uses a Makefile.

An example build command targeting `wasm` and enabling music: `make MUSIC=1 PLATFORM=wasm`

This does a few things, it will take your provided font, tile image and other resources and convert them into header files, which are then included in the emitted executable.

To compile with debug info, use `make DEBUG=1`, to compile with music provided you have supplied the expected files in `res/` use `make MUSIC=1`

To compile for other targets such as `wasm` or `win32` use `make PLATFORM=wasm` etc.

