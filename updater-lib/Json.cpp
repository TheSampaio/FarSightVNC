// Copyright (C) 2026 FarSightVNC contributors.
// Licensed under the GNU GPL, version 2 or (at your option) any later version.

#include "Json.h"

#include "Win32Util.h"

#include <charconv>
#include <system_error>

namespace fsv::update {

namespace {

const std::string kEmptyString;
const std::vector<JsonValue> kNoItems;
const std::vector<std::pair<std::string, JsonValue>> kNoMembers;

} // namespace

bool JsonValue::asBool(bool fallback) const { return m_type == Type::Bool ? m_bool : fallback; }

double JsonValue::asNumber(double fallback) const {
  return m_type == Type::Number ? m_number : fallback;
}

const std::string &JsonValue::asString() const {
  return m_type == Type::String ? m_string : kEmptyString;
}

const std::vector<JsonValue> &JsonValue::items() const {
  return m_type == Type::Array ? m_items : kNoItems;
}

const std::vector<std::pair<std::string, JsonValue>> &JsonValue::members() const {
  return m_type == Type::Object ? m_members : kNoMembers;
}

const JsonValue *JsonValue::find(std::string_view key) const {
  if (m_type != Type::Object) {
    return nullptr;
  }
  for (const auto &member : m_members) {
    if (member.first == key) {
      return &member.second;
    }
  }
  return nullptr;
}

// Strict recursive-descent parser. Friend of JsonValue so that it can fill the
// private members directly. Every failure path returns false and the caller
// discards the partially built value.
class JsonParser {
public:
  JsonParser(std::string_view text, const JsonLimits &limits) : m_text(text), m_limits(limits) {}

  std::optional<JsonValue> run() {
    if (m_text.size() > m_limits.maxInputBytes) {
      return std::nullopt;
    }
    JsonValue root;
    skipWhitespace();
    if (!parseValue(root, 0)) {
      return std::nullopt;
    }
    skipWhitespace();
    if (m_pos != m_text.size()) {
      return std::nullopt; // trailing garbage
    }
    return root;
  }

private:
  // Bounds the memory an input of at most maxInputBytes can make us allocate
  // (every JsonValue is large compared with the 2 bytes of "0,").
  static constexpr std::size_t kMaxNodes = 250000;

  bool atEnd() const { return m_pos >= m_text.size(); }
  char peek() const { return m_text[m_pos]; }

  void skipWhitespace() {
    while (!atEnd()) {
      const char c = peek();
      if (c != ' ' && c != '\t' && c != '\n' && c != '\r') {
        break;
      }
      ++m_pos;
    }
  }

  bool consumeLiteral(std::string_view literal) {
    if (m_text.substr(m_pos, literal.size()) != literal) {
      return false;
    }
    m_pos += literal.size();
    return true;
  }

  bool parseValue(JsonValue &out, std::size_t depth) {
    if (atEnd() || ++m_nodes > kMaxNodes) {
      return false;
    }
    switch (peek()) {
    case '{':
      return parseObject(out, depth + 1);
    case '[':
      return parseArray(out, depth + 1);
    case '"':
      out.m_type = JsonValue::Type::String;
      return parseString(out.m_string);
    case 't':
      out.m_type = JsonValue::Type::Bool;
      out.m_bool = true;
      return consumeLiteral("true");
    case 'f':
      out.m_type = JsonValue::Type::Bool;
      out.m_bool = false;
      return consumeLiteral("false");
    case 'n':
      out.m_type = JsonValue::Type::Null;
      return consumeLiteral("null");
    default:
      return parseNumber(out);
    }
  }

  bool parseArray(JsonValue &out, std::size_t depth) {
    if (depth > m_limits.maxDepth) {
      return false;
    }
    out.m_type = JsonValue::Type::Array;
    ++m_pos; // '['
    skipWhitespace();
    if (!atEnd() && peek() == ']') {
      ++m_pos;
      return true;
    }
    for (;;) {
      out.m_items.emplace_back();
      skipWhitespace();
      if (!parseValue(out.m_items.back(), depth)) {
        return false;
      }
      skipWhitespace();
      if (atEnd()) {
        return false;
      }
      const char c = peek();
      ++m_pos;
      if (c == ']') {
        return true;
      }
      if (c != ',') {
        return false;
      }
    }
  }

