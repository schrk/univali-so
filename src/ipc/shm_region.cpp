#include "ipc/shm_region.hpp"
#include <fcntl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>
#include <cerrno>
#include <cstring>
#include <stdexcept>
#include <string>

namespace ipc {

ShmRegion::~ShmRegion() { liberar(); }

ShmRegion::ShmRegion(ShmRegion&& o) noexcept
    : ptr_(o.ptr_), tam_(o.tam_), fd_(o.fd_), dono_(o.dono_) {
    memcpy(nome_, o.nome_, sizeof(nome_));
    o.ptr_ = nullptr; o.fd_ = -1; o.dono_ = false;
}

ShmRegion& ShmRegion::operator=(ShmRegion&& o) noexcept {
    if (this != &o) {
        liberar();
        ptr_ = o.ptr_; tam_ = o.tam_; fd_ = o.fd_; dono_ = o.dono_;
        memcpy(nome_, o.nome_, sizeof(nome_));
        o.ptr_ = nullptr; o.fd_ = -1; o.dono_ = false;
    }
    return *this;
}

void ShmRegion::criar(const char* nome, size_t tamanho) {
    // Restos de uma execução anterior que morreu de forma abrupta impediriam o
    // O_EXCL; o dono limpa antes de criar.
    shm_unlink(nome);

    fd_ = shm_open(nome, O_CREAT | O_EXCL | O_RDWR, 0600);
    if (fd_ < 0) throw std::runtime_error(std::string("shm_open(criar) ") + nome + ": " + strerror(errno));

    if (ftruncate(fd_, static_cast<off_t>(tamanho)) < 0)
        throw std::runtime_error(std::string("ftruncate: ") + strerror(errno));

    ptr_ = mmap(nullptr, tamanho, PROT_READ | PROT_WRITE, MAP_SHARED, fd_, 0);
    if (ptr_ == MAP_FAILED) { ptr_ = nullptr; throw std::runtime_error(std::string("mmap: ") + strerror(errno)); }

    memset(ptr_, 0, tamanho);
    tam_  = tamanho;
    dono_ = true;
    snprintf(nome_, sizeof(nome_), "%s", nome);
}

void ShmRegion::abrir(const char* nome) {
    fd_ = shm_open(nome, O_RDWR, 0600);
    if (fd_ < 0)
        throw std::runtime_error(std::string("shm_open(abrir) ") + nome + ": " + strerror(errno) +
                                 " — o servidor está no ar?");

    struct stat st{};
    if (fstat(fd_, &st) < 0) throw std::runtime_error(std::string("fstat: ") + strerror(errno));
    tam_ = static_cast<size_t>(st.st_size);

    ptr_ = mmap(nullptr, tam_, PROT_READ | PROT_WRITE, MAP_SHARED, fd_, 0);
    if (ptr_ == MAP_FAILED) { ptr_ = nullptr; throw std::runtime_error(std::string("mmap: ") + strerror(errno)); }

    dono_ = false;
    snprintf(nome_, sizeof(nome_), "%s", nome);
}

void ShmRegion::liberar() {
    if (ptr_) { munmap(ptr_, tam_); ptr_ = nullptr; }
    if (fd_ >= 0) { close(fd_); fd_ = -1; }
    if (dono_ && nome_[0]) shm_unlink(nome_);
    dono_ = false;
}

} // namespace ipc
