# M3 — Mini-sistema de arquivos em memória (esboço de arquitetura)

> **Status:** esboço. Ainda não tem código. Este documento define a arquitetura, as estruturas de dados
> e os fluxos principais antes da implementação. Quando o código existir, este README vira a
> documentação final pedida na seção 5.5 do enunciado.

Simulador em **C** de um sistema de arquivos Unix-like que roda inteiro em memória: árvore de diretórios,
FCB/inode por arquivo, permissões RWX com bitmask e um "disco" simulado (`char[]`) dividido em blocos
com **alocação indexada**. É operado por um shell próprio (`mfs>`), com comandos parecidos com os do Linux.

---

## 1. O que pesa na nota e como a arquitetura responde

| Critério (peso) | O que o professor olha | Decisão de arquitetura |
| --- | --- | --- |
| **Funcionalidade e robustez (40%)** | tudo funciona, erros tratados, sem memory leak | toda operação devolve um `FsErro` (nunca `exit` no meio); um único ponto imprime a mensagem; dono único de cada `malloc`; `make memcheck` com valgrind obrigatório antes de entregar; roteiros de teste automáticos |
| **Fidelidade aos conceitos (30%)** | FCB e diretórios claros, controle de acesso efetivo, ligação teoria/código, bônus (links, blocos) | FCB/inode **separado** da entrada de diretório (como no Unix) → permite `ln` (hard link) de graça; permissões checadas num único lugar (`perm_checa`) em **toda** operação, inclusive `cd` e criação em diretório; alocação de blocos indexada visível (`blocos`, `df`) |
| **Qualidade do código (20%)** | modular, comentado, uso de ponteiros, `malloc`, `typedef`, `enum` | 6 módulos com `.h` próprio, sem variáveis globais espalhadas (só o contexto `Fs`); `typedef`/`enum` para tipos, erros e permissões |
| **Documentação (10%)** | README claro, foco em teoria ↔ implementação | seção "Conceito → onde está no código" (seção 8) e comparação lado a lado com o Linux real (seção 9) |
| Apresentação (YouTube) | — | roteiro `demo.txt` que roda do começo ao fim e mostra cada conceito (seção 10) |

**Prioridade de implementação**: o que vale 70% (funcionar sem leak + conceitos corretos) vem antes de
qualquer enfeite. Ordem na seção 11.

---

## 2. Visão geral

```
             ┌──────────────────────────────────────────────┐
  stdin ───► │ shell   (lê linha, separa args, despacha)    │ ──► stdout / mensagens de erro
  (ou        └───────────────┬──────────────────────────────┘
  roteiro)                   │ chama fs_*(...) e recebe FsErro
             ┌───────────────▼──────────────────────────────┐
             │ fs      (árvore de diretórios, caminhos,     │
             │          touch, mkdir, cp, mv, rm, ln, ...)  │
             └──────┬───────────────┬───────────────┬───────┘
                    │               │               │
             ┌──────▼─────┐  ┌──────▼──────┐  ┌─────▼────────┐
             │ inode      │  │ perm        │  │ disco        │
             │ tabela de  │  │ usuários,   │  │ char[] em    │
             │ FCBs       │  │ grupos, RWX │  │ blocos +     │
             │            │  │ bitmask     │  │ bitmap livre │
             └────────────┘  └─────────────┘  └──────────────┘
```

Regra de dependência: `shell → fs → {inode, perm, disco}`. As camadas de baixo não conhecem o shell e
não imprimem nada (só devolvem código de erro). Isso deixa cada módulo testável sozinho e evita
`printf` espalhado.

### Módulos / arquivos previstos

