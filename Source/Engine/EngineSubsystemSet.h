/**
 * @file Engine/EngineSubsystemSet.h
 * @brief 호스트의 세 범위(코어 · 서비스 · 기동 단계)에 넣을 서브시스템의 **단 하나뿐인 목록**입니다.
 *
 * @details 엔진 루프(`Engine::initialize`) · 테스트 하네스(`TestHostRuntime`) · 기동 순서 테스트(`EngineInitSequenceTest`)가 모두 이 세 함수로 범위를 채운다.
 *          서비스 · 기동 단계의 서브시스템을 더하거나 빼려면 자리 표(`Engine/Foundation/EngineSubsystemRegistration.h` 의 `SW_ENGINE_SUBSYSTEM_SLOTS`)에 자리를 두고
 *          그 서브시스템의 .cpp 에 `SW_ENGINE_SUBSYSTEM` 등록을 둔다 — 호스트마다 목록을 따로 두지 않으므로 한 곳에서 빠뜨리는 일이 없고, 빠지면 목록 함수가 멈춘다.
 *          범위 값(`ServiceParts` · `StartupSetup` · `StartupParts`)은 공장이 쓰므로 그 헤더(바닥 층)에 있다. 이 헤더는 `StartupSetup` 의 대역 형식을 완전하게 한다.
 *          호스트마다 다른 것은 아래 `*Setup` 에 이름 붙은 값으로만 준다(인자 · 켜고 끄는 단계 · 본문을 바꾼 대역). 서는지 여부는 서브시스템 클래스가 선언한 호스트
 *          비트(`EngineSubsystemHost`)가 `initializeAll` 에서 가른다 — 목록은 그것을 따로 판단하지 않는다.
 *          범위의 부모(`setParentScope`)와 초기화는 호출한 쪽이 한다. 이 함수들은 만들어 넣고 범위에 거는 일까지만 한다.
 */
#pragma once
#include "Core/Foundation/Macros.h"
#include "Core/Foundation/Types.h"
#include "Core/Memory/Memory.h"

#include "Engine/Audio/IAudioService.h"
#include "Engine/Foundation/EngineSubsystemRegistration.h"
#include "Engine/HeadlessSubsystem.h"
#include "Engine/Localization/EngineDefaultAssetsSubsystem.h"
#include "Engine/Reflection/ReflectionSubsystem.h"

namespace sw
{
    class CommandLineSubsystem;
    class ContentRootSubsystem;
    class CrashHandlerSubsystem;
    class DiagnosticsSubsystem;
    class MemoryBudgetSubsystem;
    class SubsystemScope;

    namespace engine
    {
        /** @brief 코어 범위(진단 도구 · 이름 풀 · 로거 · 명령줄 · 크래시 핸들러 · 정적 저장소 · 메모리 예산)의 호스트별 값입니다. */
        struct CoreSetup
        {
            int32  _argc{ 0 };
            utf8** _ppArgv{ nullptr };
            bool   _bDiagnostics{ false };  ///< 교착 감지기 · 메모리 프로파일러를 켠다(엔진 루프는 Debug, 테스트 실행 파일은 늘)
            bool   _bMemoryBudget{ false }; ///< 메모리 예산을 둔다 — 하네스는 진단 구성의 추적을 그대로 둔다
        };
    } // namespace engine

    namespace engine
    {
        /** @brief 코어 범위에서 호스트가 직접 쓰는 서브시스템입니다(범위가 소유한다). */
        struct CoreParts
        {
            DiagnosticsSubsystem*  _pDiagnostics{ nullptr };
            CommandLineSubsystem*  _pCommandLine{ nullptr };
            CrashHandlerSubsystem* _pCrashHandler{ nullptr };
            ContentRootSubsystem*  _pContentRoot{ nullptr };  ///< 리소스 경로 상태(`AssetPaths`)의 주인 — 서비스 표가 서기 전에 경로를 쓰는 기동 코드가 받는다
            MemoryBudgetSubsystem* _pMemoryBudget{ nullptr }; ///< `_bMemoryBudget` 가 꺼졌으면 nullptr
        };
    } // namespace engine

    namespace engine
    {
        /** @brief @p coreScope 에 코어 서브시스템을 넣고 명령줄 · 전역 변수 표를 겁니다. 초기화는 호출한 쪽이 한다. */
        SW_API CoreParts addCoreSubsystems( SubsystemScope& coreScope, const CoreSetup& setup );

        /**
         * @brief 코어 범위를 초기화하고 메모리 프로파일러를 서비스 표에 겁니다. 실패하면 false 이고 이유는 `coreScope.getCollection().getError()` 입니다.
         * @details 초기화와 프로파일러 걸기를 한 호출로 묶어 호스트마다 따로 기억하지 않게 한다. 실패 로그는 호스트가 제 방식으로 낸다.
         */
        [[nodiscard]] SW_API bool initializeCoreSubsystems( SubsystemScope& coreScope, const CoreParts& parts );

        /** @brief @p services 에 엔진 범위보다 오래 사는 공용 서비스를 넣습니다. 초기화는 호출한 쪽이 한다. */
        SW_API ServiceParts addServiceSubsystems( SubsystemScope& services );

        /** @brief @p startup 에 기동 단계 서브시스템을 넣고 안의 객체(타입 표 · 기본 에셋 표 · 팩 설정 · 프레임 렌더러)를 겁니다. 부모 범위는 미리 정해 둔다. */
        SW_API StartupParts addStartupSubsystems( SubsystemScope& startup, StartupSetup&& setup );
    } // namespace engine
} // namespace sw
