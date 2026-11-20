#include "keyed_latch.hpp"
#include "tab_rows.hpp"
#include "fifo_pairs.hpp"
#include "dependency_waves.hpp"
#include "json_sequence.hpp"
#include "gzip_stream.hpp"
#include "unique_fifo.hpp"
#include <cassert>
#include <iterator>
#include <sstream>

// Run in a fresh writable directory. All fixed values here test component contracts.
int main() {
    unllm::KeyedLatch latch;
    assert(latch.observe("left", unllm::LatchEvent::trigger));
    assert(!latch.observe("left", unllm::LatchEvent::hold));
    assert(latch.observe("right", unllm::LatchEvent::trigger));
    assert(!latch.observe("left", unllm::LatchEvent::trigger));
    assert(!latch.observe("left", unllm::LatchEvent::rearm));
    assert(latch.observe("left", unllm::LatchEvent::trigger));
    std::ofstream("records") << "east\t\t1\t\r\nwest\t2";
    std::vector<std::vector<std::string>> rows;
    unllm::tab_rows("records", [&](auto pos, const auto& fields) {
        assert(pos == rows.size() + 1);
        rows.push_back(fields);
    });
    assert((rows == std::vector<std::vector<std::string>>{{"east","","1",""},{"west","2"}}));
    unllm::FifoPairs<std::string> pairs;
    pairs.observe("x", true, 1); pairs.observe("x", true, 2); pairs.observe("y", false, 3);
    pairs.observe("x", false, 4);
    assert((pairs.unmatched() == std::set<std::size_t>{2,3}));
    auto waves = unllm::dependency_waves({{"z",{}},{"x",{}},{"a",{"x","z"}}});
    assert((waves == std::vector<std::vector<std::string>>{{"x","z"},{"a"}}));
    for (const auto& bad : {unllm::Prerequisites{{"a",{"missing"}}},unllm::Prerequisites{{"a",{"a"}}}}) {
        bool rejected = false;
        try { unllm::dependency_waves(bad); } catch (const std::invalid_argument&) { rejected = true; }
        assert(rejected);
    }
    unllm::UniqueFifo<int> fifo;
    assert(fifo.push(5) && !fifo.push(5) && fifo.push(7) && fifo.cancel(5) && fifo.push(5));
    assert(fifo.pop() == 7 && fifo.pop() == 5 && !fifo.pop());
    unllm::write_json_sequence("out.json",std::vector<std::string>{"a\"b",""});
    std::ifstream json("out.json");
    std::string text;
    std::getline(json,text);
    assert(text == "[\"a\\\"b\",\"\"]");
    for (const char* mode : {"wb","ab"}) {
        gzFile file = gzopen("members.gz",mode);
        assert(file && gzwrite(file,"a\0b",3)==3 && gzclose(file)==Z_OK);
    }
    unllm::expand_gzip("members.gz","raw");
    std::ifstream raw("raw",std::ios::binary);
    std::ostringstream decoded;
    decoded << raw.rdbuf();
    assert(decoded.str() == std::string("a\0ba\0b",6));
    std::ofstream("invalid.gz") << "not a gzip member";
    bool rejected = false;
    try { unllm::expand_gzip("invalid.gz","bad-out"); } catch (const std::runtime_error&) { rejected=true; }
    assert(rejected);
}