| Arquivo | Responsabilidade |
| --- | --- |
| `main.c` | cria o `Fs`, popula usuários, entra no loop do shell, destrói tudo no fim |
| `shell.c/.h` | REPL, tokenização (aspas, `>` e `>>`), tabela de comandos `{nome, função, ajuda}` |
| `fs.c/.h` | árvore de diretórios, resolução de caminho, operações de alto nível |
| `inode.c/.h` | `FCB` (inode simulado), tabela de inodes, contador de links |
| `perm.c/.h` | usuários/grupos, usuário atual, `perm_checa`, conversão `"755"` ↔ `rwxr-xr-x` |
| `disco.c/.h` | disco simulado, bitmap de blocos livres, alocar/liberar/ler/escrever blocos |
| `erros.h` | `enum FsErro` + `fs_strerro()` |
| `Makefile` | `make`, `make teste`, `make memcheck`, `make demo` |
| `testes/` | roteiros `.txt` + saída esperada `.esperado` |

---

## 3. Estruturas de dados

### 3.1 Disco simulado e alocação indexada (objetivo 3.4)

```c
#define TAM_BLOCO     64          /* pequeno de propósito: arquivos curtos já ocupam vários blocos */
#define N_BLOCOS      1024        /* disco de 64 KiB */
#define N_DIRETOS     8           /* ponteiros diretos no inode */

typedef struct {
    char     dados[N_BLOCOS][TAM_BLOCO];  /* o "disco": um char[] contínuo */
    uint8_t  livre[N_BLOCOS];             /* bitmap (1 = livre); poderia ser bit a bit */
    int      n_livres;
} Disco;
```

- **Método escolhido: indexada** (8 ponteiros diretos + 1 bloco de índice indireto simples).
  Tamanho máximo: `(8 + 64/sizeof(int)) * 64 = (8 + 16) * 64 = 1536 bytes` — suficiente para
  mostrar o indireto sendo usado com um `echo` maior.
- Por que indexada: não tem fragmentação externa (problema da contínua), tem acesso direto ao
  bloco *n* (problema da encadeada) e é o modelo do inode do ext2/ext3, então conversa com o Linux.
- Contínua e encadeada **entram na discussão teórica do README** (vantagens/desvantagens + desenho),
  não no código. *Opcional, se sobrar tempo:* flag `--alocacao contigua` para comparar fragmentação.
- Diretórios **não** usam blocos do disco (as entradas ficam na árvore em memória). Isso é uma
  simplificação consciente e será explicada.

### 3.2 FCB / inode simulado (objetivo 3.2)

```c
typedef enum { T_DIRETORIO, T_TEXTO, T_NUMERICO, T_BINARIO, T_PROGRAMA } TipoArquivo;

typedef struct {
    int          id;                 /* número do inode (índice na tabela) */
    TipoArquivo  tipo;
    size_t       tamanho;            /* bytes */
    int          dono;               /* uid */
    int          grupo;              /* gid */
    uint16_t     perm;               /* 9 bits: rwx rwx rwx (ex.: 0755) */
    time_t       criado, modificado, acessado;
    int          n_links;            /* quantas entradas de diretório apontam para cá */
    int          n_blocos;
    int          diretos[N_DIRETOS]; /* -1 = não usado */
    int          indireto;           /* bloco que guarda mais índices, -1 = não usado */
} FCB;

typedef struct {
    FCB  **v;                        /* tabela de inodes: v[id] (NULL = livre) */
    int    cap;
} TabelaInodes;
```

O **nome não fica no FCB**, fica na entrada de diretório — exatamente como no Unix. É isso que
permite dois nomes para o mesmo inode (`ln`) e que faz `mv` não mudar o número do inode.

O tipo é inferido no `echo` (só dígitos → `T_NUMERICO`; bytes não imprimíveis → `T_BINARIO`;
conteúdo começando com `#!` → `T_PROGRAMA`; senão `T_TEXTO`) e aparece no `stat`.

### 3.3 Árvore de diretórios (objetivo 3.1)

```c
typedef struct Entrada {
    char            nome[NOME_MAX + 1]; /* NOME_MAX = 32 */
    FCB            *inode;
    struct Entrada *pai;                /* ".." */
    struct Entrada *filhos;             /* 1º filho (só se diretório) */
    struct Entrada *irmao;              /* próximo na lista do mesmo diretório */
} Entrada;
```

