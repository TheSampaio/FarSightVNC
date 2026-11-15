// Copyright (C) 2026 FarSightVNC contributors.
// Licensed under the GNU GPL, version 2 or (at your option) any later version.

// Standard and updater-lib headers first: they must see <windows.h> macros
// (min/max) only after they are parsed.
#include "updater-lib/FsUtil.h"
#include "updater-lib/MarkdownLite.h"
#include "updater-lib/UpdateTypes.h"

#include "UpdateDialog.h"

#include "resource.h"
#include "gui/DpiUtils.h"
#include "gui/d2d/Theme.h"
#include "win-system/WindowsApplication.h"

#include <commctrl.h>
#include <shellapi.h>
#include <stdio.h>

namespace {

// Dialog template size in dialog units (IDD_UPDATE_DIALOG).
const int DIALOG_WIDTH = 300;
const int MARGIN = 7;
const int BUTTON_HEIGHT = 15;
const int BUTTON_GAP = 3;
const int CHECKBOX_HEIGHT = 10;

struct ButtonWidth
{
  UINT id;
  int width;
};

const ButtonWidth BUTTON_WIDTHS[] = {
  {IDC_UPDATE_DOWNLOAD, 64},
  {IDC_UPDATE_RESTART, 92},
  {IDC_UPDATE_LATER, 64},
  {IDC_UPDATE_SKIP, 66},
  {IDC_UPDATE_RELEASES, 72},
  {IDC_UPDATE_CANCEL_DOWNLOAD, 60},
  {IDC_UPDATE_CANCEL_STAGED, 64},
  {IDOK, 128},
  {IDCANCEL, 56},
};

int widthOf(UINT id)
{
  for (size_t i = 0; i < sizeof(BUTTON_WIDTHS) / sizeof(BUTTON_WIDTHS[0]); i++) {
    if (BUTTON_WIDTHS[i].id == id) {
      return BUTTON_WIDTHS[i].width;
    }
  }
  return 60;
}

const UINT ALL_CONTENT_CONTROLS[] = {
  IDC_UPDATE_CAP_CURRENT, IDC_UPDATE_CURRENT,
  IDC_UPDATE_CAP_AVAILABLE, IDC_UPDATE_AVAILABLE,
  IDC_UPDATE_CAP_DATE, IDC_UPDATE_DATE,
  IDC_UPDATE_CAP_CHANNEL, IDC_UPDATE_CHANNEL,
  IDC_UPDATE_NOTES_LABEL, IDC_UPDATE_NOTES, IDC_UPDATE_NOTE,
  IDC_UPDATE_GITHUB_LINK,
  IDC_UPDATE_PROGRESS, IDC_UPDATE_PROGRESS_TEXT,
};

const UINT_PTR SUBCLASS_ID = 1;

COLORREF themeColor(ThemeColor color)
{
  return RGB((color >> 16) & 0xFF, (color >> 8) & 0xFF, color & 0xFF);
}

// Plain-text fallback of the release notes for the EDIT control (CRLF).
std::wstring plainNotes(const fsv::update::Document &document)
{
  const std::wstring wide = fsv::update::utf8ToWide(fsv::update::toPlainText(document));
  std::wstring result;
  result.reserve(wide.size() + wide.size() / 16);
  for (size_t i = 0; i < wide.size(); i++) {
    if (wide[i] == L'\n') {
      result += L"\r\n";
    } else {
      result += wide[i];
    }
  }
  return result;
}

const UINT ALL_BUTTONS[] = {
  IDC_UPDATE_DOWNLOAD, IDC_UPDATE_LATER, IDC_UPDATE_SKIP, IDC_UPDATE_RELEASES,
  IDC_UPDATE_CANCEL_DOWNLOAD, IDC_UPDATE_CANCEL_STAGED, IDC_UPDATE_RESTART, IDOK, IDCANCEL,
};

}

UpdateDialog::UpdateDialog(Host *host)
: BaseDialog(IDD_UPDATE_DIALOG),
  m_host(host),
  m_state(State::Checking),
  m_autoCheck(true),
  m_includePrereleases(false),
  m_received(0),
  m_total(0),
  m_marquee(false),
  m_owner(0),
  m_richNotes(false),
  m_notesDirty(true),
  m_backgroundBrush(0),
  m_fieldBrush(0)
{
}

