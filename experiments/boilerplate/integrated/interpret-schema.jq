# Build the decoding grammar from the same candidate vocabulary shown to the interpreter.
def str: {type:"string"};
def ar($items): {type:"array",items:$items};
def en($items): {type:"string",enum:$items};
def obj($props): {type:"object",properties:$props,required:($props|keys_unsorted),additionalProperties:false};
def parameter:
 if type=="array" then en(.) elif .=="column" then {type:"integer",minimum:0}
 elif .=="integer" then {type:"integer"}
 elif .=="comparison" then en(["lt","le","eq","ge","gt"]) else str end;
def requirements($ops; $common; $profile):
 [obj({outcome:{type:"null"},parameters:obj({})}+$common)] as $unknown |
 [$ops[]|obj(.+$common)] as $single |
 (if ($ops|length)>0 then ar({anyOf:[$ops[]|obj(.)]}) else ar({})+{maxItems:0} end) as $sequence |
 [obj({operations:$sequence}+$common)] as $multiple |
 {anyOf:((if $profile=="sequence" then $multiple+$single else $single+$multiple end)+$unknown)};
. as $v |
[.outcomes|to_entries[]|{outcome:{const:.key},parameters:obj(.value.parameters|map_values(parameter))}] as $ops |
{runtime:ar(en($runtime[0]|keys)),uncovered:ar(str)} as $common |
([$providers[0].providers[].to|select(startswith("out:"))|ltrimstr("out:")]+[$adapters[0][].output_type]|unique) as $outputs |
# An effect with an internal result may participate in a longer chain. An
# effect implemented exclusively as a file writer must match the role's sink.
([$providers[0].providers[] | select(.effect!=null)]
 | group_by(.effect)
 | map(select(all(.[]; .to|startswith("out:")))
       | {key:.[0].effect, value:map(.to|ltrimstr("out:"))})
 | from_entries) as $sinks |
{id:str, text:str, output_type:{}, multiple:{type:"boolean"},
 content:en(["whole","text","embedded_images","pages","metadata","audio","video","other"])} as $fields |
# Share one schema for all output formats that permit the same operations.
([$outputs[],null]
 | map(. as $mime | {mime:$mime,ops:[
       $ops[] | select($sinks[.outcome.const] as $allowed |
                      $allowed==null or ($allowed|index($mime))!=null)
   ]})
 | group_by(.ops) | to_entries
 | map({name:("requirement"+(.key|tostring)), ops:.value[0].ops,
        formats:[.value[].mime|select(.!=null)], custom:any(.value[];.mime==null)})) as $groups |
($groups | map({key:.name,value:requirements(.ops;$common;$v.profile)}) | from_entries) as $defs |
{anyOf:[
 $groups[] as $g |
 (if ($groups|length)==1 then $defs[$g.name] else {"$ref":("#/$defs/"+$g.name)} end) as $req |
 (if ($g.formats|length)>0 then obj($fields+{output_type:en($g.formats),requirement:$req}) else empty end),
 (if $g.custom then obj($fields+{output_type:{const:"other"},requirement:$req,requested_output_type:str}) else empty end)
]} as $role |
ar(obj({id:str,text:str})) as $res |
{input_type:str,roles:ar($role),responsibilities:$res} as $body |
[obj($body),obj({artifact:obj({kind:{const:"cli"}})}+$body)] as $cli |
(if ($library|length)>0 then
 [obj({artifact:obj({kind:{const:"header"},filename:str,class_name:str,value_type:en($library[0].value_types|keys),
     methods:{type:"object",additionalProperties:en(([$library[0].classes[].methods|keys[]]|unique)+["unknown"])},uncovered:ar(str)}),
     responsibilities:$res})] else [] end) as $header |
{anyOf:($cli+$header+[obj({clarify:str})])} + (if ($groups|length)>1 then {"$defs":$defs} else {} end)
