/**
 * @file Engine/Profiling/ProfilerBackendSubsystem.h
 * @brief 외부 프로파일러 출력(`-gv_tracy`)을 서비스 범위에서 켜고 끕니다.
 * @details 서비스 범위의 서브시스템이라 엔진 범위(기동 단계)보다 먼저 켜져 기동 단계부터 타임라인에 남고, 엔진 범위가 모두 사라진 뒤에 할당 관찰을 뗀다.
 *          켜지면 엔진 프로파일러(같은 범위의 `FrameProfiler` — 먼저 선다)도 켠다 — 카운터 그래프 · GPU 타임스탬프가 그 경로를 탄다.
 *          실행 중에 켜는 개발 명령은 `ProfilerBackend::startTracy` 를 직접 부른다(그때는 서비스 표가 묶여 있다).
 */
#pragma once
#include "Core/Foundation/Macros.h"
#include "Core/Subsystem/ISubsystem.h"

namespace sw
{
    class FrameProfiler;

    /** @class ProfilerBackendSubsystem @brief 외부 프로파일러 출력 서브시스템입니다. */
    class SW_API ProfilerBackendSubsystem final : public ISubsystem
    {
    public:
        /** @brief 서브시스템 이름입니다(다른 선언의 의존 칸이 이 상수를 가리킨다). */
        static constexpr const utf8* kSubsystemName = "ProfilerBackend";

        /** @brief @p frameProfiler 는 같은 범위의 것이다(이것보다 먼저 서고 나중에 사라진다). */
        explicit ProfilerBackendSubsystem( FrameProfiler& frameProfiler );

        ProfilerBackendSubsystem( const ProfilerBackendSubsystem& )            = delete;
        ProfilerBackendSubsystem& operator=( const ProfilerBackendSubsystem& ) = delete;

        /** @brief `-gv_tracy` 면 출력을 켭니다. 켜지 못해도(빌드에 없음 · 클라이언트 라이브러리 없음) 기동을 세우지 않는다 — 경고는 출력 쪽이 남긴다. */
        SubsystemInitResult initializeSubsystem() override;
        /** @brief 할당 관찰을 떼고 출력을 비웁니다. 출력 객체(Tracy)는 프로세스 수명이라 남은 구간을 닫을 수 있다. */
        void                    shutdownSubsystem() override;
        const utf8*             getSubsystemName() const override { return kSubsystemName; }
        span<const utf8* const> getSubsystemDependencies() const override;

    private:
        FrameProfiler& _frameProfiler;
    };
} // namespace sw
