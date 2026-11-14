// Copyright (C) 2026 FarSightVNC contributors.
// Licensed under the GNU GPL, version 2 or (at your option) any later version.

#include "MarkdownLite.h"

#include <algorithm>
#include <map>
#include <utility>

namespace fsv::update {

namespace {

// Bounds that keep pathological input linear: how far "[" looks for its "]"
// and "(" for its ")", how many emphasis openers a closer examines, and how
// deep lists nest.
constexpr std::size_t kMaxLinkSearch = 512;
constexpr std::size_t kMaxOpenerSearch = 32;
constexpr std::size_t kMaxListDepth = 8;

bool isSpace(char c) { return c == ' ' || c == '\t'; }
bool isWhite(char c) { return c == ' ' || c == '\t' || c == '\n' || c == '\r'; }
bool isDigit(char c) { return c >= '0' && c <= '9'; }
// Bytes of multi-byte UTF-8 characters count as word characters, so that an
// underscore inside a non-ASCII word stays literal.
bool isWordChar(char c) {
  return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || isDigit(c) ||
         static_cast<unsigned char>(c) >= 0x80;
}
bool isAsciiPunct(char c) { return c >= 0x21 && c <= 0x7E && !isWordChar(c); }

bool startsWithNoCase(std::string_view text, std::string_view prefix) {
  if (text.size() < prefix.size()) {
    return false;
  }
  for (std::size_t i = 0; i < prefix.size(); ++i) {
    char c = text[i];
    if (c >= 'A' && c <= 'Z') {
      c = static_cast<char>(c - 'A' + 'a');
    }
    if (c != prefix[i]) {
      return false;
    }
  }
  return true;
}

std::string_view trimmed(std::string_view text) {
  while (!text.empty() && isWhite(text.front())) {
    text.remove_prefix(1);
  }
  while (!text.empty() && isWhite(text.back())) {
    text.remove_suffix(1);
  }
  return text;
}

std::string_view truncateUtf8(std::string_view input, std::size_t maxBytes) {
  if (input.size() <= maxBytes) {
    return input;
  }
  std::size_t end = maxBytes;
  while (end > 0 && (static_cast<unsigned char>(input[end]) & 0xC0) == 0x80) {
    --end; // never cut inside a character: drop the one that straddles the limit
  }
  return input.substr(0, end);
}

// Splits on LF, CRLF and CR, dropping C0 controls other than tab (and DEL).
std::vector<std::string> splitLines(std::string_view text) {
  std::vector<std::string> lines(1);
  for (std::size_t i = 0; i < text.size(); ++i) {
    const char c = text[i];
    if (c == '\r' || c == '\n') {
      if (c == '\r' && i + 1 < text.size() && text[i + 1] == '\n') {
        ++i;
      }
      lines.emplace_back();
    } else if ((static_cast<unsigned char>(c) >= 0x20 && c != 0x7F) || c == '\t') {
      lines.back().push_back(c);
    }
  }
  return lines;
}

// Leading whitespace in columns (tab stops every 4) and the offset of the
// first other byte.
struct Indent {
  int columns = 0;
  std::size_t offset = 0;
};

Indent measureIndent(std::string_view line) {
  Indent result;
  while (result.offset < line.size() && isSpace(line[result.offset])) {
    result.columns += line[result.offset] == '\t' ? 4 - result.columns % 4 : 1;
    ++result.offset;
  }
  return result;
}

// Removes up to `columns` columns of leading whitespace.
std::string stripColumns(std::string_view line, int columns) {
  int column = 0;
  std::size_t i = 0;
  while (i < line.size() && isSpace(line[i]) && column < columns) {
    column += line[i] == '\t' ? 4 - column % 4 : 1;
    ++i;
  }
  return std::string(line.substr(i));
}

std::size_t runLength(std::string_view text, std::size_t pos, char c) {
  std::size_t end = pos;
  while (end < text.size() && text[end] == c) {
    ++end;
  }
  return end - pos;
}

bool isRule(std::string_view content) {
  const char c = content.empty() ? '\0' : content.front();
  if (c != '-' && c != '*' && c != '_') {
    return false;
  }
  int count = 0;
  for (const char ch : content) {
    if (ch == c) {
      ++count;
    } else if (!isSpace(ch)) {
      return false;
    }
  }
  return count >= 3;
}

// "# Title ##" -> level 1, "Title". Returns 0 when the line is no heading.
int headingLevel(std::string_view content, std::string_view &text) {
  const std::size_t hashes = runLength(content, 0, '#');
  if (hashes < 1 || hashes > 6 || (hashes < content.size() && !isSpace(content[hashes]))) {
    return 0;
  }
  text = trimmed(content.substr(hashes));
  std::size_t end = text.size();
  while (end > 0 && text[end - 1] == '#') {
    --end;
  }
  if (end == 0 || isSpace(text[end - 1])) { // optional closing sequence
    text = trimmed(text.substr(0, end));
  }
  return static_cast<int>(hashes);
}

struct ListMarker {
  bool ordered = false;
  int number = 0;
  std::size_t contentOffset = 0; // into the content string
};

bool parseListMarker(std::string_view content, ListMarker &marker) {
  std::size_t pos = 0;
  if (!content.empty() && (content[0] == '-' || content[0] == '*' || content[0] == '+')) {
    pos = 1;
  } else {
    std::size_t digits = 0;
    while (digits < content.size() && isDigit(content[digits])) {
      ++digits;
    }
    if (digits < 1 || digits > 9 || digits >= content.size() ||
        (content[digits] != '.' && content[digits] != ')')) {
      return false;
    }
    marker.ordered = true;
    marker.number = 0;
    for (std::size_t k = 0; k < digits; ++k) {
      marker.number = marker.number * 10 + (content[k] - '0'); // 9 digits fit an int
    }
    pos = digits + 1;
  }
  if (pos < content.size() && !isSpace(content[pos])) {
    return false;
  }
  while (pos < content.size() && isSpace(content[pos])) {
    ++pos;
  }
  marker.contentOffset = pos;
  return true;
}

// ---- Inline parsing --------------------------------------------------------

struct Token {
  enum class Kind { Text, Code, Delim, Link };
  Kind kind = Kind::Text;
  std::string text;        // Text and Code
  char ch = '\0';          // Delim: '*' or '_'
  std::size_t count = 0;   // Delim: run length (1 italic, 2 bold, 3 both)
  bool canOpen = false;
  bool canClose = false;
  int role = 0;            // Delim: +1 matched opener, -1 matched closer, 0 literal
  std::vector<Span> spans; // Link: rendered link text
  std::string url;         // Link: empty when the URL is not allowed
};

Token makeToken(Token::Kind kind, std::string text = {}) {
  Token token;
  token.kind = kind;
  token.text = std::move(text);
  return token;
}

std::vector<Span> parseInline(std::string_view s, bool allowLinks);

// "[text](url)" at s[pos]. On success sets end (one past ")"), text and url.
bool parseLink(std::string_view s, std::size_t pos, std::size_t &end, std::string_view &text,
               std::string_view &url) {
  const std::size_t limit = std::min(s.size(), pos + kMaxLinkSearch);
  std::size_t close = pos + 1;
  for (int depth = 1; close < limit; ++close) {
    if (s[close] == '\\') {
      ++close;
    } else if (s[close] == '[') {
      ++depth;
    } else if (s[close] == ']' && --depth == 0) {
      break;
    }
  }
  if (close + 1 >= limit || s[close] != ']' || s[close + 1] != '(') {
    return false;
  }
  const std::size_t urlLimit = std::min(s.size(), close + 2 + kMaxLinkSearch);
  std::size_t k = close + 2;
  while (k < urlLimit && isSpace(s[k])) {
    ++k;
  }
  const std::size_t urlStart = k;
  int parens = 0;
  while (k < urlLimit && !isWhite(s[k]) && !(s[k] == ')' && parens == 0)) {
    parens += s[k] == '(' ? 1 : (s[k] == ')' ? -1 : 0);
    ++k;
  }
  url = s.substr(urlStart, k - urlStart);
  while (k < urlLimit && s[k] != ')') { // an optional title is ignored
    ++k;
  }
  if (k >= urlLimit) {
    return false;
  }
  if (url.size() >= 2 && url.front() == '<' && url.back() == '>') {
    url = url.substr(1, url.size() - 2);
  }
  text = s.substr(pos + 1, close - pos - 1);
  end = k + 1;
  return true;
}

// Bare "https://..." at s[pos]: ends at whitespace or "<", without trailing
// punctuation or an unbalanced ")". Returns the length (0: not a URL).
std::size_t bareUrlLength(std::string_view s, std::size_t pos) {
  if (!startsWithNoCase(s.substr(pos), "https://")) {
    return 0;
  }
  std::size_t end = pos;
  while (end < s.size() && !isWhite(s[end]) && s[end] != '<') {
    ++end;
  }
  while (end > pos) {
    const char last = s[end - 1];
    const std::string_view candidate = s.substr(pos, end - pos);
    const bool unbalanced =
        last == ')' && std::count(candidate.begin(), candidate.end(), ')') >
                           std::count(candidate.begin(), candidate.end(), '(');
    if (std::string_view(".,:;!?'\"*_").find(last) == std::string_view::npos && !unbalanced) {
      break;
    }
    --end;
  }
  return isAllowedLinkUrl(s.substr(pos, end - pos)) ? end - pos : 0;
}

// Finds the closing backtick run of exactly `run` backticks from `from`.
// noCloser remembers, per run length, a position after which a search failed,
// so a long series of unmatched runs stays linear.
std::size_t findCodeCloser(std::string_view s, std::size_t from, std::size_t run,
                           std::map<std::size_t, std::size_t> &noCloser) {
  const auto known = noCloser.find(run);
  if (known != noCloser.end() && from >= known->second) {
    return std::string_view::npos;
  }
  for (std::size_t j = from; j < s.size();) {
    if (s[j] != '`') {
      ++j;
      continue;
    }
    const std::size_t k = runLength(s, j, '`');
    if (k == run) {
      return j;
    }
    j += k;
  }
  noCloser[run] = from;
  return std::string_view::npos;
}

std::vector<Token> tokenize(std::string_view s, bool allowLinks) {
  std::vector<Token> tokens;
  std::string text;
  std::map<std::size_t, std::size_t> noCodeCloser;
  const auto flushText = [&] {
    if (!text.empty()) {
      tokens.push_back(makeToken(Token::Kind::Text, std::move(text)));
      text.clear();
    }
  };
  const auto pushLink = [&](std::vector<Span> spans, std::string url) {
    Token token = makeToken(Token::Kind::Link);
    token.spans = std::move(spans);
    token.url = std::move(url);
    flushText();
    tokens.push_back(std::move(token));
  };
  std::size_t i = 0;
  while (i < s.size()) {
    const char c = s[i];
    std::size_t end = 0;
    std::string_view label;
    std::string_view url;
    if (c == '\\' && i + 1 < s.size() && isAsciiPunct(s[i + 1])) {
      text += s[i + 1];
      i += 2;
    } else if (c == '`') {
      const std::size_t run = runLength(s, i, '`');
      const std::size_t close = findCodeCloser(s, i + run, run, noCodeCloser);
      if (close == std::string_view::npos) {
        text.append(run, '`');
        i += run;
        continue;
      }
      std::string_view code = s.substr(i + run, close - i - run);
      if (code.size() >= 2 && code.front() == ' ' && code.back() == ' ' && !trimmed(code).empty()) {
        code = code.substr(1, code.size() - 2);
      }
      flushText();
      tokens.push_back(makeToken(Token::Kind::Code, std::string(code)));
      i = close + run;
    } else if (c == '*' || c == '_') {
      const std::size_t run = runLength(s, i, c);
      const char prev = i > 0 ? s[i - 1] : ' ';
      const char next = i + run < s.size() ? s[i + run] : ' ';
      Token token = makeToken(Token::Kind::Delim);
      token.ch = c;
      token.count = run;
      token.canOpen = run <= 3 && !isWhite(next) && (c == '*' || !isWordChar(prev));
      token.canClose = run <= 3 && !isWhite(prev) && (c == '*' || !isWordChar(next));
      flushText();
      tokens.push_back(std::move(token));
      i += run;
    } else if (allowLinks && c == '[' && parseLink(s, i, end, label, url)) {
      pushLink(parseInline(label, false), isAllowedLinkUrl(url) ? std::string(url) : std::string());
      i = end;
    } else if (allowLinks && c == '<') {
      const std::size_t limit = std::min(s.size(), i + kMaxLinkSearch);
      std::size_t gt = i + 1;
      while (gt < limit && s[gt] != '>' && s[gt] != '<' && !isWhite(s[gt])) {
        ++gt;
      }
      const std::string_view inner = s.substr(i + 1, gt - i - 1);
      if (gt < limit && s[gt] == '>' && isAllowedLinkUrl(inner)) {
        pushLink({Span{std::string(inner)}}, std::string(inner));
        i = gt + 1;
      } else {
        text += c; // HTML and every other "<" stay literal text
        ++i;
      }
    } else if (allowLinks && (c == 'h' || c == 'H') && (i == 0 || !isWordChar(s[i - 1])) &&
               (end = bareUrlLength(s, i)) > 0) {
      const std::string link(s.substr(i, end));
      pushLink({Span{link}}, link);
      i += end;
    } else {
      text += c;
      ++i;
    }
  }
  flushText();
  return tokens;
}

// Pairs emphasis delimiters of the same character and run length; the ones
// left over (and openers crossed by a pair) stay literal.
void matchDelimiters(std::vector<Token> &tokens) {
  std::vector<std::size_t> openers;
  for (std::size_t idx = 0; idx < tokens.size(); ++idx) {
    Token &token = tokens[idx];
    if (token.kind != Token::Kind::Delim) {
      continue;
    }
    if (token.canClose) {
      const std::size_t stop = openers.size() > kMaxOpenerSearch ? openers.size() - kMaxOpenerSearch : 0;
      std::size_t found = openers.size();
      for (std::size_t k = openers.size(); k > stop; --k) {
        const Token &opener = tokens[openers[k - 1]];
        if (opener.ch == token.ch && opener.count == token.count) {
          found = k - 1;
          break;
        }
      }
      if (found < openers.size()) {
        tokens[openers[found]].role = 1;
        token.role = -1;
        openers.resize(found);
        continue;
      }
    }
    if (token.canOpen) {
      openers.push_back(idx);
    }
  }
}

void appendSpan(std::vector<Span> &out, std::string_view text, bool bold, bool italic, bool code,
                const std::string &url) {
  if (text.empty()) {
    return;
  }
  if (!out.empty()) {
    Span &last = out.back();
    if (last.bold == bold && last.italic == italic && last.code == code && last.linkUrl == url) {
      last.text += text;
      return;
    }
  }
  out.push_back(Span{std::string(text), bold, italic, code, url});
}

std::vector<Span> parseInline(std::string_view s, bool allowLinks) {
  std::vector<Token> tokens = tokenize(s, allowLinks);
  matchDelimiters(tokens);
  std::vector<Span> out;
  const std::string noUrl;
  int bold = 0;
  int italic = 0;
  for (const Token &token : tokens) {
    switch (token.kind) {
    case Token::Kind::Text:
    case Token::Kind::Code:
      appendSpan(out, token.text, bold > 0, italic > 0, token.kind == Token::Kind::Code, noUrl);
      break;
    case Token::Kind::Delim:
      if (token.role == 0) {
        appendSpan(out, std::string(token.count, token.ch), bold > 0, italic > 0, false, noUrl);
      } else {
        bold += token.count >= 2 ? token.role : 0;
        italic += token.count != 2 ? token.role : 0;
      }
      break;
    case Token::Kind::Link:
      if (token.spans.empty()) {
        appendSpan(out, token.url, bold > 0, italic > 0, false, token.url);
      }
      for (const Span &span : token.spans) {
        appendSpan(out, span.text, bold > 0 || span.bold, italic > 0 || span.italic, span.code, token.url);
      }
      break;
    }
  }
  return out;
}

// ---- Block parsing ---------------------------------------------------------

Block makeBlock(BlockKind kind, int level = 0) {
  Block block;
  block.kind = kind;
  block.level = level;
  return block;
}

class BlockParser {
public:
  Document run(const std::vector<std::string> &lines) {
    std::size_t i = 0;
    while (i < lines.size()) {
      i = step(lines, i);
    }
    flush();
    return std::move(m_doc);
  }

private:
  // Consumes one or more lines starting at i; returns the next line.
  std::size_t step(const std::vector<std::string> &lines, std::size_t i) {
    const std::string_view line = lines[i];
    const Indent indent = measureIndent(line);
    const std::string_view content = line.substr(indent.offset);
    if (content.empty()) {
      flush();
      return i + 1;
    }
    const char first = content.front();
    const std::size_t fenceRun = (first == '`' || first == '~') ? runLength(content, 0, first) : 0;
    if ((indent.columns <= 3 || (m_inList && !m_open)) && fenceRun >= 3 &&
        (first == '~' || content.find('`', fenceRun) == std::string_view::npos)) {
      return fence(lines, i, indent.columns, first, fenceRun);
    }
    std::string_view headingText;
    if (indent.columns <= 3) {
      if (const int level = headingLevel(content, headingText)) {
        flush();
        endList();
        Block block = makeBlock(BlockKind::Heading, level);
        block.spans = parseInline(headingText, true);
        m_doc.blocks.push_back(std::move(block));
        return i + 1;
      }
      if (isRule(content)) {
        flush();
        endList();
        m_doc.blocks.push_back(makeBlock(BlockKind::Rule));
        return i + 1;
      }
    }
    ListMarker marker;
    if ((indent.columns <= 3 || m_inList) && parseListMarker(content, marker)) {
      flush();
      m_open = true;
      m_block = makeBlock(BlockKind::ListItem, listLevel(indent.columns));
      m_block.ordered = marker.ordered;
      m_block.number = marker.number;
      appendText(content.substr(marker.contentOffset));
      return i + 1;
    }
    if (indent.columns >= 4 && !m_open && !m_inList) {
      return indentedCode(lines, i);
    }
    if (!m_open) {
      if (indent.columns == 0) {
        endList(); // an indented paragraph after a blank line stays in the list
      }
      m_open = true;
      m_block = makeBlock(BlockKind::Paragraph);
    }
    appendText(content);
    return i + 1;
  }

