#!/usr/bin/env python3
"""Gera as tabelas de análise a partir dos CSVs brutos."""

import argparse
import csv
import statistics
import sys
from collections import defaultdict
from pathlib import Path

ESTR_MPI = ("contiguous", "cyclic")
NS = (1, 2, 4, 8)
ORDEM = ("official", "zoom", "strong", "weak")


def ler(caminho):
    with open(caminho, encoding="utf-8-sig", newline="") as f:
        return list(csv.DictReader(f, delimiter=";"))


def salvar(caminho, linhas):
    if not linhas:
        return
    with open(caminho, "w", encoding="utf-8", newline="") as f:
        w = csv.DictWriter(f, fieldnames=list(linhas[0]), delimiter=";", lineterminator="\n")
        w.writeheader()
        w.writerows(linhas)


def num(x):
    return float(x.replace(",", "."))


def fmt(v):
    return format(v, ".15g")


def pegar(linhas, **filtro):
    """Linhas que batem com o filtro, em ordem de repetição; exige 5 repetições."""
    r = [l for l in linhas if all(l[k] == v for k, v in filtro.items())]
    r.sort(key=lambda l: int(l["repetition"]))
    if len(r) != 5:
        raise ValueError(f"Esperadas 5 repetições para {filtro}; encontradas {len(r)}")
    return r


def valores(linhas, col):
    return [num(l[col]) for l in linhas]


def mediana(linhas, col):
    return statistics.median(valores(linhas, col))


def resumo(linhas, col):
    v = valores(linhas, col)
    return {"median_s": fmt(statistics.median(v)), "min_s": fmt(min(v)), "max_s": fmt(max(v))}


def tempos(linhas, col):
    return "|".join(l[col] for l in linhas)


def lista(v):
    return "|".join(format(x, ".9f") for x in v)


