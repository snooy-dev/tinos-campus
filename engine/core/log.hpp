// Copyright (c) 2026 Yoon-Seong Kim, Seong-Hyeon Cho. All rights reserved.

#pragma once

import std;

#define LOG(level, ...) LOG_##level(__VA_ARGS__)

#ifdef _DEBUG
#define LOG_Info(...) (std::cout << std::format(__VA_ARGS__) << '\n')
#define LOG_Error(...) (std::cerr << std::format(__VA_ARGS__) << '\n')

#else
#define LOG_Info(...) ((void)0)
#define LOG_Error(...) ((void)0)

#endif
