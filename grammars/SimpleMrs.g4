grammar SimpleMrs;
document: '[' LINK? STRING? header* ']' EOF;
header
 : ('LTOP' | 'TOP') ':' ATOM
 | 'INDEX' ':' variable
 | 'RELS' ':' '<' relation* '>'
 | 'HCONS' ':' '<' constraint* '>'
 | 'ICONS' ':' '<' constraint* '>'
 ;
relation: '[' predicate LINK? STRING? 'LBL' ':' ATOM argument* ']';
predicate: ATOM | STRING;
argument: ATOM ':' (variable | STRING);
variable: ATOM ('[' ATOM property* ']')?;
property: ATOM ':' ATOM;
constraint: variable ATOM variable;
LINK: '<' '-'? [0-9]+ [:#] '-'? [0-9]+ '>' | '<@' [0-9]+ '>' | '<' [0-9]+ (' '+ [0-9]+)* '>';
STRING: '"' ('\\' . | ~["\\])* '"';
ATOM: ~[ \t\r\n\u005B\u005D<>:"]+;
WS: [ \t\r\n]+ -> skip;
