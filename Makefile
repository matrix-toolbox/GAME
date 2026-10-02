# GAME2 - GAME with its own pictures (sprites.bmp) and sounds (SDL2)
#
#   make        ./GAME  (needs a C compiler and the SDL2 development files)
#   make clean  removes ./GAME

CC     = gcc
CFLAGS = -O2 -Wall -Wextra $(shell sdl2-config --cflags)
LIBS   = $(shell sdl2-config --libs) -lm
SRC    = main.c engine.c cave.c draw.c sound.c pokey.c
HDR    = game.h palette.h pokey.h font8x8_basic.h

GAME: $(SRC) $(HDR)
	$(CC) $(CFLAGS) -o $@ $(SRC) $(LIBS)

clean:
	rm -f GAME

.PHONY: clean
