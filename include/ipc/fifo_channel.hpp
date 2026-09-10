#pragma once
#include <pthread.h>
#include <cstdint>
#include <map>
#include "common/protocolo.hpp"

namespace ipc {

// Segundo canal IPC: FIFO nomeado (mkfifo), usado só no sentido servidor →
// cliente. O cliente dorme em read() — não há polling em lugar nenhum.

// Lado do cliente: cria o próprio FIFO e lê dele.
class FifoLeitor {
public:
    ~FifoLeitor();
    void criar(int32_t pid);
    bool ler(Resposta& r);   // false em EOF ou erro
    void fechar();

private:
    int  fd_ = -1;
    char caminho_[128] = {0};
};

// Lado do servidor: escreve na FIFO do cliente que fez a requisição, mantendo
// os descritores abertos em cache. sizeof(Resposta) < PIPE_BUF, então cada
// write() é atômico; o mutex garante além disso que dois workers não disputem
// o mesmo descritor.
class FifoEscritor {
public:
    ~FifoEscritor();
    bool enviar(int32_t pid_cliente, const Resposta& r);
    void fechar_tudo();

private:
    int abrir_para(int32_t pid); // exige o mutex tomado

    pthread_mutex_t mtx_ = PTHREAD_MUTEX_INITIALIZER;
    std::map<int32_t, int> fds_;
};

} // namespace ipc
