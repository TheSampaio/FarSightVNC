// Copyright (C) 2026 FarSightVNC contributors.
//
// This file is part of FarSightVNC, a modified version of TightVNC.
//
// This program is free software; you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation; either version 2 of the License, or
// (at your option) any later version.
//
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License along
// with this program; if not, write to the Free Software Foundation, Inc.,
// 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
//
// Added for FarSightVNC on 2026-11-16: clipboard file paste, viewer side
// against a fake server.
//

// The viewer side of the clipboard file paste against a fake RFB server, with
// no second machine, no socket and no desktop input. A real RemoteViewerCore
// runs its protocol thread against two in-memory pipes; the test thread plays
// the server: it speaks the TightVNC handshake with the capability list the
// scenario wants, parses everything the viewer writes into a log of events,
// and answers (or refuses to answer) the CLIPBOARD_FILES_* requests.
//
// What is real: RemoteViewerCore (handshake, capability negotiation, message
// dispatch, input queue), ClipboardFilesCapability, ClipboardFilesHandshake,
// RfbKeySym and PasteKeystroke. What is not: the window, the clipboard, the
// file transfer upload and the progress dialog (see the report in
// MODIFICATIONS.md); the upload step of the paste is simply skipped.

#include <stdio.h>
#include <string.h>
#include <chrono>
#include <condition_variable>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "rfb/AuthDefs.h"
#include "rfb/ClipboardFilesMessage.h"
#include "rfb/MsgDefs.h"
#include "rfb/RfbKeySym.h"
#include "rfb/VendorDefs.h"
#include "viewer-core/AuthHandler.h"
#include "viewer-core/ClipboardFilesCapability.h"
#include "viewer-core/ClipboardFilesHandshake.h"
#include "viewer-core/CoreEventsAdapter.h"
#include "viewer-core/PasteKeystroke.h"
#include "viewer-core/RemoteViewerCore.h"

#include "io-lib/InputStream.h"
#include "io-lib/OutputStream.h"
#include "log-writer/LogWriter.h"
#include "network/RfbInputGate.h"
#include "network/RfbOutputGate.h"
#include "util/Exception.h"

namespace {

static_assert(sizeof(TCHAR) == sizeof(wchar_t), "the test assumes a Unicode build");

int *g_passed = 0;
int *g_failed = 0;

void check(bool ok, const char *what)
{
  if (ok) {
    (*g_passed)++;
  } else {
    (*g_failed)++;
    printf("FAIL: %s\n", what);
  }
}

const int WAIT_MS = 5000;      // a step that must happen
const int QUIET_MS = 250;      // a step that must NOT happen

const char *const ROOT = "C:\\Users\\tester\\AppData\\Local\\Temp\\FarSightVNC\\Clipboard\\a1b2c3";
const char *const OTHER_ROOT = "D:\\Temp\\FarSightVNC\\Clipboard\\z9y8x7";

std::string narrow(const StringStorage &text)
{
  const TCHAR *wide = text.getString();
  int length = static_cast<int>(text.getLength());
  if (length == 0) {
    return std::string();
  }
  int bytes = WideCharToMultiByte(CP_UTF8, 0, wide, length, 0, 0, 0, 0);
  std::string out(static_cast<size_t>(bytes), '\0');
  WideCharToMultiByte(CP_UTF8, 0, wide, length, &out[0], bytes, 0, 0);
  return out;
}

StringStorage widen(const std::string &utf8)
{
  if (utf8.empty()) {
    return StringStorage(_T(""));
  }
  int chars = MultiByteToWideChar(CP_UTF8, 0, utf8.data(),
                                  static_cast<int>(utf8.size()), 0, 0);
  std::wstring out(static_cast<size_t>(chars), L'\0');
  MultiByteToWideChar(CP_UTF8, 0, utf8.data(), static_cast<int>(utf8.size()),
                      &out[0], chars);
  return StringStorage(out.c_str());
}

//----------------------------------------------------------------------------
// A byte pipe between the viewer and the fake server.

class Pipe
{
public:
  Pipe() : m_closed(false) {}

  void push(const void *data, size_t length)
  {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_data.append(static_cast<const char *>(data), length);
    m_cv.notify_all();
  }

  // Copies up to 'length' bytes. Waits up to 'timeoutMs' (forever when
  // negative) for at least one. Returns 0 on a timeout, and sets 'ended' when
  // the pipe is closed and empty.
  size_t pop(void *buffer, size_t length, int timeoutMs, bool *ended)
  {
    std::unique_lock<std::mutex> lock(m_mutex);
    auto ready = [this] { return !m_data.empty() || m_closed; };
    if (timeoutMs < 0) {
      m_cv.wait(lock, ready);
    } else {
      m_cv.wait_for(lock, std::chrono::milliseconds(timeoutMs), ready);
    }
    *ended = m_data.empty() && m_closed;
    size_t count = length < m_data.size() ? length : m_data.size();
    memcpy(buffer, m_data.data(), count);
    m_data.erase(0, count);
    return count;
  }

  size_t pending()
  {
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_data.size();
  }

  void close()
  {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_closed = true;
    m_cv.notify_all();
  }

private:
  std::mutex m_mutex;
  std::condition_variable m_cv;
  std::string m_data;
  bool m_closed;
};

class PipeInput : public InputStream
{
public:
  PipeInput(Pipe *pipe) : m_pipe(pipe) {}

  virtual size_t read(void *buffer, size_t length)
  {
    bool ended = false;
    size_t count = m_pipe->pop(buffer, length, -1, &ended);
    if (ended) {
      throw Exception(_T("pipe closed"));
    }
    return count;
  }

  virtual size_t available() { return m_pipe->pending(); }

private:
  Pipe *m_pipe;
};

class PipeOutput : public OutputStream
{
public:
  PipeOutput(Pipe *pipe) : m_pipe(pipe) {}

