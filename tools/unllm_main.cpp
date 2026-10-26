// unllm: questions about the world, answered from the reference packs built
// on this machine (tools/unllm_ingest.py), with a calculator and honest fixed
// replies for what no reference can answer (news, opinions, new writing).
// Every answer quotes its source and says where it came from; nothing is
// invented. No network beyond the local Ollama, which reads the passages the
// search found (World::set_reranker); without it, the passages are quoted.
//
// One-shot questions go through a resident copy of this program when one is
// up (--serve, started on demand, gone after UNLLM_IDLE seconds idle), so the
// packs, the indexes and the reranker load once instead of per question.

#include <fcntl.h>
#include <poll.h>
#include <signal.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/un.h>
#include <sys/wait.h>
#include <unistd.h>

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <vector>

extern char** environ;

#include "unllm/calc.h"
#include "unllm/dialogue.h"
#include "unllm/knowledge.h"
#include "unllm/local_model.h"
#include "unllm/meaning.h"
#include "unllm/reader.h"
#include "unllm/reranker.h"
#include "unllm/unllm.h"

using namespace unllm;

namespace {

std::string home_path(const char* tail) {
    const char* h = getenv("HOME");
    return std::string(h ? h : "") + tail;
}

std::string exe_dir() {
    char buf[4096];
    ssize_t n = readlink("/proc/self/exe", buf, sizeof buf - 1);
    if (n <= 0) return ".";
    buf[n] = 0;
    std::string p(buf);
    return p.substr(0, p.rfind('/'));
}

// A data file shipped with unllm: installed next to the program, or in the
// source tree when run from a build folder.
std::string data_file(const std::string& name) {
    const std::string dir = exe_dir();
    for (const auto& p : {dir + "/../share/unllm/" + name, dir + "/../data/" + name})
        if (access(p.c_str(), R_OK) == 0) return p;
    return dir + "/../data/" + name;
}

std::string user_data() {
    const char* xdg = getenv("XDG_DATA_HOME");
    return (xdg ? std::string(xdg) : home_path("/.local/share")) + "/unllm";
}

struct Reply {
    std::string answer;
    std::vector<std::string> receipts;
    std::string branch;
    std::vector<std::string> brief = {};  // the source passages behind a reference answer, for --brief
    std::vector<std::string> top = {};    // the best passages the search ranked, for --passages
    std::string asked = {};               // a follow-up made to stand on its own: the question actually answered
};

class Session {
public:
    Session() {
        auto syn = load_synonyms(data_file("synonyms.txt"));
        if (!syn.empty()) an_.set_synonyms(std::move(syn));
        if (small_.load(data_file("vectors.bin"))) an_.set_vectors(&small_);
        rewrites_.load(data_file("rewrites.txt"));
        // Chat-speak only: typo correction is tuned to machine words and
        // would turn "crush" into "crash".
        speller_.load_chatspeak(data_file("chatspeak.txt"));
        if (followups_.load(data_file("followups.txt"))) dialogue_.set_followups(&followups_);
        calc_ready_ = calc_.load_units(data_file("units.txt"));
        calc_.load_holidays(data_file("holidays.txt"));
        replies_.load(data_file("categories.txt"));
        const char* packs = getenv("UNLLM_PACKS");
        if (lib_.open(packs ? packs : user_data() + "/packs") && !lib_.empty()) {
            // UNLLM_BOOSTS="title,section": search weights, for measuring.
            float title = 1.0f, section = 0.5f;
            if (const char* b = getenv("UNLLM_BOOSTS"); b && std::sscanf(b, "%f,%f", &title, &section) == 2)
                lib_.set_ranked_boosts(title, section);
            const Vectors* v = big_.load(user_data() + "/vectors/meaning.unl2") ? &big_ : &small_;
            world_ = std::make_unique<World>(lib_, v);
            world_->load_fact_keys(data_file("factkeys.txt"));
            world_->load_extractor(data_file("answertypes.txt"), data_file("reformulations.txt"),
                                   data_file("units.txt"));
            // The reader model, when installed (UNLLM_READER names another folder).
            const char* rd = getenv("UNLLM_READER");
            if (reader_.load(rd ? rd : user_data() + "/models/reader")) world_->set_reader(&reader_);
            // Meaning search, when a vector index (vectors/*.unlpq) and the embedding model are
            // installed; UNLLM_MEANING names the index file, UNLLM_EMBED the model folder.
            std::string index_path = getenv("UNLLM_MEANING") ? getenv("UNLLM_MEANING") : "";
            if (index_path.empty()) {
                std::error_code ec;
                for (const auto& e : std::filesystem::directory_iterator(user_data() + "/vectors", ec))
                    if (e.path().extension() == ".unlpq" && (index_path.empty() || e.path().string() < index_path))
                        index_path = e.path().string();
            }
            const char* em = getenv("UNLLM_EMBED");
            if (!index_path.empty() && meaning_.load(index_path, em ? em : user_data() + "/models/bge-small-en-v1.5"))
                world_->set_meaning(&meaning_);
            // The pipeline's reranker and reader (World::set_reranker): UNLLM_RERANKER
            // names the reranker folder; UNLLM_MODEL names the Ollama model, "off" quotes.
            const char* rr = getenv("UNLLM_RERANKER");
            if (reranker_.load(rr ? rr : user_data() + "/models/reranker")) world_->set_reranker(&reranker_);
            const char* mdl = getenv("UNLLM_MODEL");
            if (model_.configure(mdl ? mdl : "qwen3:4b")) world_->set_model(&model_);
        }
    }

