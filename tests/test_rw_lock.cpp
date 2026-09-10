// Testa as duas garantias do lock de leitores-escritores:
//   1. vários leitores entram ao mesmo tempo  (senão não haveria paralelismo)
//   2. um escritor nunca divide a seção crítica com ninguém
// Um mutex único passaria em (2) e falharia em (1).

#include <pthread.h>
#include <unistd.h>
#include <atomic>
#include <cstdio>
#include "servidor/rw_lock.hpp"

namespace {

srv::RwLock lock;
std::atomic<int> leitores_dentro{0};
std::atomic<int> escritores_dentro{0};
std::atomic<int> pico_leitores{0};
std::atomic<int> violacoes{0};

void conferir_invariante() {
    int l = leitores_dentro.load();
    int e = escritores_dentro.load();
    // Combinações proibidas: escritor junto de leitor, ou dois escritores.
    if (e > 1 || (e == 1 && l > 0)) violacoes++;
}

void* leitor(void*) {
    for (int i = 0; i < 200; ++i) {
        lock.lock_leitura();
        int agora = ++leitores_dentro;
        int pico = pico_leitores.load();
        while (agora > pico && !pico_leitores.compare_exchange_weak(pico, agora)) {}
        conferir_invariante();
        usleep(50);
        conferir_invariante();
        --leitores_dentro;
        lock.unlock_leitura();
    }
    return nullptr;
}

void* escritor(void*) {
    for (int i = 0; i < 50; ++i) {
        lock.lock_escrita();
        ++escritores_dentro;
        conferir_invariante();
        usleep(100);
        conferir_invariante();
        --escritores_dentro;
        lock.unlock_escrita();
        usleep(200);
    }
    return nullptr;
}

} // namespace

int main() {
    printf("\n=== test_rw_lock: leitores simultâneos e escritor exclusivo ===\n");

    const int N_LEITORES = 8, N_ESCRITORES = 2;
    pthread_t tl[N_LEITORES], te[N_ESCRITORES];

    for (int i = 0; i < N_LEITORES; ++i)   pthread_create(&tl[i], nullptr, leitor, nullptr);
    for (int i = 0; i < N_ESCRITORES; ++i) pthread_create(&te[i], nullptr, escritor, nullptr);
    for (int i = 0; i < N_LEITORES; ++i)   pthread_join(tl[i], nullptr);
    for (int i = 0; i < N_ESCRITORES; ++i) pthread_join(te[i], nullptr);

    int pico = pico_leitores.load();
    int viol = violacoes.load();

    printf("  pico de leitores simultâneos: %d (de %d threads leitoras)\n", pico, N_LEITORES);
    printf("  [%s] leituras aconteceram em paralelo (pico > 1)\n", pico > 1 ? " ok " : "FALHA");
    printf("  [%s] nenhuma violação de exclusão mútua (%d detectada(s))\n",
           viol == 0 ? " ok " : "FALHA", viol);

    bool ok = (pico > 1) && (viol == 0);
    printf("=== test_rw_lock: %s ===\n", ok ? "passou" : "FALHOU");
    return ok ? 0 : 1;
}