  virtual size_t write(const void *buffer, size_t length)
  {
    m_pipe->push(buffer, length);
    return length;
  }

private:
  Pipe *m_pipe;
};

//----------------------------------------------------------------------------
// What the viewer wrote, parsed by the fake server.

struct Ev
{
  enum Kind {
    KEY, FB_REQUEST, SET_ENCODINGS, SET_PIXEL_FORMAT, POINTER, CUT_TEXT,
    FILES_BEGIN, FILES_SET, OTHER
  } kind;
  bool down;
  unsigned key;
  unsigned code;
  std::string root;
  std::vector<std::string> paths;

  Ev() : kind(OTHER), down(false), key(0), code(0) {}
};

class EventLog
{
public:
  void push(const Ev &event)
  {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_events.push_back(event);
    m_cv.notify_all();
  }

  std::vector<Ev> snapshot()
  {
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_events;
  }

  size_t count(Ev::Kind kind)
  {
    std::lock_guard<std::mutex> lock(m_mutex);
    size_t n = 0;
    for (size_t i = 0; i < m_events.size(); i++) {
      if (m_events[i].kind == kind) {
        n++;
      }
    }
    return n;
  }

  // Waits until at least 'n' events of 'kind' were seen.
  bool waitFor(Ev::Kind kind, size_t n, int timeoutMs)
  {
    std::unique_lock<std::mutex> lock(m_mutex);
    return m_cv.wait_for(lock, std::chrono::milliseconds(timeoutMs), [&] {
      size_t seen = 0;
      for (size_t i = 0; i < m_events.size(); i++) {
        if (m_events[i].kind == kind) {
          seen++;
        }
      }
      return seen >= n;
    });
  }

private:
  std::mutex m_mutex;
  std::condition_variable m_cv;
  std::vector<Ev> m_events;
};

//----------------------------------------------------------------------------
// The viewer side objects of one session.

// A server without a password: the viewer registers VNC authentication only,
// and the authentication is not what is being tested.
class NoAuth : public AuthHandler
{
public:
  NoAuth() : AuthHandler(AuthDefs::NONE) {}
  virtual void authenticate(DataInputStream *, DataOutputStream *) {}
  virtual void addAuthCapability(CapabilitiesManager *manager)
  {
    manager->addAuthCapability(this, AuthDefs::NONE, VendorDefs::STANDARD,
                               AuthDefs::SIG_NONE);
  }
};

// What the server advertises in its capability list.
enum Advertise {
  ADV_FULL,          // BEGIN, SET and REPLY with the FarSight vendor
  ADV_NOTHING,       // a stock TightVNC server
  ADV_BEGIN_ONLY,    // half of the extension
  ADV_NO_REPLY,      // both client messages, but no server reply capability
  ADV_WRONG_VENDOR   // the right codes and names under the TightVNC vendor
};

class Session : public CoreEventsAdapter, public RfbKeySymListener
{
public:
  Session(Advertise advertise)
  : m_log(0),
    m_input(&m_toViewer),
    m_output(&m_toServer),
    m_inGate(&m_input),
    m_outGate(&m_output),
    m_capability(0),
    m_core(0),
    m_connected(false),
    m_serverOk(false)
  {
    memset(m_keys, 0, sizeof(m_keys));
    m_capability.addCapabilities(&m_core);
    m_noAuth.addAuthCapability(&m_core);
    m_core.start(&m_inGate, &m_outGate, this, true);

    m_serverOk = serveHandshake(advertise);
    if (m_serverOk) {
      m_reader = std::thread(&Session::readClientMessages, this);
      std::unique_lock<std::mutex> lock(m_stateMutex);
      m_stateCv.wait_for(lock, std::chrono::milliseconds(WAIT_MS),
                         [this] { return m_connected; });
    }

    HKL layout = LoadKeyboardLayoutW(L"00000409", KLF_NOTELLSHELL);
    m_keySym.reset(new RfbKeySym(this, &m_log));
    if (layout != 0) {
      m_keySym->setKeyboardLayout(layout);
    }
    m_keySym->setToggleKeysOverride(false, false);
    m_keySym->setKeyStateOverride(m_keys);
  }

  ~Session()
  {
    m_core.stop();
    m_toViewer.close();
    m_toServer.close();
    m_core.waitTermination();
    if (m_reader.joinable()) {
      m_reader.join();
    }
  }

  bool ready() const { return m_serverOk && m_connected; }

  ClipboardFilesCapability &capability() { return m_capability; }
  LogWriter *log() { return &m_log; }
  EventLog &events() { return m_events; }
  RfbKeySym &keySym() { return *m_keySym; }
  const std::vector<UINT32> &enabledClientCodes() const { return m_enabledCodes; }

  // Marks the physical Ctrl key as held or released for RfbKeySym, like the
  // key state table of the viewer thread.
  void setPhysicalCtrl(bool held)
  {
    m_keys[VK_CONTROL] = held ? 0x80 : 0;
    m_keys[VK_LCONTROL] = held ? 0x80 : 0;
  }

  //
  // Server to viewer messages.
  //
  void sendReply(UINT8 kind, UINT8 status, const std::string &text)
  {
    std::string message;
    put32(&message, ServerMsgDefs::CLIPBOARD_FILES_REPLY);
    message += static_cast<char>(kind);
    message += static_cast<char>(status);
    put32(&message, static_cast<UINT32>(text.size() + 1));
    message += text;
    message += '\0'; // DataOutputStream::writeUTF8 includes the terminator.
    m_toViewer.push(message.data(), message.size());
  }

  void sendRoot(const std::string &root) { sendReply(ClipboardFilesMessage::REPLY_ROOT, 0, root); }
  void sendSetOk() { sendReply(ClipboardFilesMessage::REPLY_SET, 0, ""); }

  // Events of the viewer, in the order the server read them.
  static void put32(std::string *out, UINT32 value)
  {
    for (int shift = 24; shift >= 0; shift -= 8) {
      *out += static_cast<char>((value >> shift) & 0xff);
    }
  }

