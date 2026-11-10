// Copyright (C) 2026 FarSightVNC contributors.
// Licensed under the GNU GPL, version 2 or (at your option) any later version.

#include "TestHarness.h"

#include "updater-lib/Json.h"

#include <cmath>
#include <string>

using namespace fsv::update;

namespace {

bool valid(const std::string &text) { return parseJson(text).has_value(); }

void expectInvalid(const std::string &text, const char *what) {
  if (valid(text)) {
    std::printf("  (accepted invalid JSON: %s)\n", what);
  }
  CHECK(!valid(text));
}

void testValidDocuments() {
  const auto doc = parseJson(R"({
    "name": "FarSight",
    "ok": true, "no": false, "nothing": null,
    "list": [1, -2, 3.5, 1e3, 2E-2, 0, -0, 1.5e+2],
    "nested": {"a": {"b": [ [], {} ]}},
    "empty_obj": {}, "empty_arr": [],
    "dup": 1, "dup": 2
  })");
  CHECK(doc.has_value());
  if (!doc) {
    return;
  }
  CHECK(doc->type() == JsonValue::Type::Object);
  CHECK(doc->find("name") != nullptr && doc->find("name")->asString() == "FarSight");
  CHECK(doc->find("ok")->asBool(false));
  CHECK(!doc->find("no")->asBool(true));
  CHECK(doc->find("nothing")->isNull());
  CHECK(doc->find("missing") == nullptr);
  CHECK(doc->find("dup")->asNumber() == 1.0); // first member wins

  const JsonValue *list = doc->find("list");
  CHECK(list != nullptr && list->type() == JsonValue::Type::Array && list->items().size() == 8);
  if (list != nullptr && list->items().size() == 8) {
    CHECK(list->items()[0].asNumber() == 1.0);
    CHECK(list->items()[1].asNumber() == -2.0);
    CHECK(list->items()[2].asNumber() == 3.5);
    CHECK(list->items()[3].asNumber() == 1000.0);
    CHECK(std::fabs(list->items()[4].asNumber() - 0.02) < 1e-12);
    CHECK(list->items()[7].asNumber() == 150.0);
  }
  const JsonValue *inner = doc->find("nested")->find("a")->find("b");
  CHECK(inner != nullptr && inner->items().size() == 2);
  CHECK(doc->find("empty_obj")->members().empty());
  CHECK(doc->find("empty_arr")->items().empty());
  CHECK(doc->members().size() == 10);

  // Surrounding whitespace is allowed; scalars are valid top-level values.
  CHECK(valid(" \t\r\n[1]\n "));
  CHECK(valid("42"));
  CHECK(valid("\"x\""));
  CHECK(valid("null"));
}

void testAccessorsFallBack() {
  const auto doc = parseJson(R"({"s":"x","n":5,"a":[1]})");
  CHECK(doc.has_value());
  if (!doc) {
    return;
  }
  CHECK(doc->find("s")->asNumber(7.0) == 7.0);
  CHECK(doc->find("n")->asString().empty());
  CHECK(doc->find("n")->asBool(true));
  CHECK(doc->find("n")->items().empty());
  CHECK(doc->find("a")->members().empty());
  CHECK(doc->find("a")->find("x") == nullptr);
  CHECK(doc->find("s")->find("x") == nullptr);
}

void testStrings() {
  const auto doc = parseJson(
      R"(["a\"b\\c\/d\b\f\n\r\t", "\u0041\u00e9\u20ac", "\ud83d\ude00", "\u0000x", "raw \u00e9 é"])");
  CHECK(doc.has_value());
  if (!doc || doc->items().size() != 5) {
    return;
  }
  CHECK(doc->items()[0].asString() == "a\"b\\c/d\b\f\n\r\t");
  CHECK(doc->items()[1].asString() == "A\xC3\xA9\xE2\x82\xAC");
  CHECK(doc->items()[2].asString() == "\xF0\x9F\x98\x80"); // U+1F600 from a surrogate pair
  CHECK(doc->items()[3].asString() == std::string("\0x", 2));
  CHECK(doc->items()[4].asString() == "raw \xC3\xA9 \xC3\xA9"); // escape and raw UTF-8 agree

  // Valid raw multi-byte sequences of every length, incl. the boundary code points.
  CHECK(valid("\"\xC2\x80\""));
  CHECK(valid("\"\xDF\xBF\""));
  CHECK(valid("\"\xE0\xA0\x80\""));
  CHECK(valid("\"\xED\x9F\xBF\""));
  CHECK(valid("\"\xEF\xBF\xBF\""));
  CHECK(valid("\"\xF0\x90\x80\x80\""));
  CHECK(valid("\"\xF4\x8F\xBF\xBF\""));
}

