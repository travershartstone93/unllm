#include "unllm/local_model.h"

#include <netinet/in.h>
#include <netinet/tcp.h>
#include <sys/socket.h>
#include <unistd.h>

#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

namespace unllm {

namespace {

// The prompt the reader gets: the one measured in the bench (rerank_4b50.py).
const char* PROMPT_HEAD = "Answer the question using ONLY the numbered passages below; do not use your own knowledge.\n\n"
                          "Question: ";
const char* PROMPT_TAIL = "\n\nGive the shortest exact answer as it is stated in a passage, and that passage's number. "
                          "If no passage states the answer, reply with answer \"NONE\" and passage 0.\n"
                          "Reply with only JSON: {\"answer\": \"...\", \"passage\": N}";

std::string json_escape(const std::string& s) {
    std::string out;
    out.reserve(s.size() + 16);
    for (unsigned char c : s) {
        switch (c) {
        case '"': out += "\\\""; break;
        case '\\': out += "\\\\"; break;
        case '\n': out += "\\n"; break;
        case '\r': out += "\\r"; break;
        case '\t': out += "\\t"; break;
        default:
            if (c < 0x20) {
                char buf[8];
                std::snprintf(buf, sizeof buf, "\\u%04x", c);
                out += buf;
            } else {
                out += char(c);
            }
        }
    }
    return out;
}

void put_utf8(std::string& out, unsigned cp) {
    if (cp < 0x80) out += char(cp);
    else if (cp < 0x800) {
        out += char(0xC0 | (cp >> 6));
        out += char(0x80 | (cp & 0x3F));
    } else if (cp < 0x10000) {
        out += char(0xE0 | (cp >> 12));
        out += char(0x80 | ((cp >> 6) & 0x3F));
        out += char(0x80 | (cp & 0x3F));
    } else {
        out += char(0xF0 | (cp >> 18));
        out += char(0x80 | ((cp >> 12) & 0x3F));
        out += char(0x80 | ((cp >> 6) & 0x3F));
        out += char(0x80 | (cp & 0x3F));
    }
}

// One HTTP POST to localhost, the whole reply body back; "" on any failure.
std::string http_post(int port, const std::string& path, const std::string& body, int timeout_s) {
    int fd = ::socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) return "";
    struct timeval tv {};
    tv.tv_sec = timeout_s;
    setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof tv);
    setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof tv);
    sockaddr_in addr {};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(uint16_t(port));
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    if (::connect(fd, reinterpret_cast<sockaddr*>(&addr), sizeof addr) != 0) {
        ::close(fd);
        return "";
    }
    std::string req = "POST " + path + " HTTP/1.1\r\nHost: 127.0.0.1\r\nContent-Type: application/json\r\n"
                      "Connection: close\r\nContent-Length: " + std::to_string(body.size()) + "\r\n\r\n" + body;
    for (std::size_t sent = 0; sent < req.size();) {
        ssize_t n = ::send(fd, req.data() + sent, req.size() - sent, MSG_NOSIGNAL);
        if (n <= 0) {
            ::close(fd);
            return "";
        }
        sent += std::size_t(n);
    }
    std::string reply;
    char buf[16384];
    for (ssize_t n; (n = ::recv(fd, buf, sizeof buf, 0)) > 0;) reply.append(buf, std::size_t(n));
    ::close(fd);
    const auto hdr_end = reply.find("\r\n\r\n");
    if (hdr_end == std::string::npos) return "";
    std::string head = reply.substr(0, hdr_end), rest = reply.substr(hdr_end + 4);
    if (head.rfind("HTTP/1.1 200", 0) != 0 && head.rfind("HTTP/1.0 200", 0) != 0) return "";
    for (char& c : head) c = char(std::tolower(static_cast<unsigned char>(c)));
    if (head.find("transfer-encoding: chunked") == std::string::npos) return rest;
    // Chunked: size lines in hex, each followed by that many bytes.
    std::string out;
    std::size_t at = 0;
    while (at < rest.size()) {
        const auto eol = rest.find("\r\n", at);
        if (eol == std::string::npos) break;
        const std::size_t len = std::strtoul(rest.substr(at, eol - at).c_str(), nullptr, 16);
        if (len == 0) break;
        if (eol + 2 + len > rest.size()) break;
        out.append(rest, eol + 2, len);
        at = eol + 2 + len + 2;
    }
    return out;
}

}  // namespace