  // RfbKeySymListener.
  virtual void onRfbKeySymEvent(unsigned int rfbKeySym, bool down)
  {
    m_core.sendKeyboardEvent(down, rfbKeySym);
  }

  // CoreEventsAdapter.
  virtual void onConnected(RfbOutputGate *output)
  {
    // The same wiring as ViewerWindow::onConnected.
    m_capability.setOutput(output);
    std::vector<UINT32> clientCodes;
    m_core.getEnabledClientMsgCapabilities(&clientCodes);
    std::vector<UINT32> serverCodes;
    m_core.getEnabledServerMsgCapabilities(&serverCodes);
    m_capability.updateSupport(&clientCodes, &serverCodes);
    std::lock_guard<std::mutex> lock(m_stateMutex);
    m_enabledCodes = clientCodes;
    m_connected = true;
    m_stateCv.notify_all();
  }

private:
  bool readExact(size_t length, std::string *out, int timeoutMs)
  {
    out->clear();
    while (out->size() < length) {
      char buffer[256];
      size_t want = length - out->size();
      bool ended = false;
      size_t got = m_toServer.pop(buffer, want < sizeof(buffer) ? want : sizeof(buffer),
                                  timeoutMs, &ended);
      if (got == 0) {
        return false;
      }
      out->append(buffer, got);
    }
    return true;
  }

  static void put16(std::string *out, unsigned value)
  {
    *out += static_cast<char>((value >> 8) & 0xff);
    *out += static_cast<char>(value & 0xff);
  }

  static void putCap(std::string *out, UINT32 code, const char *vendor,
                     const char *name)
  {
    put32(out, code);
    out->append(vendor, 4);
    out->append(name, 8);
  }

  // The handshake of a TightVNC server (security type Tight, no tunnelling,
  // no authentication) with the capability list the scenario asks for.
  bool serveHandshake(Advertise advertise)
  {
    std::string message, got;
    message = "RFB 003.008\n";
    m_toViewer.push(message.data(), message.size());
    if (!readExact(12, &got, WAIT_MS)) return false;

    message.clear();
    message += static_cast<char>(1);                      // one security type
    message += static_cast<char>(SecurityDefs::TIGHT);
    m_toViewer.push(message.data(), message.size());
    if (!readExact(1, &got, WAIT_MS)) return false;       // the selected type
    if (static_cast<unsigned char>(got[0]) != SecurityDefs::TIGHT) return false;

    message.clear();
    put32(&message, 0);                                   // no tunnelling types
    put32(&message, 1);                                   // one authentication type
    putCap(&message, AuthDefs::NONE, VendorDefs::STANDARD, AuthDefs::SIG_NONE);
    m_toViewer.push(message.data(), message.size());
    if (!readExact(4, &got, WAIT_MS)) return false;       // the selected authentication

    message.clear();
    put32(&message, 0);                                   // authentication result: ok
    m_toViewer.push(message.data(), message.size());
    if (!readExact(1, &got, WAIT_MS)) return false;       // ClientInit

    // ServerInit: 16 x 16, 32 bits, "fake".
    message.clear();
    put16(&message, 16);
    put16(&message, 16);
    message += static_cast<char>(32);                     // bits per pixel
    message += static_cast<char>(24);                     // depth
    message += static_cast<char>(0);                      // big endian
    message += static_cast<char>(1);                      // true colour
    put16(&message, 255);
    put16(&message, 255);
    put16(&message, 255);
    message += static_cast<char>(16);
    message += static_cast<char>(8);
    message += static_cast<char>(0);
    message.append(3, '\0');
    put32(&message, 4);
    message += "fake";

    // The capability lists.
    std::string serverCaps, clientCaps;
    unsigned serverCount = 0, clientCount = 0;
    const char *vendor = advertise == ADV_WRONG_VENDOR ? VendorDefs::TIGHTVNC
                                                       : VendorDefs::FARSIGHT;
    if (advertise != ADV_NOTHING) {
      putCap(&clientCaps, ClientMsgDefs::CLIPBOARD_FILES_BEGIN, vendor,
             ClipboardFilesDefs::CLIPBOARD_FILES_BEGIN_SIG);
      clientCount++;
      if (advertise != ADV_BEGIN_ONLY) {
        putCap(&clientCaps, ClientMsgDefs::CLIPBOARD_FILES_SET, vendor,
               ClipboardFilesDefs::CLIPBOARD_FILES_SET_SIG);
        clientCount++;
        if (advertise != ADV_NO_REPLY) {
          putCap(&serverCaps, ServerMsgDefs::CLIPBOARD_FILES_REPLY, vendor,
                 ClipboardFilesDefs::CLIPBOARD_FILES_REPLY_SIG);
          serverCount++;
        }
      }
    }
    put16(&message, serverCount);
    put16(&message, clientCount);
    put16(&message, 0);                                   // encodings
    put16(&message, 0);                                   // padding
    message += serverCaps;
    message += clientCaps;
    m_toViewer.push(message.data(), message.size());
    return true;
  }