- Árvore n-ária no formato **primeiro-filho / próximo-irmão** (lista encadeada de filhos por
  diretório). Simples com ponteiros, sem limite fixo de filhos.
- `pai` permite `cd ..`, `pwd` (sobe até a raiz montando o caminho) e impede `mv` de um diretório
  para dentro dele mesmo.
- `.` e `..` são resolvidos na busca, não armazenados como entradas.

### 3.4 Usuários e contexto

```c
typedef struct { int uid; char nome[16]; int gid; } Usuario;
typedef struct { int gid; char nome[16]; } Grupo;

typedef struct {
    Disco         disco;
    TabelaInodes  inodes;
    Entrada      *raiz;
    Entrada      *cwd;       /* diretório atual */
    Usuario      *atual;     /* "usuário atual" do enunciado */
    Usuario       usuarios[MAX_USUARIOS];
    int           n_usuarios;
    uint16_t      umask;     /* 022 por padrão */
} Fs;
```

Usuários fixos pré-cadastrados: `root (0, grupo root)`, `alice (1000, grupo dev)`,
`bob (1001, grupo dev)`, `carol (1002, grupo outros)`. Troca com `su <nome>`. Um único `Fs`
é passado por ponteiro para todas as funções (nada de globais soltas).

### 3.5 Erros

```c
typedef enum {
    FS_OK = 0,
    FS_E_NAO_EXISTE,     /* No such file or directory */
    FS_E_JA_EXISTE,      /* File exists */
    FS_E_PERMISSAO,      /* Permission denied */
    FS_E_NAO_DIRETORIO,  /* Not a directory */
    FS_E_EH_DIRETORIO,   /* Is a directory */
    FS_E_NAO_VAZIO,      /* Directory not empty */
    FS_E_NOME_INVALIDO,  /* nome vazio, com '/', > NOME_MAX, "." ou ".." */
    FS_E_SEM_ESPACO,     /* No space left on device (sem blocos ou arquivo grande demais) */
    FS_E_SEM_MEMORIA,    /* malloc falhou */
    FS_E_ARGUMENTOS,     /* uso incorreto do comando */
    FS_E_OPERACAO        /* operação inválida (ex.: mv dir para dentro de si) */
} FsErro;
```

Mensagem padronizada pelo shell, no formato do Linux:
`cat: /home/alice/x.txt: arquivo ou diretório não encontrado`.

---

## 4. Permissões (objetivo 3.3)

Bits no mesmo layout do Unix (`0755` = `rwxr-xr-x`):

```c
enum { P_LER = 4, P_ESCREVER = 2, P_EXECUTAR = 1 };

/* único ponto de decisão de acesso */
bool perm_checa(const Fs *fs, const FCB *f, int op) {
    if (fs->atual->uid == 0) return true;               /* root ignora (como no Linux) */
    int desloc = (fs->atual->uid == f->dono)  ? 6       /* owner */
               : (fs->atual->gid == f->grupo) ? 3       /* group */
               :                                0;      /* others */
    return ((f->perm >> desloc) & op) == op;
}
```

- `chmod 640 f` → `perm = strtol("640", NULL, 8)`; também aceitar simbólico simples
  (`u+x`, `go-w`) com `|=`/`&= ~` — mostra os operadores bit a bit.
- Exibição: `ls -l` e `stat` montam `drwxr-x---` testando cada bit.
- Arquivos novos: `0666 & ~umask` (arquivo) e `0777 & ~umask` (diretório).
- `chmod`/`chown` só pelo dono ou root.

**O que exige cada permissão** (semântica real do Unix, ponto forte em "efetividade do controle"):

