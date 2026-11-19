"""Index CoNaLa for offline dataset review. Source snippets are parsed, never run."""
import ast
import collections
import json
from pathlib import Path
import random
import re
import sys
import warnings

folder = Path(sys.argv[1])
rows = [json.loads(line) for line in (folder/"train.json").read_text().splitlines()]
words = re.compile(r"\b(count\w*|sum\w*|number of|how many|tall\w*|total\w*|frequen\w*|occurr?en\w*|more than|less than|fewer than|at least|at most|greater than|smaller than|larger than|threshold|cutoff|above|below|positive|negative|nonzero)\b", re.I)
names = {"len","sum","count","Counter","count_nonzero","bincount","value_counts","size","reduce","filter"}
stats = collections.Counter()
indexed = []
for i,row in enumerate(rows,1):
    q = row["rewritten_intent"] or row["intent"]
    stats["used_original_intent"] += not bool(row["rewritten_intent"])
    reasons = []
    if words.search(q) or words.search(row["intent"]): reasons.append("wording")
    try:
        with warnings.catch_warnings():
            warnings.simplefilter("ignore")
            tree = ast.parse(row["snippet"])
        nodes = list(ast.walk(tree))
        funcs = {getattr(n.func,"id",getattr(n.func,"attr",None)) for n in nodes if isinstance(n,ast.Call)}
        if names & funcs: reasons.append("aggregate_or_filter")
        if any(isinstance(n,(ast.Compare,ast.For,ast.While,ast.AugAssign)) for n in nodes):
            reasons.append("comparison_or_statement")
        if any(isinstance(n,ast.comprehension) and n.ifs for n in nodes):
            reasons.append("filtered_comprehension")
    except (ValueError,SyntaxError):
        reasons.append("parse_failure")
    indexed.append(dict(row=i,qid=row["question_id"],english=q,snippet=row["snippet"],reasons=reasons))
others=[r for r in indexed if not r["reasons"]]
spotcheck=random.Random(26).sample(others,min(75,len(others)))
for r in spotcheck: r["reasons"].append("negative_sample")
secondary = re.compile(r"\b(amount|exceeds?|add up|adding up|fewer|minimum of|maximum of|length|equals?|equal|tally|times|frequenc\w*|non.zero|cardinality|sum|len|count\w*|reduce|operator|nonzero|size)\b", re.I)
for row in indexed:
    if not row["reasons"] and secondary.search(row["english"] + " " + row["snippet"]):
        row["reasons"].append("secondary_text")
with (folder/"index.jsonl").open("w") as f:
    for r in indexed: print(json.dumps(r),file=f)
print(len(rows),len(set(r["qid"] for r in indexed)),stats,collections.Counter(x for r in indexed for x in r["reasons"]))
print("Review pool",sum(bool(r["reasons"]) for r in indexed))
