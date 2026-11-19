# N26 · Can CoNaLa supply training pairs for the unLLM request interpreter?

The curated CoNaLa training split yielded three verified rows, two different English requests, and one independent Stack Overflow question for the current integer-list task. Both requests count values strictly above a threshold. One specifies the literal 5 and one names a scalar variable. CoNaLa contributes two useful examples for this experiment. It leaves most of the meanings we want to teach uncovered. This result applies to the current schema and this split. The broader coding roadmap and CoNaLa's larger mined release would require separate audits. [CoNaLa project](https://conala-corpus.github.io/) · [Pinned training file](https://huggingface.co/datasets/neulab/conala/blob/fbc749f1c537e5c3834e93b15784302e331debe2/data/conala-paired-train.json)

## Method

The protocol and 16 separately worded project-authored questions were frozen before examining the training rows for this audit. Twelve questions have executable meanings, two require clarification, and two request outputs outside the schema. The set varies operand order, equality boundaries, numeric literals, references to caller inputs, and explicit implementation constraints. All labels passed the N21 JSON schema check. None of the requests duplicates a training title or rewritten intent after case and whitespace normalization. These are diagnostic examples from the project, with no independent human validation. The audit produced no interpreter predictions or training run. [Frozen checksums](frozen.sha256) · [Protocol](protocol.md)

The screen covered 2,379 training rows from 1,710 source questions. It used the rewritten intent when available and fell back to the original title 79 times. The union of wording and Python syntax screens produced 423 candidates. A seeded sample of 75 other rows and a second wording sweep of 62 more brought the reviewed set to 560. The other 1,819 rows received automated screening only. The direct count records matches found under this method. It is possible that an unusual implementation or wording escaped both screens. These screens are offline data-selection tools. [Screen](screen.py)

## What mapped

| Source rows (one-based) | English meaning | Source implementation | unLLM fields |
|---|---|---|---|
| 948, 949 | Count list entries greater than 5 | Sum boolean comparisons or take the length of a filtered list | count, gt, literal 5, no required structure |
| 950 | Count list entries greater than scalar i | Convert the list to a NumPy array and sum the comparison mask | count, gt, caller input, no required structure |

All three belong to question 10543303. The exported pairs preserve the original English, snippet, question ID, and the binding between the original names and the supplied unLLM signature. Both versions of the literal example have identical English. Any later partitioning must group examples from that source question together to avoid counting copies as transfer. [Source question](https://stackoverflow.com/questions/10543303)

The direct matches cover **2 of 20** combinations of aggregate, comparison, and threshold source (2 × 5 × 2). Neither request requires a loop. We found no complete example of filtered summation, an inclusive boundary, equality, a less-than comparison, or explicitly required nesting. The source data supplies an answer snippet per request and provides no explicit clarification labels. Several near misses make useful examples of a clear request outside the schema, including returning positions or a proportion. Using them to train abstention would require assigning that label relative to unLLM's capabilities.

| Field | What can supply its label | Example from the audit |
|---|---|---|
| Aggregate and result | Read the operation's effect and check the requested result | Summing booleans in row 948 means count. Row 1011 genuinely sums qualifying values, but its input is a dictionary. |
| Comparison and threshold | Check operands, scope, and boundary in both English and code | Row 325 says greater than 5, while the snippet includes values equal to 5. Excluded. |
| Input names and types | Bind the names to the supplied signature and preserve the source binding | In row 950, j is the collection and i supplies the threshold. In row 948, i is a local loop variable. |
| Implementation constraints | Read what the English explicitly requires | Row 949 uses a comprehension without requiring one. Rows 734–735 explicitly request a for loop over a dictionary and can teach only that field here. |
| Clarification | Identify the missing choice in the English and write a question that resolves it | A snippet that silently makes the choice cannot establish that the user supplied it. |

The review ledger records selected partial labels and exclusions. Its partial rows illustrate reusable fields. They are not an exhaustive count of every reusable phrase. [Selected reviews](selected-reviews.tsv)

## Execution checks

Each of the three reviewed original snippets was run in isolation on 14 inputs, including equality, empty lists, duplicates, negative values, 64-bit endpoints, and the same data with changing cutoffs. All **42/42** results matched a reference count. Both unique labels also went through the normal unLLM CLI with the model off. Both emitted Python and compiled C++ passed all 14 cases each, for **56/56** generated-function checks. These runs supplied the labeled JSON to the CLI. English interpretation accuracy remains unmeasured by N26. [Replay results](replay.json)

## Decision

Keep the two verified requests with their source provenance. For the next data experiment, measure how many suitable aggregate and comparison labels can be recovered from WikiSQL's training questions and structured queries. Require a single numeric column and a compatible literal condition before treating a whole query as a candidate. Check whether the English still makes sense with the supplied signature and distinguish partial supervision from a full pair. WikiSQL's conditions encode a comparison value, so direct supervision for a caller-supplied threshold, explicit loop requirements, and clarification still needs another source. Those gaps are appropriate targets for deliberately paired English examples. Keep the frozen requests out of training and reserve them for the model comparison. [WikiSQL data format](https://github.com/salesforce/WikiSQL#question-query-and-table-id)

## Replay

Corpus data, held-out data and exported pairs are on the USB corpus volume. The training file SHA-256 is in the replay results. From the upgrade checkout, using a built unLLM, clang++, bubblewrap, system NumPy and the N21 Python experiment dependencies, run the following commands.

```sh
audit=/run/media/whisper/UNLLM_CORPUS/unllm/experiments/conala-audit-QucVKM
sha256sum -c archive/interpretation_diagnostics/n26/frozen.sha256
python3 archive/interpretation_diagnostics/n26/screen.py "$audit"
python3 archive/interpretation_diagnostics/n26/check.py "$audit"
```

The screen reproduces the candidate index. The replay checks the exact file digests, validates the holdout labels, checks for exact wording overlap, executes the reviewed snippets and the generated programs, and exports direct.jsonl alongside the source split. The selected review decisions are recorded observations.
