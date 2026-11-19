# Stage 13b programming-request history experiment

On 2026-11-18, the native parser produced identical canonical candidate multisets, contract identities and stop decisions across five histories for all 32 development requests. Both word orders of the mixed text-editor and counter request grounded the same contract in all ten trials. These are development cases. The independent release evaluation remains open.

## What changed

ACE 0.9.34 enumerates a bounded prefix of ERG readings with zero ranking weights. The earlier resident process returned different prefixes depending on its previous inputs. At 64 readings, the original mixed request succeeded in two of five histories. The reordered request succeeded in four of five. The cold failure reported an unhandled location predicate even though other enumerations contained a fully grounded program.

Programming requests now start their own ACE enumeration for the sentence and, when needed, its fragment fallback. The default program budget is 128 readings with four seconds for the full sentence and one second for the fallback. Factual queries retain their resident parser. At the new bound, readings 107 and 111 ground the original request. Readings 3, 13 and 16 ground the reordered request. All five witnesses yield the same normalized contract, including the uppercase and save actions, character count and increment of four. The original wording cites a different genuine ERG sense of “increase” than the historical fixture.

When enumeration reaches its limit without grounding a complete contract, the product reports a resource limit and the distinct reasons the observed readings failed. An accepted bounded result discloses the cap in the CLI's JSON parser diagnostics. Two observed incompatible complete contracts require clarification. A parser-reported timeout also stops the request, including when ACE returns partial readings.

## Results and tradeoffs

| Per 32 requests | Result |
| --- | ---: |
| Accepted with the expected executable specification | 12 |
| Unsupported | 8 |
| Ambiguous | 1 |
| Reading budget exhausted | 11 |

Of 24 annotated supported requests, 12 succeeded. Of eight annotated stop cases, six received a semantic stop and two reached the reading limit. No incorrect program was accepted. The earlier resident baseline already accepted the same 12 programs. The observed improvement is reproducibility across histories. Starting fresh and allowing more readings incurs additional work per programming request. This trial does not establish a latency improvement or rule out another contract beyond the bound. [Per-request results](development-results.json)

A direct CLI run generated the mixed Python program. An offscreen Qt interaction typed five mixed-case characters, checked the character count, clicked Upper, changed the counter from 2.5 to 6.5, and clicked Save. The saved file contained the uppercase text.

## Replay

From the upgrade checkout, give the probe a new output directory under an existing parent.

```sh
cmake --build build --target interpretation_history_probe
build/native/interpretation_history_probe "$PWD" .state/interpretation-history-replay
jq -es -f experiments/interpretation_diagnostics/summarize.jq \
  .state/interpretation-history-replay/trials.jsonl
```

The probe uses the public migration fixture and runs fresh, resident, immediately repeated, following unrelated input, and seeded shuffled histories. It saves raw ACE output, normalized text, lexical spans, a canonical identity per reading, translation decisions and grounding outcomes. The identity includes scope constraints, variable properties, argument roles, predicates, constants and spans. The diagnostic stops if a reading requires more than 1,024 tied label permutations. The product does not run this diagnostic canonicalization.

The original traces are in `.state/interpretation-history-1795049352` and the verified run in `.state/13b-verified-1795051277`. The ERG grammar image SHA-256 is `2968621bf88d50cffa9ec52c5edead7b16aabff573421f2f97c113e056ff5c82`. The next interpretation milestone introduces a typed requirements graph with source evidence for roles, modifiers and exclusions.
