/* fs.c - Árvore de diretórios, resolução de caminhos e operações com arquivos. */
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "fs.h"

/* ================= auxiliares da árvore ================= */

static int nome_valido(const char *n)
{
    size_t len = strlen(n);
    if (len == 0 || len > NOME_MAX || strcmp(n, ".") == 0 || strcmp(n, "..") == 0)
        return 0;
    for (const char *p = n; *p; p++)
        if (!isalnum((unsigned char)*p) && *p != '.' && *p != '_' && *p != '-')
            return 0;
    return 1;
}

static int eh_dir(const Entrada *e)
{
    return e->inode->tipo == T_DIRETORIO;
}

/* Cria a entrada (nome -> inode). Aumenta o contador de links do inode. */
static Entrada *entrada_nova(const char *nome, FCB *inode)
{
    Entrada *e = calloc(1, sizeof *e);
    if (!e)
        return NULL;
    snprintf(e->nome, sizeof e->nome, "%s", nome);
    e->inode = inode;
    inode->n_links++;
    return e;
}

/* Insere em ordem alfabética (deixa ls e tree previsíveis). */
static void insere_filho(Entrada *dir, Entrada *e)
{
    Entrada **pp = &dir->filhos;
    while (*pp && strcmp((*pp)->nome, e->nome) < 0)
        pp = &(*pp)->irmao;
    e->irmao = *pp;
    *pp = e;
    e->pai = dir;
}

static void remove_filho(Entrada *dir, Entrada *e)
{
    Entrada **pp = &dir->filhos;
    while (*pp && *pp != e)
        pp = &(*pp)->irmao;
    if (*pp)
        *pp = e->irmao;
    e->irmao = NULL;
    e->pai = NULL;
}

Entrada *fs_busca_filho(const Entrada *dir, const char *nome)
{
    for (Entrada *c = dir->filhos; c; c = c->irmao)
        if (strcmp(c->nome, nome) == 0)
            return c;
    return NULL;
}

/* Tira um nome do inode; quando não sobra nenhum, o inode e seus blocos são liberados. */
static void desliga_inode(Fs *fs, FCB *f)
{
    if (--f->n_links == 0)
        inode_libera(&fs->inodes, &fs->disco, f);
}

/* Libera a entrada e, se for diretório, toda a subárvore (pós-ordem). */
static void entrada_destroi(Fs *fs, Entrada *e)
{
    Entrada *c = e->filhos;
    while (c) {
        Entrada *prox = c->irmao;
        entrada_destroi(fs, c);
        c = prox;
    }
    desliga_inode(fs, e->inode);
    free(e);
}

/* Cria um novo arquivo/diretório dentro de dir, com dono = usuário atual. */
static FsErro cria_no(Fs *fs, Entrada *dir, const char *nome, TipoArquivo tipo,
                      uint16_t perm, Entrada **saida)
{
    FCB *f = inode_novo(&fs->inodes, tipo, fs->atual->uid, fs->atual->gid, perm);
    if (!f)
        return FS_E_SEM_MEMORIA;
    Entrada *e = entrada_nova(nome, f);
    if (!e) {
        inode_libera(&fs->inodes, &fs->disco, f);
        return FS_E_SEM_MEMORIA;
    }
    insere_filho(dir, e);
    dir->inode->modificado = agora();
    if (saida)
        *saida = e;
    return FS_OK;
}

/* Desfaz um cria_no quando a operação seguinte falhou (tudo ou nada). */
static void desfaz_criacao(Fs *fs, Entrada *dir, Entrada *e)
{
    remove_filho(dir, e);
    entrada_destroi(fs, e);
}

/* true se alvo é o diretório atual ou um ancestral dele. */
static int em_uso(const Fs *fs, const Entrada *alvo)
{
    for (const Entrada *x = fs->cwd;; x = x->pai) {
        if (x == alvo)
            return 1;
        if (x == fs->raiz)
            return 0;
    }
}

/* Pode criar/remover/renomear nomes dentro de dir? (precisa de w e x) */
static int pode_alterar(const Fs *fs, const Entrada *dir)
{
    return perm_checa(fs->atual, dir->inode, P_W | P_X);
}

/* ================= criação e destruição ================= */