    std::size_t packs() const { return lib_.packs().size(); }

    // A new conversation: the resident process starts every request from one,
    // so a question through it answers as a fresh run would.
    void fresh_conversation() {
        dialogue_.reset();
        shown_.clear();
    }
    // Chat mode: follow-up questions are made to stand on their own first.
    void set_chat(bool on) { chat_ = on; }
    // The conversation's state, swapped out and back so the resident copy can
    // keep one per chat: what was asked and shown, nothing heavy.
    struct Conversation {
        Dialogue dialogue;
        std::vector<Passage> shown;
    };
    Conversation swap_conversation(Conversation c) {
        std::swap(dialogue_, c.dialogue);
        std::swap(shown_, c.shown);
        dialogue_.set_followups(&followups_);
        return c;
    }

    Reply ask(const std::string& question, int64_t now) {
        const std::string fixed = speller_.fix(question);
        const std::string q = rewrites_.size() ? rewrites_.apply(fixed) : fixed;
        const Turn turn = dialogue_.interpret(q, now, an_);
        if (turn.kind == Turn::NoCard)
            return {"Ask me something first, for example: \"what is photosynthesis\".", {}, "followup:nocard"};
        if (turn.kind == Turn::FollowUp && turn.card) return follow_up(turn.act, *turn.card, now);

        // In a chat, a question that leans on the last exchange ("why is it so
        // cold there") is first made to stand on its own by the reader model.
        std::string asked, q2 = q;
        if (chat_ && model_.enabled() && dialogue_.last()) {
            std::vector<std::pair<std::string, std::string>> exchanges;  // the last two, oldest first
            for (std::size_t i = 2; i-- > 0;)
                if (const Card* c = dialogue_.nth(i)) exchanges.emplace_back(c->question, c->answer);
            if (auto rw = model_.rewrite(exchanges, q); rw && *rw != q) q2 = asked = *rw;
        }
        const Reading r = turn.kind == Turn::Fresh && asked.empty() ? turn.reading : an_.read(q2, now);
        Card card;
        card.question = q2;
        card.at = now;
        card.reading = r;
        card.subject = r.subject;
        Reply out = fresh(r, asked.empty() ? fixed : q2, q2, now, card);
        out.asked = asked;
        card.answer = out.answer;
        card.receipts = out.receipts;
        card.branch = out.branch;
        dialogue_.remember(card);
        return out;
    }

private:
    Reply fresh(const Reading& r, const std::string& fixed, const std::string& q, int64_t now, Card& card) {
        if (calc_ready_)
            if (auto c = calc_.evaluate(fixed, now)) return {c->text, {}, "calc"};
        const Route route = unllm::route(r);
        if (route == Route::Category) {
            std::string c = replies_.reply(frame_name(r.frame), tokenize(q));
            if (!c.empty()) return {c, {}, std::string("category:") + frame_name(r.frame)};
        }
        // Only a question that names the computer itself ("my laptop", "this
        // machine") is about it; "my brother" and "heavy" are not.
        if (route == Route::Domain && r.self_deictic && !r.intents.empty())
            return {"That is a question about this machine, and unllm on its own keeps no records of it. "
                    "ActivityScope's ask answers those.",
                    {}, "machine"};
        if (!world_)
            return {"No reference packs are installed yet, so I have nothing to look that up in. "
                    "See docs/PACKS.md for building them.",
                    {}, "none"};
        std::vector<Passage> ranked;
        WorldAnswer wa = world_->answer(r, q, &ranked);
        std::vector<std::string> top;
        for (const auto& p : ranked) {
            std::string t = p.text;
            for (char& c : t)
                if (c == '\n') c = ' ';
            if (t.size() > 3000) t = t.substr(0, t.rfind(' ', 3000)) + " ...";
            top.push_back(p.title + ": " + t);
        }
        if (wa.kind == WorldAnswer::None)
            return {"I couldn't find that in the library. Try asking it another way, or about one thing at a time.",
                    {}, "none", {}, top};
        Reply out = shown(wa, card);
        out.top = top;
        return out;
    }

