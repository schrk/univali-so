# minidb-ipc

Avaliação M1 — Sistemas Operacionais (Univali) — IPC, threads e paralelismo.

Simulador do núcleo de um gerenciador de requisições a banco de dados: um **processo cliente**
produz requisições em **memória compartilhada POSIX**, um **processo servidor** as consome com um
**pool de threads** e aplica cada operação sobre uma tabela protegida por um **lock de
leitores-escritores** (`pthread_mutex_t` + `sem_t`). As respostas voltam por um **FIFO nomeado** e
são registradas em log.

> **Status:** desenho fechado (fluxo + estrutura). Implementação ainda não iniciada.

## Documentação

| Arquivo | Conteúdo |
| --- | --- |
| [`docs/arquitetura.html`](docs/arquitetura.html) | Documento completo: fluxo ilustrado, árvore de arquivos, contrato, conformidade com o enunciado e divisão do trio |
| [`docs/fluxo.svg`](docs/fluxo.svg) / `.png` | Figura 1 — fluxo do sistema |
| [`docs/leitores-escritores.svg`](docs/leitores-escritores.svg) / `.png` | Figura 2 — leitura concorrente vs. escrita exclusiva |

## Fluxo do sistema

![Fluxo do sistema](docs/fluxo.png)

1. O cliente lê a carga e o `parser` converte `SELECT nome WHERE id=5` em uma `Requisicao` de tamanho fixo.
2. A thread produtora faz `sem_wait(vazias)`, entra no `sem_mutex_ring`, copia a struct no slot e faz `sem_post(cheias)`.
3. A thread despachante do servidor faz `sem_wait(cheias)`, retira o slot e devolve espaço com `sem_post(vazias)`.
4. A requisição vai para a fila interna; a despachante volta imediatamente ao ring.
5. Os `N` workers do pool acordam na condvar e retiram tarefas em paralelo.
6. `SELECT` entra como leitor — vários workers leem a tabela ao mesmo tempo.
7. `INSERT`, `UPDATE` e `DELETE` entram como escritores — acesso exclusivo.
8. O worker monta a `Resposta` com status, dados e tempo de espera no lock.
9. A resposta é escrita no FIFO (uma escrita por vez, protegida por mutex) e replicada no log.
10. A thread receptora do cliente lê a resposta, confere contra a requisição e imprime.

## Conformidade com o enunciado

| Requisito obrigatório | Onde é atendido |
| --- | --- |
| Dois binários, processos de SO distintos | `src/servidor/main.cpp`, `src/cliente/main.cpp` |
| IPC real (não polling em arquivo) | `ipc/shm_ring.cpp` (requisições), `ipc/fifo_channel.cpp` (respostas) |
| Servidor com pool de threads | `servidor/pool_threads.cpp`, `servidor/fila_tarefas.cpp` |
| Tabela protegida por mutex/semáforo real | `servidor/rw_lock.cpp`, `servidor/banco.cpp` |
| INSERT, DELETE, SELECT, UPDATE por ID | `servidor/executor.cpp` |
| `struct Registro { int id; char nome[50]; }` | `common/registro.hpp` |
| Respostas em 2º canal IPC ou log | `ipc/fifo_channel.cpp` + `common/log.cpp` (os dois) |
| Resultados de simulação para o relatório | `scripts/benchmark.sh`, `servidor/metricas.cpp` |

## Estrutura de arquivos

