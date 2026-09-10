// BINÁRIO 1 — servidor (gerenciador do banco).
//
// Processo de SO independente do cliente. Cria os canais IPC, sobe o pool de
// threads e fica no laço da thread despachante: consumir o ring e enfileirar.

#include <getopt.h>
#include <signal.h>
#include <unistd.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include "common/config.hpp"
#include "common/log.hpp"
#include "ipc/fifo_channel.hpp"
#include "ipc/shm_ring.hpp"
#include "servidor/banco.hpp"
#include "servidor/executor.hpp"
#include "servidor/fila_tarefas.hpp"
#include "servidor/metricas.hpp"
#include "servidor/persistencia.hpp"
#include "servidor/pool_threads.hpp"

namespace {

volatile sig_atomic_t encerrar = 0;

void tratar_sinal(int) { encerrar = 1; }

void uso(const char* prog) {
    printf("uso: %s [--threads N] [--slots N] [--custo-us N] [--banco arquivo] [--silencioso]\n"
           "     também aceita MINIDB_THREADS / MINIDB_SLOTS / MINIDB_CUSTO_US no ambiente\n", prog);
}

} // namespace

int main(int argc, char** argv) {
    uint32_t threads = cfg::num_threads();
    uint32_t slots   = cfg::num_slots();
    uint32_t custo_us = cfg::custo_simulado_us();
    const char* arq_inicial = cfg::ARQ_BANCO_INICIAL;
    bool silencioso = false;

    static option opcoes[] = {
        {"threads",    required_argument, nullptr, 't'},
        {"slots",      required_argument, nullptr, 's'},
        {"custo-us",   required_argument, nullptr, 'c'},
        {"banco",      required_argument, nullptr, 'b'},
        {"silencioso", no_argument,       nullptr, 'q'},
        {"ajuda",      no_argument,       nullptr, 'h'},
        {nullptr, 0, nullptr, 0}
    };
    int op;
    while ((op = getopt_long(argc, argv, "t:s:c:b:qh", opcoes, nullptr)) != -1) {
        switch (op) {
            case 't': threads = static_cast<uint32_t>(atoi(optarg)); break;
            case 's': slots   = static_cast<uint32_t>(atoi(optarg)); break;
            case 'c': custo_us = static_cast<uint32_t>(atoi(optarg)); break;
            case 'b': arq_inicial = optarg; break;
            case 'q': silencioso = true; break;
            default:  uso(argv[0]); return op == 'h' ? 0 : 1;
        }
    }
    if (threads < 1 || threads > 64) { fprintf(stderr, "threads fora da faixa 1..64\n"); return 1; }
    if (slots   < 2 || slots > 4096) { fprintf(stderr, "slots fora da faixa 2..4096\n"); return 1; }

    // Sem SA_RESTART: é o EINTR no sem_wait que tira a thread despachante do
    // bloqueio quando chega SIGINT/SIGTERM.
    struct sigaction sa {};
    sa.sa_handler = tratar_sinal;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = 0;
    sigaction(SIGINT,  &sa, nullptr);
    sigaction(SIGTERM, &sa, nullptr);
    signal(SIGPIPE, SIG_IGN);   // cliente que morreu não derruba o servidor

    srv::definir_custo_simulado(custo_us);

    logger::abrir(cfg::ARQ_LOG_SERVIDOR, !silencioso);
    logger::escrever("=== servidor no ar (pid %d) ===", getpid());

    try {
        srv::Banco banco;
        auto iniciais = srv::ler_arquivo(arq_inicial);
        banco.carregar(iniciais);
        logger::escrever("banco carregado de %s com %zu registro(s)", arq_inicial, iniciais.size());

        ipc::ShmRing ring;
        ring.criar(slots);
        logger::escrever("memória compartilhada %s criada com %u slots (%zu bytes por requisição)",
                      cfg::SHM_REQUISICOES, slots, sizeof(Requisicao));
        logger::escrever("semáforos nomeados: %s=%u  %s=0  %s=1",
                      cfg::SEM_VAZIAS, slots, cfg::SEM_CHEIAS, cfg::SEM_MUTEX_RING);

        ipc::FifoEscritor fifo;
        srv::FilaTarefas  fila;
        srv::Metricas     met;
        srv::PoolThreads  pool;
        pool.iniciar(threads, banco, fila, fifo, met);

        met.marcar_inicio();
        if (custo_us) logger::escrever("custo simulado de %u us por requisição", custo_us);
        logger::escrever("aguardando requisições — Ctrl+C ou SIGTERM encerra");

        // Thread despachante: só move dados entre o ring e a fila interna.
        // Nunca toca no banco, então nunca fica presa atrás do lock.
        Requisicao req;
        while (!encerrar) {
            if (!ring.pop(req)) break;   // false = interrompido por sinal
            fila.push(req);
        }

        met.marcar_fim();
        logger::escrever("encerrando: %zu tarefa(s) ainda na fila, pico de %zu",
                      fila.tamanho(), fila.pico());

        fila.encerrar();   // acorda os workers; eles drenam o que sobrou e saem
        pool.aguardar();

        auto final = banco.copia_bruta();
        if (srv::salvar_arquivo(cfg::ARQ_BANCO, final))
            logger::escrever("banco salvo em %s com %zu registro(s)", cfg::ARQ_BANCO, final.size());

        met.anexar_csv(cfg::ARQ_METRICAS, threads, slots);
        if (!silencioso) met.imprimir(threads, slots);
        logger::escrever("=== servidor encerrado ===");
        logger::fechar();
        // Os destrutores de ShmRing/NamedSem fazem shm_unlink e sem_unlink:
        // nenhum objeto de kernel fica para trás.
        return 0;

    } catch (const std::exception& e) {
        logger::escrever("ERRO FATAL: %s", e.what());
        logger::fechar();
        return 1;
    }
}
