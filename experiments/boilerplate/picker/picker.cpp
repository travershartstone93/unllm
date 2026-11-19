#include "unllm/json.h"
#include <curl/curl.h>
#include <xapian.h>

#include <algorithm>
#include <fstream>
#include <iostream>
#include <limits>
#include <map>
#include <memory>
#include <numeric>
#include <set>
#include <sstream>
#include <string_view>
#include <tuple>
#ifdef UNLLM_HAVE_ONNX
#include "unllm/meaning.h"
#endif

using unllm::json::Value;
using Names = std::set<std::string>;

namespace {
Value load(const char* path) {
    std::ifstream input(path);
    if (!input) throw std::runtime_error(std::string("Cannot open ") + path);
    std::ostringstream stream;
    stream << input.rdbuf();
    return Value::parse(stream.str());
}

Names strings(const Value& array) {
    if (!array.is_array()) throw std::runtime_error("Expected string array");
    Names result;
    for (const auto& entry : array.items()) {
        if (entry.text().empty()) throw std::runtime_error("Expected nonempty string");
        result.insert(entry.text());
    }
    return result;
}

struct Entry {
    std::string id, text;
    Names tags;
    Names outputs;
    std::string searchable() const {
        std::string combined = text;
        for (const auto& output : outputs) combined += " " + output;
        return combined;
    }
};

std::vector<Entry> entries(const Value& array, const char* text, const char* tags) {
    if (!array.is_array()) throw std::runtime_error("Expected an array of entries");
    std::vector<Entry> result;
    Names ids;
    for (const auto& item : array.items()) {
        Entry entry{item["id"].text(), item[text].text(), {}, {}};
        if (entry.id.empty() || entry.text.empty() || !ids.insert(entry.id).second)
            throw std::runtime_error("Expected a unique ID and nonempty description");
        if (item.contains(tags)) entry.tags = strings(item[tags]);
        if (std::string_view(text) == "description" && item.contains("outputs"))
            entry.outputs = strings(item["outputs"]);
        result.push_back(std::move(entry));
    }
    return result;
}

bool eligible(const Entry& card, const Entry& role) {
    return std::includes(card.tags.begin(), card.tags.end(), role.tags.begin(), role.tags.end());
}

class Tokenizer {
    Xapian::SimpleStopper stop;
    Xapian::TermGenerator generator;
public:
    Tokenizer() {
        std::istringstream words("a an the and or of with from to into by in on one for as that its is at");
        for (std::string word; words >> word;) stop.add(word);
        generator.set_stemmer(Xapian::Stem("english"));
        generator.set_stemming_strategy(Xapian::TermGenerator::STEM_ALL);
        generator.set_stopper(&stop);
        generator.set_stopper_strategy(Xapian::TermGenerator::STOP_ALL);
    }
    Xapian::Document document(const std::string& text) {
        Xapian::Document doc;
        generator.set_document(doc);
        generator.index_text(text);
        return doc;
    }
};

Value lexical(const std::vector<Entry>& cards, const std::vector<Entry>& roles, bool cap_overlap) {
    Tokenizer tokenizer;
    Xapian::WritableDatabase db("", Xapian::DB_BACKEND_INMEMORY);
    for (const auto& card : cards) db.add_document(tokenizer.document(card.searchable()));

    Value bindings = Value::object();
    for (const auto& role : roles) {
        const auto query = tokenizer.document(role.text);
        Names terms(query.termlist_begin(), query.termlist_end());
        double required = 0.40 * terms.size();
        if (cap_overlap) required = std::min(3.0, required);
        Xapian::Enquire search(db);
        search.set_query(Xapian::Query(Xapian::Query::OP_OR, terms.begin(), terms.end()));
        const auto hits = search.get_mset(0, cards.size());
        double best = 0;
        Value match;
        for (auto it = hits.begin(); it != hits.end(); ++it) {
            const auto& card = cards.at(*it - 1);
            if (!eligible(card, role)) continue;
            const auto doc = it.get_document();
            size_t overlap = 0;
            for (auto term = doc.termlist_begin(); term != doc.termlist_end(); ++term)
                overlap += terms.count(*term);
            if (overlap < required) continue;
            if (best > 0) {
                if (it.get_weight() >= 0.85 * best) match = Value();
                break;
            }
            best = it.get_weight();
            match = Value::string(card.id);
        }
        bindings.set(role.id, match);
    }
    return bindings;
}

struct Features {
    Names terms;
    std::string first;
};

Features features(Tokenizer& tokenizer, const std::string& text) {
    Features f;
    const auto doc = tokenizer.document(text);
    auto earliest = std::numeric_limits<Xapian::termpos>::max();
    for (auto t = doc.termlist_begin(); t != doc.termlist_end(); ++t) {
        f.terms.insert(*t);
        auto pos = t.positionlist_begin();
        if (pos != t.positionlist_end() && *pos < earliest) {
            earliest = *pos;
            f.first = *t;
        }
    }
    return f;
}

double similarity(const Features& query, const Features& example) {
    size_t shared = 0;
    for (const auto& term : query.terms) shared += example.terms.count(term);
    if (shared < 2) return 0;
    const bool same_first = !query.first.empty() && query.first == example.first;
    return 2.0 * (shared + same_first) / (query.terms.size() + example.terms.size() + 2);
}

// Negative examples describe gaps in a particular shortlist. A changed card can fill a gap.
using CatalogSignature = std::map<std::string, std::tuple<std::string, Names, Names>>;
CatalogSignature signature(const std::vector<Entry>& cards) {
    CatalogSignature result;
    for (const auto& c : cards) result.emplace(c.id, std::make_tuple(c.text, c.tags, c.outputs));
    return result;
}

Value reviewed(const std::vector<Entry>& cards, const std::vector<Entry>& roles, const Value& memory,
               bool guard) {
    const auto known = signature(cards);
    if (signature(entries(memory["catalog"], "description", "tags")) != known)
        throw std::runtime_error("Reviewed examples belong to a different catalog");
    if (!memory["examples"].is_array() || memory["examples"].size() == 0)
        throw std::runtime_error("Expected a nonempty array of reviewed examples");
    Tokenizer tokenizer;
    Names positive_starts;
    std::map<std::string, Names> card_terms;
    for (const auto& c : cards) card_terms[c.id] = features(tokenizer, c.searchable()).terms;
    std::vector<std::pair<Features, std::string>> examples;
    for (const auto& e : memory["examples"].items()) {
        auto brick = e["brick"];
        if (e["text"].text().empty() || !e.contains("brick") ||
            (!brick.null() && !known.count(brick.text())))
            throw std::runtime_error("Invalid reviewed example");
        auto f = features(tokenizer, e["text"].text());
        if (!brick.null() && !f.first.empty()) positive_starts.insert(f.first);
        examples.emplace_back(std::move(f), brick.text());
    }
    Value bindings = lexical(cards, roles, true);
    for (const auto& role : roles) {
        const auto q = features(tokenizer, role.text);
        Names allowed;
        for (const auto& c : cards)
            if (eligible(c, role)) allowed.insert(c.id);
        std::map<std::string, double> scores;
        for (const auto& [f, id] : examples) {
            if (!id.empty() && !allowed.count(id)) continue;
            scores[id] = std::max(scores[id], similarity(q, f));
        }
        std::string winner;
        double best = 0, second = 0;
        for (const auto& [id, score] : scores) {
            if (score > best) {
                second = best;
                best = score;
                winner = id;
            } else {
                second = std::max(second, score);
            }
        }
        if (best >= 0.50 && best - second >= 0.10) {
            bindings.set(role.id, winner.empty() ? Value() : Value::string(winner));
        } else if (guard) {
            const auto id = bindings[role.id].text();
            if (!id.empty() && !card_terms.at(id).count(q.first) && !positive_starts.count(q.first))
                bindings.set(role.id, Value());
        }
    }
    return bindings;
}

size_t receive(char* data, size_t size, size_t count, void* output) {
    const size_t bytes = size * count;
    static_cast<std::string*>(output)->append(data, bytes);
    return bytes;
}

Value generate(const Value& request) {
    const std::string body = request.dump();
    std::string response;
    std::unique_ptr<CURL, decltype(&curl_easy_cleanup)> curl(curl_easy_init(), curl_easy_cleanup);
    if (!curl) throw std::runtime_error("Cannot initialize curl");
    std::unique_ptr<curl_slist, decltype(&curl_slist_free_all)> headers(
        curl_slist_append(nullptr, "Content-Type: application/json"), curl_slist_free_all);
    curl_easy_setopt(curl.get(), CURLOPT_URL, "http://127.0.0.1:11434/api/generate");
    curl_easy_setopt(curl.get(), CURLOPT_NOPROXY, "*");
    curl_easy_setopt(curl.get(), CURLOPT_POSTFIELDS, body.c_str());
    curl_easy_setopt(curl.get(), CURLOPT_HTTPHEADER, headers.get());
    curl_easy_setopt(curl.get(), CURLOPT_WRITEFUNCTION, receive);
    curl_easy_setopt(curl.get(), CURLOPT_WRITEDATA, &response);
    curl_easy_setopt(curl.get(), CURLOPT_FAILONERROR, 1L);
    curl_easy_setopt(curl.get(), CURLOPT_TIMEOUT, 90L);
    auto rc = curl_easy_perform(curl.get());
    if (rc != CURLE_OK) throw std::runtime_error(curl_easy_strerror(rc));
    Value answer = Value::parse(response);
    if (!answer["done"].flag() || answer["done_reason"].text() != "stop")
        throw std::runtime_error("Incomplete model response");
    return answer;
}

Value model_request(const std::string& model, const Value& catalog, const Value& request,
                    const std::vector<Entry>& cards, const std::vector<Entry>& roles) {
    Value schema = Value::parse(R"({"type":"object","additionalProperties":false})");
    Value required = Value::array(), properties = Value::object();
    for (const auto& role : roles) {
        Value choices = Value::array();
        choices.push(Value());
        for (const auto& card : cards)
            if (eligible(card, role)) choices.push(Value::string(card.id));
        Value field = Value::object();
        field.set("enum", choices);
        properties.set(role.id, field);
        required.push(Value::string(role.id));
    }
    schema.set("properties", properties);
    schema.set("required", required);
    Value body = Value::parse(R"({"stream":false,"think":false,"options":{"temperature":0,"num_predict":512}})");
    body.set("model", Value::string(model));
    body.set("format", schema);
    body.set("prompt", Value::string(
        "For each requested C++ role return its ID as a key and a single compatible brick ID "
        "as its value. Use null when no card provides the requested capability. A related "
        "topic alone is insufficient. Return only the JSON mapping.\nCards: " + catalog.dump()
        + "\nRoles: " + request.dump()));
    return body;
}

Value resolve(const std::vector<Entry>& cards, const std::vector<Entry>& roles, const Value& bindings) {
    if (!bindings.is_object() || bindings.size() != roles.size())
        throw std::runtime_error("Expected exactly one binding per role");
    Value selected = Value::array(), missing = Value::array(), result = Value::object();
    Names seen;
    for (const auto& role : roles) {
        if (!bindings.contains(role.id)) throw std::runtime_error("Missing binding for " + role.id);
        auto bound = bindings[role.id];
        if (bound.null()) {
            missing.push(Value::string(role.id));
            continue;
        }
        auto found = std::find_if(cards.begin(), cards.end(), [&](const Entry& c) { return c.id == bound.text(); });
        if (found == cards.end() || !eligible(*found, role))
            throw std::runtime_error("Unknown or incompatible brick for " + role.id);
        if (seen.insert(found->id).second) selected.push(bound);
    }
    result.set("bindings", bindings);
    result.set("selected", selected);
    result.set("missing", missing);
    return result;
}

#ifdef UNLLM_HAVE_ONNX
class ExampleVectors {
    unllm::Meaning encoder;
    std::map<std::string, std::vector<std::vector<float>>> examples;