  std::size_t fence(const std::vector<std::string> &lines, std::size_t i, int fenceIndent, char ch,
                    std::size_t length) {
    flush();
    std::string code;
    std::size_t j = i + 1;
    for (; j < lines.size(); ++j) {
      const std::string_view content = std::string_view(lines[j]).substr(measureIndent(lines[j]).offset);
      const std::size_t run = runLength(content, 0, ch);
      if (run >= length && trimmed(content.substr(run)).empty()) {
        ++j; // closing fence; an unterminated fence runs to the end of the input
        break;
      }
      if (j > i + 1) {
        code += '\n';
      }
      code += stripColumns(lines[j], fenceIndent);
    }
    pushCode(std::move(code));
    return j;
  }

  std::size_t indentedCode(const std::vector<std::string> &lines, std::size_t i) {
    std::size_t end = i;
    std::size_t lastText = i;
    while (end < lines.size()) {
      const Indent indent = measureIndent(lines[end]);
      const bool blank = indent.offset == lines[end].size();
      if (!blank && indent.columns < 4) {
        break;
      }
      lastText = blank ? lastText : end;
      ++end;
    }
    std::string code;
    for (std::size_t j = i; j <= lastText; ++j) {
      if (j > i) {
        code += '\n';
      }
      code += stripColumns(lines[j], 4);
    }
    pushCode(std::move(code));
    return end;
  }

