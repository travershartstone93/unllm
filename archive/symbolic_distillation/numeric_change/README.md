# Numeric-change discovery run

**Outcome: feasibility gate failed at the existing parse budget.** Two discovery examples per semantic family were written before probing and all six stayed in the denominator. Five hit the 64-reading limit every time. Neither initial-value seed nor either directional-change seed supplied an uncapped reading set. The proposed pilot rules explicitly disallow accepting a complete interpretation from truncated enumeration. The versions with engineering changes and learned clauses were never fitted or scored. The contribution of symbolic distillation therefore remains unmeasured.

| Discovery family | Full-grammar readings per seed | Native baseline |
| --- | --- | --- |
| Initial value | 64, 64 | Both unsupported |
| Up/down | 64, 64 | Both unsupported |
| Take away | 20, 64 | Both unsupported |

Cold, resident and reversed-order runs following an unrelated location request yielded the same counts. This establishes count stability only. The first directional example illustrates an actual attachment problem: of 64 cold readings, the “by” modifier attached to *make* in 25, *go* in 19 and a *down* predicate in 20. In the uncapped removal seed the grammar did produce a take-away predicate linking the button and the quantity. Fewer returned readings than the cap does not certify exhaustive parsing. The unmodified grounder reported an unsupported semantic frame for every seed. [Per-request results](observed.tsv) · [Attachment counts](directional-attachments.jsonl)

**Lowering control.** The experimental manual-frame adapter instantiated exactly one state and one action using initial 8.5 and subtraction by 2.25. The existing native constructor validated Python with 25 checks and C++ with 50 across ordinary and sanitizer builds. A separately written Qt interaction oracle verified initial 8.5, two clicks to 4, editing to 7.5, and one click to 5.25 in both generated apps. These are construction results from supplied roles.

**Evaluation boundary.** The six seeds were the only discovery requests. Teacher calls, teacher tokens, proposed clauses, retained clauses and selector iterations were all zero. The 24 prospective acceptance requests and exact labels are separately reserved for a later experiment. Their roles and outcomes were authored by the implementing assistant after discovery diagnosis, before any induction. Neither the probe nor the selector consumes them. Their grammatical partitions still require design-time confirmation for a future run, so they provide no blind or scored evidence here. No production rule, teacher integration or parse-budget change was promoted.

## Reproduce

From the upgrade checkout, use a new output directory. The probe uses the production normalizer, frozen ACE/ERG, full code grammar with four seconds and up to 64 readings, and the one-second fragment grammar on empty results. It saves raw ACE output and decoded MRS for each seed and history. The final six rows exercise the original semantic parser and grounder.

```sh
e=archive/symbolic_distillation/numeric_change
d=$(mktemp -d)
cmake --build build --target numeric_discovery_probe numeric_manual_frame unllm_generate_gui
build/native/numeric_discovery_probe "$PWD" "$e/discovery-seeds.tsv" "$d" >"$d/observed.tsv"
build/native/numeric_manual_frame 8.5 sub 2.25 >"$d/manual.json"
mkdir "$d/cpp" "$d/python"
build/native/unllm_generate_gui "$PWD" "$PWD/build/native/unllm_logic_worker" "$d/manual.json" cpp "$d/cpp"
build/native/unllm_generate_gui "$PWD" "$PWD/build/native/unllm_logic_worker" "$d/manual.json" python "$d/python" --python-target /usr/bin/python3
"$e/check_widgets.sh" "$d/cpp"/job-* cpp
"$e/check_widgets.sh" "$d/python"/job-* python
```

The run's binary, source and grammar checksums are in [baseline.sha256](baseline.sha256). The local directory named in `.state/numeric-pilot.path` holds the source snapshot, frozen binaries, raw analyses, generated applications and run logs. The reserved evaluation manifest has its own [checksum](evaluation/SHA256SUMS).
