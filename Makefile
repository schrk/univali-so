# minidb-ipc — dois binários distintos, como o enunciado exige.
#   bin/servidor  processo gerenciador do banco (pool de threads)
#   bin/cliente   processo que envia requisições por IPC

CXX      := g++
CXXFLAGS := -std=c++17 -Wall -Wextra -O2 -pthread -Iinclude
LDFLAGS  := -pthread -lrt          # -lrt: shm_open/sem_open; -pthread: threads e mutex

SRC_COMMON := $(wildcard src/common/*.cpp)
SRC_IPC    := $(wildcard src/ipc/*.cpp)
SRC_SRV    := $(filter-out src/servidor/main.cpp, $(wildcard src/servidor/*.cpp))
SRC_CLI    := $(filter-out src/cliente/main.cpp,  $(wildcard src/cliente/*.cpp))

OBJ_BASE := $(patsubst src/%.cpp, obj/%.o, $(SRC_COMMON) $(SRC_IPC))
OBJ_SRV  := $(patsubst src/%.cpp, obj/%.o, $(SRC_SRV))
OBJ_CLI  := $(patsubst src/%.cpp, obj/%.o, $(SRC_CLI))

TESTES := bin/test_ipc bin/test_rw_lock bin/test_corrida

.PHONY: all testes demo benchmark limpar limpar-ipc ajuda

all: bin/servidor bin/cliente

bin/servidor: $(OBJ_BASE) $(OBJ_SRV) obj/servidor/main.o
	@mkdir -p bin
	$(CXX) $^ -o $@ $(LDFLAGS)

bin/cliente: $(OBJ_BASE) $(OBJ_CLI) obj/cliente/main.o
	@mkdir -p bin
	$(CXX) $^ -o $@ $(LDFLAGS)

testes: $(TESTES)

bin/test_ipc: tests/test_ipc.cpp $(OBJ_BASE)
	@mkdir -p bin
	$(CXX) $(CXXFLAGS) $^ -o $@ $(LDFLAGS)

bin/test_rw_lock: tests/test_rw_lock.cpp $(OBJ_BASE) $(OBJ_SRV)
	@mkdir -p bin
	$(CXX) $(CXXFLAGS) $^ -o $@ $(LDFLAGS)

bin/test_corrida: tests/test_corrida.cpp $(OBJ_BASE) $(OBJ_SRV)
	@mkdir -p bin
	$(CXX) $(CXXFLAGS) $^ -o $@ $(LDFLAGS)

obj/%.o: src/%.cpp
	@mkdir -p $(dir $@)
	$(CXX) $(CXXFLAGS) -MMD -MP -c $< -o $@

-include $(shell find obj -name '*.d' 2>/dev/null)

demo: all
	@./scripts/demo.sh

benchmark: all
	@./scripts/benchmark.sh

# Remove objetos de kernel deixados para trás por uma execução morta à força.
limpar-ipc:
	-@rm -f /dev/shm/minidb_requests /dev/shm/sem.minidb_* /tmp/minidb_resp.* 2>/dev/null || true
	@echo "objetos IPC removidos"

limpar:
	rm -rf obj bin
	@echo "build limpo"

ajuda:
	@echo "make            compila bin/servidor e bin/cliente"
	@echo "make testes     compila os três testes"
	@echo "make demo       sobe o servidor, roda um cliente e mostra o log"
	@echo "make benchmark  varia o nº de threads e gera resultados/metricas.csv"
	@echo "make limpar-ipc remove shm/semáforos/FIFOs órfãos"