  void pushCode(std::string code) {
    flush();
    Block block = makeBlock(BlockKind::CodeBlock);
    block.spans.push_back(Span{std::move(code), false, false, true, {}});
    m_doc.blocks.push_back(std::move(block));
  }

  // Nesting depth of a list item: one level deeper when indented by at least
  // two more columns than the current level, shallower when outdented.
  int listLevel(int columns) {
    while (m_listIndents.size() > 1 && m_listIndents.back() > columns) {
      m_listIndents.pop_back();
    }
    if (m_listIndents.empty() ||
        (columns >= m_listIndents.back() + 2 && m_listIndents.size() < kMaxListDepth)) {
      m_listIndents.push_back(columns);
    }
    m_inList = true;
    return static_cast<int>(m_listIndents.size()) - 1;
  }

  void endList() {
    m_listIndents.clear();
    m_inList = false;
  }

  void appendText(std::string_view text) {
    text = trimmed(text);
    if (!m_text.empty() && !text.empty()) {
      m_text += ' ';
    }
    m_text += text;
  }

  void flush() {
    if (m_open) {
      m_block.spans = parseInline(m_text, true);
      m_doc.blocks.push_back(std::move(m_block));
    }
    m_open = false;
    m_text.clear();
  }

  Document m_doc;
  Block m_block;
  std::string m_text;
  bool m_open = false;
  bool m_inList = false;
  std::vector<int> m_listIndents;
};

} // namespace

bool isAllowedLinkUrl(std::string_view url) {
  constexpr std::string_view kScheme = "https://";
  if (url.size() <= kScheme.size() || !startsWithNoCase(url, kScheme)) {
    return false;
  }
  for (const char c : url) {
    if (static_cast<unsigned char>(c) <= 0x20 || c == 0x7F) {
      return false;
    }
  }
  return true;
}

Document parseMarkdown(std::string_view input, std::size_t maxBytes) {
  return BlockParser().run(splitLines(truncateUtf8(input, maxBytes)));
}

std::string toPlainText(const Document &document) {
  std::string out;
  const Block *previous = nullptr;
  for (const Block &block : document.blocks) {
    if (previous != nullptr) {
      const bool list = block.kind == BlockKind::ListItem && previous->kind == BlockKind::ListItem;
      out += list ? "\n" : "\n\n";
    }
    if (block.kind == BlockKind::ListItem) {
      out.append(static_cast<std::size_t>(block.level) * 2, ' ');
      out += block.ordered ? std::to_string(block.number) + ". " : std::string("\xE2\x80\xA2 ");
    } else if (block.kind == BlockKind::Rule) {
      out += "---";
    }
    for (const Span &span : block.spans) {
      out += span.text;
    }
    previous = &block;
  }
  return out;
}

} // namespace fsv::update
