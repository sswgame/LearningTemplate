/**
 * @file BuildInfo.h
 * @brief 빌드 구성 · 플랫폼 이름 상수입니다. 값은 CMake 가 정합니다.
 * @details `SW_BUILD_CONFIG_NAME` 은 `cmake/Engine/BuildLayout.cmake`(Shipping 이면 "Shipping", 아니면 구성 이름),
 *          `SW_PLATFORM_NAME` 은 `cmake/Modules/Platform/` 의 플랫폼 파일(그 플랫폼의 `SW_PLATFORM_*` 매크로 옆)가 정의합니다.
 *          이름을 코드에서 `#if` 사슬로 다시 만들지 말 것 — 정본은 CMake 한 곳입니다.
 */
#pragma once
#include "Core/Common/Types.h"

#if !defined( SW_BUILD_CONFIG_NAME ) || !defined( SW_PLATFORM_NAME )
    #error "SW_BUILD_CONFIG_NAME / SW_PLATFORM_NAME are defined by CMake (sw_global_options / sw_platform_*) - link the target to sw_global_options"
#endif

namespace sw::build
{
    /** @brief 빌드 구성 이름입니다("Debug" · "Release" · "Shipping"). */
    inline constexpr const utf8* kConfigName = SW_BUILD_CONFIG_NAME;
    /** @brief 플랫폼 이름입니다("Windows" · "Linux" · "macOS"). */
    inline constexpr const utf8* kPlatformName = SW_PLATFORM_NAME;
} // namespace sw::build
