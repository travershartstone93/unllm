# Learning complete stream contracts with Stanford SEMPRE

We used Stanford's published chart parser, discriminative training objective and parameter updates offline. A small Java feature adapter supplies word and adjacent word-pair features for each candidate labeled span. The grammar describes legal sequences of numeric or text stream operations and output types. It contains no English phrases. Training used individually labeled spans. SEMPRE had to segment whole requests and assemble previously unseen programs. This experiment covers a bounded stream algebra; functions, GUI applications and arbitrary nested control flow lie outside the grammar. [SEMPRE](https://www-nlp.stanford.edu/software/sempre/) · [Frozen design](protocol.md)

| Development comparison | Result |
| --- | --- |
| Existing English CLI | One of eight positive requests passed both targets on populated and empty input. No acceptance on six controls. |
| Raw SEMPRE winner | Three exact contracts on eight positives. Five contained extra operations, including lowercasing or whitespace stripping that the request did not specify. |
| Candidate coverage | The correct contract was present in six of eight positive beams. Ranks in fixture order were absent, 23, 24, 13, absent, 1, 1 and 1. |
| Calibrated SEMPRE fallback | Off. At zero margin it accepted one correct and one wrong calibration case. At 0.5 it accepted none. No operating point added a calibration success without an error. A fallback at this setting retains the stock CLI's result. |
| Independently supplied contract | All eight supplied contracts passed both targets on populated and empty input. This isolates the interpreter and selection failures from the backend. |

The two requests differing in the order of whitespace stripping and deduplication produced different correct outputs on the same input. The raw parser added an operation in both. Supplying the desired meanings restored correct execution. These requests reuse some training phrasing; even a successful result here would need independent transfer checks.

In the initial execution fixture the expected sum of the distinct entries in `[-2,5,-2,1,0]` was 2. The distinct entries are `[-2,5,1,0]`, whose sum is 4. We corrected that expectation and reran all 36 execution checks on the identical generated sources. Both the [original](program-scores-original.tsv) and [corrected](program-scores.tsv) scores are retained. Parser training, calibration and predictions used no execution fixtures. The adjustment changes only the supplied-contract execution verdict for that row.

The original [predictions](predictions.tsv), [calibration grid](calibration-results.tsv), [stock responses](stock.json) and [candidate ranks](candidate-ranks.tsv) are preserved. The rank audit added reporting after parsing; the complete prediction file was byte-identical to the initial run. Rank zero in the audit means absent. The [fixed-pool follow-up](ranking/protocol.md) evaluates one general explanation for the extra operations on separate data.

## Replay

Use the [SEMPRE core build instructions](https://github.com/percyliang/sempre#installation) or the compiled isolated checkout under the run directory referenced by `.state/n16-bridge.path`. The measured build used OpenJDK 17 with Java 8 class compatibility, upstream SEMPRE and fig, and the core dependency versions specified in upstream's `pull-dependencies`. External source and UDPipe model digests are in [tool snapshots](../tool-snapshots.sha256). No JVM, model downloads or training are required by unllm's installed coding path.

```sh
trial_dir=$(cat .state/n16-bridge.path)
fixtures="$PWD/experiments/interpretation_diagnostics/n16/sempre"
cd "$trial_dir/tools/sempre-master"
javac --release 8 -cp 'classes:lib/*' -d classes "$fixtures/SpanFeatures.java" "$fixtures/Probe.java"
java -Xmx1500m -cp 'classes:lib/*:fig-master/external/*' edu.stanford.nlp.sempre.Probe "$fixtures" > "$trial_dir/replay.log"
rg '^(split|calibration|transfer)\s' "$trial_dir/replay.log" > "$trial_dir/replay.tsv"
cmp "$fixtures/predictions.tsv" "$trial_dir/replay.tsv"
awk -f "$fixtures/calibrate.awk" "$trial_dir/replay.tsv"
```
