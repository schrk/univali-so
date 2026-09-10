#pragma once
#include <vector>
#include "common/registro.hpp"

namespace srv {

// O banco simulado vive em memória durante a execução e é despejado em texto
// (id;nome por linha) no encerramento, para que o estado sobreviva entre
// execuções e possa ser conferido a olho nu depois do teste.

std::vector<Registro> ler_arquivo(const char* caminho);
bool salvar_arquivo(const char* caminho, const std::vector<Registro>& regs);

} // namespace srv
