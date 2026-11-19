# N15 · Lightweight programming-construct recognition

Nearest-neighbor retrieval recovered three construct sets that exact phrase lookup missed, two branching requests and one console-output request. A linear support vector machine (SVM) recovered the console-output request. Both fallbacks preserved five incorrect lookup proposals. Nearest-neighbor retrieval met the predeclared feasibility criterion and merits a trial in which suggestions must bind to specific parts of a request before use. [Frozen protocol](protocol.md) · [Per-request predictions](results.tsv)

## Results

The held split contains 20 requests for constructs and 12 controls. Each positive request can require several labels. The column “Incomplete or extra” counts nonempty proposals with a wrong set on a positive request. False alarms propose constructs on controls.

| Method | Exact set on 20 | Incomplete or extra | Positive abstentions | False alarms on 12 | Correct control abstentions |
|---|---:|---:|---:|---:|---:|
| Exact phrase lookup | 6 | 2 | 12 | 3 | 9 |
| Lookup + linear SVM fallback | 7 | 2 | 11 | 3 | 9 |
| Lookup + nearest labeled example | 9 | 2 | 9 | 3 | 9 |
| SVM alone at the fallback threshold, diagnostic | 1 | 0 | 19 | 0 | 12 |

Both fallback thresholds calibrated to 0.6 on 16 separate requests. Each added two exact proposals there. The SVM threshold measures distance in classifier-score units, and the nearest-neighbor threshold measures cosine similarity. Neither score is a probability. Training used 44 request examples and 20 single-construct phrases. Nearest neighbor and SVM saw the same examples and feature vectors. All data are author-written, and the splits share some phrasing. [Request splits](requests.tsv) · [Lookup and training phrases](lexicon.tsv)

On the held split, nearest neighbor recognized “Echo the total on the screen,” “Choose between two branches according to the sign,” and “Check the flag and take one path on success and another on failure.” The SVM recovered only the first. All three learned gains were single-construct requests. Both fallbacks inherited the two partial proposals because they ran only on empty lookups. Three controls triggered false alarms through mentions of a construct, including a quoted string and a method name read from a configuration file. A high classifier score would not establish the user's intention.

On the eight previously observed N14 requests, lookup and both combined methods recovered six exact sets and missed one label on each of the remaining two. The standalone SVM recovered two and abstained on six. “A for loop containing an if statement” and its reverse receive the same set of labels on the held split. That score gives no credit for ordering them correctly. The experiment emits only labels. End-to-end correctness requires binding roles and nesting, independent checks of program behavior, and a new evaluation through the regular CLI.

## Replay

From the checkout root, using a C++20 compiler and OpenCV's C++ core and ML libraries. The measured run used GCC 16.2.1 and OpenCV 5.0.0 on x86-64 Linux. Fitting, calibration and scoring together took 0.255 seconds in one process, excluding compilation. A second process produced byte-identical results.

```sh
trial_dir=archive/interpretation_diagnostics/construct_ml
trial_build=$(mktemp -d)
c++ -std=c++20 -O2 -Wall -Wextra $(pkg-config --cflags opencv5) \
  "$trial_dir/trial.cpp" -lopencv_ml -lopencv_core -o "$trial_build/trial"
"$trial_build/trial" "$trial_dir" > "$trial_build/results.tsv"
cmp "$trial_build/results.tsv" "$trial_dir/results.tsv"
```

The checkout was based on `f336d71869aec4afdc69db49ff5cfe5ef8f36df0`. SHA-256 digests of the isolated scoring inputs and output follow.

```text
86720556d13a3ee827b03c2e3d9ff99b0fc8d8be06549ec4e9d36fc6536e306a  protocol.md
6de904d4b165aa8c4d3f0eb36093da6c57e9c1a365e6f07a381d181457ef2d69  requests.tsv
ad9ff3045c99c35563ff155faa0483fc5e36de4b98429e0caba7ff63577402ee  lexicon.tsv
4e849b8ab970e70aa2bd9c63d1dde6ce5a62d8abe506429ad72cc68ab0207006  trial.cpp
119f0a84343bc5a53587b3c69a54f8775a0731afc9a22217eaa35d5725124642  results.tsv
```
