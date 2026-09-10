#include "ipc/fifo_channel.hpp"
#include "common/config.hpp"
#include "common/log.hpp"
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>
#include <cerrno>
#include <cstring>
#include <stdexcept>
#include <string>

namespace ipc {

// ---------- cliente ----------

FifoLeitor::~FifoLeitor() { fechar(); }

void FifoLeitor::criar(int32_t pid) {
    cfg::caminho_fifo(pid, caminho_, sizeof(caminho_));
    unlink(caminho_);
    if (mkfifo(caminho_, 0600) < 0)
        throw std::runtime_error(std::string("mkfifo ") + caminho_ + ": " + strerror(errno));

    // O_RDWR e não O_RDONLY: abrir só para leitura bloquearia até o servidor
    // abrir a outra ponta, e manter uma ponta de escrita aberta aqui impede que
    // o read() devolva EOF entre uma resposta e outra.
    fd_ = open(caminho_, O_RDWR);
    if (fd_ < 0)
        throw std::runtime_error(std::string("open ") + caminho_ + ": " + strerror(errno));
}

bool FifoLeitor::ler(Resposta& r) {
    size_t lidos = 0;
    char*  dst   = reinterpret_cast<char*>(&r);
    while (lidos < sizeof(Resposta)) {
        ssize_t n = read(fd_, dst + lidos, sizeof(Resposta) - lidos);
        if (n > 0)                    { lidos += static_cast<size_t>(n); continue; }
        if (n < 0 && errno == EINTR)  continue;
        return false;                 // EOF ou erro
    }
    return true;
}

void FifoLeitor::fechar() {
    if (fd_ >= 0) { close(fd_); fd_ = -1; }
    if (caminho_[0]) { unlink(caminho_); caminho_[0] = '\0'; }
}

// ---------- servidor ----------

FifoEscritor::~FifoEscritor() { fechar_tudo(); }

int FifoEscritor::abrir_para(int32_t pid) {
    auto it = fds_.find(pid);
    if (it != fds_.end()) return it->second;

    char caminho[128];
    cfg::caminho_fifo(pid, caminho, sizeof(caminho));

    // O_NONBLOCK na abertura para não travar o worker caso o cliente tenha
    // morrido (nesse caso open falha com ENXIO em vez de bloquear); em seguida
    // o flag é removido para que as escritas sigam o comportamento normal.
    int fd = open(caminho, O_WRONLY | O_NONBLOCK);
    if (fd < 0) {
        logger::escrever("FIFO do cliente %d indisponível (%s)", pid, strerror(errno));
        return -1;
    }
    int flags = fcntl(fd, F_GETFL, 0);
    fcntl(fd, F_SETFL, flags & ~O_NONBLOCK);

    fds_[pid] = fd;
    return fd;
}

bool FifoEscritor::enviar(int32_t pid_cliente, const Resposta& r) {
    // sizeof(Resposta) é bem menor que PIPE_BUF, então cada write() já é
    // atômico; o mutex garante que dois workers não mexam no mesmo descritor
    // nem no cache de descritores ao mesmo tempo.
    pthread_mutex_lock(&mtx_);

    int fd = abrir_para(pid_cliente);
    bool ok = false;
    if (fd >= 0) {
        const char* src = reinterpret_cast<const char*>(&r);
        size_t escritos = 0;
        ok = true;
        while (escritos < sizeof(Resposta)) {
            ssize_t n = write(fd, src + escritos, sizeof(Resposta) - escritos);
            if (n > 0)                   { escritos += static_cast<size_t>(n); continue; }
            if (n < 0 && errno == EINTR) continue;
            logger::escrever("falha ao escrever no FIFO do cliente %d: %s", pid_cliente, strerror(errno));
            close(fd);
            fds_.erase(pid_cliente);
            ok = false;
            break;
        }
    }

    pthread_mutex_unlock(&mtx_);
    return ok;
}

void FifoEscritor::fechar_tudo() {
    pthread_mutex_lock(&mtx_);
    for (auto& [pid, fd] : fds_) close(fd);
    fds_.clear();
    pthread_mutex_unlock(&mtx_);
}

} // namespace ipc