void testInvalidStrings() {
  expectInvalid("\"abc", "unterminated string");
  expectInvalid("\"abc\\", "unterminated escape");
  expectInvalid("\"\\ud800\"", "lone high surrogate");
  expectInvalid("\"\\udc00\"", "lone low surrogate");
  expectInvalid("\"\\ud800x\"", "high surrogate followed by text");
  expectInvalid("\"\\ud800\\u0041\"", "high surrogate followed by non-low escape");
  expectInvalid("\"\\ud800\\ud800\"", "two high surrogates");
  expectInvalid("\"\\udc00\\ud800\"", "reversed pair");
  expectInvalid("\"\\u12\"", "short unicode escape");
  expectInvalid("\"\\u12G4\"", "non-hex unicode escape");
  expectInvalid("\"\\x41\"", "unknown escape");
  expectInvalid("\"\\'\"", "escaped single quote");
  expectInvalid(std::string("\"a\nb\""), "raw LF in string");
  expectInvalid(std::string("\"a\tb\""), "raw tab in string");
  expectInvalid(std::string("\"a\x01" "b\""), "raw control char");
  expectInvalid(std::string("\"a\0b\"", 5), "raw NUL");
  expectInvalid(std::string("\"a\x1F\""), "raw 0x1F");

  expectInvalid("\"\xC0\xAF\"", "overlong 2-byte slash");
  expectInvalid("\"\xC1\xBF\"", "overlong C1");
  expectInvalid("\"\xE0\x80\xAF\"", "overlong 3-byte");
  expectInvalid("\"\xF0\x80\x80\xAF\"", "overlong 4-byte");
  expectInvalid("\"\xED\xA0\x80\"", "UTF-8 encoded surrogate D800");
  expectInvalid("\"\xED\xBF\xBF\"", "UTF-8 encoded surrogate DFFF");
  expectInvalid("\"\xF4\x90\x80\x80\"", "above U+10FFFF");
  expectInvalid("\"\xF5\x80\x80\x80\"", "F5 lead byte");
  expectInvalid("\"\xFF\"", "FF byte");
  expectInvalid("\"\x80\"", "lone continuation byte");
  expectInvalid("\"\xC3\"", "truncated 2-byte sequence at the quote");
  expectInvalid("\"\xE2\x82\"", "truncated 3-byte sequence");
  expectInvalid("\"\xE2\x28\xA1\"", "bad continuation");
  expectInvalid("\"\xF0\x9F\x98\"", "truncated 4-byte sequence");
  expectInvalid("\"\xC3", "truncated sequence at end of input");
}

void testInvalidStructure() {
  expectInvalid("", "empty input");
  expectInvalid("   ", "whitespace only");
  expectInvalid("[1,]", "trailing comma in array");
  expectInvalid("{\"a\":1,}", "trailing comma in object");
  expectInvalid("[,1]", "leading comma");
  expectInvalid("[1 2]", "missing comma");
  expectInvalid("{\"a\" 1}", "missing colon");
  expectInvalid("{a:1}", "unquoted key");
  expectInvalid("{'a':1}", "single-quoted key");
  expectInvalid("{\"a\":}", "missing value");
  expectInvalid("{1:2}", "numeric key");
  expectInvalid("[1", "unterminated array");
  expectInvalid("{\"a\":1", "unterminated object");
  expectInvalid("[1}", "mismatched bracket");
  expectInvalid("{\"a\":1]", "mismatched bracket 2");
  expectInvalid("[] x", "trailing garbage");
  expectInvalid("[][]", "two values");
  expectInvalid("1 2", "two scalars");
  expectInvalid("nul", "truncated literal");
  expectInvalid("nulll", "extended literal");
  expectInvalid("True", "capitalised literal");
  expectInvalid("tru", "truncated true");
  expectInvalid("[// c\n1]", "comment");
  expectInvalid("\xEF\xBB\xBF[1]", "byte order mark");
  expectInvalid(std::string("[1]\0", 4), "trailing NUL");
}

