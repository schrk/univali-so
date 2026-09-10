#!/usr/bin/env python3
"""Gráficos do relatório, a partir de resultados/metricas.csv.

    throughput_por_threads.png  escalabilidade do pool, um painel por custo simulado
    espera_no_lock.png          tempo médio de espera para entrar na seção crítica

Cada ponto é a mediana das repetições daquela combinação.
Uso: python3 scripts/plot.py     (requer matplotlib)
"""
import csv
import statistics
import sys
from collections import defaultdict
from pathlib import Path

RAIZ = Path(__file__).resolve().parent.parent
CSV = RAIZ / "resultados" / "metricas.csv"
SAIDA = RAIZ / "resultados"

# Uma cor por mix de carga, sempre a mesma em todos os painéis e nas duas
# figuras: a cor identifica a carga, nunca a posição da linha no gráfico.
COR = {100: "#2a78d6", 70: "#eb6834", 0: "#1baf7a"}
MARCA = {100: "o", 70: "s", 0: "^"}
ROTULO = {100: "100% leitura", 70: "70% leitura", 0: "só escrita"}

TINTA = "#1f2a33"
TINTA_FRACA = "#5b6b7a"


def carregar():
    if not CSV.exists():
        sys.exit(f"{CSV} não existe — rode scripts/benchmark.sh primeiro")
    with CSV.open() as f:
        linhas = list(csv.DictReader(f))
    if not linhas:
        sys.exit(f"{CSV} está vazio")
    return linhas


def mix_de(linha):
    """Percentual de leitura efetivo, arredondado para o mix pedido no benchmark."""
    total = float(linha["total"]) or 1.0
    pct = 100 * float(linha["select"]) / total
    return min(COR, key=lambda m: abs(m - pct))


def agrupar(linhas, campo):
    """(custo_us, mix, threads) -> mediana das repetições."""
    bruto = defaultdict(list)
    for l in linhas:
        chave = (int(l["custo_us"]), mix_de(l), int(l["threads"]))
        bruto[chave].append(float(l[campo]))
    return {k: statistics.median(v) for k, v in bruto.items()}


def desenhar(dados, titulo, rotulo_y, arquivo, formato="{:.0f}"):
    import matplotlib
    matplotlib.use("Agg")
    import matplotlib.pyplot as plt

    custos = sorted({c for c, _, _ in dados})
    fig, eixos = plt.subplots(1, len(custos), figsize=(5.2 * len(custos), 4.4),
                              dpi=160, sharey=False)
    if len(custos) == 1:
        eixos = [eixos]

    for ax, custo in zip(eixos, custos):
        # Deslocamento vertical por série: quando duas linhas terminam quase no
        # mesmo ponto, os rótulos diretos se sobreporiam.
        for ordem, mix in enumerate(sorted(COR, reverse=True)):
            pontos = sorted((t, v) for (c, m, t), v in dados.items()
                            if c == custo and m == mix)
            if not pontos:
                continue
            xs = [t for t, _ in pontos]
            ys = [v for _, v in pontos]
            ax.plot(xs, ys, color=COR[mix], marker=MARCA[mix], markersize=6,
                    linewidth=2, label=ROTULO[mix], zorder=3)
            # Rótulo direto no fim da linha: identidade sem depender só da cor.
            ax.annotate(formato.format(ys[-1]), (xs[-1], ys[-1]),
                        textcoords="offset points", xytext=(7, 8 - 9 * ordem),
                        fontsize=8, color=COR[mix])

        ax.set_title(f"custo simulado = {custo} µs/requisição", fontsize=11, color=TINTA)
        ax.set_xlabel("threads no pool", fontsize=9, color=TINTA_FRACA)
        ax.set_ylabel(rotulo_y, fontsize=9, color=TINTA_FRACA)
        ax.set_xticks(sorted({t for (c, _, t) in dados if c == custo}))
        ax.grid(alpha=0.25, linewidth=0.7, zorder=0)
        for lado in ("top", "right"):
            ax.spines[lado].set_visible(False)
        ax.tick_params(labelsize=8, colors=TINTA_FRACA)
        ax.legend(fontsize=8, frameon=False)

    fig.suptitle(titulo, fontsize=13, color=TINTA)
    fig.tight_layout()
    destino = SAIDA / arquivo
    fig.savefig(destino)
    print(f"gerado: {destino}")


def main():
    linhas = carregar()
    try:
        desenhar(agrupar(linhas, "throughput_req_s"),
                 "Throughput por número de threads do pool",
                 "requisições por segundo", "throughput_por_threads.png")
        desenhar(agrupar(linhas, "espera_lock_media_us"),
                 "Espera média para entrar na seção crítica",
                 "microssegundos", "espera_no_lock.png", formato="{:.1f}")
    except ImportError:
        sys.exit("matplotlib não instalado: pip install matplotlib")


if __name__ == "__main__":
    main()
