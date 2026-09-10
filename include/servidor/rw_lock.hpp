#pragma once
#include <pthread.h>
#include <semaphore.h>
#include <cstdint>

namespace srv {

// Lock de leitores-escritores montado com pthread_mutex_t + sem_t, exatamente
// os dois mecanismos que o enunciado cita.
//
//   sem_recurso  exclusão mútua sobre a tabela; o primeiro leitor toma e o
//                último devolve, então N leituras correm simultâneas
//   sem_fila     "catraca" na entrada: quem chega primeiro entra primeiro, o
//                que impede uma fila contínua de leitores de matar o escritor
//                de fome (starvation)
//   mtx_leitores protege o contador de leitores ativos
//
// Um mutex único também seria correto, mas serializaria os SELECT — e é
// justamente essa diferença que o trabalho mede.

class RwLock {
public:
    RwLock();
    ~RwLock();
    RwLock(const RwLock&) = delete;
    RwLock& operator=(const RwLock&) = delete;

    // Retornam quantos microssegundos a thread ficou esperando para entrar.
    uint64_t lock_leitura();
    uint64_t lock_escrita();
    void     unlock_leitura();
    void     unlock_escrita();

    int leitores_ativos() const { return leitores_; }

private:
    sem_t           recurso_;
    sem_t           fila_;
    pthread_mutex_t mtx_leitores_;
    int             leitores_ = 0;
};

// RAII para não esquecer nenhum unlock em caminho de erro.
class GuardaLeitura {
public:
    explicit GuardaLeitura(RwLock& l) : l_(l), espera_us_(l.lock_leitura()) {}
    ~GuardaLeitura() { l_.unlock_leitura(); }
    uint64_t espera_us() const { return espera_us_; }
private:
    RwLock&  l_;
    uint64_t espera_us_;
};

class GuardaEscrita {
public:
    explicit GuardaEscrita(RwLock& l) : l_(l), espera_us_(l.lock_escrita()) {}
    ~GuardaEscrita() { l_.unlock_escrita(); }
    uint64_t espera_us() const { return espera_us_; }
private:
    RwLock&  l_;
    uint64_t espera_us_;
};

} // namespace srv
