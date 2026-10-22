#!/usr/bin/env python3

"""Run the Phase 19 benchmark protocol and retain machine-readable evidence."""

from __future__ import annotations

import argparse
import csv
import hashlib
import json
import platform
import statistics
import subprocess
import sys
from pathlib import Path
from typing import Any


def parse_arguments() -> argparse.Namespace:
    parser = argparse.ArgumentParser()
    parser.add_argument("--build-dir", type=Path, required=True)
    parser.add_argument("--sample", type=Path, required=True)
    parser.add_argument("--full-day", type=Path)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--matching-samples", type=int, default=10_000)
    parser.add_argument("--matching-warmup", type=int, default=1_000)
    parser.add_argument("--matching-batch-size", type=int, default=64)
    parser.add_argument("--structure-operations", type=int, default=2_000_000)
    parser.add_argument("--structure-elements", type=int, default=65_536)
    parser.add_argument("--repetitions", type=int, default=7)
    parser.add_argument("--full-day-repetitions", type=int, default=3)
    arguments = parser.parse_args()

    numeric_values = (
        arguments.matching_samples,
        arguments.matching_warmup,
        arguments.matching_batch_size,
        arguments.structure_operations,
        arguments.structure_elements,
        arguments.repetitions,
        arguments.full_day_repetitions,
    )
    if any(value <= 0 for value in numeric_values):
        parser.error("all numeric benchmark parameters must be positive")
    return arguments


def run(command: list[str], cwd: Path, stdout_path: Path | None = None) -> None:
    print("+", " ".join(command), flush=True)
    if stdout_path is None:
        subprocess.run(command, cwd=cwd, check=True)
        return

    with stdout_path.open("w", encoding="utf-8") as output:
        subprocess.run(command, cwd=cwd, check=True, stdout=output)


def capture(command: list[str], cwd: Path) -> str | None:
    completed = subprocess.run(
        command,
        cwd=cwd,
        check=False,
        stdout=subprocess.PIPE,
        stderr=subprocess.STDOUT,
        text=True,
    )
    if completed.returncode != 0:
        return None
    return completed.stdout.strip()


def hardware_overview(repository: Path) -> dict[str, str] | None:
    raw = capture(["system_profiler", "SPHardwareDataType", "-json"], repository)
    if raw is None:
        return None
    try:
        payload = json.loads(raw)
        record = payload["SPHardwareDataType"][0]
    except (KeyError, IndexError, TypeError, json.JSONDecodeError):
        return None
    allowed = (
        "chip_type",
        "machine_model",
        "machine_name",
        "number_processors",
        "physical_memory",
    )
    return {key: str(record[key]) for key in allowed if key in record}


