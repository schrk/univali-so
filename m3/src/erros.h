/*
 * erros.h - Códigos de erro do mini-sistema de arquivos.
 *
 * Nenhuma função das camadas internas (fs, inode, perm, disco) imprime
 * mensagens ou encerra o programa: todas devolvem um FsErro. Só o shell
 * transforma o código em texto, no formato "comando: alvo: mensagem",
 * parecido com o que o Linux mostra.
 */
#ifndef ERROS_H
#define ERROS_H

typedef enum {
    FS_OK = 0,
    FS_E_NAO_EXISTE,      /* ENOENT  */
    FS_E_JA_EXISTE,       /* EEXIST  */
    FS_E_PERMISSAO,       /* EACCES  */
    FS_E_NAO_DIRETORIO,   /* ENOTDIR */
    FS_E_EH_DIRETORIO,    /* EISDIR  */
    FS_E_NAO_VAZIO,       /* ENOTEMPTY */
    FS_E_NOME_INVALIDO,   /* nome vazio, longo demais ou com caractere proibido */
    FS_E_SEM_ESPACO,      /* ENOSPC: acabaram os blocos do disco simulado */
    FS_E_ARQ_GRANDE,      /* EFBIG: o inode não tem mais ponteiros de bloco */
    FS_E_SEM_MEMORIA,     /* malloc/calloc/realloc falhou */
    FS_E_ARGUMENTOS,      /* uso incorreto (ex.: modo de chmod inválido) */
    FS_E_OPERACAO,        /* EPERM / EINVAL: operação não permitida */
    FS_E_NAO_EXECUTAVEL,  /* ENOEXEC: não é um programa */
    FS_E_USUARIO,         /* usuário ou grupo inexistente */
    FS_E_EM_USO           /* EBUSY: diretório atual ou ancestral dele */
} FsErro;

/* Texto legível de um código de erro. */
const char *fs_strerro(FsErro e);

#endif
