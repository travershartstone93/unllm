grammar ScalarExpr;
document: expression EOF;
expression: disjunction ('if' disjunction 'else' expression)?;
disjunction: conjunction ('or' conjunction)*;
conjunction: negation ('and' negation)*;
negation: 'not' negation | comparison;
comparison: sum (COMPARE sum)?;
sum: product (('+' | '-') product)*;
product: unary ('*' unary)*;
unary: '-' unary | atom;
atom: 'x' | 'True' | 'False' | NUMBER | '(' expression ')';
COMPARE: '>=' | '<=' | '==' | '>' | '<';
NUMBER: '0' [xX] '_'? HEX ('_'? HEX)*
      | '0' [oO] '_'? [0-7] ('_'? [0-7])*
      | '0' [bB] '_'? [01] ('_'? [01])*
      | (DIGITS ('.' DIGITS?)? | '.' DIGITS) ([eE] [+-]? DIGITS)?;
fragment HEX: [0-9a-fA-F];
fragment DIGITS: [0-9] ('_'? [0-9])*;
WS: [ \t\r\n]+ -> skip;
