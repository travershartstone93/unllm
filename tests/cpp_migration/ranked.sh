#!/usr/bin/env bash
set -euo pipefail
indexer=$1 probe=$2 reference=$3
dir=$(mktemp -d)
trap 'rm -rf "$dir"' EXIT
for shard in 000 001; do
    sqlite3 "$dir/shard-$shard.sqlite" <<'SQL'
CREATE TABLE docs(doc_id INTEGER PRIMARY KEY,title TEXT);
CREATE TABLE passages(pid INTEGER PRIMARY KEY,doc_id INTEGER,role TEXT,text TEXT,section TEXT);
CREATE TABLE meta(key TEXT PRIMARY KEY,value TEXT);
SQL
done
sqlite3 "$dir/shard-000.sqlite" <<'SQL'
INSERT INTO docs VALUES(1,'Rivers'),(2,'Yellow mountain rivers'),(3,'Arctic foxes'),(4,'The café');
INSERT INTO passages VALUES(1,1,'lead','Mountains feed the river.','Geology'),(2,1,'body','Two rivers meet near the mountain.','Yellow'),(3,2,'lead','The yellow mountain has a large summit.','Geology'),(4,2,'body','River runoff.','Rivers'),(5,3,'lead','The arctic fox hunts in snow.','Habitats'),(6,4,'lead','Coffee is often served here.','Café');
SQL
sqlite3 "$dir/shard-001.sqlite" <<'SQL'
INSERT INTO docs VALUES(1,'Snow');
INSERT INTO passages VALUES(1,1,'body','Snow is white on the mountain.','Arctic');
SQL
"$indexer" "$dir"
"$probe" "$dir/tantivy" > "$dir/results"
# Preserve result order and identities. Allow single-precision rounding of
# individual BM25 scores across implementations.
awk -F'|' '
NR==FNR {expected[FNR]=$0; next}
{
  n=split(expected[FNR],ref,"[|]")
  if(n!=NF) exit 1
  for(i=1;i<=NF;i++) {
    if(i<=5) {if(ref[i]!=$i) exit 1; continue}
    split(ref[i],a,",");split($i,b,",")
    if(a[1]!=b[1] || a[2]!=b[2] || a[3]!=b[3] || (a[4]-b[4])^2>1e-10) exit 1
  }
}
END {if(FNR!=length(expected)) exit 1}
' "$reference" "$dir/results"
# A schema-1 rebuild keeps text-only ranking and the ABI's article-filter signal.
mkdir -p "$dir/packs/fixture.v1/tantivy"
mv "$dir"/shard-*.sqlite "$dir/packs/fixture.v1/"
pack="$dir/packs/fixture.v1"
echo '{"schema":[{"name":"text"}]}' > "$pack/tantivy/meta.json"
"$indexer" --migrate "$dir/packs"
[[ -z $("$indexer" --migrate "$dir/packs") ]]
grep -Fqx '{"schema":[{"name":"text"}]}' "$pack/tantivy/meta.json"
"$probe" "$pack/tantivy" > "$dir/schema1"
grep -qx 'schema 1' "$dir/schema1"
awk -F'|' '$4==2 && $5!=-1 {exit 1}' "$dir/schema1"
