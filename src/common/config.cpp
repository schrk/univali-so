#include "common/config.hpp"
#include <cstdio>
#include <cstdlib>

namespace cfg {

static uint32_t do_ambiente(const char* var, uint32_t padrao, uint32_t min, uint32_t max) {
    const char* v = getenv(var);
    if (!v || !*v) return padrao;
    long n = strtol(v, nullptr, 10);
    if (n < static_cast<long>(min) || n > static_cast<long>(max)) return padrao;
    return static_cast<uint32_t>(n);
}

uint32_t num_threads() { return do_ambiente("MINIDB_THREADS", 4, 1, 64); }
uint32_t num_slots()   { return do_ambiente("MINIDB_SLOTS", 64, 2, 4096); }
uint32_t custo_simulado_us() { return do_ambiente("MINIDB_CUSTO_US", 0, 0, 100000); }

void caminho_fifo(int32_t pid, char* saida, size_t tam) {
    snprintf(saida, tam, "%s%d", FIFO_PREFIXO, pid);
}

} // namespace cfg
