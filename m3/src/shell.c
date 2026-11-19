/* shell.c - Comandos do mini-sistema de arquivos (estilo Linux). */
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include "shell.h"

#define MAX_ARGS  64
#define PROF_MAX  8     /* exec dentro de exec: limite contra recursão */

typedef int (*FuncCmd)(Fs *fs, int argc, char **argv);

typedef struct {
    const char *nome;
    FuncCmd     fn;
    const char *uso;
    const char *descricao;
} Comando;

static const Comando *busca_comando(const char *nome);

/* ================= mensagens ================= */

/* "cat: a.txt: permissão negada" */
static int erro(const char *cmd, const char *alvo, FsErro e)
{
    if (alvo)
        printf("%s: %s: %s\n", cmd, alvo, fs_strerro(e));
    else
        printf("%s: %s\n", cmd, fs_strerro(e));
    return 1;
}

static int uso(const char *cmd)
{
    const Comando *c = busca_comando(cmd);
    printf("uso: %s\n", c ? c->uso : cmd);
    return 1;
}

/* printf("%-*s") conta bytes; com acentos (UTF-8) a coluna desalinha.
 * Esta versão completa com espaços pela quantidade de caracteres. */
static void imprime_coluna(const char *s, int largura)
{
    int chars = 0;
    for (const char *p = s; *p; p++)
        chars += ((unsigned char)*p & 0xC0) != 0x80;
    fputs(s, stdout);
    for (; chars < largura; chars++)
        putchar(' ');
}

static void formata_data(time_t t, char *buf, size_t tam, int com_segundos)
{
    struct tm tm;
    localtime_r(&t, &tm);
    strftime(buf, tam, com_segundos ? "%Y-%m-%d %H:%M:%S" : "%Y-%m-%d %H:%M", &tm);
}

/* Lê opções no começo (ex.: "-li" ou "-l -i"). Devolve o índice do primeiro
 * argumento que não é opção, ou -1 se aparecer uma opção não aceita. */
static int opcoes(int argc, char **argv, const char *aceitas, char *ligadas)
{
    int i = 1;
    for (; i < argc && argv[i][0] == '-' && argv[i][1] != '\0'; i++)
        for (const char *p = argv[i] + 1; *p; p++) {
            if (!strchr(aceitas, *p))
                return -1;
            ligadas[(unsigned char)*p] = 1;
        }
    return i;
}

static int numero(const char *s, size_t *v)
{
    if (!s || !*s)
        return 0;
    char *fim;
    unsigned long long x = strtoull(s, &fim, 10);
    if (*fim || s[0] == '-')
        return 0;
    *v = (size_t)x;
    return 1;
}

/* Imprime bytes com \n, \t e \xHH visíveis (usado em blocos). */
static void imprime_escapado(const char *b, size_t n)
{
    for (size_t i = 0; i < n; i++) {
        unsigned char c = (unsigned char)b[i];
        if (c == '\n')       fputs("\\n", stdout);
        else if (c == '\t')  fputs("\\t", stdout);
        else if (c == '\\')  fputs("\\\\", stdout);
        else if (c == '"')   fputs("\\\"", stdout);
        else if (c < 32 || c >= 127) printf("\\x%02x", c);
        else putchar(c);
    }
}

/* ================= navegação ================= */

static int cmd_pwd(Fs *fs, int argc, char **argv)
{
    (void)argc; (void)argv;
    char c[CAMINHO_MAX];
    fs_caminho(fs, fs->cwd, c, sizeof c);
    puts(c);
    return 0;
}

static int cmd_cd(Fs *fs, int argc, char **argv)
{
    char home[CAMINHO_MAX];
    const char *alvo;
    if (argc > 2)
        return uso("cd");
    if (argc == 2) {
        alvo = argv[1];
    } else {                                     /* cd sem argumento: vai para o home */
        if (fs->atual->uid == 0)
            snprintf(home, sizeof home, "/");
        else
            snprintf(home, sizeof home, "/home/%s", fs->atual->nome);
        alvo = home;
    }
    FsErro e = fs_cd(fs, alvo);
    return e ? erro("cd", alvo, e) : 0;
}

