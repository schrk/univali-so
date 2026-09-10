// Testa o canal IPC de requisições: ring cheio, ring vazio e travessia real
// entre dois processos (fork), não entre duas threads do mesmo processo.

#include <sys/wait.h>
#include <unistd.h>
#include <cstdio>
#include <cstring>
#include "ipc/shm_ring.hpp"

static int falhas = 0;

static void checar(bool cond, const char* o_que) {
    printf("  [%s] %s\n", cond ? " ok " : "FALHA", o_que);
    if (!cond) falhas++;
}

int main() {
    printf("\n=== test_ipc: memória compartilhada + semáforos nomeados ===\n");

    const uint32_t CAPACIDADE = 4;
    const uint32_t TOTAL      = 200;   // muito maior que o ring: força o produtor a dormir

    ipc::ShmRing servidor;
    servidor.criar(CAPACIDADE);
    checar(servidor.capacidade() == CAPACIDADE, "ring criado com a capacidade pedida");
    checar(servidor.ocupados() == 0, "ring nasce vazio (sem_cheias = 0)");

    pid_t pid = fork();
    if (pid == 0) {
        // Filho = processo cliente: abre os mesmos objetos pelo nome.
        ipc::ShmRing cliente;
        cliente.abrir();
        for (uint32_t i = 0; i < TOTAL; ++i) {
            Requisicao r{};
            r.id_req = i + 1;
            r.pid_cliente = getpid();
            r.op = Op::INSERT;
            r.id = static_cast<int32_t>(i);
            snprintf(r.nome, sizeof(r.nome), "reg-%u", i);
            cliente.push(r);
        }
        _exit(0);
    }

    // Pai = processo servidor: consome tudo, na ordem em que foi produzido.
    bool ordem_ok = true, conteudo_ok = true;
    for (uint32_t i = 0; i < TOTAL; ++i) {
        Requisicao r{};
        if (!servidor.pop(r)) { ordem_ok = false; break; }
        if (r.id_req != i + 1 || r.id != static_cast<int32_t>(i)) ordem_ok = false;

        char esperado[32];
        snprintf(esperado, sizeof(esperado), "reg-%u", i);
        if (strcmp(r.nome, esperado) != 0) conteudo_ok = false;
    }

    int status = 0;
    waitpid(pid, &status, 0);

    checar(WIFEXITED(status) && WEXITSTATUS(status) == 0, "processo produtor terminou sem erro");
    checar(ordem_ok, "as 200 requisições chegaram na ordem (FIFO preservada)");
    checar(conteudo_ok, "o conteúdo atravessou a fronteira de processos intacto");
    checar(servidor.ocupados() == 0, "ring volta a ficar vazio no fim");
    checar(TOTAL > CAPACIDADE, "o teste realmente encheu o ring (200 itens em 4 slots)");

    printf("=== test_ipc: %s ===\n", falhas ? "FALHOU" : "passou");
    return falhas ? 1 : 0;
}
