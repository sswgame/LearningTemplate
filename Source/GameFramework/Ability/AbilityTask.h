/**
 * @file AbilityTask.h
 * @brief 어빌리티가 도는 동안 기다리는 일(지연 · 이벤트 대기)입니다(언리얼 `UAbilityTask`).
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"
#include "Core/Delegate/Delegate.h"
#include "Core/String/TagID.h"

#include "GameFramework/Ability/AbilitySystemEvents.h"
#include "GameFramework/GameFrameworkExports.h"

namespace sw
{
    class GameplayAbility;

    // ------------------------------------------------------------------------------
    // 1) AbilityTask — 어빌리티에 붙어 시간 · 이벤트를 기다린다
    // ------------------------------------------------------------------------------
    /**
     * @class AbilityTask
     * @brief 어빌리티 하나에 붙는 대기 작업입니다. 어빌리티가 끝나면 함께 끝납니다.
     * @details 수명: `GameplayAbility::addTask` 가 붙이고 `onActivate` 를 부릅니다 → 어빌리티 틱마다 `onTick` → 스스로 `endTask()` 하거나
     *          어빌리티가 끝나면 `onDestroy( bAbilityEnded )` 를 받고 지워집니다. 콜백(델리게이트)은 대개 어빌리티의 멤버 함수입니다 —
     *          `SW_DELEGATE_METHOD( Delegate<void()>, &MyAbility::onDelayFinished, this )`. 콜백 안에서 어빌리티를 끝내도 됩니다.
     */
    class SW_GF_API AbilityTask
    {
        friend class GameplayAbility;

    public:
        AbilityTask();
        virtual ~AbilityTask() = default;

        AbilityTask( const AbilityTask& )            = delete;
        AbilityTask& operator=( const AbilityTask& ) = delete;

        /** @brief 붙은 직후 한 번 불립니다. */
        virtual void onActivate();
        /** @brief 어빌리티가 도는 동안 매 틱 불립니다. */
        virtual void onTick( float32 deltaTime );
        /** @brief 끝났습니다. 어빌리티가 끝나서 끝났으면 @p bAbilityEnded 입니다. 걸어 둔 구독을 여기서 뗍니다. */
        virtual void onDestroy( bool bAbilityEnded );

        /** @brief 이 작업을 끝냅니다. 이미 끝났으면 아무것도 하지 않습니다. */
        void endTask();
        /** @brief 끝났으면 true 입니다(지워지기를 기다리는 중일 수 있습니다). */
        bool isFinished() const { return _bFinished == SW_TRUE; }
        /** @brief 이 작업이 붙은 어빌리티입니다. */
        GameplayAbility* getAbility() const { return _pAbility; }

    private:
        /** @brief 어빌리티가 끝내며 부릅니다 — `onDestroy( bAbilityEnded )` 를 한 번 부릅니다. */
        void finish( bool bAbilityEnded );

        GameplayAbility*       _pAbility;
        uint8                  _bFinished : 1;
        [[maybe_unused]] uint8 _reserved  : 7;
    };
} // namespace sw

namespace sw
{
    // ------------------------------------------------------------------------------
    // 2) 기본 작업 둘
    // ------------------------------------------------------------------------------
    /** @brief 정한 시간 뒤에 콜백을 한 번 부르고 끝납니다(언리얼 `UAbilityTask_WaitDelay`). 시간은 컴포넌트 시간입니다. */
    class SW_GF_API AbilityTaskWaitDelay final : public AbilityTask
    {
    public:
        AbilityTaskWaitDelay( float32 seconds, const Delegate<void()>& onFinished );

        void onActivate() override;
        void onTick( float32 deltaTime ) override;

        /** @brief 콜백까지 남은 시간입니다. */
        float32 getRemainingTime() const { return _remainingTime; }

    private:
        Delegate<void()> _onFinished;
        float32          _remainingTime;
    };
} // namespace sw

namespace sw
{
    /**
     * @brief 주인에게 정한 태그(또는 그 하위)의 게임플레이 이벤트가 오면 콜백을 부릅니다(언리얼 `UAbilityTask_WaitGameplayEvent`).
     * @details 구독은 붙을 때 걸고(`AbilitySystemComponent::registerGameplayEvent`) 끝날 때 뗍니다.
     */
    class SW_GF_API AbilityTaskWaitGameplayEvent final : public AbilityTask
    {
    public:
        AbilityTaskWaitGameplayEvent( TagID eventTag, const Delegate<void( const GameplayEventData& )>& onEvent, bool bOnlyTriggerOnce );

        void onActivate() override;
        void onDestroy( bool bAbilityEnded ) override;

        /** @brief 받은 이벤트 수입니다. */
        uint32 getReceivedCount() const { return _receivedCount; }

    private:
        /** @brief 컴포넌트가 이벤트를 넘기는 자리입니다. */
        void handleEvent( const GameplayEventData& payload );

        Delegate<void( const GameplayEventData& )> _onEvent;
        TagID                                      _eventTag;
        DelegateHandle                             _subscription;
        uint32                                     _receivedCount;
        uint8                                      _bOnlyTriggerOnce : 1;
        [[maybe_unused]] uint8                     _reserved         : 7;
    };
} // namespace sw
