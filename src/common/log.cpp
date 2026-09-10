#include "common/log.hpp"
#include <pthread.h>
#include <sys/syscall.h>
#include <unistd.h>
#include <cstdarg>
#include <ctime>

namespace logger {

static FILE*           arquivo = nullptr;
static bool            no_terminal = true;
static pthread_mutex_t mtx = PTHREAD_MUTEX_INITIALIZER;

void abrir(const char* caminho, bool tambem_no_terminal) {
    pthread_mutex_lock(&mtx);
    no_terminal = tambem_no_terminal;
    if (caminho) arquivo = fopen(caminho, "a");
    pthread_mutex_unlock(&mtx);
}

void escrever(const char* fmt, ...) {
    timespec ts{};
    clock_gettime(CLOCK_REALTIME, &ts);
    tm agora{};
    localtime_r(&ts.tv_sec, &agora);

    char horario[32];
    strftime(horario, sizeof(horario), "%H:%M:%S", &agora);

    char corpo[512];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(corpo, sizeof(corpo), fmt, ap);
    va_end(ap);

    // O id da thread na linha é o que permite ver, no log, várias threads
    // atendendo requisições ao mesmo tempo.
    long tid = syscall(SYS_gettid);

    // Uma única linha por chamada, sob mutex: sem isso as saídas dos N workers
    // se intercalam no meio da frase.
    pthread_mutex_lock(&mtx);
    if (arquivo) {
        fprintf(arquivo, "%s.%03ld [tid %ld] %s\n", horario, ts.tv_nsec / 1000000, tid, corpo);
        fflush(arquivo);
    }
    if (no_terminal) {
        fprintf(stdout, "%s.%03ld [tid %ld] %s\n", horario, ts.tv_nsec / 1000000, tid, corpo);
        fflush(stdout);
    }
    pthread_mutex_unlock(&mtx);
}

void fechar() {
    pthread_mutex_lock(&mtx);
    if (arquivo) { fclose(arquivo); arquivo = nullptr; }
    pthread_mutex_unlock(&mtx);
}

} // namespace logger