static int conta_subdirs(const Entrada *d)
{
    int n = 0;
    for (const Entrada *c = d->filhos; c; c = c->irmao)
        n += c->inode->tipo == T_DIRETORIO;
    return n;
}

static void linha_longa(const Entrada *e, int com_inode)
{
    const FCB *f = e->inode;
    char perm[11], data[32];
    perm_str(f, perm);
    formata_data(f->modificado, data, sizeof data, 0);
    /* Diretório: 2 links (".", e o nome no pai) + 1 por subdiretório (".." dele). */
    int links = f->tipo == T_DIRETORIO ? 2 + conta_subdirs(e) : f->n_links;
    if (com_inode)
        printf("%3d ", f->id);
    printf("%s %2d %-6s %-10s %5zu %s %s%s\n", perm, links, nome_usuario(f->dono),
           nome_grupo(f->grupo), f->tamanho, data, e->nome,
           f->tipo == T_DIRETORIO ? "/" : "");
}

/* Lista um caminho (diretório: seus itens; arquivo: ele mesmo). */
static int lista_um(Fs *fs, const char *alvo, const char *op, int cabecalho)
{
    Entrada *d;
    FsErro e = fs_lista(fs, alvo, &d);
    if (e)
        return erro("ls", alvo, e);

    if (d->inode->tipo != T_DIRETORIO) {         /* ls de um arquivo */
        if (op['l']) {
            linha_longa(d, op['i']);
        } else {
            if (op['i'])
                printf("%d ", d->inode->id);
            puts(d->nome);
        }
        return 0;
    }
    if (cabecalho)
        printf("%s:\n", alvo);
    for (Entrada *c = d->filhos; c; c = c->irmao) {
        if (op['l']) {
            linha_longa(c, op['i']);
            continue;
        }
        if (c != d->filhos)
            fputs("  ", stdout);
        if (op['i'])
            printf("%d ", c->inode->id);
        printf("%s%s", c->nome, c->inode->tipo == T_DIRETORIO ? "/" : "");
    }
    if (!op['l'] && d->filhos)
        putchar('\n');
    return 0;
}

static int cmd_ls(Fs *fs, int argc, char **argv)
{
    char op[256] = {0};
    int i = opcoes(argc, argv, "li", op);
    if (i < 0)
        return uso("ls");
    if (i == argc)
        return lista_um(fs, ".", op, 0);
    int st = 0, varios = argc - i > 1;
    for (int k = i; k < argc; k++) {
        Entrada *x;
        int eh_dir = fs_resolve(fs, argv[k], &x) == FS_OK && x->inode->tipo == T_DIRETORIO;
        if (varios && eh_dir && k > i)
            putchar('\n');
        st |= lista_um(fs, argv[k], op, varios && eh_dir);
    }
    return st;
}

static void desenha_arvore(const Fs *fs, const Entrada *d, const char *prefixo,
                           int *ndir, int *narq)
{
    if (!perm_checa(fs->atual, d->inode, P_R | P_X)) {
        printf("%s└── [permissão negada]\n", prefixo);
        return;
    }
    for (const Entrada *c = d->filhos; c; c = c->irmao) {
        int ultimo = c->irmao == NULL;
        int dir = c->inode->tipo == T_DIRETORIO;
        printf("%s%s %s%s\n", prefixo, ultimo ? "└──" : "├──", c->nome, dir ? "/" : "");
        if (dir) {
            (*ndir)++;
            char novo[512];
            snprintf(novo, sizeof novo, "%s%s", prefixo, ultimo ? "    " : "│   ");
            desenha_arvore(fs, c, novo, ndir, narq);
        } else {
            (*narq)++;
        }
    }
}

