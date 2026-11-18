# Generated stream-program resources

These are target-program helpers copied from the frozen stream emitters during
native migration. The C++ host reads their source as data and assembles programs
from the typed operation plan. It never imports or executes `common.py` as host
implementation. Python is needed only when the generated Python target is run.

The surrounding reader, transforms, aggregation, entry point and expressions are
constructed by `src/symbolic/stream_emit.cpp`. These helpers are shared primitives,
not complete task programs or prompt-to-program mappings.