std::string json_unescape(const std::string& s) {
    std::string out;
    out.reserve(s.size());
    for (std::size_t i = 0; i < s.size(); ++i) {
        if (s[i] != '\\' || i + 1 >= s.size()) {
            out += s[i];
            continue;
        }
        const char e = s[++i];
        switch (e) {
        case 'n': out += '\n'; break;
        case 't': out += '\t'; break;
        case 'r': out += '\r'; break;
        case 'b': out += '\b'; break;
        case 'f': out += '\f'; break;
        case 'u': {
            if (i + 4 >= s.size()) return out;
            unsigned cp = unsigned(std::strtoul(s.substr(i + 1, 4).c_str(), nullptr, 16));
            i += 4;
            if (cp >= 0xD800 && cp <= 0xDBFF && i + 6 < s.size() && s[i + 1] == '\\' && s[i + 2] == 'u') {
                const unsigned lo = unsigned(std::strtoul(s.substr(i + 3, 4).c_str(), nullptr, 16));
                if (lo >= 0xDC00 && lo <= 0xDFFF) {
                    cp = 0x10000 + ((cp - 0xD800) << 10) + (lo - 0xDC00);
                    i += 6;
                }
            }
            put_utf8(out, cp);
            break;
        }
        default: out += e;  // \" \\ \/
        }
    }
    return out;
}

std::string json_field(const std::string& text, const std::string& key) {
    auto at = text.find("\"" + key + "\"");
    if (at == std::string::npos) return "";
    at = text.find(':', at);
    if (at == std::string::npos) return "";
    ++at;
    while (at < text.size() && std::isspace(static_cast<unsigned char>(text[at]))) ++at;
    if (at < text.size() && text[at] == '"') {
        std::string raw;
        for (++at; at < text.size() && text[at] != '"'; ++at) {
            if (text[at] == '\\' && at + 1 < text.size()) raw += text[at++];
            raw += text[at];
        }
        return json_unescape(raw);
    }
    std::string out;
    while (at < text.size() && (std::isalnum(static_cast<unsigned char>(text[at])) || text[at] == '.' || text[at] == '-'))
        out += text[at++];
    return out;
}

bool LocalModel::configure(const std::string& model, int port) {
    model_ = model == "off" ? "" : model;
    port_ = port;
    return enabled();
}

std::optional<ModelAnswer> LocalModel::ask(const std::string& question, const std::vector<std::string>& passages,
                                           int timeout_s) const {
    if (!enabled() || passages.empty()) return std::nullopt;
    std::string prompt = PROMPT_HEAD + question + "\n\nPassages:\n";
    for (std::size_t i = 0; i < passages.size(); ++i) {
        std::string p = passages[i];
        if (p.size() > 900) p = p.substr(0, 900);
        prompt += (i ? "\n\n[" : "[") + std::to_string(i + 1) + "] " + p;
    }
    prompt += PROMPT_TAIL;
    // Ollama's chat call, no streaming, a JSON reply forced, thinking off, deterministic.
    const std::string body = "{\"model\":\"" + json_escape(model_) + "\",\"messages\":[{\"role\":\"user\",\"content\":\"" +
                             json_escape(prompt) + "\"}],\"stream\":false,\"format\":\"json\",\"think\":false," +
                             "\"options\":{\"temperature\":0,\"num_ctx\":8192}}";
    const std::string reply = http_post(port_, "/api/chat", body, timeout_s);
    if (reply.empty()) return std::nullopt;
    const auto msg = reply.find("\"message\"");
    if (msg == std::string::npos) return std::nullopt;
    const std::string content = json_field(reply.substr(msg), "content");
    if (content.empty()) return std::nullopt;
    ModelAnswer a;
    a.answer = json_field(content, "answer");
    const std::string n = json_field(content, "passage");
    a.passage = n.empty() ? 0 : std::atoi(n.c_str());
    while (!a.answer.empty() && std::isspace(static_cast<unsigned char>(a.answer.back()))) a.answer.pop_back();
    if (a.answer == "NONE" || a.answer == "none") a.answer.clear();
    return a;
}

}  // namespace unllm
