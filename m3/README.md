# M3 — Mini-sistema de arquivos em memória (em C)

Simulador de um sistema de arquivos Unix-like que roda **inteiro em memória**. Ele tem:

- árvore de diretórios feita com `struct` e ponteiros;
- um **FCB/inode** para cada arquivo;
- permissões **RWX para dono, grupo e outros**, verificadas com bitmask em toda operação;
- um **disco simulado** (`char[]`) dividido em blocos, com **alocação indexada**.

Nenhuma operação toca o sistema de arquivos real do Linux. Tudo acontece nas estruturas do programa.

São 2 formas de uso, que executam exatamente o mesmo código:

- **menu numerado**: a navegação foi inspirada em [eldanielG/mini_fs_linux](https://github.com/eldanielG/mini_fs_linux);
- **terminal** com comandos iguais aos do Linux (`ls -l`, `chmod 640`, `echo "x" > arq`...).

| Arquivo | O que é |
| --- | --- |
| `src/main.c` | menu interativo e escolha do modo |
| `src/shell.c/.h` | terminal: separa a linha em argumentos, despacha os comandos e imprime resultados e erros |
| `src/fs.c/.h` | **árvore de diretórios**, resolução de caminhos e operações (mkdir, cp, mv, rm, ln...) |
| `src/inode.c/.h` | **FCB/inode**, tabela de inodes e **alocação indexada** de blocos |
| `src/perm.c/.h` | usuários, grupos e **verificação RWX com bitmask**; `chmod` octal e simbólico |
| `src/disco.c/.h` | **disco simulado** e **bitmap** de blocos livres |
| `src/erros.c/.h` | `enum FsErro` e as mensagens de erro |
| `demo.txt` | roteiro da apresentação (um comando por linha) |
| `Makefile` | `make`, `make demo`, `make memcheck`, `make clean` |

---

## 1. Como compilar e executar

Funciona em Linux, WSL ou GitHub Codespaces. Só precisa de `gcc` e `make` (e de `valgrind` para o `make memcheck`).

```bash
cd m3
make
```

```bash
./mfs
```

O comando acima abre o **menu numerado**.

```bash
./mfs -t
```

O comando acima abre direto o **terminal**, com comandos estilo Linux.

```bash
make demo
```

Roda o roteiro da apresentação (`demo.txt`).

```bash
make memcheck
```

Roda o mesmo roteiro no valgrind e exige 0 erros e 0 leaks.

O terminal também aceita um roteiro pela entrada padrão (`./mfs -t < roteiro.txt`). Nesse caso cada comando é repetido depois do prompt, para a saída ficar legível.

### Menu

```
=== Mini File System (M3) ===   [alice@mfs:/home/alice$ ]
  1. Criar diretório (mkdir)                13. Remover arquivo (rm)
  2. Mudar de diretório (cd)                14. Remover diretório vazio (rmdir)
  3. Listar diretório (ls -l)               15. Criar hard link (ln)
  4. Árvore de diretórios (tree)            16. Alterar permissões (chmod)
  5. Criar arquivo (touch)                  17. Alterar dono (chown, só root)
  6. Escrever no arquivo (echo >)           18. Ver metadados / FCB (stat)
  7. Acrescentar ao arquivo (echo >>)       19. Ver blocos do arquivo (blocos)
  8. Ler arquivo (cat)                      20. Uso do disco (df)
  9. Ler a partir de uma posição (read)     21. Tabela de inodes (inodes)
 10. Truncar arquivo (truncate)             22. Trocar de usuário (su)
 11. Copiar (cp)                            23. Executar programa (exec)
 12. Mover/renomear (mv)                    24. Abrir terminal (comandos Linux)
  0. Sair
```

Cada opção pergunta os dados, monta o comando equivalente e chama o mesmo interpretador do terminal. Por isso as mensagens de erro e as permissões são idênticas nos dois modos.

Em relação ao trabalho de referência, a leitura usa `fgets`, então aceita espaços e não estoura buffer, e não é preciso usar `_` no lugar de espaço. Na opção 24, o comando `menu` volta ao menu.

### Estado inicial

| Usuário | uid | Grupo (gid) | Observação |
| --- | --- | --- | --- |
| `root` | 0 | root (0) | ignora r/w; só executa se houver algum bit `x` |
| `alice` | 1000 | dev (100) | **usuário inicial**, começa em `/home/alice` |
| `bob` | 1001 | dev (100) | mesmo grupo da alice |
| `carol` | 1002 | visitantes (200) | cai em "outros" nos arquivos do grupo dev |

Os diretórios iniciais são `/home/{alice,bob,carol}` (cada um do seu dono, modo 755) e `/tmp` (777). A troca de usuário é feita com `su` e não pede senha.

### Comandos do terminal

| Comando | O que faz | Linux |
| --- | --- | --- |
| `pwd`, `cd [dir]` | mostra o diretório atual / navega (sem argumento vai para o home) | igual |
| `ls [-l] [-i] [caminho...]` | lista; `-l` detalhado, `-i` mostra o número do inode | `ls -F` |
| `tree [dir]` | desenha a árvore | `tree` |
| `mkdir [-p] dir...`, `rmdir dir...` | cria / remove diretório vazio | igual |
| `touch arq...` | cria vazio ou atualiza as datas | igual |
| `echo [-e] [-n] texto > arq` / `>> arq` | escreve / acrescenta (`-e` aceita `\n`, `\t`, `\xHH`) | igual |
| `cat arq...` | lê | igual |
| `read arq offset n` | **reposiciona** em `offset` e lê `n` bytes | `dd skip=… count=…` |
| `truncate -s tamanho arq` | **trunca** ou estende com zeros | igual |
| `cp origem destino` | copia: novo inode e novos blocos | igual (sem `-r`) |
| `mv origem destino` | move/renomeia: mesmo inode | igual |
| `ln origem novo` | **hard link**: mesmo inode, `links + 1` | igual (sem `-s`) |
| `rm [-r] [-f] caminho...` | remove nome; o inode é liberado quando os links chegam a 0 | igual |
| `stat caminho` | mostra o **FCB** inteiro, inclusive os ponteiros de bloco | igual |
| `chmod modo arq...` | `640`, `u+x`, `go-w`, `a=r`, `u=rw,g=r,o=` | igual |
| `chown usuario[:grupo] arq...` | troca dono/grupo (só root) | igual |
| `umask [octal]` | máscara de criação (padrão `0022`) | igual |
| `exec prog` ou `./prog` | executa um programa (`#!`); cada linha é um comando do simulador | `./prog` |
| `su [usuario]`, `whoami`, `id` | usuário atual | igual |
| `blocos arq` | mostra cada bloco lógico → bloco físico e o conteúdo | `filefrag -v` / `debugfs` |
| `df` | blocos usados/livres e mapa de bits do disco | `df` |
| `inodes` | tabela de inodes em uso | `debugfs -R ls` |
| `help`, `exit` | | |

---

## 2. Arquitetura

```
                 ┌───────────────────────────┐
  usuário ──────►│ main.c  (menu numerado)    │──┐ monta argv
                 └───────────────────────────┘  │
                 ┌───────────────────────────┐  │
  usuário ──────►│ shell.c (terminal)         │◄─┘  interpreta, despacha, imprime mensagens
                 └─────────────┬─────────────┘
                               │ fs_*(...) devolve FsErro (nunca imprime)
                 ┌─────────────▼─────────────┐
                 │ fs.c  árvore, caminhos,    │
                 │       operações            │
                 └──┬──────────┬──────────┬──┘
           ┌────────▼───┐ ┌────▼──────┐ ┌─▼──────────────┐
           │ perm.c     │ │ inode.c   │ │ disco.c         │
           │ RWX bitmask│ │ FCB +     │─►│ char[][64] +   │
           │ usuários   │ │ blocos    │ │ bitmap livre    │
           └────────────┘ └───────────┘ └─────────────────┘
```

Regras que deixam o código modular:

- **As dependências só descem.** `disco` não sabe o que é arquivo; `inode` não sabe o que é diretório; `fs` não sabe o que é terminal.
- **Só o shell imprime.** Todas as outras camadas devolvem um `FsErro`.
- **Uma única função decide o acesso:** `perm_checa`, chamada por todas as operações.
- **Não há variáveis globais soltas.** O estado inteiro (disco, tabela de inodes, raiz, diretório atual, usuário atual, umask) fica em um `Fs`, criado com `calloc` e passado por ponteiro.

### Exemplo: `cat /home/alice/a.txt`

1. O `shell` separa os argumentos e chama `fs_le(fs, "/home/alice/a.txt", ...)`.
2. `fs_resolve` parte da raiz e, para cada diretório (`/`, `home`, `alice`), confere se é diretório e se o usuário tem **x**. Depois procura o próximo nome na lista de filhos.
3. Achou a `Entrada` de `a.txt`, que aponta para o inode. É diretório? Então devolve `FS_E_EH_DIRETORIO`. O usuário tem **r**? Se não, devolve `FS_E_PERMISSAO`.
4. `inode_le` converte cada posição em bloco lógico e depois em bloco físico (pelos ponteiros diretos ou pelo bloco indireto) e copia os bytes do disco para um buffer alocado com `malloc`.
5. O shell imprime o conteúdo e faz `free`. Se algo falhou, imprime `cat: /home/alice/a.txt: permissão negada`.

---

## 3. Conceitos teóricos e como estão implementados

### 3.1 Arquivo e seus atributos

Um arquivo é um **tipo abstrato de dados**: uma sequência de bytes com nome e atributos. O sistema de arquivos esconde o disco e faz o **mapeamento lógico → físico**: o usuário pede "o byte 100 de `a.txt`" e o simulador encontra o bloco e o deslocamento.

Os atributos ficam no FCB (seção 3.3):

| Atributo (teoria) | Campo no simulador |
| --- | --- |
| nome | `Entrada.nome` (fica no diretório, como no Unix) |
| identificador | `FCB.id`: número do inode |
| tipo | `FCB.tipo` (`enum TipoArquivo`): diretório, texto, numérico, binário, programa |
| localização | `FCB.diretos[8]` + `FCB.indireto` |
| tamanho | `FCB.tamanho` (bytes) e `FCB.n_blocos` |
| proteção | `FCB.perm` (9 bits), `FCB.dono`, `FCB.grupo` |
| datas | `FCB.criado`, `FCB.modificado`, `FCB.acessado` (`time_t`) |

O **tipo** é deduzido do conteúdo a cada escrita (`tipo_infere` em `inode.c`):

- começa com `#!` → **programa**;
- contém bytes de controle → **binário**;
- só dígitos e separadores → **numérico**;
- caso contrário → **texto**.

O `stat` mostra o tipo junto com os outros atributos.

### 3.2 Operações com arquivos

As 6 operações básicas da teoria e onde estão:

| Operação | Função | Comando | O que acontece |
| --- | --- | --- | --- |
| criar | `fs_touch` / `cria_no` | `touch` | aloca um inode (`inode_novo`) e insere a entrada no diretório pai |
| escrever | `fs_escreve` → `inode_escreve` | `echo >`, `echo >>` | aloca ou libera blocos e copia os bytes bloco a bloco |
| ler | `fs_le` → `inode_le` | `cat` | percorre os blocos e atualiza `acessado` |
| reposicionar | `fs_le(offset, n)` | `read arq offset n` | começa a leitura no bloco `offset / 64`, posição `offset % 64` |
| excluir | `fs_rm` | `rm` | remove o nome; `n_links--`; com 0 links libera inode e blocos |
| truncar | `fs_trunca` → `inode_trunca` | `truncate -s` | diminuindo, libera blocos; aumentando, completa com zeros |

As operações compostas são montadas com essas primitivas:

| | nova entrada? | novo inode? | novos blocos? |
| --- | --- | --- | --- |
| `cp` | sim | **sim** | **sim** (dados copiados) |
| `mv` | a mesma entrada muda de lugar | não | não |
| `ln` | sim | não (`n_links++`) | não |

Isso fica visível no `ls -i`. Saída real do `make demo`:

```
alice@mfs:/home/alice$ cp leia.txt copia.txt
alice@mfs:/home/alice$ mv copia.txt projetos/renomeado.txt
alice@mfs:/home/alice$ ln leia.txt atalho.txt
alice@mfs:/home/alice$ ls -li . projetos
.:
 10 -rw-r--r--  2 alice  dev           27 2025-11-19 14:00 atalho.txt     <- mesmo inode (10), 2 links
 10 -rw-r--r--  2 alice  dev           27 2025-11-19 14:00 leia.txt
  7 drwxr-xr-x  3 alice  dev            0 2025-11-19 14:00 projetos/

projetos:
 11 -rw-r--r--  1 alice  dev           27 2025-11-19 14:00 renomeado.txt  <- a cópia ganhou inode 11; o mv manteve
  8 drwxr-xr-x  3 alice  dev            0 2025-11-19 14:00 so/
```

### 3.3 File Control Block (FCB) e o inode simulado

```c
typedef struct {
    int         id;                 /* número do inode */
    TipoArquivo tipo;
    size_t      tamanho;
    int         dono, grupo;        /* uid, gid */
    uint16_t    perm;               /* rwxrwxrwx, ex.: 0754 */
    time_t      criado, modificado, acessado;
    int         n_links;            /* quantos nomes apontam para este inode */
    int         n_blocos;
    int         diretos[N_DIRETOS]; /* 8 ponteiros diretos para blocos */
    int         indireto;           /* bloco que guarda mais 16 ponteiros */
} FCB;
```

- **O nome não fica no FCB.** Ele fica na `Entrada` do diretório, que é só um par `nome → FCB*`, exatamente como no Unix. Com isso:
  - `mv` não muda o inode;
  - `ln` cria um segundo nome para o mesmo inode (bônus de links);
  - `rm` apaga um nome, e os dados só são liberados quando `n_links` chega a 0 (`desliga_inode` em `fs.c`).
- **Tabela de inodes.** É um vetor dinâmico `FCB **v` que cresce com `realloc` (dobra de tamanho), e o índice é o número do inode. Um número liberado é reaproveitado, como no Linux. O comando `inodes` mostra a tabela.

### 3.4 Estrutura de diretórios em árvore

```c
typedef struct Entrada {
    char            nome[NOME_MAX + 1];
    FCB            *inode;
    struct Entrada *pai;     /* ".."  (na raiz aponta para ela mesma) */
    struct Entrada *filhos;  /* primeiro filho */
    struct Entrada *irmao;   /* próximo do mesmo diretório */
} Entrada;
```

É uma árvore n-ária na forma **primeiro filho / próximo irmão**: cada diretório tem uma lista encadeada de filhos, mantida em ordem alfabética. O ponteiro `pai` permite:

- `cd ..`;
- montar o `pwd` subindo até a raiz;
- impedir `mv dir dir/sub` (diretório dentro dele mesmo);
- impedir `rmdir` do diretório atual ou de um ancestral dele (o `cwd` ficaria apontando para memória liberada).

Caminhos absolutos (`/home/alice`) começam na raiz; relativos (`docs/a.txt`) começam no diretório atual. `.` e `..` são resolvidos na hora, não são entradas guardadas.

**Vantagens da árvore em relação a diretório de nível único ou de dois níveis:**

- **Eficiência:** a busca por um nome percorre só a lista do diretório certo em cada nível do caminho, não todos os arquivos do sistema.
- **Nomeação:** o mesmo nome pode existir em diretórios diferentes (`/home/alice/notas` e `/home/bob/notas`), e cada usuário tem seu espaço de nomes.
- **Agrupamento:** arquivos relacionados ficam juntos (`projetos/so/m3`), e a permissão de um diretório protege tudo o que está abaixo dele (sem `x` no diretório não se chega aos arquivos).

Com o `ln`, a estrutura passa a ser um **grafo acíclico** (um arquivo em dois diretórios). Hard link para diretório é proibido, como no Linux, justamente para não criar ciclos.

### 3.5 Proteção: RWX, dono/grupo/outros e `chmod` com bitmask

As permissões são 9 bits, no mesmo layout do Unix:

```
            dono   grupo  outros
            r w x  r w x  r w x
    bit:    8 7 6  5 4 3  2 1 0         chmod 754 = 111 101 100 = rwxr-xr--
```

A verificação (`perm.c`) é feita em **todas** as operações, antes de qualquer alteração. É o "bloqueio obrigatório" do enunciado: não existe caminho no código que leia ou escreva sem passar por ela.

```c
bool perm_checa(const Usuario *u, const FCB *f, int op)   /* op = P_R|P_W|P_X */
{
    if (u->uid == 0) ...                     /* root: tudo, mas exec só com algum x */
    int desloc = (u->uid == f->dono)  ? 6    /* escolhe UMA classe */
               : (u->gid == f->grupo) ? 3
               :                        0;
    return ((f->perm >> desloc) & op) == op; /* desloca e testa com AND */
}
```

Na conversão para texto (`perm_str`), cada bit é testado com `f->perm & (0400 >> i)`. O `chmod` aceita:

- **octal**: cada dígito são 3 bits, `v = (v << 3) | digito`;
- **simbólico**:
  - `u+x` → `perm |= 0100`;
  - `go-w` → `perm &= ~0022`;
  - `u=rw` → `perm = (perm & ~0700) | 0600`.

O `umask` (padrão 022) é retirado na criação: `0666 & ~umask` para arquivos e `0777 & ~umask` para diretórios.

**O que cada operação exige**, igual ao Linux:

| Operação | Exige |
| --- | --- |
| usar um diretório num caminho, `cd` | `x` no diretório |
| `ls` | `r` no diretório |
| `cat`, `read`, `cp` (origem) | `r` no arquivo |
| `echo >`, `echo >>`, `truncate` | `w` no arquivo |
| criar ou apagar um nome (`touch`, `mkdir`, `rm`, `mv`, `ln`) | `w` **e** `x` no diretório **pai** (não no arquivo!) |
| `exec` / `./prog` | `x` e `r` no arquivo (o script precisa ser lido) |
| `chmod` | ser o dono ou root |
| `chown` | ser root |

Detalhes que o simulador reproduz:

- O **dono usa só os bits de dono**. Com `chmod 077`, a própria dona é negada, mesmo que "outros" tenham `rwx`.
- Com **`chmod 711` num diretório**, os outros não conseguem listá-lo (sem `r`), mas abrem um arquivo dentro dele se souberem o nome (têm `x`).
- **Apagar um arquivo depende do diretório.** Bob apaga `/tmp/do_bob` mesmo com o arquivo em `000`, porque `/tmp` é `777`.
- O **root** lê e escreve qualquer coisa, mas só executa se houver algum bit `x`.

Saída real do `make demo`:

```
alice@mfs:/home/alice$ chmod 640 salario.txt
alice@mfs:/home/alice$ su bob                      # bob é do grupo dev: r--
bob@mfs:/home/alice$ cat /home/alice/salario.txt
salario: 1000
bob@mfs:/home/alice$ echo "hack" >> /home/alice/salario.txt
echo: /home/alice/salario.txt: permissão negada
bob@mfs:/home/alice$ su carol                      # carol é "outros": ---
carol@mfs:/home/alice$ cat /home/alice/salario.txt
cat: /home/alice/salario.txt: permissão negada
```

### 3.6 Alocação de blocos

O disco é `char dados[256][64]`: 256 blocos de 64 bytes (16 KiB). O bloco é pequeno de propósito, para um texto curto já ocupar vários blocos.

**Gerência do espaço livre:** um **bitmap** (`livre[256]`) e um contador. `disco_aloca` usa *first-fit* e zera o bloco; `disco_libera` só marca o bloco como livre de novo.

**Comparação dos 3 métodos da teoria:**

| Método | Como funciona | Vantagem | Desvantagem |
| --- | --- | --- | --- |
| Contígua | o FCB guarda início + quantidade; os blocos são seguidos | leitura sequencial e acesso direto simples | **fragmentação externa**; o arquivo não cresce se o vizinho estiver ocupado |
| Encadeada | cada bloco guarda o número do próximo; o FCB guarda o primeiro | sem fragmentação externa, cresce fácil | para chegar ao bloco *n* é preciso ler os *n − 1* anteriores; um ponteiro corrompido perde o resto |
| **Indexada** (implementada) | o FCB tem uma lista de índices de blocos | **acesso direto** e **sem fragmentação externa** | gasta espaço com índices e tem tamanho máximo |

O simulador implementa a **indexada no estilo do inode do ext2**: **8 ponteiros diretos** no FCB e **1 bloco indireto simples**. O bloco indireto é um bloco do disco que guarda mais 16 números de bloco (64 bytes ÷ 4 bytes por `int32`).

- Tamanho máximo do arquivo: (8 + 16) × 64 = **1536 bytes**.
- Passar disso dá "arquivo grande demais".
- Ficar sem blocos livres dá "não há espaço no disco simulado".

Nos dois casos o arquivo **não é alterado**: `redimensiona` confere se cabe antes de mexer em qualquer coisa. `cp` e `echo` para um arquivo novo também desfazem a criação se a escrita falhar.

Mapeamento lógico → físico (`inode.c`):

```
byte p  →  bloco lógico i = p / 64,  posição p % 64
bloco lógico i < 8   →  f->diretos[i]
bloco lógico i >= 8  →  (i - 8)-ésimo int32 dentro do bloco f->indireto
```

Saída real (`make demo`): 8 blocos de 64 bytes enchem os ponteiros diretos, e o 9º bloco força a alocação do bloco de índices.

```
alice@mfs:/home/alice$ blocos grande
grande: inode 15, 544 bytes em 9 bloco(s) de 64 bytes (alocação indexada)
  lógico  físico  bytes  conteúdo
  d[0]         6     64  "Bloco 1: um texto com exatamente sessenta e quatro caracteres..\n"
  ...
  d[7]        13     64  "Bloco 8: um texto com exatamente sessenta e quatro caracteres..\n"
  ind[0]      15     32  "Bloco 9 usa o ponteiro indireto\n"
  bloco de índices (indireto) = 14 -> [15]
alice@mfs:/home/alice$ df
Disco simulado: 256 blocos de 64 bytes (16384 bytes)
Usados: 16  Livres: 240  Uso: 6%  Inodes em uso: 15
Mapa de bits (. livre  # dados  I índice):
  000 ##############I#................................................
```

A indexada também mostra sua vantagem sobre a contígua. Depois de apagar um arquivo, o disco fica com um buraco. Quando outro arquivo cresce, ele usa blocos espalhados, sem precisar de espaço contíguo, e o `stat` mostra algo como:

```
  Diretos: [3 4 2 5 6 7 8 9]  Indireto: 1
```

---

## 4. Tratamento de erros

Todos os erros são valores de `enum FsErro` (`erros.h`). O shell os mostra no formato do Linux, `comando: alvo: mensagem`:

| Situação | Mensagem |
| --- | --- |
| caminho inexistente | `cat: nada: arquivo ou diretório não encontrado` |
| sem permissão | `cat: /home/alice/nota.txt: permissão negada` |
| nome inválido (vazio, > 32 caracteres, espaço, `/`) | `mkdir: com espaco: nome inválido (...)` |
| já existe | `mkdir: /home/alice: arquivo já existe` |
| tipo errado | `cd: ok: não é um diretório` / `cat: /home: é um diretório` |
| `rmdir` com conteúdo | `rmdir: d: diretório não vazio` |
| remover o diretório atual | `rmdir: ...: diretório em uso (é o diretório atual ou um ancestral dele)` |
| disco cheio / arquivo grande | `cp: g: não há espaço no disco simulado` |
| `chmod` de arquivo alheio | `chmod: nota.txt: operação não permitida` |
| modo inválido | `chmod: abc: argumento inválido` |
| uso errado | `uso: cp origem destino` |
| aspas abertas, comando inexistente, linha longa | mensagens próprias, sem derrubar o programa |
| programa que chama a si mesmo | `exec: ./loop: limite de 8 programas aninhados atingido` |

## 5. Gerenciamento de memória

- **Cada alocação tem um único responsável por liberá-la:**
  - a `Entrada` pertence ao diretório pai;
  - o `FCB` pertence à tabela de inodes e só é liberado quando `n_links == 0`;
  - o buffer devolvido por `fs_le` é liberado por quem chamou.
- `fs_destroi` desce a árvore em pós-ordem (`entrada_destroi`) e depois libera a tabela.
- Todo `malloc`, `calloc` e `realloc` é verificado. Em falha, devolve `FS_E_SEM_MEMORIA` e desfaz o que já tinha feito.
- **Entrada limitada em tamanho:** `fgets` com tamanho, nomes de no máximo 32 caracteres e caminhos de no máximo 255.
- O `Makefile` compila com `-std=c11 -Wall -Wextra -Wpedantic`, sem nenhum warning.

Resultado de `make memcheck`, que roda o roteiro da apresentação no valgrind (`--leak-check=full --errors-for-leak-kinds=all`):

```
valgrind: 0 erros, 0 leaks
```

## 6. Comparação com o Linux real

Os mesmos cenários foram rodados no bash de um Linux (Ubuntu), com uids 1000/1001/1002 e grupos 100/200 como no simulador. A tabela compara as saídas lado a lado:

| Cenário | Linux real | Simulador |
| --- | --- | --- |
| `ln a.txt b.txt; ls -li` | `680088 -rw-r--r-- 2 ... a.txt` e `680088 ... b.txt` | `7 -rw-r--r-- 2 ... a.txt` e `7 ... b.txt` |
| `mv b.txt c.txt` | o inode continua 680088 | o inode continua 7 |
| `cp a.txt d.txt` | `d.txt` ganha um inode novo (680098) | `d.txt` ganha um inode novo (8) |
| `chmod 640 a.txt`, bob (mesmo grupo) lê | `ola mundo` | `ola mundo` |
| bob tenta `echo x >> a.txt` | `Permission denied` | `permissão negada` |
| bob tenta `chmod 777 a.txt` | `Operation not permitted` | `operação não permitida` |
| carol (outros) tenta ler | `Permission denied` | `permissão negada` |
| dona com `chmod 077` tenta ler | `Permission denied` | `permissão negada` |
| diretório 711: `ls p` / `cat p/f` / `touch p/novo` | negado / `y` / negado | negado / `y` / negado |
| `chmod u+x,go-w` | `-rwxr--r--` | `-rwxr--r--` |
| `cat nada` | `No such file or directory` | `arquivo ou diretório não encontrado` |
| `ln p plink` (p é diretório) | `hard link not allowed for directory` | `é um diretório` |

**Diferenças conhecidas** (simplificações conscientes):

- Diretórios não ocupam blocos no disco simulado; o `ls -l` mostra tamanho 0, e não 4096.
- O `ls` sem `-l` mostra `/` depois dos diretórios (como `ls -F`) e as datas saem como `--time-style=long-iso`.
- Não há links simbólicos, `cp -r`, setuid/sticky bit nem senha no `su`.
- O `exec` "interpreta" o programa: cada linha é um comando do próprio simulador, rodado com as permissões de quem o executou. Não executa código de verdade.
- O `cat` acrescenta uma quebra de linha se o arquivo não terminar com uma, para o prompt não ficar colado.

## 7. Créditos

- A ideia de menu numerado veio de [eldanielG/mini_fs_linux](https://github.com/eldanielG/mini_fs_linux). Aquele projeto usa o sistema de arquivos real; aqui toda a lógica foi reescrita sobre estruturas simuladas.
- Esboço da arquitetura e enunciado: disciplina de Sistemas Operacionais, Univali.
