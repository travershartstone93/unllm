grammar Provenance;
term: (IDENT | NEQ) '(' (value (',' value)*)? ')' EOF;
value: STRING | INTEGER;
IDENT: [a-zA-Z_] [a-zA-Z_0-9]*;
NEQ: '!=';
INTEGER: [0-9]+;
STRING: '"' ('\\' . | ~["\\\r\n])* '"';
WS: [ \t\r\n]+ -> skip;
