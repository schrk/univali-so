#include "servidor/rw_lock.hpp"
#include <ctime>

namespace srv {

static uint64_t agora_us() {
    timespec ts{};
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return static_cast<uint64_t>(ts.tv_sec) * 1000000ULL + ts.tv_nsec / 1000ULL;
}

RwLock::RwLock() {
    sem_init(&recurso_, 0, 1);   // 0 = compartilhado entre threads do processo
    sem_init(&fila_,    0, 1);
    pthread_mutex_init(&mtx_leitores_, nullptr);
}

RwLock::~RwLock() {
    sem_destroy(&recurso_);
    sem_destroy(&fila_);
    pthread_mutex_destroy(&mtx_leitores_);
}

uint64_t RwLock::lock_leitura() {
    uint64_t t0 = agora_us();

    // A catraca (fila_) é atravessada rapidamente e serve só para preservar a
    // ordem de chegada: um leitor que chegar depois de um escritor em espera
    // fica atrás dele, e o escritor não morre de fome.
    sem_wait(&fila_);
    pthread_mutex_lock(&mtx_leitores_);
    if (++leitores_ == 1) sem_wait(&recurso_);  // primeiro leitor fecha a porta para escritores
    pthread_mutex_unlock(&mtx_leitores_);
    sem_post(&fila_);

    return agora_us() - t0;
}

void RwLock::unlock_leitura() {
    pthread_mutex_lock(&mtx_leitores_);
    if (--leitores_ == 0) sem_post(&recurso_);  // último leitor reabre a porta
    pthread_mutex_unlock(&mtx_leitores_);
}

uint64_t RwLock::lock_escrita() {
    uint64_t t0 = agora_us();

    sem_wait(&fila_);
    sem_wait(&recurso_);   // espera a tabela ficar sem nenhum leitor
    sem_post(&fila_);

    return agora_us() - t0;
}

void RwLock::unlock_escrita() { sem_post(&recurso_); }

} // namespace srv