static int cmd_tree(Fs *fs, int argc, char **argv)
{
    if (argc > 2)
        return uso("tree");
    const char *alvo = argc == 2 ? argv[1] : ".";
    Entrada *d;
    FsErro e = fs_resolve(fs, alvo, &d);
    if (e)
        return erro("tree", alvo, e);
    if (d->inode->tipo != T_DIRETORIO)
        return erro("tree", alvo, FS_E_NAO_DIRETORIO);
    int ndir = 0, narq = 0;
    puts(alvo);
    desenha_arvore(fs, d, "", &ndir, &narq);
    printf("\n%d diretórios, %d arquivos\n", ndir, narq);
    return 0;
}

/* ================= diretórios ================= */

static int cmd_mkdir(Fs *fs, int argc, char **argv)
{
    char op[256] = {0};
    int i = opcoes(argc, argv, "p", op);
    if (i < 0 || i >= argc)
        return uso("mkdir");
    int st = 0;
    for (; i < argc; i++) {
        FsErro e = fs_mkdir(fs, argv[i], op['p']);
        if (e)
            st = erro("mkdir", argv[i], e);
    }
    return st;
}

static int cmd_rmdir(Fs *fs, int argc, char **argv)
{
    if (argc < 2)
        return uso("rmdir");
    int st = 0;
    for (int i = 1; i < argc; i++) {
        FsErro e = fs_rmdir(fs, argv[i]);
        if (e)
            st = erro("rmdir", argv[i], e);
    }
    return st;
}

/* ================= arquivos ================= */

static int cmd_touch(Fs *fs, int argc, char **argv)
{
    if (argc < 2)
        return uso("touch");
    int st = 0;
    for (int i = 1; i < argc; i++) {
        FsErro e = fs_touch(fs, argv[i]);
        if (e)
            st = erro("touch", argv[i], e);
    }
    return st;
}

static int hex(char c)
{
    if (c >= '0' && c <= '9') return c - '0';
    c = (char)tolower((unsigned char)c);
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    return -1;
}

/* Interpreta \n, \t, \\, \0 e \xHH (echo -e). Devolve o novo tamanho. */
static size_t interpreta_escapes(char *s, size_t n)
{
    size_t r = 0, w = 0;
    while (r < n) {
        if (s[r] != '\\' || r + 1 >= n) { s[w++] = s[r++]; continue; }
        char c = s[r + 1];
        r += 2;
        if (c == 'n')       s[w++] = '\n';
        else if (c == 't')  s[w++] = '\t';
        else if (c == '\\') s[w++] = '\\';
        else if (c == '0')  s[w++] = '\0';
        else if (c == 'x' && r < n && hex(s[r]) >= 0) {
            int v = hex(s[r++]);
            if (r < n && hex(s[r]) >= 0)
                v = v * 16 + hex(s[r++]);
            s[w++] = (char)v;
        } else { s[w++] = '\\'; s[w++] = c; }
    }
    return w;
}

static int cmd_echo(Fs *fs, int argc, char **argv)
{
    char op[256] = {0};
    int i = opcoes(argc, argv, "en", op);
    if (i < 0)
        i = 1;                                   /* "-z" vira texto, como no bash */

    /* Procura o redirecionamento "> arq" ou ">> arq". */
    int fim = argc, anexar = 0;
    const char *arquivo = NULL;
    for (int k = i; k < argc; k++)
        if (!strcmp(argv[k], ">") || !strcmp(argv[k], ">>")) {
            if (k + 2 != argc)
                return uso("echo");
            anexar = argv[k][1] == '>';
            arquivo = argv[k + 1];
            fim = k;
            break;
        }

    char texto[LINHA_MAX + 2];
    size_t n = 0;
    for (int k = i; k < fim; k++) {
        size_t l = strlen(argv[k]);
        if (n + l + 2 > sizeof texto)
            return erro("echo", NULL, FS_E_ARGUMENTOS);
        if (k > i)
            texto[n++] = ' ';
        memcpy(texto + n, argv[k], l);
        n += l;
    }
    if (op['e'])
        n = interpreta_escapes(texto, n);
    if (!op['n'])
        texto[n++] = '\n';                       /* echo termina com quebra de linha */

    if (!arquivo) {
        fwrite(texto, 1, n, stdout);
        return 0;
    }
    FsErro e = fs_escreve(fs, arquivo, texto, n, anexar);
    return e ? erro("echo", arquivo, e) : 0;
}

