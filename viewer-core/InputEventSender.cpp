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

#include "InputEventSender.h"

#include "thread/AutoLock.h"
#include "util/Exception.h"

#include "RfbCutTextEventClientMessage.h"
#include "RfbKeyEventClientMessage.h"
#include "RfbPointerEventClientMessage.h"

// Wheel and horizontal-wheel "buttons" (bits 3..6). A mask with any of these
// set is a discrete scroll tick and must never be merged.
static const UINT8 WHEEL_BITS = 0x78;

InputEventSender::InputEventSender(LogWriter *logWriter)
: m_logWriter(logWriter),
  m_failureHandler(0),
  m_output(0),
  m_closed(false)
{
}

InputEventSender::~InputEventSender()
{
  try {
    terminate();
    wait();
  } catch (...) {
  }
}

void InputEventSender::setOutput(RfbOutputGate *output)
{
  m_output = output;
  resume();
  m_logWriter->debug(_T("InputEventSender is started"));
}

void InputEventSender::postKeyEvent(bool downFlag, UINT32 key)
{
  Event event;
  event.type = KEY;
  event.downFlag = downFlag;
  event.key = key;
  push(event);
}

void InputEventSender::postPointerEvent(UINT8 buttonMask, const Point *position)
{
  Event event;
  event.type = POINTER;
  event.buttonMask = buttonMask;
  event.position = *position;
  push(event);
}

void InputEventSender::postCutText(const StringStorage *cutText, bool utf8)
{
  Event event;
  event.type = CUT_TEXT;
  event.text = *cutText;
  event.utf8 = utf8;
  push(event);
}

void InputEventSender::push(const Event &event)
{
  {
    AutoLock al(&m_queueLock);
    if (m_closed) {
      return;
    }
    // Merge a pointer move into the queued one only if that one is the very
    // last queued event (so the order relative to keys is untouched) and the
    // button mask is identical and has no wheel bits.
    if (event.type == POINTER && !m_queue.empty()) {
      Event &last = m_queue.back();
      if (last.type == POINTER && last.buttonMask == event.buttonMask &&
          (event.buttonMask & WHEEL_BITS) == 0) {
        last.position = event.position;
        return;
      }
    }
    m_queue.push_back(event);
  }
  m_eventQueued.notify();
}

bool InputEventSender::pop(Event *event)
{
  AutoLock al(&m_queueLock);
  if (m_queue.empty()) {
    return false;
  }
  *event = m_queue.front();
  m_queue.pop_front();
  return true;
}

void InputEventSender::close(bool dropQueued)
{
  AutoLock al(&m_queueLock);
  m_closed = true;
  if (dropQueued) {
    m_queue.clear();
  }
}

void InputEventSender::send(const Event &event, RfbOutputGate *output)
{
  // Each send() locks the output gate for the whole message.
  switch (event.type) {
  case KEY:
    {
      RfbKeyEventClientMessage message(event.downFlag, event.key);
      message.send(output);
    }
    break;
  case POINTER:
    {
      RfbPointerEventClientMessage message(event.buttonMask, &event.position);
      message.send(output);
    }
    break;
  case CUT_TEXT:
    {
      RfbCutTextEventClientMessage message(&event.text);
      if (event.utf8) {
        message.sendUtf8(output);
      } else {
        message.send(output);
      }
    }
    break;
  }
}

void InputEventSender::execute()
{
  StringStorage failure;
  try {
    // The loop ends only when termination was requested AND the queue is
    // empty. terminate() closes the queue first, so the remaining events (for
    // instance key releases) are still written on an orderly shutdown.
    for (;;) {
      Event event;
      if (pop(&event)) {
        send(event, m_output);
      } else if (isTerminating()) {
        break;
      } else {
        m_eventQueued.waitForEvent();
      }
    }
  } catch (const Exception &ex) {
    failure = ex.getMessage();
  } catch (...) {
    failure = _T("Unknown error");
  }

  // After a failed write the remaining events are dropped.
  close(!failure.isEmpty());

  if (!failure.isEmpty() && !isTerminating()) {
    m_logWriter->error(_T("InputEventSender: failed to send input: %s"),
                       failure.getString());
    if (m_failureHandler != 0) {
      try {
        m_failureHandler->onInputSendFailed(failure.getString());
      } catch (...) {
      }
    }
  }
}

void InputEventSender::onTerminate()
{
  // Stop accepting new events, keep the queued ones for the final flush.
  close(false);
  m_eventQueued.notify();
}