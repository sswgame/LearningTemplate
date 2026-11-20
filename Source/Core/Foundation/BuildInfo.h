/**
 * @file BuildInfo.h
 * @brief 빌드 구성 · 플랫폼 · 빌드 타깃 이름 상수입니다. 값은 CMake 가 정합니다.
 * @details `SW_BUILD_CONFIG_NAME` 은 `cmake/Modules/Build/BuildLayout.cmake`(Shipping 이면 "Shipping", 아니면 구성 이름),
 *          `SW_PLATFORM_NAME` 은 `cmake/Toolchain/Platform/` 의 플랫폼 파일(그 플랫폼의 `SW_PLATFORM_*` 매크로 옆)가 정의합니다.
 *          `SW_TARGET_NAME` 은 `cmake/Modules/Build/BuildLayout.cmake`(빌드 타깃 종류 `SW_TARGET_TYPE`)가 정의하고, 클라이언트 · 서버 코드를 담는지는 그 이름에서 만든 상수다
 *          — 전처리 갈래(`#if`)로 클라이언트 구역을 두지 못하게 매크로를 두지 않는다(`TargetMacroCheck.h`).
 *          이름을 코드에서 `#if` 사슬로 다시 만들지 말 것 — 원본은 CMake 한 곳입니다.
 */
#pragma once
#include "Core/Foundation/Macros.h"
#include "Core/Foundation/Types.h"

#if !defined( SW_BUILD_CONFIG_NAME ) || !defined( SW_PLATFORM_NAME ) || !defined( SW_TARGET_NAME )
    #error "SW_BUILD_CONFIG_NAME / SW_PLATFORM_NAME / SW_TARGET_NAME are defined by CMake (sw_global_options / sw_platform_*) - link the target to sw_global_options"
#endif

namespace sw::build
{
    /** @brief 빌드 구성 이름입니다("Debug" · "Release" · "Shipping"). */
    inline constexpr const utf8* kConfigName = SW_BUILD_CONFIG_NAME;
    /** @brief 플랫폼 이름입니다("Windows" · "Linux" · "Android" · "Web"). */
    inline constexpr const utf8* kPlatformName = SW_PLATFORM_NAME;
    /** @brief 빌드 타깃 종류 이름입니다("Game" · "Client" · "Server"). CMake `SW_TARGET_TYPE` 이 정합니다. */
    inline constexpr const utf8* kTargetName = SW_TARGET_NAME;
#if defined( SW_SHIPPING )
    /**
     * @brief 배포 구성이면 true 입니다. 개발 · 배포 차이가 값뿐이면 이 상수를 `if` · `if constexpr` 로 읽는다 — 두 갈래가 모든 구성에서 컴파일된다.
     * @details 갈래 하나가 다른 구성에 없는 헤더 · 모듈을 쓰면 그 자리만 `#if defined( SW_SHIPPING )`, 기능 전체가 배포에 없으면 그 기능이 스스로 등록하고 호스트는 찾기만 한다
     *          (빈 구현 파일 없이 기능이 스스로 등록). 규칙은 `docs/agents/BuildConfigSwitch.md`.
     */
    inline constexpr bool kShipping = true;
#else
    inline constexpr bool kShipping = false;
#endif
#if defined( SW_DO_CHECKS )
    /**
     * @brief 우리 불변식 검사(단언 · 스레드 소유 · 잠금 깊이 …)를 컴파일한 구성이면 true 입니다 — `SW_DO_CHECKS` 의 C++ 짝(배포가 아니면 켜진다).
     * @details 검사 결과를 값으로만 읽는 자리(테스트의 기대 수 등)는 `#if` 대신 이 상수를 쓴다. 검사 코드 자체(배포에 없는 함수 · 멤버)는 그대로 `SW_DO_CHECKS` 로 가른다.
     */
    inline constexpr bool kDoChecks = true;
#else
    inline constexpr bool kDoChecks = false;
#endif
#if defined( SW_DEBUG )
    /**
     * @brief Debug 구성이면 true 입니다. 비용이 커서 Debug 에만 켜는 진단(프로파일 카운터 · 셰이더 라이브 리로드 · 경고 한 줄)을 `if constexpr` 로 가른다 —
     *        두 갈래가 모든 구성에서 컴파일되어 묶음 검증이 짓지 않는 Debug 갈래가 썩지 않는다. 단언 · 검사는 이것이 아니라 `kDoChecks`(`SW_DO_CHECKS`, 배포가 아니면 켜진다)다.
     */
    inline constexpr bool kDebug = true;
#else
    inline constexpr bool kDebug = false;
#endif
#if defined( SW_STATIC_MODULES )
    /**
     * @brief 모듈(엔진 · RHI 백엔드 · GameFramework · 키트 · 게임)을 한 실행 파일에 정적으로 링크한 구성이면 true 입니다 — 배포 전부와 웹 · Android 개발 구성.
     * @details 정적 구성은 게임 하나를 굽고(`config::kShippingGameName` — `-game=` 을 보지 않는다), 설정을 디스크의 프로젝트 폴더가 아니라 구운 JSON 으로 읽는다
     *          (기기에는 프로젝트 폴더가 없다). 에디터 모듈 · 핫 리로드가 없다. 값만 다른 갈래는 이 상수를, 헤더 · 함수가 없는 갈래는 `SW_WITH_DYNAMIC_MODULES` 를 쓴다.
     */
    inline constexpr bool kStaticModules = true;
#else
    inline constexpr bool kStaticModules = false;
#endif
    static_assert( kShipping == false || kStaticModules, "a shipping build links its modules statically (SW_STATIC_MODULES)" );
    /** @brief 이 빌드에 클라이언트 코드(창 · 렌더 · 입력 장치 · 오디오 장치)가 들어 있으면 true 입니다(Game · Client). */
    inline constexpr bool kWithClientCode = string_view( kTargetName ) != "Server";
    /** @brief 이 빌드에 서버 코드가 들어 있으면 true 입니다(Game · Server). */
    inline constexpr bool kWithServerCode = string_view( kTargetName ) != "Client";
    static_assert( string_view( kTargetName ) == "Game" || string_view( kTargetName ) == "Client" || string_view( kTargetName ) == "Server",
                   "SW_TARGET_NAME must be Game, Client or Server (SW_TARGET_TYPE via sw_global_options)" );
} // namespace sw::build
