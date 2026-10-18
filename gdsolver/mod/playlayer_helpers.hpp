#pragma once
// Helpers for the PlayLayer hook (visibility-crash log, checkpoint notes).
#include "mod/render_trace.hpp"

using namespace p1;

inline uintptr_t g_visAvRip = 0, g_visAvRead = 0, g_visAvRet = 0;
#ifdef GEODE_IS_WINDOWS
inline int visAvNote(EXCEPTION_POINTERS* ep) {
    if (ep && ep->ExceptionRecord) {
        const uintptr_t base = (uintptr_t)GetModuleHandleW(nullptr);
        auto* nt = (IMAGE_NT_HEADERS*)(base + ((IMAGE_DOS_HEADER*)base)->e_lfanew);
        auto rel = [&](uintptr_t a) {
            return (a >= base && a - base < nt->OptionalHeader.SizeOfImage) ? a - base : a;
        };
        g_visAvRip = rel((uintptr_t)ep->ExceptionRecord->ExceptionAddress);
        g_visAvRead = ep->ExceptionRecord->NumberParameters >= 2
                          ? (uintptr_t)ep->ExceptionRecord->ExceptionInformation[1] : 0;
        g_visAvRet = 0;
        if (ep->ContextRecord) {
            __try {
                g_visAvRet = rel(*(uintptr_t*)ep->ContextRecord->Rsp);
            } __except (EXCEPTION_EXECUTE_HANDLER) {
            }
        }
    }
    return EXCEPTION_EXECUTE_HANDLER;
}
#endif
inline void logVisibilityCrashSwallowed() {
    static int s_count = 0;
    ++g_visAVs;   // the real count is this one (the log is cut off at 5 entries, so it cannot
                  // be used to count)
    if (++s_count <= 5) {
        log::warn("updateVisibility access violation swallowed ({}), frame skipped", s_count);
        char b[160];
        snprintf(b, sizeof(b), "visav: n=%d t=%lld rip=GeometryDash.exe+0x%llx read=0x%llx "
                 "ret=GeometryDash.exe+0x%llx",
                 s_count, (long long)g_tick, (unsigned long long)g_visAvRip,
                 (unsigned long long)g_visAvRead, (unsigned long long)g_visAvRet);
        writeResult(b);
    }
}

struct NoRecordGuard {
    GJBaseGameLayer* layer;
    bool saved;
    explicit NoRecordGuard(GJBaseGameLayer* l)
        : layer(progressBlockActive() ? l : nullptr), saved(l && l->m_isTestMode) {
        if (layer) layer->m_isTestMode = true;
    }
    ~NoRecordGuard() {
        if (layer) layer->m_isTestMode = saved;
    }
    NoRecordGuard(const NoRecordGuard&) = delete;
    NoRecordGuard& operator=(const NoRecordGuard&) = delete;
};

inline void noteCkpt(const char* what, PlayerObject* p, void* obj, long long solved) {
    char cb[256];
    snprintf(cb, sizeof(cb),
        "ckpt: %-6s tick=%lld solved=%lld x=%.2f obj=%p try=%d timeout=%d "
        "lastCkptT=%.6f totalT=%.6f",
        what, (long long)g_tick, solved,
        p ? p->getPositionX() : -1.f, obj,
        p ? (p->m_shouldTryPlacingCheckpoint ? 1 : 0) : -1,
        p ? (p->m_checkpointTimeout ? 1 : 0) : -1,
        p ? p->m_lastCheckpointTime : -1.0,
        p ? p->m_totalTime : -1.0);
    writeResult(cb);
}

inline bool visibilityPassOff() {
    if (g_started && !g_sessionOver && g_cfg.fastdt > 0 && g_cfg.skipRender && !g_realtimeOverride)
        return true;
    if (g_endzoneBurn) return true;
    if (itermap::g_seekBlindNow) return true;
    return g_secRenderWas >= 0;
}