  // Reads the client messages until the pipe closes. A message the fake server
  // does not know ends the parsing and is recorded as OTHER, because after it
  // the stream cannot be followed.
  void readClientMessages()
  {
    for (;;) {
      std::string head;
      if (!readExact(1, &head, -1)) {
        return;
      }
      Ev event;
      unsigned type = static_cast<unsigned char>(head[0]);
      std::string body;
      if (type == 0xFC) {
        if (!readExact(3, &body, -1)) return;
        event.code = 0xFC000000u | (static_cast<unsigned char>(body[0]) << 16) |
                     (static_cast<unsigned char>(body[1]) << 8) |
                     static_cast<unsigned char>(body[2]);
        type = event.code;
      } else {
        event.code = type;
      }

      bool ok = true;
      switch (type) {
      case 0: // SetPixelFormat
        event.kind = Ev::SET_PIXEL_FORMAT;
        ok = readExact(19, &body, -1);
        break;
      case 2: { // SetEncodings
        event.kind = Ev::SET_ENCODINGS;
        ok = readExact(3, &body, -1);
        if (ok) {
          unsigned count = (static_cast<unsigned char>(body[1]) << 8) |
                           static_cast<unsigned char>(body[2]);
          ok = readExact(4 * count, &body, -1);
        }
        break;
      }
      case 3: // FramebufferUpdateRequest
        event.kind = Ev::FB_REQUEST;
        ok = readExact(9, &body, -1);
        break;
      case 4: // KeyEvent
        event.kind = Ev::KEY;
        ok = readExact(7, &body, -1);
        if (ok) {
          event.down = body[0] != 0;
          event.key = (static_cast<unsigned char>(body[3]) << 24) |
                      (static_cast<unsigned char>(body[4]) << 16) |
                      (static_cast<unsigned char>(body[5]) << 8) |
                      static_cast<unsigned char>(body[6]);
        }
        break;
      case 5: // PointerEvent
        event.kind = Ev::POINTER;
        ok = readExact(5, &body, -1);
        break;
      case 6: { // ClientCutText
        event.kind = Ev::CUT_TEXT;
        ok = readExact(7, &body, -1);
        if (ok) {
          unsigned length = (static_cast<unsigned char>(body[3]) << 24) |
                            (static_cast<unsigned char>(body[4]) << 16) |
                            (static_cast<unsigned char>(body[5]) << 8) |
                            static_cast<unsigned char>(body[6]);
          ok = readExact(length, &body, -1);
        }
        break;
      }
      case ClientMsgDefs::CLIPBOARD_FILES_BEGIN:
        event.kind = Ev::FILES_BEGIN;
        break;
      case ClientMsgDefs::CLIPBOARD_FILES_SET:
        event.kind = Ev::FILES_SET;
        ok = readString(&event.root);
        if (ok) {
          std::string countBytes;
          ok = readExact(4, &countBytes, -1);
          unsigned count = ok ? ((static_cast<unsigned char>(countBytes[0]) << 24) |
                                 (static_cast<unsigned char>(countBytes[1]) << 16) |
                                 (static_cast<unsigned char>(countBytes[2]) << 8) |
                                 static_cast<unsigned char>(countBytes[3])) : 0;
          for (unsigned i = 0; ok && i < count && i < 4096; i++) {
            std::string path;
            ok = readString(&path);
            event.paths.push_back(path);
          }
        }
        break;
      default:
        event.kind = Ev::OTHER;
        m_events.push(event);
        return;
      }
      if (!ok) {
        return;
      }
      m_events.push(event);
    }
  }

  bool readString(std::string *out)
  {
    std::string lengthBytes;
    if (!readExact(4, &lengthBytes, -1)) {
      return false;
    }
    unsigned length = (static_cast<unsigned char>(lengthBytes[0]) << 24) |
                      (static_cast<unsigned char>(lengthBytes[1]) << 16) |
                      (static_cast<unsigned char>(lengthBytes[2]) << 8) |
                      static_cast<unsigned char>(lengthBytes[3]);
    if (length > 1024 * 1024) {
      return false;
    }
    if (!readExact(length, out, -1)) {
      return false;
    }
    // The production writeUTF8 format counts its terminating zero.
    if (!out->empty() && out->back() == '\0') {
      out->pop_back();
    }
    return true;
  }

  LogWriter m_log;
  Pipe m_toViewer;
  Pipe m_toServer;
  PipeInput m_input;
  PipeOutput m_output;
  RfbInputGate m_inGate;
  RfbOutputGate m_outGate;
  NoAuth m_noAuth;
  ClipboardFilesCapability m_capability;
  RemoteViewerCore m_core;
  std::unique_ptr<RfbKeySym> m_keySym;
  unsigned char m_keys[256];

  EventLog m_events;
  std::thread m_reader;
  std::mutex m_stateMutex;
  std::condition_variable m_stateCv;
  bool m_connected;
  bool m_serverOk;
  std::vector<UINT32> m_enabledCodes;
};

//----------------------------------------------------------------------------
// The staging thread of a paste: runs a blocking handshake call and keeps its
// result, so that the test thread can answer as the server meanwhile.

class Call
{
public:
  Call() : m_done(false), m_result(false) {}

  template <typename Function>
  void start(Function function)
  {
    m_thread = std::thread([this, function] {
      bool result = function();
      std::lock_guard<std::mutex> lock(m_mutex);
      m_result = result;
      m_done = true;
      m_cv.notify_all();
    });
  }

  bool waitDone(int timeoutMs)
  {
    std::unique_lock<std::mutex> lock(m_mutex);
    return m_cv.wait_for(lock, std::chrono::milliseconds(timeoutMs),
                         [this] { return m_done; });
  }

  bool done()
  {
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_done;
  }

  bool result()
  {
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_result;
  }

