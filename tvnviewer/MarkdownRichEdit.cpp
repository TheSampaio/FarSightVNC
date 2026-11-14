// Copyright (C) 2026 FarSightVNC contributors.
// Licensed under the GNU GPL, version 2 or (at your option) any later version.

#include "MarkdownRichEdit.h"

#include "gui/DpiUtils.h"

#include <richedit.h>

#include <string.h>

using fsv::update::Block;
using fsv::update::BlockKind;
using fsv::update::Span;

namespace {

// Typography in points (body size comes from ThemeTokens::fontSizeBody, in
// DIPs) and spacing in points.
const float HEADING_SCALE[7] = {1.0f, 1.5f, 1.3f, 1.15f, 1.0f, 1.0f, 1.0f};
const float CODE_SCALE = 0.95f;
const float RULE_SCALE = 0.6f;
const wchar_t CODE_FACE[] = L"Consolas";
const float LIST_INDENT = 14.0f;  // per nesting level
const float LIST_HANGING = 14.0f; // bullet/number column
const float CODE_INDENT = 6.0f;
const float SPACE_PARAGRAPH = 6.0f;
const float SPACE_LIST_ITEM = 2.0f;
const float SPACE_HEADING_BEFORE = 10.0f;
const float SPACE_HEADING_AFTER = 4.0f;
const int RULE_LENGTH = 48;
const int MARGIN_DIPS = 6;
const LONG TEXT_LIMIT = 1 << 20; // characters; the default is only 32767

COLORREF toColorRef(ThemeColor color)
{
  return RGB((color >> 16) & 0xFF, (color >> 8) & 0xFF, color & 0xFF);
}

std::wstring widen(const std::string &utf8)
{
  if (utf8.empty()) {
    return std::wstring();
  }
  const int length = MultiByteToWideChar(CP_UTF8, 0, utf8.data(), static_cast<int>(utf8.size()), 0, 0);
  std::wstring result(length > 0 ? static_cast<size_t>(length) : 0, L'\0');
  if (length > 0) {
    MultiByteToWideChar(CP_UTF8, 0, utf8.data(), static_cast<int>(utf8.size()), &result[0], length);
  }
  return result;
}

// Style of a character range, turned into a full CHARFORMAT2W.
struct CharStyle
{
  float points;
  bool bold;
  bool italic;
  bool code;
  bool link;
  ThemeColor color;
};

struct CharRun
{
  LONG start;
  LONG end;
  CharStyle style;
};

struct ParaRun
{
  LONG start;
  LONG end;
  float startIndent; // points
  float hanging;     // points; also the tab stop of the bullet
  float spaceBefore;
  float spaceAfter;
};

struct StreamSource
{
  const BYTE *data;
  size_t size;
  size_t offset;
};

DWORD CALLBACK streamIn(DWORD_PTR cookie, LPBYTE buffer, LONG bufferSize, LONG *read)
{
  StreamSource *source = reinterpret_cast<StreamSource *>(cookie);
  size_t count = source->size - source->offset;
  if (count > static_cast<size_t>(bufferSize)) {
    count = static_cast<size_t>(bufferSize);
  }
  memcpy(buffer, source->data + source->offset, count);
  source->offset += count;
  *read = static_cast<LONG>(count);
  return 0;
}

} // namespace

MarkdownRichEdit::MarkdownRichEdit()
  : m_hwnd(0),
    m_tokens(kLightTheme)
{
}

bool MarkdownRichEdit::ensureLibrary()
{
  // Full path to the system copy: never searched in the application or the
  // current directory. Loaded once and never freed (the class stays
  // registered for the life of the process).
  static HMODULE module = []() -> HMODULE {
    wchar_t path[MAX_PATH];
    const UINT length = GetSystemDirectoryW(path, MAX_PATH);
    if (length == 0 || length + 14 >= MAX_PATH) {
      return 0;
    }
    wcscpy_s(path + length, MAX_PATH - length, L"\\msftedit.dll");
    return LoadLibraryW(path);
  }();
  return module != 0;
}

