# Fresh CLI and interaction trial

This is the historical trial before the 2026-11-12 repairs. The generator was
frozen for this trial. [The subsequent repair report](REQUEST_REPAIR.md) retests
these exact questions in both languages and retains the failures below as evidence
of the earlier behavior. Source links below describe the code at that time.

The generator and executable were frozen before these requests. No generator changes were made during the trial; hashes of 57 source/binary files still match. All requests used the actual CLI with the model disabled and an isolated fact store. This is a small, deliberately varied trial, not a representative accuracy estimate.

## Ordinary English requests

| Request | Target | CLI outcome | Time |
| --- | --- | --- | ---: |
| Build an app with an editable text area, a character count, and lowercase and save buttons. | Python | Generated and internally validated; independent interaction checks pass. | 3.423 s |
| Build an app that decreases a counter by 11. | C++ | Generated and internally validated; independent decimal-typing checks fail. | 29.353 s |
| Make a to-do list app with add, remove, and save buttons. | Python | Unsupported: grammar could not analyze it within budget. | 1.318 s |
| Create a stopwatch app with start, stop, and reset buttons. | Python | Unsupported stopwatch concept. | 1.282 s |
| Create an app that converts Celsius to Fahrenheit. | C++ | Unsupported conversion predicate. | 0.920 s |
| Build a tip calculator with bill amount and tip percentage inputs. | Python | Unsupported tip requirement in the older calculator translator. | 1.302 s |

Two of six requests generated programs. The internal validated status did not establish that the counter was usable with decimal keyboard input.

The temperature rejection is particularly informative: the backend previously constructed temperature conversion from an explicit specification, but this ordinary English description does not reach it. The tip request is intercepted by the calculator-name branch in [language.py](../synthesis/language.py#L137), before the shared behaviour translator is considered.

## Independent interaction checks

These checks used new external Qt harnesses and fresh values, without invoking the built-in validation scenarios or their outcome oracle. They drove actual widgets. Dialog choices were supplied through the application's normal dialog service interface; file I/O and cancellation control flow executed in the generated program. Execution used the existing bubblewrap isolation helper.

The Python text program passed ten checks: ASCII typing plus Unicode/emoji paste, a live character count, lowercase conversion, saving exact UTF-8 contents, clean state after save, updated counts after editing, overwriting the selected file, reusing its filename, cancelling close, and preserving unsaved contents.

The C++ counter failed two decimal keyboard cases:

| Typed | Input actually accepted | Result after subtracting 11 | Required result |
| --- | ---: | ---: | ---: |
| 17.5 | 17 | 6 | 6.5 |
| 0.25 | 0 | -11 | -10.75 |
| -100 | -100 | -111 | -111 |

Its synthesized arithmetic rule is correct. The field refresh in [cpp_runtime.h](../synthesis/behaviour/cpp_runtime.h#L159) calls setValue even when the numerical value has not changed, reformatting partially entered text.

An independent Qt-only reproduction isolated the cause:

| Field implementation | Typing 17.5 produces |
| --- | ---: |
| Ordinary QDoubleSpinBox | 17.5 |
| Unconditional setValue feedback | 17 |
| Feedback guarded by a value comparison | 17.5 |

The current built-in [numeric scenario](../synthesis/behaviour/validate.py#L51) drives [setValue directly](../synthesis/behaviour/cpp_driver.cpp#L45), bypassing keyboard entry. Its generated transition inputs are integers. Those checks therefore missed this user-facing problem.

## Separate structured-specification trial

A newly named shared-cost program was requested through the CLI with the relation:

~~~text
3 * next.share == prev.rent + prev.utilities
~~~

It generated a Python application in 1.446 seconds. An independent harness entered both numeric inputs through the keyboard, clicked its split action and checked the displayed answer with Python Decimal:

| Rent | Utilities | Displayed share | Required share |
| ---: | ---: | ---: | ---: |
| 1234.50 | 138.45 | 457.65 | 457.65 |
| 596.01 | 45.66 | 213.89 | 213.89 |
| 0.01 | 0.02 | 0.01 | 0.01 |

All three pass. This is evidence for construction from explicit relational requirements. It does not turn the four rejected English requests into successful attempts.

## Assessment

The shared backend can construct new compositions from detailed contracts. Ordinary English coverage remains narrow, legacy calculator routing still affects requests, and generated GUI code has an input bug missed by the earlier checks. The prior regression and composition scores overstated practical readiness when read as evidence of reliable general request-to-program generation.

The generator was left unchanged so these failures remain reproducible.

Raw reports: [requests](../.benchmarks/manual_trial.json), [independent interactions](../.benchmarks/manual_trial_interactions.json), [isolated decimal-input cause](../.benchmarks/manual_trial_decimal_cause.json), and [structured-specification interactions](../.benchmarks/manual_trial_structured.json).

The exact requests, source snapshot, additional harnesses and structured specification are in the ignored .state/manual-trial directory. Generated source locations are recorded in the reports.
