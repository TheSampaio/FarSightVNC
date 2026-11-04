// Copyright (C) 2026 FarSightVNC contributors. GPL-2.0-or-later.
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

#ifndef _RECONNECT_POLICY_H_
#define _RECONNECT_POLICY_H_

// Assisted reconnect decisions, without any window so that they can be
// tested. An established session that drops unexpectedly offers to reconnect;
// while reconnecting, a failed attempt offers the next one. Attempts made
// automatically wait an exponential backoff (base, 2 x base, 4 x base, ...
// capped) and stop after a maximum number of attempts; after that only a
// manual reconnect is offered. A session closed by the user, an
// authentication failure and an incoming (listening mode) connection never
// offer to reconnect.
class ReconnectPolicy
{
public:
  enum Cause
  {
    // The user closed or disconnected the session.
    CAUSE_USER_CLOSED,
    // The server rejected the credentials or the user cancelled them.
    CAUSE_AUTH_FAILED,
    // An established session dropped.
    CAUSE_CONNECTION_LOST,
    // A connection attempt failed before the session was established.
    CAUSE_CONNECT_FAILED
  };

  static const unsigned int DEFAULT_MAX_ATTEMPTS = 4;
  static const unsigned int DEFAULT_BASE_DELAY_MS = 2000;
  static const unsigned int DEFAULT_MAX_DELAY_MS = 16000;

  explicit ReconnectPolicy(unsigned int maxAttempts = DEFAULT_MAX_ATTEMPTS,
                           unsigned int baseDelayMs = DEFAULT_BASE_DELAY_MS,
                           unsigned int maxDelayMs = DEFAULT_MAX_DELAY_MS)
  : m_maxAttempts(maxAttempts),
    m_baseDelayMs(baseDelayMs),
    m_maxDelayMs(maxDelayMs < baseDelayMs ? baseDelayMs : maxDelayMs)
  {
  }

  unsigned int getMaxAttempts() const { return m_maxAttempts; }

  // attemptsMade is the number of reconnection attempts already made since
  // the session dropped; it is 0 when an established session has just
  // dropped. A failed first connection (attemptsMade == 0) keeps the usual
  // error message.
  bool shouldOffer(Cause cause, bool isIncoming, unsigned int attemptsMade) const
  {
    if (isIncoming) {
      return false;
    }
    switch (cause) {
    case CAUSE_CONNECTION_LOST:
      return true;
    case CAUSE_CONNECT_FAILED:
      return attemptsMade > 0;
    default:
      return false;
    }
  }

  // Attempts made automatically are counted from the drop of the session.
  bool canRetryAutomatically(unsigned int attemptsMade) const
  {
    return attemptsMade < m_maxAttempts;
  }

  // Delay before the attempt number attemptsMade + 1:
  // base * 2^attemptsMade, capped at the maximum delay.
  unsigned int delayBeforeAttemptMs(unsigned int attemptsMade) const
  {
    unsigned int delay = m_baseDelayMs;
    for (unsigned int i = 0; i < attemptsMade && delay < m_maxDelayMs; i++) {
      delay = delay > m_maxDelayMs / 2 ? m_maxDelayMs : delay * 2;
    }
    return delay < m_maxDelayMs ? delay : m_maxDelayMs;
  }

  // Whole seconds left of a countdown, rounded up (0 once it expired).
  // Elapsed times come from a wrapping 32-bit tick counter difference.
  static unsigned int secondsLeft(unsigned int delayMs, unsigned int elapsedMs)
  {
    if (elapsedMs >= delayMs) {
      return 0;
    }
    return (delayMs - elapsedMs + 999) / 1000;
  }

private:
  unsigned int m_maxAttempts;
  unsigned int m_baseDelayMs;
  unsigned int m_maxDelayMs;
};

#endif
