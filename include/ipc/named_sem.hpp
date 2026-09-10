#pragma once
#include <semaphore.h>

namespace ipc {

// RAII sobre um semáforo nomeado POSIX (sem_t). Nomeado, e não anônimo em
// memória compartilhada, porque ele precisa ser aberto por dois processos que
// não têm parentesco entre si.

class NamedSem {
public:
    NamedSem() = default;
    ~NamedSem();
    NamedSem(const NamedSem&) = delete;
    NamedSem& operator=(const NamedSem&) = delete;

    void criar(const char* nome, unsigned valor_inicial); // servidor (é o dono)
    void abrir(const char* nome);                         // cliente

    // Retorna false quando o bloqueio foi interrompido por sinal (EINTR):
    // é assim que a thread despachante descobre que deve encerrar.
    bool wait();
    void post();

    int  valor() const;
    bool valido() const { return sem_ != SEM_FAILED && sem_ != nullptr; }

private:
    sem_t* sem_ = nullptr;
    bool   dono_ = false;
    char   nome_[64] = {0};
};

} // namespace ipc
