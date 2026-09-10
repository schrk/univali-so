// BINÁRIO 2 — cliente.
//
// Processo de SO independente do servidor: eles só se falam pelos canais IPC.
// Duas threads: uma produz requisições no ring, a outra recebe as respostas
// pelo FIFO. Separá-las é o que permite continuar enviando enquanto as
// primeiras respostas ainda estão chegando.

#include <getopt.h>
#include <pthread.h>
#include <unistd.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <vector>
#include "cliente/gerador_carga.hpp"
#include "common/config.hpp"
#include "common/log.hpp"
#include "ipc/fifo_channel.hpp"
#include "ipc/shm_ring.hpp"

namespace {

uint64_t agora_us() {
    timespec ts{};
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return static_cast<uint64_t>(ts.tv_sec) * 1000000ULL + ts.tv_nsec / 1000ULL;
}

struct ContextoProdutor {
    ipc::ShmRing*            ring;
    std::vector<Requisicao>* reqs;
};

void* produzir(void* arg) {
    auto* c = static_cast<ContextoProdutor*>(arg);
    for (auto& r : *c->reqs) {
        c->ring->push(r);   // bloqueia sozinho se o ring encher
    }
    logger::escrever("thread produtora: %zu requisição(ões) entregues ao ring", c->reqs->size());
    return nullptr;
}

struct ContextoReceptor {
    ipc::FifoLeitor* fifo;
    size_t           esperadas;
    size_t           recebidas;
    size_t           por_status[5];
    bool             detalhar;
};

void* receber(void* arg) {
    auto* c = static_cast<ContextoReceptor*>(arg);
    Resposta resp{};
    while (c->recebidas < c->esperadas) {
        if (!c->fifo->ler(resp)) break;   // FIFO fechado
        c->recebidas++;
        int s = static_cast<int>(resp.status);
        if (s >= 0 && s < 5) c->por_status[s]++;

        if (c->detalhar) {
            if (resp.status == Status::OK && resp.registro.nome[0])
                logger::escrever("resp #%u  %-14s  id=%d nome='%s'  (W%u, lock %lu us)",
                              resp.id_req, nome_status(resp.status), resp.registro.id,
                              resp.registro.nome, resp.worker,
                              static_cast<unsigned long>(resp.us_no_lock));
            else
                logger::escrever("resp #%u  %-14s  id=%d  (W%u, lock %lu us)",
                              resp.id_req, nome_status(resp.status), resp.registro.id,
                              resp.worker, static_cast<unsigned long>(resp.us_no_lock));
        }
    }
    return nullptr;
}

void uso(const char* prog) {
    printf("uso: %s [opções]\n"
           "  --carga arquivo     requisições em texto (padrão: data/cargas/basica.txt)\n"
           "  --gerar N           gera N requisições sintéticas em vez de ler arquivo\n"
           "  --leitura P         percentual de SELECT na carga sintética (padrão 70)\n"
           "  --faixa N           maior id sorteado na carga sintética (padrão 50)\n"
           "  --semente N         semente do gerador (padrão 42)\n"
           "  --silencioso        não imprime resposta a resposta\n", prog);
}

} // namespace

int main(int argc, char** argv) {
    const char* arq_carga = "data/cargas/basica.txt";
    uint32_t gerar_n = 0, pct_leitura = 70, semente = 42;
    int32_t  faixa = 50;
    bool     silencioso = false;

    static option opcoes[] = {
        {"carga",      required_argument, nullptr, 'c'},
        {"gerar",      required_argument, nullptr, 'g'},
        {"leitura",    required_argument, nullptr, 'l'},
        {"faixa",      required_argument, nullptr, 'f'},
        {"semente",    required_argument, nullptr, 'x'},
        {"silencioso", no_argument,       nullptr, 'q'},
        {"ajuda",      no_argument,       nullptr, 'h'},
        {nullptr, 0, nullptr, 0}
    };
    int op;
    while ((op = getopt_long(argc, argv, "c:g:l:f:x:qh", opcoes, nullptr)) != -1) {
        switch (op) {
            case 'c': arq_carga = optarg; break;
            case 'g': gerar_n = static_cast<uint32_t>(atoi(optarg)); break;
            case 'l': pct_leitura = static_cast<uint32_t>(atoi(optarg)); break;
            case 'f': faixa = atoi(optarg); break;
            case 'x': semente = static_cast<uint32_t>(atoi(optarg)); break;
            case 'q': silencioso = true; break;
            default:  uso(argv[0]); return op == 'h' ? 0 : 1;
        }
    }
    if (pct_leitura > 100) { fprintf(stderr, "--leitura precisa estar entre 0 e 100\n"); return 1; }
    if (faixa < 1)         { fprintf(stderr, "--faixa precisa ser >= 1\n"); return 1; }

    logger::abrir(cfg::ARQ_LOG_CLIENTE, !silencioso);

    auto reqs = gerar_n ? cli::gerar(gerar_n, pct_leitura, faixa, semente)
                        : cli::ler_de_arquivo(arq_carga);
    if (reqs.empty()) {
        fprintf(stderr, "nenhuma requisição para enviar (carga vazia ou arquivo inexistente)\n");
        return 1;
    }

    const int32_t pid = getpid();
    for (size_t i = 0; i < reqs.size(); ++i) {
        reqs[i].id_req = static_cast<uint32_t>(i + 1);
        reqs[i].pid_cliente = pid;
    }

    try {
        // O FIFO precisa existir antes da primeira requisição: o servidor pode
        // responder imediatamente depois do primeiro push.
        ipc::FifoLeitor fifo;
        fifo.criar(pid);

        ipc::ShmRing ring;
        ring.abrir();

        logger::escrever("=== cliente %d: %zu requisição(ões), ring com %u slots ===",
                      pid, reqs.size(), ring.capacidade());

        ContextoReceptor cr{&fifo, reqs.size(), 0, {0, 0, 0, 0, 0}, !silencioso};
        ContextoProdutor cp{&ring, &reqs};

        uint64_t t0 = agora_us();

        pthread_t th_receptora, th_produtora;
        pthread_create(&th_receptora, nullptr, receber, &cr);
        pthread_create(&th_produtora, nullptr, produzir, &cp);

        pthread_join(th_produtora, nullptr);
        pthread_join(th_receptora, nullptr);

        double dur_s = (agora_us() - t0) / 1e6;

        printf("\n─── cliente %d ─────────────────────────────────────────\n", pid);
        printf("enviadas ................. %zu\n", reqs.size());
        printf("respostas recebidas ...... %zu\n", cr.recebidas);
        printf("  OK ..................... %zu\n", cr.por_status[(int)Status::OK]);
        printf("  NAO_ENCONTRADO ......... %zu\n", cr.por_status[(int)Status::NAO_ENCONTRADO]);
        printf("  ID_DUPLICADO ........... %zu\n", cr.por_status[(int)Status::ID_DUPLICADO]);
        printf("  TABELA_CHEIA ........... %zu\n", cr.por_status[(int)Status::TABELA_CHEIA]);
        printf("tempo total .............. %.3f s\n", dur_s);
        if (dur_s > 0) printf("throughput ............... %.0f req/s\n", reqs.size() / dur_s);
        printf("────────────────────────────────────────────────────────\n");

        logger::fechar();
        return cr.recebidas == reqs.size() ? 0 : 2;

    } catch (const std::exception& e) {
        fprintf(stderr, "ERRO: %s\n", e.what());
        logger::fechar();
        return 1;
    }
}
