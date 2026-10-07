#!/usr/bin/env python3
"""Check saved CSV consistency, not process/sanitizer success or significance."""
import argparse
import collections
import csv
import math
from pathlib import Path


def verify(path):
    info = dict(line.rstrip().split("=", 1) for line in (path / "run.txt").read_text().splitlines())
    measured, warmups = int(info["samples"]), int(info["warmups"])
    grouped = collections.defaultdict(list)
    checksums = {}
    with (path / "raw.csv").open(newline="") as stream:
        for row in csv.DictReader(stream):
            key = row["case"], row["implementation"]
            assert int(row["operations"]) > 0, (path, key, "empty batch")
            assert math.isclose(float(row["elapsed_ns"]) / int(row["operations"]),
                                float(row["ns_per_operation"]), rel_tol=2e-11), (path, key, "batch normalization")
            checksum = int(row["checksum"])
            assert checksums.setdefault(row["case"], checksum) == checksum, (path, key, "checksum mismatch")
            grouped[key].append(row)
    with (path / "summary.csv").open(newline="") as stream:
        summaries = {(row["case"], row["implementation"]): row for row in csv.DictReader(stream)}
    assert set(grouped) == set(summaries), (path, "missing summary")
    for key, rows in grouped.items():
        summary = summaries[key]
        assert len(rows) == measured == int(summary["samples"]), (path, key, "sample count")
        assert sorted(int(row["round"]) for row in rows) == list(range(measured)), (path, key, "rounds")
        assert all(int(row["operations"]) == int(summary["operations_per_sample"]) for row in rows), (path, key, "operations")
        values = sorted(float(row["ns_per_operation"]) for row in rows)
        for label, probability in [("median_ns", .5), ("p95_ns", .95), ("p99_ns", .99)]:
            value = values[math.ceil(probability * measured) - 1]
            assert math.isclose(value, float(summary[label]), rel_tol=2e-11), (path, key, label)
        assert values[0] == float(summary["min_ns"]) and values[-1] == float(summary["max_ns"]), (path, key, "range")
    aggregate = sum(int(rows[0]["checksum"]) * (measured + warmups) for rows in grouped.values())
    assert aggregate == int(info["observable_checksum"]), (path, "aggregate checksum")
    if "case_count" in info:
        assert int(info["case_count"]) == len(grouped), (path, "case count")
    with (path / "allocations.csv").open(newline="") as stream:
        for row in csv.DictReader(stream):
            assert row["allocation_calls"] == row["deallocation_calls"], (path, "unmatched allocations")
            assert int(row["live_bytes_after_release"]) == 0, (path, "live ownership bytes")
    print(f"{path}: CSV CONSISTENT; {len(grouped)} cases, {measured} samples, {warmups} warmups, checksum {aggregate}")
    return set(grouped)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("directories", nargs="+", type=Path)
    args = parser.parse_args()
    for directory in args.directories:
        verify(directory)


if __name__ == "__main__":
    main()
