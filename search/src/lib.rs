//! Full-text search over unllm's knowledge packs with Tantivy, behind a small
//! C interface so unllm's C++ can call it.
//!
//! Each pack gets one Tantivy index (in `<pack>/tantivy`) covering all its
//! SQLite shards. The index holds the passage words (English stemming) and,
//! for every passage, which shard it lives in, its passage id and its role.
//! Schema 2 also indexes the article title and the section heading (words
//! only, nothing stored), the article's doc_id, and a doc key
//! (shard << 40 | doc_id) so a search can be kept inside chosen articles.
//! A search returns (shard, pid, doc_id, score), best first; unllm reads the
//! passage text from the shard itself.
//!
//! Indexes built before schema 2 still open and search on the passage text
//! alone; the article filter needs schema 2.

use std::ffi::{c_char, CStr};
use std::path::Path;

use tantivy::collector::TopDocs;
use tantivy::query::{BooleanQuery, BoostQuery, ConstScoreQuery, Occur, Query, TermQuery, TermSetQuery};
use tantivy::schema::{Field, IndexRecordOption, Schema, FAST, INDEXED, STORED, STRING};
use tantivy::schema::{TextFieldIndexing, TextOptions};
use tantivy::tokenizer::TokenStream;
use tantivy::{Index, IndexReader, ReloadPolicy, Term};

/// Version of the C interface (us_search2, us_set_boosts, us_schema).
pub const ABI_VERSION: u32 = 2;

/// Default weight of a word found in the article title, and in the section
/// heading, against 1.0 for the passage text.
pub const TITLE_BOOST: f32 = 4.0;
pub const SECTION_BOOST: f32 = 0.5;

/// The fields of a pack index. title, section, doc_id and doc_key are None
/// in a schema 1 index.
pub struct Fields {
    pub text: Field,
    pub shard: Field,
    pub pid: Field,
    pub role: Field,
    pub title: Option<Field>,
    pub section: Option<Field>,
    pub doc_id: Option<Field>,
    pub doc_key: Option<Field>,
}

impl Fields {
    pub fn version(&self) -> u32 {
        if self.title.is_some() && self.doc_key.is_some() {
            2
        } else {
            1
        }
    }
}

/// The key the article filter matches on: one u64 per (shard, doc_id).
pub fn doc_key(shard: u64, doc_id: u64) -> u64 {
    (shard << 40) | (doc_id & ((1u64 << 40) - 1))
}

fn words_only() -> TextOptions {
    TextOptions::default().set_indexing_options(
        TextFieldIndexing::default().set_tokenizer("en_stem").set_index_option(IndexRecordOption::WithFreqs),
    )
}

/// The schema 2 fields, as the indexer writes them.
pub fn schema() -> (Schema, Fields) {
    let mut sb = Schema::builder();
    let text = sb.add_text_field("text", words_only());
    let shard = sb.add_u64_field("shard", STORED | FAST);
    let pid = sb.add_u64_field("pid", STORED | FAST | INDEXED);
    let role = sb.add_text_field("role", STRING);
    let title = sb.add_text_field("title", words_only());
    let section = sb.add_text_field("section", words_only());
    let doc_id = sb.add_u64_field("doc_id", STORED | FAST);
    let doc_key = sb.add_u64_field("doc_key", INDEXED);
    (
        sb.build(),
        Fields {
            text,
            shard,
            pid,
            role,
            title: Some(title),
            section: Some(section),
            doc_id: Some(doc_id),
            doc_key: Some(doc_key),
        },
    )
}

pub fn fields_of(index: &Index) -> Option<Fields> {
    let s = index.schema();
    Some(Fields {
        text: s.get_field("text").ok()?,
        shard: s.get_field("shard").ok()?,
        pid: s.get_field("pid").ok()?,
        role: s.get_field("role").ok()?,
        title: s.get_field("title").ok(),
        section: s.get_field("section").ok(),
        doc_id: s.get_field("doc_id").ok(),
        doc_key: s.get_field("doc_key").ok(),
    })
}

