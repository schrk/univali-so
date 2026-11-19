# Mini File System (M3) - make | make demo | make memcheck | make clean
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

# Roda o roteiro da apresentação
demo: mfs
	./mfs -t < demo.txt

# Roda o roteiro da apresentação no valgrind: falha se houver erro ou leak
memcheck: mfs
	valgrind -q --leak-check=full --show-leak-kinds=all --errors-for-leak-kinds=all \
	    --error-exitcode=99 ./mfs -t < demo.txt > /dev/null && echo "valgrind: 0 erros, 0 leaks"

clean:
	rm -rf build mfs

.PHONY: demo memcheck clean
