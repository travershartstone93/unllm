# N27 · WikiSQL coverage for the unLLM request interpreter

We found **zero direct single-column pairs** under the N26 transfer rule. WikiSQL still yielded **12 verified, context-dependent count requests** and **106 reviewed examples of comparison wording** in a stratified sample of 120. The first number comes from screening every training query. The latter two come from reading the sample and overlap.

## Source and selection

The source is the official Salesforce archive at revision `cffb423077756d04c1bac5bcd45167c86903fbcb`, Git blob `941de4cb2ad5fa7aeb2e37d314468636ce070af7`. We extracted only the training questions, table descriptions and SQLite database. Data and exports reside on the USB corpus volume. The input digests, tool revision and review rules are recorded. The 16 N26 diagnostic requests retain their frozen digest. None exactly overlaps a WikiSQL training question after case and whitespace normalization. [Source release](https://github.com/salesforce/WikiSQL/tree/cffb423077756d04c1bac5bcd45167c86903fbcb) · [Upstream operators](https://github.com/salesforce/WikiSQL/blob/cffb423077756d04c1bac5bcd45167c86903fbcb/lib/query.py) · [Frozen protocol](protocol.md) · [Screen results](screen-results.json)

| Selection on the training split | Rows |
|---|---:|
| All questions, across 18,585 training table descriptions | 56,355 |
| Labeled COUNT or SUM | 7,156 |
| COUNT or SUM with exactly one condition | 4,352 |
| Of those, select and filter the same column, before any type filter | **0** |
| Single condition on a numeric column with a supported comparison | 929 |
| Threshold is an unambiguous signed 64-bit integer literal | 896 |
| All measured cells also agree as integers between displayed data and SQLite | 837 |

The 896 candidate questions have distinct wording under case and whitespace normalization and come from 832 tables. Their field labels break down as follows. The predicate columns are numeric. The selected columns may hold a different type. An integer threshold can include decimal notation such as 2.0 and unambiguous comma grouping. The screen skips text that needs unit stripping or guessing.

| Source aggregate | Equality | Greater than | Less than | Total |
|---|---:|---:|---:|---:|
| Count | 527 | 71 | 78 | 676 |
| Sum | 83 | 59 | 78 | 220 |

## What the English supports

Using seed 27, we drew 20 questions from each of the six cells above and reviewed all 120 alongside the selected column, filtered column, threshold and query. Equal weighting intentionally gives rare combinations a voice. These sample fractions do not estimate the proportion in the full split. One project reviewer made the semantic judgments. Ambiguous wording received no credit on the disputed field. [Row-level judgments](review.tsv)

| Original English clearly supports | Reviewed questions |
|---|---:|
| Both the labeled aggregate and the predicate, including its threshold | 33 |
| Predicate and threshold only | 73 |
| Aggregate only | 2 |
| Neither | 12 |

Of the 33 supporting both fields, **12** become whole requirements with a short, explicit definition of the input list. Eight use equality, three use greater-than and one uses less-than. All count entries relative to a fixed integer. For example, row 2478 asks how many sitting politicians were first elected in 1972. Its stored query counts the incumbent column while filtering the election-year column. Supplying a list with one first-election year per listed incumbent makes that question executable as counting entries equal to 1972. An inference system given only a generic list of integers and a cutoff would have to invent that binding. Each exported contextual example includes the original question, its binding, source identifiers and the full verified requirements.

The other 21 questions supporting both fields ask for sums over separate measurements. Row 17052 adds bronze medals after filtering on silver medals. A list of silver-medal counts loses the bronze values. Such examples can teach predicate and aggregate wording separately. Row 45961 illustrates an annotation conflict: the question explicitly asks for a sum and its SQL counts. Row 49042 includes the numeric endpoint in the English while its SQL excludes it. The field-level export retains only the fields explicitly credited in the ledger. Each record includes the original labeled query for audit, separately from the reviewed fields.

## Execution checks

For each of the 12 contextual examples we compared its label with the source SQL answer on the real table. Eleven of those answers were 1 and one was 0. We therefore also exercised both generated functions on inputs with duplicates, boundary equality, empty lists and changing unused cutoff arguments. SQLite computed expectations for the extra cases using a separate, populated payload column. All **312/312** executions passed, comprising 12 requests × 2 target languages × 13 inputs. The C++ runs compiled the emitted source. These checks exercised unLLM with the labeled requirements. English interpretation accuracy awaits a separate experiment. [Per-target results](verification.json)

We separately tested the tempting shortcut of counting or summing the filter column in every clean table. All 635 COUNT results happened to agree on their original tables. All had populated selected fields. A counterexample with two qualifying rows, one with a missing payload, gives a source COUNT of 1 and a projected count of 2. For the 202 SUM examples, only 7 agreed. Another 87 returned a different number and 108 returned SQL NULL when no row qualified. An executable list sum returns zero on empty input. Agreement on a single table can hide both a missing input and a different empty-case behavior.

## Training decision

Use the 12 contextual pairs only when the input definition travels with the question. The 108 rows with at least one reviewed field can supply partial labels; the 12 contextual pairs are already in those 108. The 896 mechanically selected queries are a candidate pool. Keeping source tables together in any later training split also avoids leakage between related questions.

WikiSQL adds wording for three comparisons against literals. It supplies no labeled inclusive comparator, caller-input threshold, required loop, or clarification outcome. Its two-column sums also leave the one-list filtered-sum task uncovered. For the next experiment, source or author a small collection of contrasting coding requests around those missing distinctions. Group by wording family before splitting. Compare the same local interpreter with and without the reviewed WikiSQL field labels, using identical training examples otherwise. Score exact meanings, individual fields, and the two stop outcomes on separately held-out English. This audit produced no trained model or new English-parser score.

## Replay and artifacts

Run from the upgrade checkout with the N26 experiment dependencies and a built unLLM. Commands below use the audited USB directory. For a fresh copy use the pinned release above, verify the Git blob, and extract `data/train.jsonl`, `data/train.tables.jsonl` and `data/train.db` from its `data.tar.bz2`.

```sh
audit=/run/media/whisper/UNLLM_CORPUS/unllm/experiments/wikisql-audit-bFKFiH
held=/run/media/whisper/UNLLM_CORPUS/unllm/experiments/conala-audit-QucVKM/holdout.jsonl
sha256sum -c archive/interpretation_diagnostics/n26/frozen.sha256
sha256sum -c archive/interpretation_diagnostics/n27/protocol.sha256
python3 archive/interpretation_diagnostics/n27/screen.py "$audit"
PYTHONDONTWRITEBYTECODE=1 python3 archive/interpretation_diagnostics/n27/check.py "$audit" "$held"
```

The screen writes `eligible.jsonl` and the seeded `sample.jsonl`. The check writes `contextual.jsonl` and `reviewed-fields.jsonl`. Both scripts print the measured results. [Screen implementation](screen.py) · [Review and execution checks](check.py)