static void imprime_conteudo(const char *b, size_t n)
{
    fwrite(b, 1, n, stdout);
    if (n && b[n - 1] != '\n')
        putchar('\n');                           /* não deixa o prompt colado */
}

static int cmd_cat(Fs *fs, int argc, char **argv)
{
    if (argc < 2)
        return uso("cat");
    int st = 0;
    for (int i = 1; i < argc; i++) {
        char *buf;
        size_t n;
        FsErro e = fs_le(fs, argv[i], 0, TAM_MAX_ARQ, &buf, &n);
        if (e) {
            st = erro("cat", argv[i], e);
            continue;
        }
        imprime_conteudo(buf, n);
        free(buf);
    }
    return st;
}

/* read arq offset n: reposiciona (seek) e lê n bytes. */
static int cmd_read(Fs *fs, int argc, char **argv)
{
    size_t off, n;
    if (argc != 4 || !numero(argv[2], &off) || !numero(argv[3], &n))
        return uso("read");
    char *buf;
    size_t lidos;
    FsErro e = fs_le(fs, argv[1], off, n, &buf, &lidos);
    if (e)
        return erro("read", argv[1], e);
    imprime_conteudo(buf, lidos);
    free(buf);
    return 0;
}

static int cmd_truncate(Fs *fs, int argc, char **argv)
{
    size_t tam;
    if (argc != 4 || strcmp(argv[1], "-s") || !numero(argv[2], &tam))
        return uso("truncate");
    FsErro e = fs_trunca(fs, argv[3], tam);
    return e ? erro("truncate", argv[3], e) : 0;
}

static int cmd_cp(Fs *fs, int argc, char **argv)
{
    if (argc != 3)
        return uso("cp");
    FsErro e = fs_cp(fs, argv[1], argv[2]);
    return e ? erro("cp", argv[1], e) : 0;
}

static int cmd_mv(Fs *fs, int argc, char **argv)
{
    if (argc != 3)
        return uso("mv");
    FsErro e = fs_mv(fs, argv[1], argv[2]);
    return e ? erro("mv", argv[1], e) : 0;
}

static int cmd_ln(Fs *fs, int argc, char **argv)
{
    if (argc != 3)
        return uso("ln");
    FsErro e = fs_ln(fs, argv[1], argv[2]);
    return e ? erro("ln", e == FS_E_JA_EXISTE ? argv[2] : argv[1], e) : 0;
}

static int cmd_rm(Fs *fs, int argc, char **argv)
{
    char op[256] = {0};
    int i = opcoes(argc, argv, "rRf", op);
    if (i < 0 || i >= argc)
        return uso("rm");
    int st = 0;
    for (; i < argc; i++) {
        FsErro e = fs_rm(fs, argv[i], op['r'] || op['R']);
        if (e == FS_E_NAO_EXISTE && op['f'])
            continue;                            /* -f ignora o que não existe */
        if (e)
            st = erro("rm", argv[i], e);
    }
    return st;
}

/* ================= metadados e permissões ================= */

static int cmd_stat(Fs *fs, int argc, char **argv)
{
    if (argc != 2)
        return uso("stat");
    Entrada *a;
    FsErro e = fs_resolve(fs, argv[1], &a);
    if (e)
        return erro("stat", argv[1], e);

    const FCB *f = a->inode;
    char perm[11], cr[32], md[32], ac[32];
    perm_str(f, perm);
    formata_data(f->criado, cr, sizeof cr, 1);
    formata_data(f->modificado, md, sizeof md, 1);
    formata_data(f->acessado, ac, sizeof ac, 1);
    int links = f->tipo == T_DIRETORIO ? 2 + conta_subdirs(a) : f->n_links;

    printf("  Arquivo: %s\n", a->nome);
    printf("  Inode: %-6d Links: %-4d Tipo: %s\n", f->id, links, tipo_nome(f->tipo));
    printf("  Tamanho: %-5zu Blocos: %-3d (bloco = %d bytes)\n", f->tamanho, f->n_blocos, TAM_BLOCO);
    printf("  Acesso: (%04o/%s)  Uid: (%d/%s)  Gid: (%d/%s)\n", f->perm, perm, f->dono,
           nome_usuario(f->dono), f->grupo, nome_grupo(f->grupo));
    printf("  Criado:     %s\n  Modificado: %s\n  Acessado:   %s\n", cr, md, ac);
    if (f->tipo != T_DIRETORIO) {
        printf("  Diretos: [");
        for (int i = 0; i < N_DIRETOS; i++)
            printf(i ? " %d" : "%d", f->diretos[i]);
        printf("]  Indireto: %d\n", f->indireto);
    }
    return 0;
}

