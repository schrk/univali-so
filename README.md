# Banco de dados com IPC e threads

Avaliação M1 — Sistemas Operacionais (Univali).

Dois programas separados: o **cliente** envia requisições por **memória compartilhada** e o
**servidor** as executa com **4 threads** sobre uma tabela protegida por **mutex**. O resultado de
cada requisição vai para um arquivo de log.

Documento com o fluxo ilustrado: [`docs/arquitetura.html`](docs/arquitetura.html)

## Arquivos

```
cliente.c     programa 1: monta as requisições e escreve na memória compartilhada
servidor.c    programa 2: cria a memória, sobe as 4 threads e executa na tabela
banco.h       structs e constantes que os dois programas precisam conhecer
banco.txt     os dados salvos, no formato id;nome
Makefile      compila os dois programas
log.txt       gerado: o que cada thread fez
```

`banco.h` existe porque cliente e servidor são processos separados: os dois precisam concordar sobre
o formato exato das estruturas que atravessam a memória compartilhada.

## Como compilar e executar

Precisa de Linux e `gcc`. No Windows, use o WSL.

```bash
make

# terminal 1
./servidor

# terminal 2 (com o servidor já no ar)
./cliente
```

No VS Code, `Ctrl+Shift+5` divide o terminal integrado em dois painéis, um para cada programa.

## Fluxo

![Fluxo do sistema](docs/fluxo.png)

1. O cliente monta uma `Requisicao` (operação, id e nome).
2. Ele espera um slot livre (`sem_wait(vazias)`), escreve no buffer da memória compartilhada e avisa
   o servidor (`sem_post(cheias)`).
3. Uma das 4 threads do servidor acorda (`sem_wait(cheias)`) e retira a requisição do buffer.
4. A thread tranca o mutex, executa a operação na tabela e destranca — essa é a **seção crítica**.
5. O resultado é escrito no `log.txt` e mostrado na tela.
6. Quando o cliente termina, o servidor salva a tabela no `banco.txt`.

## Exemplo de execução

Servidor:

```
=== SERVIDOR ===
banco.txt carregado com 4 registro(s)
memoria compartilhada e semaforos criados
4 threads criadas, esperando requisicoes...

[thread 2] INSERT id=7 nome='Joao' -> OK
[thread 1] SELECT nome WHERE id=5 -> 'Pedro'
[thread 1] DELETE WHERE id=2 -> OK
[thread 1] SELECT nome WHERE id=2 -> nao encontrado
[thread 1] INSERT id=1 -> ERRO: id ja existe
[thread 3] INSERT id=10 nome='Maria' -> OK
[thread 0] UPDATE id=7 nome='Joao Paulo' -> OK
[thread 2] SELECT nome WHERE id=7 -> 'Joao Paulo'

banco.txt salvo com 5 registro(s)
servidor encerrado
```

Há uma pausa proposital de 0,3 s dentro da seção crítica (`usleep` em `servidor.c`). Sem ela tudo
termina em milissegundos e não dá para ver as threads se revezando.

## Conformidade com o enunciado

| Requisito | Onde está |
| --- | --- |
| Cliente e servidor como executáveis distintos | `cliente.c` e `servidor.c` |
| Comunicação por IPC real | `shm_open` + `mmap` (memória compartilhada) |
| Servidor com pool de threads | `pthread_create` × 4 |
| Tabela protegida por mutex ou semáforo | `pthread_mutex_t mutex_tabela` |
| INSERT, DELETE, SELECT e UPDATE por id | `op_insert`, `op_delete`, `op_select`, `op_update` |
| `struct { int id; char nome[50]; }` | `banco.h` — `Registro` |
| Respostas em arquivo de log | `log.txt` |

## Observação

As quatro threads pegam requisições em paralelo, então **a ordem entre elas não é garantida** — é
consequência direta do paralelismo que o trabalho pede. O que o mutex garante é que a tabela nunca
fique inconsistente: duas threads nunca a alteram ao mesmo tempo.

## Falta

- Relatório em PDF (ABNT).
- Deixar o repositório público antes da entrega.
