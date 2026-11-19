/* perm.c - Usuários, grupos e verificação de permissões com bitmask. */
#include <string.h>
#include "perm.h"

static const Grupo GRUPOS[] = {
    {   0, "root" },
    { 100, "dev" },
    { 200, "visitantes" },
};

static const Usuario USUARIOS[] = {
    {    0, "root",  0   },
    { 1000, "alice", 100 },   /* alice e bob estão no mesmo grupo */
    { 1001, "bob",   100 },
    { 1002, "carol", 200 },   /* carol cai em "outros" para os arquivos do grupo dev */
};

#define TAM(v) ((int)(sizeof(v) / sizeof((v)[0])))

const Usuario *usuario_por_nome(const char *nome)
{
    for (int i = 0; i < TAM(USUARIOS); i++)
        if (strcmp(USUARIOS[i].nome, nome) == 0)
            return &USUARIOS[i];
    return NULL;
}

const Usuario *usuario_por_uid(int uid)
{
    for (int i = 0; i < TAM(USUARIOS); i++)
        if (USUARIOS[i].uid == uid)
            return &USUARIOS[i];
    return NULL;
}

const Grupo *grupo_por_nome(const char *nome)
{
    for (int i = 0; i < TAM(GRUPOS); i++)
        if (strcmp(GRUPOS[i].nome, nome) == 0)
            return &GRUPOS[i];
    return NULL;
}

const char *nome_usuario(int uid)
{
    const Usuario *u = usuario_por_uid(uid);
    return u ? u->nome : "?";
}

const char *nome_grupo(int gid)
{
    for (int i = 0; i < TAM(GRUPOS); i++)
        if (GRUPOS[i].gid == gid)
            return GRUPOS[i].nome;
    return "?";
}

bool perm_checa(const Usuario *u, const FCB *f, int op)
{
    if (u->uid == 0) {
        /* root ignora as permissões, com uma exceção do Linux: só executa
         * um arquivo se pelo menos uma classe tiver o bit x. */
        if ((op & P_X) && f->tipo != T_DIRETORIO)
            return (f->perm & 0111) != 0;
        return true;
    }

    int desloc;
    if (u->uid == f->dono)
        desloc = 6;          /* bits 8..6: dono */
    else if (u->gid == f->grupo)
        desloc = 3;          /* bits 5..3: grupo */
    else
        desloc = 0;          /* bits 2..0: outros */

    /* Só a classe escolhida conta: se o dono tem ---, ele é negado mesmo
     * que "outros" tenha rwx (igual ao Linux). */
    return ((f->perm >> desloc) & op) == op;
}

void perm_str(const FCB *f, char out[11])
{
    const char *letras = "rwxrwxrwx";
    out[0] = (f->tipo == T_DIRETORIO) ? 'd' : '-';
    for (int i = 0; i < 9; i++)
        out[1 + i] = (f->perm & (0400 >> i)) ? letras[i] : '-';
    out[10] = '\0';
}

/* Modo octal: 1 a 4 dígitos de 0 a 7. */
static bool eh_octal(const char *s)
{
    size_t n = strlen(s);
    if (n == 0 || n > 4)
        return false;
    for (size_t i = 0; i < n; i++)
        if (s[i] < '0' || s[i] > '7')
            return false;
    return true;
}

FsErro perm_parse(const char *modo, uint16_t atual, uint16_t *resultado)
{
    if (eh_octal(modo)) {
        unsigned v = 0;
        for (const char *p = modo; *p; p++)
            v = (v << 3) | (unsigned)(*p - '0');   /* cada dígito octal = 3 bits */
        if (v > 0777)
            return FS_E_ARGUMENTOS;                /* setuid/setgid/sticky não simulados */
        *resultado = (uint16_t)v;
        return FS_OK;
    }

    /* Modo simbólico: cláusulas [ugoa]*[+-=][rwx]* separadas por vírgula. */
    uint16_t perm = atual & 0777;
    const char *p = modo;
    while (*p) {
        uint16_t quem = 0;
        for (; *p && strchr("ugoa", *p); p++) {
            if (*p == 'u') quem |= 0700;
            if (*p == 'g') quem |= 0070;
            if (*p == 'o') quem |= 0007;
            if (*p == 'a') quem |= 0777;
        }
        if (quem == 0)
            quem = 0777;                            /* "+x" vale para todos */

        char op = *p;
        if (op != '+' && op != '-' && op != '=')
            return FS_E_ARGUMENTOS;
        p++;

        uint16_t bits = 0;
        for (; *p && strchr("rwx", *p); p++) {
            if (*p == 'r') bits |= 0444;
            if (*p == 'w') bits |= 0222;
            if (*p == 'x') bits |= 0111;
        }
        bits &= quem;                               /* só as classes pedidas */

        if (op == '+') perm |= bits;
        if (op == '-') perm &= (uint16_t)~bits;
        if (op == '=') perm = (uint16_t)((perm & ~quem) | bits);

        if (*p == ',')
            p++;
        else if (*p)
            return FS_E_ARGUMENTOS;
    }
    *resultado = perm;
    return FS_OK;
}
