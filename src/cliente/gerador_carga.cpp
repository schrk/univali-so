#include "cliente/gerador_carga.hpp"
#include "cliente/parser.hpp"
#include <cstdio>
#include <cstring>
#include <random>

namespace cli {

std::vector<Requisicao> ler_de_arquivo(const char* caminho) {
    std::vector<Requisicao> reqs;
    FILE* f = fopen(caminho, "r");
    if (!f) return reqs;

    char linha[256];
    int  n_linha = 0;
    while (fgets(linha, sizeof(linha), f)) {
        ++n_linha;
        linha[strcspn(linha, "\r\n")] = '\0';
        if (linha[0] == '\0' || linha[0] == '#') continue;

        Requisicao r{};
        if (interpretar(linha, r)) {
            reqs.push_back(r);
        } else {
            fprintf(stderr, "aviso: linha %d ignorada (não interpretada): %s\n", n_linha, linha);
        }
    }
    fclose(f);
    return reqs;
}

std::vector<Requisicao> gerar(uint32_t quantidade, uint32_t pct_leitura,
                              int32_t faixa_ids, unsigned semente) {
    // Carga sintética com proporção de leitura controlada: é o que permite
    // comparar "só leitura" (onde o rw_lock brilha) com uma carga mista.
    static const char* nomes[] = {"Joao", "Maria", "Ana", "Pedro", "Lucas",
                                  "Julia", "Bruno", "Carla", "Rafael", "Beatriz"};
    std::mt19937 rng(semente);
    std::uniform_int_distribution<int> sorteio(0, 99);
    std::uniform_int_distribution<int> id_aleatorio(1, faixa_ids);
    std::uniform_int_distribution<int> nome_aleatorio(0, 9);

    std::vector<Requisicao> reqs;
    reqs.reserve(quantidade);

    for (uint32_t i = 0; i < quantidade; ++i) {
        Requisicao r{};
        r.id = id_aleatorio(rng);

        if (static_cast<uint32_t>(sorteio(rng)) < pct_leitura) {
            r.op = Op::SELECT;
        } else {
            int d = sorteio(rng);
            if      (d < 50) r.op = Op::INSERT;
            else if (d < 85) r.op = Op::UPDATE;
            else             r.op = Op::DELETE;
        }
        if (r.op == Op::INSERT || r.op == Op::UPDATE)
            snprintf(r.nome, sizeof(r.nome), "%s-%d", nomes[nome_aleatorio(rng)], i);

        reqs.push_back(r);
    }
    return reqs;
}

} // namespace cli