def sha256_file(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as source:
        while chunk := source.read(8 * 1024 * 1024):
            digest.update(chunk)
    return digest.hexdigest()


def source_fingerprint(repository: Path) -> str:
    digest = hashlib.sha256()
    roots = (
        repository / "CMakeLists.txt",
        repository / "cmake",
        repository / "include",
        repository / "source",
        repository / "benchmarks",
        repository / "tests",
        repository / "examples",
        repository / "fuzz",
    )
    files: list[Path] = []
    for root in roots:
        if root.is_file():
            files.append(root)
        elif root.is_dir():
            files.extend(path for path in root.rglob("*") if path.is_file())

    for path in sorted(files):
        relative = path.relative_to(repository).as_posix().encode("utf-8")
        digest.update(len(relative).to_bytes(8, "little"))
        digest.update(relative)
        digest.update(path.stat().st_size.to_bytes(8, "little"))
        with path.open("rb") as source:
            while chunk := source.read(1024 * 1024):
                digest.update(chunk)
    return digest.hexdigest()


def executable(build_directory: Path, name: str) -> Path:
    candidate = build_directory / "benchmarks" / name
    if not candidate.is_file():
        raise FileNotFoundError(f"benchmark executable is missing: {candidate}")
    return candidate


def median_rows(path: Path, keys: tuple[str, ...], numeric: tuple[str, ...]) -> list[dict[str, Any]]:
    with path.open(newline="", encoding="utf-8") as source:
        rows = list(csv.DictReader(source))

    groups: dict[tuple[str, ...], list[dict[str, str]]] = {}
    for row in rows:
        group = tuple(row[key] for key in keys)
        groups.setdefault(group, []).append(row)

    summaries: list[dict[str, Any]] = []
    for group, samples in sorted(groups.items()):
        summary: dict[str, Any] = dict(zip(keys, group, strict=True))
        summary["repetitions"] = len(samples)
        for field in numeric:
            summary[f"median_{field}"] = statistics.median(
                float(sample[field]) for sample in samples
            )
        summaries.append(summary)
    return summaries


def create_summary(output_directory: Path) -> dict[str, Any]:
    summary: dict[str, Any] = {}
    matching_directory = output_directory / "matching"
    summary["matching_latency"] = median_rows(
        matching_directory / "matching_latency_summary.csv",
        ("benchmark",),
        (
            "mean_batch_average_ns",
            "p50_batch_average_ns",
            "p90_batch_average_ns",
            "p99_batch_average_ns",
            "p99_9_batch_average_ns",
            "max_batch_average_ns",
        ),
    )
    summary["matching_throughput"] = median_rows(
        matching_directory / "matching_throughput.csv",
        ("benchmark",),
        ("ns_per_command", "commands_per_second"),
    )
    summary["structures"] = median_rows(
        output_directory / "structures" / "structure_summary.csv",
        ("benchmark", "implementation"),
        ("ns_per_operation", "operations_per_second"),
    )

    pipeline_directories = (
        "itch-sample",
        "itch-full-parser",
        "itch-full-reconstruction",
        "itch-full-reconstruction-counting",
    )
    pipeline_summary: list[dict[str, Any]] = []
    for directory_name in pipeline_directories:
        path = output_directory / directory_name / "itch_pipeline_summary.csv"
        if not path.is_file():
            continue
        rows = median_rows(
            path,
            ("benchmark", "input_mode"),
            (
                "total_ns",
                "ns_per_message",
                "messages_per_second",
                "gib_per_second",
                "peak_rss_bytes",
            ),
        )
        for row in rows:
            row["dataset"] = directory_name
        pipeline_summary.extend(rows)
    summary["itch_pipeline"] = pipeline_summary
    return summary


def main() -> int:
    arguments = parse_arguments()
    repository = Path(__file__).resolve().parent.parent
    build_directory = arguments.build_dir.resolve()
    output_directory = arguments.output.resolve()
    sample = arguments.sample.resolve()
    full_day = arguments.full_day.resolve() if arguments.full_day else None

    if not sample.is_file():
        raise FileNotFoundError(f"sample dataset is missing: {sample}")
    if full_day is not None and not full_day.is_file():
        raise FileNotFoundError(f"full-day dataset is missing: {full_day}")

    output_directory.mkdir(parents=True, exist_ok=True)
    run(
        ["cmake", "--build", str(build_directory), "--config", "Release", "-j"],
        repository,
    )

    layout = executable(build_directory, "layout_report")
    matching = executable(build_directory, "matching_latency_benchmark")
    structures = executable(build_directory, "structure_benchmark")
    pipeline = executable(build_directory, "itch_pipeline_benchmark")

    run([str(layout)], repository, output_directory / "layout.csv")
    run(
        [
            str(matching),
            "--samples",
            str(arguments.matching_samples),
            "--warmup",
            str(arguments.matching_warmup),
            "--repetitions",
            str(arguments.repetitions),
            "--batch-size",
            str(arguments.matching_batch_size),
            "--output",
            str(output_directory / "matching"),
        ],
        repository,
    )
    run(
        [
            str(structures),
            "--operations",
            str(arguments.structure_operations),
            "--elements",
            str(arguments.structure_elements),
            "--repetitions",
            str(arguments.repetitions),
            "--output",
            str(output_directory / "structures"),
        ],
        repository,
    )
    run(
        [
            str(pipeline),
            "--input",
            str(sample),
            "--output",
            str(output_directory / "itch-sample"),
            "--mode",
            "memory",
            "--workload",
            "all",
            "--warmup",
            "1",
            "--repetitions",
            str(arguments.repetitions),
        ],
        repository,
    )

    if full_day is not None:
        run(
            [
                str(pipeline),
                "--input",
                str(full_day),
                "--output",
                str(output_directory / "itch-full-parser"),
                "--mode",
                "stream",
                "--workload",
                "parser",
                "--warmup",
                "1",
                "--repetitions",
                str(arguments.repetitions),
            ],
            repository,
        )
        run(
            [
                str(pipeline),
                "--input",
                str(full_day),
                "--output",
                str(output_directory / "itch-full-reconstruction-counting"),
                "--mode",
                "stream",
                "--workload",
                "reconstruction-counting",
                "--warmup",
                "1",
                "--repetitions",
                str(arguments.full_day_repetitions),
            ],
            repository,
        )
        run(
            [
                str(pipeline),
                "--input",
                str(full_day),
                "--output",
                str(output_directory / "itch-full-reconstruction"),
                "--mode",
                "stream",
                "--workload",
                "reconstruction",
                "--warmup",
                "1",
                "--repetitions",
                str(arguments.full_day_repetitions),
            ],
            repository,
        )

    datasets: dict[str, Any] = {
        "sample": {
            "path": str(sample),
            "bytes": sample.stat().st_size,
            "sha256": sha256_file(sample),
        }
    }
    if full_day is not None:
        datasets["full_day"] = {
            "path": str(full_day),
            "bytes": full_day.stat().st_size,
            "sha256": sha256_file(full_day),
        }

    metadata = {
        "source_fingerprint_sha256": source_fingerprint(repository),
        "git_commit": capture(["git", "rev-parse", "HEAD"], repository),
        "git_status": capture(["git", "status", "--short"], repository),
        "platform": platform.platform(),
        "machine": platform.machine(),
        "python": sys.version,
        "cmake": capture(["cmake", "--version"], repository),
        "compiler": capture(["clang++", "--version"], repository),
        "sw_vers": capture(["sw_vers"], repository),
        "uname": capture(["uname", "-a"], repository),
        "hardware": {
            key: capture(["sysctl", "-n", key], repository)
            for key in (
                "machdep.cpu.brand_string",
                "hw.model",
                "hw.physicalcpu",
                "hw.logicalcpu",
                "hw.memsize",
                "hw.cachelinesize",
                "hw.pagesize",
                "hw.perflevel0.physicalcpu",
                "hw.perflevel1.physicalcpu",
                "hw.perflevel0.l1dcachesize",
                "hw.perflevel0.l2cachesize",
            )
        },
        "hardware_overview": hardware_overview(repository),
        "power": capture(["pmset", "-g", "batt"], repository),
        "build_cache": capture(
            [
                "cmake",
                "-N",
                "-L",
                str(build_directory),
            ],
            repository,
        ),
        "binary_sha256": {
            path.name: sha256_file(path)
            for path in (layout, matching, structures, pipeline)
        },
        "datasets": datasets,
        "parameters": {
            key: value
            for key, value in vars(arguments).items()
            if not isinstance(value, Path)
        },
        "cpu_affinity": "not pinned; macOS does not expose Linux-style core affinity",
        "timer": "mach_continuous_time on macOS; p50 call-pair overhead recorded by latency benchmark",
    }
    with (output_directory / "manifest.json").open("w", encoding="utf-8") as output:
        json.dump(metadata, output, indent=2, sort_keys=True)
        output.write("\n")

    summary = create_summary(output_directory)
    with (output_directory / "summary.json").open("w", encoding="utf-8") as output:
        json.dump(summary, output, indent=2, sort_keys=True)
        output.write("\n")

    print(f"Phase 19 evidence written to {output_directory}")
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except (OSError, subprocess.CalledProcessError, ValueError) as error:
        print(f"benchmark runner failed: {error}", file=sys.stderr)
        raise SystemExit(1) from error
