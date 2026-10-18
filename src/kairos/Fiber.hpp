#pragma once
#include <chrono>
#include <exception>
#include <functional>

namespace kp {
// Runs a body in slices on the main thread (Windows fiber).
class FiberTask {
public:
    explicit FiberTask(std::function<void()> body);
    ~FiberTask();
    FiberTask(const FiberTask&) = delete;
    FiberTask& operator=(const FiberTask&) = delete;

    // Main side: runs until the body yields past the budget or ends. true = ended.
    bool resume(double budgetMs);
    bool done() const { return m_done; }
    std::exception_ptr error() const { return m_error; }

    // Body side: hands control back if the slice is over.
    static void yieldIfDue();

private:
    static void __stdcall entry(void* self);

    std::function<void()> m_body;
    void* m_fiber = nullptr;
    void* m_main = nullptr;
    bool m_done = false;
    std::exception_ptr m_error;
    std::chrono::steady_clock::time_point m_deadline;
};
} // namespace kp
