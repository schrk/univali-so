# Mini File System (M3) - make | make teste | make memcheck | make clean
CC      ?= gcc
CFLAGS  ?= -std=c11 -Wall -Wextra -Wpedantic -O0 -g
CFLAGS  += -D_POSIX_C_SOURCE=200809L
SRC     := $(wildcard src/*.c)
OBJ     := $(SRC:src/%.c=build/%.o)
HDR     := $(wildcard src/*.h)

mfs: $(OBJ)
	$(CC) $(CFLAGS) -o $@ $(OBJ)

build/%.o: src/%.c $(HDR) | build
	$(CC) $(CFLAGS) -c $< -o $@

build:
	mkdir -p build

teste: mfs
	./testes/rodar.sh

memcheck: mfs
	./testes/rodar.sh --valgrind

demo: mfs
	./mfs -t < testes/demo.txt

clean:
	rm -rf build mfs

.PHONY: teste memcheck demo clean
