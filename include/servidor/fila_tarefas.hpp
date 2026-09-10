#pragma once
#include <pthread.h>
#include <deque>
#include "common/protocolo.hpp"

namespace srv {

// Fila bloqueante entre a thread despachante (que consome o ring) e os workers.
// mutex + variável de condição: os workers dormem enquanto não há trabalho, em
// vez de girar em busy-wait consumindo CPU.
//
// Ela existe para que o consumo do IPC não fique preso atrás do lock do banco:
// a despachante devolve o slot ao ring imediatamente e volta a escutar.

struct Tarefa {
    Requisicao req;
    uint64_t   us_entrada; // instante em que entrou na fila (latência ponta a ponta)
};

class FilaTarefas {
public:
    FilaTarefas();
    ~FilaTarefas();

    void push(const Requisicao& r);
    bool pop(Tarefa& t);   // false somente quando a fila é encerrada e esvazia
    void encerrar();       // acorda todos os workers
    size_t tamanho() const;
    size_t pico() const { return pico_; }

private:
    mutable pthread_mutex_t mtx_;
    pthread_cond_t          cond_;
    std::deque<Tarefa>      fila_;
    bool                    encerrada_ = false;
    size_t                  pico_ = 0;
};

} // namespace srv
