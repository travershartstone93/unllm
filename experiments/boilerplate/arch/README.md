# Arch whole-program discovery pilot

After Big Pickle interprets a file-processing request, the [integrated runner](../integrated/README.md) searches the local package catalog for the whole task. A verified hit produces a reusable C++17 CLI, its executable and a dependency manifest. The CLI invokes an installed package at runtime. The existing brick pipeline takes over when discovery, provenance or behavior checks yield no usable program.

## Catalog

Run from the upgrade checkout on an Arch-compatible system with the official sync databases, expac, curl, jq, gzip, a C++17 compiler and Xapian installed. Set the destination on the mounted corpus drive. The index builder expects a new directory and can also take a previously downloaded AUR metadata gzip as its second argument.

```sh
export CORPUS_ROOT=/run/media/whisper/UNLLM_CORPUS/unllm
mkdir -p "$CORPUS_ROOT/arch-programs"
experiments/boilerplate/arch/build-catalog.sh "$CORPUS_ROOT/arch-programs/catalog"
```

The builder uses the local core, extra and multilib sync metadata and the AUR's bulk metadata export. It indexes names and descriptions with Xapian BM25, adding invocation descriptions from [three reviewed profiles](profiles.json) to matching official packages. A refresh can use another new directory. Set `ARCH_PROGRAM_CATALOG` to its absolute path to select it, or to `off` to reproduce the brick baseline. Counts and input digests accompany each snapshot.

## Selection and execution

Search returns up to eight candidates from each source. AUR hits are saved as review leads. Official hits need a reviewed invocation compatible with the entire interpreted request. This pilot supports a single output file. Each run keeps its candidates, dispositions, preparation diagnostics and test results. A package search miss or a declined candidate means no verified hit in this pilot, and leaves broader coverage open.

When a reviewed official package satisfies the full request and is missing, an interactive run asks `install package img2pdf from repo extra or build from scratch?` with the actual package and repository names. Type `install` to start pacman’s normal privileged transaction for the catalog version, or `build` to continue through the existing brick pipeline. Enter also selects build. Pacman uses the official repositories for dependencies, shows its normal transaction confirmation, and retains signature checks. After installation the runner verifies the package and tests the resulting program. An unattended run skips missing packages and can still reuse an already installed candidate. Set `ARCH_INSTALL_TERMINAL=alacritty` to present the choice and any pacman interaction in a separate desktop terminal, including when the runner itself has no terminal. Closing the window selects the brick fallback. AUR search results remain review leads.

Reuse requires an installed executable, its pacman-validated package, the matching version and SHA-256 from the official sync database, and the package archive in the local pacman cache and its signature from the cache or sync database. The runner checks the signature with the Arch signing keys, checks the local disabled-key list, and compares installed executable bytes with the archive. Missing evidence sends the request back to brick selection.

The generated launcher calls the reviewed command with fixed arguments inside Bubblewrap, without network access, with read-only system files and input and a private writable output directory. It publishes a fresh file after the command succeeds. The runner accepts the project only after the supplied independent evaluator passes. The generated `project/dependency.json` identifies the external tool, version and digests used. Provenance verification happens when producing the project. Deployment needs the declared runtime, Bubblewrap and a filesystem supporting hard links.

## Reproduce the checks

Use [the integrated runner setup](../integrated/README.md) for the original JPG and PDF requests. For the official-package positive control, run with the verified libjpeg-turbo package and its signed archive available locally:

```sh
printf '%s\n' 'Build a reusable C++ command-line program that converts a JPEG image to a binary PPM (portable pixmap) file.' > /tmp/unllm-ppm.request
experiments/boilerplate/integrated/run.sh /tmp/unllm-ppm.request /tmp/unllm-ppm-run experiments/boilerplate/arch/ppm-check.sh
experiments/boilerplate/arch/test-boundaries.sh /tmp/unllm-ppm-run
```

The boundary controls use the native picker environment for the fallback check. They cover an AUR name collision, a partial request, an unreviewed official match, altered provenance/arguments, rejected behavior, and a malformed evaluator. [B22 results and limits](../../../docs/EXPERIMENT_LOG.md#b22-arch-whole-program-discovery)

Run `test-install-choice.sh` with the same picker environment and the saved B21/B22 fixtures to exercise both prompt choices, the default, a failed transaction and unattended execution. The installer is simulated and the package transaction is checked in print-only mode.
