#!/usr/bin/env python3
"""Confere o simulador contra os exemplos do enunciado (seções 5.4, 5.5 e 7).

    python3 testes.py
"""
import json
import os

from simulador import simular, metricas

PASTA = os.path.join(os.path.dirname(os.path.abspath(__file__)), "exemplos")


def roda(arq, **kw):
    with open(os.path.join(PASTA, arq)) as f:
        return simular(json.load(f), **kw)


def por_pid(r, campo):
    return [campo(p) for p in sorted(r["procs"], key=lambda p: p.pid)]


espera = lambda p: p.espera
turnaround = lambda p: p.fim - p.chegada
resposta = lambda p: p.primeiro_despacho - p.chegada

falhas = 0


def confere(nome, obtido, esperado):
    global falhas
    ok = obtido == esperado
    falhas += not ok
    print("%-4s %-45s obtido=%s esperado=%s" % ("OK" if ok else "FALHOU", nome, obtido, esperado))


# 5.4 - processo 17 sem page faults (bursts sem refs) e sem troca
r = roda("ex17_sem_memoria.json", custo_troca=0)
confere("5.4 turnaround P17", por_pid(r, turnaround), [19])
confere("5.4 espera P17", por_pid(r, espera), [0])

# 5.5 - processo 17 com page faults: 1 CPU, troca ignorada, fault = 20
r = roda("ex17.json", custo_troca=0, custo_fault=20)
confere("5.5 turnaround P17", por_pid(r, turnaround), [99])
confere("5.5 termino P17", r["procs"][0].fim, 441)
confere("5.5 page faults", r["faults"], 4)

# 7.1 - sem memória, 1 CPU, FCFS, troca = 1
r = roda("ex71.json", sched="fcfs", custo_troca=1)
c = r["cpu"][0]
confere("7.1 espera", por_pid(r, espera), [0, 6, 8])
confere("7.1 resposta", por_pid(r, resposta), [0, 6, 8])
confere("7.1 turnaround", por_pid(r, turnaround), [6, 10, 11])
confere("7.1 util/troca/ocioso/makespan", (c.util, c.troca, c.ocioso, r["makespan"]), (10, 3, 0, 13))
m = metricas(r)
confere("7.1 espera media", round(m["espera_media"], 2), 4.67)
confere("7.1 turnaround medio", m["turnaround_medio"], 9)

# 7.1 refeito com SJF e RR (q=2), calculado à mão:
#   SJF: troca 0-1, P2 1-4 | troca 4-5, P3 5-7 | troca 7-8, P1 8-13
r = roda("ex71.json", sched="sjf", custo_troca=1)
confere("7.1 SJF espera", por_pid(r, espera), [7, 0, 2])
confere("7.1 SJF makespan", r["makespan"], 13)
#   RR q=2: P1 1-3 | P2 4-6 | P3 7-9 (fim) | P1 10-12 | P2 13-14 (fim) | P1 15-16 (fim)
r = roda("ex71.json", sched="rr", quantum=2, custo_troca=1)
confere("7.1 RR q2 espera", por_pid(r, espera), [8, 9, 4])
confere("7.1 RR q2 termino", por_pid(r, lambda p: p.fim), [16, 14, 9])
confere("7.1 RR q2 trocas", r["cpu"][0].n_trocas, 6)

# 7.2 - page faults e fila: 1 CPU, FCFS, troca = 0, fault = 5
r = roda("ex72.json", sched="fcfs", custo_troca=0, custo_fault=5)
confere("7.2 espera", por_pid(r, espera), [0, 0, 0])
confere("7.2 turnaround", por_pid(r, turnaround), [12, 12, 6])
confere("7.2 makespan", r["makespan"], 13)

# 7.3 - a fila aparece: 1 CPU, troca = 0, fault = 20
r = roda("ex73.json", sched="fcfs", custo_troca=0, custo_fault=20)
confere("7.3 FCFS espera", por_pid(r, espera), [0, 4, 7])
confere("7.3 FCFS termino", por_pid(r, lambda p: p.fim), [24, 27, 29])
r = roda("ex73.json", sched="sjf", custo_troca=0, custo_fault=20)
confere("7.3 SJF espera", por_pid(r, espera), [5, 2, 0])
confere("7.3 SJF termino", por_pid(r, lambda p: p.fim), [29, 25, 22])

print("\n%s" % ("todos os testes passaram" if not falhas else "%d teste(s) falharam" % falhas))
raise SystemExit(1 if falhas else 0)