static Entrada *cria_inicial(Fs *fs, Entrada *dir, const char *nome, uint16_t perm,
                             const char *dono)
{
    Entrada *e = NULL;
    if (cria_no(fs, dir, nome, T_DIRETORIO, perm, &e) != FS_OK)
        return NULL;
    const Usuario *u = usuario_por_nome(dono);
    e->inode->dono = u->uid;
    e->inode->grupo = u->gid;
    return e;
}

Fs *fs_cria(void)
{
    Fs *fs = calloc(1, sizeof *fs);
    if (!fs)
        return NULL;
    disco_init(&fs->disco);
    if (inodes_init(&fs->inodes) != FS_OK) {
        free(fs);
        return NULL;
    }
    fs->umask = 022;
    fs->atual = usuario_por_nome("root");

    FCB *r = inode_novo(&fs->inodes, T_DIRETORIO, 0, 0, 0755);
    if (!r || !(fs->raiz = entrada_nova("/", r))) {
        if (r)
            inode_libera(&fs->inodes, &fs->disco, r);
        inodes_destroi(&fs->inodes, &fs->disco);
        free(fs);
        return NULL;
    }
    fs->raiz->pai = fs->raiz;
    fs->cwd = fs->raiz;

    Entrada *home = cria_inicial(fs, fs->raiz, "home", 0755, "root");
    Entrada *alice = home ? cria_inicial(fs, home, "alice", 0755, "alice") : NULL;
    if (!home || !alice || !cria_inicial(fs, home, "bob", 0755, "bob")
        || !cria_inicial(fs, home, "carol", 0755, "carol")
        || !cria_inicial(fs, fs->raiz, "tmp", 0777, "root")) {
        fs_destroi(fs);
        return NULL;
    }

    fs->atual = usuario_por_nome("alice");
    fs->cwd = alice;
    return fs;
}

void fs_destroi(Fs *fs)
{
    if (!fs)
        return;
    if (fs->raiz)
        entrada_destroi(fs, fs->raiz);
    inodes_destroi(&fs->inodes, &fs->disco); /* não deve sobrar nenhum, mas garante */
    free(fs);
}

/* ================= caminhos ================= */

FsErro fs_resolve(Fs *fs, const char *caminho, Entrada **saida)
{
    if (!caminho || !*caminho)
        return FS_E_NAO_EXISTE;
    if (strlen(caminho) >= CAMINHO_MAX)
        return FS_E_NOME_INVALIDO;

    char buf[CAMINHO_MAX];
    strcpy(buf, caminho);
    Entrada *at = (caminho[0] == '/') ? fs->raiz : fs->cwd;

    char *ctx = NULL;
    for (char *comp = strtok_r(buf, "/", &ctx); comp; comp = strtok_r(NULL, "/", &ctx)) {
        if (!eh_dir(at))
            return FS_E_NAO_DIRETORIO;
        if (!perm_checa(fs->atual, at->inode, P_X))   /* atravessar exige x */
            return FS_E_PERMISSAO;
        if (strcmp(comp, ".") == 0)
            continue;
        if (strcmp(comp, "..") == 0) {
            at = at->pai;
            continue;
        }
        Entrada *f = fs_busca_filho(at, comp);
        if (!f)
            return FS_E_NAO_EXISTE;
        at = f;
    }

    size_t n = strlen(caminho);
    if (n > 1 && caminho[n - 1] == '/' && !eh_dir(at))
        return FS_E_NAO_DIRETORIO;
    *saida = at;
    return FS_OK;
}

/* Separa "a/b/c" em diretório pai (a/b, já resolvido) e último nome (c). */
static FsErro resolve_pai(Fs *fs, const char *caminho, Entrada **pai, char nome[NOME_MAX + 1])
{
    if (!caminho || !*caminho)
        return FS_E_NAO_EXISTE;
    if (strlen(caminho) >= CAMINHO_MAX)
        return FS_E_NOME_INVALIDO;

    char buf[CAMINHO_MAX];
    strcpy(buf, caminho);
    size_t n = strlen(buf);
    while (n > 1 && buf[n - 1] == '/')
        buf[--n] = '\0';

    Entrada *p;
    const char *ultimo;
    char *barra = strrchr(buf, '/');
    if (!barra) {
        p = fs->cwd;
        ultimo = buf;
    } else {
        ultimo = barra + 1;
        if (barra == buf) {
            p = fs->raiz;
        } else {
            *barra = '\0';
            FsErro e = fs_resolve(fs, buf, &p);
            if (e)
                return e;
        }
    }

    if (!eh_dir(p))
        return FS_E_NAO_DIRETORIO;
    if (!perm_checa(fs->atual, p->inode, P_X))
        return FS_E_PERMISSAO;
    if (*ultimo == '\0')
        return FS_E_OPERACAO;            /* o caminho é a própria raiz */
    if (!nome_valido(ultimo))
        return FS_E_NOME_INVALIDO;

    memcpy(nome, ultimo, strlen(ultimo) + 1); /* nome_valido garante <= NOME_MAX */
    *pai = p;
    return FS_OK;
}

