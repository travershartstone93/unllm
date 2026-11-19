# Run with jq -es -f summarize.jq trials.jsonl
def histories: ["fresh", "repeat", "resident", "shuffled", "unrelated"];
def candidates: [.attempts[] | {cap, error, candidates:(.candidates | sort)}];
def contracts: [.grounding[] | select(startswith("contract "))] | unique;
def reasons: [.translation[], .grounding[] | select(startswith("ambiguous: ") or startswith("unsupported: "))] | unique;
group_by(.id) | map(
  if (map(.mode) | sort) != histories then error("missing or duplicate history for " + .[0].id) else . end
  | if (map(candidates) | unique | length) != 1 then error("candidate drift for " + .[0].id) else . end
  | if (map([.status,.reason,contracts,reasons]) | unique | length) != 1 then error("decision drift for " + .[0].id) else . end
  | if any(.[]; .status == "accepted" and .fixture_contract_match != true) then error("unexpected contract for " + .[0].id) else . end
  | .[0] | {id, question, normalized, readings:(.attempts[-1].candidates|length), cap:.attempts[-1].cap, status, reason, contracts:contracts, distinct_rejections:reasons,
             witness_positions:[.grounding|to_entries[]|select(.value|startswith("contract "))|.key+1]}
) | if length != 32 then error("expected 32 development requests") else . end
