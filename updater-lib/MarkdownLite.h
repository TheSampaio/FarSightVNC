// Copyright (C) 2026 FarSightVNC contributors.
// Licensed under the GNU GPL, version 2 or (at your option) any later version.
//
// Small, bounded Markdown reader for the release notes of the update dialog.
// Input is untrusted (the body of a GitHub release): the parser never throws,
// never reads out of bounds and degrades every malformed or unterminated
// construct to plain text. It produces a UI-free document model; rendering is
// up to the caller.
//
// Supported subset: ATX headings, paragraphs (soft line breaks become a space),
// unordered (- * +) and ordered (1. or 1)) lists nested by indentation, fenced
// (``` ~~~) and 4-space indented code blocks, horizontal rules (--- *** ___),
// and inline **bold**, __bold__, *italic*, _italic_, `code`, [text](url),
// <https://...> autolinks, bare https:// URLs and backslash escapes.
//
// Security rules: only https:// URLs become links (anything else keeps its
// text and loses the link); HTML is never interpreted (tags stay literal
// text); C0 control characters other than tab and line breaks are dropped.

#pragma once

#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

namespace fsv::update {

struct Span {
  std::string text; // UTF-8
  bool bold = false;
  bool italic = false;
  bool code = false;
  std::string linkUrl; // empty, or an https:// URL
};

enum class BlockKind { Heading, Paragraph, ListItem, CodeBlock, Rule };

struct Block {
  BlockKind kind = BlockKind::Paragraph;
  int level = 0;        // Heading: 1-6. ListItem: nesting depth, 0 = top level.
  bool ordered = false; // ListItem only.
  int number = 0;       // ListItem with ordered = true: the number written.
  std::vector<Span> spans; // CodeBlock: one span with the raw text (LF separated).
};

struct Document {
  std::vector<Block> blocks;
};

inline constexpr std::size_t kMarkdownMaxBytes = 64 * 1024;

// Parses at most maxBytes of input (cut back to a UTF-8 character boundary).
Document parseMarkdown(std::string_view input, std::size_t maxBytes = kMarkdownMaxBytes);

// Plain text rendering: one block per line group, "• " / "N. " list prefixes
// indented by two spaces per level, rules as "---". LF line breaks.
std::string toPlainText(const Document &document);

// True for an https:// URL (case-insensitive scheme) made of printable ASCII
// only, without '"', '<', '>' or backslash: the only kind of URL kept as a
// link (punycode hosts are ASCII).
bool isAllowedLinkUrl(std::string_view url);

} // namespace fsv::update
