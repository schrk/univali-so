#!/usr/bin/env python3
"""Simulador de escalonamento multiprocessado com paginação sob demanda (M2 - SO).

Uso:
    python3 simulador.py --arquivo cenarios/cenario_A_t0.json --cpus 4 --sched rr --quantum 10
    python3 simulador.py            (sem argumentos: abre o menu interativo)

Opções principais (seção 8 do enunciado):
    --arquivo  --cpus  --sched  --quantum  --subst  --quadros  --custo-troca  --log
Extras:
    --custo-fault N   tempo de um page fault (padrão 20)
    --log-ate T       limita o log aos instantes 0..T (padrão 2000; 0 = tudo)
    --ao-vivo         mostra a simulação acontecendo, unidade por unidade
    --atraso MS       pausa entre unidades no modo ao vivo (padrão 150 ms)
"""

import argparse
import heapq
import json
import os
import sys
import time
from collections import OrderedDict, deque


# ----------------------------------------------------------------------------
# Processo
# ----------------------------------------------------------------------------
class Processo:
    def __init__(self, d):
        self.pid = d["pid"]
        self.chegada = d["chegada"]
        self.classe = d.get("classe", "-")
        self.paginas = d.get("paginas", 0)
        self.bursts = d["bursts"]
        self.etapa = 0          # índice em bursts
        self.pos = 0            # quantas unidades do burst de CPU atual já executaram
        self.espera = 0
        self.entrou_fila = None
        self.primeiro_despacho = None
        self.fim = None

    def burst(self):
        return self.bursts[self.etapa]

    def restante(self):
        """Quanto falta do burst de CPU atual (chave do SJF)."""
        return self.burst()["cpu"] - self.pos

    def ref_atual(self):
        refs = self.burst().get("refs")
        return refs[self.pos] if refs else None  # None = burst sem referências


# ----------------------------------------------------------------------------
# Fila de prontos: deque (FCFS/RR) ou heap (SJF)
# ----------------------------------------------------------------------------
class FilaProntos:
    def __init__(self, sched):
        self.sjf = sched == "sjf"
        self.itens = [] if self.sjf else deque()
        self.seq = 0  # desempate do SJF: quem entrou primeiro

    def entra(self, p, t):
        p.entrou_fila = t
        if self.sjf:
            heapq.heappush(self.itens, (p.restante(), self.seq, p))
            self.seq += 1
        else:
            self.itens.append(p)

    def sai(self):
        return heapq.heappop(self.itens)[2] if self.sjf else self.itens.popleft()

    def pids(self):
        if self.sjf:
            return [p.pid for _, _, p in sorted(self.itens)]
        return [p.pid for p in self.itens]

    def __len__(self):
        return len(self.itens)


# ----------------------------------------------------------------------------
# Memória física: quadros globais
# ----------------------------------------------------------------------------
class Memoria:
    """OrderedDict (pid, pagina) -> protegida.

    A ordem do dicionário é a ordem de substituição: o primeiro item é a vítima.
    FIFO: a ordem é a de carga (acerto não mexe).
    LRU : todo acerto move a página para o fim, então o primeiro é o menos usado.
    Busca, inserção, remoção e move_to_end são O(1); a vítima é o primeiro item
    não protegido (quase sempre o primeiro, pois poucas páginas ficam protegidas).
    """

    def __init__(self, quadros, subst):
        self.quadros = quadros
        self.lru = subst == "lru"
        self.tab = OrderedDict()
        self.por_pid = {}  # pid -> set de páginas na memória (para liberar no término)

    def acesso(self, pid, pag):
        """Retorna True se acerto. Acerto remove a proteção (a referência foi feita)."""
        chave = (pid, pag)
        if chave not in self.tab:
            return False
        self.tab[chave] = False
        if self.lru:
            self.tab.move_to_end(chave)
        return True

    def carrega(self, pid, pag):
        """Põe a página num quadro (protegida). Retorna False se não há vítima."""
        if len(self.tab) >= self.quadros:
            vitima = next((k for k, prot in self.tab.items() if not prot), None)
            if vitima is None:
                return False
            del self.tab[vitima]
            self.por_pid[vitima[0]].discard(vitima[1])
        self.tab[(pid, pag)] = True
        self.por_pid.setdefault(pid, set()).add(pag)
        return True

    def libera(self, pid):
        for pag in self.por_pid.pop(pid, ()):
            del self.tab[(pid, pag)]

    def ocupados(self):
        return len(self.tab)