UpdateDialog::~UpdateDialog()
{
  deleteBrushes();
}

void UpdateDialog::setOptions(bool autoCheck, bool includePrereleases)
{
  m_autoCheck = autoCheck;
  m_includePrereleases = includePrereleases;
  if (isCreated()) {
    CheckDlgButton(m_ctrlThis.getWindow(), IDC_UPDATE_AUTOCHECK,
                   m_autoCheck ? BST_CHECKED : BST_UNCHECKED);
    CheckDlgButton(m_ctrlThis.getWindow(), IDC_UPDATE_PRERELEASE,
                   m_includePrereleases ? BST_CHECKED : BST_UNCHECKED);
  }
}

void UpdateDialog::setOwner(HWND owner)
{
  m_owner = owner;
}

void UpdateDialog::setAvailable(const AvailableInfo &info)
{
  m_available = info;
  m_notes = fsv::update::parseMarkdown(info.notesMarkdown);
  m_notesDirty = true;
}

void UpdateDialog::present(State state, const std::wstring &text, Focus focus)
{
  m_state = state;
  m_text = text;

  bool wasOpen = isCreated();
  if (!wasOpen) {
    INITCOMMONCONTROLSEX icc = {};
    icc.dwSize = sizeof(icc);
    icc.dwICC = ICC_LINK_CLASS | ICC_PROGRESS_CLASS | ICC_STANDARD_CLASSES;
    InitCommonControlsEx(&icc);

    loadIcon(IDI_APPICON);
    create(); // WM_INITDIALOG calls apply()
    WindowsApplication::addModelessDialog(m_ctrlThis.getWindow());
    if (focus == Focus::Keep) {
      focus = Focus::NoActivate;
    }
  } else {
    apply();
  }

  HWND window = m_ctrlThis.getWindow();
  if (window == 0) {
    return;
  }
  if (m_owner != 0 && IsWindow(m_owner) &&
      (HWND)GetWindowLongPtr(window, GWLP_HWNDPARENT) != m_owner) {
    SetWindowLongPtr(window, GWLP_HWNDPARENT, (LONG_PTR)m_owner);
  }
  switch (focus) {
  case Focus::Activate:
    ShowWindow(window, SW_SHOW);
    SetForegroundWindow(window);
    {
      LRESULT def = SendMessage(window, DM_GETDEFID, 0, 0);
      if (HIWORD(def) == DC_HASDEFID) {
        HWND button = GetDlgItem(window, LOWORD(def));
        if (button != 0) {
          SendMessage(window, WM_NEXTDLGCTL, (WPARAM)button, TRUE);
        }
      }
    }
    break;
  case Focus::NoActivate: {
    // Visible without taking the focus: on top of the other windows (and
    // always above its owner shell), with the taskbar button flashing until
    // FarSightVNC comes to the foreground.
    if (!IsWindowVisible(window)) {
      ShowWindow(window, SW_SHOWNOACTIVATE);
    }
    SetWindowPos(window, HWND_TOP, 0, 0, 0, 0, SWP_NOACTIVATE | SWP_NOMOVE | SWP_NOSIZE);
    FLASHWINFO flash = {};
    flash.cbSize = sizeof(flash);
    flash.hwnd = (m_owner != 0 && IsWindow(m_owner)) ? m_owner : window;
    flash.dwFlags = FLASHW_TRAY | FLASHW_TIMERNOFG;
    FlashWindowEx(&flash);
    break;
  }
  case Focus::Keep:
    break;
  }
}

void UpdateDialog::setProgress(std::uint64_t received, std::uint64_t total)
{
  m_received = received;
  m_total = total;
  if (isCreated() && m_state == State::Downloading) {
    updateProgressControls();
  }
}

void UpdateDialog::reopen(Focus focus)
{
  present(m_state, m_text, focus);
}

void UpdateDialog::close()
{
  if (isCreated()) {
    kill(IDCANCEL);
  }
}

