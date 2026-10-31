#!/usr/bin/env python3
"""Generate BENCHMARKS.md from the benchmark runner's JSON lines.

Standard library only (Python 3.8+).

    python3 scripts/gen-benchmarks.py                    # find + run the bench binary
    python3 scripts/gen-benchmarks.py --bench PATH       # run this binary
    python3 scripts/gen-benchmarks.py --input FILE ...   # read JSON lines ('-' = stdin)
    python3 scripts/gen-benchmarks.py --output PATH      # default: <repo>/BENCHMARKS.md

Build the runner first:

    cmake --preset <preset> -DPORTCULLIS_BUILD_BENCH=ON
    cmake --build --preset <preset> --target portcullis_bench

Input is JSON lines; each line is classified by the keys it has, so the
runner, the layout report and the compile-time timings can be concatenated into
one stream (several --input flags are fine):

  throughput  {"format", "parser", "mbps", "ns_per_parse", "bytes", "sample",
               "iterations", "batch", "optimized"}               portcullis_bench
  layout      {"format", "fixed_prefix_v", "fixed_fields", "total_fields"}
                                                       bench/layout_report.cpp
  compile     {"schema", "compile_ms"}      per-schema wall-clock build time

Any number of parsers per format is rendered; a row whose format also has a
"handwritten" row gets a throughput ratio against it. Sections with no input
are replaced by a one-line note, so the document never implies a measurement
that was not made.
"""

from __future__ import annotations

import argparse
import datetime
import json
import os
import pathlib
import platform
import shutil
import subprocess
import sys

REPO = pathlib.Path(__file__).resolve().parent.parent
BASELINE_PARSER = "handwritten"


# --------------------------------------------------------------------------
# Getting the data


def find_bench_binary(repo: pathlib.Path):
    """Most recently built build/*/bench/portcullis_bench[.exe], or None."""
    found = []
    for pattern in ("build/*/bench/portcullis_bench", "build/*/bench/portcullis_bench.exe",
                    "build/bench/portcullis_bench", "build/bench/portcullis_bench.exe"):
        found.extend(p for p in repo.glob(pattern) if p.is_file())
    if not found:
        return None
    return max(found, key=lambda p: p.stat().st_mtime)


def run_bench(binary: pathlib.Path, iterations):
    cmd = [str(binary)]
    if iterations is not None:
        cmd += ["--iterations", str(iterations)]
    proc = subprocess.run(cmd, capture_output=True, text=True)
    if proc.stderr:
        sys.stderr.write(proc.stderr)
    if proc.returncode != 0:
        sys.exit("gen-benchmarks: %s exited with status %d" % (binary, proc.returncode))
    return proc.stdout


def read_input(spec: str) -> str:
    if spec == "-":
        return sys.stdin.read()
    return pathlib.Path(spec).read_text(encoding="utf-8")


def parse_records(text: str, source: str):
    """JSON-object lines -> dicts. Other lines are reported and skipped."""
    records = []
    for number, line in enumerate(text.splitlines(), 1):
        line = line.strip()
        if not line:
            continue
        if not line.startswith("{"):
            sys.stderr.write("gen-benchmarks: %s:%d: skipping non-JSON line\n" % (source, number))
            continue
        try:
            records.append(json.loads(line))
        except json.JSONDecodeError as err:
            sys.exit("gen-benchmarks: %s:%d: bad JSON: %s" % (source, number, err))
    return records


# --------------------------------------------------------------------------
# Provenance


def run_text(cmd, cwd=None):
    try:
        proc = subprocess.run(cmd, capture_output=True, text=True, cwd=cwd)
    except OSError:
        return None
    if proc.returncode != 0:
        return None
    return proc.stdout.strip()


def git_commit(repo: pathlib.Path) -> str:
    """Short hash, with -dirty if tracked files differ (BENCHMARKS.md excluded)."""
    short = run_text(["git", "rev-parse", "--short", "HEAD"], cwd=repo)
    if short is None:
        return "unknown"
    changed = run_text(["git", "status", "--porcelain", "--untracked-files=no", "--", ".",
                        ":(exclude)BENCHMARKS.md"], cwd=repo)
    return short + ("-dirty" if changed else "")


def cache_value(build_dir: pathlib.Path, key: str):
    cache = build_dir / "CMakeCache.txt"
    if not cache.is_file():
        return None
    prefix = key + ":"
    for line in cache.read_text(encoding="utf-8", errors="replace").splitlines():
        if line.startswith(prefix) and "=" in line:
            return line.split("=", 1)[1].strip()
    return None


def compiler_version(cxx: str) -> str:
    exe = shutil.which(cxx)
    if exe is None:
        return "%s --version: not found" % cxx
    out = run_text([exe, "--version"])
    return out if out else "%s --version: failed" % cxx


# --------------------------------------------------------------------------
# Rendering


