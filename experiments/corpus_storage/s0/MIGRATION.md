# Timetec corpus migration, 2026-11-18

The corpus on this host occupies 69,874,961,233 bytes across 587 files. It includes 28 packs, 42 SQLite shards, the existing Tantivy indexes and three vector files. The machine-readable [inventory](corpus-inventory.json) includes versions, per-pack sizes, sources, shard manifests, licenses and installed tool versions. The [SHA-256 manifest](corpus.sha256) uses paths relative to the data root.

## Storage and commands

The Timetec 35TTFP6PCIE- reports serial DD56419883CA2 and 256,060,514,304 bytes through a JMicron enclosure at 10 Gbit/s. The GPT disk has one ext4 partition labeled UNLLM_CORPUS with UUID `1f243ff1-6e8f-4562-af7c-f6edf0bf3300`. The desktop mounted it under `/run/media/whisper/UNLLM_CORPUS`. The launchers locate the mounted UUID and check a marker before starting a process. The drive must be mounted at login or after reconnection. The account could not query SMART health through the bridge.

| Shell command or directory | Purpose |
| --- | --- |
| `unllm` | Existing executable in the original checkout, now launched with the external packs and vectors |
| `unllm-upgrade` | Native upgrade build, including its normal symbolic engine |
| `unllm_ingest_native`, `unllm_update_native` | Upgrade tools with the same external data, pack and source roots |
| `unllm/packs`, `unllm/vectors` on the USB volume | Installed corpus and search indexes |
| `unllm/sources`, `unllm/staging` on the USB volume | New downloads and pack build workspace |
| `~/.local/share/unllm` | Default data root, linked to the mounted USB volume through a UUID-specific alias |
| `~/Backups/unllm-internal-corpus` | Detached internal corpus archive |
| `~/.local/share/unllm-models` | Internal model assets used by the external data root |
| `~/Backups/unllm-preformat-DD56419883CA2` | File-level tar archives of both Bazzite installer volumes and their SHA-256 sums |

The shell configuration lives in `~/.local/libexec/unllm-corpus` and `~/.config/unllm-upgrade/corpus.uuid`. Direct binaries using the default data directory also resolve to the external corpus. The archive requires explicit selection for recovery. The default source-download directory `~/Downloads/unllm-knowledge` links to the USB sources directory. The separate `~/Downloads/unllm-corpus-sources` tree contains development materials and is retained internally. The installer archives preserve files, not the former bootable partition layout.

## Validation

The migration copied files and flushed the filesystem before a real unmount and remount. The launcher returned exit 69 for a query while the USB was unmounted. The temporary ingestion trial on the USB interrupted an import before publication, retried it, queried all 300 documents and removed the fixture pack.

The [retrieval record](retrieval-baseline.json) contains raw answers, sources, passages, timings and diagnostic output for two Wikipedia and two Stack Exchange questions. All 12 fresh or resident USB responses matched the corresponding internal responses after removing elapsed-time text. Both runs reported the existing ranked-index migration warning. These trials used the upgrade's isolated single-pack fallback retrieval without embedding or generation models. Fresh means a new process with ordinary OS caching. The first USB trial overlapped checksum verification, so its timings are unsuitable for estimating a hardware slowdown. A later physical disconnect interrupted a write. After reconnect, ext4 journal recovery and the offline filesystem check passed. All 587 checksums matched again and the upgrade returned a cited plate tectonics answer. Reboot behavior remains untested.

After remount, the SHA-256 manifests for the internal and external corpus matched byte for byte. Their digest is `d7d7c371c9877f4a2e261037247049fac01894ee18beb65db762ec1a84f6b2c3`. SQLite returned `ok` for full integrity checks on the Wikipedia Simple, Cooking Stack Exchange and WordNet shards. The switched ingestion command opened all 28 external manifests. A normal upgrade query for photosynthesis returned the Wikipedia excerpt with its source URL. The filesystem reported about 157 GiB available to the user after migration.