# ----------------------------------------------------------------------------
# CPU
# ----------------------------------------------------------------------------
class CPU:
    def __init__(self, i):
        self.i = i
        self.proc = None
        self.ultimo = None      # pid do último processo que executou aqui
        self.troca_fim = None   # instante em que termina a troca de contexto em curso
        self.executou = False   # executou a unidade [t-1, t)
        self.quantum_usado = 0
        self.util = self.troca = self.ocioso = 0
        self.n_trocas = 0

    def estado(self):
        if self.proc is None:
            return "OCIOSA"
        if self.troca_fim is not None:
            return "TROCA"
        return "P%d" % self.proc.pid


# ----------------------------------------------------------------------------
# Simulação
# ----------------------------------------------------------------------------
def simular(processos, cpus=1, sched="fcfs", quantum=10, subst="lru", quadros=256,
            custo_troca=1, custo_fault=20, log=None, ao_vivo=False, atraso=0.15,
            janela_gantt=200, log_ate=0):
    procs = sorted((Processo(d) for d in processos), key=lambda p: (p.chegada, p.pid))
    total = len(procs)
    fila = FilaProntos(sched)
    mem = Memoria(quadros, subst)
    cpu = [CPU(i) for i in range(cpus)]
    bloq = []           # heap (retorno, pid, motivo, proc); motivo: "es", "fault", "retry"
    bloq_info = {}      # pid -> (retorno, motivo) para o log
    gantt = [[] for _ in range(cpus)]   # (rótulo, pid) por unidade, até janela_gantt
    prox = 0            # próximo processo a chegar (procs ordenado por chegada)
    terminados = 0
    faults = 0
    faults_sem_vitima = 0
    referencias = 0
    t = 0

    def bloqueia(p, ate, motivo):
        heapq.heappush(bloq, (ate, p.pid, motivo, p))
        bloq_info[p.pid] = (ate, motivo)

    while True:
        ev = []  # eventos deste instante (log / ao vivo)

        # 1) fim da unidade [t-1, t) em cada CPU: fim de burst, término ou quantum
        expirados = []
        for c in cpu:
            if c.troca_fim == t:
                c.troca_fim = None
            if not c.executou:
                continue
            c.executou = False
            p = c.proc
            p.pos += 1
            c.quantum_usado += 1
            if p.pos == p.burst()["cpu"]:          # fim do burst de CPU
                p.etapa += 1
                p.pos = 0
                c.proc = None
                if p.etapa == len(p.bursts):
                    p.fim = t
                    terminados += 1
                    mem.libera(p.pid)
                    ev.append("TERMINO P%d (cpu%d)" % (p.pid, c.i))
                else:
                    es = p.burst()["es"]
                    p.etapa += 1
                    bloqueia(p, t + es, "es")
                    ev.append("FIM_BURST P%d (cpu%d) -> E/S ate %d" % (p.pid, c.i, t + es))
            elif sched == "rr" and c.quantum_usado >= quantum:
                expirados.append(c)

        if terminados == total:
            break

        # 2) chegadas e retornos de E/S / page fault, em ordem de pid
        entrando = []
        while prox < total and procs[prox].chegada == t:
            entrando.append((procs[prox], "CHEGADA"))
            prox += 1
        while bloq and bloq[0][0] == t:
            _, _, motivo, p = heapq.heappop(bloq)
            del bloq_info[p.pid]
            if motivo == "retry":   # tenta de novo conseguir um quadro (fault não é recontado)
                if mem.carrega(p.pid, p.ref_atual()):
                    bloqueia(p, t + custo_fault, "fault")   # agora a página é trazida do disco
                    ev.append("QUADRO_OBTIDO P%d, carregando ate %d" % (p.pid, t + custo_fault))
                else:
                    bloqueia(p, t + custo_fault, "retry")
                    ev.append("SEM_VITIMA P%d de novo, ate %d" % (p.pid, t + custo_fault))
                continue
            entrando.append((p, "RETORNO_ES" if motivo == "es" else "RETORNO_FAULT"))
        entrando.sort(key=lambda x: x[0].pid)
        for p, nome in entrando:
            fila.entra(p, t)
            ev.append("%s P%d" % (nome, p.pid))

        # 3) preempção por quantum: vai para o fim da fila se há outro pronto
        for c in expirados:
            if len(fila):
                p = c.proc
                c.proc = None
                fila.entra(p, t)
                ev.append("PREEMPCAO P%d (cpu%d)" % (p.pid, c.i))
            else:
                c.quantum_usado = 0     # continua sem custo, novo quantum

        # 4) despachos + referência à página no início da unidade
        mudou = True
        while mudou:
            mudou = False
            for c in cpu:
                if c.proc is None and len(fila):
                    p = fila.sai()
                    p.espera += t - p.entrou_fila
                    if p.primeiro_despacho is None:
                        p.primeiro_despacho = t
                    c.proc = p
                    c.quantum_usado = 0
                    if p.pid != c.ultimo:
                        c.n_trocas += 1
                        if custo_troca > 0:
                            c.troca_fim = t + custo_troca
                    c.ultimo = p.pid
                    ev.append("DESPACHO P%d (cpu%d)%s" % (
                        p.pid, c.i, " com troca" if c.troca_fim else ""))
                if c.proc is None or c.troca_fim is not None or c.executou:
                    continue
                p = c.proc
                pag = p.ref_atual()
                if pag is None or mem.acesso(p.pid, pag):
                    c.executou = True           # acerto: executa [t, t+1)
                    referencias += pag is not None
                    continue
                # page fault: processo bloqueia, CPU fica livre, resto do quantum é perdido
                faults += 1
                c.proc = None
                if mem.carrega(p.pid, pag):
                    bloqueia(p, t + custo_fault, "fault")
                    ev.append("FAULT P%d pag %d (cpu%d) ate %d" % (p.pid, pag, c.i, t + custo_fault))
                else:
                    faults_sem_vitima += 1
                    bloqueia(p, t + custo_fault, "retry")
                    ev.append("FAULT P%d pag %d sem vitima, ate %d" % (p.pid, pag, t + custo_fault))
                mudou = True

        # 5) contabiliza a unidade [t, t+1) de cada CPU
        for c in cpu:
            e = c.estado()
            if e == "OCIOSA":
                c.ocioso += 1
            elif e == "TROCA":
                c.troca += 1
            else:
                c.util += 1
            if t < janela_gantt:
                gantt[c.i].append((e, c.proc.pid if c.proc else None))

        if log and ev and (not log_ate or t <= log_ate):
            log.write("t=%d\n" % t)
            for e in ev:
                log.write("  evento: %s\n" % e)
            log.write("  prontos: %s\n" % fila.pids())
            log.write("  bloqueados: %s\n" % " ".join(
                "P%d->%d%s" % (pid, r, "(fault)" if m != "es" else "")
                for pid, (r, m) in sorted(bloq_info.items())))
            log.write("  cpus: %s\n" % " ".join("cpu%d=%s" % (c.i, c.estado()) for c in cpu))
            log.write("  quadros: %d/%d\n" % (mem.ocupados(), quadros))

        if ao_vivo:
            try:
                painel(t, cpu, fila, bloq_info, mem, quadros, terminados, total, faults, ev)
                time.sleep(atraso)
            except KeyboardInterrupt:   # Ctrl+C: sai do modo ao vivo e termina a simulação
                ao_vivo = False
                print("\nmodo ao vivo encerrado em t=%d, terminando a simulacao..." % t)

        t += 1

    makespan = t
    return {
        "procs": procs, "cpu": cpu, "makespan": makespan, "faults": faults,
        "faults_sem_vitima": faults_sem_vitima, "referencias": referencias,
        "gantt": gantt,
    }


