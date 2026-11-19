package edu.stanford.nlp.sempre;

import java.util.HashSet;
import java.util.Set;

/** Domain-independent lexical features for labeled word spans in the offline trial. */
public class SpanFeatures implements FeatureComputer {
  public void extractLocal(Example ex, Derivation deriv) {
    if (deriv.start < 0 || deriv.rule == null || !(deriv.rule.sem instanceof ConstantFn))
      return;
    Set<String> features = new HashSet<>();
    for (int i = deriv.start; i < deriv.end; ++i) {
      features.add(ex.token(i));
      if (i > deriv.start)
        features.add(ex.token(i - 1) + " " + ex.token(i));
    }
    for (String feature : features)
      deriv.addFeature("atom", deriv.formula + " / " + feature);
  }
}
