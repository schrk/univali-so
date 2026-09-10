#include "servidor/banco.hpp"
#include "common/config.hpp"
#include <cstring>

namespace srv {

void Banco::carregar(const std::vector<Registro>& iniciais) {
    tabela_ = iniciais;
    tabela_.reserve(cfg::MAX_REGISTROS);
}

const Registro* Banco::buscar(int id) const {
    for (const auto& r : tabela_)
        if (r.id == id) return &r;
    return nullptr;
}

Registro* Banco::buscar(int id) {
    for (auto& r : tabela_)
        if (r.id == id) return &r;
    return nullptr;
}

bool Banco::inserir(const Registro& r) {
    if (tabela_.size() >= cfg::MAX_REGISTROS) return false;
    tabela_.push_back(r);
    return true;
}

bool Banco::remover(int id) {
    for (size_t i = 0; i < tabela_.size(); ++i) {
        if (tabela_[i].id == id) {
            tabela_.erase(tabela_.begin() + static_cast<long>(i));
            return true;
        }
    }
    return false;
}

} // namespace srv
