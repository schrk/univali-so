/*
 * main.c - Ponto de entrada: menu interativo numerado ou terminal.
 *
 * O menu segue a ideia do trabalho de referência (eldanielG/mini_fs_linux):
 * opções numeradas que pedem os dados ao usuário. A diferença é que cada
 * opção monta um comando e chama shell_executa_argv, ou seja, o menu e o
 * terminal executam exatamente o mesmo código, sobre a estrutura simulada.
 *
 * Uso:
 *   ./mfs         menu interativo
 *   ./mfs -t      direto no terminal (comandos estilo Linux)
 *   ./mfs -t < roteiro.txt   executa um roteiro (ex.: demo.txt)
 */
#include <stdio.h>
#include <string.h>
#include "fs.h"
#include "shell.h"

typedef struct {
    const char *titulo;
    const char *comando;      /* nome do comando no shell */
    const char *perguntas[3]; /* o que pedir ao usuário, na ordem dos argumentos */
} OpcaoMenu;

static const OpcaoMenu OPCOES[] = {
    /*  1 */ { "Criar diretório (mkdir)",           "mkdir",  { "Nome do diretório" } },
    /*  2 */ { "Mudar de diretório (cd)",           "cd",     { "Diretório (.. sobe, / raiz)" } },
    /*  3 */ { "Listar diretório (ls -l)",          "ls",     { NULL } },
    /*  4 */ { "Árvore de diretórios (tree)",       "tree",   { NULL } },
    /*  5 */ { "Criar arquivo (touch)",             "touch",  { "Nome do arquivo" } },
    /*  6 */ { "Escrever no arquivo (echo >)",      "echo",   { "Arquivo", "Conteúdo (\\n quebra linha)" } },
    /*  7 */ { "Acrescentar ao arquivo (echo >>)",  "echo",   { "Arquivo", "Conteúdo (\\n quebra linha)" } },
    /*  8 */ { "Ler arquivo (cat)",                 "cat",    { "Arquivo" } },
    /*  9 */ { "Ler a partir de uma posição (read)", "read",  { "Arquivo", "Posição (offset)", "Quantidade de bytes" } },
    /* 10 */ { "Truncar arquivo (truncate)",        "truncate", { "Arquivo", "Novo tamanho" } },
    /* 11 */ { "Copiar (cp)",                       "cp",     { "Origem", "Destino" } },
    /* 12 */ { "Mover/renomear (mv)",               "mv",     { "Origem", "Destino" } },
    /* 13 */ { "Remover arquivo (rm)",              "rm",     { "Arquivo" } },
    /* 14 */ { "Remover diretório vazio (rmdir)",   "rmdir",  { "Diretório" } },
    /* 15 */ { "Criar hard link (ln)",              "ln",     { "Arquivo existente", "Novo nome" } },
    /* 16 */ { "Alterar permissões (chmod)",        "chmod",  { "Arquivo", "Modo (ex.: 640 ou u+x)" } },
    /* 17 */ { "Alterar dono (chown, só root)",     "chown",  { "Arquivo", "usuario[:grupo]" } },
    /* 18 */ { "Ver metadados / FCB (stat)",        "stat",   { "Caminho" } },
    /* 19 */ { "Ver blocos do arquivo (blocos)",    "blocos", { "Arquivo" } },
    /* 20 */ { "Uso do disco (df)",                 "df",     { NULL } },
    /* 21 */ { "Tabela de inodes (inodes)",         "inodes", { NULL } },
    /* 22 */ { "Trocar de usuário (su)",            "su",     { "Usuário (root, alice, bob, carol)" } },
    /* 23 */ { "Executar programa (exec)",          "exec",   { "Programa" } },
    /* 24 */ { "Abrir terminal (comandos Linux)",   NULL,     { NULL } },
};

#define N_OPCOES ((int)(sizeof OPCOES / sizeof OPCOES[0]))

/* Lê uma linha com fgets (aceita espaços, não estoura o buffer). 0 no EOF. */
static int ler(const char *pergunta, char *buf, size_t tam)
{
    printf("%s: ", pergunta);
    fflush(stdout);
    if (!fgets(buf, (int)tam, stdin))
        return 0;
    size_t n = strcspn(buf, "\n");
    if (buf[n] != '\n' && !feof(stdin)) {        /* linha maior que o buffer */
        int c;
        while ((c = getchar()) != '\n' && c != EOF)
            ;
    }
    buf[n] = '\0';
    return 1;
}

