# Unchanged-generator retest

Retested on 2026-11-12 after [the request repairs](REQUEST_REPAIR.md).
The generator was left unchanged; SHA-256 hashes still match all 54 captured
Python/C++ runtime, generation and executable files. Tests wrote only new
artifacts and reports. The original checkout and carried user edits remain intact.

The same six English requests were regenerated through the actual CLI in both
languages. All **12/12 outputs** passed the independent Qt keyboard/mouse/file
workflows again, covering 78 assertions. The complete CTest run also passed
**12/12 suites**, including 64 Python tests across the six upgrade suites,
in 209.80 seconds.

Three further requests gave mixed results:

| Fresh request | Language | Result |
| --- | --- | --- |
| Create an app that converts Kelvin to Celsius. | C++ | Generated and independently passed 273.15 → 0, 0 → −273.15 and 310.15 → 37. |
| Build a tax calculator with amount and tax percentage inputs. | Python | Generated and independently passed variable rates, zero tax and small decimals; 199.99 at 17.5% gives 34.99825 tax and 234.98825 total. |
| Build an app with an editable text area, a character count, and uppercase and save buttons, and a counter that increases by 4. | Python | Unsupported: `No semantic frame is defined for loc_nonsp`. No app was generated. |

Reordering the failed request to:

> Build an app with an editable text area, a character count, a counter that increases by 4, and uppercase and save buttons.

generated an app that independently passed Unicode editing, character counting,
typing 17.5 and incrementing to 21.5, uppercase conversion and exact UTF-8 saving.
A shorter equivalent request without the character count also passed.
Repeating the two full requests through the CLI reproduced the original rejection
and the reordered request's validated result.

This exposes remaining sensitivity to grammatical phrasing and coordination.
The combined text/counter behavior works once grounded; ordinary descriptions do
not reliably reach it yet. The returned loc_nonsp error is the first rejected
interpretation's diagnostic, not proof that adding one predicate would repair
every reading. No fix was made during this retest, and the rejected attempt stays
in the results. These are development probes, not a blinded accuracy estimate.

[Raw local results](../.benchmarks/request_retest.json) contain the exact requests,
interaction outcomes, job paths, repeat confirmations, suite counts and isolation
audit. The report points to the dated .state/retest artifact directory, which also
preserves the preceding reports and the rerun logs.
