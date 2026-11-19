// C ABI for the local worker. This translation unit is built as a separate
// shared library: legacy unllm clients do not initialize either engine.
#include "unllm/json.h"
#include <SWI-Prolog.h>
#include <souffle/SouffleInterface.h>
#include <souffle/provenance/ExplainProvenanceImpl.h>
#include <souffle/provenance/ExplainTree.h>
#include <chrono>
#include <algorithm>
#include <cstdlib>
#include <cstring>
#include <map>
#include <memory>
#include <mutex>
#include <sstream>
#include <stdexcept>
#include <string>

using unllm::json::Value;
namespace {
std::mutex mutex_;
bool initialized = false;
std::unique_ptr<souffle::SouffleProgram> background;
std::unique_ptr<souffle::ExplainProvenanceImpl> explainer;
std::string loaded_key;
Value rules;
Value world_input;
size_t background_size=0;
bool used_prepared=false;

void check(int result) {
    if (!result) throw std::runtime_error("Prolog term or predicate failed");
}
void atom(term_t t, const std::string& text) {
    check(PL_put_chars(t, PL_ATOM | REP_UTF8, text.size(), text.data()));
}
std::string chars(term_t t) {
    char* p = nullptr;
    size_t n = 0;
    if (!PL_get_nchars(t, &n, &p, CVT_ATOM | CVT_STRING | CVT_INTEGER | CVT_FLOAT | REP_UTF8 | BUF_MALLOC))
        throw std::runtime_error("Expected a ground Prolog value");
    std::string out(p, n);
    PL_free(p);
    return out;
}
void call(const char* name, int arity, term_t args) {
    const auto pred = PL_predicate(name, arity, "unllm_logic");
    const auto q = PL_open_query(nullptr, PL_Q_NODEBUG | PL_Q_CATCH_EXCEPTION, pred, args);
    const int ok = PL_next_solution(q);
    std::string error;
    if (!ok && PL_exception(q)) {
        char* p = nullptr;
        if (PL_get_chars(PL_exception(q), &p, CVT_WRITE | BUF_RING)) error = p;
    }
    PL_close_query(q);
    if (!ok) throw std::runtime_error(error.empty() ? std::string("Prolog failed: ") + name : error);
}
void initialize() {
    if (initialized) return;
    static char name[] = "unllm-upgrade";
    static char quiet[] = "-q";
    static char signals[] = "--nosignals";
    static char flag[] = "-f";
    static char none[] = "none";
    char* argv[] = {name, quiet, signals, flag, none, nullptr};
    if (!PL_initialise(5, argv)) throw std::runtime_error("Cannot initialize embedded SWI-Prolog");
    term_t arg = PL_new_term_ref();
#ifdef UNLLM_NATIVE_RULE_PATH
    const char *rules = std::getenv("UNLLM_NATIVE_RULE_PATH");
    if (!rules || !*rules) throw std::runtime_error("Missing native Prolog rules path");
    atom(arg, rules);
#else
    atom(arg, UNLLM_PROLOG_RULES);
#endif
    call("consult", 1, arg);
    initialized = true;
}
void build_background(const Value& request) {
    explainer.reset();
    background.reset(souffle::ProgramFactory::newInstance("background"));
    if (!background) throw std::runtime_error("Souffle background module is unavailable");
    background->setNumThreads(1);
    auto* sources = background->getRelation("source");
    auto* trans = background->getRelation("transitive");
    auto* inverse = background->getRelation("inverse");
    auto* domains = background->getRelation("arg_domain");
    auto* ranges = background->getRelation("arg_range");
    for (const auto& relation : request["relations"].items()) {
        const auto p = relation["name"].text();
        if (relation["transitive"].flag()) {
            souffle::tuple t(trans); t << p; trans->insert(t);
        }
        const auto other = relation["inverse"].text();
        if (!other.empty()) {
            souffle::tuple t(inverse); t << p << other; inverse->insert(t);
        }
        const auto domain = relation["domain"].text("entity");
        const auto range = relation["range"].text("entity");
        if (!domain.empty()) {
            souffle::tuple t(domains); t << p << domain; domains->insert(t);
        }
        if (!range.empty() && range!="bool" && range!="number" && range!="quantity") {
            souffle::tuple t(ranges); t << p << range; ranges->insert(t);
        }
    }
    const auto facts = request["facts"].items();
    if (facts.size() > 100000) throw std::runtime_error("Input fact budget exceeded (100000)");
    for (const auto& fact : facts) {
        const auto id = fact["id"].text(), p = fact["predicate"].text();
        const auto s = fact["subject"].text(), o = fact["object"].text();
        const bool negative = fact["negative"].flag();
        if (!negative) {
            souffle::tuple t(sources); t << id << p << s << o; sources->insert(t);
        }
    }
    background->run();
    auto* closure = background->getRelation("closure");
    if (closure->size() > 500000) throw std::runtime_error("Derived relation budget exceeded (500000)");
    background_size=closure->size();
    explainer = std::make_unique<souffle::ExplainProvenanceImpl>(*background);
    explainer->setup();
    std::ostringstream rule_text; explainer->printRulesJSON(rule_text);
    try { rules = Value::parse("{"+rule_text.str()+"}"); } catch (...) { rules = Value::string(rule_text.str()); }
}
void prepare(const Value& request) {
    if(request["facts"].null())return;
    const auto key=request["world_key"].text();
    if(!key.empty() && key==loaded_key)return;
    call("reset_world",0,PL_new_term_ref());
    world_input=request;
    explainer.reset();background.reset();
    for(const auto& fact:request["facts"].items()) {
        const auto a=PL_new_term_refs(5);
        atom(a,fact["negative"].flag()?"neg":"pos");
        atom(a+1,fact["predicate"].text());atom(a+2,fact["subject"].text());
        atom(a+3,fact["object"].text());atom(a+4,fact["id"].text());
        call("add_source",5,a);
    }
    const auto prepared=request["prepared"];
    used_prepared=!prepared["closure"].null();
    if(used_prepared) {
        auto rows=prepared["closure"].items();
        if(rows.size()>500000)throw std::runtime_error("Prepared relation budget exceeded");
        for(const auto& row:rows) {
            const auto values=row.items();
            if(values.size()!=3)throw std::runtime_error("Malformed prepared relation");
            const auto a=PL_new_term_refs(3);
            for(size_t i=0;i<3;++i)atom(a+i,values[i].text());
            call("add_background",3,a);
        }
        background_size=rows.size();rules=prepared["rules"];
    } else {
        build_background(request);
        for(auto& row:*background->getRelation("closure")) {
            std::string p,s,o;row>>p>>s>>o;
            const auto a=PL_new_term_refs(3);
            atom(a,p);atom(a+1,s);atom(a+2,o);call("add_background",3,a);
        }
    }
    for(const auto& complete:request["complete"].items()) {
        const auto a=PL_new_term_refs(2);
        atom(a,complete["predicate"].text());atom(a+1,complete["subject"].text());
        call("add_complete",2,a);
    }
    loaded_key=key;
}
Value from_term(term_t t, int depth = 0) {
    if (depth > 256) throw std::runtime_error("Explanation depth budget exceeded");
    if (PL_get_nil(t)) return Value::array();
    if (PL_is_list(t)) {
        Value out = Value::array();
        term_t tail = PL_new_term_ref(), head = PL_new_term_ref();
        check(PL_put_term(tail,t));
        while (PL_get_list(tail,head,tail)) out.push(from_term(head,depth+1));
        if (!PL_get_nil(tail)) throw std::runtime_error("Improper Prolog proof list");
        return out;
    }
    if (PL_is_variable(t)) return {};
    if (PL_is_compound(t)) {
        atom_t functor; size_t arity;
        check(PL_get_name_arity(t,&functor,&arity));
        const std::string name = PL_atom_chars(functor);
        Value out = Value::object(), args = Value::array();
        for (size_t i=1;i<=arity;++i) {
            term_t a = PL_new_term_ref(); check(PL_get_arg(i,t,a));
            args.push(from_term(a,depth+1));
        }
        out.set("functor",Value::string(name)); out.set("args",args);
        if (name=="background" && arity==3) {
            // Persisted closure answers are cheap to load. Reconstruct their
            // proof with the same compiled program on first demand.
            if(!explainer)build_background(world_input);
            const auto values=args.items();
            auto tree=explainer->explain("closure",
                {"\""+values[0].text()+"\"","\""+values[1].text()+"\"","\""+values[2].text()+"\""},96);
            std::ostringstream stream; tree->printJSON(stream,0);
            out.set("tree",Value::parse(stream.str()));
        }
        return out;
    }
    return Value::string(chars(t));
}
void put_arg(term_t t, const std::string& value, std::map<std::string,term_t>& vars) {
    if (!value.empty() && value[0]=='?') {
        auto it=vars.find(value);
        if (it==vars.end()) it=vars.emplace(value,PL_new_term_ref()).first;
        check(PL_put_term(t,it->second));
    } else atom(t,value);
}
Value execute(const Value& request) {
    initialize();
    const fid_t frame=PL_open_foreign_frame();
    struct Close { fid_t f; ~Close(){PL_discard_foreign_frame(f);} } close{frame};
    auto start=std::chrono::steady_clock::now();
    if(request["facts"].null() && request["world_key"].text()!=loaded_key)
        throw std::runtime_error("world snapshot is not loaded");
    prepare(request);
    if(request["operation"].text()=="application_plan") {
        auto output=Value::object();
        const auto args=PL_new_term_refs(4);
        check(PL_put_nil(args));
        const auto roots=request["requirements"].items();
        if(roots.size()>32)throw std::runtime_error("Application requirement budget exceeded");
        for(auto it=roots.rbegin();it!=roots.rend();++it) {
            const auto head=PL_new_term_ref();atom(head,it->text());
            check(PL_cons_list(args,head,args));
        }
        atom(args+1,request["language"].text());
        const auto goal=PL_new_term_ref();
        static functor_t plan_functor=PL_new_functor(PL_new_atom("application_plan"),4);
        check(PL_cons_functor_v(goal,plan_functor,args));
        const auto limited=PL_new_term_refs(3);
        check(PL_put_term(limited,goal));check(PL_put_integer(limited+1,1000000));
        const auto pred=PL_predicate("call_with_inference_limit",3,"unllm_logic");
        const auto query=PL_open_query(nullptr,PL_Q_NODEBUG|PL_Q_CATCH_EXCEPTION,pred,limited);
        struct QueryClose {qid_t q;~QueryClose(){PL_close_query(q);}} cleanup{query};
        const bool success=PL_next_solution(query);
        if(PL_exception(query))throw std::runtime_error("Application planning raised a Prolog exception");
        const bool exhausted=success && chars(limited+2)=="inference_limit_exceeded";
        output.set("status",Value::string(exhausted?"resource_limit":success?"complete":"unsupported"));
        if(success && !exhausted) {
            output.set("trees",from_term(args+2));output.set("proofs",from_term(args+3));
        }
        output.set("world_key",Value::string(loaded_key));
        return output;
    }
    if(request["operation"].text()=="snapshot") {
        if(!background)build_background(world_input);
        auto snapshot=Value::object(),rows=Value::array();
        for(auto& row:*background->getRelation("closure")) {
            std::string p,s,o;row>>p>>s>>o;
            auto values=Value::array();values.push(Value::string(p));
            values.push(Value::string(s));values.push(Value::string(o));rows.push(values);
        }
        snapshot.set("status",Value::string("complete"));snapshot.set("closure",rows);
        snapshot.set("rules",rules);snapshot.set("world_key",Value::string(loaded_key));
        return snapshot;
    }
    Value result=Value::object(), rows=Value::array();
    std::map<std::string,term_t> vars;
    const term_t goals=PL_new_term_ref(); check(PL_put_nil(goals));
    static functor_t goal_functor=PL_new_functor(PL_new_atom("goal"),4);
    static functor_t solve_functor=PL_new_functor(PL_new_atom("solve"),2);
    auto input=request["goals"].items();
    for (auto it=input.rbegin();it!=input.rend();++it) {
        const term_t a=PL_new_term_refs(4);
        atom(a,(*it)["negative"].flag() ? "neg" : "pos");
        put_arg(a+1,(*it)["predicate"].text(),vars);
        put_arg(a+2,(*it)["subject"].text(),vars);
        put_arg(a+3,(*it)["object"].text(),vars);
        const term_t g=PL_new_term_ref();
        check(PL_cons_functor_v(g,goal_functor,a));
        check(PL_cons_list(goals,g,goals));
    }
    const term_t solve_args=PL_new_term_refs(2);
    check(PL_put_term(solve_args,goals));
    const term_t goal=PL_new_term_ref();
    check(PL_cons_functor_v(goal,solve_functor,solve_args));
    const term_t limited=PL_new_term_refs(3);
    check(PL_put_term(limited,goal));
    check(PL_put_integer(limited+1,1000000));
    const auto pred=PL_predicate("call_with_inference_limit",3,"unllm_logic");
    const auto query=PL_open_query(nullptr,PL_Q_NODEBUG | PL_Q_CATCH_EXCEPTION,pred,limited);
    const int64_t limit=std::min<int64_t>(std::max<int64_t>(request["limit"].integer(100),1),1000);
    std::string status="complete";
    bool truncated=false;
    int64_t count=0;
    while (PL_next_solution(query)) {
        if (chars(limited+2)=="inference_limit_exceeded") {status="resource_limit";break;}
        if (count++>=limit) {truncated=true;break;}
        Value row=Value::object(), bindings=Value::object();
        for (const auto& [name,t] : vars) bindings.set(name,from_term(t));
        row.set("bindings",bindings);
        row.set("proofs",from_term(solve_args+1));
        rows.push(row);
    }
    if (PL_exception(query)) {
        char* p=nullptr;
        if (PL_get_chars(PL_exception(query),&p,CVT_WRITE | BUF_RING)) {
            const std::string error=p;
            PL_close_query(query); throw std::runtime_error(error);
        }
    }
    PL_close_query(query);
    result.set("status",Value::string(status)); result.set("solutions",rows);
    result.set("truncated",Value::boolean(truncated));
    result.set("rules",rules);
    result.set("world_key",Value::string(loaded_key));
    result.set("background_size",Value::number(background_size));
    result.set("used_prepared",Value::boolean(used_prepared));
    result.set("elapsed_us",Value::number(std::chrono::duration_cast<std::chrono::microseconds>(
        std::chrono::steady_clock::now()-start).count()));
    return result;
}
}

extern "C" char* unllm_logic_request(const char* request) {
    std::lock_guard<std::mutex> lock(mutex_);
    try {
        return ::strdup(execute(Value::parse(request ? request : "{}")).dump().c_str());
    } catch (const std::exception& error) {
        loaded_key.clear();
        auto out=Value::object();out.set("status",Value::string("error"));
        out.set("error",Value::string(error.what()));
        return ::strdup(out.dump().c_str());
    }
}
extern "C" void unllm_logic_free(char* value) { std::free(value); }