/// Function words left out of the title and section clauses: few titles
/// hold them, so their idf there is high and "what is the capital" would
/// rank "What Is Love" first. The text clause keeps them, as before.
/// Matched after stemming ("does" becomes "doe", "why" becomes "whi").
const STOPWORDS: &[&str] = &[
    "a", "about", "after", "all", "also", "am", "an", "and", "ani", "are", "as", "at", "be", "becaus", "been",
    "befor", "between", "but", "by", "can", "could", "did", "do", "doe", "dure", "each", "for", "from", "had",
    "has", "have", "he", "her", "him", "his", "how", "i", "if", "in", "into", "is", "it", "me", "more", "most",
    "my", "no", "not", "of", "on", "or", "other", "our", "s", "she", "should", "so", "some", "such", "than",
    "that", "the", "their", "them", "then", "there", "these", "they", "this", "those", "to", "under", "up", "us",
    "veri", "was", "we", "were", "what", "when", "where", "whi", "which", "while", "who", "whom", "whose", "will",
    "with", "would", "you", "your",
];

fn is_stopword(t: &str) -> bool {
    STOPWORDS.binary_search(&t).is_ok()
}

/// One search hit.
#[derive(Clone, Copy, Debug)]
pub struct Hit {
    pub shard: u32,
    pub pid: i64,
    pub doc_id: i64, // 0 in a schema 1 index
    pub score: f32,
}

/// An open pack index, ready to search.
pub struct Searcher {
    index: Index,
    reader: IndexReader,
    fields: Fields,
    title_boost: f32,
    section_boost: f32,
}

impl Searcher {
    pub fn open(path: &Path) -> Option<Searcher> {
        let index = Index::open_in_dir(path).ok()?;
        let fields = fields_of(&index)?;
        let reader = index.reader_builder().reload_policy(ReloadPolicy::Manual).try_into().ok()?;
        Some(Searcher { index, reader, fields, title_boost: TITLE_BOOST, section_boost: SECTION_BOOST })
    }

    pub fn version(&self) -> u32 {
        self.fields.version()
    }

    /// Weights for title and section words; 0 leaves that field out.
    pub fn set_boosts(&mut self, title: f32, section: f32) {
        self.title_boost = title.max(0.0);
        self.section_boost = section.max(0.0);
    }

