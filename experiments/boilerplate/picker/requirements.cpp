// Annotate structured roles with explicit technology and output requirements.
#include "unllm/json.h"
#include <udpipe.h>
#include <algorithm>
#include <cctype>
#include <fstream>
#include <iostream>
#include <map>
#include <memory>
#include <set>
#include <sstream>
#include <stdexcept>
#include <vector>

using unllm::json::Value;
using namespace ufal::udpipe;
using Names = std::set<std::string>;

namespace {
std::string lower(std::string text) {
    for (char& c : text) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return text;
}
struct Term {
    std::string label;
    std::vector<std::string> words;
};
std::vector<Term> terms(const Value& mapping) {
    if (!mapping.is_object()) throw std::runtime_error("Expected a requirement vocabulary");
    std::vector<Term> result;
    for (const auto& [label, aliases] : mapping.members()) {
        if (label.empty() || !aliases.is_array()) throw std::runtime_error("Invalid aliases");
        for (const auto& alias : aliases.items()) {
            Term term{label, {}};
            std::istringstream in(lower(alias.text()));
            for (std::string word; in >> word;) term.words.push_back(word);
            if (term.words.empty()) throw std::runtime_error("Empty alias");
            result.push_back(std::move(term));
        }
    }
    return result;
}
bool matches(const sentence& s, size_t start, const Term& term) {
    if (start + term.words.size() > s.words.size()) return false;
    for (size_t j = 0; j < term.words.size(); ++j) {
        const auto& w = s.words[start+j];
        // Hard constraints require a named entity in the request. Technical tokens
        // can have unrelated English lemmas (for example the C token in C++).
        if (term.words[j] != lower(w.form)) return false;
    }
    return true;
}
bool has_case(const sentence& s, const word& w, const Names& modifiers) {
    for (int i : w.children)
        if (s.words[i].deprel == "case" && modifiers.count(lower(s.words[i].lemma))) return true;
    return false;
}
bool contextual(const sentence& s, int idx) {
    static const Names context{"from","inside","within","after","before","than","around"};
    for (; idx > 0; idx=s.words[idx].head) {
        if (has_case(s,s.words[idx],context)) return true;
        for (int i : s.words[idx].children)
            if (s.words[i].deprel=="nmod:poss") return true;
    }
    return false;
}
bool produced(const sentence& s, const word& w) {
    static const Names construction{"create","build","generate","make","declare","define","implement","provide","supply","scaffold","produce","write","add","register","set","start","bootstrap"};
    if (w.head <= 0) return false;
    const auto& parent=s.words[w.head];
    if (!construction.count(lower(parent.lemma))) return false;
    if (w.deprel=="obl" && has_case(s,w,{"as"})) return true;
    return w.deprel=="obj" && lower(parent.lemma)!="register";
}
class Interpreter {
    std::unique_ptr<model> parser;
    std::unique_ptr<input_format> tokenizer;
    std::vector<Term> frameworks, outputs;
public:
    Interpreter(const char* path, const Value& vocab)
        : parser(model::load(path)), frameworks(terms(vocab["frameworks"])), outputs(terms(vocab["outputs"])) {
        if (!parser) throw std::runtime_error("Cannot load UDPipe model");
        tokenizer.reset(parser->new_tokenizer(model::TOKENIZER_PRESEGMENTED));
        if (!tokenizer) throw std::runtime_error("Cannot load UDPipe tokenizer");
    }
    Value annotate(Value request) {
        if (!request.is_array()) throw std::runtime_error("Expected a role array");
        for (Value role : request.items()) {
            std::string text=role["text"].text();
            if (text.empty()) throw std::runtime_error("Role needs text");
            Names hard, tech, kinds;
            if (role.contains("requires")) {
                if (!role["requires"].is_array()) throw std::runtime_error("Expected requires array");
                for (const auto& x : role["requires"].items()) hard.insert(x.text());
            }
            bool ambiguous=false;
            std::replace(text.begin(),text.end(),'\n',' ');
            tokenizer->set_text(text);
            sentence s;
            std::string err;
            while (tokenizer->next_sentence(s,err)) {
                if (!parser->tag(s,model::DEFAULT,err) || !parser->parse(s,model::DEFAULT,err))
                    throw std::runtime_error(err);
                static const Names modifiers{"or","nor","not","never","without","except","excluding","instead","unlike"};
                for (size_t i=1;i<s.words.size();++i) {
                    const auto& w=s.words[i];
                    if (modifiers.count(lower(w.lemma))) ambiguous=true;
                    for (const auto& t : frameworks)
                        if (matches(s,i,t) && !contextual(s,static_cast<int>(i))) tech.insert(t.label);
                    for (const auto& t : outputs)
                        if (matches(s,i,t) && produced(s,s.words[i+t.words.size()-1])) kinds.insert(t.label);
                }
            }
            if (!err.empty()) throw std::runtime_error(err);
            if (!ambiguous) {
                if (tech.size()==1) hard.insert(*tech.begin());
                hard.insert(kinds.begin(),kinds.end());
            }
            if (!hard.empty()) {
                Value list=Value::array();
                for (const auto& name : hard) list.push(Value::string(name));
                role.set("requires",list);
            }
        }
        return request;
    }
};
}
int main(int argc,char** argv) try {
    if (argc!=3) throw std::runtime_error("Usage: brick-requirements VOCABULARY ENGLISH_UDPIPE_MODEL < role-arrays.jsonl");
    std::ifstream file(argv[1]);
    if (!file) throw std::runtime_error("Cannot load vocabulary");
    std::ostringstream contents; contents<<file.rdbuf();
    Interpreter interpreter(argv[2],Value::parse(contents.str()));
    for (std::string line; std::getline(std::cin,line);)
        std::cout<<interpreter.annotate(Value::parse(line)).dump()<<std::endl;
    return std::cin.eof()?0:1;
} catch (const std::exception& e) {
    std::cerr<<e.what()<<'\n';
    return 1;
}
