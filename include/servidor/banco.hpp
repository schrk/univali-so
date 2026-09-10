#pragma once
#include <vector>
#include "common/registro.hpp"
#include "servidor/rw_lock.hpp"

namespace srv {

// O "banco de dados simulado": um vetor de Registro compartilhado por todas as
// threads do pool. Todo acesso passa pelo rw_lock — a classe não expõe o vetor.

class Banco {
public:
    RwLock& lock() { return lock_; }

    // Chamados apenas na subida/descida do servidor, sem concorrência.
    void carregar(const std::vector<Registro>& iniciais);
    std::vector<Registro> copia_bruta() const { return tabela_; }

    // Usados pelo executor, sempre com o lock já tomado.
    const Registro* buscar(int id) const;
    Registro*       buscar(int id);
    bool            inserir(const Registro& r);   // false se a tabela encheu
    bool            remover(int id);
    size_t          tamanho() const { return tabela_.size(); }

private:
    std::vector<Registro> tabela_;
    RwLock                lock_;
};

} // namespace srv
