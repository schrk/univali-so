#include "cliente/parser.hpp"
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace cli {

namespace {

const char* pular_espacos(const char* p) {
    while (*p && isspace(static_cast<unsigned char>(*p))) ++p;
    return p;
}

// Extrai o inteiro de "id=<n>" em qualquer posição da linha.
bool extrair_id(const char* linha, int32_t& id) {
    const char* p = strstr(linha, "id=");
    if (!p) return false;
    p += 3;
    p = pular_espacos(p);
    char* fim = nullptr;
    long v = strtol(p, &fim, 10);
    if (fim == p) return false;
    id = static_cast<int32_t>(v);
    return true;
}

// Extrai o texto de "nome='...'" (também aceita aspas duplas ou sem aspas).
bool extrair_nome(const char* linha, char* saida, size_t tam) {
    const char* p = strstr(linha, "nome=");
    if (!p) return false;
    p += 5;
    p = pular_espacos(p);

    char aspas = 0;
    if (*p == '\'' || *p == '"') { aspas = *p; ++p; }

    size_t i = 0;
    while (*p && i + 1 < tam) {
        if (aspas && *p == aspas) break;
        if (!aspas && isspace(static_cast<unsigned char>(*p))) break;
        saida[i++] = *p++;
    }
    saida[i] = '\0';
    return i > 0;
}

} // namespace

bool interpretar(const char* linha, Requisicao& saida) {
    const char* p = pular_espacos(linha);
    memset(&saida, 0, sizeof(saida));

    if      (strncasecmp(p, "INSERT", 6) == 0) saida.op = Op::INSERT;
    else if (strncasecmp(p, "SELECT", 6) == 0) saida.op = Op::SELECT;
    else if (strncasecmp(p, "UPDATE", 6) == 0) saida.op = Op::UPDATE;
    else if (strncasecmp(p, "DELETE", 6) == 0) saida.op = Op::DELETE;
    else return false;

    if (!extrair_id(p, saida.id)) return false;

    // Só INSERT e UPDATE carregam nome; nas outras o campo fica vazio.
    if (saida.op == Op::INSERT || saida.op == Op::UPDATE) {
        if (!extrair_nome(p, saida.nome, sizeof(saida.nome))) return false;
    }
    return true;
}

} // namespace cli
