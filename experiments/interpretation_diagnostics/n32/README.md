# N32 · Local interpreter comparison

The candidate set includes two instruction-tuned encoder–decoder families and three causal instruction checkpoints. FLAN-T5 and mT0 provide text-to-text alternatives to GLiNER2. SmolLM2 and Qwen2.5 test smaller chat models, and the larger Qwen coder checkpoint tests whether code-oriented instruction tuning transfers to requirements interpretation. The study selected among these five checkpoints on this task. Historical Qwen3 and GLiNER2 trials used different interfaces.

| Key | Publisher checkpoint | Tested revision |
|---|---|---|
| flan | [Google FLAN-T5-base](https://huggingface.co/google/flan-t5-base) | 7bcac572ce56db69c1ea7c8af255c5d7c9672fc2 |
| mt0 | [BigScience mT0-small](https://huggingface.co/bigscience/mt0-small) | 8116a34237e19160ec003147e758f065876d95f0 |
| smol | [Hugging Face SmolLM2-360M-Instruct](https://huggingface.co/HuggingFaceTB/SmolLM2-360M-Instruct) | a10cc1512eabd3dde888204e902eca88bddb4951 |
| qwen | [Qwen2.5-0.5B-Instruct](https://huggingface.co/Qwen/Qwen2.5-0.5B-Instruct) | 7ae557604adf67be50417f59c2c2f167def9a775 |
| coder | [Qwen2.5-Coder-1.5B-Instruct](https://huggingface.co/Qwen/Qwen2.5-Coder-1.5B-Instruct) | 2e1fd397ee46e1388853d2af2c993145b0f1098a |

## Method

Each vanilla checkpoint received the same instructions and zero worked examples. The chat models used their native chat wrappers. A four-field expression represents the aggregate, comparison, caller input or signed literal, and required structure. Two additional forms represent clarification with a reason and an unsupported request. A deterministic serializer converts a valid expression into the N21 JSON schema. A fixed collection name follows from the supplied signature. The output grammar has no access to the English request, the gold labels, or a list of potential literal values. A parser rejects out-of-range integers.

The plain arm used greedy decoding. The constrained arm used greedy decoding with LM Format Enforcer limiting the available tokens. Both arms had a 48-token output cap. They scored the entire expression on the first attempt. A prose answer or an incomplete expression fails the interface check. A valid expression with wrong requirements fails the meaning check. Each checkpoint also passed grammar reachability controls for a negative literal, an input reference and both stop outcomes. [Protocol and recovery record](protocol.md) · [Runner](bench.py)

The frozen N30 development split contains 20 executable, two clarification and two unsupported requests. Selection uses exact meanings across all 24, then correct stops, then smaller model size. The older, already inspected N26 set has 12 executable and four stop requests. Its scores were excluded from selection. For clarification, a question addressing the gold missing choice counts as equivalent. File IDs and target values are available to the scorer and excluded from inference prompts. [N30 dataset](../n30/README.md) · [N26 protocol](../n26/protocol.md)

## Results

The plain vanilla arm produced zero parseable first answers on both sets for all five models. In the vanilla constrained arm, every output was well-formed. Exact meaning scores below include the stop cases.

| Unmodified checkpoint | Exact development / 24 | Executable / 20 | Stops / 4 | Exact regression / 16 |
|---|---:|---:|---:|---:|
| FLAN-T5-base | 0 | 0 | 0 | 0 |
| mT0-small | 0 | 0 | 0 | 0 |
| SmolLM2-360M-Instruct | 2 | 0 | 2 | 2 |
| Qwen2.5-0.5B-Instruct | 2 | 0 | 2 | 0 |
| Qwen2.5-Coder-1.5B-Instruct | 1 | 1 | 0 | 2 |

SmolLM2 declined all 24 development requests. Qwen2.5-0.5B declined the two unsupported requests, attempted construction on the other 22 and missed all their complete meanings. Qwen2.5-Coder got one sum correct. FLAN and mT0 produced executable-form answers to all 24. Model size broke the tie in favor of SmolLM2 under the original ranking rule. No candidate correctly asked for clarification in its vanilla development run.

| After training on 60 | Plain development exact (valid) / 24 | Constrained development exact / 24 | Executable / 20 | Clarify / 2 | Unsupported / 2 | Regression exact / 16 | Training fit / 60 |
|---|---:|---:|---:|---:|---:|---:|---:|
| SmolLM2, primary | 1 (24) | 1 | 1 | 0 | 0 | 2 | 21 |
| Qwen2.5-0.5B, exploratory tied candidate | **9 (23)** | **9** | **5** | **2** | **2** | **8** | **47** |

Each adapter received 75 optimizer updates with batch four, AdamW at 0.0003, rank eight, alpha 16 and dropout 0.05. SmolLM2 updated 819,200 adapter parameters and Qwen updated 540,672. Both were trained in FP32. Their five-epoch mean training losses dropped from 4.315 to 0.472 and 2.880 to 0.253, respectively. Both adapters were reloaded from the USB drive for evaluation. The tie follow-up began after the primary model's results were known. Three of the new candidates remain untested after tuning.

On the development set, trained Qwen succeeded on five of ten sums and zero of ten counts. It returned appropriate questions for a missing tie policy and an unresolved count-versus-sum choice, and declined an index and a median request. On the regression set it interpreted h04 correctly but reversed the meaning of h03. The earlier N31 GLiNER2 trained on the same examples scored 5/24 (five executable, no correct stops) and 8/16 under its native extraction schema. Qwen's four-point development gain over that GLiNER2 run is accounted for by the four stops. A schema-valid answer still frequently selected the wrong operation or threshold.

Two selected model outputs were passed as-is to unLLM's native CLI for both targets. For d16, with inputs [-4,-1,2], summing temperatures strictly below -1 returned -4 as requested. For d15, given fees [2,9] and cutoff 5, the request asks for the fees the cutoff exceeds. Both programs returned 9, whereas the English requires 2. This directly demonstrates a reversed comparison surviving schema validation, C++ compilation and Python execution. [Witness results](/run/media/whisper/UNLLM_CORPUS/unllm/experiments/vanilla-interpreters-BgP2Kx/backend-witnesses.json)

**Decision.** Trained Qwen2.5-0.5B is the best of the two fine-tuned candidates on this development yardstick. Its 5/20 executable accuracy makes it an experimental interpreter. The evaluation gives no ranking of the other three models' fine-tuning potential. [Machine-readable results](results.json) · [Vanilla outputs](/run/media/whisper/UNLLM_CORPUS/unllm/experiments/vanilla-interpreters-BgP2Kx/vanilla) · [Qwen base](/run/media/whisper/UNLLM_CORPUS/unllm/experiments/vanilla-interpreters-BgP2Kx/models/qwen) · [Qwen adapter](/run/media/whisper/UNLLM_CORPUS/unllm/experiments/vanilla-interpreters-BgP2Kx/finetuned/qwen/adapter) · [Qwen development outputs](/run/media/whisper/UNLLM_CORPUS/unllm/experiments/vanilla-interpreters-BgP2Kx/finetuned/qwen/dev-constrained.jsonl)

## Replay

From the checkout, use the N25 local training environment (Torch 2.11.0, Transformers 4.57.6), PEFT 0.17.1, and lm-format-enforcer 0.11.3. The run location is read from `.state/n32.path`. To reproduce into a new directory, point that path at a fresh directory on the USB corpus drive before starting. The N26 and N30 location files identify the frozen benchmark inputs.

```sh
py="$HOME/.cache/unllm-train-gpu-venv/bin/python"
trial=experiments/interpretation_diagnostics/n32
python3 "$trial/fetch.py"
export PYTHONPATH="$PWD/.state/n32-deps:$PWD/.state/n25/overlay:$PWD/.state/n25/support"
export HF_HUB_OFFLINE=1 PYTHONDONTWRITEBYTECODE=1
for key in flan mt0 smol qwen coder; do
    "$py" "$trial/bench.py" vanilla "$key"
done
winner=$(python3 "$trial/select_model.py")
"$py" "$trial/bench.py" train "$winner"
"$py" "$trial/bench.py" trained "$winner"
```

The primary winner and the exploratory tied candidate each use their own LoRA adapter on their original pinned base. To reproduce the tie follow-up after the primary replay, run the same train and trained commands with key `qwen`. The evaluator loads each adapter from disk in a new process.

All five full checkpoint files matched their published SHA-256 digests. The preliminary FP16 causal run produced invalid numerical scores on this RTX 2070 Max-Q and was excluded; its files are under `preliminary-half`. Every ranked arm uses FP32 inference with a finite-logits check. Decoding times in the saved summaries cover batched generation, parsing and scoring; they exclude checkpoint loading and the tokenizer index build. Batch size differs for the 1.5B model due to memory limits. [Run artifacts](/run/media/whisper/UNLLM_CORPUS/unllm/experiments/vanilla-interpreters-BgP2Kx)