| Operação | Precisa |
| --- | --- |
| `cat`, `cp` (origem) | `r` no arquivo + `x` em cada diretório do caminho |
| `echo >`, `echo >>`, `truncate` | `w` no arquivo |
| `exec` | `x` no arquivo (e tipo `T_PROGRAMA`) |
| `ls` | `r` no diretório |
| `cd`, atravessar caminho | `x` no diretório |
| `touch`/`mkdir`/`rm`/`mv`/`ln` (criar/remover nome) | `w` + `x` no diretório **pai** |

---

## 5. Comandos do shell

| Comando | O que faz | Linux equivalente |
| --- | --- | --- |
| `mkdir [-p] <dir>` | cria diretório | `mkdir` |
| `rmdir <dir>` | remove diretório vazio | `rmdir` |
| `cd <dir>` / `pwd` | navega / mostra caminho | `cd` / `pwd` |
| `ls [-l] [-i] [dir]` | lista; `-l` com permissões, dono, tamanho, data; `-i` mostra inode | `ls -li` |
| `tree [dir]` | desenha a árvore | `tree` |
| `touch <arq>` | cria vazio ou atualiza datas | `touch` |
| `echo "txt" > arq` / `>> arq` | escreve / anexa (aloca blocos) | `echo` |
| `cat <arq>` | lê conteúdo (atualiza `acessado`) | `cat` |
| `read <arq> <offset> <n>` | lê a partir de uma posição (**reposicionar**) | `dd skip=` / `tail -c` |
| `truncate <arq> <tam>` | corta/estende (**truncar**, libera blocos) | `truncate -s` |
| `cp <orig> <dest>` | novo inode + novos blocos | `cp` |
| `mv <orig> <dest>` | move/renomeia; inode **não muda** | `mv` |
| `rm [-r] <arq>` | remove nome; libera inode quando `n_links == 0` | `rm` |
| `ln <orig> <novo>` | hard link (bônus) | `ln` |
| `stat <arq>` | mostra o FCB inteiro | `stat` |
| `chmod <octal\|simb> <arq>` | altera permissões | `chmod` |
| `chown <usuario>[:grupo] <arq>` | altera dono (root) | `chown` |
| `exec <arq>` | "executa" programa (checa `x`, imprime o conteúdo como se rodasse) | `./arq` |
| `su <usuario>` / `whoami` / `id` | troca/mostra usuário atual | `su` / `whoami` / `id` |
| `umask [octal]` | máscara de criação | `umask` |
| `blocos <arq>` | mostra os blocos do arquivo e o conteúdo de cada um | `debugfs -R "blocks"` / `filefrag -v` |
| `df` | blocos usados/livres + mapa do bitmap | `df` |
| `help`, `exit` | | |

Modos de execução: interativo (`./mfs`) e por roteiro (`./mfs < roteiro.txt` ou `./mfs -f roteiro.txt`),
que imprime cada comando antes da saída — usado pelos testes e pela demo.

---

## 6. Fluxos principais

### 6.1 Resolução de caminho (usado por todos os comandos)

```
resolve(fs, "a/b/c", &entrada):
  atual = caminho começa com '/' ? raiz : cwd
  para cada componente:
      "."  → continua
      ".." → atual = atual->pai (raiz: fica na raiz)
      se atual não é diretório       → FS_E_NAO_DIRETORIO
      se !perm_checa(atual, X)       → FS_E_PERMISSAO
      atual = busca_filho(atual, nome) ou FS_E_NAO_EXISTE
  devolve atual
```

Variante `resolve_pai` devolve o diretório pai + o último nome (para criar/remover/renomear).

### 6.2 Escrita (`echo "..." > arq`)

