# N25 · Local extraction and compositional parsing

Ran the ten English requests and signature from N21 in order, without training, in-context demonstrations, repair or retries. The two extractors used one field vocabulary. It provides the possible reductions, both parameter roles, operand orientation, implementation constraints and possible unresolved decisions. The adapters translate predicted labels into the N21 schema, can normalize an extracted integer with text2num, and can turn a predicted missing-role label into a question. They do not inspect the request text. A missing or unusable active field fails validation. Inactive fields are ignored. GLiNER2 receives native classification and entity definitions; NuExtract receives its native typed template. The two models passed independent controls (extract a person name). Record the first output per prompt, full contract validity and exact meaning. Warm seconds measure local inference plus extraction parsing; checkpoint load and backend execution are excluded.

| Candidate | Valid contracts | Exact meaning | Median warm seconds |
|---|---:|---:|---:|
| GLiNER2 base v1, 205M, CPU (four Torch threads) | 8/10 | 5/10 | 0.235 |
| NuExtract 2.0 2B, GPU (RTX 2070 Max-Q, FP16) | 1/10 | 0/10 | 2.903 |
| SpanBasedSP author release | unscored | unscored | — |

GLiNER2 passed 1, 6, 7, 8 and 10. The four executable interpretations passed all 88 N23 independent Python/C++ execution checks. Its correct unsupported decision returned no source on both targets. The earlier Qwen 4B run passed 1–7. GLiNER2 therefore added two individually correct answers that Qwen missed, but also failed four that Qwen passed. Neither identified the missing comparison on case 9. No ensemble was trained or scored. NuExtract returned parseable JSON; the template output still had to bind a usable threshold. On case 1 it selected a sum and a fixed threshold with a null value. No correct answer advanced to the constructor.

SpanBasedSP has a distinct prerequisite. Its upstream experiment driver trains for GeoQuery, SCAN or CLEVR and the tree-to-program mappers produce domain-specific programs. The checkout has no trained parser weights for this task. The upstream SCAN converter produced the expected logical form for “jump twice”; it rejected each coding prompt at its SCAN vocabulary check. This measures release compatibility only. Evaluating the neural architecture on these requests requires a programming-domain mapper, labeled training data separate from the test set, and a trained checkpoint. [Author release](https://github.com/jonathanherzig/span-based-sp)

## Replay

The local snapshots are in `.state/n25/{gli,nu,span}`. Model refs are fastino/gliner2-base-v1 at f9634218e53580c56edf0de97ca1a7d3f1c2354e and numind/NuExtract-2.0-2B at 5eb4a99746edc2ac44900f56a5c9431d3c03b16d. Upstream SpanBasedSP commit 7c7f87b8ef9d3061652cd25758869e78b57179a4. Both safetensors digests matched the download server's linked etags. Use the installed test environment from the repository root:

```bash
d="$PWD/.state/n25"
export PYTHONPATH="$d/overlay:$d/support:$d/numbers" HF_HUB_OFFLINE=1 PYTHONDONTWRITEBYTECODE=1
py="$HOME/.cache/unllm-train-gpu-venv/bin/python"
trial=archive/interpretation_diagnostics/n25
"$py" "$trial/evaluate.py" gliner
"$py" "$trial/evaluate.py" nuextract
python3 "$trial/span_probe.py"
python3 "$trial/execute.py" gliner
```

Torch 2.11.0+cu128, transformers 4.57.6, gliner2 2.1.0. NuExtract used its published chat template, greedy text-only decoding, 256 new tokens, 45-second generation budget and a repeated-text stop. The models ran on different devices, and the fixture is small and already known. These timings and counts apply to these configurations.
