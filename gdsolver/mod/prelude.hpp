#pragma once

#include <Geode/Geode.hpp>
#include <Geode/modify/GJBaseGameLayer.hpp>
#include <Geode/modify/EnhancedGameObject.hpp>
#include <Geode/modify/PlayerObject.hpp>
#include <Geode/modify/PlayLayer.hpp>
#include <Geode/modify/MenuLayer.hpp>
#include <Geode/modify/GameManager.hpp>
#include <Geode/modify/GameStatsManager.hpp>
#include <Geode/modify/GJGameLevel.hpp>
#include <Geode/modify/FMODAudioEngine.hpp>
#include <Geode/modify/AppDelegate.hpp>
#include "mod/gd_offsets.hpp"   // raw offsets into GD, per platform
#ifdef GEODE_IS_WINDOWS
#include <Windows.h>
#endif
#include <algorithm>
#include <chrono>
#include <coroutine>
#include <thread>
#include <atomic>
#include <cmath>
#include <exception>
#include <new>
#include <climits>
#include <deque>
#include <cstring>
#include <map>
#include <unordered_map>
#include <unordered_set>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <vector>

using namespace geode::prelude;