/* Monta o caminho absoluto de e subindo pelos ponteiros pai. */
void fs_caminho(const Fs *fs, const Entrada *e, char *buf, size_t tam)
{
    const Entrada *pilha[CAMINHO_MAX];
    int n = 0;
    for (const Entrada *x = e; x != fs->raiz && n < CAMINHO_MAX; x = x->pai)
        pilha[n++] = x;

    size_t usado = 0;
    buf[0] = '\0';
    if (n == 0)
        snprintf(buf, tam, "/");
    for (int i = n - 1; i >= 0 && usado < tam; i--)
        usado += (size_t)snprintf(buf + usado, tam - usado, "/%s", pilha[i]->nome);
}

/* Destino de cp/mv/ln: se "destino" for um diretório existente, o item vai
 * para dentro dele com o nome de origem; senão é o caminho do novo nome. */
static FsErro resolve_destino(Fs *fs, const char *destino, const char *nome_origem,
                              Entrada **pai, char nome[NOME_MAX + 1])
{
    Entrada *d;
    if (fs_resolve(fs, destino, &d) == FS_OK && eh_dir(d)) {
        if (!perm_checa(fs->atual, d->inode, P_X))
            return FS_E_PERMISSAO;
        snprintf(nome, NOME_MAX + 1, "%s", nome_origem);
        *pai = d;
        return FS_OK;
    }
    return resolve_pai(fs, destino, pai, nome);
}

/* ================= diretórios ================= */

FsErro fs_mkdir(Fs *fs, const char *caminho, int criar_pais)
{
    if (criar_pais) {
        /* mkdir -p: cria cada prefixo que ainda não existe. */
        if (!caminho || !*caminho)
            return FS_E_NAO_EXISTE;
        if (strlen(caminho) >= CAMINHO_MAX)
            return FS_E_NOME_INVALIDO;
        char buf[CAMINHO_MAX];
        strcpy(buf, caminho);
        for (char *p = buf + 1;; p++) {
            if (*p != '/' && *p != '\0')
                continue;
            char c = *p;
            *p = '\0';
            FsErro e = fs_mkdir(fs, buf, 0);
            if (e == FS_E_JA_EXISTE) {
                Entrada *x;
                e = fs_resolve(fs, buf, &x);
                if (e == FS_OK && !eh_dir(x))
                    e = FS_E_NAO_DIRETORIO;
            }
            if (e)
                return e;
            *p = c;
            if (c == '\0' || p[1] == '\0')
                break;
        }
        return FS_OK;
    }

    Entrada *pai;
    char nome[NOME_MAX + 1];
    FsErro e = resolve_pai(fs, caminho, &pai, nome);
    if (e)
        return e;
    if (fs_busca_filho(pai, nome))
        return FS_E_JA_EXISTE;
    if (!pode_alterar(fs, pai))
        return FS_E_PERMISSAO;
    return cria_no(fs, pai, nome, T_DIRETORIO, (uint16_t)(0777 & ~fs->umask), NULL);
}

FsErro fs_rmdir(Fs *fs, const char *caminho)
{
    Entrada *pai;
    char nome[NOME_MAX + 1];
    FsErro e = resolve_pai(fs, caminho, &pai, nome);
    if (e)
        return e;
    Entrada *alvo = fs_busca_filho(pai, nome);
    if (!alvo)
        return FS_E_NAO_EXISTE;
    if (!eh_dir(alvo))
        return FS_E_NAO_DIRETORIO;
    if (alvo->filhos)
        return FS_E_NAO_VAZIO;
    if (em_uso(fs, alvo))
        return FS_E_EM_USO;
    if (!pode_alterar(fs, pai))
        return FS_E_PERMISSAO;
    remove_filho(pai, alvo);
    pai->inode->modificado = agora();
    entrada_destroi(fs, alvo);
    return FS_OK;
}

