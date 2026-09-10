#include "servidor/executor.hpp"
#include <cstdio>
#include <cstring>
#include <ctime>

namespace srv {

namespace {

uint32_t custo_us = 0;

uint64_t agora_us() {
    timespec ts{};
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return static_cast<uint64_t>(ts.tv_sec) * 1000000ULL + ts.tv_nsec / 1000ULL;
}

// Ocupa a CPU de verdade em vez de dormir: uma espera passiva mostraria
// concorrência, mas não paralelismo em múltiplos núcleos.
void gastar_cpu(uint32_t us) {
    if (!us) return;
    uint64_t fim = agora_us() + us;
    volatile uint64_t acumulador = 0;
    while (agora_us() < fim) for (int i = 0; i < 64; ++i) acumulador += i;
}

} // namespace

void definir_custo_simulado(uint32_t us) { custo_us = us; }
uint32_t custo_simulado() { return custo_us; }

bool eh_leitura(Op op) { return op == Op::SELECT; }

Resposta executar(Banco& banco, const Requisicao& req, uint32_t worker) {
    Resposta resp{};
    resp.id_req = req.id_req;
    resp.worker = worker;
    resp.registro.id = req.id;
    resp.registro.nome[0] = '\0';

    if (eh_leitura(req.op)) {
        // Leitura compartilhada: outros SELECT entram na tabela ao mesmo tempo.
        GuardaLeitura g(banco.lock());
        resp.us_no_lock = g.espera_us();

        gastar_cpu(custo_us);
        const Registro* r = banco.buscar(req.id);
        if (r) {
            resp.status = Status::OK;
            resp.registro = *r;
        } else {
            resp.status = Status::NAO_ENCONTRADO;
        }
        return resp;
    }

    // Escrita exclusiva: nenhum leitor e nenhum outro escritor dentro da tabela.
    GuardaEscrita g(banco.lock());
    resp.us_no_lock = g.espera_us();

    gastar_cpu(custo_us);

    switch (req.op) {
        case Op::INSERT: {
            if (banco.buscar(req.id)) { resp.status = Status::ID_DUPLICADO; break; }
            Registro novo{};
            novo.id = req.id;
            snprintf(novo.nome, sizeof(novo.nome), "%s", req.nome);
            resp.status = banco.inserir(novo) ? Status::OK : Status::TABELA_CHEIA;
            resp.registro = novo;
            break;
        }
        case Op::UPDATE: {
            Registro* r = banco.buscar(req.id);
            if (!r) { resp.status = Status::NAO_ENCONTRADO; break; }
            snprintf(r->nome, sizeof(r->nome), "%s", req.nome);
            resp.status = Status::OK;
            resp.registro = *r;
            break;
        }
        case Op::DELETE: {
            resp.status = banco.remover(req.id) ? Status::OK : Status::NAO_ENCONTRADO;
            break;
        }
        default:
            resp.status = Status::MALFORMADA;
    }
    return resp;
}

} // namespace srv