    // A reference answer, its passage remembered for "tell me more".
    Reply shown(const WorldAnswer& wa, Card& card) {
        if (!wa.used.empty()) {
            shown_.push_back(wa.used[0]);
            card.world_ref = int(shown_.size());
        }
        Reply out{wa.text, wa.receipts,
                  "world:" + std::string(wa.by_model ? "model:" : "") + (wa.used.empty() ? std::string("?") : wa.used[0].pack),
                  {}};
        for (std::size_t i = 0; i < wa.used.size() && i < 3; ++i) {
            std::string t = wa.used[i].text;
            for (char& c : t)
                if (c == '\n') c = ' ';
            if (t.size() > 900) t = t.substr(0, t.rfind(' ', 900)) + " ...";
            out.brief.push_back(wa.used[i].title + ": " + t);
        }
        return out;
    }

    Reply follow_up(Act act, const Card& last, int64_t now) {
        Card card = last;
        card.at = now;
        Reply out;
        const bool world = last.world_ref > 0 && world_ && std::size_t(last.world_ref) <= shown_.size();
        switch (act) {
        case Act::Thanks:
            return {"You're welcome.", {}, "followup:thanks"};
        case Act::More:
            if (!world) {
                out = {"That's all I have on it.", {}, "followup:more"};
                break;
            }
            if (WorldAnswer wa = world_->more(shown_[last.world_ref - 1]); wa.kind != WorldAnswer::None) {
                shown_.push_back(wa.used.back());  // the next "more" continues from here
                card.world_ref = int(shown_.size());
                out = {wa.text, wa.receipts, "followup:more"};
            } else {
                out = {"That's everything that source says on it.", {}, "followup:more"};
            }
            break;
        case Act::Evidence:
            out = {last.receipts.empty() ? "That answer had no source to show: it was worked out, not looked up."
                                         : "It came from:",
                   last.receipts, "followup:evidence"};
            break;
        case Act::Why: {
            if (!world || last.subject.empty()) {
                out = {"I have no reason on record for that.", {}, "followup:why"};
                break;
            }
            Reading r = last.reading;
            r.wants = Wants::Cause;
            WorldAnswer wa = world_->answer(r, "why " + last.subject);
            if (wa.kind == WorldAnswer::None) {
                out = {"The sources I have don't say why.", {}, "followup:why"};
                break;
            }
            Reply rep = shown(wa, card);
            out = {rep.answer, rep.receipts, "followup:why"};
            break;
        }
        default:
            out = {"I can't follow that up from a reference answer. Ask it as a new question.", {}, "followup:other"};
            break;
        }
        card.answer = out.answer;
        card.receipts = out.receipts.empty() ? last.receipts : out.receipts;
        dialogue_.remember(card);
        return out;
    }

