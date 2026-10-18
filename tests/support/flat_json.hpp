#pragma once
// Parses the flat JSON objects the harness emits (string, number, null values; no nesting).
#include <cctype>
#include <cstdlib>
#include <map>
#include <stdexcept>
#include <string>

namespace bcc_test {

struct FlatValue {
  enum Kind { Str, Num, Null } kind = Null;
  std::string s;
  double d = 0.0;
  bool is_int = false;  // the literal had no fraction or exponent
};

using FlatJson = std::map<std::string, FlatValue>;

inline FlatJson parseFlatJson(const std::string& t) {
  FlatJson out;
  std::size_t i = 0;
  auto ws = [&] {
    while (i < t.size() && std::isspace(static_cast<unsigned char>(t[i]))) ++i;
  };
  auto str = [&] {
    if (t.at(i) != '"') throw std::runtime_error("expected string at " + std::to_string(i));
    std::string r;
    for (++i; t.at(i) != '"'; ++i) {
      if (t[i] == '\\') ++i;
      r.push_back(t[i]);
    }
    ++i;
    return r;
  };
  ws();
  if (t.at(i++) != '{') throw std::runtime_error("expected {");
  ws();
  while (t.at(i) != '}') {
    const std::string key = str();
    ws();
    if (t.at(i++) != ':') throw std::runtime_error("expected :");
    ws();
    FlatValue v;
    if (t[i] == '"') {
      v.kind = FlatValue::Str;
      v.s = str();
    } else if (t.compare(i, 4, "null") == 0) {
      i += 4;
    } else {
      std::size_t j = i;
      while (j < t.size() && (std::isdigit(static_cast<unsigned char>(t[j])) || t[j] == '-' || t[j] == '+' ||
                              t[j] == '.' || t[j] == 'e' || t[j] == 'E'))
        ++j;
      const std::string lit = t.substr(i, j - i);
      v.kind = FlatValue::Num;
      v.d = std::strtod(lit.c_str(), nullptr);
      v.is_int = lit.find_first_of(".eE") == std::string::npos;
      i = j;
    }
    out[key] = v;
    ws();
    if (t.at(i) == ',') {
      ++i;
      ws();
    }
  }
  return out;
}

}  // namespace bcc_test
