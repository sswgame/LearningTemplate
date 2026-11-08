/**
 * @file Engine/Profiling/ProfilerBackendSubsystem.h
 * @brief 외부 프로파일러 출력(`ProfilerBackend`)을 서비스 범위에서 내립니다.
 * @details 서비스 범위의 서브시스템이라 엔진 범위(기동 단계)가 모두 사라진 뒤에 할당 관찰을 떼고 출력을 비운다. 켜는 쪽은 개발 도구다 —
 *          실행 인자(`-gv_tracy`)는 `DevTools/Profiling/ProfilerLaunchSwitches` 가 기동 단계에, 에디터의 "Tracy 열기" 는 실행 중에 `TracyProfilerBackend::start`(개발 도구)를 부른다.
 */
#pragma once
#include "Core/Foundation/Macros.h"
#include "Core/Subsystem/ISubsystem.h"

namespace sw
{
    /** @class ProfilerBackendSubsystem @brief 외부 프로파일러 출력 서브시스템입니다. */
    class SW_API ProfilerBackendSubsystem final : public ISubsystem
    {
    public:
        /** @brief 서브시스템 이름입니다(다른 선언의 의존 칸이 이 상수를 가리킨다). */
        static constexpr const utf8* kSubsystemName = "ProfilerBackend";

        ProfilerBackendSubsystem()                                             = default;
        ProfilerBackendSubsystem( const ProfilerBackendSubsystem& )            = delete;
        ProfilerBackendSubsystem& operator=( const ProfilerBackendSubsystem& ) = delete;

        /** @brief 세울 것이 없다 — 켜는 쪽은 개발 도구다(머리말). */
        SubsystemInitResult initializeSubsystem() override { return SubsystemInitResult::Succeeded; }
        /** @brief 할당 관찰을 떼고 출력을 비웁니다. 출력 객체(Tracy)는 프로세스 수명이라 남은 구간을 닫을 수 있다. */
        void                    shutdownSubsystem() override;
        const utf8*             getSubsystemName() const override { return kSubsystemName; }
        span<const utf8* const> getSubsystemDependencies() const override;
    };
} // namespace sw