    Analyzer an_;
    Vectors small_, big_;
    Rewrites rewrites_;
    Speller speller_;
    FollowUps followups_;
    Dialogue dialogue_;
    Calc calc_;
    bool calc_ready_ = false;
    Categories replies_;
    Library lib_;
    Reader reader_;
    Meaning meaning_;
    Reranker reranker_;
    LocalModel model_;
    bool chat_ = false;
    std::unique_ptr<World> world_;
    std::vector<Passage> shown_;  // Card::world_ref - 1 indexes this
};

// What a one-shot run prints for a reply.
std::string render(const Reply& r, bool show_branch, bool show_brief, int show_top) {
    std::string out = r.answer + "\n";
    if (show_branch) out += "[branch: " + r.branch + "]\n";
    if (show_branch && !r.asked.empty()) out += "[asked: " + r.asked + "]\n";
    if (show_brief)
        for (const auto& b : r.brief) out += "[brief] " + b + "\n";
    for (int k = 0; k < show_top && k < int(r.top.size()); ++k) out += "[passage] " + r.top[k] + "\n";
    if (!r.receipts.empty()) {
        out += "\n";
        for (const auto& line : r.receipts) out += "  " + line + "\n";
    }
    return out;
}

// How long an answer took, as the last line of what is printed: everything from
// the question going in to the answer coming out, the resident copy's start
// included when it had to be started.
using Clock = std::chrono::steady_clock;
std::string with_timing(const std::string& out, Clock::time_point since) {
    char buf[48];
    std::snprintf(buf, sizeof buf, "\n  answered in %.1f s\n", std::chrono::duration<double>(Clock::now() - since).count());
    return out + buf;
}

// The resident copy: one-shot questions reach it over a socket in the user's
// runtime folder, as "header line, question line, then the UNLLM_* environment
// to the end", and get back exactly what a fresh run would print. A client set
// up differently (other UNLLM_* variables) is told so and answers on its own.
std::string socket_path() {
    if (const char* p = getenv("UNLLM_SOCKET")) return p;
    const char* rt = getenv("XDG_RUNTIME_DIR");
    return (rt ? std::string(rt) : "/tmp") + "/unllm-" + std::to_string(getuid()) + ".sock";
}

std::string env_fingerprint() {
    std::vector<std::string> v;
    for (char** e = environ; *e; ++e) {
        const std::string s = *e;
        if (s.rfind("UNLLM_", 0) != 0) continue;
        if (s.rfind("UNLLM_SOCKET=", 0) == 0 || s.rfind("UNLLM_NO_SERVER=", 0) == 0 || s.rfind("UNLLM_IDLE=", 0) == 0)
            continue;
        v.push_back(s);
    }
    std::sort(v.begin(), v.end());
    std::string out;
    for (const auto& s : v) out += s + "\n";
    return out;
}

int connect_server() {
    int fd = ::socket(AF_UNIX, SOCK_STREAM, 0);
    if (fd < 0) return -1;
    sockaddr_un addr {};
    addr.sun_family = AF_UNIX;
    const std::string path = socket_path();
    if (path.size() >= sizeof addr.sun_path) {
        ::close(fd);
        return -1;
    }
    std::strcpy(addr.sun_path, path.c_str());
    if (::connect(fd, reinterpret_cast<sockaddr*>(&addr), sizeof addr) != 0) {
        ::close(fd);
        return -1;
    }
    return fd;
}

std::string read_all(int fd) {
    std::string out;
    char buf[65536];
    for (ssize_t n; (n = ::read(fd, buf, sizeof buf)) > 0;) out.append(buf, std::size_t(n));
    return out;
}

bool write_all(int fd, const std::string& s) {
    for (std::size_t at = 0; at < s.size();) {
        ssize_t n = ::send(fd, s.data() + at, s.size() - at, MSG_NOSIGNAL);
        if (n <= 0) return false;
        at += std::size_t(n);
    }
    return true;
}

// Starts a resident copy in the background (its own session, no terminal).
void spawn_server() {
    pid_t pid = fork();
    if (pid < 0) return;
    if (pid == 0) {
        if (fork() != 0) _exit(0);  // the middle process leaves at once: no zombie for the client
        setsid();
        const char* rt = getenv("XDG_RUNTIME_DIR");
        const std::string log = (rt ? std::string(rt) : "/tmp") + "/unllm-server.log";
        int null = ::open("/dev/null", O_RDONLY);
        int out = ::open(log.c_str(), O_WRONLY | O_CREAT | O_APPEND, 0600);
        if (null >= 0) dup2(null, 0);
        if (out >= 0) {
            dup2(out, 1);
            dup2(out, 2);
        }
        execl("/proc/self/exe", "unllm", "--serve", (char*)nullptr);
        _exit(1);
    }
    int status;
    waitpid(pid, &status, 0);
}

// The one-shot question through the resident copy, started if need be; nothing
// when that fails or does not apply, and the caller answers in this process.
std::optional<std::string> via_server(const std::string& question, int64_t now, bool branch, bool brief, int top,
                                      const std::string& session = "") {
    if (getenv("UNLLM_NO_SERVER") || question.find('\n') != std::string::npos) return std::nullopt;
    int fd = connect_server();
    if (fd < 0) {
        spawn_server();
        for (int i = 0; i < 300 && fd < 0; ++i) {  // the packs and the models take a few seconds to load
            usleep(100000);
            fd = connect_server();
        }
        if (fd < 0) return std::nullopt;
    }
    std::string req = "unllm 1 now=" + std::to_string(now) + " branch=" + (branch ? "1" : "0") + " brief=" +
                      (brief ? "1" : "0") + " passages=" + std::to_string(top) +
                      (session.empty() ? "" : " session=" + session) + "\n" + question + "\n" + env_fingerprint();
    if (!write_all(fd, req)) {
        ::close(fd);
        return std::nullopt;
    }
    shutdown(fd, SHUT_WR);
    std::string reply = read_all(fd);
    ::close(fd);
    if (reply.empty() || reply.rfind("MISMATCH", 0) == 0) return std::nullopt;
    return reply;
}

// The resident copy's loop: one request per connection, gone after idle_s
// seconds without one so the GPU memory comes back.
int serve(int idle_s) {
    signal(SIGPIPE, SIG_IGN);
    const std::string path = socket_path();
    if (int other = connect_server(); other >= 0) {  // one is up already
        ::close(other);
        return 0;
    }
    unlink(path.c_str());
    int lfd = ::socket(AF_UNIX, SOCK_STREAM, 0);
    if (lfd < 0) return 1;
    sockaddr_un addr {};
    addr.sun_family = AF_UNIX;
    if (path.size() >= sizeof addr.sun_path) return 1;
    std::strcpy(addr.sun_path, path.c_str());
    const mode_t old = umask(0077);
    if (::bind(lfd, reinterpret_cast<sockaddr*>(&addr), sizeof addr) != 0 || ::listen(lfd, 16) != 0) {
        umask(old);
        return 1;
    }
    umask(old);
    Session s;
    const std::string own = env_fingerprint();
    // One conversation per chat session token, kept for half an hour after its last turn.
    struct Chat {
        Session::Conversation conversation;
        time_t last = 0;
    };
    std::map<std::string, Chat> chats;
    std::fprintf(stderr, "unllm --serve: %zu packs, listening on %s, idle limit %d s\n", s.packs(), path.c_str(), idle_s);
    while (true) {
        pollfd p{lfd, POLLIN, 0};
        const int ready = poll(&p, 1, idle_s * 1000);
        if (ready == 0) break;  // idle: leave, the next question starts a fresh copy
        if (ready < 0) continue;
        int cfd = ::accept(lfd, nullptr, nullptr);
        if (cfd < 0) continue;
        const std::string req = read_all(cfd);
        const auto nl1 = req.find('\n');
        const auto nl2 = nl1 == std::string::npos ? nl1 : req.find('\n', nl1 + 1);
        std::string reply = "MISMATCH\n";
        if (nl2 != std::string::npos && req.rfind("unllm 1 ", 0) == 0 && req.substr(nl2 + 1) == own) {
            const std::string head = req.substr(0, nl1), question = req.substr(nl1 + 1, nl2 - nl1 - 1);
            auto field = [&](const char* key, long fallback) {
                const auto at = head.find(std::string(key) + "=");
                return at == std::string::npos ? fallback : std::atol(head.c_str() + at + std::strlen(key) + 1);
            };
            const int64_t now = field("now", 0);
            std::string token;
            if (const auto at = head.find(" session="); at != std::string::npos) {
                token = head.substr(at + 9);
                token = token.substr(0, token.find(' '));
            }
            const time_t wall = time(nullptr);
            for (auto it = chats.begin(); it != chats.end();)
                it = wall - it->second.last > 1800 ? chats.erase(it) : std::next(it);
            if (token.empty()) {
                s.fresh_conversation();
                s.set_chat(false);
            } else {
                s.swap_conversation(std::move(chats[token].conversation));
                s.set_chat(true);
            }
            reply = render(s.ask(question, now ? now : int64_t(time(nullptr))), field("branch", 0) != 0,
                           field("brief", 0) != 0, int(field("passages", 0)));
            if (!token.empty()) {
                chats[token].conversation = s.swap_conversation(Session::Conversation{});
                chats[token].last = wall;
            }
        }
        write_all(cfd, reply);
        ::close(cfd);
    }
    ::close(lfd);
    unlink(path.c_str());
    return 0;
}

}  // namespace