/* Escreve s e completa com espaços até "largura" caracteres (conta UTF-8). */
static void coluna(const char *s, int largura)
{
    int chars = 0;
    for (const char *p = s; *p; p++)
        chars += ((unsigned char)*p & 0xC0) != 0x80;
    fputs(s, stdout);
    for (; chars < largura; chars++)
        putchar(' ');
}

static void mostra_menu(const Fs *fs)
{
    char prompt[CAMINHO_MAX + 64];
    shell_prompt(fs, prompt, sizeof prompt);
    printf("\n=== Mini File System (M3) ===   [%s]\n", prompt);
    int metade = (N_OPCOES + 1) / 2;
    for (int i = 0; i < metade; i++) {           /* duas colunas */
        printf("%3d. ", i + 1);
        coluna(OPCOES[i].titulo, 38);
        if (i + metade < N_OPCOES)
            printf("%3d. %s", i + metade + 1, OPCOES[i + metade].titulo);
        putchar('\n');
    }
    printf("  0. Sair\n");
}

/* Pede os dados da opção, monta argv e executa pelo shell. 0 se acabou a entrada. */
static int executa_opcao(Fs *fs, int n)
{
    const OpcaoMenu *o = &OPCOES[n - 1];
    char resp[3][LINHA_MAX];
    int nresp = 0;
    for (; nresp < 3 && o->perguntas[nresp]; nresp++)
        if (!ler(o->perguntas[nresp], resp[nresp], sizeof resp[nresp]))
            return 0;

    char *argv[8];
    int argc = 0;
    argv[argc++] = (char *)o->comando;

    if (!strcmp(o->comando, "echo")) {           /* echo -e "conteúdo" > arquivo */
        argv[argc++] = "-e";
        argv[argc++] = resp[1];
        argv[argc++] = n == 7 ? ">>" : ">";
        argv[argc++] = resp[0];
    } else if (!strcmp(o->comando, "chmod") || !strcmp(o->comando, "chown")) {
        argv[argc++] = resp[1];                  /* modo/dono vem antes do arquivo */
        argv[argc++] = resp[0];
    } else if (!strcmp(o->comando, "truncate")) {
        argv[argc++] = "-s";
        argv[argc++] = resp[1];
        argv[argc++] = resp[0];
    } else if (!strcmp(o->comando, "ls")) {
        argv[argc++] = "-l";
    } else {
        for (int i = 0; i < nresp; i++)
            argv[argc++] = resp[i];
    }
    shell_executa_argv(fs, argc, argv);
    return 1;
}

static void menu(Fs *fs)
{
    char linha[32];
    for (;;) {
        mostra_menu(fs);
        if (!ler("Escolha uma opção", linha, sizeof linha))
            break;
        int n;
        char sobra;
        if (sscanf(linha, "%d %c", &n, &sobra) != 1 || n < 0 || n > N_OPCOES) {
            puts("Opção inválida. Tente de novo.");
            continue;
        }
        if (n == 0)
            break;
        if (OPCOES[n - 1].comando == NULL) {
            if (shell_loop(fs, 1))
                break;
            continue;
        }
        if (!executa_opcao(fs, n))
            break;
    }
    puts("Saindo do Mini File System.");
}

int main(int argc, char **argv)
{
    int terminal = argc == 2 && !strcmp(argv[1], "-t");
    if (argc > 2 || (argc == 2 && !terminal)) {
        fprintf(stderr, "uso: %s [-t]\n  sem opção: menu interativo\n  -t: terminal (aceita roteiro pela entrada padrão)\n", argv[0]);
        return 2;
    }

    Fs *fs = fs_cria();
    if (!fs) {
        fprintf(stderr, "mfs: não foi possível inicializar o sistema de arquivos\n");
        return 1;
    }
    if (terminal)
        shell_loop(fs, 0);
    else
        menu(fs);
    fs_destroi(fs); /* libera árvore, inodes e tabela: valgrind deve mostrar 0 leaks */
    return 0;
}