  ~Call()
  {
    if (m_thread.joinable()) {
      m_thread.join();
    }
  }

private:
  std::mutex m_mutex;
  std::condition_variable m_cv;
  std::thread m_thread;
  bool m_done;
  bool m_result;
};

void sleepMs(int ms)
{
  std::this_thread::sleep_for(std::chrono::milliseconds(ms));
}

// The paste decision with everything the session can supply and the rest set
// to "ready", like DesktopWindow::tryStartFilePaste.
bool wouldIntercept(Session &session)
{
  ClipboardFilesCapability::PasteContext context;
  context.capabilityEnabled = session.capability().isSupported();
  context.fileTransferEnabled = true;
  context.connected = true;
  context.clipboardEnabled = true;
  context.clipboardHasFiles = true;
  return ClipboardFilesCapability::shouldInterceptPaste(context);
}

bool contains(const std::vector<UINT32> &codes, UINT32 code)
{
  for (size_t i = 0; i < codes.size(); i++) {
    if (codes[i] == code) {
      return true;
    }
  }
  return false;
}

// Key events in the order the server read them, as "+ffe3" and "-76".
std::string keyTrace(const std::vector<Ev> &events, size_t from = 0)
{
  std::string trace;
  for (size_t i = from; i < events.size(); i++) {
    if (events[i].kind != Ev::KEY) {
      continue;
    }
    char text[24];
    sprintf_s(text, "%s%x", events[i].down ? "+" : "-", events[i].key);
    if (!trace.empty()) {
      trace += " ";
    }
    trace += text;
  }
  return trace;
}

bool keysBalanced(const std::vector<Ev> &events)
{
  std::vector<unsigned> held;
  for (size_t i = 0; i < events.size(); i++) {
    if (events[i].kind != Ev::KEY) {
      continue;
    }
    if (events[i].down) {
      held.push_back(events[i].key);
      continue;
    }
    bool found = false;
    for (size_t j = 0; j < held.size(); j++) {
      if (held[j] == events[i].key) {
        held.erase(held.begin() + j);
        found = true;
        break;
      }
    }
    if (!found) {
      return false;
    }
  }
  return held.empty();
}

// The plain Ctrl+V a stock server must see: what DesktopWindow::onKey sends
// when the paste is not intercepted.
void typePlainCtrlV(Session &session)
{
  UINT scanCtrl = MapVirtualKey(VK_LCONTROL, MAPVK_VK_TO_VSC);
  UINT scanV = MapVirtualKey('V', MAPVK_VK_TO_VSC);
  session.setPhysicalCtrl(true);
  session.keySym().processKeyEvent(VK_LCONTROL, scanCtrl << 16);
  session.keySym().processKeyEvent('V', scanV << 16);
  session.keySym().processKeyEvent('V', (scanV << 16) | 0x80000000);
  session.keySym().processKeyEvent(VK_LCONTROL, (scanCtrl << 16) | 0x80000000);
  session.setPhysicalCtrl(false);
}

const char *const CTRL_V = "+ffe3 +76 -76 -ffe3";

//----------------------------------------------------------------------------
// 1. Capability negotiation.

void testNegotiationFull()
{
  Session session(ADV_FULL);
  check(session.ready(), "negotiation: the session reaches the connected state");
  check(session.capability().isSupported(),
        "negotiation: both messages advertised -> the paste path is live");
  check(contains(session.enabledClientCodes(), ClientMsgDefs::CLIPBOARD_FILES_BEGIN) &&
        contains(session.enabledClientCodes(), ClientMsgDefs::CLIPBOARD_FILES_SET),
        "negotiation: the viewer registered BEGIN and SET, and the FarSight vendor matched");
  check(wouldIntercept(session),
        "negotiation: with the capability the paste decision intercepts Ctrl+V");
}

// A stock TightVNC server: nothing of the extension is advertised. The paste
// path stays dormant and nothing at all is written for it.
void testNegotiationStockServer()
{
  Session session(ADV_NOTHING);
  check(session.ready(), "stock server: the session reaches the connected state");
  check(!session.capability().isSupported(),
        "stock server: the capability is not supported");
  check(!contains(session.enabledClientCodes(), ClientMsgDefs::CLIPBOARD_FILES_BEGIN) &&
        !contains(session.enabledClientCodes(), ClientMsgDefs::CLIPBOARD_FILES_SET),
        "stock server: BEGIN and SET are not among the enabled client messages");
  check(!wouldIntercept(session),
        "stock server: the paste decision does not intercept Ctrl+V");

  // Even a caller that ignores the decision writes nothing.
  StringStorage root, error;
  ClipboardFilesHandshake handshake(&session.capability(), session.log());
  bool ok = handshake.requestStagingRoot(0, 200, &root, &error);
  check(!ok, "stock server: the handshake refuses to start");
  std::vector<StringStorage> names(1, StringStorage(_T("a.txt")));
  check(!handshake.announceStagedFiles(&root, &names, 0, 200, &error),
        "stock server: the file list is not announced");
  ClipboardFilesMessage message;
  message.setRoot(_T("C:\\x\\FarSightVNC\\Clipboard\\t"));
  message.addPath(_T("C:\\x\\FarSightVNC\\Clipboard\\t\\a"));
  session.capability().sendBegin();
  session.capability().sendSet(&message);

  // The keystroke goes through as a plain Ctrl+V, and is the only thing that
  // reaches the server besides the ordinary session traffic.
  typePlainCtrlV(session);
  bool seen = session.events().waitFor(Ev::KEY, 4, WAIT_MS);
  check(seen, "stock server: the plain Ctrl+V reaches the server");
  sleepMs(QUIET_MS);
  std::vector<Ev> events = session.events().snapshot();
  check(keyTrace(events) == CTRL_V,
        "stock server: the keystroke is exactly Ctrl down, V down, V up, Ctrl up");
  check(session.events().count(Ev::FILES_BEGIN) == 0 &&
        session.events().count(Ev::FILES_SET) == 0,
        "stock server: no CLIPBOARD_FILES_* message was ever written");
  check(session.events().count(Ev::OTHER) == 0,
        "stock server: every message the viewer wrote is a standard RFB message");
}

void testNegotiationPartial()
{
  {
    Session session(ADV_NO_REPLY);
    check(session.ready() && !session.capability().isSupported(),
          "partial: BEGIN and SET without REPLY do not enable file paste");
    check(!wouldIntercept(session),
          "partial: missing REPLY leaves plain Ctrl+V unchanged");
    session.capability().sendBegin();
    typePlainCtrlV(session);
    check(session.events().waitFor(Ev::KEY, 4, WAIT_MS),
          "partial: plain Ctrl+V reaches a server without REPLY");
    sleepMs(QUIET_MS);
    check(session.events().count(Ev::FILES_BEGIN) == 0 &&
          session.events().count(Ev::FILES_SET) == 0,
          "partial: no extension message is sent without REPLY");
  }
  {
    Session session(ADV_BEGIN_ONLY);
    check(session.ready() && !session.capability().isSupported(),
          "partial: BEGIN without SET and REPLY is not enough (both messages are needed)");
    check(!wouldIntercept(session), "partial: the paste decision does not intercept");
    session.capability().sendBegin();
    typePlainCtrlV(session);
    session.events().waitFor(Ev::KEY, 4, WAIT_MS);
    sleepMs(QUIET_MS);
    check(session.events().count(Ev::FILES_BEGIN) == 0,
          "partial: BEGIN is not written when the capability is not complete");
  }
  {
    // The same codes and names under the TightVNC vendor must never match:
    // that is what the private vendor signature is for.
    Session session(ADV_WRONG_VENDOR);
    check(session.ready() && !session.capability().isSupported(),
          "vendor: the codes under the TGHT vendor are not enabled");
    check(!contains(session.enabledClientCodes(), ClientMsgDefs::CLIPBOARD_FILES_BEGIN),
          "vendor: BEGIN is not enabled by a TightVNC vendor signature");
    check(!wouldIntercept(session), "vendor: the paste decision does not intercept");
  }
}

//----------------------------------------------------------------------------
// 2. The handshake, and 5. the keystroke after it.

void testHandshakeAndKeystroke(bool physicalCtrlHeld)
{
  const char *label = physicalCtrlHeld ? "handshake (Ctrl still held)"
                                       : "handshake (Ctrl released)";
  char text[160];
  Session session(ADV_FULL);
  check(session.ready(), "handshake: session ready");
  if (!session.ready()) {
    return;
  }
  ClipboardFilesHandshake handshake(&session.capability(), session.log());

  StringStorage root, error;
  Call rootCall;
  rootCall.start([&] { return handshake.requestStagingRoot(0, 10000, &root, &error); });

  bool began = session.events().waitFor(Ev::FILES_BEGIN, 1, WAIT_MS);
  sprintf_s(text, "%s: BEGIN reaches the server", label);
  check(began, text);
  sleepMs(QUIET_MS);
  sprintf_s(text, "%s: the viewer waits for the reply (no SET, no key yet)", label);
  check(!rootCall.done() && session.events().count(Ev::FILES_SET) == 0 &&
        session.events().count(Ev::KEY) == 0, text);

  session.sendRoot(ROOT);
  bool rootDone = rootCall.waitDone(WAIT_MS);
  sprintf_s(text, "%s: the staging root is accepted", label);
  check(rootDone && rootCall.result() && narrow(root) == ROOT, text);

  // A non-ASCII name must survive the UTF-8 round trip.
  std::vector<StringStorage> names;
  names.push_back(StringStorage(_T("report.txt")));
  names.push_back(widen("relat\xC3\xB3rio \xC3\xA7" "a.docx"));
  names.push_back(StringStorage(_T("folder")));
  Call setCall;
  setCall.start([&] { return handshake.announceStagedFiles(&root, &names, 0, 10000, &error); });

  bool setSeen = session.events().waitFor(Ev::FILES_SET, 1, WAIT_MS);
  sprintf_s(text, "%s: SET reaches the server", label);
  check(setSeen, text);

  std::vector<Ev> events = session.events().snapshot();
  const Ev *set = 0;
  for (size_t i = 0; i < events.size(); i++) {
    if (events[i].kind == Ev::FILES_SET) {
      set = &events[i];
    }
  }
  sprintf_s(text, "%s: SET carries exactly the root the server handed out", label);
  check(set != 0 && set->root == ROOT, text);
  std::vector<std::string> expected;
  expected.push_back(std::string(ROOT) + "\\report.txt");
  expected.push_back(std::string(ROOT) + "\\relat\xC3\xB3rio \xC3\xA7" "a.docx");
  expected.push_back(std::string(ROOT) + "\\folder");
  sprintf_s(text, "%s: SET carries the expected path list, in order", label);
  check(set != 0 && set->paths == expected, text);
  if (set != 0) {
    ClipboardFilesMessage rebuilt;
    rebuilt.setRoot(widen(set->root).getString());
    for (size_t i = 0; i < set->paths.size(); i++) {
      rebuilt.addPath(widen(set->paths[i]).getString());
    }
    sprintf_s(text, "%s: the server would accept the list it received", label);
    check(rebuilt.check() == ClipboardFilesMessage::CHECK_OK, text);
  }

  // The keystroke must not exist before the reply of SET.
  sleepMs(QUIET_MS);
  sprintf_s(text, "%s: no key reaches the server before the SET reply", label);
  check(session.events().count(Ev::KEY) == 0 && !setCall.done(), text);

  session.sendSetOk();
  bool setDone = setCall.waitDone(WAIT_MS);
  sprintf_s(text, "%s: the SET reply completes the handshake", label);
  check(setDone && setCall.result(), text);

  // The caller releases the keystroke now, and only now.
  session.setPhysicalCtrl(physicalCtrlHeld);
  PasteKeystroke::send(&session.keySym());
  bool keysSeen = session.events().waitFor(Ev::KEY, 4, WAIT_MS);
  sleepMs(QUIET_MS);
  events = session.events().snapshot();
  sprintf_s(text, "%s: the synthesized Ctrl+V reaches the server", label);
  check(keysSeen, text);
  sprintf_s(text, "%s: Ctrl down, V down, V up, Ctrl up and nothing else", label);
  check(keyTrace(events) == CTRL_V, text);
  sprintf_s(text, "%s: no modifier is left pressed on the server", label);
  check(keysBalanced(events), text);

  size_t beginIndex = events.size(), setIndex = events.size(), firstKey = events.size();
  for (size_t i = 0; i < events.size(); i++) {
    if (events[i].kind == Ev::FILES_BEGIN && beginIndex == events.size()) beginIndex = i;
    if (events[i].kind == Ev::FILES_SET && setIndex == events.size()) setIndex = i;
    if (events[i].kind == Ev::KEY && firstKey == events.size()) firstKey = i;
  }
  sprintf_s(text, "%s: server order is BEGIN, SET, then the keys", label);
  check(beginIndex < setIndex && setIndex < firstKey && firstKey < events.size(), text);
  sprintf_s(text, "%s: exactly one BEGIN and one SET", label);
  check(session.events().count(Ev::FILES_BEGIN) == 1 && session.events().count(Ev::FILES_SET) == 1,
        text);
}

//----------------------------------------------------------------------------
// 3. Failure paths.

// Runs BEGIN and answers it with a REPLY_ROOT of 'status' and 'text'. Returns
// the result of the handshake call.
bool runBegin(Session &session, ClipboardFilesHandshake &handshake,
              UINT8 status, const std::string &text, StringStorage *root,
              StringStorage *error)
{
  size_t seen = session.events().count(Ev::FILES_BEGIN);
  Call call;
  call.start([&] { return handshake.requestStagingRoot(0, 10000, root, error); });
  if (!session.events().waitFor(Ev::FILES_BEGIN, seen + 1, WAIT_MS)) {
    check(false, "BEGIN reaches the server");
    return false;
  }
  session.sendReply(ClipboardFilesMessage::REPLY_ROOT, status, text);
  if (!call.waitDone(WAIT_MS)) {
    check(false, "the handshake returns after the reply");
    return false;
  }
  return call.result();
}

void testSetFailed()
{
  Session session(ADV_FULL);
  if (!session.ready()) {
    check(false, "set failed: session ready");
    return;
  }
  ClipboardFilesHandshake handshake(&session.capability(), session.log());
  StringStorage root, error;
  check(runBegin(session, handshake, 0, ROOT, &root, &error),
        "set failed: the staging root is accepted first");

  std::vector<StringStorage> names(1, StringStorage(_T("a.txt")));
  Call call;
  call.start([&] { return handshake.announceStagedFiles(&root, &names, 0, 10000, &error); });
  session.events().waitFor(Ev::FILES_SET, 1, WAIT_MS);
  session.sendReply(ClipboardFilesMessage::REPLY_SET, ClipboardFilesMessage::STATUS_FAILED,
                    "clipboard busy");
  bool done = call.waitDone(WAIT_MS);
  check(done && !call.result(), "set failed: a non-zero SET status fails the handshake");
  check(narrow(error).find("clipboard busy") != std::string::npos,
        "set failed: the reason of the server is reported");
  sleepMs(QUIET_MS);
  check(session.events().count(Ev::KEY) == 0,
        "set failed: the server sees no key at all (no Ctrl, no V)");
  check(keysBalanced(session.events().snapshot()),
        "set failed: nothing is left pressed");
}

void testBeginRefused()
{
  Session session(ADV_FULL);
  if (!session.ready()) {
    check(false, "begin refused: session ready");
    return;
  }
  ClipboardFilesHandshake handshake(&session.capability(), session.log());
  StringStorage root, error;
  bool ok = runBegin(session, handshake, ClipboardFilesMessage::STATUS_FAILED,
                     "no interactive user", &root, &error);
  check(!ok, "begin refused: a refused BEGIN fails the handshake");
  check(narrow(error).find("no interactive user") != std::string::npos,
        "begin refused: the reason of the server is reported");
  check(root.isEmpty(), "begin refused: no staging root is taken");
  sleepMs(QUIET_MS);
  check(session.events().count(Ev::FILES_SET) == 0,
        "begin refused: SET is never sent");
  check(session.events().count(Ev::KEY) == 0,
        "begin refused: the server sees no key at all (no Ctrl, no V)");
}

void testTimeoutAndCancel()
{
  Session session(ADV_FULL);
  if (!session.ready()) {
    check(false, "timeout: session ready");
    return;
  }
  ClipboardFilesHandshake handshake(&session.capability(), session.log());
  StringStorage root, error;

  // The server never answers.
  bool ok = handshake.requestStagingRoot(0, 300, &root, &error);
  check(!ok, "timeout: no reply fails the handshake");
  check(narrow(error).find("did not answer") != std::string::npos,
        "timeout: the error says the server did not answer");

  // The protocol has no request IDs. Retrying after a timeout could take an
  // old reply as the root of a new paste, so this handshake is retired.
  session.sendRoot(ROOT);
  sleepMs(QUIET_MS);
  StringStorage secondRoot;
  check(!handshake.requestStagingRoot(0, 100, &secondRoot, &error),
        "timeout: another paste requires reconnection");
  check(narrow(error).find("Reconnect") != std::string::npos && secondRoot.isEmpty(),
        "timeout: no stale root is accepted and the recovery is explained");
  session.sendRoot(OTHER_ROOT);
  sleepMs(QUIET_MS);
  check(session.events().count(Ev::FILES_BEGIN) == 1,
        "timeout: no second BEGIN is written after reply correlation is lost");

  // Cancel wakes the wait at once.
  Session cancelSession(ADV_FULL);
  check(cancelSession.ready(), "cancel: session ready");
  ClipboardFilesHandshake cancelHandshake(&cancelSession.capability(), cancelSession.log());
  HANDLE cancel = CreateEvent(0, FALSE, FALSE, 0);
  Call cancelled;
  StringStorage cancelRoot;
  cancelled.start([&] { return cancelHandshake.requestStagingRoot(cancel, 10000, &cancelRoot, &error); });
  cancelSession.events().waitFor(Ev::FILES_BEGIN, 1, WAIT_MS);
  SetEvent(cancel);
  check(cancelled.waitDone(WAIT_MS) && !cancelled.result(),
        "cancel: a cancelled paste stops waiting for the server");
  CloseHandle(cancel);
  cancelSession.sendRoot(ROOT);
  sleepMs(QUIET_MS);
  check(!cancelHandshake.requestStagingRoot(0, 100, &cancelRoot, &error),
        "cancel: a later paste cannot consume a cancelled request reply");
  check(cancelSession.events().count(Ev::FILES_BEGIN) == 1,
        "cancel: no second BEGIN is written until reconnection");
  check(session.events().count(Ev::KEY) == 0 && cancelSession.events().count(Ev::KEY) == 0,
        "timeout and cancel: no key reaches the server");
}

//----------------------------------------------------------------------------
// 4. Hostile and mismatched replies.

void testHostileRoots()
{
  const char *roots[] = {
    "",                                                         // nothing
    "C:\\Windows\\System32",                                    // not a staging folder
    "\\\\evil\\share\\FarSightVNC\\Clipboard\\t1",              // UNC
    "\\\\?\\C:\\Temp\\FarSightVNC\\Clipboard\\t1",              // device path
    "C:\\Temp\\..\\FarSightVNC\\Clipboard\\t1",                 // traversal in the path
    "C:\\Temp\\FarSightVNC\\Clipboard\\..",                     // token is ".."
    "C:\\Temp\\FarSightVNC\\Clipboard\\t1\\sub",                // one level too deep
    "C:\\Temp\\FarSightVNC\\Clipboard\\t 1;calc",               // bad token characters
    "Temp\\FarSightVNC\\Clipboard\\t1",                         // relative
  };
  Session session(ADV_FULL);
  if (!session.ready()) {
    check(false, "hostile root: session ready");
    return;
  }
  ClipboardFilesHandshake handshake(&session.capability(), session.log());
  for (size_t i = 0; i < sizeof(roots) / sizeof(roots[0]); i++) {
    StringStorage root, error;
    bool ok = runBegin(session, handshake, 0, roots[i], &root, &error);
    char text[200];
    sprintf_s(text, "hostile root: \"%s\" is refused", roots[i]);
    check(!ok && root.isEmpty(), text);
  }
  sleepMs(QUIET_MS);
  check(session.events().count(Ev::FILES_SET) == 0,
        "hostile root: SET is never sent after a bad root");
  check(session.events().count(Ev::KEY) == 0,
        "hostile root: the server sees no key at all");
}

void testMismatchedReplies()
{
  Session session(ADV_FULL);
  if (!session.ready()) {
    check(false, "mismatch: session ready");
    return;
  }
  ClipboardFilesHandshake handshake(&session.capability(), session.log());
  StringStorage root, error;

  // A SET confirmation before anything was asked is not an answer to BEGIN.
  Call beginCall;
  beginCall.start([&] { return handshake.requestStagingRoot(0, 10000, &root, &error); });
  session.events().waitFor(Ev::FILES_BEGIN, 1, WAIT_MS);
  session.sendSetOk();
  sleepMs(QUIET_MS);
  check(!beginCall.done(), "mismatch: a SET reply does not answer BEGIN");
  session.sendRoot(ROOT);
  check(beginCall.waitDone(WAIT_MS) && beginCall.result() && narrow(root) == ROOT,
        "mismatch: the real root reply still answers BEGIN");

  // A second, different root after the first was accepted changes nothing.
  session.sendRoot(OTHER_ROOT);
  sleepMs(QUIET_MS);
  check(narrow(root) == ROOT, "mismatch: a second root reply does not replace the root");

  // An early SET confirmation, before SET was even sent, is ignored; so is a
  // root reply while SET is outstanding.
  session.sendSetOk();
  sleepMs(QUIET_MS);
  std::vector<StringStorage> names(1, StringStorage(_T("a.txt")));
  Call setCall;
  setCall.start([&] { return handshake.announceStagedFiles(&root, &names, 0, 10000, &error); });
  session.events().waitFor(Ev::FILES_SET, 1, WAIT_MS);
  sleepMs(QUIET_MS);
  check(!setCall.done(), "mismatch: an early SET confirmation does not confirm the paste");
  session.sendRoot(OTHER_ROOT);
  sleepMs(QUIET_MS);
  check(!setCall.done(), "mismatch: a root reply does not confirm SET");

  std::vector<Ev> events = session.events().snapshot();
  const Ev *set = 0;
  for (size_t i = 0; i < events.size(); i++) {
    if (events[i].kind == Ev::FILES_SET) set = &events[i];
  }
  check(set != 0 && set->root == ROOT && set->paths.size() == 1 &&
        set->paths[0] == std::string(ROOT) + "\\a.txt",
        "mismatch: SET still names the root of the first reply, not a later one");
  check(session.events().count(Ev::KEY) == 0,
        "mismatch: no key reaches the server while SET is unconfirmed");

  // An unknown reply kind is dropped.
  session.sendReply(9, 0, "?");
  sleepMs(QUIET_MS);
  check(!setCall.done(), "mismatch: an unknown reply kind confirms nothing");

  session.sendSetOk();
  check(setCall.waitDone(WAIT_MS) && setCall.result(),
        "mismatch: only the real SET reply confirms the paste");
}

} // namespace

void runClipboardFilesSessionTests(int *passed, int *failed)
{
  g_passed = passed;
  g_failed = failed;
#define RUN_TEST(t) (printf("RUN " #t "\n"), t)
  RUN_TEST(testNegotiationFull());
  RUN_TEST(testNegotiationStockServer());
  RUN_TEST(testNegotiationPartial());
  RUN_TEST(testHandshakeAndKeystroke(false));
  RUN_TEST(testHandshakeAndKeystroke(true));
  RUN_TEST(testSetFailed());
  RUN_TEST(testBeginRefused());
  RUN_TEST(testTimeoutAndCancel());
  RUN_TEST(testHostileRoots());
  RUN_TEST(testMismatchedReplies());
#undef RUN_TEST
}
