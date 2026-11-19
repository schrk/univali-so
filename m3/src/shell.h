/*
 * shell.h - Interpretador de comandos no estilo Linux.
 *
 * O shell é a única camada que conversa com o usuário: separa a linha em
 * argumentos, chama a função do fs e transforma o FsErro em mensagem.
 * O menu numerado (main.c) também usa shell_executa_argv, então os dois
 * modos de uso têm exatamente o mesmo comportamento.
 */
#ifndef SHELL_H
#define SHELL_H

#include "fs.h"

#define LINHA_MAX  1024
#define SHELL_SAIR (-1)   /* devolvido quando o usuário digita exit */

/* Executa uma linha ("echo oi > a.txt"). 0 = ok, 1 = erro, SHELL_SAIR = exit. */
int  shell_executa_linha(Fs *fs, const char *linha);

/* Executa um comando já separado em argumentos. */
int  shell_executa_argv(Fs *fs, int argc, char **argv);

/* Laço do terminal. No modo menu, "menu" volta ao menu.
 * Devolve 1 se o usuário pediu para encerrar o programa. */
int  shell_loop(Fs *fs, int dentro_do_menu);

void shell_prompt(const Fs *fs, char *buf, size_t tam);

#endif
