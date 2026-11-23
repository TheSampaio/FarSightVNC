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

// Deterministic test for the tabs of a shell window (tvnviewer/shell/
// TabModel.h): insertion after the active tab, removal and the choice of the
// next active tab, replacement, activation and cycling. No window is created.

#include "tvnviewer/shell/TabModel.h"

#include <memory>
#include <string>

namespace {

struct FakeTab
{
  explicit FakeTab(int number) : id(number) {}
  int id;
};

typedef fsv::shell::TabModel<FakeTab> Model;

std::unique_ptr<FakeTab> tab(int id)
{
  return std::unique_ptr<FakeTab>(new FakeTab(id));
}

// Ids of the tabs in order, the active one in brackets: " 1 [2] 3 ".
std::string order(const Model &model)
{
  std::string text;
  for (size_t i = 0; i < model.size(); i++) {
    bool active = i == model.activeIndex();
    text += active ? '[' : ' ';
    text += static_cast<char>('0' + model.at(i)->id);
    text += active ? ']' : ' ';
  }
  return text;
}

}

// Called by main() of FavoritesTest.cpp with its check().
void testTabModel(void (*check)(bool, const char *))
{
  Model model;
  check(model.empty() && model.size() == 0, "new model is empty");
  check(model.active() == 0 && model.activeIndex() == Model::npos, "empty model has no active tab");
  check(!model.activate(0), "cannot activate in an empty model");
  check(model.remove(0) == nullptr, "remove from an empty model returns null");
  model.activateNext();
  check(model.active() == 0, "cycling an empty model does nothing");

  // Add: the first tab, then each new one right after the active tab.
  check(model.add(tab(1)) == 0, "first tab has index 0");
  check(order(model) == "[1]", "first tab is active");
  check(model.add(tab(2)) == 1, "second tab goes after the first");
  check(order(model) == " 1 [2]", "new tab is activated");
  check(model.activate(0), "activate the first tab");
  check(model.add(tab(3)) == 1, "new tab goes right after the active tab, not at the end");
  check(order(model) == " 1 [3] 2 ", "tab inserted in the middle is active");
  check(model.indexOf(model.at(2)) == 2 && model.indexOf(model.at(0)) == 0, "indexOf finds a tab");
  FakeTab stranger(9);
  check(model.indexOf(&stranger) == Model::npos, "indexOf of a foreign tab is npos");

  // Activate.
  check(!model.activate(3), "activate out of range fails");
  check(order(model) == " 1 [3] 2 ", "a failed activation keeps the active tab");
  check(model.activate(2) && model.active()->id == 2, "activate by index");

  // Cycling wraps around.
  model.activateNext();
  check(model.activeIndex() == 0, "next wraps to the first tab");
  model.activatePrevious();
  check(model.activeIndex() == 2, "previous wraps to the last tab");
  model.activatePrevious();
  check(model.activeIndex() == 1, "previous moves left");
  Model single;
  single.add(tab(1));
  single.activateNext();
  single.activatePrevious();
  check(single.activeIndex() == 0, "cycling one tab keeps it");

  // Remove: a tab before the active one keeps the same tab active.
  model.clear();
  for (int i = 1; i <= 4; i++) {
    model.add(tab(i));
  }
  model.activate(2);
  check(order(model) == " 1  2 [3] 4 ", "four tabs, third active");
  std::unique_ptr<FakeTab> removed = model.remove(0);
  check(removed != nullptr && removed->id == 1, "remove returns the tab");
  check(order(model) == " 2 [3] 4 ", "removing a tab before the active one keeps the active tab");
  // A tab after the active one.
  model.remove(2);
  check(order(model) == " 2 [3]", "removing a tab after the active one keeps the active tab");
  // The active tab: the right neighbour, else the left one.
  model.add(tab(5));
  model.activate(1);
  check(order(model) == " 2 [3] 5 ", "tabs 2, 3, 5 with 3 active");
  model.remove(1);
  check(order(model) == " 2 [5]", "removing the active tab activates the right neighbour");
  model.remove(1);
  check(order(model) == "[2]", "removing the active last tab activates the left neighbour");
  model.remove(0);
  check(model.empty() && model.active() == 0, "removing the only tab empties the model");
  check(model.remove(0) == nullptr, "nothing left to remove");

  // Remove out of range changes nothing.
  model.add(tab(1));
  model.add(tab(2));
  check(model.remove(5) == nullptr && model.size() == 2, "remove out of range does nothing");
  check(order(model) == " 1 [2]", "out of range remove keeps the active tab");

  // Replace keeps the position and the active index.
  std::unique_ptr<FakeTab> old = model.replace(0, tab(7));
  check(old != nullptr && old->id == 1, "replace returns the old tab");
  check(order(model) == " 7 [2]", "replace keeps the order and the active tab");
  old = model.replace(1, tab(8));
  check(order(model) == " 7 [8]", "replace of the active tab keeps it active");
  check(model.replace(4, tab(6)) == nullptr && model.size() == 2, "replace out of range does nothing");

  model.clear();
  check(model.empty() && model.activeIndex() == Model::npos, "clear empties the model");
}
