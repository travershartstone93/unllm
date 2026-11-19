package edu.stanford.nlp.sempre;

import fig.basic.LogInfo;
import java.nio.file.*;
import java.util.*;

public class Probe {
  static Builder parser(Path grammar) {
    Grammar.opts.inPaths = Collections.singletonList(grammar.toString());
    Builder builder = new Builder();
    builder.build();
    return builder;
  }

  static Example example(String text, String target) {
    Example.Builder b = new Example.Builder().setUtterance(
        text.toLowerCase(Locale.ROOT).replace(",", " ").replace(".", " ").trim());
    if (target != null) b.setTargetValue(new StringValue(target));
    Example ex = b.createExample();
    ex.preprocess();
    return ex;
  }

  public static void main(String[] args) throws Exception {
    Path fixtures = Paths.get(args[0]);
    LogInfo.init();
    Builder.opts.executor = "FormulaMatchExecutor";
    FeatureExtractor.opts.featureComputers = Collections.singleton("SpanFeatures");
    Parser.opts.beamSize = 100;
    Builder training = parser(fixtures.resolve("atoms.grammar"));
    List<Example> examples = new ArrayList<>();
    Set<String> vocab = new HashSet<>(Collections.singleton("and"));
    for (String line : Files.readAllLines(fixtures.resolve("atoms.tsv"))) {
      String[] fields = line.split("\\t");
      Example ex = example(fields[1], fields[0]);
      examples.add(ex);
      vocab.addAll(ex.getTokens());
    }
    Random shuffle = new Random(19);
    for (int epoch = 0; epoch < 12; ++epoch) {
      Collections.shuffle(examples, shuffle);
      for (Example ex : examples)
        training.params.update(training.parser.parse(training.params, ex, true).expectedCounts);
    }
    training.params.finalizeWeights();
    Builder production = parser(fixtures.resolve("pipeline.grammar"));
    System.out.println("split\texpected\tbest\tgap\tvocabulary_covered\trequest");
    for (String split : Arrays.asList("calibration", "transfer")) {
      List<String> cases = Files.readAllLines(fixtures.resolve(split + ".tsv"));
      for (String line : cases.subList(1, cases.size())) {
        String[] fields = line.split("\\t", 3);
        Example ex = example(fields[2], null);
        production.parser.parse(training.params, ex, false);
        String winner = "-";
        double top = Double.NEGATIVE_INFINITY, gap = Double.POSITIVE_INFINITY;
        for (Derivation d : ex.predDerivations) {
          String candidate = ((StringValue)((ValueFormula<?>)d.formula).value).value;
          if (winner.equals("-")) {
            winner = candidate;
            top = d.score;
          } else if (!candidate.equals(winner)) {
            gap = top - d.score;
            break;
          }
        }
        System.out.println(split+"\t"+fields[1]+"\t"+winner+"\t"+gap+"\t"+
            vocab.containsAll(ex.getTokens())+"\t"+fields[2]);
      }
    }
  }
}