# ----------------------------------------------------------------------------
# Modo ao vivo
# ----------------------------------------------------------------------------
def painel(t, cpu, fila, bloq_info, mem, quadros, terminados, total, faults, ev):
    linhas = ["\033[H\033[J", "=== SIMULADOR AO VIVO ===   t = %d   (Ctrl+C sai do modo ao vivo)" % t, ""]
    for c in cpu:
        linhas.append("  CPU %d : %-8s  util=%d troca=%d ocioso=%d" % (
            c.i, c.estado(), c.util, c.troca, c.ocioso))
    pids = fila.pids()
    linhas.append("")
    linhas.append("  Prontos (%d): %s%s" % (len(pids), " ".join("P%d" % x for x in pids[:20]),
                                            " ..." if len(pids) > 20 else ""))
    blq = sorted(bloq_info.items(), key=lambda x: x[1][0])
    linhas.append("  Bloqueados (%d): %s%s" % (len(blq), " ".join(
        "P%d->%d" % (pid, r) for pid, (r, _) in blq[:12]), " ..." if len(blq) > 12 else ""))
    linhas.append("  Quadros: %d/%d   Page faults: %d   Terminados: %d/%d" % (
        mem.ocupados(), quadros, faults, terminados, total))
    linhas.append("")
    linhas.append("  Eventos em t=%d:" % t)
    linhas += ["    " + e for e in ev[:10]] or ["    -"]
    print("\n".join(linhas), flush=True)


