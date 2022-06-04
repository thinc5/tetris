CC				:= gcc
CFLAGS 			:= -std=c11 -Wall -pedantic
LINKER  		:= gcc
LFLAGS			:= -lSDL2 -lSDL2_image -lSDL2_ttf 
XXD				:= xxd
FORMATTER		:= clang-format

RESDIR			:= res
INCDIR			:= .
OUTDIR			:= out

RM				:= rm -rf
MKDIR			:= mkdir -p

SOURCES  		:= tetris.c
HEADERS			:= 
RESOURCES		:= $(INCDIR)/tiles.h $(INCDIR)/font.h

FORMAT_TARGETS	:= $(SOURCES) $(HEADERS)

TITLE			:= tetris

TARGET			:= $(TITLE)

ifeq ($(DEBUG), 1)
    CFLAGS += -g
	LFLAGS += -g
endif

ifeq ($(MUSIC), 1)
	RESOURCES += $(INCDIR)/clear.h $(INCDIR)/fall.h $(INCDIR)/level.h
	RESOURCES += $(INCDIR)/line.h $(INCDIR)/over.h $(INCDIR)/theme.h
    CFLAGS += -DMUSIC
	LFLAGS += -lSDL2_mixer
endif

ifeq ($(PLATFORM), win32)
	TARGET = $(TITLE).exe
endif
ifeq ($(PLATFORM), wasm)
	CC = emcc
	LFLAGS = -s WASM=1 -s USE_SDL=2 -s USE_SDL_TTF=2
	LFLAGS += -s USE_SDL_IMAGE=2 -s SDL2_IMAGE_FORMATS='["png"]'
	ifeq ($(MUSIC), 1)
		LFLAGS += -s USE_SDL_MIXER=2 -s SDL2_MIXER_FORMATS='["mp3", "wav"]'
	endif
	LFLAGS += --shell-file template.html
	CFLAGS += -DWASM
	TARGET = $(TITLE).html
endif

default: build

$(OUTDIR):
	@$(MKDIR) $(OUTDIR)

$(INCDIR)/%.h : $(RESDIR)/%.*
	$(shell $(XXD) -i $< > $@)

$(OUTDIR)/$(TARGET): $(SOURCES) $(HEADERS) $(RESOURCES) $(OUTDIR) 
	$(CC) $(CFLAGS) $(LFLAGS) $(SOURCES) -o $@

build: $(OUTDIR)/$(TARGET)

.PHONY:	clean format

$(FORMAT_TARGETS):
	$(info Formatting: $@)
	$(shell clang-format $@ > $@.temp)
	$(shell mv $@.temp $@)

format: $(FORMAT_TARGETS)

clean:
	@$(RM) $(RESOURCES)
	@$(RM) $(OUTDIR)