  bool parseObject(JsonValue &out, std::size_t depth) {
    if (depth > m_limits.maxDepth) {
      return false;
    }
    out.m_type = JsonValue::Type::Object;
    ++m_pos; // '{'
    skipWhitespace();
    if (!atEnd() && peek() == '}') {
      ++m_pos;
      return true;
    }
    for (;;) {
      skipWhitespace();
      if (atEnd() || peek() != '"') {
        return false;
      }
      std::string key;
      if (!parseString(key)) {
        return false;
      }
      skipWhitespace();
      if (atEnd() || peek() != ':') {
        return false;
      }
      ++m_pos;
      skipWhitespace();
      out.m_members.emplace_back(std::move(key), JsonValue{});
      if (!parseValue(out.m_members.back().second, depth)) {
        return false;
      }
      skipWhitespace();
      if (atEnd()) {
        return false;
      }
      const char c = peek();
      ++m_pos;
      if (c == '}') {
        return true;
      }
      if (c != ',') {
        return false;
      }
    }
  }

  bool isDigit() const { return !atEnd() && peek() >= '0' && peek() <= '9'; }

  bool parseNumber(JsonValue &out) {
    const std::size_t start = m_pos;
    if (!atEnd() && peek() == '-') {
      ++m_pos;
    }
    if (!isDigit()) {
      return false;
    }
    if (peek() == '0') {
      ++m_pos;
      if (isDigit()) {
        return false; // leading zero
      }
    } else {
      while (isDigit()) {
        ++m_pos;
      }
    }
    if (!atEnd() && peek() == '.') {
      ++m_pos;
      if (!isDigit()) {
        return false;
      }
      while (isDigit()) {
        ++m_pos;
      }
    }
    if (!atEnd() && (peek() == 'e' || peek() == 'E')) {
      ++m_pos;
      if (!atEnd() && (peek() == '+' || peek() == '-')) {
        ++m_pos;
      }
      if (!isDigit()) {
        return false;
      }
      while (isDigit()) {
        ++m_pos;
      }
    }
    const char *first = m_text.data() + start;
    const char *last = m_text.data() + m_pos;
    double value = 0.0;
    const auto result = std::from_chars(first, last, value);
    if (result.ec != std::errc() || result.ptr != last) {
      return false; // out of range (1e999) or otherwise unparsable
    }
    out.m_type = JsonValue::Type::Number;
    out.m_number = value;
    return true;
  }

  static int hexValue(char c) {
    if (c >= '0' && c <= '9') {
      return c - '0';
    }
    if (c >= 'a' && c <= 'f') {
      return c - 'a' + 10;
    }
    if (c >= 'A' && c <= 'F') {
      return c - 'A' + 10;
    }
    return -1;
  }

  bool parseHex4(std::uint32_t &value) {
    if (m_text.size() - m_pos < 4) {
      return false;
    }
    std::uint32_t result = 0;
    for (std::size_t i = 0; i < 4; ++i) {
      const int digit = hexValue(m_text[m_pos + i]);
      if (digit < 0) {
        return false;
      }
      result = (result << 4) | static_cast<std::uint32_t>(digit);
    }
    m_pos += 4;
    value = result;
    return true;
  }

  static void appendUtf8(std::string &out, std::uint32_t cp) {
    if (cp < 0x80) {
      out.push_back(static_cast<char>(cp));
    } else if (cp < 0x800) {
      out.push_back(static_cast<char>(0xC0 | (cp >> 6)));
      out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
    } else if (cp < 0x10000) {
      out.push_back(static_cast<char>(0xE0 | (cp >> 12)));
      out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
      out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
    } else {
      out.push_back(static_cast<char>(0xF0 | (cp >> 18)));
      out.push_back(static_cast<char>(0x80 | ((cp >> 12) & 0x3F)));
      out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
      out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
    }
  }