# ----------------------------------------------------------------------------
# Saídas: resumo e Gantt
# ----------------------------------------------------------------------------
def metricas(r):
    procs, mk = r["procs"], r["makespan"]
    n = len(procs)
    m = {
        "makespan": mk,
        "espera_media": sum(p.espera for p in procs) / n,
        "espera_max": max(p.espera for p in procs),
        "turnaround_medio": sum(p.fim - p.chegada for p in procs) / n,
        "resposta_media": sum(p.primeiro_despacho - p.chegada for p in procs) / n,
        "trocas": sum(c.n_trocas for c in r["cpu"]),
        "faults": r["faults"],
        "faults_sem_vitima": r["faults_sem_vitima"],
        "taxa_faults": r["faults"] / (r["faults"] + r["referencias"]) if r["referencias"] else 0.0,
        "util_media": sum(c.util + c.troca for c in r["cpu"]) / (mk * len(r["cpu"])),
    }
    for cl in ("curto", "longo"):
        g = [p for p in procs if p.classe == cl]
        if g:
            m["espera_media_" + cl] = sum(p.espera for p in g) / len(g)
            m["espera_max_" + cl] = max(p.espera for p in g)
            m["turnaround_medio_" + cl] = sum(p.fim - p.chegada for p in g) / len(g)
    return m


def resumo(r, params):
    m = metricas(r)
    mk = m["makespan"]
    s = ["=" * 64, "RESUMO DA SIMULACAO", "=" * 64, "Parametros:"]
    for k, v in params.items():
        s.append("  %-13s %s" % (k, v))
    s += ["", "Metricas globais:",
          "  Makespan ............... %d" % mk,
          "  Espera media ........... %.2f" % m["espera_media"],
          "  Espera maxima .......... %d" % m["espera_max"],
          "  Turnaround medio ....... %.2f" % m["turnaround_medio"],
          "  Resposta media ......... %.2f" % m["resposta_media"],
          "  Trocas de contexto ..... %d" % m["trocas"],
          "", "Por CPU:",
          "  CPU      util  %util    troca %troca   ocioso %ocioso  trocas"]
    for c in r["cpu"]:
        s.append("  %3d %9d %5.1f%% %8d %5.1f%% %8d %6.1f%% %7d" % (
            c.i, c.util, 100 * c.util / mk, c.troca, 100 * c.troca / mk,
            c.ocioso, 100 * c.ocioso / mk, c.n_trocas))
    s += ["", "Memoria:",
          "  Page faults ............ %d" % m["faults"],
          "  Referencias (acertos) .. %d" % r["referencias"],
          "  Taxa de faults ......... %.2f%%  (faults / acessos)" % (100 * m["taxa_faults"]),
          "  Faults sem vitima ...... %d" % m["faults_sem_vitima"]]
    if "espera_media_longo" in m:
        s += ["", "Por classe (so para analise; o escalonador nao usa):",
              "  curto: espera media %.2f  max %d  turnaround %.2f" % (
                  m["espera_media_curto"], m["espera_max_curto"], m["turnaround_medio_curto"]),
              "  longo: espera media %.2f  max %d  turnaround %.2f" % (
                  m["espera_media_longo"], m["espera_max_longo"], m["turnaround_medio_longo"])]
    s.append("=" * 64)
    return "\n".join(s)


def gantt_csv(gantt):
    linhas = ["cpu,inicio,fim,evento"]
    for i, faixa in enumerate(gantt):
        ini = 0
        for k in range(1, len(faixa) + 1):
            # (estado, pid): trocas seguidas para processos diferentes ficam separadas
            if k == len(faixa) or faixa[k] != faixa[ini]:
                linhas.append("%d,%d,%d,%s" % (i, ini, k, faixa[ini][0]))
                ini = k
    return "\n".join(linhas) + "\n"


def nome_execucao(a):
    cen = os.path.splitext(os.path.basename(a.arquivo))[0]
    q = "_q%d" % a.quantum if a.sched == "rr" else ""
    return "%s_cpus%d_%s%s_%s_f%d" % (cen, a.cpus, a.sched, q, a.subst, a.quadros)


# ----------------------------------------------------------------------------
# Menu (sem argumentos)
# ----------------------------------------------------------------------------
def pergunta(texto, padrao, tipo=str, opcoes=None):
    while True:
        v = input("%s [%s]: " % (texto, padrao)).strip() or str(padrao)
        try:
            v = tipo(v)
        except ValueError:
            print("  valor invalido")
            continue
        if opcoes and v not in opcoes:
            print("  opcoes: %s" % ", ".join(map(str, opcoes)))
            continue
        return v