BOOL UpdateDialog::onInitDialog()
{
  HWND window = m_ctrlThis.getWindow();
  SetWindowSubclass(window, subclassProc, SUBCLASS_ID, reinterpret_cast<DWORD_PTR>(this));
  createNotesControl();
  applyTheme();
  m_notesDirty = true;
  apply();
  // No control takes the focus here: automatic results are shown unfocused.
  return FALSE;
}

BOOL UpdateDialog::onNotify(UINT controlID, LPARAM data)
{
  const NMHDR *header = reinterpret_cast<const NMHDR *>(data);
  if (controlID == IDC_UPDATE_GITHUB_LINK && header != 0 &&
      (header->code == NM_CLICK || header->code == NM_RETURN)) {
    openUrl(m_available.releaseUrl);
    return TRUE;
  }
  // A click on a link of the release notes: the URL comes from the range map
  // of the RichEdit, and is checked again before it is opened.
  std::wstring url;
  if (m_richNotes && m_notesView.handleNotify(header, url)) {
    if (fsv::update::isAllowedLinkUrl(fsv::update::wideToUtf8(url))) {
      openUrl(url);
    }
    return TRUE;
  }
  return FALSE;
}

BOOL UpdateDialog::onCommand(UINT controlID, UINT notificationID)
{
  if (notificationID != BN_CLICKED) {
    return FALSE;
  }
  switch (controlID) {
  case IDC_UPDATE_DOWNLOAD:
    if (m_host != 0) {
      m_host->onUpdateDownloadRequested();
    }
    break;
  case IDC_UPDATE_LATER:
  case IDOK:
  case IDCANCEL:
    close();
    break;
  case IDC_UPDATE_SKIP:
    if (m_host != 0) {
      m_host->onUpdateSkipRequested();
    }
    close();
    break;
  case IDC_UPDATE_RELEASES:
    openUrl(fsv::update::utf8ToWide(fsv::update::kReleasesPageUrl));
    break;
  case IDC_UPDATE_CANCEL_DOWNLOAD:
    EnableWindow(GetDlgItem(m_ctrlThis.getWindow(), IDC_UPDATE_CANCEL_DOWNLOAD), FALSE);
    if (m_host != 0) {
      m_host->onUpdateCancelDownload();
    }
    break;
  case IDC_UPDATE_CANCEL_STAGED:
    if (m_host != 0) {
      m_host->onUpdateCancelStaged();
    }
    close();
    break;
  case IDC_UPDATE_RESTART:
    if (m_host != 0) {
      m_host->onUpdateRestartRequested();
    }
    break;
  case IDC_UPDATE_AUTOCHECK:
    m_autoCheck = IsDlgButtonChecked(m_ctrlThis.getWindow(), IDC_UPDATE_AUTOCHECK) == BST_CHECKED;
    if (m_host != 0) {
      m_host->onUpdateAutoCheckToggled(m_autoCheck);
    }
    break;
  case IDC_UPDATE_PRERELEASE:
    m_includePrereleases =
      IsDlgButtonChecked(m_ctrlThis.getWindow(), IDC_UPDATE_PRERELEASE) == BST_CHECKED;
    if (m_host != 0) {
      m_host->onUpdatePrereleaseToggled(m_includePrereleases);
    }
    break;
  }
  return FALSE;
}

BOOL UpdateDialog::onDestroy()
{
  WindowsApplication::removeModelessDialog(m_ctrlThis.getWindow());
  RemoveWindowSubclass(m_ctrlThis.getWindow(), subclassProc, SUBCLASS_ID);
  m_richNotes = false; // the RichEdit is destroyed with the dialog
  // Also when the window goes without close() (its owner was destroyed):
  // never keep a handle that Windows may give to another window.
  m_ctrlThis.setWindow(NULL);
  return FALSE;
}