int main(int argc, char** argv) {
    std::string question, script;
    int64_t fixed_now = 0;
    bool show_branch = false, show_brief = false, serve_mode = false;
    int show_top = 0;
    for (int i = 1; i < argc; ++i) {
        std::string a = argv[i];
        if (a == "--script" && i + 1 < argc) { script = argv[++i]; continue; }
        if (a == "--now" && i + 1 < argc) { fixed_now = std::atoll(argv[++i]); continue; }
        if (a == "--branch") { show_branch = true; continue; }
        if (a == "--brief") { show_brief = true; continue; }
        if (a == "--passages" && i + 1 < argc) { show_top = std::atoi(argv[++i]); continue; }
        if (a == "--serve") { serve_mode = true; continue; }
        if (a == "--read" && i + 1 < argc) {  // measuring: the deterministic reading's time window alone
            Analyzer an;
            auto syn = load_synonyms(data_file("synonyms.txt"));
            if (!syn.empty()) an.set_synonyms(std::move(syn));
            Vectors v;
            if (v.load(data_file("vectors.bin"))) an.set_vectors(&v);
            const std::string text = argv[++i];
            const Reading r = an.read(text, fixed_now ? fixed_now : int64_t(time(nullptr)));
            static const char* forms[] = {"open", "polar", "count"};
            static const char* judges[] = {"none", "normal", "compare", "trend", "changed"};
            std::string toks;
            for (const auto& t : tokenize(text)) toks += (toks.empty() ? "" : ",") + ("\"" + t.lower + "/" + t.norm + "\"");
            std::string extra;
            auto pair = [&](const char* name, const std::optional<TimeWindow>& w) {
                if (w) extra += ", \"" + std::string(name) + "\": {\"from\": " + std::to_string(w->from) +
                                ", \"to\": " + std::to_string(w->to) + "}";
            };
            pair("second_window", r.second_window);
            pair("compare_window", r.compare_window);
            auto field = [&](const char* name, const std::string& v) {
                if (!v.empty()) extra += ", \"" + std::string(name) + "\": \"" + v + "\"";
            };
            field("predicate", r.predicate);
            field("condition", r.condition);
            field("condition_clause", r.condition_clause);
            field("condition_thing", r.condition_thing);
            field("event_clause", r.event_clause);
            if (r.alternatives.size() == 2) extra += ", \"alternatives\": [\"" + r.alternatives[0] + "\", \"" + r.alternatives[1] + "\"]";
            if (!r.event_clause.empty())
                extra += ", \"event_span\": " + std::to_string(r.event_span) +
                         ", \"event_after\": " + (r.event_after ? "true" : "false");
            if (r.window.shifted) extra += ", \"shifted\": true";
            if (r.window.all_history) extra += ", \"all_history\": true";
            std::printf("{\"live\": %s, \"stated\": %s, \"from\": %lld, \"to\": %lld, \"intents\": %zu, \"wants\": \"%s\", "
                        "\"form\": \"%s\", \"judge\": \"%s\", \"frame\": \"%s\"%s, \"tokens\": [%s]}\n",
                        r.window.live ? "true" : "false", r.window.stated ? "true" : "false", (long long)r.window.from,
                        (long long)r.window.to, r.intents.size(), wants_name(r.wants), forms[int(r.form)], judges[int(r.judge)],
                        frame_name(r.frame), extra.c_str(), toks.c_str());
            return 0;
        }
        if (a == "--rewrite") {  // measuring: the follow-up rewrite alone; three lines on stdin, JSON out
            std::string prev_q, prev_a, q;
            std::getline(std::cin, prev_q);
            std::getline(std::cin, prev_a);
            std::getline(std::cin, q);
            LocalModel m;
            const char* mdl = getenv("UNLLM_MODEL");
            m.configure(mdl ? mdl : "qwen3:4b");
            auto r = m.rewrite({{prev_q, prev_a}}, q);
            std::printf("{\"question\": \"%s\"}\n", r ? r->c_str() : "");
            return r ? 0 : 1;
        }
        if (a == "update") {
            // Keeping the packs current (tools/unllm_update.py): "unllm update --check" asks each source whether newer
            // data exists than the packs were built from, downloading nothing. The tool is looked for in UNLLM_TOOLS,
            // beside the binary's source tree, then under the data folder.
            char exe[4096];
            const ssize_t n = readlink("/proc/self/exe", exe, sizeof exe - 1);
            const std::string bin = n > 0 ? std::string(exe, std::size_t(n)) : std::string();
            const char* home = getenv("HOME");
            const char* env = getenv("UNLLM_TOOLS");
            std::string tool;
            for (const std::string& dir : {std::string(env ? env : ""), bin.substr(0, bin.rfind('/')) + "/../tools",
                                           std::string(home ? home : "") + "/.local/share/unllm/tools"})
                if (!dir.empty() && access((dir + "/unllm_update.py").c_str(), R_OK) == 0) {
                    tool = dir + "/unllm_update.py";
                    break;
                }
            if (tool.empty()) {
                std::fprintf(stderr, "unllm update: tools/unllm_update.py not found (set UNLLM_TOOLS to the folder holding it)\n");
                return 1;
            }
            bool check = false, json = false;
            for (int j = i + 1; j < argc; ++j) {
                const std::string b = argv[j];
                check |= b == "--check";
                json |= b == "--json";
            }
            if (!check) {
                std::puts("unllm update: only the check is built so far. \"unllm update --check\" asks each source whether newer data exists.");
                return 1;
            }
            std::vector<const char*> args = {"python3", tool.c_str(), "check"};
            if (json) args.push_back("--json");
            args.push_back(nullptr);
            execvp("python3", const_cast<char* const*>(args.data()));
            std::perror("unllm update: python3");
            return 1;
        }
        if (a == "--help" || a == "-h") {
            std::puts("unllm: questions about the world, answered from reference packs on this machine.\n"
                      "  unllm what is photosynthesis\n"
                      "  unllm                     (no question: a conversation; \"tell me more\" works)\n"
                      "  unllm --script FILE       replay a conversation, one question per line\n"
                      "  unllm --branch ...        also print which part answered\n"
                      "  unllm --brief ...         also print the source passages behind the answer\n"
                      "  unllm --passages N ...    also print the N best passages the search found\n"
                      "  unllm --serve             stay resident and answer one-shot runs over a socket\n"
                      "                            (started on demand; UNLLM_IDLE seconds idle ends it, 600;\n"
                      "                            UNLLM_NO_SERVER=1 answers in the run itself)\n"
                      "  unllm update --check      ask each source whether newer data exists than the\n"
                      "                            packs were built from (no download; --json for scripts)\n"
                      "Packs: ~/.local/share/unllm/packs (UNLLM_PACKS overrides); build them with\n"
                      "tools/unllm_ingest.py. Every answer quotes its source; the local Ollama model\n"
                      "(UNLLM_MODEL, qwen3:4b; \"off\" quotes instead) reads the passages the search found.");
            return 0;
        }
        question += std::string(question.empty() ? "" : " ") + a;
    }
    auto now = [&] { return fixed_now ? fixed_now : int64_t(time(nullptr)); };
    if (serve_mode) {
        const char* idle = getenv("UNLLM_IDLE");
        return serve(idle ? std::max(std::atoi(idle), 1) : 600);
    }
    const auto t_start = Clock::now();
    if (!question.empty())
        if (auto out = via_server(question, now(), show_branch, show_brief, show_top)) {
            std::fputs(with_timing(*out, t_start).c_str(), stdout);
            return 0;
        }
    // A conversation through the resident copy: one session token for the whole chat,
    // so it starts at once and follow-ups keep their context there.
    if (question.empty() && script.empty()) {
        const std::string token = std::to_string(getpid()) + "-" + std::to_string(time(nullptr));
        const bool tty = isatty(0);
        bool served = false;
        std::string line;
        while (true) {
            if (tty) {
                if (!served) std::printf("unllm: ask anything; a blank line or quit ends it.\n");
                std::printf("> ");
                std::fflush(stdout);
            }
            if (!std::getline(std::cin, line)) return 0;
            if (line.empty() || line == "quit" || line == "exit") return 0;
            const auto t_line = Clock::now();
            auto out = via_server(line, now(), show_branch, show_brief, show_top, token);
            if (!out) break;  // no resident copy: carry on in this process below
            served = true;
            std::fputs(with_timing(*out, t_line).c_str(), stdout);
            if (tty) std::printf("\n");
        }
        Session s;
        s.set_chat(true);
        auto answer = [&](const std::string& q) {
            const auto t0 = Clock::now();
            std::fputs(with_timing(render(s.ask(q, now()), show_branch, show_brief, show_top), t0).c_str(), stdout);
        };
        answer(line);
        if (tty) std::printf("\n");
        while (true) {
            if (tty) {
                std::printf("> ");
                std::fflush(stdout);
            }
            if (!std::getline(std::cin, line)) break;
            if (line.empty() || line == "quit" || line == "exit") break;
            answer(line);
            if (tty) std::printf("\n");
        }
        return 0;
    }
    Session s;
    s.set_chat(!script.empty());
    auto answer = [&](const std::string& q) {
        const auto t0 = Clock::now();
        std::fputs(with_timing(render(s.ask(q, now()), show_branch, show_brief, show_top), t0).c_str(), stdout);
    };
    if (!script.empty()) {
        std::ifstream f(script);
        if (!f) {
            std::printf("unllm: cannot read %s\n", script.c_str());
            return 1;
        }
        std::string line;
        while (std::getline(f, line)) {
            if (line.empty() || line[0] == '#') continue;
            std::printf("> %s\n", line.c_str());
            answer(line);
            std::printf("---\n");
        }
        return 0;
    }
    answer(question);
    return 0;
}
