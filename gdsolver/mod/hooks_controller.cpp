#include "mod/config.hpp"

#ifdef GEODE_IS_WINDOWS   // XInput exists only on Windows; the file is empty elsewhere

using namespace p1;

namespace {
DWORD WINAPI xinputGetStateOff(DWORD, void*) { return ERROR_DEVICE_NOT_CONNECTED; }
DWORD WINAPI xinputGetCapabilitiesOff(DWORD, DWORD, void*) { return ERROR_DEVICE_NOT_CONNECTED; }

template <class Detour>
int hookExport(HMODULE mod, const char* dll, const char* func, Detour* detour) {
    auto* p = reinterpret_cast<void*>(GetProcAddress(mod, func));
    if (!p) return 0;
    auto res = Mod::get()->hook(p, detour, std::string(dll) + "!" + func);
    if (res.isErr()) {
        log::error("controller: hooking {}!{} failed: {}", dll, func, res.unwrapErr());
        return 0;
    }
    return 1;
}
}  // namespace

$execute {
    resolveDataDir();
    bool off = workerTag() != "local";
    if (auto arg = Mod::get()->getLaunchArgument("controller")) {
        if (*arg == "off") off = true;
        else if (*arg == "on") off = false;
        else log::error("controller: ignoring unknown value '{}' (want off|on)", *arg);
    }
    if (!off) return;
    int n = 0;
    for (const char* dll : { "xinput1_4.dll", "xinput1_3.dll", "xinput1_2.dll",
                             "xinput1_1.dll", "xinput9_1_0.dll" }) {
        HMODULE m = GetModuleHandleA(dll);
        if (!m) m = LoadLibraryA(dll);
        if (!m) continue;  // this Windows does not have that generation — nothing to hook
        n += hookExport(m, dll, "XInputGetState", &xinputGetStateOff);
        n += hookExport(m, dll, "XInputGetCapabilities", &xinputGetCapabilitiesOff);
    }
    log::info("controller: disabled for this process ({}), {} XInput entry points hooked",
              workerTag(), n);
    if (n == 0)
        log::warn("controller: no XInput entry point found — the pad is NOT blocked");
}

#endif  // GEODE_IS_WINDOWS
