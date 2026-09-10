#pragma once
#include <cstddef>
#include <cstdint>

namespace ipc {

// RAII sobre um segmento de memória compartilhada POSIX:
// shm_open + ftruncate + mmap na criação, munmap + close (+ shm_unlink no dono)
// na destruição. Quem cria é o dono e é quem remove o objeto do kernel.

class ShmRegion {
public:
    ShmRegion() = default;
    ~ShmRegion();
    ShmRegion(const ShmRegion&) = delete;
    ShmRegion& operator=(const ShmRegion&) = delete;
    ShmRegion(ShmRegion&& o) noexcept;
    ShmRegion& operator=(ShmRegion&& o) noexcept;

    // Servidor: cria (removendo restos de uma execução anterior) e zera.
    void criar(const char* nome, size_t tamanho);
    // Cliente: abre um segmento existente; o tamanho vem do fstat.
    void abrir(const char* nome);

    void*  ptr()      const { return ptr_; }
    size_t tamanho()  const { return tam_; }
    bool   valida()   const { return ptr_ != nullptr; }

private:
    void liberar();

    void*  ptr_  = nullptr;
    size_t tam_  = 0;
    int    fd_   = -1;
    bool   dono_ = false;
    char   nome_[64] = {0};
};

} // namespace ipc
