# Knowledge packs

unllm answers questions about the world from reference text kept on your own
machine: an encyclopedia, a dictionary, question-and-answer sites and a
collection of poems. Each source is turned into a **pack**, a folder of
SQLite files that unllm can search in milliseconds. No dataset text ships
with unllm: you download the sources and build the packs yourself with the
two tools in `tools/`, and every answer names the source it came from.

## Getting the sources

`tools/download_knowledge.sh` downloads everything into
`~/Downloads/unllm-knowledge`, one folder per source. Re-running it resumes
anything unfinished. Each folder gets a `source.json` note (name, version,
licence, date) that the pack builder reads.

| Folder | Source | Size | Licence |
|---|---|---|---|
| `finewiki-en` | English Wikipedia, from FineWiki (HuggingFaceFW/finewiki) | 38 GB | CC BY-SA 4.0 |
| `finewiki-simple` | Simple English Wikipedia, from FineWiki | 0.5 GB | CC BY-SA 4.0 |
| `stackexchange` | 23 StackExchange sites (archive.org dumps) | 9.4 GB | CC BY-SA 2.5, 3.0 or 4.0, by post date |
| `wiktionary` | English Wiktionary, from kaikki.org | 3.2 GB | CC BY-SA 4.0 and GFDL |
| `wordnet` | Open English WordNet | 10 MB | CC BY 4.0 |
| `gutenberg-poetry` | Gutenberg poetry corpus | 73 MB | Public domain in the US |

**Why FineWiki.** FineWiki is extracted from the pages Wikipedia itself
renders, so everything a template produces is still there: measurements,
units, dates and formulas. The older `wikimedia/wikipedia` dump is extracted
from the page source and loses all of that: on English Wikipedia about one
article in twelve has a hole where a number was ("It is about  high") and one
in ten has emptied brackets. The pack builder still reads that dump, but the
download script fetches FineWiki, and checks each file against the checksum
Hugging Face publishes for it (`SHA256SUMS` in the folder).

## Building packs

The builder is `tools/unllm_ingest.py`. It needs Python 3 with `pyarrow` for
the Wikipedia files, and `7z` for StackExchange.

```
unllm_ingest.py add PATH...         build packs from files or folders, in that order
unllm_ingest.py progress [--watch]  bars for the build running now and the ones queued
unllm_ingest.py inbox               build whatever has been dropped in the inbox folder
unllm_ingest.py list                the installed packs
unllm_ingest.py check [NAME]        measure text defects in the installed packs
unllm_ingest.py remove NAME         delete a pack
```

**Watching a build.** `progress --watch` redraws every two seconds: one bar
for all the work (weighted by source size, with how many tasks are done and
the time left at the speed measured so far), then one line per task, in the
order they will run, marked done, NOW or waiting. The task being built shows
what it is doing ("312,000 of 470,000 articles", "posts: 1.2 of 3.4 GB",
"building the search index (4 min so far)"). The builder writes these
figures to small files in `~/.local/share/unllm/progress`, and removes them
when it finishes. A watch window restarts itself when the tool is updated.

**In the system tray.** On KDE Plasma, the machine's progress tray
(`~/.local/lib/taskprogress/progress_tray.py`, user service `progress-tray`)
shows the same jobs as progress items in the tray, like a file copy in
Dolphin: one appears when a job starts and closes when it ends ("stopped" if
its process died). It is not part of this repo. `tools/taskprogress.py` here
only loads that helper, so the scripts in `tools/` and `bench/` report their
own progress with `from taskprogress import Task`.

Packs go to `~/.local/share/unllm/packs`. A build happens in a separate
`NAME.building` folder and replaces the installed pack in one step, so
unllm keeps answering from the old pack until the new one is ready. Long
builds are gentle on the machine: low priority, a pause whenever the CPU
passes 85 C, and resumable file by file if interrupted.

**Every build is checked before it is installed.** The builder reads a random
sample of about 6,000 passages and counts the kinds of damage real sources
have shown:

| Defect | What it looks like | Limit |
|---|---|---|
| number holes | "It is about  high": a number the source lost | 3% |
| empty brackets | "Alabama () is a state": brackets emptied by removed markup | 3% |
| leftover markup | HTML tags, `&amp;`, `{{...}}`, `[[...]]` | 2% |
| broken characters | text decoded with the wrong encoding ("Ã©", "â€") | 1% |
| raw formulas | LaTeX left unconverted | reported only |
| tiny passages | passages under 15 characters | reported only |

The rates are written into the pack's `manifest.json`. A build over any limit
is not installed; the log says why, shows an example, and the build stays in
its folder for inspection (`add --force` installs it anyway). Code passages
are not measured, because code legitimately contains `()` and `<tags>`. Your
own documents (folders of txt, md or pdf) are measured but never refused.

**Versions.** A newer version of a source replaces the installed pack; an
older or equal one is skipped. When the cleaning rules change, the builder's
`INGEST_VERSION` is raised, and every pack built by an older version is
rebuilt on its next `add`.

## What is inside a pack

A pack folder holds `manifest.json` and one or more `shard-NNN.sqlite` files
(English Wikipedia has one shard per input file; the other sources have one).
The manifest names the source, version, licence, the attribution sentence
used in answers, the counts, and the quality measurements. Each shard also
carries those keys in its `meta` table, so the C++ reader needs no JSON.

| Table | Holds |
|---|---|
| `meta` | the manifest's keys, plus counts |
| `docs` | one row per document: title, `title_key` (lowercase, leading "the/a/an" dropped), URL, author, licence, tags, score, extra JSON |
| `passages` | the searchable text, a few sentences each, with the document, its position, its section and its role |
| `infobox` | Wikipedia fact boxes, one row per fact: `Elevation` = `8,848.86 m (29,031.7 ft)` |
| `passages_fts`, `titles_fts` | SQLite FTS5 full-text indexes (Porter stemming, accents folded) |
| `vocab` | each indexed word and how many passages contain it |

Every passage has a **role**, which decides how answers may use it:

| Role | Meaning | Quoted as a sentence? |
|---|---|---|
| `lead` | the opening passage of an article | yes |
| `body` | article prose | yes |
| `list` | a list, headed by what introduced it | yes |
| `table` | table rows, one per line | no (searchable only) |
| `code` | a code block, line breaks kept | no (shown inside how-to answers) |
| `question` | a Q&A thread's question | no (it only finds the thread) |
| `accepted`, `answer` | the accepted answer, or the best one | yes |
| `definition` | a dictionary sense | yes |
| `form` | a sense that only points at another word ("simple past of run") | yes, like a definition |
| `verse` | lines of a poem | yes, for poem requests |

## How each source is cleaned

