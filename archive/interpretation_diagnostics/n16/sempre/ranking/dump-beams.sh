#!/usr/bin/env bash
set -euo pipefail
fixture=$(cd "$(dirname "$0")" && pwd)
base=$(dirname "$fixture")
sempre=$(cd "${1:?Specify compiled SEMPRE checkout}" && pwd)
out=$(mkdir -p "${2:?Specify output directory}" && cd "$2" && pwd)
# Keep the preregistered training and parser initialization verbatim. Replace only reporting.
awk '/System.out.println\("split/{exit} {print}' "$base/Probe.java" > "$out/Probe.java"
cat >> "$out/Probe.java" <<'JAVA'
    for (String split : Arrays.asList("fit", "calibration", "transfer")) {
      Path path = split.equals("fit") ? fixtures.resolve("transfer.tsv")
                                     : fixtures.resolve("ranking").resolve(split + ".tsv");
      List<String> rows = Files.readAllLines(path);
      for (String line : rows.subList(1, rows.size())) {
        String[] fields = line.split("\\t", 3);
        Example ex = example(fields[2], null);
        production.parser.parse(training.params, ex, false);
        Map<String,Double> scores = new LinkedHashMap<>();
        for (Derivation d : ex.predDerivations) {
          String program = ((StringValue)((ValueFormula<?>)d.formula).value).value;
          scores.merge(program, d.score, Math::max);
        }
        if (scores.isEmpty()) scores.put("-", 0.0);
        for (Map.Entry<String,Double> entry : scores.entrySet())
          System.out.println("BEAM\t"+split+"\t"+fields[2]+"\t"+fields[1]+"\t"+
              vocab.containsAll(ex.getTokens())+"\t"+entry.getKey()+"\t"+entry.getValue());
      }
    }
  }
}
JAVA
cd "$sempre"
"${JAVAC:-javac}" --release 8 -cp 'classes:lib/*' -d "$out" "$base/SpanFeatures.java" "$out/Probe.java"
"${JAVA:-java}" -Xmx1500m -cp "$out:classes:lib/*:fig-master/external/*" edu.stanford.nlp.sempre.Probe "$base" > "$out/log"
{ printf 'split\trequest\texpected\tcovered\tcandidate\tscore\n'; awk '/^BEAM\t/{sub(/^BEAM\t/,""); print}' "$out/log"; } > "$out/beams.tsv"