    /// Passages holding any of the words in their text, title or section,
    /// ranked by bm25; only those with the given role when one is named;
    /// only inside the given (shard, doc_id) articles when docs is not None.
    /// None when an article filter is asked of a schema 1 index.
    pub fn search_in(&self, words: &str, role: Option<&str>, docs: Option<&[(u32, i64)]>, k: usize) -> Option<Vec<Hit>> {
        if docs.is_some() && self.fields.doc_key.is_none() {
            return None;
        }
        if k == 0 {
            return Some(Vec::new());
        }
        let Ok(mut tokenizer) = self.index.tokenizer_for_field(self.fields.text) else {
            return Some(Vec::new());
        };
        let mut terms: Vec<(Occur, Box<dyn Query>)> = Vec::new();
        let mut seen = std::collections::HashSet::new();
        // Measuring: a search restricted to given articles may use its own title weight and
        // minimum match (UNLLM_INNER_TITLE_BOOST, UNLLM_INNER_MIN_MATCH).
        let inner = docs.is_some();
        let env = |k: &str| std::env::var(k).ok();
        let title_boost = if inner { env("UNLLM_INNER_TITLE_BOOST").and_then(|v| v.parse().ok()).unwrap_or(self.title_boost) } else { self.title_boost };
        let mut stream = tokenizer.token_stream(words);
        let term_query = |field: Field, t: &str| -> Box<dyn Query> {
            Box::new(TermQuery::new(Term::from_field_text(field, t), IndexRecordOption::WithFreqs))
        };
        // One clause per question word: its text match plus, for non-stopwords, its title and
        // section matches. The sum of the matching parts is the same score as before; grouping
        // them lets the minimum match count words, not fields.
        while stream.advance() {
            let t = stream.token().text.clone();
            if !seen.insert(t.clone()) {
                continue;
            }
            let mut parts: Vec<(Occur, Box<dyn Query>)> = vec![(Occur::Should, term_query(self.fields.text, &t))];
            if !is_stopword(&t) {
                for (field, boost) in [(self.fields.title, title_boost), (self.fields.section, self.section_boost)] {
                    if let Some(f) = field {
                        if boost > 0.0 {
                            parts.push((Occur::Should, Box::new(BoostQuery::new(term_query(f, &t), boost))));
                        }
                    }
                }
            }
            let word: Box<dyn Query> = if parts.len() == 1 { parts.pop().unwrap().1 } else { Box::new(BooleanQuery::new(parts)) };
            terms.push((Occur::Should, word));
        }
        if terms.is_empty() {
            return Some(Vec::new());
        }
        // UNLLM_MIN_MATCH: unset = all but one word (default since 2026-10-19); 0 = any one word;
        // "n-1" = all but one word;
        // a fraction (0.5) = that share of the words, rounded up.
        let n = terms.len();
        let min_var = if inner && env("UNLLM_INNER_MIN_MATCH").is_some() { "UNLLM_INNER_MIN_MATCH" } else { "UNLLM_MIN_MATCH" };
        let min = match std::env::var(min_var).ok().as_deref() {
            Some("n-1") => n.saturating_sub(1).max(1),
            Some(v) => v.parse::<f64>().map(|f| ((f * n as f64).ceil() as usize).min(n)).unwrap_or(0),
            None => n.saturating_sub(1).max(1),
        };
        let any: Box<dyn Query> = if min > 1 {
            Box::new(BooleanQuery::with_minimum_required_clauses(terms, min))
        } else {
            Box::new(BooleanQuery::new(terms))
        };
        let mut must: Vec<(Occur, Box<dyn Query>)> = Vec::new();
        if let Some(r) = role {
            let q = TermQuery::new(Term::from_field_text(self.fields.role, r), IndexRecordOption::Basic);
            must.push((Occur::Must, Box::new(q)));
        }
        if let (Some(list), Some(key)) = (docs, self.fields.doc_key) {
            if list.is_empty() {
                return Some(Vec::new());
            }
            let keys = list.iter().map(|&(s, d)| Term::from_field_u64(key, doc_key(s as u64, d as u64)));
            must.push((Occur::Must, Box::new(ConstScoreQuery::new(Box::new(TermSetQuery::new(keys)), 0.0))));
        }
        let query: Box<dyn Query> = if must.is_empty() {
            any
        } else {
            must.push((Occur::Must, any));
            Box::new(BooleanQuery::new(must))
        };
        let searcher = self.reader.searcher();
        let Ok(hits) = searcher.search(&query, &TopDocs::with_limit(k).order_by_score()) else {
            return Some(Vec::new());
        };
        // Read shard, pid and doc_id from the fast fields (column per
        // segment), not the doc store: no block decompression per hit.
        let schema = self.index.schema();
        let name = |f: Field| schema.get_field_name(f).to_string();
        let mut out = Vec::with_capacity(hits.len());
        let mut cols: std::collections::HashMap<u32, _> = std::collections::HashMap::new();
        for (score, addr) in hits {
            let c = cols.entry(addr.segment_ord).or_insert_with(|| {
                let ff = searcher.segment_reader(addr.segment_ord).fast_fields();
                (
                    ff.u64(&name(self.fields.shard)).ok(),
                    ff.u64(&name(self.fields.pid)).ok(),
                    self.fields.doc_id.and_then(|f| ff.u64(&name(f)).ok()),
                )
            });
            let get = |col: &Option<tantivy::columnar::Column<u64>>| {
                col.as_ref().and_then(|c| c.first(addr.doc_id)).unwrap_or(0)
            };
            out.push(Hit { shard: get(&c.0) as u32, pid: get(&c.1) as i64, doc_id: get(&c.2) as i64, score });
        }
        Some(out)
    }

