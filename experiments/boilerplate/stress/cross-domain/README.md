# B26 cross-domain coding and brick-selection tests

Six English requests cover replenishment, telemetry, payment reconciliation, build scheduling, binary compression and a reusable C++ library. The [requests](requests.json), [literal expected results](fixtures.json), [runtime evaluator](check.py) and [standalone C++ library client](waitlist-test.cpp) were frozen before the live runs. Each request went through the ordinary runner with fresh interpretation, package discovery when eligible, the existing 43 cards, and at most one model repair.

| Case | Discriminating checks | Purpose |
|---|---|---|
| Inventory | Equal thresholds, two interleaved SKUs, suppressed repeats and rearming | Low-side, independently tracked state |
| Sensors | Equality at both thresholds, interleaved sensors, negative temperatures, one-based input rows | Transfer the same operation to a high-side trigger and different output |
| Finance | Duplicate payments, oldest opposite entry first, opposite side arriving first, negative values and zero | Distinguish one-to-one matching from set membership |
| Build | Dependency diamond, independent target, duplicate edge, late cycle and undeclared target | Distinguish parallel waves from any valid serial topological order |
| gzip | Binary payload, concatenated members, empty member, corrupt checksum and missing input | Existing whole-program control |
| Waiting list library | Include the delivered header in an independent C++17 client and check duplicate, cancellation, rejoin and FIFO behavior | Deliverable outside the file-processing CLI contract |

Run in the [integrated environment](../../integrated/README.md). Select a case with `DOMAIN_CASE` to set the independent oracle. For example:

```sh
export DOMAIN_CASE=finance ARCH_INSTALL_TERMINAL=alacritty
suite=experiments/boilerplate/stress/cross-domain
jq -r --arg c "$DOMAIN_CASE" '.[$c]' "$suite/requests.json" > /tmp/unllm-finance.request
experiments/boilerplate/integrated/run.sh /tmp/unllm-finance.request /tmp/unllm-finance-run "$suite/check.py"
```

## Admission decision for the next brick

Screen the official Arch and AUR catalogs first. Preserve the shortlist and distinguish a verified package invocation from a relevant package description. A stock gzip invocation with standard input/output wiring satisfies all five compression checks on this host. Its package is in official core. This belongs in invocation-profile coverage. The current host utilities used in these diagnostics are distro builds. The raw tsort control only produces a serial order from edge pairs. The catalog search alone cannot establish the absence of software for any of the other tasks.

The experimental [keyed latch](candidate/keyed_latch.hpp) records which identifiers have already triggered. Its C++ callers decide when to trigger or rearm, how to parse input, and what to emit. These [two diagnostic callers](candidate/check-consumers.cpp) are hand-wired and do not ship as bricks. Build each with C++17, `-DINVENTORY` or `-DSENSORS`, and `-ljson-c`. Both use the exact same header. A mutation that always accepts a trigger distinguishes genuine use of its state from code that simply links or mentions the component.

Selection is scored independently. Append the [single candidate card](candidate/card.json) to the 43-card catalog, update the catalog snapshot in the review file, retain all original examples, and submit the same saved role lists to the deterministic picker and Qwen on its gaps. Separately ask an interpreter without access to the catalog for reusable responsibilities. Compare grouped versus individual role presentation and card orders. A hand-wired success or a diagnostic model selection earns no integrated reuse credit. Promotion requires selection, actual binding, and a passing executable in the normal pipeline on unseen requests. [Experiment log](../../../../docs/EXPERIMENT_LOG.md#b26-cross-domain-requests-and-a-selective-brick-trial)
