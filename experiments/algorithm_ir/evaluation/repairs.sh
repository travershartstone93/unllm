#!/usr/bin/env bash
set -euo pipefail
cli=$(realpath "${1:?algorithm executable}")
out=$(realpath -m "${2:?fresh result directory}")
python=$(realpath "${3:?target interpreter}")
fixtures=$(realpath "$(dirname "$0")/../fixtures")
mkdir "$out"
for defect in expression branch dependency index collection; do
  dir="$out/$defect"; mkdir "$dir"
  case $defect in
    expression) spec=prefix; mutation='walk(if type=="array" and .[0]=="add" then .[0]="sub" else . end)' ;;
    branch) spec=positive_total; mutation='walk(if type=="array" and .[0]=="if" then [.[0],.[1],.[3],.[2]] else . end)' ;;
    dependency) spec=positive_total; mutation='.functions |= ([.[0],(.[0]|.name="other"|.return=["lt",["var","n"],["int","0"]]),.[1]]) | walk(if type=="array" and .[0]=="call" then .[1]="other" else . end)' ;;
    index) spec=adjacent; mutation='walk(if type=="array" and .[0]=="at" and (.[2]|type)=="array" and .[2][0]=="sub" then .[2]=.[2][1] else . end)' ;;
    collection) spec=prefix; mutation='walk(if type=="array" and .[0]=="append" then .[2]=["var","element"] else . end)' ;;
  esac
  jq "$mutation" "$fixtures/$spec.json" > "$dir/seed.json"
  case $spec in
    prefix) result=ints; expected='[[],["2","5","1"],["-7","-7","-5"]]'; withheld='["5","4","7"]' ;;
    positive_total) result=int; expected='["0","5","2"]'; withheld='"8"' ;;
    adjacent) result=ints; expected='[[],["1","-7"],["7","2"]]'; withheld='["-6","4"]' ;;
  esac
  jq -n --arg t "$result" --argjson y "$expected" '{input:"ints",output:$t,examples:([[],["2","3","-4"],["-7","0","2"]]|to_entries|map({input:.value,output:$y[.key]}))}' >"$dir/contract.json"
  jq -n --arg t "$result" --argjson y "$withheld" '{input:"ints",output:$t,examples:[{input:["5","-1","3"],output:$y}]}' >"$dir/check.json"
  if "$cli" check "$dir/seed.json" "$dir/contract.json" "$python" >"$dir/before.log" 2>&1; then echo "undetected defect: $defect" >&2; exit 1; fi
  "$cli" repair "$dir/seed.json" "$dir/contract.json" >"$dir/result.json"
  status=$(jq -r .status "$dir/result.json")
  if [[ $status == candidate ]]; then
    jq .ir "$dir/result.json" >"$dir/repaired.json"
    if "$cli" check "$dir/repaired.json" "$dir/contract.json" "$python" >"$dir/fit.json" 2>"$dir/fit.stderr" &&
       "$cli" check "$dir/repaired.json" "$dir/check.json" "$python" >"$dir/unseen.json" 2>"$dir/unseen.stderr"; then status=passed; else status=failed; fi
  fi
  jq -nc --arg name "$defect" --arg status "$status" --slurpfile r "$dir/result.json" '{defect:$name,status:$status,method:$r[0].method,candidates:$r[0].unique_candidates,site:$r[0].edit_path}' >>"$out/repairs.jsonl"
done
