grammar Arithmetic;

start      : expression EOF ;
expression : term ((PLUS | MINUS) term)* ;
term       : unary ((STAR | SLASH) unary)* ;
unary      : (PLUS | MINUS) unary | primary ;
primary    : NUMBER | LPAREN expression RPAREN ;

PLUS   : '+' ;
MINUS  : '-' ;
STAR   : '*' ;
SLASH  : '/' ;
LPAREN : '(' ;
RPAREN : ')' ;
NUMBER : ([0-9]+ ('.' [0-9]*)? | '.' [0-9]+) ([eE] [+-]? [0-9]+)? ;
WS     : [ \t\r\n]+ -> skip ;
