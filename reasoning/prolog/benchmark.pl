% A separate goal-driven comparison for the benchmark's dependency task.
% This module is never consulted by the product's reasoning worker.
:- use_module(library(http/json)).
:- dynamic fact/4.
:- table reach/2.
:- initialization(main,main).

reach(S,O) :- fact(depends_on,S,O,_).
reach(S,O) :- fact(depends_on,S,M,_),reach(M,O).
path(S,O,_,[Id]) :- fact(depends_on,S,O,Id).
path(S,O,Seen,[Id|Ids]) :-
    fact(depends_on,S,M,Id), \+ memberchk(M,Seen),path(M,O,[M|Seen],Ids).

load(F) :- assertz(fact(F.predicate,F.subject,F.object,F.id)).
sample(Ms) :-
    get_time(Start),forall(between(1,100,_),(once(reach(g2499_0,g2499_4)),once(path(g2499_0,g2499_4,[],_)))),
    get_time(End),Ms is (End-Start)*10.

peak_rss(KiB) :-
    read_file_to_string('/proc/self/status',Text,[]),split_string(Text,"\n","",Lines),
    member(Line,Lines),sub_string(Line,0,6,_,"VmHWM:"),
    split_string(Line," \t"," \t",[_,Number|_]),number_string(KiB,Number),!.

main([File]) :-
    get_time(Start),setup_call_cleanup(open(File,read,In),json_read_dict(In,Facts,[value_string_as(atom)]),close(In)),
    maplist(load,Facts),once(reach(g2499_0,g2499_4)),once(path(g2499_0,g2499_4,[],Ids)),get_time(Ready),
    findall(Ms,(between(1,60,_),sample(Ms)),Times),peak_rss(Memory),
    Prepare is (Ready-Start)*1000,
    json_write_dict(current_output,_{prepare_and_first_query_ms:Prepare,times_ms:Times,
                      proof_ids:Ids,process_peak_rss_kib:Memory}),nl.
