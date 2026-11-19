/*
 * fs.h - Núcleo do sistema de arquivos: árvore de diretórios e operações.
 *
 * Diretório estruturado em árvore. Cada nó (Entrada) é uma entrada de
 * diretório: um NOME ligado a um INODE. Os filhos de um diretório formam
 * uma lista encadeada (primeiro filho / próximo irmão), ordenada por nome:
 *
 *     /                      raiz->filhos = home
 *     └── home               home->filhos = alice, home->irmao = tmp
 *         ├── alice          alice->irmao = bob
 *         └── bob
 *     └── tmp
 *
 * Toda operação:
 *   1. resolve o caminho (absoluto "/a/b" ou relativo "a/b", com "." e "..");
 *   2. verifica as permissões do usuário atual (perm_checa);
 *   3. só então altera a árvore, o inode ou os blocos;
 *   4. devolve um FsErro (nunca imprime nada).
 */
#ifndef FS_H
#define FS_H

#include <stddef.h>
#include "disco.h"
#include "erros.h"
#include "inode.h"
#include "perm.h"

#define NOME_MAX    32
#define CAMINHO_MAX 256

typedef struct Entrada {
    char            nome[NOME_MAX + 1];
    FCB            *inode;   /* metadados e blocos (pode ser compartilhado por hard links) */
    struct Entrada *pai;     /* ".." (na raiz aponta para ela mesma) */
    struct Entrada *filhos;  /* primeiro filho (só diretórios) */
    struct Entrada *irmao;   /* próximo item do mesmo diretório */
} Entrada;

typedef struct {
    Disco          disco;
    TabelaInodes   inodes;
    Entrada       *raiz;
    Entrada       *cwd;       /* diretório atual */
    const Usuario *atual;     /* "usuário atual" */
    uint16_t       umask;     /* bits retirados das permissões na criação */
    int            prof_exec; /* profundidade de exec (evita recursão infinita) */
} Fs;

/* Cria o sistema com /home/{alice,bob,carol} e /tmp; começa como alice. */
Fs    *fs_cria(void);
void   fs_destroi(Fs *fs);

FsErro fs_resolve(Fs *fs, const char *caminho, Entrada **saida);
Entrada *fs_busca_filho(const Entrada *dir, const char *nome);
void   fs_caminho(const Fs *fs, const Entrada *e, char *buf, size_t tam);

FsErro fs_mkdir(Fs *fs, const char *caminho, int criar_pais);
FsErro fs_rmdir(Fs *fs, const char *caminho);
FsErro fs_cd(Fs *fs, const char *caminho);
FsErro fs_lista(Fs *fs, const char *caminho, Entrada **saida);

FsErro fs_touch(Fs *fs, const char *caminho);
FsErro fs_escreve(Fs *fs, const char *caminho, const char *dados, size_t len, int anexar);
FsErro fs_le(Fs *fs, const char *caminho, size_t offset, size_t n, char **buf, size_t *len);
FsErro fs_trunca(Fs *fs, const char *caminho, size_t tamanho);
FsErro fs_cp(Fs *fs, const char *origem, const char *destino);
FsErro fs_mv(Fs *fs, const char *origem, const char *destino);
FsErro fs_rm(Fs *fs, const char *caminho, int recursivo);
FsErro fs_ln(Fs *fs, const char *origem, const char *destino);

FsErro fs_chmod(Fs *fs, const char *modo, const char *caminho);
FsErro fs_chown(Fs *fs, const char *dono_grupo, const char *caminho);
FsErro fs_exec(Fs *fs, const char *caminho, char **conteudo, size_t *len);
FsErro fs_su(Fs *fs, const char *usuario);

#endif
