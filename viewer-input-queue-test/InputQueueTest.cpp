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

// Modified for FarSightVNC on 2026-11-02: cover pointer transition positions,
// bounded shutdown waits and fail-fast watchdogs without detached threads.

// Deterministic test for InputEventSender (the input queue that moves the
// socket writes of key, pointer and cut text messages off the UI thread). No
// network and no desktop input is involved: the RfbOutputGate writes to an
// in-memory stream that can be blocked or made to fail on demand, and the
// RFB messages that reach it are parsed back and compared.

#include "viewer-core/InputEventSender.h"
#include "viewer-core/TcpConnection.h"
#include "network/socket/SocketIPv4.h"
#include "network/RfbOutputGate.h"
#include "io-lib/OutputStream.h"
#include "log-writer/LogWriter.h"
#include "util/Exception.h"

#include <stdlib.h>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <mutex>
#include <memory>
#include <stdio.h>
#include <string>
#include <thread>
#include <vector>

namespace {

int g_passed = 0;
int g_failed = 0;

void check(bool ok, const char *what)
{
  if (ok) {
    g_passed++;
  } else {
    g_failed++;
    printf("FAIL: %s\n", what);
  }
}

//----------------------------------------------------------------------------
// In-memory stream: records bytes, can be blocked and can fail.

class TestStream : public OutputStream
{
public:
  TestStream() : m_blocked(false), m_failAfterWrites(-1), m_writes(0) {}

  virtual size_t write(const void *buffer, size_t len)
  {
    std::unique_lock<std::mutex> lock(m_mutex);
    m_cv.wait(lock, [this] { return !m_blocked || m_failNow(); });
    if (m_failNow()) {
      throw Exception(_T("test stream: write failed"));
    }
    m_data.append(static_cast<const char *>(buffer), len);
    m_writes++;
    return len;
  }

  void setBlocked(bool blocked)
  {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_blocked = blocked;
    m_cv.notify_all();
  }

  // Fails every write once 'n' more write() calls have succeeded (-1 = never).
  void failAfter(int n)
  {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_failAfterWrites = n < 0 ? -1 : m_writes + n;
    m_cv.notify_all();
  }

  std::string data()
  {
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_data;
  }

  // Polls until at least 'bytes' were recorded or the timeout expires.
  bool waitForBytes(size_t bytes, int timeoutMs)
  {
    for (int i = 0; i < timeoutMs; i++) {
      if (data().size() >= bytes) {
        return true;
      }
      std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    return false;
  }

private:
  bool m_failNow() const { return m_failAfterWrites >= 0 && m_writes >= m_failAfterWrites; }

  std::mutex m_mutex;
  std::condition_variable m_cv;
  std::string m_data;
  bool m_blocked;
  int m_failAfterWrites;
  int m_writes;
};

//----------------------------------------------------------------------------
// Parsing of the recorded client messages

struct Msg
{
  enum Kind { KEY, POINTER, CUT, BAD } kind;
  bool down;
  unsigned key;
  unsigned mask;
  unsigned x, y;
  std::string text;

  Msg() : kind(BAD), down(false), key(0), mask(0), x(0), y(0) {}

