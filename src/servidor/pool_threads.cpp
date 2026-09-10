#include "servidor/pool_threads.hpp"
#include "common/log.hpp"
#include "servidor/executor.hpp"
#include <ctime>
#include <stdexcept>

namespace srv {

static uint64_t agora_us() {
    timespec ts{};
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return static_cast<uint64_t>(ts.tv_sec) * 1000000ULL + ts.tv_nsec / 1000ULL;
}

void* PoolThreads::laco(void* arg) {
    Contexto* c = static_cast<Contexto*>(arg);
    logger::escrever("worker W%u no ar", c->id);

    Tarefa t;
    while (c->fila->pop(t)) {          // dorme na condvar enquanto não há trabalho
        uint64_t t0 = agora_us();

        // Aqui dentro está a seção crítica: o executor toma o lock de leitura
        // ou de escrita conforme a operação.
        Resposta resp = executar(*c->banco, t.req, c->id);

        uint64_t us_total = agora_us() - t.us_entrada;
        resp.us_total = us_total;

        char texto[128];
        descrever(t.req, texto, sizeof(texto));
        logger::escrever("W%u  %-40s -> %-14s  lock %4lu us  total %5lu us",
                      c->id, texto, nome_status(resp.status),
                      static_cast<unsigned long>(resp.us_no_lock),
                      static_cast<unsigned long>(us_total));

        c->fifo->enviar(t.req.pid_cliente, resp);
        c->met->registrar(t.req.op, resp.us_no_lock, agora_us() - t0, c->id);
    }

    logger::escrever("worker W%u encerrado", c->id);
    return nullptr;
}

void PoolThreads::iniciar(uint32_t n, Banco& banco, FilaTarefas& fila,
                          ipc::FifoEscritor& fifo, Metricas& met) {
    // ctx_ é dimensionado de uma vez: um realloc no meio invalidaria os
    // ponteiros já entregues às threads criadas.
    ctx_.resize(n);
    threads_.resize(n);

    for (uint32_t i = 0; i < n; ++i) {
        ctx_[i] = Contexto{i, &banco, &fila, &fifo, &met};
        if (pthread_create(&threads_[i], nullptr, &PoolThreads::laco, &ctx_[i]) != 0)
            throw std::runtime_error("pthread_create falhou");
    }
    logger::escrever("pool com %u threads criado (pthread_create)", n);
}

void PoolThreads::aguardar() {
    for (pthread_t t : threads_) pthread_join(t, nullptr);
    threads_.clear();
}

} // namespace srv
