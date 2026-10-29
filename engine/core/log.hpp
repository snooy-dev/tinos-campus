// Copyright (c) 2026 Yoon-Seong Kim, Seong-Hyeon Cho. All rights reserved.

#pragma once

import std;

#ifdef _DEBUG
#define LOG_LEVEL_Info std::cout
#define LOG_LEVEL_Error std::cerr
#define LOG(level, ...) (LOG_LEVEL_##level << std::format(__VA_ARGS__) << '\n')
#else
#define LOG(level, ...) ((void)0)
#endif
