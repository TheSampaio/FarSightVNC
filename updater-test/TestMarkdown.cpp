// Copyright (C) 2026 FarSightVNC contributors.
// Licensed under the GNU GPL, version 2 or (at your option) any later version.

#include "TestHarness.h"

#include "updater-lib/MarkdownLite.h"

#include <chrono>
#include <cstdint>
#include <string>

using namespace fsv::update;

namespace {

std::string blockText(const Block &block) {
  std::string text;
  for (const Span &span : block.spans) {
    text += span.text;
  }
  return text;
}

bool hasLink(const Document &doc) {
  for (const Block &block : doc.blocks) {
    for (const Span &span : block.spans) {
      if (!span.linkUrl.empty()) {
        return true;
      }
    }
  }
  return false;
}

void testEmpty() {
  CHECK(parseMarkdown("").blocks.empty());
  CHECK(parseMarkdown("\n\n  \r\n\t\n").blocks.empty());
  CHECK(toPlainText(parseMarkdown("")).empty());
}

void testHeadings() {
  const Document doc = parseMarkdown("# One\n## Two ##\n###### Six\n####### Seven\n\n#NoSpace\n#\n");
  CHECK(doc.blocks.size() == 6);
  if (doc.blocks.size() != 6) {
    return;
  }
  CHECK(doc.blocks[0].kind == BlockKind::Heading && doc.blocks[0].level == 1 && blockText(doc.blocks[0]) == "One");
  CHECK(doc.blocks[1].level == 2 && blockText(doc.blocks[1]) == "Two");
  CHECK(doc.blocks[2].level == 6 && blockText(doc.blocks[2]) == "Six");
  CHECK(doc.blocks[3].kind == BlockKind::Paragraph); // 7 hashes: text
  CHECK(blockText(doc.blocks[3]) == "####### Seven");
  CHECK(doc.blocks[4].kind == BlockKind::Paragraph && blockText(doc.blocks[4]) == "#NoSpace");
  CHECK(doc.blocks[5].kind == BlockKind::Heading && doc.blocks[5].spans.empty());
}

void testParagraphs() {
  const Document doc = parseMarkdown("first line\nsecond   line  \r\n\r\nnext paragraph\rcontinued");
  CHECK(doc.blocks.size() == 2);
  if (doc.blocks.size() == 2) {
    CHECK(doc.blocks[0].kind == BlockKind::Paragraph);
    CHECK(blockText(doc.blocks[0]) == "first line second   line");
    CHECK(blockText(doc.blocks[1]) == "next paragraph continued");
  }
  CHECK(toPlainText(doc) == "first line second   line\n\nnext paragraph continued");
}

void testLists() {
  const Document doc = parseMarkdown("- one\n* two\n  continued\n+ three\n  - nested\n    - deeper\n- back\n\n"
                                     "1. first\n2) second\n   3. sub\n10. tenth\n\nafter\n");
  CHECK(doc.blocks.size() == 11);
  if (doc.blocks.size() != 11) {
    return;
  }
  for (int i = 0; i < 10; ++i) {
    CHECK(doc.blocks[i].kind == BlockKind::ListItem);
  }
  CHECK(!doc.blocks[0].ordered && doc.blocks[0].level == 0 && blockText(doc.blocks[0]) == "one");
  CHECK(blockText(doc.blocks[1]) == "two continued");
  CHECK(doc.blocks[3].level == 1 && blockText(doc.blocks[3]) == "nested");
  CHECK(doc.blocks[4].level == 2 && blockText(doc.blocks[4]) == "deeper");
  CHECK(doc.blocks[5].level == 0 && blockText(doc.blocks[5]) == "back");
  CHECK(doc.blocks[6].ordered && doc.blocks[6].number == 1 && doc.blocks[6].level == 0);
  CHECK(doc.blocks[7].ordered && doc.blocks[7].number == 2);
  CHECK(doc.blocks[8].number == 3 && doc.blocks[8].level == 1);
  CHECK(doc.blocks[9].number == 10 && doc.blocks[9].level == 0 && blockText(doc.blocks[9]) == "tenth");
  CHECK(doc.blocks[10].kind == BlockKind::Paragraph);
  // Not list markers: no space after the marker, too many digits.
  const Document text = parseMarkdown("-nope\n1234567890. big\n");
  CHECK(text.blocks.size() == 1 && text.blocks[0].kind == BlockKind::Paragraph);
  CHECK(toPlainText(parseMarkdown("- a\n  - b\n3. c")) == "\xE2\x80\xA2 a\n  \xE2\x80\xA2 b\n3. c");
}

void testCodeBlocks() {
  const Document doc = parseMarkdown("```cpp\nint a = 1;\n  <b>*x*</b>\n```\ntext\n~~~~\n~~~\nstill code\n~~~~\n");
  CHECK(doc.blocks.size() == 3);
  if (doc.blocks.size() == 3) {
    CHECK(doc.blocks[0].kind == BlockKind::CodeBlock && doc.blocks[0].spans.size() == 1);
    CHECK(blockText(doc.blocks[0]) == "int a = 1;\n  <b>*x*</b>");
    CHECK(doc.blocks[0].spans[0].code);
    CHECK(doc.blocks[1].kind == BlockKind::Paragraph);
    CHECK(blockText(doc.blocks[2]) == "~~~\nstill code");
  }
  const Document open = parseMarkdown("before\n```\nunterminated\n# not a heading");
  CHECK(open.blocks.size() == 2);
  if (open.blocks.size() == 2) {
    CHECK(open.blocks[1].kind == BlockKind::CodeBlock);
    CHECK(blockText(open.blocks[1]) == "unterminated\n# not a heading");
  }
  const Document empty = parseMarkdown("```\n```");
  CHECK(empty.blocks.size() == 1 && empty.blocks[0].kind == BlockKind::CodeBlock && blockText(empty.blocks[0]).empty());
  // "```code```" on one line is inline code, not a fence.
  const Document inlineFence = parseMarkdown("```code```");
  CHECK(inlineFence.blocks.size() == 1 && inlineFence.blocks[0].kind == BlockKind::Paragraph);

  const Document indented = parseMarkdown("para\n\n    line 1\n\n\tline 2\n\nafter");
  CHECK(indented.blocks.size() == 3);
  if (indented.blocks.size() == 3) {
    CHECK(indented.blocks[1].kind == BlockKind::CodeBlock);
    CHECK(blockText(indented.blocks[1]) == "line 1\n\nline 2");
  }
  // An indented line right after a paragraph continues it.
  const Document lazy = parseMarkdown("para\n    more");
  CHECK(lazy.blocks.size() == 1 && blockText(lazy.blocks[0]) == "para more");
}

void testRules() {
  const Document doc = parseMarkdown("---\n***\n_ _ _\n--\n- - -x\n");
  CHECK(doc.blocks.size() == 5);
  if (doc.blocks.size() == 5) {
    CHECK(doc.blocks[0].kind == BlockKind::Rule && doc.blocks[1].kind == BlockKind::Rule &&
          doc.blocks[2].kind == BlockKind::Rule);
    CHECK(doc.blocks[3].kind == BlockKind::Paragraph);
    CHECK(doc.blocks[4].kind == BlockKind::ListItem);
  }
}

void testInline() {
  const Document doc = parseMarkdown("a **bold** and __b2__ *it* _i2_ `co*de*` ***both*** end");
  CHECK(doc.blocks.size() == 1);
  if (doc.blocks.empty()) {
    return;
  }
  const std::vector<Span> &s = doc.blocks[0].spans;
  CHECK(blockText(doc.blocks[0]) == "a bold and b2 it i2 co*de* both end");
  bool sawBold = false, sawB2 = false, sawIt = false, sawI2 = false, sawCode = false, sawBoth = false;
  for (const Span &span : s) {
    sawBold |= span.text == "bold" && span.bold && !span.italic;
    sawB2 |= span.text == "b2" && span.bold;
    sawIt |= span.text == "it" && span.italic && !span.bold;
    sawI2 |= span.text == "i2" && span.italic;
    sawCode |= span.text == "co*de*" && span.code && !span.italic;
    sawBoth |= span.text == "both" && span.bold && span.italic;
  }
  CHECK(sawBold && sawB2 && sawIt && sawI2 && sawCode && sawBoth);

  // Unmatched and intraword delimiters stay literal.
  CHECK(blockText(parseMarkdown("**open only").blocks[0]) == "**open only");
  CHECK(blockText(parseMarkdown("snake_case_name").blocks[0]) == "snake_case_name");
  CHECK(!parseMarkdown("snake_case_name").blocks[0].spans[0].italic);
  CHECK(blockText(parseMarkdown("2 * 3 * 4").blocks[0]) == "2 * 3 * 4");
  CHECK(blockText(parseMarkdown("\\*not\\* \\`code\\`").blocks[0]) == "*not* `code`");
  CHECK(blockText(parseMarkdown("`` a ` b ``").blocks[0]) == "a ` b");
  CHECK(blockText(parseMarkdown("`unterminated").blocks[0]) == "`unterminated");
  const Document nested = parseMarkdown("*a **b** c*");
  CHECK(blockText(nested.blocks[0]) == "a b c");
  CHECK(nested.blocks[0].spans.size() == 3 && nested.blocks[0].spans[1].bold && nested.blocks[0].spans[1].italic);
}

void testLinks() {
  const Document doc = parseMarkdown("See [the **notes**](https://github.com/x/y/releases \"title\") now.");
  CHECK(doc.blocks.size() == 1);
  if (doc.blocks.empty()) {
    return;
  }
  CHECK(blockText(doc.blocks[0]) == "See the notes now.");
  bool linkBold = false, linkPlain = false;
  for (const Span &span : doc.blocks[0].spans) {
    linkBold |= span.text == "notes" && span.bold && span.linkUrl == "https://github.com/x/y/releases";
    linkPlain |= span.text == "the " && span.linkUrl == "https://github.com/x/y/releases";
    CHECK(span.linkUrl.empty() || span.text == "the " || span.text == "notes");
  }
  CHECK(linkBold && linkPlain);

  // Only https:// becomes a link; the text survives.
  for (const char *text : {"[t](http://example.com)", "[t](javascript:alert(1))", "[t](file:///C:/x)",
                           "[t](HTTPS:)", "<http://example.com>", "http://example.com/bare",
                           "[t](https://)"}) {
    const Document d = parseMarkdown(text);
    CHECK(!hasLink(d));
  }
  CHECK(blockText(parseMarkdown("[t](http://example.com)").blocks[0]) == "t");

  const Document autolink = parseMarkdown("go <https://a.example/x?y=1> or https://b.example/path). Done");
  CHECK(autolink.blocks.size() == 1);
  if (!autolink.blocks.empty()) {
    const std::vector<Span> &s = autolink.blocks[0].spans;
    CHECK(s.size() == 5);
    if (s.size() == 5) {
      CHECK(s[1].text == "https://a.example/x?y=1" && s[1].linkUrl == s[1].text);
      CHECK(s[2].linkUrl.empty() && s[4].linkUrl.empty());
      CHECK(s[3].linkUrl == "https://b.example/path" && s[3].text == s[3].linkUrl);
      CHECK(s[4].text == "). Done");
    }
    CHECK(blockText(autolink.blocks[0]) == "go https://a.example/x?y=1 or https://b.example/path). Done");
  }
  CHECK(parseMarkdown("HTTPS://UP.example").blocks[0].spans[0].linkUrl == "HTTPS://UP.example");
  CHECK(!hasLink(parseMarkdown("xhttps://glued.example")));
  CHECK(blockText(parseMarkdown("[a [b] c](https://x.example)").blocks[0]) == "a [b] c");
  CHECK(blockText(parseMarkdown("[no close (https://x.example)").blocks[0]) == "[no close (https://x.example)");
  CHECK(isAllowedLinkUrl("https://x"));
  CHECK(!isAllowedLinkUrl("https://"));
  CHECK(!isAllowedLinkUrl("https://a b"));
  CHECK(!isAllowedLinkUrl("ftp://x"));
}

void testHtmlIsLiteral() {
  const std::string html = "<script>alert('x')</script> <b>bold</b> <img src=x onerror=y>";
  const Document doc = parseMarkdown(html);
  CHECK(doc.blocks.size() == 1);
  CHECK(!doc.blocks.empty() && blockText(doc.blocks[0]) == html);
  CHECK(!hasLink(doc));
  // Control characters are dropped; tabs are kept.
  const Document controls = parseMarkdown(std::string("a\x01" "b\x7F\tc\0d", 8));
  CHECK(controls.blocks.size() == 1 && blockText(controls.blocks[0]) == "ab\tcd");
}

void testTruncation() {
  // 3-byte characters straddling the 64 KiB limit: the cut lands on a boundary.
  std::string text;
  while (text.size() < kMarkdownMaxBytes + 300) {
    text += "\xE2\x82\xAC"; // euro sign
  }
  const Document doc = parseMarkdown(text);
  CHECK(doc.blocks.size() == 1);
  if (!doc.blocks.empty()) {
    const std::string out = blockText(doc.blocks[0]);
    CHECK(out.size() <= kMarkdownMaxBytes);
    CHECK(out.size() % 3 == 0);
    CHECK(out.size() == (kMarkdownMaxBytes / 3) * 3);
  }
  const Document small = parseMarkdown("abc\xC3\xA9xyz", 4); // cut inside U+00E9
  CHECK(small.blocks.size() == 1 && blockText(small.blocks[0]) == "abc");
  CHECK(parseMarkdown("abcdef", 0).blocks.empty());
}

template <typename F> double millisecondsOf(F &&f) {
  const auto start = std::chrono::steady_clock::now();
  f();
  return std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
}

void testPathological() {
  const std::string inputs[] = {
      std::string(10000, '*'),
      std::string(10000, '['),
      std::string(5000, '[') + std::string(5000, ']') + "(https://x.example)",
      std::string(20000, '`'),
      [] { std::string s; for (int i = 0; i < 5000; ++i) { s += "*a "; } return s; }(),
      [] { std::string s; for (int i = 1; i < 300; ++i) { s += std::string(i, '`') + "x"; } return s; }(),
      [] { std::string s; for (int i = 0; i < 8000; ++i) { s += "[a"; } return s; }(),
      [] { std::string s; for (int i = 0; i < 6000; ++i) { s += "<https:"; } return s; }(),
      [] { std::string s; for (int i = 0; i < 3000; ++i) { s += std::string(i % 40, ' ') + "- x\n"; } return s; }(),
      std::string(1024 * 1024, '#'),
      std::string(1024 * 1024, '\n'),
  };
  for (const std::string &input : inputs) {
    Document doc;
    const double ms = millisecondsOf([&] { doc = parseMarkdown(input); });
    CHECK(!doc.blocks.empty() || input.find_first_not_of('\n') == std::string::npos);
    CHECK(ms < 2000.0);
    if (ms >= 2000.0) {
      std::printf("  (slow: %.0f ms for %zu bytes)\n", ms, input.size());
    }
  }
  const Document stars = parseMarkdown(std::string(10000, '*'));
  CHECK(stars.blocks.size() == 1 && stars.blocks[0].kind == BlockKind::Rule);
  const Document hashes = parseMarkdown(std::string(1024 * 1024, '#'));
  CHECK(hashes.blocks.size() == 1 && blockText(hashes.blocks[0]).size() == kMarkdownMaxBytes);
}

void testFuzz() {
  std::uint32_t state = 0x5EED1234u;
  const auto next = [&state] {
    state ^= state << 13;
    state ^= state >> 17;
    state ^= state << 5;
    return state;
  };
  const char alphabet[] = "*_`[]()<>#-+~.!\\ \t\n\rhttps:/x1a" "\xC3\xA9";
  int completed = 0;
  for (int round = 0; round < 2000; ++round) {
    const std::size_t length = next() % 400;
    std::string input;
    for (std::size_t i = 0; i < length; ++i) {
      const std::uint32_t r = next();
      input += (r & 1) ? static_cast<char>(r >> 8) : alphabet[(r >> 8) % (sizeof(alphabet) - 1)];
    }
    const Document doc = parseMarkdown(input, 1 + next() % 512);
    (void)toPlainText(doc);
    ++completed;
  }
  CHECK(completed == 2000);
}

} // namespace

void runMarkdownTests() {
  fsv::test::beginSuite("markdown");
  testEmpty();
  testHeadings();
  testParagraphs();
  testLists();
  testCodeBlocks();
  testRules();
  testInline();
  testLinks();
  testHtmlIsLiteral();
  testTruncation();
  testPathological();
  testFuzz();
}
