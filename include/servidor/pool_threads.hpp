#pragma once
#include <pthread.h>
#include <vector>
#include "ipc/fifo_channel.hpp"
#include "servidor/banco.hpp"
#include "servidor/fila_tarefas.hpp"
#include "servidor/metricas.hpp"

namespace srv {

// Pool de N threads criadas com pthread_create. Todas rodam o mesmo laço:
// retirar tarefa da fila, executar sobre o banco compartilhado e devolver a
// resposta pelo FIFO do cliente. É onde o paralelismo do servidor acontece.

class PoolThreads {
public:
    void iniciar(uint32_t n, Banco& banco, FilaTarefas& fila,
                 ipc::FifoEscritor& fifo, Metricas& met);
    void aguardar();   // pthread_join em todas

    uint32_t tamanho() const { return static_cast<uint32_t>(threads_.size()); }

private:
    struct Contexto {
        uint32_t           id;
        Banco*             banco;
        FilaTarefas*       fila;
        ipc::FifoEscritor* fifo;
        Metricas*          met;
    };
    static void* laco(void* arg);

    std::vector<pthread_t> threads_;
    std::vector<Contexto>  ctx_;
};

} // namespace srv
