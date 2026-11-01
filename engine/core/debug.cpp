// Copyright (c) 2026 Yoon-Seong Kim, Seong-Hyeon Cho. All rights reserved.

module Debug;

namespace
{
std::atomic_flag reporting_crash{};
}

void Debug::ReportCrash(const char* message, int exit_code) noexcept
{
    // A fault in the crash reporter must not recursively invoke it again.
    if (!reporting_crash.test_and_set(std::memory_order_relaxed))
        Platform::ShowCrash(message ? message : "An unknown fatal error occurred.");

    std::_Exit(exit_code == 0 ? 1 : exit_code);
}
