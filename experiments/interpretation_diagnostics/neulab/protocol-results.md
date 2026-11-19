## N20 results

NeuLab checkout 7bd432f96a76bef861f8ef0320f98f2cf9a1759b, unchanged. Python 3.7.16, PyTorch 1.1.0, astor 0.7.1, NumPy 1.21.6, NLTK 3.4.5 with punkt, XGBoost 0.90, CPU. The authors' CoNaLa configuration loads the parser and all reranking weights. The Python 3.7 environment and models are local to the experiment. Use the project root for checksum verification. For replay, enter the downloaded checkout and run:

```sh
cd .state/neulab/upstream
NLTK_DATA="$PWD/../nltk_data" PYTHONPATH=. OMP_NUM_THREADS=1 OPENBLAS_NUM_THREADS=1 ../env/bin/python ../../../experiments/interpretation_diagnostics/neulab/run.py
cd ../../..
python3 experiments/interpretation_diagnostics/neulab/score.py
```

All 41 first answers compiled as Python. The upstream demonstration produced a reverse iterator; it yielded the expected order on all three test lists after binding the snippet's input name. N16 behavioral tasks scored 0/8, mixed structural/behavioral tasks 0/8. On the eight structural tasks one two-argument lambda worked with integers, satisfying S01's functional signature; it did not declare a named function. The remaining seven missed the requested structure. This gives 1/24 by functional signature, 0/24 if an explicit declaration is required. All eight controls received code. Five original cutoff phrasings failed; naming both identifiers produced a constant threshold of 5 and passed only four of eight reserved cases. The additional “Count the values” and interval prompts produced an equality count over dictionary values and a range of 100 respectively (qualitative diagnostics). Results describe this released Python snippet model on these fixtures. Keep it outside the product.
