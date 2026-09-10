#!/usr/bin/env bash
# Bateria de medições para o relatório: varia o número de threads do pool e a
# proporção de leitura da carga, acumulando tudo em resultados/metricas.csv.
set -euo pipefail
cd "$(dirname "$0")/.."

REQUISICOES="${REQUISICOES:-20000}"
REPETICOES="${REPETICOES:-3}"
THREADS_LISTA="${THREADS_LISTA:-1 2 4 8}"
LEITURA_LISTA="${LEITURA_LISTA:-100 70 0}"
# Custo de processamento simulado por requisição. 0 mede o sistema como está
# (dominado pelo IPC); um valor > 0 representa uma consulta cara e é o cenário
# em que o paralelismo do pool aparece.
CUSTO_LISTA="${CUSTO_LISTA:-0 200}"

make --no-print-directory all
mkdir -p resultados data/logs
rm -f resultados/metricas.csv

echo "==> $REQUISICOES requisições por execução, $REPETICOES repetição(ões)"
echo "==> threads: $THREADS_LISTA | leitura(%): $LEITURA_LISTA | custo(us): $CUSTO_LISTA"

for custo in $CUSTO_LISTA; do
 for leitura in $LEITURA_LISTA; do
  for threads in $THREADS_LISTA; do
    for rep in $(seq 1 "$REPETICOES"); do
      printf "  custo=%-4s leitura=%-3s threads=%-2s rep=%s ... " "$custo" "$leitura" "$threads" "$rep"

      rm -f data/banco.txt
      ./bin/servidor --threads "$threads" --custo-us "$custo" --silencioso &
      SERVIDOR_PID=$!
      for _ in $(seq 1 50); do [ -e /dev/shm/minidb_requests ] && break; sleep 0.1; done

      ./bin/cliente --gerar "$REQUISICOES" --leitura "$leitura" \
                    --semente "$rep" --silencioso > /dev/null

      kill -TERM "$SERVIDOR_PID"
      wait "$SERVIDOR_PID" 2>/dev/null || true
      echo "ok"
    done
  done
 done
done

echo
echo "==> resultados/metricas.csv"
awk -F, 'NR==1 { printf "%-8s %-8s %-8s %-8s %-11s %-14s\n", "threads","custo_us","total","select","req/s","espera_lock_us"; next }
         { printf "%-8s %-8s %-8s %-8s %-11.0f %-14.2f\n", $1,$3,$4,$5,$10,$12 }' resultados/metricas.csv
echo
echo "gráficos: python3 scripts/plot.py"
