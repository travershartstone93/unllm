:- module(unllm_logic, [reset_world/0, add_source/5, add_background/3,
                        add_complete/2, solve/2, missing/4, application_plan/4]).
:- dynamic source/5, background/3, complete/2.
:- table support/4 as incremental.
:- dynamic source/5 as incremental.
:- dynamic background/3 as incremental.
:- dynamic complete/2 as incremental.

reset_world :-
    retractall(source(_,_,_,_,_)),
    retractall(background(_,_,_)),
    retractall(complete(_,_)),
    abolish_all_tables.

add_source(Sign,P,S,O,Id) :- assertz(source(Sign,P,S,O,Id)).
add_background(P,S,O) :- assertz(background(P,S,O)).
add_complete(P,S) :- assertz(complete(P,S)).

support(Sign,P,S,O) :- source(Sign,P,S,O,_).
support(pos,P,S,O) :- background(P,S,O).
support(pos,blocked,S,true) :-
    support(pos,depends_on,S,D), support(neg,available,D,true).
support(neg,blocked,S,true) :-
    complete(depends_on,S),
    \+ (support(pos,depends_on,S,D), \+ support(pos,available,D,true)).
support(Sign,taller_than,S,O) :-
    support(pos,height,S,A), support(pos,height,O,B),
    numeric_value(A,X), numeric_value(B,Y), greater(Sign,X,Y).

% Imported numeric properties use a bounded binary64 contract. Height is
% normalized to metres by the source adapter, never guessed from a unit label.
numeric_value(A,X) :- atom(A), catch((atom_number(A,N),X is float(N)),_,fail), abs(X)=<1.0e15.
greater(pos,X,Y) :- X>Y.
greater(neg,X,Y) :- X=<Y.

solve([], []).
solve([goal(Sign,P,S,O)|Rest], [proof(Sign,P,S,O,W)|Proofs]) :-
    support(Sign,P,S,O),
    witness(Sign,P,S,O,[],W),
    solve(Rest,Proofs).

% Reconstruct witnesses after tabled truth resolution. Do not table proof
% trees: cyclic relations would otherwise produce infinitely many answers.
witness(Sign,P,S,O,_,claim(Id)) :- source(Sign,P,S,O,Id).
witness(pos,P,S,O,_,background(P,S,O)) :-
    background(P,S,O), \+ source(pos,P,S,O,_).
witness(pos,blocked,S,true,Seen,rule(requirement_unavailable,[A,B])) :-
    \+ memberchk(blocked-S,Seen),
    support(pos,depends_on,S,D), support(neg,available,D,true),
    witness(pos,depends_on,S,D,[blocked-S|Seen],A),
    witness(neg,available,D,true,[blocked-S|Seen],B).
witness(neg,blocked,S,true,_,rule(requirements_satisfied,Proofs)) :-
    complete(depends_on,S),
    findall(D,support(pos,depends_on,S,D),Ds0), sort(Ds0,Ds),
    available_witnesses(Ds,Proofs).
witness(Sign,taller_than,S,O,_,rule(height_comparison,[A,B])) :-
    support(pos,height,S,X), support(pos,height,O,Y),
    numeric_value(X,N), numeric_value(Y,M), greater(Sign,N,M),
    witness(pos,height,S,X,[],A), witness(pos,height,O,Y,[],B).

available_witnesses([], []).
available_witnesses([D|Ds],[W|Ws]) :-
    witness(pos,available,D,true,[],W), available_witnesses(Ds,Ws).

% A bounded diagnostic over the declared requirements rule. Missing evidence
% is returned as a premise to investigate, never asserted as a source claim.
missing(blocked,S,true,needs(available,D,true)) :-
    support(pos,depends_on,S,D),
    \+ support(pos,available,D,true),
    \+ support(neg,available,D,true).

% Application construction searches providers and recursively discharges
% their requirements. Unlike the stream planner this can produce a graph
% of nested obligations, rather than checking one preselected linear list.
application_plan(Roots,Language,Trees,Proofs) :-
    length(Roots,N),N=<32,
    app_roots(Roots,Language,[],Trees,Proofs).
app_roots([],_,_,[],[]).
app_roots([Cap|Caps],Lang,Seen,[Tree|Trees],Proofs) :-
    app_expand(Cap,Lang,Seen,Tree,A),
    app_roots(Caps,Lang,Seen,Trees,B),
    append(A,B,Proofs).
app_expand(Cap,Lang,Seen,node(Cap,Component,Children),Proofs) :-
    \+ memberchk(Cap,Seen),length(Seen,N),N<16,
    support(pos,implements,Component,Cap),
    member(Target,[Lang,any]),support(pos,target,Component,Target),
    support(pos,available,Component,true),
    \+ support(neg,available,Component,true),
    findall(D,support(pos,requires_capability,Component,D),Ds0),sort(Ds0,Ds),
    app_roots(Ds,Lang,[Cap|Seen],Children,ChildProofs),
    app_evidence([goal(pos,implements,Component,Cap),goal(pos,target,Component,Target),
                  goal(pos,available,Component,true)],Here),
    app_requirements(Component,Ds,Dependencies),
    append([Here,Dependencies,ChildProofs],Proofs).
app_requirements(_,[],[]).
app_requirements(C,[D|Ds],[proof(pos,requires_capability,C,D,W)|Proofs]) :-
    witness(pos,requires_capability,C,D,[],W),app_requirements(C,Ds,Proofs).
app_evidence([],[]).
app_evidence([goal(Sign,P,S,O)|Gs],[proof(Sign,P,S,O,W)|Ps]) :-
    witness(Sign,P,S,O,[],W),app_evidence(Gs,Ps).