void UpdateDialog::createNotesControl()
{
  HWND window = m_ctrlThis.getWindow();
  HWND placeholder = GetDlgItem(window, IDC_UPDATE_NOTES);
  m_richNotes = false;
  if (placeholder == 0) {
    return;
  }
  SendMessage(placeholder, EM_SETLIMITTEXT, 1000000, 0);
  if (!MarkdownRichEdit::ensureLibrary()) {
    return; // no msftedit.dll: the plain EDIT shows the notes as text
  }
  RECT rect;
  GetWindowRect(placeholder, &rect);
  MapWindowPoints(0, window, reinterpret_cast<POINT *>(&rect), 2);
  // The id moves to the RichEdit, so every control id lookup finds it.
  SetWindowLongPtr(placeholder, GWLP_ID, 0);
  HWND rich = m_notesView.create(window, IDC_UPDATE_NOTES, rect);
  if (rich == 0) {
    SetWindowLongPtr(placeholder, GWLP_ID, IDC_UPDATE_NOTES);
    return;
  }
  // Same place in the tab order as the EDIT of the template.
  SetWindowPos(rich, placeholder, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
  DestroyWindow(placeholder);
  m_richNotes = true;
}

void UpdateDialog::showNotes()
{
  if (!m_notesDirty) {
    return;
  }
  m_notesDirty = false;
  if (m_richNotes) {
    m_notesView.setMarkdown(m_notes);
  } else {
    setText(IDC_UPDATE_NOTES, plainNotes(m_notes));
    SendDlgItemMessage(m_ctrlThis.getWindow(), IDC_UPDATE_NOTES, EM_SETSEL, 0, 0);
  }
}

void UpdateDialog::deleteBrushes()
{
  if (m_backgroundBrush != 0) {
    DeleteObject(m_backgroundBrush);
    m_backgroundBrush = 0;
  }
  if (m_fieldBrush != 0) {
    DeleteObject(m_fieldBrush);
    m_fieldBrush = 0;
  }
}

void UpdateDialog::applyTheme()
{
  HWND window = m_ctrlThis.getWindow();
  const ThemeTokens &tokens = currentTheme();
  deleteBrushes();
  m_backgroundBrush = CreateSolidBrush(themeColor(tokens.background));
  m_fieldBrush = CreateSolidBrush(themeColor(tokens.field));
  Theme::applyWindowDarkMode(window);
  Theme::applyControlThemeTree(window);
  if (m_richNotes) {
    m_notesView.setTheme(tokens);
  }
  RedrawWindow(window, 0, 0, RDW_INVALIDATE | RDW_ERASE | RDW_ALLCHILDREN | RDW_FRAME);
}

LRESULT CALLBACK UpdateDialog::subclassProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam,
                                            UINT_PTR, DWORD_PTR data)
{
  return reinterpret_cast<UpdateDialog *>(data)->onSubclassMessage(hwnd, message, wParam, lParam);
}

// Theme colors of the dialog and its controls, as in shell/SettingsForm.cpp.
LRESULT UpdateDialog::onSubclassMessage(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam)
{
  // Not a constant (RegisterWindowMessage), so it cannot be a case label.
  if (message == Theme::changedMessage()) {
    applyTheme();
    return 0;
  }
  const ThemeTokens &tokens = currentTheme();
  switch (message) {
  case WM_CTLCOLORDLG:
    return reinterpret_cast<LRESULT>(m_backgroundBrush);
  case WM_CTLCOLORSTATIC:
  case WM_CTLCOLORBTN:
  case WM_CTLCOLOREDIT: {
    // The read-only EDIT of the notes (fallback) sends WM_CTLCOLORSTATIC.
    const bool field = message == WM_CTLCOLOREDIT ||
                       GetDlgCtrlID(reinterpret_cast<HWND>(lParam)) == IDC_UPDATE_NOTES;
    HDC dc = reinterpret_cast<HDC>(wParam);
    SetTextColor(dc, themeColor(tokens.text));
    SetBkColor(dc, themeColor(field ? tokens.field : tokens.background));
    return reinterpret_cast<LRESULT>(field ? m_fieldBrush : m_backgroundBrush);
  }
  case WM_NOTIFY: {
    // Check boxes are drawn by Theme in the dark theme (readable labels).
    const NMHDR *header = reinterpret_cast<const NMHDR *>(lParam);
    if (header != 0 && header->code == NM_CUSTOMDRAW &&
        Theme::customDrawButton(header->hwndFrom, reinterpret_cast<void *>(lParam))) {
      return CDRF_SKIPDEFAULT;
    }
    break;
  }
  case WM_DPICHANGED: {
    LRESULT result = DefSubclassProc(hwnd, message, wParam, lParam);
    if (m_richNotes) {
      m_notesView.refresh(); // font sizes follow the new DPI
    }
    return result;
  }
  case WM_PAINT: {
    // A thin border around the RichEdit: its background is the field color,
    // which is the dialog color in the light theme.
    LRESULT result = DefSubclassProc(hwnd, message, wParam, lParam);
    HWND notes = GetDlgItem(hwnd, IDC_UPDATE_NOTES);
    if (m_richNotes && notes != 0 && IsWindowVisible(notes)) {
      RECT rect;
      GetWindowRect(notes, &rect);
      MapWindowPoints(0, hwnd, reinterpret_cast<POINT *>(&rect), 2);
      InflateRect(&rect, 1, 1);
      HDC dc = GetDC(hwnd);
      HBRUSH border = CreateSolidBrush(themeColor(tokens.border));
      FrameRect(dc, &rect, border);
      DeleteObject(border);
      ReleaseDC(hwnd, dc);
    }
    return result;
  }
  }
  return DefSubclassProc(hwnd, message, wParam, lParam);
}