void testInvalidNumbers() {
  expectInvalid("01", "leading zero");
  expectInvalid("-01", "negative leading zero");
  expectInvalid("00", "double zero");
  expectInvalid("+1", "plus sign");
  expectInvalid("-", "lone minus");
  expectInvalid("1.", "no fraction digits");
  expectInvalid(".5", "no integer part");
  expectInvalid("1e", "empty exponent");
  expectInvalid("1e+", "exponent sign only");
  expectInvalid("1.e3", "dot without digits before exponent");
  expectInvalid("0x10", "hex");
  expectInvalid("NaN", "NaN");
  expectInvalid("Infinity", "Infinity");
  expectInvalid("-Infinity", "-Infinity");
  expectInvalid("[nan]", "nan in array");
  expectInvalid("1e999", "out of range exponent");
  expectInvalid("--1", "double minus");
  expectInvalid("1_000", "underscore");
}

void testLimits() {
  // 10000 opening brackets: the depth limit stops the recursion long before
  // the stack is a problem.
  CHECK(!valid(std::string(10000, '[')));
  CHECK(!valid(std::string(10000, '[') + std::string(10000, ']')));
  CHECK(!valid(std::string(100000, '{')));
  std::string nestedObjects;
  for (int i = 0; i < 10000; ++i) {
    nestedObjects += "{\"a\":";
  }
  CHECK(!valid(nestedObjects));

  // Default depth 32: exactly 32 levels pass, 33 do not.
  CHECK(valid(std::string(32, '[') + std::string(32, ']')));
  CHECK(!valid(std::string(33, '[') + std::string(33, ']')));
  JsonLimits shallow;
  shallow.maxDepth = 2;
  CHECK(parseJson("[[1]]", shallow).has_value());
  CHECK(!parseJson("[[[1]]]", shallow).has_value());
  CHECK(!parseJson("{\"a\":{\"b\":{}}}", shallow).has_value());
  CHECK(parseJson("{\"a\":{}}", shallow).has_value());

  // Input size limit.
  JsonLimits small;
  small.maxInputBytes = 8;
  CHECK(parseJson("[1,2,3]", small).has_value());      // 7 bytes
  CHECK(parseJson("[1,2,3,4]", small).has_value() == false); // 9 bytes
  CHECK(parseJson("[1,2,3] ", small).has_value());     // exactly 8
  CHECK(!parseJson("[1,2,3]  ", small).has_value());   // 9

  // String length limit (applies to the decoded string).
  JsonLimits strings;
  strings.maxStringBytes = 4;
  CHECK(parseJson("\"abcd\"", strings).has_value());
  CHECK(!parseJson("\"abcde\"", strings).has_value());
  CHECK(parseJson("\"\\u20ac\"", strings).has_value()); // 3 bytes decoded
  CHECK(!parseJson("\"\\u20ac\\u20ac\"", strings).has_value());
  CHECK(!parseJson("{\"abcde\":1}", strings).has_value()); // keys are bounded too

  // A large flat array of tiny values must not be able to exhaust memory.
  std::string many = "[";
  for (int i = 0; i < 300000; ++i) {
    many += "0,";
  }
  many += "0]";
  CHECK(!valid(many));
  // ... while a realistic number of elements is fine.
  std::string some = "[";
  for (int i = 0; i < 1000; ++i) {
    some += "0,";
  }
  some += "0]";
  CHECK(valid(some));
}

} // namespace

void runJsonTests() {
  fsv::test::beginSuite("json");
  testValidDocuments();
  testAccessorsFallBack();
  testStrings();
  testInvalidStrings();
  testInvalidStructure();
  testInvalidNumbers();
  testLimits();
}
