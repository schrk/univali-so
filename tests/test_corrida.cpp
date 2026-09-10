// Demonstração da condição de corrida.
//
// A mesma carga é aplicada duas vezes por N threads: uma vez SEM exclusão mútua
// e outra COM o lock. Sem o lock, o total final varia a cada execução e não bate
// com o esperado — é a prova, em número, de que a seção crítica é necessária.

#include <pthread.h>
#include <cstdio>
#include <vector>
#include "common/registro.hpp"
#include "servidor/rw_lock.hpp"

namespace {

const int N_THREADS = 8;
const int POR_THREAD = 2000;

std::vector<Registro> tabela;
srv::RwLock lock;
bool usar_lock = false;

void* inserir(void* arg) {
    long base = reinterpret_cast<long>(arg) * POR_THREAD;
    for (int i = 0; i < POR_THREAD; ++i) {
        Registro r{};
        r.id = static_cast<int>(base + i);
        snprintf(r.nome, sizeof(r.nome), "n%ld", base + i);

        if (usar_lock) {
            // Escrita exclusiva: push_back nunca é interrompido no meio.
            srv::GuardaEscrita g(lock);
            tabela.push_back(r);
        } else {
            // Sem proteção: dois push_back concorrentes corrompem o tamanho do
            // vetor e podem perder elementos (ou derrubar o processo).
            tabela.push_back(r);
        }
    }
    return nullptr;
}

size_t rodar(bool com_lock) {
    tabela.clear();
    tabela.reserve(N_THREADS * POR_THREAD * 2); // evita realloc no meio da corrida
    usar_lock = com_lock;

    pthread_t th[N_THREADS];
    for (long i = 0; i < N_THREADS; ++i) pthread_create(&th[i], nullptr, inserir, reinterpret_cast<void*>(i));
    for (int i = 0; i < N_THREADS; ++i)  pthread_join(th[i], nullptr);
    return tabela.size();
}

} // namespace

int main() {
    const size_t esperado = static_cast<size_t>(N_THREADS) * POR_THREAD;
    printf("\n=== test_corrida: %d threads × %d inserções (esperado: %zu) ===\n",
           N_THREADS, POR_THREAD, esperado);

    size_t sem = rodar(false);
    printf("  SEM lock ... %zu registros  (%s)\n", sem,
           sem == esperado ? "coincidiu desta vez — repita, o resultado é instável"
                           : "REGISTROS PERDIDOS — condição de corrida");

    size_t com = rodar(true);
    printf("  COM lock ... %zu registros  (%s)\n", com,
           com == esperado ? "correto" : "ERRO: o lock deveria ter garantido o total");

    printf("  diferença .. %ld registro(s)\n", static_cast<long>(esperado) - static_cast<long>(sem));
    printf("=== test_corrida: %s ===\n", com == esperado ? "lock validado" : "FALHOU");

    // O critério de sucesso é o caminho COM lock: a versão sem lock é a
    // demonstração, e ela pode acertar por sorte em uma execução isolada.
    return com == esperado ? 0 : 1;
}
