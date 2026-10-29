/**
 * @file Engine/EngineFrameHookList.h
 * @brief 엔진 루프의 프레임 경계에 붙은 훅 묶음(`EngineFrameHooks`)의 목록입니다 — 붙이기 · 시점마다 부르기 · 종료 코드 모으기를 맡습니다.
 * @details 엔진 루프(`Engine`)가 값으로 들고 프레임 시점마다 부른다. 붙이는 쪽(App · 서버 · 개발 도구 · 텔레메트리)은 `Engine::getFrameHooks().add` 로 붙인다.
 *          서브시스템이 아닌 까닭: 크래시 보고 프로세스는 서비스 범위를 세우지 않고 기동을 끝내지만 호스트는 그 뒤에도 훅을 붙인다 — 엔진과 수명이 같다.
 *          공용 멀티캐스트라 훅이 순회 중에 자기 구독을 놓아도 그 항목은 순회가 끝난 뒤 빠진다(언리얼 `FCoreDelegates::OnBeginFrame` 과 같은 자리).
 *          스레드: 게임 스레드에서만 부른다.
 */
#pragma once
#include "Core/Delegate/Delegate.h"
#include "Core/Delegate/Subscription.h"
#include "Core/Foundation/Macros.h"
#include "Core/Foundation/Types.h"

#include "Engine/EngineFrameHooks.h"

namespace sw
{
    /**
     * @class EngineFrameHookList
     * @brief 프레임 관찰자 목록입니다(대상은 소유하지 않는다). 프레임 앞(입력 전) · 씬 틱 뒤(UI 앞) · 프레임 뒤(렌더 제출 뒤) · 창 닫힘에서 붙인 순서대로 부릅니다.
     */
    class SW_API EngineFrameHookList
    {
    public:
        EngineFrameHookList();
        ~EngineFrameHookList();

        EngineFrameHookList( const EngineFrameHookList& )            = delete;
        EngineFrameHookList& operator=( const EngineFrameHookList& ) = delete;

        /**
         * @brief 훅 묶음을 붙입니다. 붙인 순서대로 부릅니다. 돌려받은 구독이 사라지면 떨어집니다.
         * @details 훅의 대상과 구독은 붙인 쪽이 쥐고, 구독을 대상보다 먼저 놓습니다. 칸이 모두 비었으면 오류를 남기고 빈 구독을 돌려줍니다.
         *          구독 하나가 등록 하나라 같은 묶음을 두 번 붙이면 두 번 불립니다. 엔진 종료(`Engine::shutdown`)는 남은 구독을 모두 끊습니다.
         */
        [[nodiscard]] Subscription add( const EngineFrameHooks& hooks );
        /** @brief 남은 구독을 모두 무효화하고 저장소를 놓습니다(엔진 종료가 서비스보다 먼저 부른다 — 훅이 서비스를 빌려 쓴다). */
        void clear();

        /** @brief 붙은 훅이 있으면 true 입니다. 부르는 쪽은 없으면 문맥을 만들지 않는다. */
        bool isBound() const { return _listHook.isBound(); }

        /** @brief 프레임 앞 훅(`_onFrameBegin`)을 모두 부릅니다. */
        void notifyFrameBegin( const EngineFrameContext& context );
        /** @brief 씬 틱 뒤 훅(`_onAfterSceneTick`)을 모두 부릅니다. */
        void notifyAfterSceneTick( const EngineFrameContext& context );
        /** @brief 프레임 뒤 훅(`_onFrameEnd`)을 **모두** 부르고(멈추지 않는다) 처음으로 낸 종료 코드를 돌려줍니다. 아무도 내지 않았으면 `kEngineFrameRunning` 입니다. */
        [[nodiscard]] int32 notifyFrameEnd( const EngineFrameContext& context );
        /** @brief 창 닫힘 훅(`_onWindowClosed`)을 붙인 순서대로 부르다 처음으로 코드를 낸 훅에서 멈추고 그 코드를 돌려줍니다. 아무도 내지 않았으면 `kEngineFrameRunning` 입니다. */
        [[nodiscard]] int32 notifyWindowClosed( const EngineFrameContext& context );

    private:
        /** @brief 훅 묶음의 칸 하나(프레임 시점)입니다. 묶음 하나가 멀티캐스트 항목 하나라 구독 하나로 떨어진다. */
        enum class FramePoint : uint8
        {
            FrameBegin,
            AfterSceneTick,
            FrameEnd,
            WindowClosed,
        };
        using FrameHookDelegate = Delegate<int32( FramePoint, const EngineFrameContext& )>;
        using FrameHookList     = MulticastDelegate<int32( FramePoint, const EngineFrameContext& )>;

        FrameHookList _listHook; ///< 붙인 훅 묶음(붙인 순서)
    };
} // namespace sw
