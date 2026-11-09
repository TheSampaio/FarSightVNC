// Copyright (C) 2026 FarSightVNC contributors.
// Licensed under the GNU GPL, version 2 or (at your option) any later version.

#include "Json.h"

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

// STUB(worker): strict bounded RFC 8259 parser (JsonParser) is not written yet.
std::optional<JsonValue> parseJson(std::string_view, const JsonLimits &) { return std::nullopt; }

} // namespace fsv::update
