#pragma once
#include "common/protocolo.hpp"
#include "servidor/banco.hpp"

namespace srv {

// Aplica uma requisição sobre o banco. É aqui que se decide, por operação, se o
// acesso é de leitura (compartilhado) ou de escrita (exclusivo):
//
//   SELECT                     -> lock de leitura   (vários workers juntos)
//   INSERT / UPDATE / DELETE   -> lock de escrita   (um worker por vez)

bool eh_leitura(Op op);

// Custo de processamento simulado, em microssegundos, gasto dentro da seção
// crítica a cada requisição. Com uma tabela pequena o trabalho real é curto
// demais e o benchmark acaba medindo só o custo do IPC; este parâmetro
// representa a consulta cara de um banco de verdade e é o que torna o efeito
// do pool de threads mensurável.
void definir_custo_simulado(uint32_t us);
uint32_t custo_simulado();

Resposta executar(Banco& banco, const Requisicao& req, uint32_t worker);

} // namespace srv
