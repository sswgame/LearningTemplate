/**
 * @file BuildInfo.h
 * @brief 빌드 구성 · 플랫폼 · 빌드 타깃 이름 상수입니다. 값은 CMake 가 정합니다.
 * @details `SW_BUILD_CONFIG_NAME` 은 `cmake/Engine/BuildLayout.cmake`(Shipping 이면 "Shipping", 아니면 구성 이름),
 *          `SW_PLATFORM_NAME` 은 `cmake/Modules/Platform/` 의 플랫폼 파일(그 플랫폼의 `SW_PLATFORM_*` 매크로 옆)가 정의합니다.
 *          `SW_TARGET_NAME` · `SW_WITH_CLIENT_CODE` · `SW_WITH_SERVER_CODE` 는 `cmake/Engine/BuildLayout.cmake`(빌드 타깃 종류 `SW_TARGET_TYPE`)가 정의합니다.
 *          이름을 코드에서 `#if` 사슬로 다시 만들지 말 것 — 정본은 CMake 한 곳입니다.
 */
#pragma once
#include "Core/Common/Types.h"

#if !defined( SW_BUILD_CONFIG_NAME ) || !defined( SW_PLATFORM_NAME ) || !defined( SW_TARGET_NAME )
    #error "SW_BUILD_CONFIG_NAME / SW_PLATFORM_NAME / SW_TARGET_NAME are defined by CMake (sw_global_options / sw_platform_*) - link the target to sw_global_options"
#endif

namespace sw::build
{
    /** @brief 빌드 구성 이름입니다("Debug" · "Release" · "Shipping"). */
    inline constexpr const utf8* kConfigName = SW_BUILD_CONFIG_NAME;
    /** @brief 플랫폼 이름입니다("Windows" · "Linux"). */
    inline constexpr const utf8* kPlatformName = SW_PLATFORM_NAME;
    /** @brief 빌드 타깃 종류 이름입니다("Game" · "Client" · "Server"). CMake `SW_TARGET_TYPE` 이 정합니다. */
    inline constexpr const utf8* kTargetName = SW_TARGET_NAME;
#if defined( SW_WITH_CLIENT_CODE )
    /** @brief 이 빌드에 클라이언트 코드(창 · 렌더 · 입력 장치 · 오디오 장치)가 들어 있으면 true 입니다(Game · Client). */
    inline constexpr bool kWithClientCode = true;
#else
    inline constexpr bool kWithClientCode = false;
#endif
#if defined( SW_WITH_SERVER_CODE )
    /** @brief 이 빌드에 서버 코드가 들어 있으면 true 입니다(Game · Server). */
    inline constexpr bool kWithServerCode = true;
#else
    inline constexpr bool kWithServerCode = false;
#endif
} // namespace sw::build
