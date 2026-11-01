// Copyright (c) 2026 Yoon-Seong Kim, Seong-Hyeon Cho. All rights reserved.

module;

#define WIN32_LEAN_AND_MEAN
#include <Windows.h>

module Debug;

void Debug::Platform::ShowCrash(const char* message) noexcept
{
#ifdef _DEBUG
    const HANDLE output = GetStdHandle(STD_ERROR_HANDLE);
    DWORD written{};
    WriteFile(output, message, static_cast<DWORD>(std::strlen(message)), &written, nullptr);
    WriteFile(output, "\n", 1, &written, nullptr);
#else
    std::array<wchar_t, 1024> text{};
    const bool converted = MultiByteToWideChar(
        CP_UTF8, MB_ERR_INVALID_CHARS, message, -1,
        text.data(), static_cast<int>(text.size())) != 0;
    const wchar_t* displayed = converted ? text.data() :
        L"A fatal error occurred. Its message could not be displayed.";
    MessageBoxW(nullptr, displayed, L"Tino's Campus", MB_OK | MB_ICONERROR);
#endif

    if (IsDebuggerPresent())
        DebugBreak();
}
