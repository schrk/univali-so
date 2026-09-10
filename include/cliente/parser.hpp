#pragma once
#include "common/protocolo.hpp"

namespace cli {

// Converte a sintaxe do enunciado em uma Requisicao. Formatos aceitos:
//
//   INSERT id=7 nome='João'
//   SELECT nome WHERE id=5
//   UPDATE nome='Ana' WHERE id=7
//   DELETE WHERE id=7
//
// Linhas vazias e começadas por '#' são ignoradas pelo chamador.

bool interpretar(const char* linha, Requisicao& saida);

} // namespace cli
