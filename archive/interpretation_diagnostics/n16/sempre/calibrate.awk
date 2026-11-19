BEGIN {
  FS = OFS = "\t"
  count = split("0 0.25 0.5 1 2 4", grid, " ")
}
$1 == "calibration" {
  for (i = 1; i <= count; ++i) {
    if ($5 != "true" || $3 == "-" || $3 ~ /(^| )reject( |$)/ || $4+0 < grid[i]+0) continue
    if ($2 == $3) correct[i]++
    else wrong[i]++
  }
}
END {
  selected = "off"
  print "threshold", "exact_positive", "incorrect_acceptance"
  for (i = 1; i <= count; ++i) {
    print grid[i], correct[i]+0, wrong[i]+0
    if (!wrong[i] && correct[i] > 0 && correct[i] >= best) {
      best = correct[i]
      selected = grid[i]
    }
  }
  print "selected", selected
}
