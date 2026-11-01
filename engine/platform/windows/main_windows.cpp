// Copyright (c) 2026 Yoon-Seong Kim, Seong-Hyeon Cho. All rights reserved.

#define WIN32_LEAN_AND_MEAN
#include <Windows.h>

import std;
import Engine;
import Debug;

namespace
{
int GuardedMain()
{
    Engine engine;
    const auto result = engine.Initialize();
    if (!result)
        Debug::ReportCrash(result.error());

    engine.Run();

    return 0;
}

const char* DescribeException(unsigned long code)
{
    switch (code)
    {
    case EXCEPTION_ACCESS_VIOLATION:
        return "Invalid memory access.";
    case EXCEPTION_IN_PAGE_ERROR:
        return "A required memory page could not be loaded.";
    case EXCEPTION_ILLEGAL_INSTRUCTION:
        return "Invalid processor instruction.";
    case EXCEPTION_PRIV_INSTRUCTION:
        return "Unauthorized processor instruction.";
    case EXCEPTION_DATATYPE_MISALIGNMENT:
        return "Misaligned memory access.";
    case EXCEPTION_ARRAY_BOUNDS_EXCEEDED:
        return "Hardware-detected array bounds violation.";
    case EXCEPTION_INT_DIVIDE_BY_ZERO:
        return "Integer division by zero.";
    case EXCEPTION_INT_OVERFLOW:
        return "Integer arithmetic overflow.";
    case EXCEPTION_FLT_DIVIDE_BY_ZERO:
        return "Floating-point division by zero.";
    case EXCEPTION_FLT_OVERFLOW:
        return "Floating-point overflow.";
    case EXCEPTION_FLT_UNDERFLOW:
        return "Floating-point underflow.";
    case EXCEPTION_FLT_INVALID_OPERATION:
        return "Invalid floating-point operation.";
    case EXCEPTION_FLT_DENORMAL_OPERAND:
        return "Denormal floating-point operand.";
    case EXCEPTION_FLT_INEXACT_RESULT:
        return "Inexact floating-point result.";
    case EXCEPTION_FLT_STACK_CHECK:
        return "Floating-point stack fault.";
    default:
        return "An unexpected execution fault occurred.";
    }
}

int FilterCrash(unsigned long code)
{
    // Preserve debugger control and leave exhausted-stack handling to Windows.
    if (IsDebuggerPresent() ||
        code == EXCEPTION_BREAKPOINT ||
        code == EXCEPTION_SINGLE_STEP ||
        code == EXCEPTION_STACK_OVERFLOW)
        return EXCEPTION_CONTINUE_SEARCH;

    // Terminate before unwinding potentially damaged application state.
    std::array<char, 256> message{};
    std::snprintf(message.data(), message.size(), "%s\nException code: 0x%08lX",
        DescribeException(code), code);
    Debug::ReportCrash(message.data(), static_cast<int>(code));
}

int Startup()
{
    __try
    {
        return GuardedMain();
    }
    __except (FilterCrash(GetExceptionCode()))
    {
        return 1;
    }
}
}

int main()
{
    return Startup();
}

int WINAPI wWinMain(_In_ HINSTANCE, _In_opt_ HINSTANCE, _In_ LPWSTR, _In_ int)
{
    return Startup();
}
