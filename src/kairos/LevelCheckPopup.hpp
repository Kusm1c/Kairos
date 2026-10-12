#pragma once
#include "mod/level_warnings.hpp"

#include <Geode/Geode.hpp>

#include <string>
#include <vector>

namespace kp {

// "Level check": what the solver found about a level, most severe first, each with why.
void showLevelCheck(const std::string& levelName, std::vector<levelwarn::Note> notes);

// A severity's colour, shared with the play menu's status line.
cocos2d::ccColor3B severityColor(levelwarn::Severity s);

} // namespace kp