HWND MarkdownRichEdit::create(HWND parent, int id, const RECT &rc)
{
  if (!ensureLibrary()) {
    return 0;
  }
  const DWORD style = WS_CHILD | WS_VISIBLE | WS_VSCROLL | WS_TABSTOP | ES_MULTILINE | ES_READONLY |
                      ES_AUTOVSCROLL | ES_NOOLEDRAGDROP;
  HWND hwnd = CreateWindowExW(0, MSFTEDIT_CLASS, L"", style, rc.left, rc.top, rc.right - rc.left,
                              rc.bottom - rc.top, parent,
                              reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)),
                              GetModuleHandleW(0), 0);
  if (hwnd == 0) {
    return 0;
  }
  m_hwnd = hwnd;
  configure();
  return m_hwnd;
}

bool MarkdownRichEdit::attach(HWND richEdit)
{
  if (richEdit == 0) {
    return false;
  }
  m_hwnd = richEdit;
  configure();
  return true;
}

void MarkdownRichEdit::configure()
{
  SetWindowTextW(m_hwnd, L""); // EM_SETTEXTMODE only works on an empty control
  SendMessageW(m_hwnd, EM_SETTEXTMODE, TM_RICHTEXT, 0);
  SendMessageW(m_hwnd, EM_AUTOURLDETECT, FALSE, 0); // links come only from the map
  SendMessageW(m_hwnd, EM_EXLIMITTEXT, 0, TEXT_LIMIT);
  SendMessageW(m_hwnd, EM_SETUNDOLIMIT, 0, 0);
  SendMessageW(m_hwnd, EM_SETEVENTMASK, 0, ENM_LINK);
  render();
}

void MarkdownRichEdit::setTheme(const ThemeTokens &tokens)
{
  m_tokens = tokens;
  render();
}

void MarkdownRichEdit::setMarkdown(const fsv::update::Document &document)
{
  m_document = document;
  render();
}

void MarkdownRichEdit::refresh()
{
  render();
}

LONG MarkdownRichEdit::twips(float points) const
{
  UINT systemDpi = DpiUtils::DEFAULT_DPI;
  HDC screen = GetDC(0);
  if (screen != 0) {
    systemDpi = static_cast<UINT>(GetDeviceCaps(screen, LOGPIXELSY));
    ReleaseDC(0, screen);
  }
  const UINT windowDpi = m_hwnd != 0 ? DpiUtils::getDpiForWindow(m_hwnd) : systemDpi;
  const float scale = systemDpi != 0 ? static_cast<float>(windowDpi) / static_cast<float>(systemDpi) : 1.0f;
  return static_cast<LONG>(points * 20.0f * scale + 0.5f);
}

bool MarkdownRichEdit::handleNotify(const NMHDR *header, std::wstring &outUrl) const
{
  if (header == 0 || m_hwnd == 0 || header->hwndFrom != m_hwnd || header->code != EN_LINK) {
    return false;
  }
  const ENLINK *link = reinterpret_cast<const ENLINK *>(header);
  if (link->msg != WM_LBUTTONUP) {
    return false;
  }
  for (size_t i = 0; i < m_links.size(); ++i) {
    if (link->chrg.cpMin >= m_links[i].start && link->chrg.cpMin < m_links[i].end) {
      outUrl = m_links[i].url;
      return true;
    }
  }
  return false;
}

