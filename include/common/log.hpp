#pragma once
#include <cstdio>

// Log com timestamp e identificação da thread. Uma única linha por chamada,
// serializada por mutex: sem isso as mensagens dos N workers se intercalam.

namespace logger {

void abrir(const char* caminho, bool tambem_no_terminal);
void escrever(const char* fmt, ...) __attribute__((format(printf, 1, 2)));
void fechar();

} // namespace logger
