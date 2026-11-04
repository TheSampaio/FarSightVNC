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

#ifndef _CONNECTION_FAVORITES_H_
#define _CONNECTION_FAVORITES_H_

#include "util/StringStorage.h"

#include <vector>

/**
 * Favorite hosts of the viewer and the host list shown in the login dialog.
 *
 * Favorites are kept in insertion order in their own registry key
 * (HKCU\Software\FarSightVNC\Viewer\Favorites, values "0".."N-1"), so the
 * TightVNC history key shared with TightVNC Viewer is not changed. Host names
 * are compared case-insensitively, like registry key names.
 */
class ConnectionFavorites
{
public:
  static const size_t DEFAULT_LIMIT = 64;
  static const TCHAR REGISTRY_PATH[];

  ConnectionFavorites(size_t limit = DEFAULT_LIMIT);

  // Case-insensitive host comparison; null and empty hosts never match.
  static bool isSameHost(const TCHAR *a, const TCHAR *b);

  bool contains(const TCHAR *host) const;
  // Returns false if the host is empty, already a favorite or the limit is reached.
  bool add(const TCHAR *host);
  // Returns false if the host is not a favorite.
  bool remove(const TCHAR *host);
  size_t getCount() const;
  const TCHAR *getHost(size_t i) const;
  size_t getLimit() const;

  /**
   * Builds the host list shown to the user: favorites first, then recents
   * (in the given most-recent-first order) without favorites and without
   * case-insensitive duplicates, at most recentLimit of them.
   * @return number of favorites at the head of the list.
   */
  size_t buildHostList(const std::vector<StringStorage> &recents,
                       size_t recentLimit,
                       std::vector<StringStorage> *list) const;

  // Loads favorites from the registry (does not create the key).
  void load();
  // Saves favorites to the registry (creates the key if needed).
  void save() const;

private:
  std::vector<StringStorage> m_hosts;
  size_t m_limit;
};

#endif
