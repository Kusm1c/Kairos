#include "Fiber.hpp"

#include <stdexcept>

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>

namespace kp {
namespace {
FiberTask* s_current = nullptr;
}

FiberTask::FiberTask(std::function<void()> body) : m_body(std::move(body)) {
    m_fiber = CreateFiberEx(0, 32u << 20, 0, &FiberTask::entry, this);
    if (!m_fiber) {
        m_done = true;
        m_error = std::make_exception_ptr(std::runtime_error("CreateFiberEx failed"));
    }
}

FiberTask::~FiberTask() {
    // An unfinished body leaks its stack objects rather than unwinding them here.
    if (m_fiber) DeleteFiber(m_fiber);
}

void __stdcall FiberTask::entry(void* self) {
    auto* t = static_cast<FiberTask*>(self);
    try {
        t->m_body();
    } catch (...) {
        t->m_error = std::current_exception();
    }
    t->m_done = true;
    SwitchToFiber(t->m_main);   // never returns
}

bool FiberTask::resume(double budgetMs) {
    if (m_done) return true;
    const bool wasFiber = IsThreadAFiber() != FALSE;
    m_main = wasFiber ? GetCurrentFiber() : ConvertThreadToFiber(nullptr);
    if (!m_main) return m_done;
    m_deadline = std::chrono::steady_clock::now() +
                 std::chrono::microseconds((long long)(budgetMs * 1000.0));
    FiberTask* prev = s_current;
    s_current = this;
    SwitchToFiber(m_fiber);
    s_current = prev;
    if (!wasFiber) ConvertFiberToThread();
    return m_done;
}

void FiberTask::yieldIfDue() {
    FiberTask* t = s_current;
    if (!t || std::chrono::steady_clock::now() < t->m_deadline) return;
    SwitchToFiber(t->m_main);
}

} // namespace kp