FsErro fs_cd(Fs *fs, const char *caminho)
{
    Entrada *d;
    FsErro e = fs_resolve(fs, caminho, &d);
    if (e)
        return e;
    if (!eh_dir(d))
        return FS_E_NAO_DIRETORIO;
    if (!perm_checa(fs->atual, d->inode, P_X))
        return FS_E_PERMISSAO;
    fs->cwd = d;
    return FS_OK;
}

FsErro fs_lista(Fs *fs, const char *caminho, Entrada **saida)
{
    Entrada *d;
    FsErro e = fs_resolve(fs, caminho, &d);
    if (e)
        return e;
    if (eh_dir(d) && !perm_checa(fs->atual, d->inode, P_R)) /* listar exige r */
        return FS_E_PERMISSAO;
    if (eh_dir(d))
        d->inode->acessado = agora();
    *saida = d;
    return FS_OK;
}

/* ================= arquivos ================= */

FsErro fs_touch(Fs *fs, const char *caminho)
{
    Entrada *pai;
    char nome[NOME_MAX + 1];
    FsErro e = resolve_pai(fs, caminho, &pai, nome);
    if (e)
        return e;

    Entrada *alvo = fs_busca_filho(pai, nome);
    if (alvo) {
        /* Já existe: só atualiza as datas (exige ser o dono ou ter w). */
        if (alvo->inode->dono != fs->atual->uid && !perm_checa(fs->atual, alvo->inode, P_W))
            return FS_E_PERMISSAO;
        alvo->inode->acessado = alvo->inode->modificado = agora();
        return FS_OK;
    }
    if (!pode_alterar(fs, pai))
        return FS_E_PERMISSAO;
    return cria_no(fs, pai, nome, T_TEXTO, (uint16_t)(0666 & ~fs->umask), NULL);
}

FsErro fs_escreve(Fs *fs, const char *caminho, const char *dados, size_t len, int anexar)
{
    Entrada *pai;
    char nome[NOME_MAX + 1];
    FsErro e = resolve_pai(fs, caminho, &pai, nome);
    if (e)
        return e;

    Entrada *alvo = fs_busca_filho(pai, nome);
    int criado = 0;
    if (alvo) {
        if (eh_dir(alvo))
            return FS_E_EH_DIRETORIO;
        if (!perm_checa(fs->atual, alvo->inode, P_W))
            return FS_E_PERMISSAO;
    } else {
        if (!pode_alterar(fs, pai))
            return FS_E_PERMISSAO;
        e = cria_no(fs, pai, nome, T_TEXTO, (uint16_t)(0666 & ~fs->umask), &alvo);
        if (e)
            return e;
        criado = 1;
    }
    e = inode_escreve(&fs->disco, alvo->inode, dados, len, anexar);
    if (e && criado)
        desfaz_criacao(fs, pai, alvo);       /* não deixa arquivo vazio para trás */
    return e;
}

FsErro fs_le(Fs *fs, const char *caminho, size_t offset, size_t n, char **buf, size_t *len)
{
    Entrada *a;
    FsErro e = fs_resolve(fs, caminho, &a);
    if (e)
        return e;
    if (eh_dir(a))
        return FS_E_EH_DIRETORIO;
    if (!perm_checa(fs->atual, a->inode, P_R))
        return FS_E_PERMISSAO;

    FCB *f = a->inode;
    size_t disponivel = offset < f->tamanho ? f->tamanho - offset : 0;
    if (n > disponivel)
        n = disponivel;

    char *b = malloc(n + 1);   /* quem chama faz free */
    if (!b)
        return FS_E_SEM_MEMORIA;
    *len = inode_le(&fs->disco, f, offset, n, b);
    b[*len] = '\0';
    f->acessado = agora();
    *buf = b;
    return FS_OK;
}

FsErro fs_trunca(Fs *fs, const char *caminho, size_t tamanho)
{
    Entrada *a;
    FsErro e = fs_resolve(fs, caminho, &a);
    if (e)
        return e;
    if (eh_dir(a))
        return FS_E_EH_DIRETORIO;
    if (!perm_checa(fs->atual, a->inode, P_W))
        return FS_E_PERMISSAO;
    return inode_trunca(&fs->disco, a->inode, tamanho);
}

