#pragma once
#include <vector>
#include "common/protocolo.hpp"

namespace cli {

// Duas origens de carga: um arquivo com as requisições em texto, ou uma carga
// sintética com proporção de leitura configurável — é ela que permite comparar
// cenários "só leitura" e "leitura e escrita" no relatório.

std::vector<Requisicao> ler_de_arquivo(const char* caminho);
std::vector<Requisicao> gerar(uint32_t quantidade, uint32_t pct_leitura,
                              int32_t faixa_ids, unsigned semente);

} // namespace cli
