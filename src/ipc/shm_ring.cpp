#include "ipc/shm_ring.hpp"
#include "common/config.hpp"
#include <cstring>
#include <stdexcept>

namespace ipc {

static constexpr uint32_t MAGIC = 0x4D444231; // "MDB1"

Requisicao* ShmRing::slots() {
    return reinterpret_cast<Requisicao*>(reinterpret_cast<char*>(cab_) + sizeof(CabecalhoRing));
}

void ShmRing::criar(uint32_t capacidade) {
    size_t tamanho = sizeof(CabecalhoRing) + static_cast<size_t>(capacidade) * sizeof(Requisicao);
    regiao_.criar(cfg::SHM_REQUISICOES, tamanho);

    cab_ = static_cast<CabecalhoRing*>(regiao_.ptr());
    cab_->magic      = MAGIC;
    cab_->capacidade = capacidade;
    cab_->head = cab_->tail = 0;
    cab_->total_push = cab_->total_pop = 0;

    // vazias começa com todos os slots livres, cheias com nenhum pendente e o
    // mutex do ring liberado.
    vazias_.criar(cfg::SEM_VAZIAS, capacidade);
    cheias_.criar(cfg::SEM_CHEIAS, 0);
    mutex_.criar(cfg::SEM_MUTEX_RING, 1);
}

void ShmRing::abrir() {
    regiao_.abrir(cfg::SHM_REQUISICOES);
    cab_ = static_cast<CabecalhoRing*>(regiao_.ptr());
    if (cab_->magic != MAGIC)
        throw std::runtime_error("segmento de memória compartilhada inválido");

    vazias_.abrir(cfg::SEM_VAZIAS);
    cheias_.abrir(cfg::SEM_CHEIAS);
    mutex_.abrir(cfg::SEM_MUTEX_RING);
}

void ShmRing::push(const Requisicao& r) {
    while (!vazias_.wait()) {}   // espera um slot livre (produtor não é interrompível)

    while (!mutex_.wait()) {}    // seção crítica sobre head
    slots()[cab_->head] = r;     // cópia byte a byte para dentro da shm
    cab_->head = (cab_->head + 1) % cab_->capacidade;
    cab_->total_push++;
    mutex_.post();

    cheias_.post();              // avisa o consumidor
}

bool ShmRing::pop(Requisicao& r) {
    if (!cheias_.wait()) return false;  // interrompido por sinal: hora de encerrar

    while (!mutex_.wait()) {}
    r = slots()[cab_->tail];
    cab_->tail = (cab_->tail + 1) % cab_->capacidade;
    cab_->total_pop++;
    mutex_.post();

    vazias_.post();              // devolve o slot ao produtor
    return true;
}

uint32_t ShmRing::ocupados() const {
    int v = cheias_.valor();
    return v < 0 ? 0 : static_cast<uint32_t>(v);
}

void ShmRing::acordar_consumidor() { cheias_.post(); }

} // namespace ipc