def fmt_int(value) -> str:
    return "{:,.0f}".format(value)


def fmt_ns(value) -> str:
    return "{:,.2f}".format(value) if value < 100 else "{:,.1f}".format(value)


def table(header, aligns, rows):
    sep = {"l": ":---", "r": "---:"}
    lines = ["| " + " | ".join(header) + " |",
             "| " + " | ".join(sep[a] for a in aligns) + " |"]
    for row in rows:
        lines.append("| " + " | ".join(str(c) for c in row) + " |")
    return "\n".join(lines)


def ordered_unique(items):
    seen, out = set(), []
    for item in items:
        if item not in seen:
            seen.add(item)
            out.append(item)
    return out


def throughput_section(rows):
    # Last row wins for a repeated (format, parser); say so.
    by_key = {}
    for row in rows:
        key = (row["format"], row["parser"])
        if key in by_key:
            sys.stderr.write("gen-benchmarks: duplicate row for %s/%s; keeping the last\n" % key)
        by_key[key] = row

    formats = ordered_unique(r["format"] for r in rows)
    body = []
    for fmt in formats:
        parsers = ordered_unique(p for (f, p) in by_key if f == fmt)
        parsers.sort(key=lambda p: (p != BASELINE_PARSER,))  # baseline first, rest in order
        base = by_key.get((fmt, BASELINE_PARSER))
        for parser in parsers:
            row = by_key[(fmt, parser)]
            if base is None:
                ratio = "n/a"
            elif parser == BASELINE_PARSER:
                ratio = "1.00x"
            else:
                ratio = "{:.2f}x".format(row["mbps"] / base["mbps"])
            sample = "%s (%s B)" % (row.get("sample", "?"), fmt_int(row.get("bytes", 0)))
            body.append([fmt, parser, fmt_int(row["mbps"]), fmt_ns(row["ns_per_parse"]),
                         ratio, sample])

    return table(["Format", "Parser", "MB/s", "ns / parse", "Throughput vs handwritten", "Sample"],
                 ["l", "l", "r", "r", "r", "l"], body)


def layout_section(rows) -> str:
    body = []
    for row in rows:
        total = row["total_fields"]
        share = "{:.0f}%".format(100.0 * row["fixed_fields"] / total) if total else "n/a"
        body.append([row["format"], row["fixed_prefix_v"], row["fixed_fields"], total, share])
    return table(["Format", "`fixed_prefix_v` (bytes)", "Fields in the fixed prefix",
                  "Total fields", "Share fixed"], ["l", "r", "r", "r", "r"], body)


def compile_section(rows) -> str:
    body = [[row["schema"], "{:,.0f}".format(row["compile_ms"])] for row in rows]
    return table(["Schema", "Compile wall time (ms)"], ["l", "r"], body)


def render(throughput, layout, compile_rows, env) -> str:
    out = []
    out.append("# Benchmarks\n")
    out.append("Generated by `scripts/gen-benchmarks.py` from the runner's output, at commit "
               "`%s` on %s. Do not edit by hand; rerun the script.\n" % (env["commit"], env["date"]))

    if any(not r.get("optimized", True) for r in throughput):
        out.append("> **WARNING: at least one row was measured by a binary built without "
                   "optimisation. Those numbers are not meaningful. Rebuild with optimisation "
                   "and regenerate.**\n")

    out.append("## Environment\n")
    out.append(table(["", ""], ["l", "l"], [
        ["Commit", "`%s`" % env["commit"]],
        ["Compiler", "`%s` (first line of `%s --version`)" % (env["compiler_line"], env["cxx"])],
        ["Build", env["build"]],
        ["Host", env["host"]],
    ]))
    out.append("\n```\n%s\n```\n" % env["compiler_full"])

    out.append("## Parse throughput\n")
    if any(r["parser"] != BASELINE_PARSER for r in throughput):
        out.append("The honest cost of the guarantees: a handwritten, explicitly bounds-checked "
                   "C++ parser (`bench/handwritten/`) against Portcullis, parsing identical "
                   "bytes.\n")
    else:
        out.append("The handwritten, explicitly bounds-checked C++ parsers "
                   "(`bench/handwritten/`): the baseline Portcullis is measured against. "
                   "Portcullis rows are added when its parsers are integrated.\n")
    out.append(throughput_section(throughput))
    out.append("")
    out.append("How to read this:\n")
    iterations = ordered_unique(r.get("iterations") for r in throughput)
    out.append("- Each row is the **median of %s timed batches**; a batch is a run of back-to-back "
               "parses sized to take at least about 100 microseconds, because one parse is far "
               "below the resolution of a clock read. These are throughput numbers (no data "
               "dependency between parses), not single-parse latency." %
               "/".join(str(i) for i in iterations if i is not None))
    out.append("- 1 MB = 10^6 bytes. The parsers validate **structure** and never read payload "
               "bytes (a heartbeat payload, PNG `IDAT` data, TIFF value areas), so MB/s grows "
               "with the share of the sample that is payload. Compare parsers **on the same "
               "row's sample**; do not compare MB/s across formats. `ns / parse` is the number "
               "to read across formats.")
    out.append("- Run-to-run spread on a laptop is easily 10-25% (frequency scaling, other "
               "load). Read ratios from one run, and treat differences under that as noise.")
    out.append("- What each handwritten parser checks, and what it leaves to the caller, is "
               "written at the top of its header in `bench/handwritten/`. The Portcullis schema "
               "it is compared with must check the same things, or the ratio is not like for "
               "like.\n")

    out.append("## Checks resolved at compile time\n")
    if layout:
        out.append(layout_section(layout))
        out.append("")
    else:
        out.append("_Not measured yet: the layout report (`fixed_prefix_v` and field counts per "
                   "schema) is added with the Portcullis rows._\n")

    out.append("## Compile time per schema\n")
    if compile_rows:
        out.append(compile_section(compile_rows))
        out.append("")
    else:
        out.append("_Not measured yet: per-schema compile wall time is added with the Portcullis "
                   "rows._\n")

    return "\n".join(out).rstrip("\n") + "\n"