    /// The schema 1 call: any role or one, no article filter.
    pub fn search(&self, words: &str, role: Option<&str>, k: usize) -> Vec<(u32, i64, f32)> {
        self.search_in(words, role, None, k)
            .unwrap_or_default()
            .into_iter()
            .map(|h| (h.shard, h.pid, h.score))
            .collect()
    }
}

fn opt_str(p: *const c_char) -> Option<String> {
    if p.is_null() {
        None
    } else {
        Some(unsafe { CStr::from_ptr(p) }.to_string_lossy().into_owned())
    }
}

/// The C interface version: 2 when us_search2, us_set_boosts and us_schema exist.
#[no_mangle]
pub extern "C" fn us_version() -> u32 {
    ABI_VERSION
}

/// Opens the index at path; null when there is none.
#[no_mangle]
pub extern "C" fn us_open(path: *const c_char) -> *mut Searcher {
    let Some(p) = opt_str(path) else { return std::ptr::null_mut() };
    match Searcher::open(Path::new(&p)) {
        Some(s) => Box::into_raw(Box::new(s)),
        None => std::ptr::null_mut(),
    }
}

/// Schema of an open index: 1 (passage text only) or 2 (text, title,
/// section, doc_id); 0 for null.
#[no_mangle]
pub extern "C" fn us_schema(s: *const Searcher) -> u32 {
    if s.is_null() {
        0
    } else {
        unsafe { &*s }.version()
    }
}

/// Sets the title and section weights (text is 1.0). Call it before
/// searching from several threads, not while they search.
#[no_mangle]
pub extern "C" fn us_set_boosts(s: *mut Searcher, title: f32, section: f32) {
    if !s.is_null() {
        unsafe { &mut *s }.set_boosts(title, section);
    }
}

/// Searches; writes up to k results into the three arrays and returns how
/// many it wrote. role may be null for any role.
#[no_mangle]
pub extern "C" fn us_search(
    s: *const Searcher,
    words: *const c_char,
    role: *const c_char,
    k: u32,
    shards: *mut u32,
    pids: *mut i64,
    scores: *mut f32,
) -> i32 {
    us_search2(s, words, role, std::ptr::null(), std::ptr::null(), 0, k, shards, pids, std::ptr::null_mut(), scores)
}

/// Searches, optionally only inside the articles given as n_docs pairs
/// (doc_shards[i], doc_ids[i]); doc_shards null means no article filter.
/// Writes up to k results; out_doc_ids may be null. Returns how many it
/// wrote, or -1 when an article filter is asked of a schema 1 index.
#[no_mangle]
pub extern "C" fn us_search2(
    s: *const Searcher,
    words: *const c_char,
    role: *const c_char,
    doc_shards: *const u32,
    doc_ids: *const i64,
    n_docs: u32,
    k: u32,
    shards: *mut u32,
    pids: *mut i64,
    out_doc_ids: *mut i64,
    scores: *mut f32,
) -> i32 {
    if s.is_null() || words.is_null() || shards.is_null() || pids.is_null() || scores.is_null() {
        return 0;
    }
    let s = unsafe { &*s };
    let w = opt_str(words).unwrap_or_default();
    let r = opt_str(role);
    let docs: Option<Vec<(u32, i64)>> = if doc_shards.is_null() {
        None
    } else if n_docs > 0 && doc_ids.is_null() {
        return 0;
    } else {
        Some((0..n_docs as usize).map(|i| unsafe { (*doc_shards.add(i), *doc_ids.add(i)) }).collect())
    };
    let Some(hits) = s.search_in(&w, r.as_deref(), docs.as_deref(), k as usize) else { return -1 };
    for (i, h) in hits.iter().enumerate() {
        unsafe {
            *shards.add(i) = h.shard;
            *pids.add(i) = h.pid;
            *scores.add(i) = h.score;
            if !out_doc_ids.is_null() {
                *out_doc_ids.add(i) = h.doc_id;
            }
        }
    }
    hits.len() as i32
}

/// Frees an index opened with us_open.
#[no_mangle]
pub extern "C" fn us_close(s: *mut Searcher) {
    if !s.is_null() {
        unsafe { drop(Box::from_raw(s)) };
    }
}
