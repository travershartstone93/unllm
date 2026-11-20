# Resolve generated slots before running the existing range-patch validator.
# Render the complete generated header in assembly order, independent of reply order.
def source_lines:
  type == "array" and all(.[]; type == "string" and (test("[\r\n]") | not));

if length != 1 then error("expected one edit response") else .[0] end |
if type != "object" or (.edits | type) != "array" then error("expected an edits array") else . end |
($gaps[0]) as $required | ($ARGS.named.typed // {}) as $typed |
if ($typed|length)>0 and all($required[]; . as $n | $typed|has($n)) and (.edits|length)>0 then error("typed repair must preserve planned wiring") else . end |
# Typed support definitions can span lines. Normalize those strings to the same
# source-line representation before the ordinary patch validator sees them.
if (.functions|type)=="array" then .functions |= map(
  if (.name|type)=="string" and (.name as $n | $typed|has($n)) and (.support|type)=="array" then
    .support |= [ .[] | if type=="string" then split("\n")[] else . end ]
  else . end) else . end |
(if has("functions") then .functions else [] end) as $functions |
if ($functions | type) != "array" or
   any($functions[]; type != "object" or (.name | type) != "string" or
       (.support | source_lines | not) or (.body | source_lines | not) or (.body|length)==0)
then error("invalid function implementation") else . end |
if ($functions | map(.name) | sort) != ($required | sort)
then error("expected exactly one implementation per missing function") else . end |
if any(.edits[]; .file == "bricks/gaps.hpp")
then error("use named implementations for the generated gap header") else . end |
if ($required | length) > 0 then
  ($functions | INDEX(.name)) as $by_name |
  ($files[0]["bricks/gaps.hpp"] | length) as $original_length |
  .edits += [{
    file:"bricks/gaps.hpp", start:1, end:$original_length,
    lines:(["#pragma once", "#include <stdexcept>"] + (if ($typed|length)>0 then ["#include \"provider_types.hpp\""] else [] end) + [
      $required[] as $name | $by_name[$name] |
      .support[], (($typed[$name].signature // "inline void \($name)(const char* input_path, const char* output_path)") + " {"), .body[], "}"
    ])
  }]
else . end | del(.functions)
