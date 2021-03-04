CCOMP = gcc
SDL_LIB = -I/usr/include/SDL2 -D_REENTRANT -pthread -lSDL2
CCOMPFLAGS = -Wall -c
LDFLAGS = $(SDL_LIB)
EXE = chip8vm

all: $(EXE)

$(EXE): main.o
	$(CCOMP) $< $(LDFLAGS) -o $@

main.o: main.c
	$(CCOMP) $(CCOMPFLAGS) $< -o $@

clean:
	rm -rf *.o && rm -rf $(EXE)
