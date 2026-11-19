# N30 · Authored English requests and expected JSON

The dataset contains 60 training records grouped into 30 two-request contrasts and 24 separately worded development records. Both files live on the USB corpus volume. The development file was written and hashed before writing the training file. The previously inspected N26 regression set remains separate.

[Training JSONL](/run/media/whisper/UNLLM_CORPUS/unllm/experiments/authored-pairs-0WcZHU/train.jsonl) · [Development JSONL](/run/media/whisper/UNLLM_CORPUS/unllm/experiments/authored-pairs-0WcZHU/dev.jsonl) · [Checksums](SHA256SUMS)

## Reading a record

Every request uses the N21 signature. The caller provides `values`, a list of integers, and `cutoff`, an integer. Development requests sometimes name the values by their application meaning, such as bids or payments. That context is part of the English request. The task is to interpret requirements for the existing filtered-count or filtered-sum constructor.

Use `request` as the English input and `expected` as its JSON target. The record ID and training-only `contrast_id` are metadata. Numeric literals remain distinct from references to the supplied cutoff. The empty implementation-constraint list leaves the choice of implementation open. The explicit constraints record a for loop or an if nested inside a for loop. For clarification requests the label supplies one appropriate question. When scoring a different phrasing of that question, check that it resolves the same missing choice.

| Rows | Executable | Clarify | Unsupported | Coverage |
|---|---:|---:|---:|---|
| Training 1–30 | 20 | 5 | 5 | Every aggregate × comparison × threshold-source combination once |
| Training 31–60 | 20 | 5 | 5 | A second wording for each combination |
| Development 1–24 | 20 | 2 | 2 | Every combination once, plus four stop decisions |

For the 30-example run, use the first 30 physical records before any shuffling. Each half keeps 15 complete contrast groups. Ten training groups vary the meaning between two executable requests. Ten pair an executable request with a clarification and ten pair it with a clear request outside the schema. The controls include competing choices, a referenced decision missing from context, and a requested output such as positions, a percentage or a minimum. Some ambiguities are explicit in the wording. Performance on these project-authored controls alone cannot establish clarification quality in ordinary conversations.

## Verification

Check the checksums from the corpus directory. From the project checkout, run the following command with the N26 experiment dependencies, a built CLI and clang++ available.

```sh
python3 archive/interpretation_diagnostics/n30/verify.py \
  /run/media/whisper/UNLLM_CORPUS/unllm/experiments/authored-pairs-0WcZHU \
  /run/media/whisper/UNLLM_CORPUS/unllm/experiments/conala-audit-QucVKM/holdout.jsonl
```

The verifier checks schema, coverage, grouping, the two balanced halves, and exact wording duplication after case and whitespace normalization across both authored splits and N26. It supplies the authored labels to the CLI and executes the generated functions on empty lists, ties, duplicates, mixed signs and varying cutoff arguments. Explicit loop constraints get syntax-tree checks in both languages. For a stop label it checks the reported status and absence of emitted source, and for a clarification it also checks that the question survives the handoff. These checks validate label format and backend behavior. The English-to-label judgments are author-reviewed; testing a trained interpreter is a separate experiment.
