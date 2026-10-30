"""Coleta os cenários Serial, OpenMP e MPI do Mandelbrot"""

import argparse
import csv
import datetime
import getpass
import json
import os
import platform
import shutil
import socket
import subprocess
import sys
from pathlib import Path

CONTAGENS = (1, 2, 4, 8)
OFICIAL = dict(width=4096, height=4096, iterations=1000, bounds=("-2", "1", "-1.5", "1.5"))
ZOOM = dict(width=4096, height=4096, iterations=5000,
            bounds=("-0.745143887", "-0.742143887", "0.130325904", "0.133325904"))
LADO_FRACA = {1: 4096, 2: 5793, 4: 8192, 8: 11585}


def cenarios(suite, contagens):
    if suite == "official":
        return [dict(OFICIAL, name="official")]
    if suite == "zoom":
        return [dict(ZOOM, name="zoom")]
    if suite == "strong":
        return [dict(OFICIAL, name="strong", workers=n) for n in contagens]
    if suite == "weak":
        return [dict(OFICIAL, name="weak", workers=n, width=LADO_FRACA[n], height=LADO_FRACA[n])
                for n in contagens]
    return [c for s in ("official", "zoom", "strong", "weak") for c in cenarios(s, contagens)]


def executar(comando, log):
    r = subprocess.run(comando, text=True, stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
                       encoding="utf-8", errors="replace")
    saida = r.stdout or ""
    log.parent.mkdir(parents=True, exist_ok=True)
    log.write_text("COMMAND_ARGV_JSON: " + json.dumps(comando, ensure_ascii=False) + "\n" + saida,
                   encoding="utf-8")
    if r.returncode != 0:
        raise RuntimeError(f"Comando falhou ({r.returncode}): {comando!r}\nConsulte {log}\n{saida}")
    return saida


def ultimo_registro(saida, prefixo, log):
    linhas = [l.strip() for l in saida.splitlines() if l.startswith(prefixo + ";")]
    if not linhas:
        raise RuntimeError(f"Registro {prefixo} ausente em {log}")
    return linhas[-1].split(";")


def salvar_csv(caminho, linhas):
    if not linhas:
        return
    with caminho.open("w", encoding="utf-8", newline="") as f:
        w = csv.DictWriter(f, fieldnames=list(linhas[0]), delimiter=";", lineterminator="\n")
        w.writeheader()
        w.writerows(linhas)


def versao(programa):
    caminho = shutil.which(programa)
    if not caminho:
        return "unavailable"
    try:
        r = subprocess.run([caminho, "--version"], text=True, stdout=subprocess.PIPE,
                           stderr=subprocess.STDOUT, encoding="utf-8", errors="replace", timeout=10)
        return r.stdout.splitlines()[0] if r.stdout else f"exit={r.returncode}"
    except (OSError, subprocess.TimeoutExpired) as e:
        return f"unavailable: {e}"


