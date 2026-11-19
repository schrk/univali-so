/*
 * disco.h - "Disco" simulado: um vetor de bytes dividido em blocos.
 *
 * O disco não sabe nada de arquivos. Ele só guarda blocos de TAM_BLOCO
 * bytes e um mapa de bits (bitmap) dizendo quais blocos estão livres.
 * Quem decide quais blocos pertencem a qual arquivo é o inode (inode.c),
 * usando alocação indexada.
 */
#ifndef DISCO_H
#define DISCO_H

#include <stdint.h>

#define TAM_BLOCO 64   /* bytes por bloco: pequeno de propósito, para um
                          texto curto já ocupar vários blocos */
#define N_BLOCOS  256  /* 256 * 64 = 16 KiB de disco */

typedef struct {
    char    dados[N_BLOCOS][TAM_BLOCO]; /* o disco propriamente dito */
    uint8_t livre[N_BLOCOS];            /* bitmap: 1 = livre, 0 = ocupado */
    int     n_livres;                   /* contador, evita varrer o bitmap */
} Disco;

void disco_init(Disco *d);

/* Aloca o primeiro bloco livre (first-fit), zera seu conteúdo e devolve o
 * número dele, ou -1 se o disco estiver cheio. */
int  disco_aloca(Disco *d);

/* Marca o bloco como livre de novo no bitmap. O conteúdo não é apagado,
 * como num disco real; ele é zerado na próxima alocação. */
void disco_libera(Disco *d, int bloco);

#endif
