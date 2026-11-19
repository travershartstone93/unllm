def compatible($r; $input):
  . != null and .input_type == $input and .output_type == $r.output_type
  and .content == $r.content and .multiple == $r.multiple;
. as $spec |
[.roles[] as $r |
  $selection[0].bindings[$r.id] as $id |
  ($adapters[0][$id // ""] // null) as $a |
  {role:$r.id, proposed:$proposal[0].bindings[$r.id], selected:$id,
   adapter:(if ($a | compatible($r; $spec.input_type)) then $a else null end)}
] as $rows |
($rows | map(select(.adapter != null) | .adapter.input) | unique) as $inputs |
if ($inputs|length)>1 then error("incompatible input ports") else . end |
reduce range(0; $rows|length) as $i
  ({rows:[], args:[$inputs[0] // "input_arg"], goals:[]};
    $rows[$i] as $r |
    ($r.adapter.output // "output_\($i)") as $port |
    ($r.adapter != null and (.args | index($port)) == null) as $bound |
    .rows += [$r + {bound:$bound}] |
    .args += [if $bound then $port else "output_\($i)" end] |
    .goals += [if $bound then $r.adapter.goal else "missing_\($i)" end])
