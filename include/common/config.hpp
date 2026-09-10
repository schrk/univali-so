#pragma once
#include <cstddef>
#include <cstdint>

// Nomes dos objetos de kernel e limites do sistema.
// Os valores ajustáveis vêm de variável de ambiente para que o benchmark possa
// variar o número de threads e de slots sem recompilar.

namespace cfg {

inline constexpr const char* SHM_REQUISICOES = "/minidb_requests";
inline constexpr const char* SEM_VAZIAS      = "/minidb_sem_vazias";
inline constexpr const char* SEM_CHEIAS      = "/minidb_sem_cheias";
inline constexpr const char* SEM_MUTEX_RING  = "/minidb_sem_mutex_ring";

// Um FIFO por cliente: o servidor descobre o caminho a partir do pid que veio
// na requisição, então respostas de clientes concorrentes nunca se misturam.
inline constexpr const char* FIFO_PREFIXO    = "/tmp/minidb_resp.";

inline constexpr uint32_t MAX_REGISTROS = 4096;
inline constexpr const char* ARQ_BANCO_INICIAL = "data/banco.inicial.txt";
inline constexpr const char* ARQ_BANCO         = "data/banco.txt";
inline constexpr const char* ARQ_LOG_SERVIDOR  = "data/logs/servidor.log";
inline constexpr const char* ARQ_LOG_CLIENTE   = "data/logs/cliente.log";
inline constexpr const char* ARQ_METRICAS      = "resultados/metricas.csv";

// Padrões, sobrepostos por MINIDB_THREADS / MINIDB_SLOTS ou pela linha de comando.
uint32_t num_threads();
uint32_t num_slots();
uint32_t custo_simulado_us();

// Caminho do FIFO de respostas de um cliente.
void caminho_fifo(int32_t pid, char* saida, size_t tam);

} // namespace cfg