FsErro fs_cp(Fs *fs, const char *origem, const char *destino)
{
    Entrada *s;
    FsErro e = fs_resolve(fs, origem, &s);
    if (e)
        return e;
    if (eh_dir(s))
        return FS_E_EH_DIRETORIO;           /* sem cp -r */
    if (!perm_checa(fs->atual, s->inode, P_R))
        return FS_E_PERMISSAO;

    Entrada *dpai;
    char nome[NOME_MAX + 1];
    e = resolve_destino(fs, destino, s->nome, &dpai, nome);
    if (e)
        return e;
    Entrada *d = fs_busca_filho(dpai, nome);
    if (d && d->inode == s->inode)
        return FS_E_OPERACAO;               /* origem e destino são o mesmo arquivo */
    if (d && eh_dir(d))
        return FS_E_EH_DIRETORIO;
    if (d && !perm_checa(fs->atual, d->inode, P_W))
        return FS_E_PERMISSAO;
    if (!d && !pode_alterar(fs, dpai))
        return FS_E_PERMISSAO;

    /* Copia os dados para um buffer: o destino ganha blocos NOVOS. */
    size_t len = s->inode->tamanho;
    char *buf = malloc(len ? len : 1);
    if (!buf)
        return FS_E_SEM_MEMORIA;
    inode_le(&fs->disco, s->inode, 0, len, buf);
    s->inode->acessado = agora();

    int criado = 0;
    if (!d) {
        e = cria_no(fs, dpai, nome, T_TEXTO,
                    (uint16_t)(s->inode->perm & ~fs->umask), &d); /* inode NOVO */
        criado = e == FS_OK;
    }
    if (e == FS_OK)
        e = inode_escreve(&fs->disco, d->inode, buf, len, 0);
    if (e && criado)
        desfaz_criacao(fs, dpai, d);
    free(buf);
    return e;
}

FsErro fs_mv(Fs *fs, const char *origem, const char *destino)
{
    Entrada *spai;
    char snome[NOME_MAX + 1];
    FsErro e = resolve_pai(fs, origem, &spai, snome);
    if (e)
        return e;
    Entrada *s = fs_busca_filho(spai, snome);
    if (!s)
        return FS_E_NAO_EXISTE;
    if (!pode_alterar(fs, spai))
        return FS_E_PERMISSAO;

    Entrada *dpai;
    char nome[NOME_MAX + 1];
    e = resolve_destino(fs, destino, s->nome, &dpai, nome);
    if (e)
        return e;
    if (!pode_alterar(fs, dpai))
        return FS_E_PERMISSAO;

    if (eh_dir(s)) /* não pode mover um diretório para dentro dele mesmo */
        for (Entrada *x = dpai;; x = x->pai) {
            if (x == s)
                return FS_E_OPERACAO;
            if (x == fs->raiz)
                break;
        }

    Entrada *d = fs_busca_filho(dpai, nome);
    if (d == s)
        return FS_OK;                       /* mesmo lugar e mesmo nome */
    if (d) {                                /* destino existe: é substituído */
        if (eh_dir(d)) {
            if (!eh_dir(s))
                return FS_E_EH_DIRETORIO;
            if (d->filhos)
                return FS_E_NAO_VAZIO;
            if (em_uso(fs, d))
                return FS_E_EM_USO;
        } else if (eh_dir(s)) {
            return FS_E_NAO_DIRETORIO;
        }
        remove_filho(dpai, d);
        entrada_destroi(fs, d);
    }

    /* Só a ENTRADA muda de lugar/nome: inode e blocos continuam os mesmos. */
    remove_filho(spai, s);
    snprintf(s->nome, sizeof s->nome, "%s", nome);
    insere_filho(dpai, s);
    spai->inode->modificado = dpai->inode->modificado = agora();
    return FS_OK;
}

/* rm -r: confere a subárvore inteira antes de apagar (tudo ou nada). */
static FsErro checa_remocao_recursiva(const Fs *fs, const Entrada *dir)
{
    if (dir->filhos && !perm_checa(fs->atual, dir->inode, P_R | P_W | P_X))
        return FS_E_PERMISSAO;
    for (const Entrada *c = dir->filhos; c; c = c->irmao)
        if (eh_dir(c)) {
            FsErro e = checa_remocao_recursiva(fs, c);
            if (e)
                return e;
        }
    return FS_OK;
}

