/**
 * @file Engine/Bootstrap/MemoryBudgetSubsystem.h
 * @brief 메모리 태그 예산 감시(`MemoryBudgetMonitor`)를 부트스트랩 범위에서 세웁니다 — 추적 설정(`-gv_memoryTracking`)을 걸고 예산 표(데이터)를 읽습니다.
 * @details 부트스트랩 범위의 끝(리소스 루트 · 게임이 정해진 뒤)에 서므로 서비스 · 엔진 범위의 생성부터 센다. 프레임 끝의 검사는 엔진 루프가
 *          `onFrameEnd()` 로 부른다(프레임 단계의 일). 크래시 보고 프로세스에서는 리소스 루트가 없어 표를 읽지 않는다. 배포본에서는 감시가 아무 일도 하지 않는다.
 * @warning 예산 표는 초기화에서 읽지 않는다 — 표는 리플렉션으로 읽는데 타입 등록부는 엔진 범위(`ReflectionSubsystem`)에서 선다. 첫 프레임 끝에 한 번 읽는다
 *          (예산 검사도 프레임 끝에서만 돌므로 기동 끝에 읽던 것과 같다). 언리얼도 LLM 추적은 기동 초에 켜고 예산은 설정이 선 뒤에 읽는 것으로 알고 있다.
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Subsystem/ISubsystem.h"

#include "Engine/Profiling/MemoryBudgetMonitor.h"

namespace sw
{
    class CrashHandlerSubsystem;

    /** @class MemoryBudgetSubsystem @brief 메모리 예산 감시 서브시스템입니다. */
    class SW_API MemoryBudgetSubsystem final : public ISubsystem
    {
    public:
        /** @brief 서브시스템 이름입니다(다른 선언의 의존 칸이 이 상수를 가리킨다). */
        static constexpr const utf8* kSubsystemName = "MemoryBudget";

        /** @brief @p crashHandler 는 같은 범위의 것이다(크래시 보고 프로세스인지 · 리소스 루트가 섰는지를 본다). */
        explicit MemoryBudgetSubsystem( const CrashHandlerSubsystem& crashHandler );

        MemoryBudgetSubsystem( const MemoryBudgetSubsystem& )            = delete;
        MemoryBudgetSubsystem& operator=( const MemoryBudgetSubsystem& ) = delete;

        /** @brief 추적 설정을 겁니다. */
        SubsystemInitResult     initializeSubsystem() override;
        void                    shutdownSubsystem() override {}
        const utf8*             getSubsystemName() const override { return kSubsystemName; }
        span<const utf8* const> getSubsystemDependencies() const override;

        /**
         * @brief 프레임 끝의 예산 검사 · 보고입니다. 엔진 루프가 프레임마다 부릅니다.
         * @details 첫 호출에서 예산 표를 읽는다. 틀린 표는 오류를 남기고 예산 없이 간다 — 진단 설정 하나로 기동을 세우지 않는다.
         */
        void onFrameEnd();

    private:
        const CrashHandlerSubsystem& _crashHandler;
        MemoryBudgetMonitor          _monitor;
        bool                         _bBudgetFileRead; ///< 예산 표를 읽었는가(첫 프레임 끝에 한 번)
    };
} // namespace sw
