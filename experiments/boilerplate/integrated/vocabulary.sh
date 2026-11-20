#!/bin/bash
# FTS5 ranks the extra public outcome descriptions locally. Brick IDs and private bindings stay on disk.
set -euo pipefail
schema=$1; request=$2
if ! jq -e 'has("base_outcomes")' "$schema" >/dev/null; then
    jq --rawfile request "$request" -c '{profile:(if ([$request|ascii_downcase|scan("[a-z0-9]+")]|any(.=="hpp" or .=="hh" or .=="class" or .=="library")) then "sequence" else "single" end),formats:([.providers[]|.from,.to|select(startswith("in:") or startswith("out:"))|sub("^[^:]+:";"")]|unique),outcomes}' "$schema"
    exit
fi
# Keep filenames and arbitrary request text out of SQL syntax.
jq -nr --slurpfile schema "$schema" --rawfile request "$request" '
 def sql: "\u0027"+gsub("\u0027";"\u0027\u0027")+"\u0027";
 "CREATE VIRTUAL TABLE words USING fts5(name,meaning,tokenize=\"porter unicode61\");",
 ($schema[0].outcomes|to_entries[]|"INSERT INTO words VALUES("+(.key|sql)+","+((.key|gsub("_";" "))+" "+.value.meaning|sql)+");"),
 "CREATE VIRTUAL TABLE prompt USING fts5(body,tokenize=\"unicode61\");",
 ("INSERT INTO prompt VALUES("+($request|sql)+");"),"CREATE VIRTUAL TABLE terms USING fts5vocab(prompt,row);",
 "SELECT name FROM words WHERE words MATCH (SELECT coalesce(group_concat(term,\u0027 OR \u0027),\u0027\u0022\u0022\u0027) FROM (SELECT char(34)||term||char(34) AS term FROM terms WHERE length(term)>1)) ORDER BY bm25(words,4,1),name LIMIT 10;"
' | sqlite3 -batch -bail :memory: | jq -Rsc --slurpfile base "$(dirname "$0")/../component-trial/providers/contracts.json" --rawfile request "$request" --slurpfile schema "$schema" '
 (split("\n")|map(select(length>0))) as $hits | $schema[0] |
 # Include nearby operations from the best matching implementation family so
 # a later step in a requested sequence can use a differently worded operation.
 (.providers|map(select(.effect==$hits[0] and .effect!=null))|first|.header) as $family |
 ([.providers[]|select(.header==$family and .effect!=null)|.effect]|unique) as $related |
 . as $s | ([$request|ascii_downcase|scan("[a-z0-9]+")] | unique) as $words |
 ([$request|ascii_downcase|scan("[a-z0-9]+")|. as $word|$s.format_terms[$word]|select(.!=null)]|first) as $format |
 ($s.entry_outcomes[$format // ""] // []) as $entry |
 ([$hits[]|select((split("_")-["lines","names","items","order","file","text","archive","database"]) as $tokens |
           any($tokens[]; . as $token | $words|index($token)!=null))]) as $explicit |
 ((($entry|length)>0) and (($entry-.base_outcomes)|length)==0 and
   ([($hits[:3] - .base_outcomes)[] | . as $k | select($explicit|index($k)!=null)] |length)==0 and
   ([$words[]|select(.=="hpp" or .=="hh" or .=="hxx" or .=="library" or .=="class")]|length)==0) as $compact |
 (if $compact then .base_outcomes elif ($entry|length)>0 then ($entry+$explicit|unique)
  else (.base_outcomes+$hits+$related|unique) end) as $selected |
 {profile:(if $compact then "single" else "sequence" end),
  formats:(if $compact then $base[0].providers else .providers end |
           [.[]|.from,.to|select(startswith("in:") or startswith("out:"))|sub("^[^:]+:";"")]|unique),
  outcomes:(.outcomes|with_entries(select(.key as $k|$selected|index($k))))}'
