grammar BehaviourExpr;
@lexer::members { int grouping = 0; }
document: NEWLINE* expression NEWLINE* EOF;
expression: disjunction ('if' disjunction 'else' expression)?;
disjunction: conjunction ('or' conjunction)*;
conjunction: negation ('and' negation)*;
negation: 'not' negation | comparison;
comparison: sum (COMPARE sum)*;
sum: product (('+' | '-') product)*;
product: unary (('*' | '/') unary)*;
unary: ('-' | '+') unary | atom;
atom: 'True' | 'False' | NUMBER | STRING+ | '[' ']'
    | NAME ('.' NAME | '(' arguments? ')')? | '(' expression ')';
arguments: expression (',' expression)* ','?;
COMPARE: '>=' | '<=' | '==' | '!=' | '>' | '<';
NUMBER: '0' [xX] '_'? HEX ('_'? HEX)*
      | '0' [oO] '_'? [0-7] ('_'? [0-7])*
      | '0' [bB] '_'? [01] ('_'? [01])*
      | (DIGITS ('.' DIGITS?)? | '.' DIGITS) ([eE] [+-]? DIGITS)?;
fragment HEX: [0-9a-fA-F];
fragment DIGITS: [0-9] ('_'? [0-9])*;
STRING: [rRuU]? ('\'\'\'' (ESC | .)*? '\'\'\''
       | '"""' (ESC | .)*? '"""'
       | '\'' (ESC | ~['\\\r\n])* '\''
       | '"' (ESC | ~["\\\r\n])* '"');
fragment ESC: '\\' .;
NAME: [a-zA-Z_] [a-zA-Z_0-9]*;
LPAREN: '(' { ++grouping; };
RPAREN: ')' { --grouping; };
LBRACKET: '[' { ++grouping; };
RBRACKET: ']' { --grouping; };
COMMENT: '#' ~[\r\n]* -> skip;
CONTINUATION: '\\' '\r'? '\n' -> skip;
NEWLINE: ('\r\n' | '\r' | '\n') { if (grouping > 0) skip(); };
WS: [ \t]+ -> skip;