```
1. resolve_pai → (dir, nome); valida nome
2. existe?  não → cria (passo 6.3)   sim → é diretório? FS_E_EH_DIRETORIO
3. perm_checa(arq, W)                     senão FS_E_PERMISSAO
4. calcula blocos necessários = ceil(len / TAM_BLOCO)
5. cabe? (n_livres + blocos já do arquivo ≥ necessários, e ≤ máximo do inode)
                                          senão FS_E_SEM_ESPACO  (nada é alterado)
6. libera blocos antigos (">") ou continua do último (">>")
7. aloca blocos livres no bitmap, copia pedaços de TAM_BLOCO, preenche diretos[]/indireto
8. atualiza tamanho, tipo, modificado; acessado
```

A checagem do passo 5 **antes** de mexer em qualquer coisa garante que uma falha não deixa o arquivo
pela metade (robustez).

### 6.3 Criação (`touch`, `mkdir`)

```
resolve_pai → (dir, nome) → valida nome → perm_checa(dir, W|X)
→ já existe? (touch: só atualiza datas; mkdir: FS_E_JA_EXISTE)
→ inode_novo(tipo, dono=atual, grupo=atual.gid, perm=padrão & ~umask)
→ entrada_nova(nome, inode) → insere na lista de filhos de dir
```

### 6.4 Remoção (`rm`) e ciclo de vida do inode

```
resolve_pai → perm_checa(pai, W|X) → é diretório e sem -r? FS_E_EH_DIRETORIO
→ desliga a entrada da lista do pai, free(entrada)
→ inode->n_links--
→ se n_links == 0: disco_libera(blocos), tabela.v[id] = NULL, free(inode)
```

### 6.5 `cp` × `mv` × `ln` (ponto didático para o vídeo)

| | nova entrada | novo inode | novos blocos |
| --- | --- | --- | --- |
| `cp` | sim | sim | sim (cópia dos dados) |
| `mv` | move a mesma entrada | não | não |
| `ln` | sim | não (`n_links++`) | não |

`ls -i` antes e depois mostra isso, igual ao Linux.

---

## 7. Gestão de memória (parte do 40%)

- **Dono único** de cada alocação:
  - `Entrada` → pertence ao diretório pai (lista de filhos);
  - `FCB` → pertence à `TabelaInodes`; só é liberado quando `n_links == 0`;
  - blocos do disco → array estático, liberados só no bitmap.
- `fs_destroi()` desce a árvore em pós-ordem liberando entradas e, ao zerar links, os inodes.
- Toda função que faz `malloc` checa `NULL` e devolve `FS_E_SEM_MEMORIA`.
- Strings com tamanho fixo (`NOME_MAX`) + `snprintf`/`strncpy` → sem overflow.
- `make memcheck` roda todos os roteiros com
  `valgrind --leak-check=full --error-exitcode=1`. **Critério de pronto: 0 leaks, 0 erros.**
- Compilação com `-Wall -Wextra -Werror -std=c11 -g` e, em desenvolvimento,
  `-fsanitize=address,undefined`.

---

## 8. Conceito teórico → onde está no código

Esta tabela é o esqueleto da seção "discussão explícita dos conceitos" exigida em 5.5.

| Conceito (enunciado 5.5) | Onde aparece | Como será demonstrado |
| --- | --- | --- |
| Arquivo e seus atributos | `FCB` em `inode.h` | `stat arq` mostra nome, id, tipo, tamanho, dono, permissões, 3 datas |
| Operações (criar, escrever, ler, reposicionar, excluir, truncar) | `fs_touch`, `fs_escreve`, `fs_le`, `fs_le_offset`, `fs_remove`, `fs_trunca` | um comando para cada uma |
| FCB e inode | `FCB` separado de `Entrada`; `TabelaInodes` | `ls -i`, `ln`, `mv` preservando o id |
| Diretório em árvore (eficiência, nomeação, agrupamento) | `Entrada` com `pai/filhos/irmao` | mesmo nome em pastas diferentes; busca só no diretório certo; `tree` |
| Proteção RWX + owner/group/others, `chmod` com bitmask | `perm.c` (`perm_checa`, `>> desloc & op`) | sessão com `su alice`/`su bob`/`su carol` negando e permitindo |
| Alocação de blocos | `disco.c`, `diretos[]`/`indireto` | `blocos arq` e `df` antes/depois de `echo`, `truncate`, `rm` |
| Contínua × encadeada × indexada | texto + diagrama no README | comparação teórica; indexada no código |

