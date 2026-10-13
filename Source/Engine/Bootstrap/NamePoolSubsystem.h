/**
 * @file Engine/Bootstrap/NamePoolSubsystem.h
 * @brief 이름 풀(`HashedStringPool`)을 세우고 내립니다. 부트스트랩 범위에서 진단 도구 다음에 섭니다.
 * @details 풀은 hashed_string 을 든 객체가 **모두** 사라진 뒤에 내려야 한다 — 그래서 종료가 아니라 소멸자에서 내린다(소멸은 모든 종료 뒤이고,
 *          같은 범위에서 이것에 기대는 것 · 위 범위의 것이 먼저 소멸한다).
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Subsystem/ISubsystem.h"

namespace sw
{
    /** @class NamePoolSubsystem @brief 이름 풀 서브시스템입니다. */
    class SW_API NamePoolSubsystem final : public ISubsystem
    {
    public:
        /** @brief 서브시스템 이름입니다(다른 선언의 의존 칸이 이 상수를 가리킨다). */
        static constexpr const utf8* kSubsystemName = "NamePool";

        NamePoolSubsystem();
        /** @brief 세웠으면 풀을 내립니다. */
        ~NamePoolSubsystem() override;

        NamePoolSubsystem( const NamePoolSubsystem& )            = delete;
        NamePoolSubsystem& operator=( const NamePoolSubsystem& ) = delete;

        SubsystemInitResult     initializeSubsystem() override;
        void                    shutdownSubsystem() override {}
        const utf8*             getSubsystemName() const override { return kSubsystemName; }
        span<const utf8* const> getSubsystemDependencies() const override;

    private:
        bool _bInitialized; ///< 풀을 세웠다(= 소멸자가 내릴 것이 있다)
    };
} // namespace sw