```
minidb-ipc/
├── Makefile                       # gera bin/servidor e bin/cliente (-pthread -lrt)
├── README.md
├── .gitignore
│
├── include/
│   ├── common/                    # CONTRATO — travado antes de qualquer código
│   │   ├── registro.hpp           # struct Registro { int id; char nome[50]; }
│   │   ├── protocolo.hpp          # Op, Status, Requisicao, Resposta (POD, tam. fixo)
│   │   ├── config.hpp             # nomes de shm/FIFO/sem, N_SLOTS, N_THREADS
│   │   └── log.hpp                # log com timestamp e id da thread
│   ├── ipc/                       # CAMADA IPC — só objetos do kernel
│   │   ├── shm_region.hpp         # RAII: shm_open + ftruncate + mmap + unlink
│   │   ├── named_sem.hpp          # RAII: sem_open / sem_wait / sem_post / sem_unlink
│   │   ├── shm_ring.hpp           # ring buffer produtor-consumidor sobre a shm
│   │   └── fifo_channel.hpp       # RAII: mkfifo + open + read/write de Resposta
│   ├── servidor/                  # CONCORRÊNCIA — threads e exclusão mútua
│   │   ├── rw_lock.hpp            # leitores-escritores: pthread_mutex_t + sem_t
│   │   ├── banco.hpp              # tabela std::vector<Registro> guardada pelo rw_lock
│   │   ├── fila_tarefas.hpp       # fila bloqueante: mutex + pthread_cond_t
│   │   ├── pool_threads.hpp       # pthread_create / pthread_join dos N workers
│   │   ├── executor.hpp           # aplica INSERT / DELETE / SELECT / UPDATE
│   │   ├── persistencia.hpp       # carrega e salva data/banco.txt
│   │   └── metricas.hpp           # latência, throughput, espera no lock
│   └── cliente/
│       ├── parser.hpp             # "SELECT nome WHERE id=5" → Requisicao
│       └── gerador_carga.hpp      # carga de arquivo ou sintética (mix leitura/escrita)
│
├── src/                           # espelha include/, 1 .cpp por cabeçalho
│   ├── common/    protocolo.cpp · log.cpp
│   ├── ipc/       shm_region.cpp · named_sem.cpp · shm_ring.cpp · fifo_channel.cpp
│   ├── servidor/
│   │   ├── main.cpp               # BINÁRIO 1: cria canais, sobe o pool, despacha
│   │   └── rw_lock.cpp · banco.cpp · fila_tarefas.cpp · pool_threads.cpp
│   │       executor.cpp · persistencia.cpp · metricas.cpp
│   └── cliente/
│       ├── main.cpp               # BINÁRIO 2: thread produtora + thread receptora
│       └── parser.cpp · gerador_carga.cpp
│
├── data/
│   ├── banco.inicial.txt          # estado carregado na subida do servidor
│   ├── cargas/                    # basica.txt · so_leitura.txt · mista.txt · disputa_id.txt
│   └── logs/                      # servidor.log e cliente.log (gerados)
│
├── scripts/
│   ├── demo.sh                    # sobe o servidor, roda 1 cliente, mostra o log
│   ├── multi_cliente.sh           # vários clientes disputando o mesmo ring
│   ├── benchmark.sh               # varia N_THREADS (1,2,4,8) e o mix de operações
│   └── plot.py                    # gráficos do relatório a partir dos CSVs
│
├── tests/
│   ├── test_ipc.cpp               # ring cheio, ring vazio, cliente antes do servidor
│   ├── test_rw_lock.cpp           # N leitores juntos, escritor exclusivo, sem starvation
│   └── test_corrida.cpp           # mesma carga SEM lock: exibe a condição de corrida
│
├── resultados/                    # CSVs e gráficos citados no relatório
└── docs/
    ├── arquitetura.html           # documento de arquitetura
    ├── fluxo.svg / .png           # figura 1
    ├── leitores-escritores.svg / .png  # figura 2
    └── relatorio/                 # artigo em PDF (ABNT) + fontes
```

## Contrato entre os dois binários

Structs POD de tamanho fixo, sem ponteiros e sem `std::string`: um slot do ring e uma mensagem do
FIFO são cópias byte a byte, então o mesmo cabeçalho serve aos dois processos.

```cpp
// include/common/protocolo.hpp
enum class Op     : uint8_t { INSERT, DELETE, SELECT, UPDATE };
enum class Status : uint8_t { OK, NAO_ENCONTRADO, ID_DUPLICADO, TABELA_CHEIA, MALFORMADA };

struct Requisicao {   // cliente → servidor, via ring na memória compartilhada
    uint32_t id_req;      // correlaciona requisição e resposta
    int32_t  pid_cliente; // permite vários clientes no mesmo ring
    Op       op;
    int32_t  id;          // chave do registro
    char     nome[50];    // carga útil de INSERT / UPDATE
};

struct Resposta {     // servidor → cliente, via FIFO nomeado
    uint32_t id_req;
    Status   status;
    Registro registro;    // preenchido em SELECT
    uint64_t us_no_lock;  // tempo esperando o lock — alimenta as métricas
    uint32_t worker;      // qual thread do pool atendeu
};
```

## Divisão do trio

| Frente | Arquivos | Apresenta |
| --- | --- | --- |
| 1 — IPC | `ipc/*`, `tests/test_ipc.cpp` | `shm_open`, `mmap`, `sem_open`, `mkfifo` |
| 2 — Concorrência | `servidor/rw_lock`, `banco`, `fila_tarefas`, `pool_threads`, `executor` | seção crítica, condição de corrida, deadlock evitado |
| 3 — Cliente e medição | `cliente/*`, `metricas`, `scripts/` | resultados e escalabilidade por nº de threads |

O trio fecha junto `common/protocolo.hpp` e `common/config.hpp` antes de qualquer implementação.

## Sequência de implementação

1. `common/` + `Makefile`: dois binários que compilam e imprimem versão.
2. `ipc/`: cliente envia uma requisição, servidor imprime — sem banco ainda.
3. `banco` + `executor` com uma única thread: as quatro operações corretas.
4. `fila_tarefas` + `pool_threads`: N workers, ainda com mutex simples.
5. `rw_lock`: leitura compartilhada e escrita exclusiva no lugar do mutex único.
6. FIFO de respostas, log e persistência fechando o ciclo até o cliente.
7. `tests/` + `benchmark.sh`: medições, CSVs e gráficos do relatório.
8. Relatório em PDF (ABNT) e ensaio da apresentação com o repositório público.
