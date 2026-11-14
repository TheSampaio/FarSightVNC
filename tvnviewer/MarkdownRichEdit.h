// Copyright (C) 2026 FarSightVNC contributors.
// Licensed under the GNU GPL, version 2 or (at your option) any later version.
//
// Read-only RichEdit 4.1 control (MSFTEDIT_CLASS) that shows a Markdown
// document parsed by updater-lib/MarkdownLite: the text goes in as plain
// Unicode text (EM_STREAMIN SF_TEXT, never RTF, so untrusted text cannot
// inject RTF or OLE objects) and each range is styled with CHARFORMAT2 and
// PARAFORMAT2. Colors and fonts follow ThemeTokens.
//
// Links: a link range only exists for an https:// URL (MarkdownLite drops the
// others) and its URL comes from a map kept here (range -> URL), never from
// the visible text. The parent forwards WM_NOTIFY to handleNotify() and must
// re-check isAllowedLinkUrl() before ShellExecute.

#ifndef _MARKDOWN_RICH_EDIT_H_
#define _MARKDOWN_RICH_EDIT_H_

// Standard and updater-lib headers first: they must not see the min/max
// macros of <windows.h>.
#include "updater-lib/MarkdownLite.h"

#include <string>
#include <vector>

#include "gui/d2d/ThemeTokens.h"
#include "util/CommonHeader.h"

class MarkdownRichEdit
{
public:
  MarkdownRichEdit();

  // Loads msftedit.dll from the system directory once. False when it is
  // missing (the caller then keeps a plain EDIT control).
  static bool ensureLibrary();

  // Creates the control as a child of parent (rc in client pixels). Returns 0
  // when the library or the window cannot be created.
  HWND create(HWND parent, int id, const RECT &rc);
  // Uses an existing, empty MSFTEDIT_CLASS window instead.
  bool attach(HWND richEdit);
  HWND hwnd() const { return m_hwnd; }

  // Colors and fonts; renders the current document again.
  void setTheme(const ThemeTokens &tokens);
  // Replaces the content. The document is kept to render it again when the
  // theme or the DPI changes.
  void setMarkdown(const fsv::update::Document &document);
  // Renders again (for example after WM_DPICHANGED).
  void refresh();

  // For WM_NOTIFY of the parent. Returns true when header is an EN_LINK of
  // this control for a left click (WM_LBUTTONUP) on a link range; outUrl is
  // then the URL stored for that range.
  bool handleNotify(const NMHDR *header, std::wstring &outUrl) const;

private:
  struct LinkRange {
    LONG start;
    LONG end;
    std::wstring url;
  };

  void configure();
  void render();
  // Font height in twips for a size in points, corrected for the monitor DPI
  // of the window (RichEdit scales twips by the system DPI only).
  LONG twips(float points) const;

  HWND m_hwnd;
  ThemeTokens m_tokens;
  fsv::update::Document m_document;
  std::vector<LinkRange> m_links;
};

#endif
