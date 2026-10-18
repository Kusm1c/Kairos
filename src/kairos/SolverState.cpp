// The only Kairos file that reads the solver's own state (src/mod).
#include "mod/playlayer_helpers.hpp"

#include "Jobs.hpp"
#include "ModGuard.hpp"

#include <chrono>
#include <fstream>
#include <sstream>

namespace kp {
bool solverDriving() { return p1::g_started && !p1::g_sessionOver; }

SolveStatus solveStatus() {
    using namespace p1;
    SolveStatus s;
    s.solving = solverDriving() && g_cfg.dpSolve;
    if (!s.solving) return s;
    if (auto* pl = PlayLayer::get(); pl && pl->m_levelLength > 1.f)
        s.level = std::clamp((double)g_hudVerifiedX / pl->m_levelLength, 0.0, 1.0);
    if (const auto pr = dpbridge::progress(); pr.running && pr.horizon > pr.from)
        s.search = std::clamp((double)(pr.tick - pr.from) / (double)(pr.horizon - pr.from), 0.0, 1.0);
    s.iteration = g_hudIter;
    s.phase = g_hudPhase;
    s.seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - solver::g_solveStart).count();
    return s;
}

void stopSolve() {
    if (!p1::g_sessionOver) p1::endSession("kairos_stop");   // ends with the game's onQuit
}

void saveModelWorld(int levelId, const std::filesystem::path& dir) {
    using namespace p1::dpsolve;
    if (g_csv.empty() || p1::g_cfg.levelId != levelId) return;
    std::error_code ec;
    std::filesystem::create_directories(dir, ec);
    std::ofstream(dir / "level.csv", std::ios::binary) << g_csv;
    std::vector<std::string> a{"--shipyq", num(kYq), "--shipvq", num(kVq)};
    addWorldArgs(a);
    if (!g_fixupPath.empty() && std::filesystem::exists(g_fixupPath, ec)) {
        a.push_back("--fixups");
        a.push_back(g_fixupPath);
    }
    // Files go beside, named "@file" in the arguments.
    std::ofstream args(dir / "args.txt");
    for (const auto& s : a) {
        const std::filesystem::path p(s);
        if (s.size() > 3 && std::filesystem::is_regular_file(p, ec)) {
            std::filesystem::copy_file(p, dir / p.filename(), std::filesystem::copy_options::overwrite_existing, ec);
            args << "@" << p.filename().string() << "\n";
        } else {
            args << s << "\n";
        }
    }
}

long long modelReplay(const std::filesystem::path& dir, const kpsim::macro::Plan& plan, double& seconds,
                      std::string& error) {
    seconds = 0;
    std::ifstream csvIn(dir / "level.csv", std::ios::binary), argsIn(dir / "args.txt");
    if (!csvIn || !argsIn) {
        error = "no model world saved for this level (solve it first)";
        return -2;
    }
    std::stringstream csv;
    csv << csvIn.rdbuf();
    const auto planPath = dir / "plan.txt";
    std::ofstream(planPath) << kpsim::macro::writeSolverPlan(plan);
    std::vector<std::string> a{"--replay", planPath.string(), "--out", (dir / "check").string(), "--threads", "1"};
    for (std::string line; std::getline(argsIn, line);)
        if (!line.empty()) a.push_back(line[0] == '@' ? (dir / line.substr(1)).string() : line);
    const auto t0 = std::chrono::steady_clock::now();
    const int rc = dpbridge::solveInProcess(csv.str(), a);
    seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
    if (rc < 0) {
        error = "the solver core failed (" + std::to_string(rc) + ")";
        return -2;
    }
    return dpbridge::outcome().replayDiedT;
}
} // namespace kp