static int cmd_chmod(Fs *fs, int argc, char **argv)
{
    if (argc < 3)
        return uso("chmod");
    int st = 0;
    for (int i = 2; i < argc; i++) {
        FsErro e = fs_chmod(fs, argv[1], argv[i]);
        if (e == FS_E_ARGUMENTOS)
            return erro("chmod", argv[1], e);
        if (e)
            st = erro("chmod", argv[i], e);
    }
    return st;
}

static int cmd_chown(Fs *fs, int argc, char **argv)
{
    if (argc < 3)
        return uso("chown");
    int st = 0;
    for (int i = 2; i < argc; i++) {
        FsErro e = fs_chown(fs, argv[1], argv[i]);
        if (e == FS_E_USUARIO)
            return erro("chown", argv[1], e);
        if (e)
            st = erro("chown", argv[i], e);
    }
    return st;
}

static int cmd_umask(Fs *fs, int argc, char **argv)
{
    if (argc == 1) {
        printf("%04o\n", fs->umask);
        return 0;
    }
    uint16_t m;
    if (argc != 2 || strspn(argv[1], "01234567") != strlen(argv[1])
        || perm_parse(argv[1], 0, &m) != FS_OK)
        return uso("umask");
    fs->umask = m;
    return 0;
}

/* ================= usuários ================= */

static int cmd_su(Fs *fs, int argc, char **argv)
{
    if (argc > 2)
        return uso("su");
    const char *u = argc == 2 ? argv[1] : "root";
    FsErro e = fs_su(fs, u);
    return e ? erro("su", u, e) : 0;
}

static int cmd_whoami(Fs *fs, int argc, char **argv)
{
    (void)argc; (void)argv;
    puts(fs->atual->nome);
    return 0;
}

static int cmd_id(Fs *fs, int argc, char **argv)
{
    (void)argc; (void)argv;
    printf("uid=%d(%s) gid=%d(%s)\n", fs->atual->uid, fs->atual->nome, fs->atual->gid,
           nome_grupo(fs->atual->gid));
    return 0;
}

/* ================= execução ================= */

static int executa_programa(Fs *fs, const char *caminho, const char *cmd)
{
    if (fs->prof_exec >= PROF_MAX) {
        printf("%s: %s: limite de %d programas aninhados atingido\n", cmd, caminho, PROF_MAX);
        return 1;
    }
    char *codigo;
    size_t n;
    FsErro e = fs_exec(fs, caminho, &codigo, &n);
    if (e)
        return erro(cmd, caminho, e);

    /* "Interpreta" o programa: cada linha é um comando deste shell,
     * executado com as permissões de quem chamou. */
    fs->prof_exec++;
    int st = 0;
    char *ctx = NULL;
    for (char *l = strtok_r(codigo, "\n", &ctx); l; l = strtok_r(NULL, "\n", &ctx)) {
        int r = shell_executa_linha(fs, l);
        if (r == SHELL_SAIR)
            break;
        st |= r;
    }
    fs->prof_exec--;
    free(codigo);
    return st;
}

static int cmd_exec(Fs *fs, int argc, char **argv)
{
    if (argc != 2)
        return uso("exec");
    return executa_programa(fs, argv[1], "exec");
}

/* ================= disco e blocos ================= */