FsErro fs_rm(Fs *fs, const char *caminho, int recursivo)
{
    Entrada *pai;
    char nome[NOME_MAX + 1];
    FsErro e = resolve_pai(fs, caminho, &pai, nome);
    if (e)
        return e;
    Entrada *alvo = fs_busca_filho(pai, nome);
    if (!alvo)
        return FS_E_NAO_EXISTE;
    /* Apagar um nome depende de w+x no DIRETÓRIO, não no arquivo. */
    if (!pode_alterar(fs, pai))
        return FS_E_PERMISSAO;
    if (eh_dir(alvo)) {
        if (!recursivo)
            return FS_E_EH_DIRETORIO;
        if (em_uso(fs, alvo))
            return FS_E_EM_USO;
        e = checa_remocao_recursiva(fs, alvo);
        if (e)
            return e;
    }
    remove_filho(pai, alvo);
    pai->inode->modificado = agora();
    entrada_destroi(fs, alvo);   /* n_links--, libera inode/blocos se chegar a 0 */
    return FS_OK;
}

FsErro fs_ln(Fs *fs, const char *origem, const char *destino)
{
    Entrada *s;
    FsErro e = fs_resolve(fs, origem, &s);
    if (e)
        return e;
    if (eh_dir(s))
        return FS_E_EH_DIRETORIO;           /* hard link para diretório não é permitido */

    Entrada *dpai;
    char nome[NOME_MAX + 1];
    e = resolve_destino(fs, destino, s->nome, &dpai, nome);
    if (e)
        return e;
    if (fs_busca_filho(dpai, nome))
        return FS_E_JA_EXISTE;
    if (!pode_alterar(fs, dpai))
        return FS_E_PERMISSAO;

    Entrada *novo = entrada_nova(nome, s->inode); /* mesmo inode, n_links++ */
    if (!novo)
        return FS_E_SEM_MEMORIA;
    insere_filho(dpai, novo);
    dpai->inode->modificado = agora();
    return FS_OK;
}

/* ================= metadados e usuários ================= */

FsErro fs_chmod(Fs *fs, const char *modo, const char *caminho)
{
    Entrada *a;
    FsErro e = fs_resolve(fs, caminho, &a);
    if (e)
        return e;
    if (fs->atual->uid != 0 && fs->atual->uid != a->inode->dono)
        return FS_E_OPERACAO;               /* só o dono ou o root */
    uint16_t novo;
    e = perm_parse(modo, a->inode->perm, &novo);
    if (e)
        return e;
    a->inode->perm = novo;
    return FS_OK;
}

FsErro fs_chown(Fs *fs, const char *dono_grupo, const char *caminho)
{
    char buf[40];
    if (strlen(dono_grupo) >= sizeof buf)
        return FS_E_USUARIO;
    strcpy(buf, dono_grupo);
    char *grupo = strchr(buf, ':');
    if (grupo)
        *grupo++ = '\0';

    const Usuario *u = usuario_por_nome(buf);
    if (!u)
        return FS_E_USUARIO;
    const Grupo *g = NULL;
    if (grupo && *grupo && !(g = grupo_por_nome(grupo)))
        return FS_E_USUARIO;

    Entrada *a;
    FsErro e = fs_resolve(fs, caminho, &a);
    if (e)
        return e;
    if (fs->atual->uid != 0)
        return FS_E_OPERACAO;               /* só o root troca o dono */
    a->inode->dono = u->uid;
    if (g)
        a->inode->grupo = g->gid;
    return FS_OK;
}

FsErro fs_exec(Fs *fs, const char *caminho, char **conteudo, size_t *len)
{
    Entrada *a;
    FsErro e = fs_resolve(fs, caminho, &a);
    if (e)
        return e;
    if (eh_dir(a))
        return FS_E_EH_DIRETORIO;
    /* Um script precisa de x (executar) e de r (o interpretador lê o texto). */
    if (!perm_checa(fs->atual, a->inode, P_X) || !perm_checa(fs->atual, a->inode, P_R))
        return FS_E_PERMISSAO;
    if (a->inode->tipo != T_PROGRAMA)
        return FS_E_NAO_EXECUTAVEL;
    return fs_le(fs, caminho, 0, a->inode->tamanho, conteudo, len);
}

FsErro fs_su(Fs *fs, const char *usuario)
{
    const Usuario *u = usuario_por_nome(usuario);
    if (!u)
        return FS_E_USUARIO;
    fs->atual = u;
    return FS_OK;
}
