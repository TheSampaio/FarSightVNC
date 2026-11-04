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

#include "ConnectionFavorites.h"

#include "win-system/Registry.h"
#include "win-system/RegistryKey.h"

const TCHAR ConnectionFavorites::REGISTRY_PATH[] =
  _T("Software\\FarSightVNC\\Viewer\\Favorites");

ConnectionFavorites::ConnectionFavorites(size_t limit)
: m_limit(limit)
{
}

bool ConnectionFavorites::isSameHost(const TCHAR *a, const TCHAR *b)
{
  if (a == 0 || b == 0 || a[0] == _T('\0') || b[0] == _T('\0')) {
    return false;
  }
  return _tcsicmp(a, b) == 0;
}

bool ConnectionFavorites::contains(const TCHAR *host) const
{
  for (size_t i = 0; i < m_hosts.size(); i++) {
    if (isSameHost(m_hosts[i].getString(), host)) {
      return true;
    }
  }
  return false;
}

bool ConnectionFavorites::add(const TCHAR *host)
{
  if (host == 0 || host[0] == _T('\0') || contains(host) ||
      m_hosts.size() >= m_limit) {
    return false;
  }
  m_hosts.push_back(StringStorage(host));
  return true;
}

bool ConnectionFavorites::remove(const TCHAR *host)
{
  for (std::vector<StringStorage>::iterator it = m_hosts.begin();
       it != m_hosts.end(); it++) {
    if (isSameHost(it->getString(), host)) {
      m_hosts.erase(it);
      return true;
    }
  }
  return false;
}

size_t ConnectionFavorites::getCount() const
{
  return m_hosts.size();
}

const TCHAR *ConnectionFavorites::getHost(size_t i) const
{
  return m_hosts.at(i).getString();
}

size_t ConnectionFavorites::getLimit() const
{
  return m_limit;
}

size_t ConnectionFavorites::buildHostList(const std::vector<StringStorage> &recents,
                                          size_t recentLimit,
                                          std::vector<StringStorage> *list) const
{
  list->assign(m_hosts.begin(), m_hosts.end());
  size_t recentCount = 0;
  for (size_t i = 0; i < recents.size() && recentCount < recentLimit; i++) {
    const TCHAR *host = recents[i].getString();
    bool listed = (host == 0 || host[0] == _T('\0'));
    for (size_t j = 0; j < list->size() && !listed; j++) {
      listed = isSameHost(list->at(j).getString(), host);
    }
    if (!listed) {
      list->push_back(recents[i]);
      recentCount++;
    }
  }
  return m_hosts.size();
}

void ConnectionFavorites::load()
{
  m_hosts.clear();
  RegistryKey key(Registry::getCurrentUserKey(), REGISTRY_PATH, false);
  if (!key.isOpened()) {
    return;
  }
  StringStorage valueName;
  StringStorage value;
  for (size_t i = 0; i < m_limit; i++) {
    valueName.format(_T("%u"), (unsigned int)i);
    if (!key.getValueAsString(valueName.getString(), &value)) {
      break;
    }
    add(value.getString());
  }
}

void ConnectionFavorites::save() const
{
  RegistryKey key(Registry::getCurrentUserKey(), REGISTRY_PATH, true);
  if (!key.isOpened()) {
    return;
  }
  StringStorage valueName;
  size_t i = 0;
  for (; i < m_hosts.size(); i++) {
    valueName.format(_T("%u"), (unsigned int)i);
    key.setValueAsString(valueName.getString(), m_hosts[i].getString());
  }
  // Remove the values left over from a longer list.
  StringStorage value;
  for (;; i++) {
    valueName.format(_T("%u"), (unsigned int)i);
    if (!key.getValueAsString(valueName.getString(), &value)) {
      break;
    }
    key.deleteValue(valueName.getString());
  }
}
