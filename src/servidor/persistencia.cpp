#include "servidor/persistencia.hpp"
#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace srv {

// Formato: uma linha por registro, "id;nome". Texto puro para que o estado do
// banco possa ser conferido a olho nu depois de cada execução.

std::vector<Registro> ler_arquivo(const char* caminho) {
    std::vector<Registro> regs;
    FILE* f = fopen(caminho, "r");
    if (!f) return regs;

    char linha[256];
    while (fgets(linha, sizeof(linha), f)) {
        if (linha[0] == '#' || linha[0] == '\n') continue;
        char* sep = strchr(linha, ';');
        if (!sep) continue;
        *sep = '\0';

        Registro r{};
        r.id = atoi(linha);
        char* nome = sep + 1;
        nome[strcspn(nome, "\r\n")] = '\0';
        snprintf(r.nome, sizeof(r.nome), "%s", nome);
        regs.push_back(r);
    }
    fclose(f);
    return regs;
}

bool salvar_arquivo(const char* caminho, const std::vector<Registro>& regs) {
    FILE* f = fopen(caminho, "w");
    if (!f) return false;
    fprintf(f, "# banco simulado — id;nome — %zu registro(s)\n", regs.size());
    for (const auto& r : regs) fprintf(f, "%d;%s\n", r.id, r.nome);
    fclose(f);
    return true;
}

} // namespace srv