def main():
    p = argparse.ArgumentParser(description="Coleta os cenários Serial, OpenMP e MPI.")
    p.add_argument("--bin-dir", type=Path, required=True,
                   help="Pasta com benchmark_serial, benchmark_openmp e mandelbrot_mpi.")
    p.add_argument("--output-dir", type=Path, required=True,
                   help="Pasta nova para CSVs, logs e metadados; não pode existir.")
    p.add_argument("--implementation", choices=("serial", "openmp", "mpi", "all"), default="all")
    p.add_argument("--suite", choices=("official", "zoom", "strong", "weak", "all"), default="all")
    p.add_argument("--mpi-strategy", choices=("contiguous", "cyclic", "both"), default="both")
    p.add_argument("--repetitions", type=int, default=5)
    p.add_argument("--warmups", type=int, default=0)
    p.add_argument("--threads", type=int, default=8)
    p.add_argument("--processes", type=int, default=8)
    p.add_argument("--scaling-counts", default="1,2,4,8")
    p.add_argument("--mpiexec", default="mpiexec.exe" if os.name == "nt" else "mpiexec")
    args = p.parse_args()

    if args.repetitions < 1 or args.warmups < 0 or args.threads < 1 or args.processes < 1:
        p.error("repetitions, threads e processes devem ser positivos; warmups não pode ser negativo")
    try:
        contagens = [int(v) for v in args.scaling_counts.split(",") if v]
    except ValueError:
        p.error("--scaling-counts deve ser uma lista como 1,2,4,8")
    if not contagens or any(v not in CONTAGENS for v in contagens):
        p.error("--scaling-counts aceita somente 1, 2, 4 e 8")
    if len(set(contagens)) != len(contagens):
        p.error("--scaling-counts não pode repetir valores")
    if args.suite in ("strong", "weak", "all") and 1 not in contagens:
        p.error("--scaling-counts deve incluir 1 em escalabilidade")

    bin_dir = args.bin_dir.resolve()
    ext = ".exe" if os.name == "nt" else ""
    exe = {"serial": bin_dir / f"benchmark_serial{ext}",
           "openmp": bin_dir / f"benchmark_openmp{ext}",
           "mpi": bin_dir / f"mandelbrot_mpi{ext}"}
    tipos = ("serial", "openmp", "mpi") if args.implementation == "all" else (args.implementation,)
    for t in tipos:
        if not exe[t].is_file():
            p.error(f"Executável não encontrado: {exe[t]}")
    mpiexec = shutil.which(args.mpiexec)
    if "mpi" in tipos and not mpiexec:
        p.error(f"Launcher MPI não encontrado no PATH: {args.mpiexec}")

    saida_dir = args.output_dir.resolve()
    if saida_dir.exists():
        p.error(f"Recusando sobrescrever resultados: {saida_dir}")
    log_dir = saida_dir / "logs"
    log_dir.mkdir(parents=True)

    serial, openmp, mpi, ranks = [], [], [], []
    n_exec = 0

    for c in cenarios(args.suite, contagens):
        nome, w, h, it = c["name"], c["width"], c["height"], c["iterations"]
        b = c["bounds"]
        # serial só roda nos cenários de uma única configuração
        usar = tipos if nome in ("official", "zoom") else [t for t in tipos if t != "serial"]

        for tipo in usar:
            if tipo == "serial":
                n, variantes = 1, ["serial"]
            elif tipo == "openmp":
                n, variantes = c.get("workers", args.threads), ["static"]
            else:
                n = c.get("workers", args.processes)
                variantes = ["contiguous", "cyclic"] if args.mpi_strategy == "both" else [args.mpi_strategy]

            for var in variantes:
                dims = [str(w), str(h), str(it), *b]
                if tipo == "serial":
                    cmd = [str(exe["serial"]), *dims]
                elif tipo == "openmp":
                    cmd = [str(exe["openmp"]), str(n), *dims, "static", "1"]
                else:
                    cmd = [mpiexec, "-n", str(n), str(exe["mpi"]), "--strategy", var, "--no-write",
                           "--largura", str(w), "--altura", str(h), "--max-iter", str(it),
                           "--re-min", b[0], "--re-max", b[1], "--im-min", b[2], "--im-max", b[3]]
                cmd_json = json.dumps(cmd, ensure_ascii=False)

                for k in range(1, args.warmups + 1):
                    executar(cmd, log_dir / f"{nome}_{tipo}_{var}_n{n}_warmup{k}.log")

                for rep in range(1, args.repetitions + 1):
                    n_exec += 1
                    log = log_dir / f"{n_exec:03d}_{nome}_{tipo}_{var}_n{n}_rep{rep}.log"
                    saida = executar(cmd, log)

                    if tipo == "serial":
                        f = ultimo_registro(saida, "SERIAL", log)
                        serial.append({
                            "experiment": nome, "workers": 1, "width": w, "height": h,
                            "max_iter": it, "repetition": rep, "total_s": float(f[4]),
                            "command": cmd_json, "log": log.name})

                    elif tipo == "openmp":
                        f = ultimo_registro(saida, "OMP", log)
                        openmp.append({
                            "experiment": nome, "strategy": "static", "threads": int(f[1]),
                            "width": int(f[2]), "height": int(f[3]), "max_iter": int(f[4]),
                            "schedule": f[5], "chunk": int(f[6]), "repetition": rep,
                            "total_s": float(f[7]), "compute_min_s": float(f[8]),
                            "compute_mean_s": float(f[9]), "compute_max_s": float(f[10]),
                            "fbc_maxmin": float(f[12]), "command": cmd_json, "log": log.name})

                    else:
                        f = ultimo_registro(saida, "RUN", log)
                        mpi.append({
                            "experiment": nome, "strategy": var, "processes": n,
                            "width": w, "height": h, "max_iter": it, "repetition": rep,
                            "total_max_s": float(f[6]), "compute_min_s": float(f[7]),
                            "compute_max_s": float(f[8]), "distribution_max_s": float(f[9]),
                            "communication_max_s": float(f[10]), "gather_max_s": float(f[11]),
                            "write_s": float(f[12]), "fbc_all": float(f[13]),
                            "fbc_workers": float(f[14]), "write_ok": int(f[15]),
                            "command": cmd_json, "log": log.name})
                        for linha in saida.splitlines():
                            if linha.startswith("RANK;"):
                                r = linha.split(";")
                                ranks.append({
                                    "experiment": nome, "strategy": var, "processes": n,
                                    "width": w, "height": h, "max_iter": it, "repetition": rep,
                                    "rank": int(r[3]), "rows": int(r[4]), "tasks": int(r[5]),
                                    "compute_s": float(r[6]), "distribution_s": float(r[7]),
                                    "communication_s": float(r[8]), "gather_s": float(r[9]),
                                    "total_s": float(r[10]), "log": log.name})

    salvar_csv(saida_dir / "serial_raw.csv", serial)
    salvar_csv(saida_dir / "openmp_raw.csv", openmp)
    salvar_csv(saida_dir / "mpi_raw_runs.csv", mpi)
    salvar_csv(saida_dir / "mpi_raw_ranks.csv", ranks)

    usa_mpi = "mpi" in tipos
    meta = [
        f"date={datetime.datetime.now().astimezone().isoformat(timespec='seconds')}",
        f"operator={getpass.getuser()}", f"computer={socket.gethostname()}",
        f"os={platform.platform()}", f"cpu_count={os.cpu_count()}",
        f"python={sys.version.split()[0]}", f"gcc={versao('gcc')}", f"mpicc={versao('mpicc')}",
        f"mpi_launcher={mpiexec if usa_mpi else 'not used'}",
        f"mpi_launcher_version={versao(args.mpiexec) if usa_mpi else 'not used'}",
        f"binary_directory={bin_dir}",
        f"repetitions={args.repetitions}", f"warmups={args.warmups}",
        f"implementations={args.implementation}", f"suite={args.suite}",
        f"threads={args.threads}", f"processes={args.processes}",
        f"mpi_strategy={args.mpi_strategy}", f"scaling_counts={','.join(map(str, contagens))}",
        f"serial_runs={len(serial)}", f"openmp_runs={len(openmp)}",
        f"mpi_runs={len(mpi)}", f"mpi_rank_rows={len(ranks)}",
        "mpi_clock=MPI_Wtime; mpi_total=maximum rank total_s",
        "mpi_write=disabled for MPI performance runs (--no-write)",
        "mpi_communication=configuration broadcast plus elapsed inside MPI_Gatherv including collective wait",
        "mpi_gather=elapsed MPI_Gatherv; cyclic also includes root line reordering",
        "serial_openmp_total=matrix generation call only",
    ]
    (saida_dir / "metadata.txt").write_text("\n".join(meta) + "\n", encoding="utf-8")

    print(f"Coleta concluída: {saida_dir}")
    print(f"Serial={len(serial)} OpenMP={len(openmp)} MPI={len(mpi)} ranks={len(ranks)}")


if __name__ == "__main__":
    try:
        main()
    except (OSError, RuntimeError, ValueError, IndexError) as e:
        print(f"Erro: {e}", file=sys.stderr)
        sys.exit(2)