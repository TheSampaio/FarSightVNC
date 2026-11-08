// Precompiled header of the CMake build (FSV_USE_PCH). Only for translation
// units that reach util/winhdr.h before any other SDK header: it must stay the
// first include, because it fixes WINVER/_WIN32_WINNT before <windows.h>.
// Add only stable headers that the code really uses.
#pragma once

#include "util/winhdr.h"

#include <algorithm>
#include <deque>
#include <functional>
#include <list>
#include <map>
#include <memory>
#include <set>
#include <sstream>
#include <string>
#include <utility>
#include <vector>
