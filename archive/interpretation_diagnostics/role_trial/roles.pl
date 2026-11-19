:- module(role_trial,[evaluate/2]).
:- use_module(library(http/json)).
:- use_module(library(lists)).

% Input consists of lexical concepts and grammatical variables from one reading.
ep(R,W,E,A,B) :- member([W,E,A,B,_,_],R).
noun(R,W,E) :- member([W,E,_,_,noun,_],R).
same_entity(_,X,X).
same_entity(R,X,Y) :- dif(X,Y), (ep(R,appos,_,X,Y);ep(R,appos,_,Y,X)).
amount(R,Event,Prep,Number) :-
    ep(R,Prep,_,Event,Target), member([card,C,A,_,_,Number],R),
    (same_entity(R,Target,C);same_entity(R,Target,A)).
counter_roles(R,I,D) :-
    noun(R,counter,C), ep(R,start,S,C,_), amount(R,S,at,I),
    member([decrease,V,A,B,verb,_],R), (C=A;C=B), (amount(R,V,by,D); amount(R,C,by,D)).
save_target(R,document) :- (noun(R,text,_);noun(R,editor,_)).
save_target(R,collection) :- noun(R,list,_).
roles(R,Outcome) :-
    ( ep(R,save,_,_,_) ->
        findall(T,save_target(R,T),Ts), sort(Ts,Targets), Outcome=save(Targets)
    ; findall([I,D],counter_roles(R,I,D),Pairs), sort(Pairs,Unique), ( Unique=[] -> findall(W,(member([W,_,_,_,verb,_],R),memberchk(W,[start,decrease])),Vs),sort(Vs,Verbs),Outcome=incomplete(Verbs) ; Outcome=counter(Unique) )
    ).
evaluate(Input,Output) :-
    atom_json_term(Input,Rows,[]),
    call_with_inference_limit(roles(Rows,Result),100000,Limit),
    (Limit==inference_limit_exceeded -> Output=resource_limit ; term_to_atom(Result,Output)).
