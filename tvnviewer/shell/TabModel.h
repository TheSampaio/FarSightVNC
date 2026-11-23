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

#ifndef _TAB_MODEL_H_
#define _TAB_MODEL_H_

#include <cstddef>
#include <memory>
#include <utility>
#include <vector>

namespace fsv::shell {

// Tabs of a shell window, in the order of the title bar, and the active one.
// It owns the tabs and knows nothing about windows, so that it can be tested
// with any type of tab. At most one tab is active; there is none only while
// the model is empty.
template <class Tab>
class TabModel
{
public:
  static constexpr size_t npos = static_cast<size_t>(-1);

  TabModel() : m_active(0) {}

  size_t size() const { return m_tabs.size(); }
  bool empty() const { return m_tabs.empty(); }
  // Tab at index, or 0 when there is none.
  Tab *at(size_t index) const { return index < m_tabs.size() ? m_tabs[index].get() : 0; }
  // Active tab, or 0 when the model is empty.
  Tab *active() const { return at(m_active); }
  // Index of the active tab, or npos when the model is empty.
  size_t activeIndex() const { return m_tabs.empty() ? npos : m_active; }

  // Inserts tab right after the active tab (at the start when the model is
  // empty) and activates it. Returns its index.
  size_t add(std::unique_ptr<Tab> tab)
  {
    size_t index = m_tabs.empty() ? 0 : m_active + 1;
    m_tabs.insert(m_tabs.begin() + index, std::move(tab));
    m_active = index;
    return index;
  }

  // Takes the tab at index out of the model; null when there is no such tab.
  // The active tab stays active unless it is the removed one: then the
  // neighbour to the right takes its place, else the one to the left.
  std::unique_ptr<Tab> remove(size_t index)
  {
    if (index >= m_tabs.size()) {
      return std::unique_ptr<Tab>();
    }
    std::unique_ptr<Tab> removed = std::move(m_tabs[index]);
    m_tabs.erase(m_tabs.begin() + index);
    if (m_tabs.empty()) {
      m_active = 0;
    } else if (index < m_active) {
      m_active--;
    } else if (index == m_active && m_active >= m_tabs.size()) {
      m_active = m_tabs.size() - 1;
    }
    return removed;
  }

  // Puts tab in the place of the one at index, which is returned (null when
  // there is no such tab; tab is dropped then). The active index is unchanged.
  std::unique_ptr<Tab> replace(size_t index, std::unique_ptr<Tab> tab)
  {
    if (index >= m_tabs.size()) {
      return std::unique_ptr<Tab>();
    }
    std::unique_ptr<Tab> old = std::move(m_tabs[index]);
    m_tabs[index] = std::move(tab);
    return old;
  }

  // Makes the tab at index the active one; false (nothing changes) when
  // there is no such tab.
  bool activate(size_t index)
  {
    if (index >= m_tabs.size()) {
      return false;
    }
    m_active = index;
    return true;
  }

  // Next or previous tab, wrapping around the ends. Nothing changes with one
  // tab or none.
  void activateNext()
  {
    if (m_tabs.size() > 1) {
      m_active = (m_active + 1) % m_tabs.size();
    }
  }
  void activatePrevious()
  {
    if (m_tabs.size() > 1) {
      m_active = (m_active + m_tabs.size() - 1) % m_tabs.size();
    }
  }

  // Index of tab, or npos when it is not in the model.
  size_t indexOf(const Tab *tab) const
  {
    for (size_t i = 0; i < m_tabs.size(); i++) {
      if (m_tabs[i].get() == tab) {
        return i;
      }
    }
    return npos;
  }

  void clear()
  {
    m_tabs.clear();
    m_active = 0;
  }

private:
  std::vector<std::unique_ptr<Tab> > m_tabs;
  size_t m_active;
};

}

#endif
