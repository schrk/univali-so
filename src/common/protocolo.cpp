#include "common/protocolo.hpp"
#include <cstdio>
#include <cstring>

const char* nome_op(Op op) {
    switch (op) {
        case Op::INSERT: return "INSERT";
        case Op::DELETE: return "DELETE";
        case Op::SELECT: return "SELECT";
        case Op::UPDATE: return "UPDATE";
    }
    return "?";
}

const char* nome_status(Status s) {
    switch (s) {
        case Status::OK:             return "OK";
        case Status::NAO_ENCONTRADO: return "NAO_ENCONTRADO";
        case Status::ID_DUPLICADO:   return "ID_DUPLICADO";
        case Status::TABELA_CHEIA:   return "TABELA_CHEIA";
        case Status::MALFORMADA:     return "MALFORMADA";
    }
    return "?";
}

void descrever(const Requisicao& r, char* saida, size_t tam) {
    switch (r.op) {
        case Op::INSERT:
            snprintf(saida, tam, "INSERT id=%d nome='%s'", r.id, r.nome);
            break;
        case Op::UPDATE:
            snprintf(saida, tam, "UPDATE nome='%s' WHERE id=%d", r.nome, r.id);
            break;
        case Op::SELECT:
            snprintf(saida, tam, "SELECT nome WHERE id=%d", r.id);
            break;
        case Op::DELETE:
            snprintf(saida, tam, "DELETE WHERE id=%d", r.id);
            break;
        default:
            snprintf(saida, tam, "(operacao invalida)");
    }
}
