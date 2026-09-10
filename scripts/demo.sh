#!/usr/bin/env bash
# Demonstração ponta a ponta: sobe o servidor, roda um cliente e mostra o log.
set -euo pipefail
cd "$(dirname "$0")/.."

CARGA="${1:-data/cargas/basica.txt}"
THREADS="${MINIDB_THREADS:-4}"

make --no-print-directory all

rm -f data/banco.txt
mkdir -p data/logs resultados
: > data/logs/servidor.log

echo "==> subindo o servidor com $THREADS threads"
./bin/servidor --threads "$THREADS" &
SERVIDOR_PID=$!

# Espera a memória compartilhada existir antes de soltar o cliente.
for _ in $(seq 1 50); do
    [ -e /dev/shm/minidb_requests ] && break
    sleep 0.1
done

echo
echo "==> cliente enviando $(grep -cvE '^\s*(#|$)' "$CARGA") requisições de $CARGA"
./bin/cliente --carga "$CARGA"

echo
echo "==> encerrando o servidor (SIGTERM)"
kill -TERM "$SERVIDOR_PID"
wait "$SERVIDOR_PID" || true

echo
echo "==> estado final do banco (data/banco.txt)"
cat data/banco.txt

echo
echo "==> últimas linhas do log do servidor"
tail -n 15 data/logs/servidor.log
