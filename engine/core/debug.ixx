// Copyright (c) 2026 Yoon-Seong Kim, Seong-Hyeon Cho. All rights reserved.

export module Debug;

import std;

export namespace Debug
{
[[noreturn]] void ReportCrash(const char* message, int exit_code = 1) noexcept;
}

namespace Debug
{
namespace Platform
{
void ShowCrash(const char* message) noexcept;
}
}