void UpdateDialog::showControl(UINT id, bool visible)
{
  HWND control = GetDlgItem(m_ctrlThis.getWindow(), id);
  if (control != 0) {
    ShowWindow(control, visible ? SW_SHOWNA : SW_HIDE);
  }
}

void UpdateDialog::moveControl(UINT id, int x, int y, int width, int height)
{
  HWND window = m_ctrlThis.getWindow();
  HWND control = GetDlgItem(window, id);
  if (control == 0) {
    return;
  }
  RECT rect = {x, y, x + width, y + height};
  MapDialogRect(window, &rect);
  SetWindowPos(control, 0, rect.left, rect.top, rect.right - rect.left, rect.bottom - rect.top,
               SWP_NOZORDER | SWP_NOACTIVATE);
}

void UpdateDialog::setText(UINT id, const std::wstring &text)
{
  SetDlgItemTextW(m_ctrlThis.getWindow(), id, text.c_str());
}

// Places the buttons right-aligned on the bottom row, the option check boxes
// above them, and resizes the dialog so that it ends right below the buttons.
void UpdateDialog::layout(const UINT *buttons, int buttonCount, UINT defaultButton,
                          int contentBottom)
{
  HWND window = m_ctrlThis.getWindow();

  int checkY = contentBottom + 4;
  moveControl(IDC_UPDATE_AUTOCHECK, MARGIN, checkY, 150, CHECKBOX_HEIGHT);
  moveControl(IDC_UPDATE_PRERELEASE, 160, checkY, DIALOG_WIDTH - MARGIN - 160, CHECKBOX_HEIGHT);

  int buttonY = checkY + CHECKBOX_HEIGHT + 6;
  int total = 0;
  for (int i = 0; i < buttonCount; i++) {
    total += widthOf(buttons[i]) + (i > 0 ? BUTTON_GAP : 0);
  }
  int x = DIALOG_WIDTH - MARGIN - total;
  for (size_t i = 0; i < sizeof(ALL_BUTTONS) / sizeof(ALL_BUTTONS[0]); i++) {
    showControl(ALL_BUTTONS[i], false);
  }
  for (int i = 0; i < buttonCount; i++) {
    HWND button = GetDlgItem(window, buttons[i]);
    moveControl(buttons[i], x, buttonY, widthOf(buttons[i]), BUTTON_HEIGHT);
    EnableWindow(button, TRUE);
    SendMessage(button, BM_SETSTYLE,
                buttons[i] == defaultButton ? BS_DEFPUSHBUTTON : BS_PUSHBUTTON, TRUE);
    showControl(buttons[i], true);
    x += widthOf(buttons[i]) + BUTTON_GAP;
  }
  SendMessage(window, DM_SETDEFID, defaultButton, 0);

  int clientHeight = buttonY + BUTTON_HEIGHT + MARGIN;
  RECT client = {0, 0, DIALOG_WIDTH, clientHeight};
  MapDialogRect(window, &client);
  DWORD style = (DWORD)GetWindowLongPtr(window, GWL_STYLE);
  DWORD exStyle = (DWORD)GetWindowLongPtr(window, GWL_EXSTYLE);
  AdjustWindowRectEx(&client, style, FALSE, exStyle);
  SetWindowPos(window, 0, 0, 0, client.right - client.left, client.bottom - client.top,
               SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
}

void UpdateDialog::updateProgressControls()
{
  HWND window = m_ctrlThis.getWindow();
  HWND bar = GetDlgItem(window, IDC_UPDATE_PROGRESS);
  bool wantMarquee = (m_state == State::Checking) ||
                     (m_state == State::Downloading && m_total == 0);
  LONG_PTR style = GetWindowLongPtr(bar, GWL_STYLE);
  if (wantMarquee != m_marquee || ((style & PBS_MARQUEE) != 0) != wantMarquee) {
    if (wantMarquee) {
      SetWindowLongPtr(bar, GWL_STYLE, style | PBS_MARQUEE);
      SendMessage(bar, PBM_SETMARQUEE, TRUE, 30);
    } else {
      SendMessage(bar, PBM_SETMARQUEE, FALSE, 0);
      SetWindowLongPtr(bar, GWL_STYLE, style & ~(LONG_PTR)PBS_MARQUEE);
    }
    m_marquee = wantMarquee;
  }

  if (m_state != State::Downloading) {
    return;
  }
  SendMessage(bar, PBM_SETRANGE32, 0, 1000);
  wchar_t line[96];
  double receivedMb = (double)m_received / (1024.0 * 1024.0);
  if (m_total > 0) {
    double totalMb = (double)m_total / (1024.0 * 1024.0);
    unsigned permille = (unsigned)((m_received >= m_total) ? 1000 : (m_received * 1000 / m_total));
    SendMessage(bar, PBM_SETPOS, permille, 0);
    swprintf_s(line, L"%.1f MB of %.1f MB (%u%%)", receivedMb, totalMb, permille / 10);
  } else {
    swprintf_s(line, L"%.1f MB", receivedMb);
  }
  setText(IDC_UPDATE_PROGRESS_TEXT, line);
}

void UpdateDialog::apply()
{
  HWND window = m_ctrlThis.getWindow();
  if (window == 0) {
    return;
  }

  for (size_t i = 0; i < sizeof(ALL_CONTENT_CONTROLS) / sizeof(ALL_CONTENT_CONTROLS[0]); i++) {
    showControl(ALL_CONTENT_CONTROLS[i], false);
  }
  CheckDlgButton(window, IDC_UPDATE_AUTOCHECK, m_autoCheck ? BST_CHECKED : BST_UNCHECKED);
  CheckDlgButton(window, IDC_UPDATE_PRERELEASE, m_includePrereleases ? BST_CHECKED : BST_UNCHECKED);
  setText(IDC_UPDATE_STATUS, m_text);

  // Text-only states: the status text, then the buttons.
  const int textTop = MARGIN;
  int statusHeight = 34;
  int contentBottom = 0;
  const UINT closeOnly[] = {IDCANCEL};
  const UINT releasesAndClose[] = {IDC_UPDATE_RELEASES, IDCANCEL};

  switch (m_state) {
  case State::Checking:
    statusHeight = 10;
    moveControl(IDC_UPDATE_STATUS, MARGIN, textTop, DIALOG_WIDTH - 2 * MARGIN, statusHeight);
    moveControl(IDC_UPDATE_PROGRESS, MARGIN, 22, DIALOG_WIDTH - 2 * MARGIN, 11);
    showControl(IDC_UPDATE_PROGRESS, true);
    updateProgressControls();
    layout(closeOnly, 1, IDCANCEL, 36);
    break;

  case State::UpToDate:
  case State::Disabled:
  case State::Updated:
    statusHeight = 20;
    moveControl(IDC_UPDATE_STATUS, MARGIN, textTop, DIALOG_WIDTH - 2 * MARGIN, statusHeight);
    updateProgressControls();
    if (m_state != State::Disabled) {
      layout(closeOnly, 1, IDCANCEL, textTop + statusHeight);
    } else {
      layout(releasesAndClose, 2, IDCANCEL, textTop + statusHeight);
    }
    break;

  case State::Failed:
  case State::StageFailed:
    moveControl(IDC_UPDATE_STATUS, MARGIN, textTop, DIALOG_WIDTH - 2 * MARGIN, statusHeight);
    updateProgressControls();
    layout(releasesAndClose, 2, IDC_UPDATE_RELEASES, textTop + statusHeight);
    break;

  case State::Ready: {
    moveControl(IDC_UPDATE_STATUS, MARGIN, textTop, DIALOG_WIDTH - 2 * MARGIN, statusHeight);
    updateProgressControls();
    // Installing at exit is the default: restarting closes the sessions.
    const UINT readyButtons[] = {IDC_UPDATE_RESTART, IDOK, IDC_UPDATE_CANCEL_STAGED};
    layout(readyButtons, 3, IDOK, textTop + statusHeight);
    break;
  }

  case State::Downloading: {
    statusHeight = 10;
    moveControl(IDC_UPDATE_STATUS, MARGIN, textTop, DIALOG_WIDTH - 2 * MARGIN, statusHeight);
    moveControl(IDC_UPDATE_PROGRESS, MARGIN, 22, DIALOG_WIDTH - 2 * MARGIN, 11);
    moveControl(IDC_UPDATE_PROGRESS_TEXT, MARGIN, 36, DIALOG_WIDTH - 2 * MARGIN, 9);
    showControl(IDC_UPDATE_PROGRESS, true);
    showControl(IDC_UPDATE_PROGRESS_TEXT, true);
    updateProgressControls();
    const UINT downloadButtons[] = {IDC_UPDATE_CANCEL_DOWNLOAD};
    layout(downloadButtons, 1, IDC_UPDATE_CANCEL_DOWNLOAD, 47);
    break;
  }

  case State::Available: {
    statusHeight = 10;
    moveControl(IDC_UPDATE_STATUS, MARGIN, textTop, DIALOG_WIDTH - 2 * MARGIN, statusHeight);
    setText(IDC_UPDATE_CURRENT, m_available.currentVersion);
    setText(IDC_UPDATE_AVAILABLE, m_available.availableVersion);
    setText(IDC_UPDATE_DATE, m_available.releaseDate);
    setText(IDC_UPDATE_CHANNEL, m_available.channel);
    showNotes();

    std::wstring note = m_available.note;
    if (!m_available.canInstall) {
      note = L"This release cannot be installed automatically." +
             (note.empty() ? std::wstring() : L" " + note);
    }
    setText(IDC_UPDATE_NOTE, note);

    static const UINT shown[] = {
      IDC_UPDATE_CAP_CURRENT, IDC_UPDATE_CURRENT, IDC_UPDATE_CAP_AVAILABLE, IDC_UPDATE_AVAILABLE,
      IDC_UPDATE_CAP_DATE, IDC_UPDATE_DATE, IDC_UPDATE_CAP_CHANNEL, IDC_UPDATE_CHANNEL,
      IDC_UPDATE_NOTES_LABEL, IDC_UPDATE_NOTES,
    };
    for (size_t i = 0; i < sizeof(shown) / sizeof(shown[0]); i++) {
      showControl(shown[i], true);
    }
    showControl(IDC_UPDATE_GITHUB_LINK, !m_available.releaseUrl.empty());
    contentBottom = 148;
    if (!note.empty()) {
      showControl(IDC_UPDATE_NOTE, true);
      contentBottom = 168;
    }
    if (m_available.canInstall) {
      const UINT buttons[] = {IDC_UPDATE_DOWNLOAD, IDC_UPDATE_LATER, IDC_UPDATE_SKIP};
      layout(buttons, 3, IDC_UPDATE_DOWNLOAD, contentBottom);
    } else {
      const UINT buttons[] = {IDC_UPDATE_RELEASES, IDC_UPDATE_LATER, IDC_UPDATE_SKIP};
      layout(buttons, 3, IDC_UPDATE_RELEASES, contentBottom);
    }
    break;
  }
  }
  InvalidateRect(window, 0, TRUE);
}

void UpdateDialog::openUrl(const std::wstring &url)
{
  if (url.empty()) {
    return;
  }
  ShellExecuteW(m_ctrlThis.getWindow(), L"open", url.c_str(), 0, 0, SW_SHOWNORMAL);
}