    std::vector<float> encode(const std::string& text) const {
        auto vector = encoder.embed(text, false);
        if (vector.empty()) throw std::runtime_error("BGE returned no sentence vector");
        return vector;
    }
public:
    ExampleVectors(const std::vector<Entry>& cards, const Value& reviews, const char* model_dir) {
        if (!encoder.load_model(model_dir)) throw std::runtime_error("Cannot load the BGE encoder");
        std::map<std::string, Names> texts;
        for (const auto& card : cards) {
            texts[card.id] = card.outputs;
            texts[card.id].insert(card.text);
        }
        for (const auto& e : reviews["examples"].items())
            texts[e["brick"].text()].insert(e["text"].text());
        for (const auto& [id, samples] : texts)
            for (const auto& sample : samples) examples[id].push_back(encode(sample));
    }

    Value match(const std::vector<Entry>& cards, const Entry& role, bool corroborate) const {
        // The empty label represents reviewed examples for which no brick was suitable.
        Names allowed{""};
        for (const auto& card : cards)
            if (eligible(card, role)) allowed.insert(card.id);
        auto query = encode(role.text);
        double best = -1, runner_up = -1;
        unsigned support = 0;
        std::string winner;
        for (const auto& [id, vectors] : examples) {
            if (!allowed.count(id)) continue;
            double score = -1;
            unsigned count = 0;
            for (const auto& v : vectors) {
                double similarity = std::inner_product(v.begin(), v.end(), query.begin(), 0.0);
                score = std::max(score, similarity);
                if (similarity >= 0.75) ++count;
            }
            if (score > best) {
                winner = id;
                runner_up = best;
                best = score;
                support = count;
            } else {
                runner_up = std::max(runner_up, score);
            }
        }
        constexpr double minimum_cosine = 0.85;  // Fixed on B13 development cases.
        const bool accepted = best >= minimum_cosine ||
            (corroborate && support >= 2 && best - runner_up >= 0.05);
        return accepted && best > runner_up && !winner.empty() ? Value::string(winner) : Value();
    }
};

void guarded_bge(const std::vector<Entry>& cards, const char* roles_path,
                 const Value& reviews, const char* model_dir, bool corroborate) {
    std::unique_ptr<ExampleVectors> index;
    auto pick = [&](const Value& request) {
        auto roles = entries(request, "text", "requires");
        Value first = reviewed(cards, roles, reviews, true);
        Value bindings = first.clone();
        for (const auto& role : roles) {
            if (!first[role.id].null()) continue;
            if (!index) index = std::make_unique<ExampleVectors>(cards, reviews, model_dir);
            bindings.set(role.id, index->match(cards, role, corroborate));
        }
        Value result = resolve(cards, roles, bindings);
        result.set("first_pass", first);
        std::cout << result.dump() << std::endl;
    };
    if (std::string(roles_path) != "-") {
        pick(load(roles_path));
        return;
    }
    std::string line;
    while (std::getline(std::cin, line)) pick(Value::parse(line));
    if (!std::cin.eof()) throw std::runtime_error("Failed reading roles from stdin");
}
#endif
} // namespace

