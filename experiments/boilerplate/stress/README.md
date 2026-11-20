# B23 file-processing stress test

Freeze the six English requests, fixtures and evaluator before the first model call. Use the B22 package catalog and reviewed invocation profiles, B16 brick catalog and B21 integrated selection/assembly/repair policy. Run each once with a new Big Pickle session. Keep the same independent checks before and after the single permitted edit. Use Arch/AUR discovery only. If a missing reviewed official package qualifies, present the existing install choice in a separate terminal window. Record package leads, selection and actual bound goals separately. A top-eight shortlist miss does not establish absence across the AUR or official repositories.

| Case | Boundary | Acceptance |
|---|---|---|
| plain | Known signed official program, positive control | Independently decode two asymmetric JPEGs and compare P6 pixels without mirroring. |
| mirror | Same input/output types and package as plain, new required behavior | Compare both P6 images to horizontally flipped originals. The plain converter must fail both checks. |
| textstats | Package and brick can extract text. Second output consumes the first. | Compare text order and complete JSON word frequencies, including repeats across pages, punctuation, mixed case and image-only input. |
| pages | Near match to embedded-image export, distinct operation | Verify JPEG filenames, count, dimensions and full-page raster against an independent reference on mixed, image-only and text-only documents. |
| ledger | Custom stateful policy outside the executable brick collection | Verify order, reset, discard, signed arithmetic, unfinished batches and malformed input with no published output. |
| clarify | Mutually exclusive, unresolved interpretations | Ask a specific question distinguishing pages from embedded pictures before discovery/build. |

Every executable also gets a missing-input check. The evaluator runs it in a fresh network-isolated Bubblewrap sandbox per invocation, with read-only inputs. Python is used only by the external test oracle. Syntax errors, exhausted budgets, invalid model patches, and missing artifacts count as failures. Successful model repairs count separately from reuse. An expected clarification counts separately from executable passes. Do not improve the pilot, labels or prompts using results during this run.

## Replay

Set the picker and UDPipe environment described in the [integrated runner guide](../integrated/README.md). For example, run the ledger case from the repository root with a new output directory:

```sh
export STRESS_CASE=ledger ARCH_INSTALL_TERMINAL=alacritty
jq -r --arg c "$STRESS_CASE" '.[$c]' experiments/boilerplate/stress/requests.json > /tmp/unllm-stress-request.txt
experiments/boilerplate/integrated/run.sh /tmp/unllm-stress-request.txt /tmp/unllm-ledger-run experiments/boilerplate/stress/check.py
```

For clarification, inspect the question and stop stage. An executable or an unrelated clarification fails that case. Score an unexpected early exit without a result file as a failure. The external harness uses system Python with Pillow and Pycairo, pdftoppm, the B16 fixture directory and Bubblewrap. [Frozen run and outcomes](../../../docs/EXPERIMENT_LOG.md#b23-complex-requests-and-routing-boundaries)
