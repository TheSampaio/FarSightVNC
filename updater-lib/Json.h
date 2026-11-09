// Copyright (C) 2026 FarSightVNC contributors.
// Licensed under the GNU GPL, version 2 or (at your option) any later version.
//
// Minimal, bounded JSON reader (RFC 8259) for the GitHub Releases API
// response. Input is untrusted: depth, size and string lengths are limited and
// every malformed document is rejected (no partial results).

#pragma once

#include <cstddef>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace fsv::update {

class JsonValue {
public:
  enum class Type { Null, Bool, Number, String, Array, Object };

  Type type() const { return m_type; }
  bool isNull() const { return m_type == Type::Null; }

  // Typed accessors return the fallback when the type does not match.
  bool asBool(bool fallback = false) const;
  double asNumber(double fallback = 0.0) const;
  const std::string &asString() const; // UTF-8; empty if not a string

  const std::vector<JsonValue> &items() const; // array elements; empty otherwise
  const std::vector<std::pair<std::string, JsonValue>> &members() const;
  const JsonValue *find(std::string_view key) const; // first member; nullptr if absent

private:
  friend class JsonParser;
  Type m_type = Type::Null;
  bool m_bool = false;
  double m_number = 0.0;
  std::string m_string;
  std::vector<JsonValue> m_items;
  std::vector<std::pair<std::string, JsonValue>> m_members;
};

struct JsonLimits {
  std::size_t maxInputBytes = 4 * 1024 * 1024;
  std::size_t maxDepth = 32;
  std::size_t maxStringBytes = 1024 * 1024;
};

// Strict parse: one value, optional surrounding whitespace, UTF-8 input,
// \uXXXX escapes (including surrogate pairs; lone surrogates rejected).
std::optional<JsonValue> parseJson(std::string_view text, const JsonLimits &limits = {});

} // namespace fsv::update
