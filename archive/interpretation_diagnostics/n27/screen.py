"""Measure WikiSQL coverage using structured annotations and the training tables."""
import argparse
from collections import Counter, defaultdict
from decimal import Decimal
import hashlib
import json
from pathlib import Path
import random
import re
import sqlite3

AGGS = ["none", "max", "min", "count", "sum", "avg"]
OPS = ["eq", "gt", "lt", "other"]
FILES = {
    "train.db": "7b9bafabb43c190c52605322c787604e9019a0d7d602c2dc00833f81b3f28095",
    "train.jsonl": "b52247eab1ea56bd5d181b54814449495740df52492193f36926bdc2ee822488",
    "train.tables.jsonl": "5e65a734e355771d398b1e49049b40c98f78632616a2ee4171a4f5995f24eb50",
}
NUMERIC = re.compile(r"[+-]?(?:[0-9]+|[0-9]{1,3}(?:,[0-9]{3})+)(?:\.[0-9]+)?")

def integer(value):
    s = str(value).strip()
    if not NUMERIC.fullmatch(s):
        return None
    n = Decimal(s.replace(",", ""))
    if n != n.to_integral_value() or not -(2**63) <= n < 2**63:
        return None
    return int(n)

def lines(path):
    with path.open() as f:
        yield from map(json.loads, f)

def dump(path, rows):
    with path.open("w") as out:
        for row in rows:
            print(json.dumps(row, ensure_ascii=False), file=out)

def table_name(tid):
    if not re.fullmatch(r"[0-9]+(?:-[0-9]+)+", tid):
        raise ValueError(tid)
    return "table_" + tid.replace("-", "_")

def audit(folder):
    data = folder / "data"
    for name, expected in FILES.items():
        with (data / name).open("rb") as f:
            assert hashlib.file_digest(f, "sha256").hexdigest() == expected
    db = sqlite3.connect((data / "train.db").resolve().as_uri() + "?mode=ro", uri=True)
    tables = {t["id"]: t for t in lines(data / "train.tables.jsonl")}
    stages, pool_sizes, aggregates, ops, arities = (Counter() for _ in range(5))
    stages["same_column_before_type_filter"] = 0
    stages["same_integer_data"] = 0
    eligible, sample = [], []
    groups = defaultdict(list)
    for line, q in enumerate(lines(data / "train.jsonl"), 1):
        stages["all_rows"] += 1
        sql = q["sql"]
        agg = AGGS[sql["agg"]]
        conds = sql["conds"]
        aggregates[agg] += 1
        ops.update(OPS[c[1]] for c in conds)
        if agg not in ("count", "sum"):
            continue
        stages["count_or_sum"] += 1
        arities[len(conds)] += 1
        if len(conds) != 1:
            continue
        stages["one_condition"] += 1
        col, op, literal = conds[0]
        tid = q["table_id"]
        tab = tables[tid]
        op = OPS[op]
        sel = sql["sel"]
        if sel == col:
            stages["same_column_before_type_filter"] += 1
        if tab["types"][col] != "real" or op == "other":
            continue
        stages["numeric_predicate_supported_op"] += 1
        k = integer(literal)
        if k is None:
            continue
        stages["integer_threshold"] += 1
        shape = "same" if sel == col else "other"
        fields = f"{agg}/{op}"
        pool_sizes[f"{shape}:{fields}"] += 1
        values = [integer(r[col]) for r in tab["rows"]]
        # Keep display data and SQLite independently accountable for the projection.
        records = list(db.execute(f"SELECT col{col}, col{sel} FROM {table_name(tid)}"))
        stored = [integer(v) for v, _ in records if isinstance(v, (int, float))]
        clean = bool(values) and None not in values and None not in stored
        clean = clean and len(stored) == len(records) and sorted(values) == sorted(stored)
        if clean:
            stages[f"{shape}_integer_data"] += 1
        key = f"{shape}:{fields}"
        row = dict(row=line, table_id=tid, question=q["question"], aggregate=agg,
                   comparison=op, threshold=k, selected=tab["header"][sel], filtered=tab["header"][col],
                   query=sql, clean_integer_column=clean,
                   selected_all_present=all(v is not None and str(v).strip() for _, v in records),
                   shape=shape)
        eligible.append(row)
        groups[key].append(row)
    rng = random.Random(27)
    same_size = sum(len(v) for k, v in groups.items() if k.startswith("same:"))
    for key, group in sorted(groups.items()):
        picked = group if key.startswith("same:") and same_size <= 120 else rng.sample(group, min(20, len(group)))
        sample.extend(picked)
    dump(folder / "eligible.jsonl", eligible)
    dump(folder / "sample.jsonl", sorted(sample, key=lambda r: r["row"]))
    return dict(inputs=FILES, tables=len(tables), stages=stages, aggregate_labels=aggregates,
                predicates=ops, count_sum_condition_arities=arities, candidate_groups=pool_sizes,
                total_reviewed=len(sample),
                sampled_groups=Counter(f"{r['shape']}:{r['aggregate']}/{r['comparison']}" for r in sample),
                unique_eligible_tables=len({r["table_id"] for r in eligible}),
                unique_eligible_wordings=len({" ".join(r["question"].casefold().split()) for r in eligible}))

if __name__ == "__main__":
    parser = argparse.ArgumentParser()
    parser.add_argument("folder", type=Path)
    print(json.dumps(audit(parser.parse_args().folder), indent=2, sort_keys=True))
