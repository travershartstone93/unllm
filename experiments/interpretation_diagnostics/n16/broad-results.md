# N16 broader result and decision

After the adjusted SEMPRE candidate passed the separate bounded-stream gate, open the previously frozen coding fixture. All 32 requests ran through the normal CLI in both targets, using isolated state. The product returned 18 unsupported and 14 resource-limit responses in each language. On each response the experimental fallback's frozen vocabulary requirement failed. We reused the actual SEMPRE tokenizer and training vocabulary to make this check. Its chart was short-circuited on these requests. The adjusted hybrid consequently reproduced the product responses. [Evaluation protocol](broad-protocol.md) · [CLI responses](broad-results.json) · [Vocabulary outcomes](broad-vocabulary.tsv)

| Requirement | Stock and experimental hybrid |
| --- | --- |
| Requested structure | 0/8 produced a program |
| Requested behavior | 0/8 produced a program |
| Behavior with explicit implementation constraint | 0/8 produced a program |
| Eight ambiguity, contradiction and unavailable-capability controls | 8/8 stopped, 0/8 gave the specified choice, conflict or missing-input diagnosis |
| Wrong accepted programs | 0/32 |

The four clarification cases never established a pending choice, so no follow-up answer could be scored against one. Zero wrong acceptances here coincided with zero coverage. These author-designed prompts were frozen before candidate development. They are now regression data. Independent confirmation was not reached.

**Decision.** Keep the verified structured-contract bridge. End this stream-parser candidate's general-coding evaluation and keep it outside the product. Its learned ranking improved a real but limited task: composing known stream operations from familiar wording. Removing the vocabulary guard would expose unmeasured errors, and its target grammar still cannot request a function declaration, bind arbitrary parameters, or express statement nesting. Expanding wording alone cannot address these representation gaps. A future candidate needs to represent those requirements from a supplied structured contract and demonstrate that they survive construction, before choosing another English model. [Integration evidence](bridge.md) · [Bounded-stream scores](sempre/ranking/README.md)