  bool operator==(const Msg &o) const
  {
    return kind == o.kind && down == o.down && key == o.key && mask == o.mask &&
           x == o.x && y == o.y && text == o.text;
  }
};

unsigned be16(const std::string &s, size_t p)
{
  return (static_cast<unsigned char>(s[p]) << 8) | static_cast<unsigned char>(s[p + 1]);
}

unsigned be32(const std::string &s, size_t p)
{
  return (be16(s, p) << 16) | be16(s, p + 2);
}

std::vector<Msg> parse(const std::string &s)
{
  std::vector<Msg> out;
  size_t p = 0;
  while (p < s.size()) {
    Msg m;
    unsigned char type = static_cast<unsigned char>(s[p]);
    if (type == 4 && p + 8 <= s.size()) {
      m.kind = Msg::KEY;
      m.down = s[p + 1] != 0;
      m.key = be32(s, p + 4);
      p += 8;
    } else if (type == 5 && p + 6 <= s.size()) {
      m.kind = Msg::POINTER;
      m.mask = static_cast<unsigned char>(s[p + 1]);
      m.x = be16(s, p + 2);
      m.y = be16(s, p + 4);
      p += 6;
    } else if (type == 6 && p + 8 <= s.size()) {
      unsigned len = be32(s, p + 4);
      if (p + 8 + len > s.size()) {
        m.kind = Msg::BAD;
        out.push_back(m);
        return out;
      }
      m.kind = Msg::CUT;
      m.text = s.substr(p + 8, len);
      p += 8 + len;
    } else {
      m.kind = Msg::BAD;
      out.push_back(m);
      return out;
    }
    out.push_back(m);
  }
  return out;
}

Msg key(bool down, unsigned k)
{
  Msg m;
  m.kind = Msg::KEY;
  m.down = down;
  m.key = k;
  return m;
}

Msg ptr(unsigned mask, unsigned x, unsigned y)
{
  Msg m;
  m.kind = Msg::POINTER;
  m.mask = mask;
  m.x = x;
  m.y = y;
  return m;
}

Msg cut(const char *text)
{
  Msg m;
  m.kind = Msg::CUT;
  m.text = text;
  return m;
}

//----------------------------------------------------------------------------

class CountingHandler : public InputEventSender::FailureHandler
{
public:
  CountingHandler() : calls(0) {}
  virtual void onInputSendFailed(const TCHAR *) { calls++; }
  std::atomic<int> calls;
};

// Everything one scenario needs.
struct Fixture
{
  Fixture() : log(0), gate(&stream), sender(&log)
  {
    sender.setFailureHandler(&handler);
  }

