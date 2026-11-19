# N33 · Were the N32 vanilla models used correctly?

N32 measured a strict output interface. Across five models and 24 questions per model, none of the 120 plain zero-shot responses parsed. One example shows how a partially interpreted instruction received zero credit. On d02 Qwen Coder emitted “OP COUNT COMPARISON ABOVE -2 STRUCTURE”. That captures the operation, comparison and threshold, and omits the required nested if. FLAN often returned code-like text, while SmolLM2 often echoed the field names.

## Retest

| Checkpoint | Zero-shot comparison alone / 20 | N32 plain exact / 24 | Four-shot plain valid / 24 | Four-shot plain exact / 24 | N32 constrained exact / 24 | Four-shot constrained exact / 24 |
|---|---:|---:|---:|---:|---:|---:|
| flan | 5 | 0 | 0 | 0 | 0 | 0 |
| mt0 | 4 | 0 | 0 | 0 | 0 | 0 |
| smol | 7 | 0 | 16 | 2 | 2 | 2 |
| qwen | 6 | 0 | 15 | 1 | 2 | 3 |
| coder | 9 | 0 | 24 | 3 | 1 | 3 |

The examples came from the existing training split. All weights remained unchanged. Giving a completed output pattern and a final answer cue helped the chat models follow the interface. The comparison-only task gives credit for one of five balanced relational labels and assumes the request is executable. Its scores cannot be compared directly with full-contract accuracy. All five failed an exact-copy control for a signed structured expression.

## Implementation checks

The pinned publisher cards use a seq2seq model on raw text for FLAN and mT0, and causal models with native chat formatting for the other three. N32 standardized on deterministic FP32 inference. Publisher examples vary in sampling, decoding budgets, and chat options. N33 verified native input token IDs under the chosen wrappers and checked every returned decoder-start token or causal prompt boundary before extracting generated output. The N32 grammar permitted the tokenized gold answer including EOS for all 120 model/request combinations. No prompt was truncated. These checks address wrong model classes, double tokenization, lost answer prefixes and impossible reference strings; they cannot establish a best inference recipe.

Both T5 models translated the French control to “I love you.” An independent CPU run using the model-card loading patterns and default generation reproduced the three common-control outputs exactly for each checkpoint. FLAN answered “london” and mT0 answered “France” to the capital-of-France question. Both missed exact copying. The CPU run is a cross-check using the same Transformers installation, not an independent implementation of the model. [FLAN model card](https://huggingface.co/google/flan-t5-base) · [mT0 model card](https://huggingface.co/bigscience/mt0-small)

An exploratory second interface ranked answer choices for each field by mean teacher-forced log probability. Both T5 models showed a strong bias toward asking for clarification. FLAN got the two clarification cases right and zero executable requests (2/24 overall); mT0 got 0/24. These known development tasks and post-hoc prompts are diagnostic. The results do not rank the five families after comparable fine-tuning. [Protocol](protocol.md) · [Raw outputs and reference runs](/run/media/whisper/UNLLM_CORPUS/unllm/experiments/usage-audit-AXT0AI) · [Validated results](results.json)