void MarkdownRichEdit::render()
{
  if (m_hwnd == 0) {
    return;
  }
  const float body = m_tokens.fontSizeBody * 0.75f; // DIPs to points
  std::wstring text;
  std::vector<CharRun> chars;
  std::vector<ParaRun> paras;
  std::vector<LinkRange> links;

  const auto pos = [&text]() { return static_cast<LONG>(text.size()); };
  const auto addText = [&](const std::wstring &value, const CharStyle &style) {
    if (!value.empty()) {
      const LONG start = pos();
      text += value;
      chars.push_back(CharRun{start, pos(), style});
    }
  };

  const std::vector<Block> &blocks = m_document.blocks;
  for (size_t index = 0; index < blocks.size(); ++index) {
    const Block &block = blocks[index];
    const bool last = index + 1 == blocks.size();
    if (index > 0) {
      text += L'\r';
    }
    const LONG start = pos();
    CharStyle style = {body, false, false, false, false, m_tokens.text};
    ParaRun para = {start, start, 0.0f, 0.0f, 0.0f, SPACE_PARAGRAPH};

    switch (block.kind) {
    case BlockKind::Heading: {
      const int level = block.level >= 1 && block.level <= 6 ? block.level : 1;
      style.points = body * HEADING_SCALE[level];
      style.bold = true;
      para.spaceBefore = index > 0 ? SPACE_HEADING_BEFORE : 0.0f;
      para.spaceAfter = SPACE_HEADING_AFTER;
      break;
    }
    case BlockKind::ListItem: {
      const int level = block.level < 0 ? 0 : block.level;
      para.startIndent = LIST_INDENT * static_cast<float>(level);
      para.hanging = LIST_HANGING;
      const bool nextIsItem = !last && blocks[index + 1].kind == BlockKind::ListItem;
      para.spaceAfter = nextIsItem ? SPACE_LIST_ITEM : SPACE_PARAGRAPH;
      CharStyle marker = style;
      marker.color = m_tokens.mutedText;
      addText(block.ordered ? std::to_wstring(block.number) + L".\t" : std::wstring(L"\x2022\t"), marker);
      break;
    }
    case BlockKind::CodeBlock: {
      style.points = body * CODE_SCALE;
      style.code = true;
      para.startIndent = CODE_INDENT;
      std::wstring code = block.spans.empty() ? std::wstring() : widen(block.spans[0].text);
      for (size_t i = 0; i < code.size(); ++i) {
        if (code[i] == L'\n') {
          code[i] = L'\r';
        }
      }
      addText(code.empty() ? std::wstring(L" ") : code, style);
      // The lines of the block sit together; only the last one gets the
      // space after (it is a paragraph range of its own, applied later).
      const LONG lastLine = static_cast<LONG>(text.rfind(L'\r')) + 1;
      if (lastLine > start) {
        ParaRun inner = para;
        inner.end = lastLine;
        inner.spaceAfter = 0.0f;
        paras.push_back(inner);
        para.start = lastLine;
      }
      break;
    }
    case BlockKind::Rule: {
      style.points = body * RULE_SCALE;
      style.color = m_tokens.border;
      para.spaceBefore = SPACE_LIST_ITEM;
      addText(std::wstring(RULE_LENGTH, L'\x2500'), style);
      break;
    }
    case BlockKind::Paragraph:
      break;
    }

    if (block.kind != BlockKind::CodeBlock && block.kind != BlockKind::Rule) {
      for (size_t i = 0; i < block.spans.size(); ++i) {
        const Span &span = block.spans[i];
        CharStyle spanStyle = style;
        spanStyle.bold = style.bold || span.bold;
        spanStyle.italic = span.italic;
        spanStyle.code = span.code;
        if (span.code) {
          spanStyle.points = style.points * CODE_SCALE;
        }
        const bool link = fsv::update::isAllowedLinkUrl(span.linkUrl);
        if (link) {
          spanStyle.link = true;
          spanStyle.color = m_tokens.accent;
        }
        const LONG spanStart = pos();
        addText(widen(span.text), spanStyle);
        if (link && pos() > spanStart) {
          links.push_back(LinkRange{spanStart, pos(), widen(span.linkUrl)});
        }
      }
    }
    para.end = pos();
    paras.push_back(para);
  }

  SendMessageW(m_hwnd, WM_SETREDRAW, FALSE, 0);

  // Plain Unicode text only: SF_TEXT never runs the RTF reader, whatever the
  // text starts with.
  StreamSource source = {reinterpret_cast<const BYTE *>(text.c_str()), text.size() * sizeof(wchar_t), 0};
  EDITSTREAM stream = {};
  stream.dwCookie = reinterpret_cast<DWORD_PTR>(&source);
  stream.pfnCallback = streamIn;
  SendMessageW(m_hwnd, EM_STREAMIN, SF_TEXT | SF_UNICODE, reinterpret_cast<LPARAM>(&stream));
  m_links.swap(links);

  SendMessageW(m_hwnd, EM_SETBKGNDCOLOR, 0, toColorRef(m_tokens.field));

  const auto charFormat = [this](const CharStyle &style) {
    CHARFORMAT2W format = {};
    format.cbSize = sizeof(format);
    format.dwMask = CFM_FACE | CFM_SIZE | CFM_COLOR | CFM_BOLD | CFM_ITALIC | CFM_UNDERLINE |
                    CFM_LINK | CFM_BACKCOLOR;
    format.dwEffects = (style.bold ? CFE_BOLD : 0) | (style.italic ? CFE_ITALIC : 0) |
                       (style.link ? CFE_LINK | CFE_UNDERLINE : 0) | (style.code ? 0 : CFE_AUTOBACKCOLOR);
    format.yHeight = twips(style.points);
    format.crTextColor = toColorRef(style.color);
    format.crBackColor = toColorRef(m_tokens.surface);
    lstrcpynW(format.szFaceName, style.code ? CODE_FACE : m_tokens.fontFamily, LF_FACESIZE);
    return format;
  };

  CHARFORMAT2W base = charFormat(CharStyle{body, false, false, false, false, m_tokens.text});
  SendMessageW(m_hwnd, EM_SETCHARFORMAT, SCF_ALL, reinterpret_cast<LPARAM>(&base));
  for (size_t i = 0; i < chars.size(); ++i) {
    CHARRANGE range = {chars[i].start, chars[i].end};
    CHARFORMAT2W format = charFormat(chars[i].style);
    SendMessageW(m_hwnd, EM_EXSETSEL, 0, reinterpret_cast<LPARAM>(&range));
    SendMessageW(m_hwnd, EM_SETCHARFORMAT, SCF_SELECTION, reinterpret_cast<LPARAM>(&format));
  }
  for (size_t i = 0; i < paras.size(); ++i) {
    const ParaRun &para = paras[i];
    PARAFORMAT2 format = {};
    format.cbSize = sizeof(format);
    format.dwMask = PFM_STARTINDENT | PFM_OFFSET | PFM_SPACEBEFORE | PFM_SPACEAFTER | PFM_TABSTOPS |
                    PFM_NUMBERING;
    format.dxStartIndent = twips(para.startIndent);
    format.dxOffset = twips(para.hanging);
    format.dySpaceBefore = twips(para.spaceBefore);
    format.dySpaceAfter = twips(para.spaceAfter);
    format.wNumbering = 0; // bullets and numbers are part of the text
    if (para.hanging > 0.0f) {
      format.cTabCount = 1;
      format.rgxTabs[0] = twips(para.startIndent + para.hanging);
    }
    CHARRANGE range = {para.start, para.end};
    SendMessageW(m_hwnd, EM_EXSETSEL, 0, reinterpret_cast<LPARAM>(&range));
    SendMessageW(m_hwnd, EM_SETPARAFORMAT, 0, reinterpret_cast<LPARAM>(&format));
  }

  CHARRANGE top = {0, 0};
  SendMessageW(m_hwnd, EM_EXSETSEL, 0, reinterpret_cast<LPARAM>(&top));
  const int margin = DpiUtils::scale(MARGIN_DIPS, DpiUtils::getDpiForWindow(m_hwnd));
  SendMessageW(m_hwnd, EM_SETMARGINS, EC_LEFTMARGIN | EC_RIGHTMARGIN, MAKELPARAM(margin, margin));
  SendMessageW(m_hwnd, WM_VSCROLL, SB_TOP, 0);
  SendMessageW(m_hwnd, WM_SETREDRAW, TRUE, 0);
  InvalidateRect(m_hwnd, 0, TRUE);
}
