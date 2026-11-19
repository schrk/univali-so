/*
 * inode.c - Tabela de inodes e alocação indexada de blocos.
 *
 * Mapeamento lógico -> físico: o byte p de um arquivo está no bloco lógico
 * p / TAM_BLOCO, na posição p % TAM_BLOCO. O bloco lógico i vira bloco
 * físico por:
 *     i <  N_DIRETOS : f->diretos[i]
 *     i >= N_DIRETOS : (i - N_DIRETOS)-ésimo número guardado no bloco f->indireto
 */
#include <ctype.h>
#include <stdlib.h>
#include <string.h>
#include "inode.h"

time_t agora(void)
{
    static int fixa = -1;
    if (fixa < 0)
        fixa = getenv("MFS_DATA_FIXA") != NULL;
    return fixa ? (time_t)1763560800 /* 2025-11-19 14:00 UTC */ : time(NULL);
}

const char *tipo_nome(TipoArquivo t)
{
    switch (t) {
    case T_DIRETORIO: return "diretório";
    case T_TEXTO:     return "texto";
    case T_NUMERICO:  return "numérico";
    case T_BINARIO:   return "binário";
    case T_PROGRAMA:  return "programa";
    }
    return "?";
}

/* ---------------- tabela de inodes ---------------- */

FsErro inodes_init(TabelaInodes *t)
{
    t->cap = 16;
    t->usados = 0;
    t->v = calloc((size_t)t->cap, sizeof *t->v);
    return t->v ? FS_OK : FS_E_SEM_MEMORIA;
}

FCB *inode_novo(TabelaInodes *t, TipoArquivo tipo, int dono, int grupo, uint16_t perm)
{
    int id = -1;
    for (int i = 1; i < t->cap; i++)           /* reaproveita o menor número livre */
        if (t->v[i] == NULL) { id = i; break; }

    if (id < 0) {                               /* tabela cheia: dobra de tamanho */
        int nova_cap = t->cap * 2;
        FCB **nv = realloc(t->v, (size_t)nova_cap * sizeof *nv);
        if (!nv)
            return NULL;
        memset(nv + t->cap, 0, (size_t)(nova_cap - t->cap) * sizeof *nv);
        id = t->cap;
        t->v = nv;
        t->cap = nova_cap;
    }

    FCB *f = calloc(1, sizeof *f);
    if (!f)
        return NULL;
    f->id = id;
    f->tipo = tipo;
    f->dono = dono;
    f->grupo = grupo;
    f->perm = perm & 0777;
    f->criado = f->modificado = f->acessado = agora();
    f->indireto = -1;
    for (int i = 0; i < N_DIRETOS; i++)
        f->diretos[i] = -1;

    t->v[id] = f;
    t->usados++;
    return f;
}

/* ---------------- índices de blocos ---------------- */

/* Os números dentro do bloco indireto são lidos/gravados com memcpy para
 * não violar alinhamento nem aliasing (o disco é um vetor de char). */
static int ler_idx(const Disco *d, const FCB *f, int i)
{
    if (i < N_DIRETOS)
        return f->diretos[i];
    int32_t v;
    memcpy(&v, d->dados[f->indireto] + (size_t)(i - N_DIRETOS) * sizeof v, sizeof v);
    return v;
}

static void grava_idx(Disco *d, FCB *f, int i, int bloco)
{
    if (i < N_DIRETOS) {
        f->diretos[i] = bloco;
        return;
    }
    int32_t v = bloco;
    memcpy(d->dados[f->indireto] + (size_t)(i - N_DIRETOS) * sizeof v, &v, sizeof v);
}

int inode_bloco(const Disco *d, const FCB *f, int i)
{
    if (i < 0 || i >= f->n_blocos)
        return -1;
    return ler_idx(d, f, i);
}

static int blocos_para(size_t tamanho)
{
    return (int)((tamanho + TAM_BLOCO - 1) / TAM_BLOCO);
}

/* Blocos de disco gastos por um arquivo com n blocos de dados
 * (conta o bloco de índices quando ele é necessário). */
static int custo_total(int n)
{
    return n + (n > N_DIRETOS ? 1 : 0);
}

/* Ajusta o arquivo para ter exatamente novo_n blocos de dados.
 * Primeiro verifica se cabe; só depois mexe em algo (tudo ou nada). */
static FsErro redimensiona(Disco *d, FCB *f, int novo_n)
{
    if (novo_n > MAX_BLOCOS_ARQ)
        return FS_E_ARQ_GRANDE;
    if (custo_total(novo_n) - custo_total(f->n_blocos) > d->n_livres)
        return FS_E_SEM_ESPACO;

    if (novo_n > f->n_blocos) {
        if (novo_n > N_DIRETOS && f->indireto < 0) {
            f->indireto = disco_aloca(d);
            for (int i = 0; i < PTRS_INDIRETO; i++)
                grava_idx(d, f, N_DIRETOS + i, -1);
        }
        for (int i = f->n_blocos; i < novo_n; i++)
            grava_idx(d, f, i, disco_aloca(d));
    } else {
        for (int i = f->n_blocos - 1; i >= novo_n; i--) {
            disco_libera(d, ler_idx(d, f, i));
            grava_idx(d, f, i, -1);
        }
        if (novo_n <= N_DIRETOS && f->indireto >= 0) {
            disco_libera(d, f->indireto);
            f->indireto = -1;
        }
    }
    f->n_blocos = novo_n;
    return FS_OK;
}

