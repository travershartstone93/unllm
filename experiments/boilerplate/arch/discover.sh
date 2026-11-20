#!/bin/bash
set -euo pipefail
here=$(cd -- "$(dirname -- "$0")" && pwd)
catalog=$1
spec=$2
query=$(jq -er '[.input_type] + [.roles[] | .text, .output_type] | join(" ") | ascii_downcase' "$spec")
"$catalog/catalog" search "$catalog/index" "$query" |
    jq -Rn --slurpfile profiles "$here/profiles.json" --slurpfile spec "$spec" '
    [inputs | split("\t") as [$repo,$name,$version,$description] |
      {repo:$repo,package:$name,version:$version,description:$description} |
      .profile = (if $repo == "aur" then null else (first($profiles[0][] |
        select(.package==$name and .input==$spec[0].input_type and
        ($spec[0].roles|length)==1 and (.output==$spec[0].roles[0].output_type) and
        (.content==$spec[0].roles[0].content) and ($spec[0].roles[0].multiple==false))) // null) end) |
      .disposition = (if $repo=="aur" then "review_only"
        elif .profile==null then "no_reviewed_full_invocation" else "check_provenance" end)]'
