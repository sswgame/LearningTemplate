/**
 * @file Engine/Module/ModuleImagesSubsystem.h
 * @brief 기동 순서의 ModuleImages 자리입니다 — 종료 쪽에서 모듈 DLL 을 내리는 호스트 훅을 부릅니다.
 * @details 씬은 방금 사라졌고 서비스는 아직 살아 있는 구간이 모듈 DLL 을 내리기에 알맞은 유일한 자리다(타입 · 코덱 · 캐시를 떼고 태스크를 기다린다).
 *          엔진은 거기서 무슨 일이 일어나는지 모른다 — 훅(`Engine::setOnScenesReleased`)은 호스트가 건다. 테스트 하네스는 훅 없이 자리만 둔다.
 *          모듈 코드를 드는 낮은 층(비동기 IO 의 완료 콜백 · 오디오 · 입력 · 씬)은 이 이름을 모르므로, 이것이 그들을 역방향 의존으로 적어 그들이 먼저 내려가게 한다.
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Delegate/Delegate.h"
#include "Core/Subsystem/ISubsystem.h"

namespace sw
{
    /**
     * @class ModuleImagesSubsystem
     * @brief 종료에서 모듈 DLL 내리기 훅을 부르는 서브시스템입니다.
     */
    class SW_API ModuleImagesSubsystem final : public ISubsystem
    {
    public:
        /** @brief 서브시스템 이름입니다(다른 선언의 의존 칸이 이 상수를 가리킨다). */
        static constexpr const utf8* kSubsystemName = "ModuleImages";

        /** @brief @p pOnScenesReleased 는 종료가 부를 훅입니다(호스트 소유 — 이것보다 오래 산다). nullptr 이면 부를 것이 없습니다. */
        explicit ModuleImagesSubsystem( const Delegate<void()>* pOnScenesReleased = nullptr );

        SubsystemInitResult     initializeSubsystem() override { return SubsystemInitResult::Succeeded; }
        void                    shutdownSubsystem() override;
        const utf8*             getSubsystemName() const override { return kSubsystemName; }
        span<const utf8* const> getSubsystemDependencies() const override;
        span<const utf8* const> getSubsystemDependents() const override;

    private:
        const Delegate<void()>* _pOnScenesReleased;
    };
} // namespace sw