static int cmd_blocos(Fs *fs, int argc, char **argv)
{
    if (argc != 2)
        return uso("blocos");
    Entrada *a;
    FsErro e = fs_resolve(fs, argv[1], &a);
    if (e)
        return erro("blocos", argv[1], e);
    const FCB *f = a->inode;
    if (f->tipo == T_DIRETORIO)
        return erro("blocos", argv[1], FS_E_EH_DIRETORIO);

    printf("%s: inode %d, %zu bytes em %d bloco(s) de %d bytes (alocação indexada)\n",
           a->nome, f->id, f->tamanho, f->n_blocos, TAM_BLOCO);
    if (f->n_blocos == 0)
        return 0;
    printf("  lógico  físico  bytes  conteúdo\n");
    for (int i = 0; i < f->n_blocos; i++) {
        int b = inode_bloco(&fs->disco, f, i);
        size_t usados = f->tamanho - (size_t)i * TAM_BLOCO;
        if (usados > TAM_BLOCO)
            usados = TAM_BLOCO;
        char rotulo[16];
        if (i < N_DIRETOS)
            snprintf(rotulo, sizeof rotulo, "d[%d]", i);
        else
            snprintf(rotulo, sizeof rotulo, "ind[%d]", i - N_DIRETOS);
        printf("  %-7s %6d  %5zu  ", rotulo, b, usados);
        size_t z = 0;
        while (z < usados && fs->disco.dados[b][z] == '\0')
            z++;
        if (z == usados) {                       /* bloco só com zeros (truncate) */
            printf("(%zu bytes zerados)\n", usados);
            continue;
        }
        putchar('"');
        imprime_escapado(fs->disco.dados[b], usados);
        printf("\"\n");
    }
    if (f->indireto >= 0) {
        printf("  bloco de índices (indireto) = %d -> [", f->indireto);
        for (int i = N_DIRETOS; i < f->n_blocos; i++)
            printf(i > N_DIRETOS ? " %d" : "%d", inode_bloco(&fs->disco, f, i));
        printf("]\n");
    }
    return 0;
}

static int cmd_df(Fs *fs, int argc, char **argv)
{
    (void)argc; (void)argv;
    const Disco *d = &fs->disco;
    int usados = N_BLOCOS - d->n_livres;

    char mapa[N_BLOCOS];
    for (int b = 0; b < N_BLOCOS; b++)
        mapa[b] = d->livre[b] ? '.' : '#';
    for (int i = 0; i < fs->inodes.cap; i++)          /* destaca os blocos de índice */
        if (fs->inodes.v[i] && fs->inodes.v[i]->indireto >= 0)
            mapa[fs->inodes.v[i]->indireto] = 'I';

    printf("Disco simulado: %d blocos de %d bytes (%d bytes)\n", N_BLOCOS, TAM_BLOCO,
           N_BLOCOS * TAM_BLOCO);
    printf("Usados: %d  Livres: %d  Uso: %d%%  Inodes em uso: %d\n", usados, d->n_livres,
           usados * 100 / N_BLOCOS, fs->inodes.usados);
    printf("Mapa de bits (. livre  # dados  I índice):\n");
    for (int b = 0; b < N_BLOCOS; b += 64)
        printf("  %03d %.64s\n", b, mapa + b);
    return 0;
}

static int cmd_inodes(Fs *fs, int argc, char **argv)
{
    (void)argc; (void)argv;
    printf("  id  tipo        links  tamanho  blocos  dono\n");
    for (int i = 0; i < fs->inodes.cap; i++) {
        const FCB *f = fs->inodes.v[i];
        if (!f)
            continue;
        printf("  %-3d ", f->id);
        imprime_coluna(tipo_nome(f->tipo), 11);
        printf(" %5d  %7zu  %6d  %s\n", f->n_links, f->tamanho, f->n_blocos, nome_usuario(f->dono));
    }
    return 0;
}

static int cmd_help(Fs *fs, int argc, char **argv);

/* ================= tabela de comandos ================= */

