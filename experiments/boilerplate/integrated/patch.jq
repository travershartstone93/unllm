# Validate the whole edit set against the original lines before writing any files.
. as $answer | $files[0] as $sources |
if (.edits|type)!="array" or (.add_packages|type)!="array" or
   ((.edits|length)+(.add_packages|length)==0) or
   any(.add_packages[]; type!="string" or (test("^[A-Za-z0-9][A-Za-z0-9._+-]*$")|not))
then error("invalid patch or packages") else . end |
reduce (.edits | group_by(.file)[]) as $group
  ($sources; $group[0].file as $f |
   if (has($f)|not) then error("file outside offered context") else . end |
   .[$f] |= (reduce ($group | sort_by(.start) | reverse[]) as $e
     ({lines:., ceiling:(length+1)};
      if ([$e.start,$e.end]|all(type=="number" and floor==.)) and
         $e.start>=1 and $e.end>=$e.start and $e.end<.ceiling and
         ($e.lines|type)=="array" and all($e.lines[];type=="string" and (test("[\r\n]")|not))
      then .lines = (.lines[:($e.start-1)] + $e.lines + .lines[$e.end:]) |
           .ceiling=$e.start
      else error("invalid edit range") end) | .lines))
