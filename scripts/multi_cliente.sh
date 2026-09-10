#!/usr/bin/env bash
# Vários processos cliente disputando o mesmo ring ao mesmo tempo.
# Cada cliente tem seu próprio FIFO de respostas (nomeado pelo pid), então as
# respostas nunca se misturam mesmo com todos escrevendo no mesmo canal de ida.
set -euo pipefail
cd "$(dirname "$0")/.."

N_CLIENTES="${1:-4}"
REQ_POR_CLIENTE="${2:-500}"
THREADS="${MINIDB_THREADS:-4}"

make --no-print-directory all

rm -f data/banco.txt
mkdir -p data/logs resultados

echo "==> servidor com $THREADS threads, $N_CLIENTES clientes × $REQ_POR_CLIENTE requisições"
./bin/servidor --threads "$THREADS" --silencioso &
SERVIDOR_PID=$!
for _ in $(seq 1 50); do [ -e /dev/shm/minidb_requests ] && break; sleep 0.1; done

PIDS=()
for i in $(seq 1 "$N_CLIENTES"); do
    ./bin/cliente --gerar "$REQ_POR_CLIENTE" --leitura 70 --semente "$i" --silencioso &
    PIDS+=($!)
done

FALHAS=0
for pid in "${PIDS[@]}"; do
    wait "$pid" || FALHAS=$((FALHAS + 1))
done

kill -TERM "$SERVIDOR_PID"
wait "$SERVIDOR_PID" || true

echo
if [ "$FALHAS" -eq 0 ]; then
    echo "==> todos os $N_CLIENTES clientes receberam todas as respostas"
else
    echo "==> ATENÇÃO: $FALHAS cliente(s) não receberam todas as respostas"
fi
echo "==> registros no banco ao final: $(grep -cv '^#' data/banco.txt || true)"
exit "$FALHAS"
