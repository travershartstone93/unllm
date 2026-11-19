def median: sort | length as $n | if $n%2==1 then .[$n/2|floor] else (.[$n/2-1]+.[$n/2])/2 end;
def grade:
  . as $row
  | [.gold | to_entries[] |
       .key as $id | .value as $gold | $row.result.bindings[$id] as $got |
       if $row.rc!=0 then "error"
       elif $gold==$got then "correct"
       elif $got==null then "unfilled_despite_match"
       else "wrong_placement" end] as $grades
  | . + {grades:$grades,exact:(.rc==0 and .gold==.result.bindings),
         partial:([.gold[]]|any(.==null) and any(.!=null))};
map(grade) | group_by(.engine) | map(
  . as $runs | [.[].grades[]] as $roles |
  {engine:.[0].engine,requests:length,exact:map(select(.exact))|length,
   partial_requests:map(select(.partial))|length,partial_exact:map(select(.partial and .exact))|length,
   role_assignments:($roles|length),correct_roles:($roles|map(select(.=="correct"))|length),
   wrong_placements:($roles|map(select(.=="wrong_placement"))|length),
   unfilled_despite_match:($roles|map(select(.=="unfilled_despite_match"))|length),
   invalid_runs:map(select(.rc!=0))|length,
   requests_changed_by_order:(group_by(.case)|map(select(length==2 and .[0].rc==0 and .[1].rc==0 and .[0].result.bindings!=.[1].result.bindings))|length),
   median_ms:map(.ms)|median,
   input_tokens:map(.result.input_tokens // 0)|add,
   output_tokens:map(.result.output_tokens // 0)|add})
