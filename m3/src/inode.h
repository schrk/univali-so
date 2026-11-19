/*
 * inode.h - File Control Block (FCB) / inode simulado.
 *
 * O FCB guarda TODOS os atributos de um arquivo, menos o nome. O nome fica
 * na entrada de diretório (fs.h), como no Unix. Por isso:
 *   - mv só troca a entrada de lugar e o número do inode não muda;
 *   - ln cria outra entrada apontando para o MESMO inode (n_links++);
 *   - o inode só é destruído quando n_links chega a zero.
 *
 * Os dados ficam no disco simulado, em blocos apontados por índices
 * (alocação indexada): N_DIRETOS ponteiros diretos no próprio FCB e um
 * bloco indireto, que é um bloco do disco cheio de números de blocos.
 */
#ifndef INODE_H
#define INODE_H

#include <stddef.h>
#include <stdint.h>
#include <time.h>
#include "disco.h"
#include "erros.h"

#define N_DIRETOS      8
#define PTRS_INDIRETO  ((int)(TAM_BLOCO / sizeof(int32_t)))   /* 16 */
#define MAX_BLOCOS_ARQ (N_DIRETOS + PTRS_INDIRETO)            /* 24 */
#define TAM_MAX_ARQ    ((size_t)MAX_BLOCOS_ARQ * TAM_BLOCO)   /* 1536 bytes */

typedef enum {
    T_DIRETORIO,
    T_TEXTO,      /* caracteres */
    T_NUMERICO,   /* só dígitos, sinais e separadores */
    T_BINARIO,    /* contém bytes não imprimíveis */
    T_PROGRAMA    /* começa com "#!" e pode ser executado com exec */
} TipoArquivo;

typedef struct {
    int         id;          /* número do inode (índice na tabela) */
    TipoArquivo tipo;
    size_t      tamanho;     /* em bytes */
    int         dono;        /* uid */
    int         grupo;       /* gid */
    uint16_t    perm;        /* 9 bits rwxrwxrwx, ex.: 0754 */
    time_t      criado;
    time_t      modificado;
    time_t      acessado;
    int         n_links;     /* entradas de diretório que apontam para cá */
    int         n_blocos;    /* blocos de dados em uso */
    int         diretos[N_DIRETOS]; /* -1 = não usado */
    int         indireto;    /* bloco de índices, -1 = não usado */
} FCB;

/* Tabela de inodes: vetor dinâmico de ponteiros, cresce com realloc.
 * v[id] == NULL indica um número de inode livre. O índice 0 não é usado. */
typedef struct {
    FCB **v;
    int   cap;
    int   usados;
} TabelaInodes;

FsErro inodes_init(TabelaInodes *t);
void   inodes_destroi(TabelaInodes *t, Disco *d);

/* Cria um inode vazio (sem blocos) no primeiro número livre. NULL se faltar memória. */
FCB   *inode_novo(TabelaInodes *t, TipoArquivo tipo, int dono, int grupo, uint16_t perm);

/* Libera os blocos do inode, tira ele da tabela e faz free. */
void   inode_libera(TabelaInodes *t, Disco *d, FCB *f);

/* Número do bloco físico que guarda o bloco lógico i do arquivo, ou -1. */
int    inode_bloco(const Disco *d, const FCB *f, int i);

/* Substitui (anexar = 0) ou acrescenta (anexar = 1) conteúdo.
 * Se não couber, devolve erro SEM alterar nada no arquivo. */
FsErro inode_escreve(Disco *d, FCB *f, const char *dados, size_t len, int anexar);

/* Copia até n bytes a partir de offset para saida. Devolve quantos copiou. */
size_t inode_le(const Disco *d, const FCB *f, size_t offset, size_t n, char *saida);

/* Muda o tamanho: diminuindo, libera blocos; aumentando, completa com zeros. */
FsErro inode_trunca(Disco *d, FCB *f, size_t novo_tamanho);

const char *tipo_nome(TipoArquivo t);

/* Relógio do simulador. Com a variável MFS_DATA_FIXA definida, devolve
 * sempre o mesmo instante (útil para comparar saídas). */
time_t agora(void);

#endif
