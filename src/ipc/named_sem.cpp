#include "ipc/named_sem.hpp"
#include <fcntl.h>
#include <cerrno>
#include <cstdio>
#include <cstring>
#include <stdexcept>
#include <string>

namespace ipc {

NamedSem::~NamedSem() {
    if (valido()) sem_close(sem_);
    if (dono_ && nome_[0]) sem_unlink(nome_);
}

void NamedSem::criar(const char* nome, unsigned valor_inicial) {
    sem_unlink(nome); // limpa sobra de execução anterior
    sem_ = sem_open(nome, O_CREAT | O_EXCL, 0600, valor_inicial);
    if (sem_ == SEM_FAILED)
        throw std::runtime_error(std::string("sem_open(criar) ") + nome + ": " + strerror(errno));
    dono_ = true;
    snprintf(nome_, sizeof(nome_), "%s", nome);
}

void NamedSem::abrir(const char* nome) {
    sem_ = sem_open(nome, 0);
    if (sem_ == SEM_FAILED)
        throw std::runtime_error(std::string("sem_open(abrir) ") + nome + ": " + strerror(errno));
    dono_ = false;
    snprintf(nome_, sizeof(nome_), "%s", nome);
}

bool NamedSem::wait() {
    while (sem_wait(sem_) != 0) {
        // EINTR: chegou um sinal (SIGTERM/SIGINT). Quem chamou decide se é
        // encerramento ou se deve continuar esperando.
        if (errno == EINTR) return false;
        throw std::runtime_error(std::string("sem_wait: ") + strerror(errno));
    }
    return true;
}

void NamedSem::post() {
    if (sem_post(sem_) != 0)
        throw std::runtime_error(std::string("sem_post: ") + strerror(errno));
}

int NamedSem::valor() const {
    int v = -1;
    sem_getvalue(sem_, &v);
    return v;
}

} // namespace ipc
