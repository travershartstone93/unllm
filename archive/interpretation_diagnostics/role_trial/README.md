# Embedded SWI role-binding trial

N10 passes the native parser's bounded MRS readings into a development-only executable linked to the existing native logic bridge. Each Prolog invocation gets one reading, including argument variables, predicate classes and signed literal values, with a 100,000-inference limit. The two outputs under study are numeric role pairs and the set of available save targets in an unspecified-target request. The probe omits full requirement coverage checks and program construction.

Run from the checkout using a configured native build.

```sh
cmake -S . -B build
cmake --build build --target interpretation_role_trial
build/native/interpretation_role_trial "$PWD"
```

The first version required the update preposition to attach to the change event. It missed both previously supported controls, then terminated when the grammar returned no reading for the original save request. The [partial run](strict-partial.jsonl) retains those failures. Inspection of the previous accepted counter showed the preposition attached to the affected entity. The follow-up allowed that attachment, recorded parser stops, and added alternative save phrasings as diagnostic controls.

The [final run](results.jsonl) retains the first witness for each outcome and counts all readings. Both previously supported numerical pairs were recovered. For the coordinated request, just five readings contained both verbs. In the first such reading both prepositions point to the same variable and neither verb has the counter as a direct argument. Across all 128 readings the trial found no complete pair. All 18 readings of the alternate ambiguous save request retained both targets. All six readings of the single-list control retained only the collection. Three requests stopped during parsing.

Timing is one observation per request in a single process. Binding includes row projection, JSON transport, SWI search and result decoding. Baseline timing covers complete native grounding. Both exclude ACE. These figures measure the cost of the prototype and cannot predict the latency of a production replacement.

Keep the binder experimental. Next inspect the five two-verb readings for relative-clause, coordination and numeric attachment evidence. Check any proposed rule against reversed operands and unrelated numbers before extending the grounder.
