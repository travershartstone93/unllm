//! unllm_index: build the Tantivy index of a knowledge pack.
//!
//!   unllm_index PACK_DIR [--threads N] [--heap-mb M]
//!
//! Reads every shard-NNN.sqlite in the pack and writes PACK_DIR/tantivy
//! (built in PACK_DIR/tantivy.building and swapped in at the end, so a
//! search never sees a half-built index). Pauses whenever the CPU passes
//! 85 C, like the pack builder.

use std::fs;
use std::path::{Path, PathBuf};
use std::time::{Duration, Instant};

use rusqlite::{Connection, OpenFlags};
use tantivy::{doc, Index};
use unllm_search::{doc_key, schema};

fn cpu_temp() -> Option<f64> {
    let mut best: Option<f64> = None;
    for entry in fs::read_dir("/sys/class/hwmon").ok()?.flatten() {
        let dir = entry.path();
        let name = fs::read_to_string(dir.join("name")).unwrap_or_default();
        if !matches!(name.trim(), "coretemp" | "k10temp" | "zenpower") {
            continue;
        }
        for f in fs::read_dir(&dir).ok()?.flatten() {
            let n = f.file_name().to_string_lossy().into_owned();
            if n.starts_with("temp") && n.ends_with("_input") {
                if let Ok(v) = fs::read_to_string(f.path()).unwrap_or_default().trim().parse::<f64>() {
                    best = Some(best.map_or(v / 1000.0, |b: f64| b.max(v / 1000.0)));
                }
            }
        }
    }
    best
}

fn cool_down() {
    while let Some(t) = cpu_temp() {
        if t <= 85.0 {
            break;
        }
        eprintln!("  cpu at {t:.0} C, pausing 30 s");
        std::thread::sleep(Duration::from_secs(30));
    }
}

fn shards(pack: &Path) -> Vec<(u64, PathBuf)> {
    let mut out = Vec::new();
    if let Ok(rd) = fs::read_dir(pack) {
        for e in rd.flatten() {
            let n = e.file_name().to_string_lossy().into_owned();
            if let Some(num) = n.strip_prefix("shard-").and_then(|s| s.strip_suffix(".sqlite")) {
                if let Ok(i) = num.parse::<u64>() {
                    out.push((i, e.path()));
                }
            }
        }
    }
    out.sort();
    out
}

/// Writes ~/.local/share/unllm/progress/index-<pack>.json for the progress viewer.
fn report(pack: &Path, started: f64, shard: u64, shards: usize, done: u64, total: u64, phase: &str) {
    let Ok(home) = std::env::var("HOME") else { return };
    let dir = PathBuf::from(home).join(".local/share/unllm/progress");
    let _ = fs::create_dir_all(&dir);
    let name = pack.file_name().map(|n| n.to_string_lossy().into_owned()).unwrap_or_default();
    let body = format!(
        "{{\"pid\": {}, \"pack\": \"{}\", \"started\": {:.0}, \"shard\": {}, \"shards\": {}, \"done\": {}, \"total\": {}, \"phase\": \"{}\"}}\n",
        std::process::id(), name, started, shard, shards, done, total, phase
    );
    let tmp = dir.join(format!("index-{name}.json.tmp"));
    if fs::write(&tmp, body).is_ok() {
        let _ = fs::rename(&tmp, dir.join(format!("index-{name}.json")));
    }
}

fn now_secs() -> f64 {
    std::time::SystemTime::now().duration_since(std::time::UNIX_EPOCH).map(|d| d.as_secs_f64()).unwrap_or(0.0)
}

fn main() {
    let args: Vec<String> = std::env::args().collect();
    if args.len() < 2 || args[1] == "--help" {
        eprintln!("usage: unllm_index PACK_DIR [--threads N] [--heap-mb M]");
        std::process::exit(2);
    }
    let pack = PathBuf::from(&args[1]);
    let mut threads = 2usize;
    let mut heap_mb = 1000usize;
    let mut i = 2;
    while i + 1 < args.len() {
        match args[i].as_str() {
            "--threads" => threads = args[i + 1].parse().unwrap_or(2),
            "--heap-mb" => heap_mb = args[i + 1].parse().unwrap_or(1000),
            _ => {}
        }
        i += 2;
    }
    let parts = shards(&pack);
    if parts.is_empty() {
        eprintln!("unllm_index: no shards in {}", pack.display());
        std::process::exit(1);
    }
    let building = pack.join("tantivy.building");
    let _ = fs::remove_dir_all(&building);
    fs::create_dir_all(&building).expect("create index folder");
    let (schema, f) = schema();
    let index = Index::create_in_dir(&building, schema).expect("create index");
    let mut writer = index.writer_with_num_threads(threads, heap_mb * 1_000_000).expect("index writer");
    let start = Instant::now();
    let started = now_secs();
    let expected: u64 = parts
        .iter()
        .filter_map(|(_, p)| {
            let db = Connection::open_with_flags(p, OpenFlags::SQLITE_OPEN_READ_ONLY).ok()?;
            db.query_row("SELECT value FROM meta WHERE key = 'passages'", [], |r| r.get::<_, String>(0))
                .ok()?
                .parse::<u64>()
                .ok()
        })
        .sum();
    let mut total = 0u64;
    for (num, path) in &parts {
        cool_down();
        let db = Connection::open_with_flags(path, OpenFlags::SQLITE_OPEN_READ_ONLY).expect("open shard");
        let mut st = db
            .prepare(
                "SELECT p.pid, p.role, p.text, p.doc_id, COALESCE(d.title, ''), COALESCE(p.section, '') \
                 FROM passages p LEFT JOIN docs d ON d.doc_id = p.doc_id",
            )
            .expect("read passages");
        let rows = st
            .query_map([], |r| {
                Ok((
                    r.get::<_, i64>(0)?,
                    r.get::<_, String>(1)?,
                    r.get::<_, String>(2)?,
                    r.get::<_, i64>(3)?,
                    r.get::<_, String>(4)?,
                    r.get::<_, String>(5)?,
                ))
            })
            .expect("query passages");
        let (title_f, section_f, doc_id_f, doc_key_f) =
            (f.title.unwrap(), f.section.unwrap(), f.doc_id.unwrap(), f.doc_key.unwrap());
        let mut n = 0u64;
        for row in rows.flatten() {
            let (pid, role, text, doc_id, title, section) = row;
            writer
                .add_document(doc!(
                    f.text => text,
                    f.shard => *num,
                    f.pid => pid as u64,
                    f.role => role,
                    title_f => title,
                    section_f => section,
                    doc_id_f => doc_id as u64,
                    doc_key_f => doc_key(*num, doc_id as u64)
                ))
                .expect("add passage");
            n += 1;
            if n % 100_000 == 0 {
                report(&pack, started, *num, parts.len(), total + n, expected, "reading");
            }
            if n % 1_000_000 == 0 {
                cool_down();
            }
        }
        total += n;
        eprintln!("  shard {num}: {n} passages ({total} so far, {:.0} s)", start.elapsed().as_secs_f64());
    }
    report(&pack, started, 0, parts.len(), total, expected.max(total), "merging");
    writer.commit().expect("commit");
    writer.wait_merging_threads().expect("merge");
    let final_dir = pack.join("tantivy");
    let _ = fs::remove_dir_all(&final_dir);
    fs::rename(&building, &final_dir).expect("install index");
    report(&pack, started, 0, parts.len(), total, total, "done");
    eprintln!("{}: {total} passages indexed in {:.0} s", pack.display(), start.elapsed().as_secs_f64());
}
