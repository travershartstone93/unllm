# Two-call C++ prototype

Run the interpreter and editor against a prepared C++ project. The first fresh model call converts the English request into JSON. The second sees that JSON and numbered project files and returns replacements for original line ranges. The C++ driver validates the replacements and writes a new source tree. All files outside the replacement ranges retain their original bytes.

```sh
experiments/boilerplate/two_call/run.sh request.txt assembled-dir new-run-dir
cmake -S new-run-dir/project -B new-run-dir/build -G Ninja
cmake --build new-run-dir/build
ctest --test-dir new-run-dir/build --output-on-failure
```

Use a fresh run directory outside the input tree. The driver expects regular text files and edits existing lines in existing files. A replacement can add multiple lines. Intersecting ranges and unknown files fail the run. Build and acceptance checks run separately. Each model call saves its prompt, answer, CLI events, and reported usage. Set `MODEL` and `EFFORT` to select a different model or reasoning budget.

B06 replays the four-brick assembly selected for B04 to isolate interpretation and editing costs. Catalog search and selecting a different assembly are separate experiments. The comparison uses B05’s direct-generation baseline, its original coding conventions and B04’s frozen external checks. Count reasoning inside output and cached input inside input. Count failed calls if a run needs a repair.