int main(int argc, char** argv) try {
    if (argc < 4 || argc > 6)
        throw std::runtime_error("Usage: brick-picker MODE CATALOG ROLES [REVIEWS|MODEL|BINDINGS] [MODEL]");
    std::string mode = argv[1];
    Value catalog = load(argv[2]);
    auto cards = entries(catalog, "description", "tags");
    if ((mode == "guarded-bge" || mode == "evidence-bge") && argc == 6) {
#ifdef UNLLM_HAVE_ONNX
        guarded_bge(cards, argv[3], load(argv[4]), argv[5], mode == "evidence-bge");
        return 0;
#else
        throw std::runtime_error("guarded-bge requires a picker built with ONNX Runtime");
#endif
    }
    Value request = load(argv[3]);
    auto roles = entries(request, "text", "requires");
    Value bindings, answer, first_pass;
    if ((mode == "bm25" || mode == "bm25-capped") && argc == 4)
        bindings = lexical(cards, roles, mode == "bm25-capped");
    else if ((mode == "reviewed" || mode == "reviewed-guard") && argc == 5)
        bindings = reviewed(cards, roles, load(argv[4]), mode == "reviewed-guard");
    else if (mode == "resolve" && argc == 5) bindings = load(argv[4]);
    else if (mode == "ollama" && argc == 5) {
        answer = generate(model_request(argv[4], catalog, request, cards, roles));
        bindings = Value::parse(answer["response"].text());
    } else if ((mode == "fill-gaps" || mode == "review-all" || mode == "review-proposals") && argc == 6) {
        first_pass = mode == "review-proposals" ? load(argv[4]) : reviewed(cards, roles, load(argv[4]), true);
        resolve(cards, roles, first_pass);
        bindings = first_pass.clone();
        Value pending = Value::array();
        for (const auto& item : request.items())
            if (mode != "fill-gaps" || first_pass[item["id"].text()].null()) pending.push(item);
        if (pending.size() > 0) {
            const auto pending_roles = entries(pending, "text", "requires");
            Value body = model_request(argv[5], catalog, pending, cards, pending_roles);
            if (mode != "fill-gaps")
                body.set("prompt", Value::string(body["prompt"].text() +
                    "\nCheck the provisional assignments below against the cards, including roles marked null. "
                    "Keep correct assignments and replace any mistaken ID or null.\nProvisional: " + first_pass.dump()));
            answer = generate(body);
            Value updates = Value::parse(answer["response"].text());
            resolve(cards, pending_roles, updates);
            for (const auto& [id, value] : updates.members()) bindings.set(id, value);
        }
    } else throw std::runtime_error("Invalid mode or arguments");
    Value result = resolve(cards, roles, bindings);
    if (!first_pass.null()) result.set("first_pass", first_pass);
    if (!answer.null()) {
        result.set("model", answer["model"]);
        result.set("input_tokens", answer["prompt_eval_count"]);
        result.set("output_tokens", answer["eval_count"]);
    }
    std::cout << result.dump() << '\n';
    return 0;
} catch (const Xapian::Error& e) {
    std::cerr << e.get_description() << '\n';
    return 1;
} catch (const std::exception& e) {
    std::cerr << e.what() << '\n';
    return 1;
}