---

## 9. Comparação com o Linux real

Script `comparacao.sh` executa a mesma sequência no bash real (em um diretório temporário) e no
simulador, e o README mostra as duas saídas lado a lado. Exemplo do que se espera mostrar:

```
mfs> echo "ola mundo" > a.txt          $ echo "ola mundo" > a.txt
mfs> ln a.txt b.txt                    $ ln a.txt b.txt
mfs> ls -li                            $ ls -li
  3 -rw-r--r-- 2 alice dev 10 a.txt      1234 -rw-r--r-- 2 alice dev 10 a.txt
  3 -rw-r--r-- 2 alice dev 10 b.txt      1234 -rw-r--r-- 2 alice dev 10 b.txt
mfs> chmod 600 a.txt                   $ chmod 600 a.txt
mfs> su bob                            $ sudo -u bob cat a.txt
mfs> cat a.txt                         cat: a.txt: Permission denied
cat: a.txt: permissão negada
```

Diferenças conhecidas (documentar): diretórios não ocupam blocos; não há links simbólicos (a menos que
sobre tempo); `exec` não executa de verdade; usuários fixos.

---

## 10. Testes e demonstração

- `testes/NN_nome.txt` + `testes/NN_nome.esperado`; `make teste` roda cada roteiro e faz `diff`.
  Para a saída ser comparável, datas são impressas fixas quando `MFS_DATA_FIXA=1`.
- Casos mínimos:
  1. árvore: `mkdir -p`, `cd`, `cd ..`, `pwd`, caminho absoluto/relativo;
  2. arquivos: `touch`, `echo >`/`>>`, `cat`, `read`, `truncate`, `cp`, `mv`, `rm`;
  3. permissões: dono/grupo/outros, `chmod` octal e simbólico, diretório sem `x`, root;
  4. blocos: arquivo de 1 bloco, de vários, usando o indireto, disco cheio (falha sem corromper);
  5. erros: não existe, já existe, nome inválido, `rmdir` não vazio, `rm` em diretório, `mv` dir para dentro de si;
  6. links: `ln`, `rm` de um dos nomes mantém os dados.
- `demo.txt` (roteiro do vídeo, ~10 min): árvore → FCB/`stat` → operações → `cp`/`mv`/`ln` com
  `ls -i` → permissões com troca de usuário → blocos com `blocos`/`df` → `make memcheck` limpo.

---

## 11. Ordem de implementação

| Fase | Entrega | Critério coberto |
| --- | --- | --- |
| 1 | `erros.h`, `inode`, árvore + `mkdir`/`cd`/`pwd`/`ls`/`tree`, shell básico | 3.1 |
| 2 | `touch`, `echo`, `cat`, `rm`, `cp`, `mv`, `stat` (conteúdo ainda em `malloc` simples) | 3.2 |
| 3 | usuários, `su`, `perm_checa` em todas as operações, `chmod`, `chown`, `ls -l` | 3.3 |
| 4 | `disco.c`: troca o conteúdo em `malloc` por blocos indexados; `blocos`, `df`, `truncate`, `read` | 3.4 |
| 5 | `ln`, `exec`, `umask`, `chmod` simbólico | bônus |
| 6 | testes + `make memcheck` zerado | 40% |
| 7 | README final (teoria ↔ código, comparação Linux), vídeo | 30% + 10% |

Cada fase termina compilando sem warnings e passando no valgrind — não acumular leaks para o final.

---

## 12. Fora de escopo (decisões conscientes)

Persistência em arquivo real, concorrência/threads, links simbólicos, journaling, cache de blocos,
diretórios ocupando blocos no disco. Podem ser citados no README final como extensões possíveis.
