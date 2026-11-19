/* disco.c - Gerência de espaço livre do disco simulado (bitmap). */
#include <string.h>
#include "disco.h"

void disco_init(Disco *d)
{
    memset(d->dados, 0, sizeof d->dados);
    memset(d->livre, 1, sizeof d->livre);
    d->n_livres = N_BLOCOS;
}

int disco_aloca(Disco *d)
{
    for (int b = 0; b < N_BLOCOS; b++) {
        if (d->livre[b]) {
            d->livre[b] = 0;
            d->n_livres--;
            memset(d->dados[b], 0, TAM_BLOCO);
            return b;
        }
    }
    return -1;
}

void disco_libera(Disco *d, int bloco)
{
    if (bloco < 0 || bloco >= N_BLOCOS || d->livre[bloco])
        return; /* número inválido ou já livre: ignora */
    d->livre[bloco] = 1;
    d->n_livres++;
}
