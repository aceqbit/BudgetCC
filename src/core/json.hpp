#pragma once
// Minimal JSON object builder. Numbers go through fmt, so output is locale independent and the
// shortest representation that round-trips; non-finite doubles become null.
#include <fmt/format.h>

#include <cmath>
#include <cstdint>
#include <iterator>
#include <string>
#include <string_view>

namespace bcc {

inline void appendJsonString(std::string& out, std::string_view v) {
  out.push_back('"');
  for (unsigned char c : v) {
    switch (c) {
      case '"': out += "\\\""; break;
      case '\\': out += "\\\\"; break;
      case '\n': out += "\\n"; break;
      case '\r': out += "\\r"; break;
      case '\t': out += "\\t"; break;
      default:
        if (c < 0x20) {
          fmt::format_to(std::back_inserter(out), "\\u{:04x}", c);
        } else {
          out.push_back(static_cast<char>(c));
        }
    }
  }
  out.push_back('"');
}

class JsonObject {
 public:
  JsonObject() : s_("{") {}

  JsonObject& str(std::string_view key, std::string_view v) {
    k(key);
    appendJsonString(s_, v);
    return *this;
  }
  JsonObject& i64(std::string_view key, std::int64_t v) {
    k(key);
    fmt::format_to(std::back_inserter(s_), "{}", v);
    return *this;
  }
  JsonObject& u64(std::string_view key, std::uint64_t v) {
    k(key);
    fmt::format_to(std::back_inserter(s_), "{}", v);
    return *this;
  }
  JsonObject& num(std::string_view key, double v) {
    k(key);
    if (std::isfinite(v)) {
      fmt::format_to(std::back_inserter(s_), "{}", v);
    } else {
      s_ += "null";
    }
    return *this;
  }
  JsonObject& boolean(std::string_view key, bool v) {
    k(key);
    s_ += v ? "true" : "false";
    return *this;
  }
  JsonObject& null(std::string_view key) {
    k(key);
    s_ += "null";
    return *this;
  }
  // `json` must already be valid JSON (an object, array or literal).
  JsonObject& raw(std::string_view key, std::string_view json) {
    k(key);
    s_ += json;
    return *this;
  }
  std::string finish() const { return s_ + "}"; }

 private:
  void k(std::string_view key) {
    if (!first_) s_.push_back(',');
    first_ = false;
    appendJsonString(s_, key);
    s_.push_back(':');
  }
  std::string s_;
  bool first_ = true;
};

}  // namespace bcc
