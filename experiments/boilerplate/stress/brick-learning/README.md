# B31 generation and reuse of a C++ brick

Run `test.sh NEW_DIRECTORY` with the native targets `brick-provider-plan`, `unllm_logic_worker` and `brick-gir-adapter` built. Fresh interpretations use the integrated hosted OpenCode fallback order. Set `BRICK_REPLAY_DIR` to the absolute path of the sibling `evidence` directory to reuse the recorded interpretations. Use a fresh directory for each test.

The fixture contains seven original components and three text operations from the project. Both tasks use the integrated provider planner with an empty picker-hint list, a fixed one-input/one-output C++ CLI and a sandboxed evaluator. Package discovery, whole-output selection and model editing are outside this comparison. The native generator sees the missing port types and installed GLib metadata. The independent C++ client receives the generated function by macro and tests it with different inputs. The second request receives neither the client nor any instruction to select the learned brick.

The runner saves the catalog before and after publication, the model responses, source, plans, test reports, checksum receipt and negative controls. The original bundle is retained to repeat the second request after removing the treatment. [Frozen requests](requests.json) · [Experiment results](../../../../docs/EXPERIMENT_LOG.md#b31-creating-and-reusing-a-brick)

The B31 qualification client is preserved in `evidence/b31-client.cpp`. The current `client.cpp` also checks a comment immediately after an empty line. The full USB admission and repeat measurements are recorded in the [B32 experiment log](../../../../docs/EXPERIMENT_LOG.md#b32-uri-reader-on-the-full-usb-library).
