#pragma once
#include <pthread.h>
#include <cstdint>
#include "common/protocolo.hpp"

namespace srv {

// Contadores agregados do servidor. Alimentam as tabelas e os gráficos do
// relatório: throughput por número de threads, latência média e — o número que
// mostra o efeito do lock — tempo médio de espera para entrar na seção crítica.

class Metricas {
public:
    void registrar(Op op, uint64_t us_no_lock, uint64_t us_total, uint32_t worker);
    void marcar_inicio();
    void marcar_fim();

    void   imprimir(uint32_t threads, uint32_t slots) const;
    bool   anexar_csv(const char* caminho, uint32_t threads, uint32_t slots) const;
    uint64_t total() const { return total_; }

private:
    mutable pthread_mutex_t mtx_ = PTHREAD_MUTEX_INITIALIZER;
    uint64_t por_op_[4]      = {0, 0, 0, 0};
    uint64_t por_worker_[64] = {0};
    uint64_t total_          = 0;
    uint64_t soma_lock_us_   = 0;
    uint64_t soma_total_us_  = 0;
    uint64_t pior_lock_us_   = 0;
    uint64_t t_inicio_us_    = 0;
    uint64_t t_fim_us_       = 0;
};

} // namespace srv
