#pragma once
#include <cstddef>
#include <cstdint>
#include "common/registro.hpp"

// Contrato entre os dois binários. Nada de ponteiros ou std::string: um slot do
// ring e uma mensagem do FIFO são cópias byte a byte, então cliente e servidor
// enxergam exatamente o mesmo layout de memória.

enum class Op : uint8_t { INSERT = 0, DELETE = 1, SELECT = 2, UPDATE = 3 };

enum class Status : uint8_t {
    OK = 0,
    NAO_ENCONTRADO = 1,
    ID_DUPLICADO   = 2,
    TABELA_CHEIA   = 3,
    MALFORMADA     = 4
};

struct Requisicao {
    uint32_t id_req;      // correlaciona requisição e resposta
    int32_t  pid_cliente; // permite vários clientes no mesmo ring
    Op       op;
    int32_t  id;          // chave do registro
    char     nome[50];    // carga útil de INSERT / UPDATE
};

struct Resposta {
    uint32_t id_req;
    Status   status;
    Registro registro;    // preenchido em SELECT
    uint64_t us_no_lock;  // tempo esperando o lock — alimenta as métricas
    uint64_t us_total;    // tempo total de atendimento dentro do worker
    uint32_t worker;      // qual thread do pool atendeu
};

const char* nome_op(Op op);
const char* nome_status(Status s);

// Formata a requisição de volta no texto original ("SELECT nome WHERE id=5").
void descrever(const Requisicao& r, char* saida, size_t tam);