void inode_libera(TabelaInodes *t, Disco *d, FCB *f)
{
    redimensiona(d, f, 0); /* diminuir sempre funciona */
    t->v[f->id] = NULL;
    t->usados--;
    free(f);
}

void inodes_destroi(TabelaInodes *t, Disco *d)
{
    for (int i = 0; i < t->cap; i++)
        if (t->v[i])
            inode_libera(t, d, t->v[i]);
    free(t->v);
    t->v = NULL;
    t->cap = 0;
}

/* ---------------- conteúdo ---------------- */

/* Copia len bytes de src para o arquivo a partir de off, bloco a bloco.
 * src == NULL grava zeros. Os blocos já precisam estar alocados. */
static void copia_para_blocos(Disco *d, FCB *f, size_t off, const char *src, size_t len)
{
    size_t pos = off, fim = off + len;
    while (pos < fim) {
        int    logico = (int)(pos / TAM_BLOCO);
        size_t desl   = pos % TAM_BLOCO;
        size_t n      = TAM_BLOCO - desl;
        if (n > fim - pos)
            n = fim - pos;
        char *dst = d->dados[ler_idx(d, f, logico)] + desl;
        if (src)
            memcpy(dst, src + (pos - off), n);
        else
            memset(dst, 0, n);
        pos += n;
    }
}

static FsErro escreve_em(Disco *d, FCB *f, size_t off, const char *src, size_t len)
{
    size_t fim = off + len;
    if (fim > TAM_MAX_ARQ)
        return FS_E_ARQ_GRANDE;
    if (fim > f->tamanho) {
        FsErro e = redimensiona(d, f, blocos_para(fim));
        if (e)
            return e;
        if (off > f->tamanho) /* buraco entre o fim antigo e off: zera */
            copia_para_blocos(d, f, f->tamanho, NULL, off - f->tamanho);
    }
    copia_para_blocos(d, f, off, src, len);
    if (fim > f->tamanho)
        f->tamanho = fim;
    f->modificado = agora();
    return FS_OK;
}

size_t inode_le(const Disco *d, const FCB *f, size_t offset, size_t n, char *saida)
{
    if (offset >= f->tamanho)
        return 0;
    if (n > f->tamanho - offset)
        n = f->tamanho - offset;
    size_t pos = offset, fim = offset + n;
    while (pos < fim) {
        int    logico = (int)(pos / TAM_BLOCO);
        size_t desl   = pos % TAM_BLOCO;
        size_t k      = TAM_BLOCO - desl;
        if (k > fim - pos)
            k = fim - pos;
        memcpy(saida + (pos - offset), d->dados[ler_idx(d, f, logico)] + desl, k);
        pos += k;
    }
    return n;
}

/* Classifica o conteúdo (atributo "tipo" do FCB). */
static TipoArquivo tipo_infere(const char *b, size_t n)
{
    if (n >= 2 && b[0] == '#' && b[1] == '!')
        return T_PROGRAMA;
    int tem_digito = 0, so_numeros = 1;
    for (size_t i = 0; i < n; i++) {
        unsigned char c = (unsigned char)b[i];
        if ((c < 32 && c != '\n' && c != '\t' && c != '\r') || c == 127)
            return T_BINARIO;
        if (isdigit(c))
            tem_digito = 1;
        else if (!strchr(" \n\t\r.,+-", c))
            so_numeros = 0;
    }
    return (tem_digito && so_numeros) ? T_NUMERICO : T_TEXTO;
}

static void atualiza_tipo(const Disco *d, FCB *f)
{
    char buf[TAM_MAX_ARQ];
    size_t n = inode_le(d, f, 0, sizeof buf, buf);
    f->tipo = tipo_infere(buf, n);
}

FsErro inode_escreve(Disco *d, FCB *f, const char *dados, size_t len, int anexar)
{
    FsErro e;
    if (anexar) {
        e = escreve_em(d, f, f->tamanho, dados, len);
    } else {
        if (len > TAM_MAX_ARQ)
            return FS_E_ARQ_GRANDE;
        e = redimensiona(d, f, blocos_para(len)); /* reaproveita os blocos que já tinha */
        if (e)
            return e;
        f->tamanho = 0;
        e = escreve_em(d, f, 0, dados, len);
        f->modificado = agora();
    }
    if (e == FS_OK)
        atualiza_tipo(d, f);
    return e;
}

FsErro inode_trunca(Disco *d, FCB *f, size_t novo)
{
    FsErro e = FS_OK;
    if (novo > TAM_MAX_ARQ)
        return FS_E_ARQ_GRANDE;
    if (novo < f->tamanho) {
        e = redimensiona(d, f, blocos_para(novo));
        if (e == FS_OK)
            f->tamanho = novo;
    } else if (novo > f->tamanho) {
        e = escreve_em(d, f, f->tamanho, NULL, novo - f->tamanho);
    }
    if (e == FS_OK) {
        f->modificado = agora();
        atualiza_tipo(d, f);
    }
    return e;
}
