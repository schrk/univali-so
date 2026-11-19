/*
 * perm.h - Usuários, grupos e controle de acesso RWX.
 *
 * As permissões ficam em 9 bits no FCB, no mesmo layout do Unix:
 *
 *        dono   grupo  outros
 *        r w x  r w x  r w x
 *  bit:  8 7 6  5 4 3  2 1 0      ex.: 0754 = rwxr-xr--
 *
 * Para saber se um usuário pode fazer uma operação, escolhe-se UMA classe
 * (dono, senão grupo, senão outros), desloca-se o campo dela para os 3 bits
 * de baixo e testa-se com AND. Esse é o "bloqueio obrigatório": toda
 * operação do fs passa por perm_checa antes de tocar nos dados.
 */
#ifndef PERM_H
#define PERM_H

#include <stdbool.h>
#include <stdint.h>
#include "inode.h"

enum { P_X = 1, P_W = 2, P_R = 4 };

typedef struct { int uid; char nome[16]; int gid; } Usuario;
typedef struct { int gid; char nome[16]; } Grupo;

/* Usuários e grupos fixos do simulador (sem senha). */
const Usuario *usuario_por_nome(const char *nome);
const Usuario *usuario_por_uid(int uid);
const Grupo   *grupo_por_nome(const char *nome);
const char    *nome_usuario(int uid);
const char    *nome_grupo(int gid);

/* true se o usuário u pode fazer op (combinação de P_R|P_W|P_X) em f. */
bool perm_checa(const Usuario *u, const FCB *f, int op);

/* "drwxr-x---": 1 caractere de tipo + 9 de permissão. out precisa de 11 bytes. */
void perm_str(const FCB *f, char out[11]);

/* Interpreta o modo do chmod: octal ("640") ou simbólico ("u+x,go-w", "a=r").
 * atual é a permissão de hoje (o modo simbólico parte dela). */
FsErro perm_parse(const char *modo, uint16_t atual, uint16_t *resultado);

#endif
