"""Verify the adjudicated WikiSQL examples against SQLite and unLLM's ordinary CLI."""
import argparse
from collections import Counter
import csv
import json
import operator
import os
from pathlib import Path
import runpy
import sqlite3
import subprocess
import tempfile

import jsonschema
from screen import dump, integer, lines, table_name

HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[2]
OPERATORS = {"eq": ("=", operator.eq), "gt": (">", operator.gt), "lt": ("<", operator.lt)}

def source_result(db, row):
    sql = row["query"]
    col = sql["conds"][0][0]
    op, _ = OPERATORS[row["comparison"]]
    tab = table_name(row["table_id"])
    expr = f"{row['aggregate']}(col{sql['sel']})"
    answer, = db.execute(f"SELECT {expr} FROM {tab} WHERE col{col} {op} ?", (row["threshold"],)).fetchone()
    values = [integer(x) for x, in db.execute(f"SELECT col{col} FROM {tab}")]
    return values, answer

def fixtures(db, values, answer, row):
    k = row["threshold"]
    op, _ = OPERATORS[row["comparison"]]
    cases = [(values, k, answer)]
    # Different payloads, repeated payloads, and changing unused cutoff challenge a row-count projection.
    for xs in ([], [k], [k - 1, k, k + 1], [k + 1, k - 1, k, k + 1, k, k - 1], [k, k], [-3, -2, -1]):
        db.execute("DELETE FROM fixture")
        db.executemany("INSERT INTO fixture VALUES (?,?)", [(x, 10 + i % 2) for i, x in enumerate(xs)])
        expected, = db.execute(f"SELECT count(payload) FROM fixture WHERE measured {op} ?", (k,)).fetchone()
        cases.extend([(xs, k - 1, expected), (xs, k + 1, expected)])
    return cases

def verify(folder, holdout):
    schema = runpy.run_path(str(HERE.parent / "qwen_contract/trial.py"))["SCHEMA"]
    replay = runpy.run_path(str(ROOT / "archive/algorithm_ir/requirements_replay/replay.py"))
    dataset = folder / "data"
    source = sqlite3.connect((dataset / "train.db").resolve().as_uri() + "?mode=ro", uri=True)
    synthetic = sqlite3.connect(":memory:")
    synthetic.execute("CREATE TABLE fixture (measured INTEGER, payload INTEGER)")
    with (HERE / "review.tsv").open() as f:
        labels = {int(r["row"]): r for r in csv.DictReader(f, delimiter="\t")}
    sample = {r["row"]: r for r in lines(folder / "sample.jsonl")}
    assert len(labels) == len(sample) == 120 and labels.keys() == sample.keys()
    def normalized(question):
        return " ".join(question.casefold().split())
    frozen = {normalized(r["request"]) for r in lines(holdout)}
    overlaps = sum(normalized(r["question"]) in frozen for r in lines(dataset / "train.jsonl"))
    assert len(frozen) == 16 and overlaps == 0

    # This measures agreement on one published table, including accidental agreement.
    table_agreement = Counter()
    for row in lines(folder / "eligible.jsonl"):
        if not row["clean_integer_column"]:
            continue
        xs, expected = source_result(source, row)
        compare = OPERATORS[row["comparison"]][1]
        qualifying = [x for x in xs if compare(x, row["threshold"])]
        agg = row["aggregate"]
        projected = len(qualifying) if agg == "count" else sum(qualifying)
        outcome = "sql_null" if expected is None else ("equal" if projected == expected else "different")
        table_agreement[f"{agg}/{outcome}"] += 1

    parts, contextual, runs = [], [], []
    judgments, meanings = Counter(), Counter()
    with tempfile.TemporaryDirectory() as tmp:
        work = Path(tmp)
        env = dict(os.environ, UNLLM_MODEL="off", UNLLM_NO_SERVER="1", UNLLM_ARTIFACTS=tmp,
                   UNLLM_FACT_DB=str(work / "facts.sqlite"), UNLLM_PYTHON_TARGET="/usr/bin/python3")
        for n, row in sorted(sample.items()):
            judgment = labels[n]
            fields = {}
            for field in ("predicate", "aggregate"):
                assert judgment[field] in {"yes", "no", "uncertain"}
                judgments[f"{field}/{judgment[field]}"] += 1
            if judgment["predicate"] == "yes":
                fields.update(comparison=row["comparison"], compare_to={"literal": row["threshold"]})
            if judgment["aggregate"] == "yes":
                fields.update(aggregate=row["aggregate"])
            binding = judgment["binding_or_note"]
            is_bound = binding.startswith("values holds ")
            if fields:
                meanings["both_fields" if len(fields) == 3 else "predicate_only" if "comparison" in fields else "aggregate_only"] += 1
                parts.append(dict(source=row, reviewed_fields=fields))
            else:
                meanings["neither_field"] += 1
            if not is_bound:
                continue
            assert row["aggregate"] == "count" and row["selected_all_present"] and row["clean_integer_column"]
            assert len(fields) == 3
            req = dict(status="ok", collection="values", required_structure=[], **fields)
            jsonschema.validate(req, schema)
            xs, expected = source_result(source, row)
            compare = OPERATORS[row["comparison"]][1]
            assert expected == sum(compare(x, row["threshold"]) for x in xs)
            cases = fixtures(synthetic, xs, expected, row)
            contextual.append(dict(row=n, table_id=row["table_id"], original_request=row["question"],
                                   binding=binding, requirements=req, source_query=row["query"]))
            for lang in ("cpp", "python"):
                proc = subprocess.run([ROOT / "build/unllm", "--json", "--code", lang, json.dumps(req)],
                                      env=env, capture_output=True, text=True, timeout=60, check=True)
                result = json.loads(proc.stdout)
                assert result["status"] == "validated", result
                replay["execute"](Path(result["source"]), lang, work, cases, nested=False)
                runs.append(dict(row=n, target=lang, passed=len(cases), table_answer=expected))
    # Two counterexamples for the two-column projections, plus the empty-SUM behavior.
    synthetic.execute("DELETE FROM fixture")
    synthetic.executemany("INSERT INTO fixture VALUES (?, ?)", [(7, None), (7, 20)])
    witness = synthetic.execute("SELECT count(payload), count(measured), sum(payload), sum(measured) FROM fixture WHERE measured > 5").fetchone()
    empty, = synthetic.execute("SELECT sum(payload) FROM fixture WHERE measured > 9").fetchone()
    assert witness == (1, 2, 20, 14) and empty is None
    dump(folder / "contextual.jsonl", contextual)
    dump(folder / "reviewed-fields.jsonl", parts)
    return dict(review_counts=judgments, field_combinations=meanings, contextual=len(contextual),
                contextual_forms=Counter(f"{r['requirements']['aggregate']}/{r['requirements']['comparison']}" for r in contextual),
                source_table_agreement=table_agreement, heldout_exact_overlaps=overlaps, executions=runs,
                total_generated_executions=sum(r["passed"] for r in runs),
                worker_sha256=replay["sha"](ROOT / "build/native/unllm_symbolic_worker"),
                witnesses=dict(source_count=witness[0], single_column_count=witness[1], source_sum=witness[2],
                               single_column_sum=witness[3], empty_sql_sum=empty, empty_list_sum=sum([])))

if __name__ == "__main__":
    parser = argparse.ArgumentParser()
    parser.add_argument("folder", type=Path)
    parser.add_argument("holdout", type=Path)
    args = parser.parse_args()
    print(json.dumps(verify(args.folder, args.holdout), indent=2, sort_keys=True))