**Wikipedia (FineWiki).** Headings become section names. Link and reference
sections are dropped (External links, Other websites, See also, References,
Notes, Further reading, Gallery and the like). Prose is packed into passages
of 60 to 180 words; the first is the lead. A run of short list items becomes
one passage headed by what introduced it: the line before it ("The main
types are: red; blue."), a bare label line ("Boardroom: Ann Smith; Bob
Jones."), or the section name; nested items stay with their parent ("July 2:
Ann Smith, Bob Jones"). A short label line that labels nothing is dropped.
Tables become table passages, code blocks code passages. Formulas are
rewritten as readable text: `{\displaystyle E=mc^{2}}` becomes `E=mc²`,
`\frac{1}{2}mv^2` becomes `(1/2)mv²`, `\ce{CO2}` becomes `CO₂`. Fact boxes
go to the `infobox` table.

**StackExchange.** Questions scoring at least 1 are kept, each with its
accepted answer, or else its best answer scoring at least 2. Posts are HTML:
paragraphs become text, and code blocks keep their line breaks so commands
stay one per line. On the sites that write maths between dollar signs
(Mathematics, Physics, Chemistry, Astronomy, Electronics, Biology and other
maths-enabled sites) formulas are rewritten as text; everywhere else a dollar
sign is left alone, because it is a shell variable or a price. Pleasantries
("Good luck!", "Hope this helps", "edit") are dropped, labels ("Source:",
"Hint:") join what they label, and scraps under 25 characters join the text
before them. Each answer keeps its author's name, its licence (by post date)
and a link to the question.

**Wiktionary.** The first three senses of each English entry. A sense that
only points at another word ("plural of cat", "simple past of run") is
stored with the role `form`, and the word it points at is kept in the
document's extra data, so an answer can follow the link. Forms rank like
definitions: for "ran" the useful sense is the form ("simple past of run"),
not the rare noun. The etymology is kept in the extra data too.

**WordNet.** Up to four senses per word and part of speech, each with an
example and up to four synonyms.

**Poetry.** Lines from each Gutenberg book are grouped into excerpts of 8 to
16 lines, ending at a sentence end where possible, with the book's title and
author from the Gutenberg catalogue. The corpus has no poem boundaries, so
answers always say "an excerpt from".

## How answers use packs

A question is searched in three stages, all packs and shards in parallel
under a time limit: documents titled exactly like the subject, then passages
containing all of the question's words, then passages containing any of
them. Candidates are reranked by text match, meaning (word vectors), title
match, role and pack kind. The answer is built from sentences of the best
document, chosen by what the question asks for: a date for "when", a cause
for "why", a measurement for "how tall". Every answer ends with an
attribution sentence ("From Wikipedia, "Mount Everest".") and carries
receipts with the URL and licence. Weak matches say so ("The closest thing I
know is: ..."), and below a minimum there is no answer at all rather than a
wrong one.

## The pipeline: reranker and reader (since 2026-10-22)

With the packs in place, a world question runs search -> reranker -> reader. The
word search (SQLite and Tantivy) and the meaning search (the vector index, see
above) each give their top 25; the pool, meaning list first and duplicates
dropped, goes to the reranker; the top 10 come back in its order; the reader
answers from the top 3 and cites the passage it used, or the top passage is
quoted. The rule layers do not run on this path. UNLLM_LAYERS set by hand still
takes the old path, for measuring.

Reranker files, under `~/.local/share/unllm/models/reranker/`: `rerank.json`
(names the files and the token ids), the half-precision ONNX export of
BAAI/bge-reranker-v2-m3 (`model-fp16.onnx` plus its `.data`), and its
`sentencepiece.bpe.model`. `bench/export_reranker.py` lays them out and
writes the reference `unllm_rerank check` compares the C++ tokenizer and
scores against. The reranker runs on the GPU when ONNX Runtime's CUDA
provider loads (`UNLLM_RERANK_CPU=1` keeps it on the CPU, where it takes
about 15 s a question).

Reader: the local Ollama at 127.0.0.1:11434. `UNLLM_MODEL` names the model
(default `qwen3:4b`); `off` quotes instead of reading. No network beyond
that.

Build: sentencepiece and ONNX Runtime with its CUDA provider. The packaged
sentencepiece on CachyOS (2026-10) was linked against an older abseil and
does not load, so build it from source as a static library into `~/.local`
(`-DSPM_ENABLE_SHARED=OFF -DCMAKE_INSTALL_PREFIX=$HOME/.local`); CMake
prefers that copy. The packaged `onnxruntime-cuda` 1.29 was built for cuDNN
8 and its GPU provider does not load with cuDNN 9; point the build at
another runtime with `-DUNLLM_ORT_LIBDIR=DIR`, a folder holding
`libonnxruntime.so.1` and `libonnxruntime_providers_cuda.so` (name links to
a Python onnxruntime-gpu install work).

Resident copy: a one-shot `unllm QUESTION` goes through `unllm --serve`,
started on demand, so the packs, the indexes and the reranker load once
(about 3 s a question instead of 10). It listens on
`$XDG_RUNTIME_DIR/unllm-UID.sock`, starts each request as a new
conversation, leaves after `UNLLM_IDLE` seconds idle (600), and answers only
runs with the same `UNLLM_*` settings as its own; others answer in-process,
as does `UNLLM_NO_SERVER=1`.

## Chat: follow-up questions (since 2026-10-22)

`unllm` with no question is a conversation. "tell me more", "why" and "show me
the source" act on the last answer as before. A follow-up that only makes
sense in context ("why is it so cold there" after the capital of Mongolia) is
first made to stand on its own by the reader model, from the previous question
and answer ("why is it so cold in ulaanbaatar"), and that question is the one
answered; `--branch` prints it as `[asked: ...]`. A question that stands on its
own is left as it is. One-shot runs (`unllm QUESTION`) never rewrite: each is
its own conversation.

The chat runs through the resident copy, so it starts at once; the resident
copy keeps one conversation per chat for half an hour after its last turn, and
leaves after `UNLLM_IDLE` seconds without any request (600), which ends the
conversation too. `--script FILE` answers in-process with the same chat
behaviour.

`unllm --rewrite` runs the rewrite alone for measuring: three lines on stdin
(previous question, its answer, the new question), JSON out.
`bench/rewrite50.py` scores it on QReCC turns against the set's gold rewrites.

## Keeping packs current

`unllm update --check` asks each source's server whether newer data exists
than the packs were built from, and downloads nothing: a few small requests
over IPv4. It prints one block per source, what is installed (the pack's
version and build day, the fetch day while the download folder is still
there) against what is available, marked `ok`, `NEWER` or `?` when the
server could not be reached. `--json` gives the same as one object. The exit
code is 0 when everything is current, 1 when something newer exists, 2 when a
source could not be reached.

How each source is judged:

- Wikipedia (FineWiki): the checksum Hugging Face publishes for every data
  file against the checksums the download script verified. That list is
  copied into `~/.local/share/unllm/update/` on the first check, so the
  downloads can be deleted afterwards and the comparison still works.
- StackExchange: each site dump's date, size and checksum on archive.org,
  against the file fetched (while it is here) and the fetch day.
- Wiktionary: the file's date and size on kaikki.org against the fetch day.
- WordNet: the latest release's edition on GitHub.
- Gutenberg poetry: a fixed file; it never changes.

`unllm update` does the refresh. It fetches only the sources the check marks
newer (`--full` takes them all; `--source NAME` limits it), builds their packs
under `<data>/staging/packs` with the ingest tool while the old ones stay in
use, embeds their passages with `unllm_embed` (the GPU through ONNX Runtime
when its CUDA provider loads; on battery this laptop's card is capped at 30 W
and runs about 140 passages a second, on mains about 4,000) and writes a new
copy of the index with those shards replaced: the trained part of the index
is kept, so everything untouched searches exactly as before. The candidate
set is then scored on the verification questions (`verify_tsv` in the state
file, or `UNLLM_VERIFY_TSV`) against the live set and goes live only when it
is not worse; packs and index are swapped, the resident unllm is stopped so
the next question starts on the new set, and the old set waits under
`<data>/previous` for seven days (`--prune` deletes it sooner, `--rollback`
puts it back; under 80 GB free at the start it goes as soon as the new set
passes). Raw downloads are not kept. `--dry-run` prints the plan with sizes
and passage counts and changes nothing. `--repair` re-embeds every shard
whose entries in the index disagree with its pack (`unllm_embed audit` shows
them), without fetching anything.

`unllm_embed` is the C++ behind it: `audit` (the index against the packs,
shard by shard), `encode` (a shard embedded and encoded with the index's own
centres and codebooks; measured 2026-10-26 to reproduce faiss's codes for a
shard already in the index, 99.9% the same list and 99.7% the same bytes) and
`patch` (the index rewritten with shards dropped or added).