static const Comando COMANDOS[] = {
    { "pwd",      cmd_pwd,      "pwd",                       "mostra o diretório atual" },
    { "cd",       cmd_cd,       "cd [dir]",                  "muda de diretório (sem argumento: home)" },
    { "ls",       cmd_ls,       "ls [-l] [-i] [caminho]",    "lista; -l detalhado, -i número do inode" },
    { "tree",     cmd_tree,     "tree [dir]",                "desenha a árvore de diretórios" },
    { "mkdir",    cmd_mkdir,    "mkdir [-p] dir...",         "cria diretório(s)" },
    { "rmdir",    cmd_rmdir,    "rmdir dir...",              "remove diretório vazio" },
    { "touch",    cmd_touch,    "touch arq...",              "cria arquivo vazio ou atualiza as datas" },
    { "echo",     cmd_echo,     "echo [-e] [-n] texto [> arq | >> arq]", "escreve (>) ou anexa (>>) texto" },
    { "cat",      cmd_cat,      "cat arq...",                "mostra o conteúdo" },
    { "read",     cmd_read,     "read arq offset n",         "reposiciona em offset e lê n bytes" },
    { "truncate", cmd_truncate, "truncate -s tamanho arq",   "trunca ou estende o arquivo" },
    { "cp",       cmd_cp,       "cp origem destino",         "copia (novo inode e novos blocos)" },
    { "mv",       cmd_mv,       "mv origem destino",         "move/renomeia (mesmo inode)" },
    { "ln",       cmd_ln,       "ln origem novo_nome",       "cria hard link (mesmo inode)" },
    { "rm",       cmd_rm,       "rm [-r] [-f] caminho...",   "remove arquivo (-r: diretório)" },
    { "stat",     cmd_stat,     "stat caminho",              "mostra o FCB (inode) completo" },
    { "chmod",    cmd_chmod,    "chmod modo arq...",         "permissões: octal (640) ou simbólico (u+x,go-w)" },
    { "chown",    cmd_chown,    "chown usuario[:grupo] arq...", "troca dono/grupo (só root)" },
    { "umask",    cmd_umask,    "umask [octal]",             "mostra/define a máscara de criação" },
    { "exec",     cmd_exec,     "exec programa",             "executa um programa (#!); também ./programa" },
    { "su",       cmd_su,       "su [usuario]",              "troca o usuário atual (root, alice, bob, carol)" },
    { "whoami",   cmd_whoami,   "whoami",                    "mostra o usuário atual" },
    { "id",       cmd_id,       "id",                        "mostra uid e gid do usuário atual" },
    { "blocos",   cmd_blocos,   "blocos arq",                "mostra os blocos do arquivo no disco" },
    { "df",       cmd_df,       "df",                        "uso do disco e mapa de bits" },
    { "inodes",   cmd_inodes,   "inodes",                    "mostra a tabela de inodes" },
    { "help",     cmd_help,     "help",                      "esta ajuda" },
};

#define N_COMANDOS ((int)(sizeof COMANDOS / sizeof COMANDOS[0]))

static const Comando *busca_comando(const char *nome)
{
    for (int i = 0; i < N_COMANDOS; i++)
        if (strcmp(COMANDOS[i].nome, nome) == 0)
            return &COMANDOS[i];
    return NULL;
}

static int cmd_help(Fs *fs, int argc, char **argv)
{
    (void)fs; (void)argc; (void)argv;
    for (int i = 0; i < N_COMANDOS; i++)
        printf("  %-40s %s\n", COMANDOS[i].uso, COMANDOS[i].descricao);
    printf("  %-40s %s\n", "exit", "sai");
    return 0;
}

/* ================= interpretação da linha ================= */

/* Separa a linha em argumentos, respeitando aspas, e destaca > e >>.
 * Devolve argc, -1 se a linha for grande demais ou -2 se faltar fechar aspas. */
