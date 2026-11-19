#!/bin/bash
# Roda cada roteiro testes/NN_*.txt no simulador e compara com NN_*.esperado.
#   ./testes/rodar.sh              compara a saída
#   ./testes/rodar.sh --valgrind   além disso, exige 0 erros e 0 leaks no valgrind
#   ATUALIZAR=1 ./testes/rodar.sh  regrava os .esperado (use só depois de conferir)
cd "$(dirname "$0")/.." || exit 1
export MFS_DATA_FIXA=1 TZ=UTC LC_ALL=C.UTF-8
VALGRIND=""
[ "$1" = "--valgrind" ] && VALGRIND="valgrind -q --leak-check=full --show-leak-kinds=all --errors-for-leak-kinds=all --error-exitcode=99"

ok=0; falhou=0
for roteiro in testes/[0-9]*.txt; do
    nome=$(basename "$roteiro" .txt)
    esperado="testes/$nome.esperado"
    modo="-t"
    [[ "$nome" == *menu* ]] && modo=""
    saida=$($VALGRIND ./mfs $modo < "$roteiro" 2>&1); status=$?
    if [ "$ATUALIZAR" = "1" ]; then
        printf '%s\n' "$saida" > "$esperado"
    fi
    if [ $status -ne 0 ]; then
        echo "FALHOU  $nome (código $status)"; printf '%s\n' "$saida" | tail -20; falhou=$((falhou+1))
    elif ! diff -u "$esperado" <(printf '%s\n' "$saida") > /dev/null; then
        echo "FALHOU  $nome (saída diferente)"; diff -u "$esperado" <(printf '%s\n' "$saida") | head -30
        falhou=$((falhou+1))
    else
        echo "ok      $nome${VALGRIND:+ (valgrind: 0 erros, 0 leaks)}"; ok=$((ok+1))
    fi
done
echo "$ok ok, $falhou falha(s)"
[ $falhou -eq 0 ]