  bool parseEscape(std::string &out) {
    if (atEnd()) {
      return false;
    }
    const char c = peek();
    ++m_pos;
    switch (c) {
    case '"':
      out.push_back('"');
      return true;
    case '\\':
      out.push_back('\\');
      return true;
    case '/':
      out.push_back('/');
      return true;
    case 'b':
      out.push_back('\b');
      return true;
    case 'f':
      out.push_back('\f');
      return true;
    case 'n':
      out.push_back('\n');
      return true;
    case 'r':
      out.push_back('\r');
      return true;
    case 't':
      out.push_back('\t');
      return true;
    case 'u':
      break;
    default:
      return false;
    }
    std::uint32_t cp = 0;
    if (!parseHex4(cp)) {
      return false;
    }
    if (cp >= 0xDC00 && cp <= 0xDFFF) {
      return false; // lone low surrogate
    }
    if (cp >= 0xD800 && cp <= 0xDBFF) {
      std::uint32_t low = 0;
      if (!consumeLiteral("\\u") || !parseHex4(low) || low < 0xDC00 || low > 0xDFFF) {
        return false; // lone high surrogate
      }
      cp = 0x10000 + ((cp - 0xD800) << 10) + (low - 0xDC00);
    }
    appendUtf8(out, cp);
    return true;
  }

  // Validates one multi-byte UTF-8 sequence starting at m_pos (lead >= 0x80)
  // and appends it. Rejects overlong forms, UTF-8 encoded surrogates and
  // code points above U+10FFFF.
  bool copyUtf8Sequence(std::string &out) {
    const auto byteAt = [this](std::size_t offset) -> int {
      return m_pos + offset < m_text.size() ? static_cast<unsigned char>(m_text[m_pos + offset])
                                            : -1;
    };
    const int lead = byteAt(0);
    std::size_t length = 0;
    int low2 = 0x80;
    int high2 = 0xBF;
    if (lead >= 0xC2 && lead <= 0xDF) {
      length = 2;
    } else if (lead >= 0xE0 && lead <= 0xEF) {
      length = 3;
      if (lead == 0xE0) {
        low2 = 0xA0;
      } else if (lead == 0xED) {
        high2 = 0x9F;
      }
    } else if (lead >= 0xF0 && lead <= 0xF4) {
      length = 4;
      if (lead == 0xF0) {
        low2 = 0x90;
      } else if (lead == 0xF4) {
        high2 = 0x8F;
      }
    } else {
      return false;
    }
    const int second = byteAt(1);
    if (second < low2 || second > high2) {
      return false;
    }
    for (std::size_t i = 2; i < length; ++i) {
      const int next = byteAt(i);
      if (next < 0x80 || next > 0xBF) {
        return false;
      }
    }
    out.append(m_text.data() + m_pos, length);
    m_pos += length;
    return true;
  }

  bool parseString(std::string &out) {
    ++m_pos; // opening quote
    for (;;) {
      if (atEnd()) {
        return false; // unterminated
      }
      const unsigned char c = static_cast<unsigned char>(peek());
      if (c == '"') {
        ++m_pos;
        return true;
      }
      if (c < 0x20) {
        return false; // raw control character
      }
      if (c == '\\') {
        ++m_pos;
        if (!parseEscape(out)) {
          return false;
        }
      } else if (c < 0x80) {
        out.push_back(static_cast<char>(c));
        ++m_pos;
      } else if (!copyUtf8Sequence(out)) {
        return false;
      }
      if (out.size() > m_limits.maxStringBytes) {
        return false;
      }
    }
  }

  std::string_view m_text;
  const JsonLimits &m_limits;
  std::size_t m_pos = 0;
  std::size_t m_nodes = 0;
};

std::optional<JsonValue> parseJson(std::string_view text, const JsonLimits &limits) {
  return guarded([&]() -> std::optional<JsonValue> { return JsonParser(text, limits).run(); },
                 std::optional<JsonValue>{});
}

} // namespace fsv::update
