# GAME (SDL2)
#
#   make            ./GAME, the pictures sprites_1.bmp (Linux; GAME.EXE in MSYS2 MINGW64)
#   make ROBBO=1    the same with the pictures sprites_2.bmp
#   make WIN=1      GAME.EXE made on Linux (with ROBBO=1 too); needs gcc-mingw-w64-x86-64 and
#                   SDL2-devel-2.32.10-mingw.tar.gz unpacked in ~ (elsewhere: SDL2_WIN=...)
#   make clean      removes what make makes
#
# GAME.EXE needs no DLLs (SDL2 linked in); the pictures and the caves Cxx.data go next to it.

CC     = gcc
SDL2   = sdl2-config
CFLAGS = -O2 -Wall -Wextra $(shell $(SDL2) --cflags)
LIBS   = $(shell $(SDL2) --libs) -lm
SRC    = main.c engine.c cave.c draw.c sound.c pokey.c
HDR    = game.h palette.h pokey.h font8x8_basic.h
SHEET  = sprites_1.bmp
ifeq ($(ROBBO),1)
SHEET  = sprites_2.bmp
endif
CFLAGS += -DSHEET=\"$(SHEET)\"
EXE    = GAME

SDL2_WIN = $(HOME)/SDL2-2.32.10/x86_64-w64-mingw32
ifeq ($(WIN),1)
CC     = x86_64-w64-mingw32-gcc
SDL2   = $(SDL2_WIN)/bin/sdl2-config
OS     = Windows_NT
endif

ifeq ($(OS),Windows_NT)
EXE    = GAME.EXE
LIBS   = -static $(shell $(SDL2) --static-libs) -s
endif

$(EXE): $(SRC) $(HDR)
	$(CC) $(CFLAGS) -o $@ $(SRC) $(LIBS)

clean:
	rm -f GAME GAME.EXE

.PHONY: $(EXE) clean
