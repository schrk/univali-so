#include "servidor/metricas.hpp"
#include "servidor/executor.hpp"
#include <sys/stat.h>
#include <cstdio>
#include <ctime>

namespace srv {

static uint64_t agora_us() {
    timespec ts{};
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return static_cast<uint64_t>(ts.tv_sec) * 1000000ULL + ts.tv_nsec / 1000ULL;
}

// A janela de medição vai da PRIMEIRA à ÚLTIMA requisição atendida. Marcar o
// início na subida do servidor contaria o tempo ocioso esperando o cliente
// conectar, o que achata o throughput de qualquer execução curta.
void Metricas::marcar_inicio() {
    pthread_mutex_lock(&mtx_);
    t_inicio_us_ = 0;
    pthread_mutex_unlock(&mtx_);
}

void Metricas::marcar_fim() {
    pthread_mutex_lock(&mtx_);
    if (t_fim_us_ == 0) t_fim_us_ = agora_us();
    pthread_mutex_unlock(&mtx_);
}

void Metricas::registrar(Op op, uint64_t us_no_lock, uint64_t us_total, uint32_t worker) {
    uint64_t t = agora_us();
    pthread_mutex_lock(&mtx_);
    if (t_inicio_us_ == 0) t_inicio_us_ = t;  // primeira requisição atendida
    t_fim_us_ = t;                            // a última sempre vence
    por_op_[static_cast<int>(op)]++;
    if (worker < 64) por_worker_[worker]++;
    total_++;
    soma_lock_us_  += us_no_lock;
    soma_total_us_ += us_total;
    if (us_no_lock > pior_lock_us_) pior_lock_us_ = us_no_lock;
    pthread_mutex_unlock(&mtx_);
}

void Metricas::imprimir(uint32_t threads, uint32_t slots) const {
    pthread_mutex_lock(&mtx_);
    double dur_s = (t_fim_us_ > t_inicio_us_) ? (t_fim_us_ - t_inicio_us_) / 1e6 : 0.0;
    printf("\n─── métricas ───────────────────────────────────────────\n");
    printf("threads do pool .......... %u\n", threads);
    printf("slots do ring ............ %u\n", slots);
    printf("requisições atendidas .... %lu\n", (unsigned long)total_);
    printf("  SELECT ................. %lu\n", (unsigned long)por_op_[(int)Op::SELECT]);
    printf("  INSERT ................. %lu\n", (unsigned long)por_op_[(int)Op::INSERT]);
    printf("  UPDATE ................. %lu\n", (unsigned long)por_op_[(int)Op::UPDATE]);
    printf("  DELETE ................. %lu\n", (unsigned long)por_op_[(int)Op::DELETE]);
    printf("duração .................. %.3f s\n", dur_s);
    if (dur_s > 0) printf("throughput ............... %.0f req/s\n", total_ / dur_s);
    if (total_) {
        printf("latência média ........... %.1f us\n", (double)soma_total_us_ / total_);
        printf("espera média no lock ..... %.1f us\n", (double)soma_lock_us_ / total_);
        printf("pior espera no lock ...... %lu us\n", (unsigned long)pior_lock_us_);
    }
    printf("distribuição por worker ... ");
    for (uint32_t i = 0; i < threads && i < 64; ++i)
        printf("W%u=%lu ", i, (unsigned long)por_worker_[i]);
    printf("\n────────────────────────────────────────────────────────\n");
    pthread_mutex_unlock(&mtx_);
}

bool Metricas::anexar_csv(const char* caminho, uint32_t threads, uint32_t slots) const {
    struct stat st{};
    bool novo = (stat(caminho, &st) != 0 || st.st_size == 0);

    FILE* f = fopen(caminho, "a");
    if (!f) return false;
    if (novo)
        fprintf(f, "threads,slots,custo_us,total,select,insert,update,delete,"
                   "duracao_s,throughput_req_s,latencia_media_us,espera_lock_media_us,pior_lock_us\n");

    pthread_mutex_lock(&mtx_);
    double dur_s = (t_fim_us_ > t_inicio_us_) ? (t_fim_us_ - t_inicio_us_) / 1e6 : 0.0;
    fprintf(f, "%u,%u,%u,%lu,%lu,%lu,%lu,%lu,%.6f,%.1f,%.2f,%.2f,%lu\n",
            threads, slots, custo_simulado(), (unsigned long)total_,
            (unsigned long)por_op_[(int)Op::SELECT], (unsigned long)por_op_[(int)Op::INSERT],
            (unsigned long)por_op_[(int)Op::UPDATE], (unsigned long)por_op_[(int)Op::DELETE],
            dur_s, dur_s > 0 ? total_ / dur_s : 0.0,
            total_ ? (double)soma_total_us_ / total_ : 0.0,
            total_ ? (double)soma_lock_us_ / total_ : 0.0,
            (unsigned long)pior_lock_us_);
    pthread_mutex_unlock(&mtx_);

    fclose(f);
    return true;
}

} // namespace srv
