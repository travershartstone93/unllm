#include "unllm/symbolic/model.h"
#include <set>
#include <unicode/uchar.h>
#include <unicode/utf8.h>
namespace unllm::native {
bool identifier(const std::string &text) {
  bool first = true;
  for (int32_t i = 0; i < static_cast<int32_t>(text.size());) {
    UChar32 cp;
    U8_NEXT(text.data(), i, static_cast<int32_t>(text.size()), cp);
    if (cp < 0 ||
        !(cp == '_' || u_hasBinaryProperty(cp, first ? UCHAR_XID_START
                                                     : UCHAR_XID_CONTINUE)))
      return false;
    first = false;
  }
  return !first;
}
static bool polarity(const Json &v) {
  if (!v.is_boolean())
    throw Unsupported("Polarity must be Boolean");
  return v.flag();
}
Goal Goal::from(const Json &value) {
  fields_only(value, {"predicate", "subject", "object", "negative"});
  Goal g{required_text(value, "predicate"), required_text(value, "subject"),
         required_text(value, "object"),
         value.contains("negative") ? polarity(value["negative"]) : false};
  if (!identifier(g.predicate))
    throw Unsupported("Invalid predicate identifier");
  return g;
}
Json Goal::data() const {
  auto out = Json::object();
  out.set("predicate", Json::string(predicate));
  out.set("subject", Json::string(subject));
  out.set("object", Json::string(object));
  out.set("negative", Json::boolean(negative));
  return out;
}
Claim Claim::from(const Json &value) {
  fields_only(value,
              {"id", "predicate", "subject", "object", "negative", "valid_from",
               "valid_to", "source", "origin", "qualifiers"});
  Claim c;
  c.id = required_text(value, "id");
  c.predicate = required_text(value, "predicate");
  c.subject = required_text(value, "subject");
  c.object = required_text(value, "object");
  c.negative = value.contains("negative") ? polarity(value["negative"]) : false;
  if (!identifier(c.predicate))
    throw Unsupported("Invalid predicate identifier");
  for (auto *text : {&c.id, &c.subject, &c.object})
    if (text->find_first_of("\"\\\n\r") != std::string::npos ||
        text->find('\0') != std::string::npos)
      throw Unsupported("Claim symbols must be quote-free identifiers");
  if (c.subject.starts_with('?') || c.object.starts_with('?'))
    throw Unsupported("Source claims must be ground");
  if (value.contains("source"))
    c.source = required_text(value, "source");
  if (value.contains("origin"))
    c.origin = required_text(value, "origin");
  if (!std::set<std::string>{"source", "session", "hypothetical", "metadata"}
           .contains(c.origin))
    throw Unsupported("Invalid claim origin");
  auto bound = [&](const char *n) -> std::optional<int64_t> {
    auto v = value[n];
    if (v.null())
      return {};
    if (!v.is_integer())
      throw Unsupported("Validity bounds must be integer seconds");
    return v.integer();
  };
  c.valid_from = bound("valid_from");
  c.valid_to = bound("valid_to");
  if (c.valid_from && c.valid_to && *c.valid_from >= *c.valid_to)
    throw Unsupported("Validity must be a nonempty half-open interval");
  if (value.contains("qualifiers"))
    c.qualifiers = value["qualifiers"].clone();
  fields_only(c.qualifiers, {"source_location"});
  return c;
}
Json Claim::data() const {
  auto out = Goal{predicate, subject, object, negative}.data();
  out.set("id", Json::string(id));
  out.set("source", Json::string(source));
  out.set("origin", Json::string(origin));
  out.set("qualifiers", qualifiers.clone());
  out.set("valid_from", valid_from ? Json::number(*valid_from) : Json{});
  out.set("valid_to", valid_to ? Json::number(*valid_to) : Json{});
  return out;
}
bool Claim::active(int64_t now) const {
  return (!valid_from || *valid_from <= now) && (!valid_to || now < *valid_to);
}
Json relation(const Json &value) {
  fields_only(value,
              {"name", "domain", "range", "transitive", "inverse", "words"});
  auto name = required_text(value, "name");
  if (!identifier(name))
    throw Unsupported("Invalid relation identifier");
  auto out = Json::object();
  out.set("name", Json::string(name));
  for (auto name : {"domain", "range"})
    out.set(name, value.contains(name)
                      ? Json::string(required_text(value, name))
                      : Json::string("entity"));
  out.set("transitive", Json::boolean(value.contains("transitive")
                                          ? polarity(value["transitive"])
                                          : false));
  auto inverse =
      value.contains("inverse") ? value["inverse"] : Json::string("");
  if (!inverse.is_string() ||
      (!inverse.text().empty() && !identifier(inverse.text())))
    throw Unsupported("Invalid inverse relation");
  out.set("inverse", inverse);
  auto words = value.contains("words") ? value["words"] : Json::array();
  if (!words.is_array())
    throw Unsupported("Relation words must be an array");
  for (auto w : words.items())
    if (!w.is_string())
      throw Unsupported("Invalid relation word");
  out.set("words", words.clone());
  return out;
}
} // namespace unllm::native
