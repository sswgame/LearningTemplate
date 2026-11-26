/**
 * @file BuildConfig.h
 * @brief 빌드 구성 깃발 상수입니다(배포 · 검사 · Debug · 정적 모듈) — `SW_SHIPPING` · `SW_DO_CHECKS` · `SW_DEBUG` · `SW_STATIC_MODULES` 의 C++ 짝과, 그 깃발로 고르는 진단 기본값.
 * @details 구성 이름 · 플랫폼 · 타깃 이름(`BuildInfo.h`)과 달리 CMake 이름 매크로가 없어도 컴파일된다 — 리플렉션 파서처럼 sw_global_options 를 링크하지 않는
 *          도구가 읽는 기반 헤더(`MemoryTag.h` 등)는 이 헤더만 포함한다(언리얼 `Build.h` 의 UE_BUILD_* 와 같은 자리).
 */
#pragma once
#include "Core/Foundation/Macros.h"

namespace sw::build
{
#if defined( SW_SHIPPING )
    /**
     * @brief 배포 구성이면 true 입니다. 개발 · 배포 차이가 값뿐이면 이 상수를 `if` · `if constexpr` 로 읽는다 — 두 갈래가 모든 구성에서 컴파일된다.
     * @details 갈래 하나가 다른 구성에 없는 헤더 · 모듈을 쓰면 그 자리만 `#if defined( SW_SHIPPING )`, 기능 전체가 배포에 없으면 그 기능이 스스로 등록하고 호스트는 찾기만 한다
     *          (빈 구현 파일 없이 기능이 스스로 등록). 규칙은 `docs/agents/CppStyle.md "Build configuration switches"`.
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

    // 진단 기본값. 구성마다 켜고 끄는 진단을 파일마다 따로 고르지 않고 여기서 정한다(언리얼 `Build.h` 의 `STATS` · `ENABLE_LOW_LEVEL_MEM_TRACKER` 자리).
    /**
     * @brief 메모리 진단(CRT 누수 검사 · 메모리 태그 추적 · 누수 창)을 켜는 구성이면 true 입니다. 할당마다 추적 비용이 붙어 Debug 만 켠다.
     * @details ASan 구성은 Release 라 여기서 꺼진다. Windows ASan 은 CRT 디버그 힙을 쓸 수 없고 힙 오용은 ASan 이 직접 잡는다(언리얼 -EnableASan 도 메모리 추적을 더하지 않는다).
     */
    inline constexpr bool kMemoryDiagnostics = kDebug;
    /** @brief 교착 탐지(`-deadlock-detection`, `sw::mutex` 잠금 추적)를 받는 구성이면 true 입니다. 배포는 잠금 추적을 짓지 않는다. */
    inline constexpr bool kDeadlockDetection = kShipping == false;
    /**
     * @brief 그래픽 API 의 개발 진단 확장(Vulkan `VK_EXT_device_fault` — 장치 소실 때 결함 주소 · 공급사 코드)을 켜는 구성이면 true 입니다. 배포는 확장을 켜지 않는다.
     * @details 장치 소실 브레드크럼(DRED · GPU 표지)은 배포에서도 시작 인자로 켜는 런타임 스위치라(`RHIDiagnosticSwitch`) 여기 들지 않는다.
     */
    inline constexpr bool kGraphicsDeviceFaultReport = kShipping == false;
    /** @brief 비싼 프로파일 카운터(`SW_PROFILE_COUNT_VERBOSE`, 항목마다 세는 값)를 세는 구성이면 true 입니다. 언리얼 stat 의 verbose 단계 자리다. */
    inline constexpr bool kVerboseProfileCounters = kDebug;
} // namespace sw::build
