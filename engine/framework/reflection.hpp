// Copyright (c) 2026 Yoon-Seong Kim, Seong-Hyeon Cho. All rights reserved.
#pragma once
// Include before importing GameFramework when using RTTR APIs or registration macros.
#pragma warning(push)
// RTTR 0.9.6 uses std::aligned_storage, deprecated in C++23.
// Its exported registration classes also contain non-exported implementation types.
#pragma warning(disable: 4996 4251)
#include <rttr/type>
#include <rttr/registration>
#pragma warning(pop)
