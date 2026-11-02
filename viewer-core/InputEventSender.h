// Copyright (C) 2026 FarSightVNC contributors.
//
// This file is part of FarSightVNC, a modified version of the TightVNC
// software. It is free software; you can redistribute it and/or modify it
// under the terms of the GNU General Public License as published by the Free
// Software Foundation; either version 2 of the License, or (at your option)
// any later version. It is distributed WITHOUT ANY WARRANTY; see the GNU
// General Public License for more details.
//
// Added for FarSightVNC on 2026-11-01: sends client input messages (keyboard,
// pointer, cut text) from a dedicated thread, so the UI thread never blocks on
// the socket.
//

// Modified for FarSightVNC on 2026-11-02: preserve pointer transitions and
// provide a bounded wait for the sender to drain during shutdown.

#ifndef _INPUT_EVENT_SENDER_H_
#define _INPUT_EVENT_SENDER_H_

#include <deque>

#include "log-writer/LogWriter.h"
#include "network/RfbOutputGate.h"
#include "region/Point.h"
#include "thread/LocalMutex.h"
#include "thread/Thread.h"
#include "util/StringStorage.h"
#include "win-system/WindowsEvent.h"

// Ordered queue of client->server input messages and the thread that writes
// them to the socket.
//
// Guarantees:
//  - Strict FIFO order across key, pointer and cut text events.
//  - Only consecutive pointer events with an identical button mask that has no
//    wheel bits may be merged (the newer position replaces the older one).
//    Button changes, wheel ticks and key events are never merged or dropped.
//  - Every message is written while holding the RfbOutputGate lock (taken by
//    the message's send()), so writes stay serialized with all other
//    client->server messages sent by other threads.
//
// Lifecycle: setOutput() starts the thread. terminate() stops accepting new
// events; the thread first writes the events already queued (so a key release
// is not lost on an orderly disconnect) and then ends. If the connection is
// closed meanwhile the flush fails silently. A failed write while not
// terminating drops the pending events, ignores later ones and calls the
// failure handler (once) from the sender thread.
class InputEventSender : public Thread
{
public:
  // Called once from the sender thread after a write failed.
  class FailureHandler
  {
  public:
    virtual ~FailureHandler() {}
    virtual void onInputSendFailed(const TCHAR *message) = 0;
  };

  explicit InputEventSender(LogWriter *logWriter);
  virtual ~InputEventSender();

  // Must be set before setOutput().
  void setFailureHandler(FailureHandler *failureHandler) { m_failureHandler = failureHandler; }

  // Sets the output gate and starts the thread. Call once.
  void setOutput(RfbOutputGate *output);

  // Call after terminate(). Waits for sender completion, including failed flushes.
  // A never-started sender is already complete. Repeated waits are supported.
  bool waitForDrain(DWORD timeout);

  void postKeyEvent(bool downFlag, UINT32 key);
  void postPointerEvent(UINT8 buttonMask, const Point *position);
  void postCutText(const StringStorage *cutText, bool utf8);

protected:
  virtual void execute();
  virtual void onTerminate();

private:
  enum EventType { KEY, POINTER, CUT_TEXT };

  struct Event
  {
    Event() : type(KEY), downFlag(false), key(0), buttonMask(0), pointerMove(false), utf8(false) {}

    EventType type;
    bool downFlag;      // KEY
    UINT32 key;         // KEY
    UINT8 buttonMask;   // POINTER
    bool pointerMove;  // POINTER: no button transition or wheel tick
    Point position;     // POINTER
    StringStorage text; // CUT_TEXT
    bool utf8;          // CUT_TEXT
  };

  void push(const Event &event);
  bool pop(Event *event);
  void send(const Event &event, RfbOutputGate *output);
  void close(bool dropQueued);

  LogWriter *m_logWriter;
  FailureHandler *m_failureHandler;

  RfbOutputGate *m_output;

  // Protects queue state and m_started. Never held while writing to the socket.
  LocalMutex m_queueLock;
  std::deque<Event> m_queue;
  bool m_closed;
  UINT8 m_lastButtonMask;
  bool m_started;
  WindowsEvent m_eventQueued;
  HANDLE m_finished; // manual-reset: completion remains observable after a wait
};

#endif
