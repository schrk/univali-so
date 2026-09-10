#pragma once
#include "common/protocolo.hpp"
#include "ipc/named_sem.hpp"
#include "ipc/shm_region.hpp"

namespace ipc {

// Ring buffer de requisições em memória compartilhada — o produtor-consumidor
// clássico, com os três semáforos nomeados fazendo a sincronização:
//
//   sem_vazias  (inicia em N)  quantos slots livres existem
//   sem_cheias  (inicia em 0)  quantas requisições esperam para ser lidas
//   sem_mutex   (inicia em 1)  exclusão mútua sobre head/tail
//
// Nenhum lado faz busy-wait: o produtor dorme quando o ring enche e o
// consumidor dorme quando ele esvazia.

struct CabecalhoRing {
    uint32_t magic;
    uint32_t capacidade;
    uint32_t head;         // próxima posição a escrever
    uint32_t tail;         // próxima posição a ler
    uint64_t total_push;
    uint64_t total_pop;
};

class ShmRing {
public:
    void criar(uint32_t capacidade); // servidor
    void abrir();                    // cliente

    void push(const Requisicao& r);  // bloqueia enquanto o ring estiver cheio
    bool pop(Requisicao& r);         // bloqueia; false se interrompido por sinal

    uint32_t capacidade() const { return cab_->capacidade; }
    uint32_t ocupados()   const;
    void     acordar_consumidor();   // destrava um pop() pendente no encerramento

private:
    Requisicao* slots();

    ShmRegion      regiao_;
    CabecalhoRing* cab_ = nullptr;
    NamedSem       vazias_, cheias_, mutex_;
};

} // namespace ipc