static int tokeniza(const char *lin, char *buf, size_t tam, char **argv, int max)
{
    size_t w = 0;
    int argc = 0;
    const char *r = lin;
    for (;;) {
        while (*r && isspace((unsigned char)*r))
            r++;
        if (!*r)
            break;
        if (argc >= max || w + 3 >= tam)
            return -1;
        argv[argc++] = buf + w;
        if (*r == '>') {
            buf[w++] = *r++;
            if (*r == '>')
                buf[w++] = *r++;
            buf[w++] = '\0';
            continue;
        }
        while (*r && !isspace((unsigned char)*r) && *r != '>') {
            if (*r == '"' || *r == '\'') {
                char aspa = *r++;
                while (*r && *r != aspa) {
                    if (w + 2 >= tam)
                        return -1;
                    buf[w++] = *r++;
                }
                if (!*r)
                    return -2;
                r++;
            } else {
                if (w + 2 >= tam)
                    return -1;
                buf[w++] = *r++;
            }
        }
        buf[w++] = '\0';
    }
    return argc;
}

int shell_executa_argv(Fs *fs, int argc, char **argv)
{
    if (argc == 0)
        return 0;
    if (!strcmp(argv[0], "exit") || !strcmp(argv[0], "sair"))
        return SHELL_SAIR;

    if (strcmp(argv[0], "echo"))
        for (int i = 1; i < argc; i++)
            if (!strcmp(argv[i], ">") || !strcmp(argv[i], ">>")) {
                printf("%s: redirecionamento só é suportado no echo\n", argv[0]);
                return 1;
            }

    if (strchr(argv[0], '/'))                    /* ./programa */
        return argc == 1 ? executa_programa(fs, argv[0], "exec") : uso("exec");

    const Comando *c = busca_comando(argv[0]);
    if (!c) {
        printf("%s: comando não encontrado (digite help)\n", argv[0]);
        return 1;
    }
    return c->fn(fs, argc, argv);
}

int shell_executa_linha(Fs *fs, const char *linha)
{
    while (isspace((unsigned char)*linha))
        linha++;
    if (*linha == '\0' || *linha == '#')         /* linha vazia ou comentário */
        return 0;

    char buf[2 * LINHA_MAX];
    char *argv[MAX_ARGS];
    int argc = tokeniza(linha, buf, sizeof buf, argv, MAX_ARGS);
    if (argc == -2) {
        puts("erro: aspas não fechadas");
        return 1;
    }
    if (argc < 0) {
        puts("erro: linha longa demais");
        return 1;
    }
    return shell_executa_argv(fs, argc, argv);
}

void shell_prompt(const Fs *fs, char *buf, size_t tam)
{
    char c[CAMINHO_MAX];
    fs_caminho(fs, fs->cwd, c, sizeof c);
    snprintf(buf, tam, "%s@mfs:%s%c ", fs->atual->nome, c, fs->atual->uid == 0 ? '#' : '$');
}

int shell_loop(Fs *fs, int dentro_do_menu)
{
    /* Com a entrada vindo de um arquivo (ex.: demo.txt), repete o
     * comando depois do prompt para a saída ficar legível. */
    int eco = !isatty(STDIN_FILENO);
    char linha[LINHA_MAX], prompt[CAMINHO_MAX + 64];

    if (dentro_do_menu)
        puts("Terminal do MFS. 'help' lista os comandos, 'menu' volta ao menu, 'exit' sai.");
    for (;;) {
        shell_prompt(fs, prompt, sizeof prompt);
        fputs(prompt, stdout);
        if (!fgets(linha, sizeof linha, stdin)) {
            putchar('\n');
            return 1;                            /* fim da entrada */
        }
        size_t n = strlen(linha);
        if (n && linha[n - 1] != '\n' && !feof(stdin)) {
            int c;
            while ((c = getchar()) != '\n' && c != EOF)
                ;                                /* descarta o resto */
            puts("erro: linha longa demais");
            continue;
        }
        if (eco)
            printf("%s%s", linha, (n && linha[n - 1] == '\n') ? "" : "\n");

        char *p = linha;
        while (isspace((unsigned char)*p))
            p++;
        if (dentro_do_menu && !strncmp(p, "menu", 4) && (p[4] == '\0' || isspace((unsigned char)p[4])))
            return 0;
        if (shell_executa_linha(fs, linha) == SHELL_SAIR)
            return 1;
    }
}