def menu():
    print("=== SIMULADOR DE ESCALONAMENTO + PAGINACAO ===")
    pasta = os.path.join(os.path.dirname(os.path.abspath(__file__)), "cenarios")
    jsons = sorted(f for f in os.listdir(pasta) if f.endswith(".json")) if os.path.isdir(pasta) else []
    for i, f in enumerate(jsons, 1):
        print("  %d) %s" % (i, f))
    arq = pergunta("Arquivo (numero ou caminho)", 1 if jsons else "")
    if arq.isdigit() and 1 <= int(arq) <= len(jsons):
        arq = os.path.join(pasta, jsons[int(arq) - 1])
    a = argparse.Namespace(arquivo=arq)
    a.cpus = pergunta("CPUs", 1, int, [1, 4])
    a.sched = pergunta("Escalonador (fcfs/sjf/rr)", "fcfs", str.lower, ["fcfs", "sjf", "rr"])
    a.quantum = pergunta("Quantum", 10, int) if a.sched == "rr" else 10
    a.subst = pergunta("Substituicao (fifo/lru)", "lru", str.lower, ["fifo", "lru"])
    a.quadros = pergunta("Quadros", 256, int)
    a.custo_troca = pergunta("Custo da troca", 1, int)
    a.custo_fault = pergunta("Custo do page fault", 20, int)
    a.log = pergunta("Gerar log? (s/n)", "n", str.lower, ["s", "n"]) == "s"
    a.log_ate = pergunta("Log ate o instante (0 = tudo)", 2000, int) if a.log else 0
    a.ao_vivo = pergunta("Ver ao vivo? (s/n)", "n", str.lower, ["s", "n"]) == "s"
    a.atraso = pergunta("Atraso por unidade em ms", 150, int) if a.ao_vivo else 150
    a.saida = "saida"
    return a


def main():
    if len(sys.argv) == 1:
        a = menu()
    else:
        ap = argparse.ArgumentParser(description="Simulador de escalonamento com paginacao")
        ap.add_argument("--arquivo", required=True, help="caminho do JSON")
        ap.add_argument("--cpus", type=int, default=1, choices=[1, 4])
        ap.add_argument("--sched", default="fcfs", choices=["fcfs", "sjf", "rr"])
        ap.add_argument("--quantum", type=int, default=10, help="usado so no RR")
        ap.add_argument("--subst", default="lru", choices=["fifo", "lru"])
        ap.add_argument("--quadros", type=int, default=256)
        ap.add_argument("--custo-troca", type=int, default=1)
        ap.add_argument("--custo-fault", type=int, default=20)
        ap.add_argument("--log", action="store_true", help="gera o log de estados (secao 9.3)")
        ap.add_argument("--log-ate", type=int, default=2000,
                        help="registra o log ate este instante (0 = simulacao inteira)")
        ap.add_argument("--ao-vivo", action="store_true", help="mostra a execucao em tempo real")
        ap.add_argument("--atraso", type=int, default=150, help="ms por unidade no modo ao vivo")
        ap.add_argument("--saida", default="saida", help="pasta dos arquivos gerados")
        a = ap.parse_args()

    if a.quantum < 1 or a.quadros < 1 or a.custo_troca < 0 or a.custo_fault < 1:
        sys.exit("parametros invalidos")

    with open(a.arquivo) as f:
        processos = json.load(f)

    os.makedirs(a.saida, exist_ok=True)
    base = os.path.join(a.saida, nome_execucao(a))
    log = open(base + "_log.txt", "w") if a.log else None

    params = OrderedDict([
        ("arquivo", a.arquivo), ("processos", len(processos)), ("cpus", a.cpus),
        ("sched", a.sched), ("quantum", a.quantum if a.sched == "rr" else "-"),
        ("subst", a.subst), ("quadros", a.quadros), ("custo-troca", a.custo_troca),
        ("custo-fault", a.custo_fault)])

    inicio = time.time()
    try:
        r = simular(processos, a.cpus, a.sched, a.quantum, a.subst, a.quadros,
                    a.custo_troca, a.custo_fault, log, a.ao_vivo, a.atraso / 1000,
                    log_ate=a.log_ate)
    except KeyboardInterrupt:
        sys.exit("\nsimulacao interrompida")
    finally:
        if log:
            log.close()

    texto = resumo(r, params)
    print(texto)
    print("tempo de execucao do simulador: %.2f s" % (time.time() - inicio))
    with open(base + "_resumo.txt", "w") as f:
        f.write(texto + "\n")
    with open(base + "_gantt.csv", "w") as f:
        f.write(gantt_csv(r["gantt"]))
    print("arquivos: %s_resumo.txt, %s_gantt.csv%s" % (
        base, base, ", %s_log.txt" % base if a.log else ""))


if __name__ == "__main__":
    main()