  LogWriter log;
  TestStream stream;
  RfbOutputGate gate;
  CountingHandler handler;
  InputEventSender sender;
};

void postPointer(InputEventSender &s, UINT8 mask, int x, int y)
{
  Point p(x, y);
  s.postPointerEvent(mask, &p);
}

void postCut(InputEventSender &s, const TCHAR *text, bool utf8)
{
  StringStorage t(text);
  s.postCutText(&t, utf8);
}

// Runs fn on a helper thread and reports whether it finished within timeoutMs.
// A timeout ends this test process: no helper may outlive its captured state.
template <class F>
bool finishesWithin(F fn, int timeoutMs)
{
  std::atomic<bool> done(false);
  std::thread t([&] { fn(); done = true; });
  for (int i = 0; i < timeoutMs && !done; i++) {
    std::this_thread::sleep_for(std::chrono::milliseconds(1));
  }
  if (done) {
    t.join();
    return true;
  }
  fprintf(stderr, "FAIL: test watchdog expired after %d ms\n", timeoutMs);
  fflush(stderr);
  TerminateProcess(GetCurrentProcess(), 2);
  abort();
}

//----------------------------------------------------------------------------
// Scenarios

void testFifoMixed()
{
  Fixture f;
  std::vector<Msg> expected;
  // Alternating masks so nothing can be merged: strict order must be kept.
  for (int i = 0; i < 2000; i++) {
    unsigned k = 0x61 + (i % 26);
    f.sender.postKeyEvent(true, k);
    expected.push_back(key(true, k));
    postPointer(f.sender, 1, i % 1000, i % 700);
    expected.push_back(ptr(1, i % 1000, i % 700));
    postPointer(f.sender, 0, i % 900, i % 600);
    expected.push_back(ptr(0, i % 900, i % 600));
    f.sender.postKeyEvent(false, k);
    expected.push_back(key(false, k));
    if (i % 100 == 0) {
      postCut(f.sender, _T("clip"), false);
      expected.push_back(cut("clip"));
    }
  }
  f.sender.setOutput(&f.gate);
  f.sender.terminate();
  f.sender.wait();
  std::vector<Msg> got = parse(f.stream.data());
  check(got == expected, "FIFO: key, pointer and cut text events keep their exact order");
  check(f.handler.calls == 0, "FIFO: no failure reported");
}

void testPressReleasePairing()
{
  Fixture f;
  f.sender.setOutput(&f.gate);
  // Shift down, a down, a up, shift up, in that order.
  f.sender.postKeyEvent(true, 0xFFE1);
  f.sender.postKeyEvent(true, 0x41);
  f.sender.postKeyEvent(false, 0x41);
  f.sender.postKeyEvent(false, 0xFFE1);
  f.sender.terminate();
  f.sender.wait();
  std::vector<Msg> expected;
  expected.push_back(key(true, 0xFFE1));
  expected.push_back(key(true, 0x41));
  expected.push_back(key(false, 0x41));
  expected.push_back(key(false, 0xFFE1));
  check(parse(f.stream.data()) == expected, "pairing: press/release sequence delivered intact");
}

void testPointerMerge()
{
  Fixture f;
  // Posted before the thread starts, so the merge outcome is deterministic.
  postPointer(f.sender, 0, 1, 1);
  postPointer(f.sender, 0, 2, 2);   // merges with the previous one
  f.sender.postKeyEvent(true, 0x41);
  postPointer(f.sender, 0, 3, 3);   // a key lies between: no merge
  postPointer(f.sender, 1, 4, 4);   // different mask: no merge
  postPointer(f.sender, 1, 45, 45); // first drag move: keep the down position
  postPointer(f.sender, 1, 5, 5);   // merges with the preceding move only
  postPointer(f.sender, 0, 10, 10); // release: retain this position
  postPointer(f.sender, 0, 11, 11); // first move after release
  postPointer(f.sender, 0, 12, 12); // merge moves only
  postPointer(f.sender, 8, 6, 6);   // wheel tick
  postPointer(f.sender, 8, 7, 7);   // wheel tick: never merged
  postPointer(f.sender, 0x20, 8, 8);  // horizontal wheel
  postPointer(f.sender, 0x20, 9, 9);  // never merged
  f.sender.setOutput(&f.gate);
  f.sender.terminate();
  f.sender.wait();
  std::vector<Msg> expected;
  expected.push_back(ptr(0, 2, 2));
  expected.push_back(key(true, 0x41));
  expected.push_back(ptr(0, 3, 3));
  expected.push_back(ptr(1, 4, 4));
  expected.push_back(ptr(1, 5, 5));
  expected.push_back(ptr(0, 10, 10));
  expected.push_back(ptr(0, 12, 12));
  expected.push_back(ptr(8, 6, 6));
  expected.push_back(ptr(8, 7, 7));
  expected.push_back(ptr(0x20, 8, 8));
  expected.push_back(ptr(0x20, 9, 9));
  check(parse(f.stream.data()) == expected, "merge: only identical-mask moves with no key between are merged");
}

void testPostsDoNotBlockOnStalledSocket()
{
  Fixture f;
  f.stream.setBlocked(true);
  f.sender.setOutput(&f.gate);
  const int count = 20000;
  bool finished = finishesWithin([&] {
    for (int i = 0; i < count; i++) {
      f.sender.postKeyEvent((i & 1) == 0, 0x100 + (i >> 1));
      postPointer(f.sender, (i & 1) ? 1 : 0, i & 0xFFF, 5);
    }
  }, 5000);
  check(finished, "stalled socket: posting 40000 events does not block the caller");
  f.stream.setBlocked(false);
  f.sender.terminate();
  bool joined = finishesWithin([&] { f.sender.wait(); }, 20000);
  check(joined, "stalled socket: sender ends after the socket is released");
  std::vector<Msg> got = parse(f.stream.data());
  // Pointer events may be merged, key events never: they must be all present, in order.
  std::vector<Msg> keys;
  bool wellFormed = true;
  for (size_t i = 0; i < got.size(); i++) {
    if (got[i].kind == Msg::KEY) {
      keys.push_back(got[i]);
    }
    wellFormed = wellFormed && got[i].kind != Msg::BAD;
  }
  check(wellFormed, "stalled socket: stream is well formed");
  bool ordered = keys.size() == static_cast<size_t>(count);
  for (size_t i = 0; ordered && i < keys.size(); i++) {
    ordered = keys[i].down == ((i & 1) == 0) && keys[i].key == 0x100 + (i >> 1);
  }
  check(ordered, "stalled socket: all key events delivered, in order");
}

void testReleaseNotLostOnTerminate()
{
  Fixture f;
  f.stream.setBlocked(true);
  f.sender.setOutput(&f.gate);
  f.sender.postKeyEvent(true, 0x41);
  f.sender.postKeyEvent(true, 0xFFE3);
  f.sender.postKeyEvent(false, 0xFFE3);
  f.sender.postKeyEvent(false, 0x41);
  f.sender.terminate();                       // orderly disconnect requested
  f.sender.postKeyEvent(true, 0x7A);          // too late: must be ignored
  f.stream.setBlocked(false);
  bool joined = finishesWithin([&] { f.sender.wait(); }, 5000);
  check(joined, "terminate: sender ends");
  std::vector<Msg> expected;
  expected.push_back(key(true, 0x41));
  expected.push_back(key(true, 0xFFE3));
  expected.push_back(key(false, 0xFFE3));
  expected.push_back(key(false, 0x41));
  check(parse(f.stream.data()) == expected, "terminate: queued releases are flushed, later events ignored");
  check(f.handler.calls == 0, "terminate: orderly flush is not a failure");
}

void testWriteFailure()
{
  Fixture f;
  f.stream.failAfter(3);     // each message is one write() of the gate buffer: the 4th fails
  f.sender.setOutput(&f.gate);
  for (int i = 0; i < 50; i++) {
    f.sender.postKeyEvent(true, 0x41 + i);
  }
  bool joined = finishesWithin([&] { f.sender.wait(); }, 5000);
  check(joined, "failure: sender thread ends by itself");
  check(f.handler.calls == 1, "failure: handler called exactly once");
  check(f.stream.data().size() == 3 * 8, "failure: exactly the 3 messages before the failure were written");
  size_t before = f.stream.data().size();
  f.sender.postKeyEvent(true, 0x7A);
  f.sender.postKeyEvent(false, 0x7A);
  std::this_thread::sleep_for(std::chrono::milliseconds(50));
  check(f.stream.data().size() == before, "failure: events after the failure are ignored");
  f.sender.terminate();
  check(finishesWithin([&] { f.sender.wait(); }, 2000), "failure: terminate/wait afterwards does not hang");
  check(f.handler.calls == 1, "failure: still a single report");
}

void testFailureDuringTerminate()
{
  Fixture f;
  f.stream.setBlocked(true);
  f.sender.setOutput(&f.gate);
  f.sender.postKeyEvent(true, 0x41);
  f.sender.postKeyEvent(false, 0x41);
  // Let the thread enter the blocked write, then request termination and
  // then break the "connection": the write error must be swallowed.
  std::this_thread::sleep_for(std::chrono::milliseconds(50));
  f.sender.terminate();
  f.stream.failAfter(0);
  bool joined = finishesWithin([&] { f.sender.wait(); }, 5000);
  check(joined, "terminate+close: sender ends without hanging");
  check(f.handler.calls == 0, "terminate+close: aborted write is not reported as failure");
}

void testBoundedDrain()
{
  Fixture notStarted;
  notStarted.sender.terminate();
  check(notStarted.sender.waitForDrain(0), "drain: never-started sender is complete");

  Fixture empty;
  empty.sender.setOutput(&empty.gate);
  empty.sender.terminate();
  check(empty.sender.waitForDrain(5000), "drain: empty sender completes");
  check(empty.sender.waitForDrain(0), "drain: completion remains signaled");
  empty.sender.wait();

  Fixture blocked;
  blocked.stream.setBlocked(true);
  blocked.sender.postKeyEvent(true, 0x41);
  blocked.sender.postKeyEvent(false, 0x41);
  blocked.sender.setOutput(&blocked.gate);
  blocked.sender.terminate();
  check(!blocked.sender.waitForDrain(20), "drain: blocked socket times out");
  blocked.stream.setBlocked(false);
  check(blocked.sender.waitForDrain(5000), "drain: sender completes after socket unblocks");
  blocked.sender.wait();
  std::vector<Msg> expected;
  expected.push_back(key(true, 0x41));
  expected.push_back(key(false, 0x41));
  check(parse(blocked.stream.data()) == expected, "drain: timeout does not discard queued releases");
}

// Real TCP regression: closing either kind of TcpConnection (outbound or an
// accepted reverse connection) must end a blocked protocol read once the peer
// answers the FIN.
void checkConnectionClose(TcpConnection &connection, SocketIPv4 &peer,
                          const char *scenario)
{
  const char marker = 'q';
  if (peer.send(&marker, 1) != 1) {
    fprintf(stderr, "FAIL: loopback marker could not be sent (%s)\n", scenario);
    fflush(stderr);
    TerminateProcess(GetCurrentProcess(), 2);
    abort();
  }
  std::atomic<bool> entered(false);
  std::atomic<bool> finished(false);
  std::atomic<bool> interrupted(false);
  std::atomic<bool> markerRead(false);
  std::thread reader([&] {
    try {
      markerRead = connection.getInput()->readUInt8() == marker;
      entered = true;
      connection.getInput()->readUInt8();
    } catch (const Exception &) {
      interrupted = true;
      entered = true;
    }
    finished = true;
  });
  finishesWithin([&] {
    while (!entered) {
      std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
  }, 5000);
  std::this_thread::sleep_for(std::chrono::milliseconds(20));
  check(markerRead && !finished, "TCP close: reader blocks while peer stays connected");
  connection.close();
  // close() is a Winsock shutdown(SD_BOTH): it sends a FIN, but a recv that is
  // already pending only completes once the peer answers it (observed on
  // Windows 11; the real server closes its end when it sees the FIN). Play
  // that part of the peer, then the blocked read must end.
  bool peerSawFin = false;
  finishesWithin([&] {
    try {
      char c;
      peer.recv(&c, 1);
    } catch (const Exception &) {
      peerSawFin = true;
    }
  }, 5000);
  check(peerSawFin, "TCP close: peer observes the end of stream");
  try {
    peer.shutdown(SD_BOTH);
  } catch (const Exception &) {
  }
  finishesWithin([&] { reader.join(); }, 5000);
  if (!interrupted) {
    fprintf(stderr, "FAIL: blocked read did not report EOF/error (%s)\n", scenario);
  }
  check(interrupted, scenario);
}

void testTcpConnectionClose()
{
  // SocketIPv4 owns WsaStartup. Never listen on wildcard addresses or port 5900.
  SocketIPv4 listener;
  try {
    listener.setExclusiveAddrUse();
    listener.bind(_T("127.0.0.1"), 5923);
    listener.listen(2);
  } catch (const Exception &) {
    fprintf(stderr, "FAIL: cannot exclusively bind/listen on 127.0.0.1:5923; aborting\n");
    fflush(stderr);
    TerminateProcess(GetCurrentProcess(), 2);
    abort();
  }

  LogWriter log(0);
  try {
    {
      SocketIPv4 peer;
      peer.connect(_T("127.0.0.1"), 5923);
      std::unique_ptr<SocketIPv4> accepted(listener.accept());
      {
        TcpConnection connection(&log);
        connection.bind(accepted.get());
        connection.connect();
        checkConnectionClose(connection, peer,
                             "TCP close: accepted reverse connection interrupts blocked read");
      } // TcpConnection does not own accepted; destroy its gates first.
    }
    {
      TcpConnection connection(&log);
      connection.bind(_T("127.0.0.1"), 5923);
      connection.connect();
      std::unique_ptr<SocketIPv4> peer(listener.accept());
      checkConnectionClose(connection, *peer,
                           "TCP close: outbound connection interrupts blocked read");
    }
  } catch (const Exception &) {
    fprintf(stderr, "FAIL: TCP close regression setup failed on 127.0.0.1:5923\n");
    fflush(stderr);
    TerminateProcess(GetCurrentProcess(), 2);
    abort();
  }
}

void testDestructorWithoutStart()
{
  bool ok = finishesWithin([] {
    LogWriter log(0);
    InputEventSender sender(&log);
    sender.postKeyEvent(true, 0x41);
  }, 2000);
  check(ok, "never started: destructor does not hang");
}

void testDestructorWhileRunning()
{
  bool ok = finishesWithin([] {
    LogWriter log(0);
    TestStream stream;
    RfbOutputGate gate(&stream);
    InputEventSender sender(&log);
    sender.setOutput(&gate);
    for (int i = 0; i < 1000; i++) {
      sender.postKeyEvent(true, 0x41);
    }
  }, 5000);
  check(ok, "destructor while running: no hang");
}

void testConcurrentProducers()
{
  Fixture f;
  f.sender.setOutput(&f.gate);
  const int producers = 4;
  const int perProducer = 5000;
  std::vector<std::thread> threads;
  for (int p = 0; p < producers; p++) {
    threads.push_back(std::thread([&, p] {
      for (int i = 0; i < perProducer; i++) {
        f.sender.postKeyEvent(true, static_cast<UINT32>(p * 100000 + i));
      }
    }));
  }
  for (size_t i = 0; i < threads.size(); i++) {
    threads[i].join();
  }
  f.sender.terminate();
  f.sender.wait();
  std::vector<Msg> got = parse(f.stream.data());
  check(got.size() == static_cast<size_t>(producers * perProducer), "producers: no event lost");
  std::vector<int> next(producers, 0);
  bool ordered = true;
  for (size_t i = 0; i < got.size(); i++) {
    int p = got[i].key / 100000;
    int n = got[i].key % 100000;
    if (p >= producers || n != next[p]++) {
      ordered = false;
    }
  }
  check(ordered, "producers: per-producer order preserved");
}

void testCutTextUtf8()
{
  Fixture f;
  f.sender.setOutput(&f.gate);
  postCut(f.sender, _T("abc"), false);
  f.sender.postKeyEvent(true, 0x76);
  postCut(f.sender, _T("abc"), true);
  f.sender.terminate();
  f.sender.wait();
  std::string d = f.stream.data();
  // ANSI cut text: type 6, 3 padding bytes, length, text. Then a key event.
  // UTF-8 cut text (TightVNC extension): a 32-bit message id, length, text.
  check(d.size() >= 11 + 8 + 4 + 4 + 3, "cut text: both messages written");
  check(d.compare(0, 11, std::string("\x06\0\0\0\0\0\0\x03" "abc", 11)) == 0, "cut text: ANSI message first");
  check(d.compare(11, 8, std::string("\x04\x01\0\0\0\0\0\x76", 8)) == 0, "cut text: key event keeps its place");
  check(d.compare(d.size() - 3, 3, "abc") == 0, "cut text: UTF-8 message last");
}

} // namespace

int main()
{
  setvbuf(stdout, NULL, _IONBF, 0); // keep results visible if a watchdog ends the process
  // The name is printed before each scenario so a watchdog exit shows which
  // one was running.
#define RUN_TEST(t) (printf("RUN " #t "\n"), t())
  RUN_TEST(testFifoMixed);
  RUN_TEST(testPressReleasePairing);
  RUN_TEST(testPointerMerge);
  RUN_TEST(testPostsDoNotBlockOnStalledSocket);
  RUN_TEST(testReleaseNotLostOnTerminate);
  RUN_TEST(testWriteFailure);
  RUN_TEST(testFailureDuringTerminate);
  RUN_TEST(testBoundedDrain);
  RUN_TEST(testTcpConnectionClose);
  RUN_TEST(testDestructorWithoutStart);
  RUN_TEST(testDestructorWhileRunning);
  RUN_TEST(testConcurrentProducers);
  RUN_TEST(testCutTextUtf8);
#undef RUN_TEST
  printf("viewer-input-queue-test: %d passed, %d failed\n", g_passed, g_failed);
  return g_failed == 0 ? 0 : 1;
}