# --------------------------------------------------------------------------


def main(argv=None) -> int:
    ap = argparse.ArgumentParser(description=__doc__.split("\n", 1)[0])
    ap.add_argument("--input", action="append", default=[], metavar="FILE",
                    help="JSON-lines file to read ('-' for stdin); repeatable. "
                         "If given, the runner is not executed.")
    ap.add_argument("--bench", metavar="PATH", help="runner binary to execute "
                    "(default: newest build/*/bench/portcullis_bench)")
    ap.add_argument("--iterations", type=int, metavar="N",
                    help="passed to the runner as --iterations N")
    ap.add_argument("--output", metavar="PATH", default=str(REPO / "BENCHMARKS.md"))
    ap.add_argument("--cxx", metavar="CMD",
                    help="compiler to report (default: CMAKE_CXX_COMPILER of the runner's build "
                         "directory, else clang++)")
    args = ap.parse_args(argv)

    bench_path = None
    texts = []
    if args.input:
        texts = [(spec, read_input(spec)) for spec in args.input]
    else:
        bench_path = pathlib.Path(args.bench) if args.bench else find_bench_binary(REPO)
        if bench_path is None or not bench_path.is_file():
            sys.exit("gen-benchmarks: no portcullis_bench binary found. Build it with\n"
                     "  cmake --preset <preset> -DPORTCULLIS_BUILD_BENCH=ON\n"
                     "  cmake --build --preset <preset> --target portcullis_bench\n"
                     "or pass --bench PATH or --input FILE.")
        texts = [(str(bench_path), run_bench(bench_path, args.iterations))]

    records = []
    for source, text in texts:
        records.extend(parse_records(text, source))

    throughput = [r for r in records if "mbps" in r]
    layout = [r for r in records if "fixed_prefix_v" in r]
    compile_rows = [r for r in records if "compile_ms" in r]
    for r in records:
        if not ("mbps" in r or "fixed_prefix_v" in r or "compile_ms" in r):
            sys.stderr.write("gen-benchmarks: ignoring unrecognised record: %s\n" % json.dumps(r))
    if not throughput:
        sys.exit("gen-benchmarks: no throughput rows (records with \"mbps\") in the input")

    build_dir = bench_path.parent.parent if bench_path is not None else None
    cxx = args.cxx or (cache_value(build_dir, "CMAKE_CXX_COMPILER") if build_dir else None) \
        or os.environ.get("CXX") or "clang++"
    compiler_full = compiler_version(cxx)
    build_type = cache_value(build_dir, "CMAKE_BUILD_TYPE") if build_dir else None
    if build_dir is not None and build_type is not None:
        build = "`%s`, CMAKE_BUILD_TYPE=%s (the bench target is compiled with -O2 or better)" % (
            build_dir.name, build_type or "(none)")
    else:
        build = "runner output supplied via --input; build configuration not recorded"

    env = {
        "commit": git_commit(REPO),
        "date": datetime.date.today().isoformat(),
        "cxx": cxx,
        "compiler_full": compiler_full,
        "compiler_line": compiler_full.splitlines()[0] if compiler_full else cxx,
        "build": build,
        "host": "%s, %s, %s logical CPUs" % (platform.platform(), platform.machine(),
                                             os.cpu_count() or "?"),
    }

    text = render(throughput, layout, compile_rows, env)
    with open(args.output, "w", encoding="utf-8", newline="\n") as handle:
        handle.write(text)
    print("gen-benchmarks: wrote %s (%d throughput rows)" % (args.output, len(throughput)))
    return 0


if __name__ == "__main__":
    sys.exit(main())
