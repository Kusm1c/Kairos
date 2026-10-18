#pragma once
namespace orbtrace {
inline long long g_lastPress = -1; // most recent press tick
inline int g_lines = 0;            // total log lines (cap against flooding)
inline void reset() { g_lastPress = -1; }
}
namespace padtrace {
inline int g_lines = 0;            // total log lines (cap against flooding)
inline void reset() { g_lines = 0; }
}
namespace touchseed {
inline std::unordered_map<int, int> g_first;   // trigger uid -> first tick
inline int g_p2 = 0;
inline int g_calls = 0;
inline void reset() { g_first.clear(); g_p2 = 0; g_calls = 0; }
}
namespace touchfire {
inline std::unordered_map<int, int> g_first;   // touch trigger uid -> first tick, p1
inline int g_p2 = 0;                           // calls for the second player, counted only
inline int g_calls = 0;                        // a denominator for a zero -- see touchseed::g_calls
inline void reset() { g_first.clear(); g_p2 = 0; g_calls = 0; }
}
namespace portalseed {
inline std::unordered_map<int, int> g_first;    // gravity-portal uid -> first tick, p1
inline std::unordered_map<int, int> g_first2;   // ...and p2
inline int g_calls = 0;   // a denominator for a zero -- see touchseed::g_calls
inline void reset() { g_first.clear(); g_first2.clear(); g_calls = 0; }
}
namespace padseed {
inline std::unordered_map<int, int> g_first;    // pad uid -> first tick, p1
inline int g_calls = 0;   // a denominator for a zero -- see touchseed::g_calls
inline void reset() { g_first.clear(); g_calls = 0; }
}
namespace ringseed {
inline std::vector<std::pair<int, int>> g_fired;   // (ring uid, tick), p1, oldest first
inline std::vector<std::pair<int, int>> g_fired2;  // ...p2's
inline int g_calls = 0;   // ringJump calls on p1 -- a denominator for a zero
inline void reset() { g_fired.clear(); g_fired2.clear(); g_calls = 0; }
}
