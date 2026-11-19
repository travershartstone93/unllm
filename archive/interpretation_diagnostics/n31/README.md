# N31 · GLiNER2 on the authored contrast pairs

Each fine-tune started from the local fastino/gliner2-base-v1 snapshot at revision f9634218e53580c56edf0de97ca1a7d3f1c2354e. One used the first 30 records of the frozen N30 training file. The other used all 60. The arms used identical trainer settings: two full epochs, batch size two, FP32 on the RTX 2070 Max-Q, encoder learning rate 0.00001, task learning rate 0.00005 and seed 42. Thus the 60-example arm also received twice as many optimizer updates. Both runs saved full checkpoints on USB. The original checkpoint was evaluated separately under the same interface. [Authored data and checksums](../n30/README.md)

Class labels and integer spans come from the authored requirements. The literal span uses the number as spelled in the request. Unknown or inapplicable executable fields receive no classification target on a stop request. All three arms use the N25 extraction and normalization interface with one additional clarification reason for whether equality is included. The same generic question corresponding to the gold missing choice receives full clarification credit. Otherwise every field of the normalized requirements must match. Evaluation uses first answers, with a separate schema-validity score. Only the authored training split supplied gradient updates. The 24 development requests and 16 previously inspected regression requests are reported separately.

The evaluation reloads each saved state dictionary into the same original architecture and tokenizer, with strict parameter-name matching. Checkpoint directories include full model and tokenizer files. Each output record preserves the original request, gold fields, raw model extraction, normalized answer and any conversion error.

## Measured first outputs

| Training examples | Development exact | Development valid | Executable meanings (20) | Correct clarification or unsupported (4) | Regression exact |
|---:|---:|---:|---:|---:|---:|
| 0 | 2/24 | 22/24 | 0 | 2 | 3/16 |
| 30 | 3/24 | 15/24 | 1 | 2 | 8/16 |
| 60 | 5/24 | 17/24 | 5 | 0 | 8/16 |

Both trained checkpoints changed 251 tensors from the base checkpoint. The 60-example checkpoint correctly interpreted the donation sum with an inclusive threshold and explicit nested if. Four of its five correct executable development meanings used equality. It attempted construction on all four development stop requests. Of its seven invalid answers, six lacked a single fixed threshold span and one exposed a normalization failure on “minus one”. These measurements use one seed, a small development set, and an already inspected regression set.

[Scores](scores.json) · [60-example checkpoint](/run/media/whisper/UNLLM_CORPUS/unllm/experiments/gliner-authored-yTR3g2/60/final) · [Development predictions](/run/media/whisper/UNLLM_CORPUS/unllm/experiments/gliner-authored-yTR3g2/60/dev.jsonl) · [Regression predictions](/run/media/whisper/UNLLM_CORPUS/unllm/experiments/gliner-authored-yTR3g2/60/regression.jsonl)

Standalone loading from each saved directory reproduced the scored raw output on four development probes. Transformers emitted a tokenizer-regex warning on standalone loading. Both saved tokenizer JSON files have the same SHA-256 as the base tokenizer. The scored comparison uses that base tokenizer in all three arms.

## Replay

From the project root, use the same environment as N25. The path file `.state/n31.path` points to this run's USB output directory. To repeat training, point it at a new directory first to preserve the measured checkpoints.

```sh
export PYTHONPATH="$PWD/.state/n25/overlay:$PWD/.state/n25/support:$PWD/.state/n25/numbers"
export HF_HUB_OFFLINE=1 PYTHONDONTWRITEBYTECODE=1
py="$HOME/.cache/unllm-train-gpu-venv/bin/python"
runner=archive/interpretation_diagnostics/n31/run.py
"$py" "$runner" eval 0
for n in 30 60; do
    "$py" "$runner" train "$n"
    "$py" "$runner" eval "$n"
done
```
