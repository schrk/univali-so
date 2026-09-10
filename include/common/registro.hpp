#pragma once
// Registro do "banco de dados simulado" — estrutura exigida pelo enunciado.
// POD de tamanho fixo: é copiado byte a byte entre os dois processos.

struct Registro {
    int  id;
    char nome[50];
};
