#include "servidor/fila_tarefas.hpp"
#include <ctime>

namespace srv {

static uint64_t agora_us() {
    timespec ts{};
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return static_cast<uint64_t>(ts.tv_sec) * 1000000ULL + ts.tv_nsec / 1000ULL;
}

FilaTarefas::FilaTarefas() {
    pthread_mutex_init(&mtx_, nullptr);
    pthread_cond_init(&cond_, nullptr);
}

FilaTarefas::~FilaTarefas() {
    pthread_mutex_destroy(&mtx_);
    pthread_cond_destroy(&cond_);
}

void FilaTarefas::push(const Requisicao& r) {
    pthread_mutex_lock(&mtx_);
    fila_.push_back(Tarefa{r, agora_us()});
    if (fila_.size() > pico_) pico_ = fila_.size();
    pthread_mutex_unlock(&mtx_);

    // Uma tarefa, um worker acordado.
    pthread_cond_signal(&cond_);
}

bool FilaTarefas::pop(Tarefa& t) {
    pthread_mutex_lock(&mtx_);
    // while, e não if: a espera em condvar pode acordar sem motivo (spurious
    // wakeup) e outro worker pode ter levado a tarefa antes.
    while (fila_.empty() && !encerrada_)
        pthread_cond_wait(&cond_, &mtx_);

    if (fila_.empty()) {           // encerrada e vazia: o worker termina
        pthread_mutex_unlock(&mtx_);
        return false;
    }
    t = fila_.front();
    fila_.pop_front();
    pthread_mutex_unlock(&mtx_);
    return true;
}

void FilaTarefas::encerrar() {
    pthread_mutex_lock(&mtx_);
    encerrada_ = true;
    pthread_mutex_unlock(&mtx_);
    pthread_cond_broadcast(&cond_);  // acorda todos os workers dormindo
}

size_t FilaTarefas::tamanho() const {
    pthread_mutex_lock(&mtx_);
    size_t n = fila_.size();
    pthread_mutex_unlock(&mtx_);
    return n;
}

} // namespace srv
