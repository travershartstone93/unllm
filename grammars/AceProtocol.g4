grammar AceProtocol;
document: value* EOF;
value: '(' value* ('.' value)? ')' | STRING | ATOM;
STRING: '"' ('\\' . | ~["\\])* '"';
ATOM: ~[ \t\r\n()"]+;
WS: [ \t\r\n]+ -> skip;