def main():
    raiz = Path(__file__).resolve().parent.parent / "resultados"
    p = argparse.ArgumentParser(description="Gera tabelas derivadas dos CSVs brutos.")
    p.add_argument("--results-dir", type=Path, default=raiz)
    p.add_argument("--data-dir", type=Path, default=None,
                   help="Pasta com os CSVs brutos (padrão: resultados/dados).")
    p.add_argument("--output-dir", type=Path, default=raiz / "analise_reproduzida",
                   help="Pasta nova para as tabelas; não pode existir.")
    args = p.parse_args()
    saida = args.output_dir.resolve()
    dados = (args.data_dir or args.results_dir / "dados").resolve()
    if saida.exists():
        raise ValueError(f"A pasta de saída já existe; nada será sobrescrito: {saida}")

    serial = ler(dados / "serial_raw.csv")
    omp = ler(dados / "openmp_raw.csv")
    mpi = ler(dados / "mpi_raw_runs.csv")
    ranks = ler(dados / "mpi_raw_ranks.csv")
    arq_pareado = dados / "controle-p1" / "paired_raw.csv"
    pareado = ler(arq_pareado) if arq_pareado.is_file() else []

    # FBC de cada execução MPI: (Tmax - Tmin) / Tmax sobre o compute_s dos ranks
    chave = ("experiment", "strategy", "processes", "width", "height", "max_iter", "repetition")
    t_rank = defaultdict(list)
    for r in ranks:
        t_rank[tuple(r[c] for c in chave)].append(num(r["compute_s"]))

    def fbc_mpi(linha):
        t = t_rank[tuple(linha[c] for c in chave)]
        if len(t) != int(linha["processes"]):
            raise ValueError(f"Ranks incompletos na execução {tuple(linha[c] for c in chave)}")
        return (max(t) - min(t)) / max(t) if max(t) else 0.0

    # --- official e zoom ---
    # No caso oficial, OpenMP e MPI usam a coleta "strong" com p=8 (é o que o relatório publica).
    serial_ref = {c: pegar(serial, experiment=c) for c in ("official", "zoom")}
    med_serial = {c: mediana(l, "total_s") for c, l in serial_ref.items()}

    def linha_oz(cenario, impl, estr, n, fonte, linhas, col, bruto):
        v = valores(linhas, col)
        sp = med_serial[cenario] / statistics.median(v)
        return {"scenario": cenario, "implementation": impl, "strategy": estr, "workers": n,
                "n": len(v), "source_experiment": fonte, **resumo(linhas, col),
                "times_s": tempos(linhas, col) if bruto else "|".join(fmt(x) for x in v),
                "speedup_vs_serial": fmt(sp), "efficiency_vs_serial": fmt(sp / n)}

    oficial_zoom = [
        linha_oz("official", "Serial", "serial", 1, "official", serial_ref["official"], "total_s", False),
        linha_oz("official", "OpenMP", "static", 8, "strong",
                 pegar(omp, experiment="strong", strategy="static", threads="8"), "total_s", False),
        linha_oz("zoom", "Serial", "serial", 1, "zoom", serial_ref["zoom"], "total_s", False),
        linha_oz("zoom", "OpenMP", "static", 8, "zoom",
                 pegar(omp, experiment="zoom", strategy="static", threads="8"), "total_s", False),
    ]
    for cenario, fonte in (("official", "strong"), ("zoom", "zoom")):
        for estr in ESTR_MPI:
            linhas = pegar(mpi, experiment=fonte, strategy=estr, processes="8")
            oficial_zoom.append(linha_oz(cenario, "MPI", estr, 8, fonte, linhas, "total_max_s", True))

    # --- escalabilidade forte ---
    forte = []
    for impl, tabela, campo, col, estrategias in (
            ("OpenMP", omp, "threads", "total_s", ("static",)),
            ("MPI", mpi, "processes", "total_max_s", ESTR_MPI)):
        for estr in estrategias:
            por_n = {n: pegar(tabela, experiment="strong", strategy=estr, **{campo: str(n)}) for n in NS}
            med_p1 = mediana(por_n[1], col)
            for n in NS:
                med = mediana(por_n[n], col)
                sp_serial, sp_p1 = med_serial["official"] / med, med_p1 / med
                forte.append({
                    "implementation": impl, "strategy": estr, "workers": n, "n": 5,
                    **resumo(por_n[n], col), "times_s": tempos(por_n[n], col),
                    "speedup_vs_serial": fmt(sp_serial), "efficiency_vs_serial": fmt(sp_serial / n),
                    "speedup_vs_p1": fmt(sp_p1), "efficiency_vs_p1": fmt(sp_p1 / n)})

    # --- escalabilidade fraca (referência: p=1 da mesma implementação/estratégia) ---
    fraca_omp = []
    p1 = mediana(pegar(omp, experiment="weak", strategy="static", threads="1"), "total_s")
    for n in NS:
        linhas = pegar(omp, experiment="weak", strategy="static", threads=str(n))
        w, h = int(linhas[0]["width"]), int(linhas[0]["height"])
        fraca_omp.append({
            "implementation": "OpenMP", "workers": n, "width": w, "height": h,
            "pixels": w * h, "pixels_per_worker": fmt(w * h / n), "n": 5,
            **resumo(linhas, "total_s"), "times_s": tempos(linhas, "total_s"),
            "weak_efficiency": fmt(p1 / mediana(linhas, "total_s"))})

    fraca_mpi = []
    for estr in ESTR_MPI:
        p1 = mediana(pegar(mpi, experiment="weak", strategy=estr, processes="1"), "total_max_s")
        for n in NS:
            linhas = pegar(mpi, experiment="weak", strategy=estr, processes=str(n))
            r = resumo(linhas, "total_max_s")
            fbcs = [fbc_mpi(l) for l in linhas]
            fraca_mpi.append({
                "strategy": estr, "processes": n, "width": int(linhas[0]["width"]),
                "height": int(linhas[0]["height"]), "max_iter": int(linhas[0]["max_iter"]),
                "repetitions": 5, "median_total_s": r["median_s"], "min_total_s": r["min_s"],
                "max_total_s": r["max_s"], "weak_efficiency": fmt(p1 / mediana(linhas, "total_max_s")),
                "median_fbc": fmt(statistics.median(fbcs)), "fbc_per_run": lista(fbcs),
                "times_s": tempos(linhas, "total_max_s")})

    # --- FBC (MPI calculado dos ranks; OpenMP vem pronto do benchmark) ---
    fbc = []
    for cenario, fonte in (("official", "strong"), ("zoom", "zoom")):
        for estr in ESTR_MPI:
            fbcs = [fbc_mpi(l) for l in pegar(mpi, experiment=fonte, strategy=estr, processes="8")]
            fbc.append({"scenario": cenario, "source_experiment": fonte, "implementation": "MPI",
                        "strategy": estr, "workers": 8, "n": 5,
                        "median_fbc": fmt(statistics.median(fbcs)), "fbc_by_run": lista(fbcs),
                        "definition": "(Tmax-Tmin)/Tmax over rank compute_s"})
    for n in NS:
        fbcs = valores(pegar(omp, experiment="strong", strategy="static", threads=str(n)), "fbc_maxmin")
        fbc.append({"scenario": "strong", "source_experiment": "strong", "implementation": "OpenMP",
                    "strategy": "static", "workers": n, "n": 5,
                    "median_fbc": fmt(statistics.median(fbcs)), "fbc_by_run": lista(fbcs),
                    "definition": "benchmark per-run FBC"})

    # FBC de todas as configurações MPI
    combos = sorted({(l["experiment"], l["strategy"], int(l["processes"])) for l in mpi},
                    key=lambda c: (ORDEM.index(c[0]), c[1], c[2]))
    fbc_todos = []
    for exp, estr, n in combos:
        fbcs = [fbc_mpi(l) for l in pegar(mpi, experiment=exp, strategy=estr, processes=str(n))]
        fbc_todos.append({"scenario": exp, "processes": n, "strategy": estr, "n_repetitions": 5,
                          "median_fbc": fmt(statistics.median(fbcs)), "fbc_each_run": lista(fbcs),
                          "formula": "(Tmax-Tmin)/Tmax using per-rank compute_s"})

    # --- tempos MPI: comunicação, gather e total ---
    tempos_mpi = []
    for exp, ns in (("strong", NS), ("zoom", (8,))):
        for estr in ESTR_MPI:
            for n in ns:
                linhas = pegar(mpi, experiment=exp, strategy=estr, processes=str(n))
                for metrica, col in (("communication", "communication_max_s"),
                                     ("MPI_Gatherv_elapsed", "gather_max_s"),
                                     ("total", "total_max_s")):
                    tempos_mpi.append({
                        "scenario": "official" if exp == "strong" else "zoom",
                        "source_experiment": exp, "strategy": estr, "processes": n,
                        "metric": metrica, "n": 5, **resumo(linhas, col),
                        "times_s": tempos(linhas, col),
                        "note": "gather includes wait; cyclic also includes root reordering; intervals overlap"})

    # --- controle pareado Serial x OpenMP p=1 (opcional) ---
    resumo_pareado = []
    if pareado:
        pareado.sort(key=lambda l: int(l["repetition"]))
        s, o = valores(pareado, "serial_s"), valores(pareado, "openmp_s")
        razoes = [b / a for a, b in zip(s, o)]
        difs = [b - a for a, b in zip(s, o)]
        resumo_pareado = [{
            "scenario": "official 4096x4096 max_iter=1000", "repetitions": len(pareado),
            "serial_median_s": fmt(statistics.median(s)), "openmp_p1_median_s": fmt(statistics.median(o)),
            "median_paired_ratio_openmp_over_serial": fmt(statistics.median(razoes)),
            "median_paired_difference_s": fmt(statistics.median(difs)),
            "serial_times_s": tempos(pareado, "serial_s"), "openmp_times_s": tempos(pareado, "openmp_s"),
            "paired_ratios": "|".join(format(r, ".6f") for r in razoes)}]

    # --- escrita ---
    saida.mkdir(parents=True)
    salvar(saida / "official_zoom.csv", oficial_zoom)
    salvar(saida / "strong_scaling.csv", forte)
    salvar(saida / "weak_scaling_openmp.csv", fraca_omp)
    salvar(saida / "mpi_weak.csv", fraca_mpi)
    salvar(saida / "fbc.csv", fbc)
    salvar(saida / "mpi_fbc_all.csv", fbc_todos)
    salvar(saida / "mpi_times.csv", tempos_mpi)
    salvar(saida / "serial_openmp_p1.csv", resumo_pareado)

    meta = [
        f"data_directory={dados}",
        f"serial_raw_rows={len(serial)}", f"openmp_raw_rows={len(omp)}",
        f"mpi_raw_run_rows={len(mpi)}", f"mpi_raw_rank_rows={len(ranks)}",
        f"paired_raw_rows={len(pareado)}",
        "statistics=median, minimum and maximum of the five repetitions in each configuration",
        "speedup=reference median divided by the parallel median; efficiency=speedup divided by workers",
        "weak_efficiency=one-worker median of the same implementation/strategy divided by the scaled-problem median",
        "mpi_fbc=(maximum rank compute_s - minimum rank compute_s)/maximum rank compute_s, per run, then median",
        "openmp_fbc=benchmark fbc_maxmin field",
        "mpi_times: communication, gather and total are overlapping intervals and must not be summed",
        "official OpenMP and MPI p=8 values use experiment=strong (see source_experiment)",
        "serial_openmp_p1.csv is generated only when controle-p1/paired_raw.csv is present",
    ]
    (saida / "analysis_metadata.txt").write_text("\n".join(meta) + "\n", encoding="utf-8")
    print(f"Tabelas reproduzidas em: {saida}")


if __name__ == "__main__":
    try:
        main()
    except (OSError, ValueError, KeyError) as e:
        print(f"Erro: {e}", file=sys.stderr)
        sys.exit(2)