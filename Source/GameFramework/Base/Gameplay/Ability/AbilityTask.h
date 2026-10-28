/**
 * @file AbilityTask.h
 * @brief 어빌리티가 실행되는 동안 기다리는 작업입니다. 지연과 이벤트 대기가 있습니다(언리얼의 `UAbilityTask` 에 해당).
 */
#pragma once
#include "Core/Delegate/Delegate.h"
#include "Core/Foundation/Macros.h"
#include "Core/Foundation/Types.h"
#include "Core/String/TagID.h"
#include "Core/Time/Countdown.h"

#include "GameFramework/Base/Gameplay/Ability/AbilitySystemEvents.h"
#include "GameFramework/GameFrameworkExports.h"

namespace sw
{
    class GameplayAbility;

    // ------------------------------------------------------------------------------
    // 1) AbilityTask. 어빌리티에 붙어 시간이나 이벤트를 기다린다
    // ------------------------------------------------------------------------------
    /**
     * @class AbilityTask
     * @brief 어빌리티 하나에 붙는 대기 작업입니다. 어빌리티가 끝나면 함께 끝납니다.
     * @details 수명은 다음과 같다. `GameplayAbility::addTask` 가 작업을 붙이고 `onActivate` 를 부른다. 어빌리티가 틱할 때마다 `onTick` 이 불린다.
     *          작업이 스스로 `endTask()` 를 부르거나 어빌리티가 끝나면 `onDestroy( bAbilityEnded )` 가 불리고 작업이 지워진다.
     *          콜백(델리게이트)은 대개 어빌리티의 멤버 함수이다. 예: `SW_DELEGATE_METHOD( Delegate<void()>, &MyAbility::onDelayFinished, this )`.
     *          콜백 안에서 어빌리티를 끝내도 된다.
     */
    class SW_GF_API AbilityTask
    {
        friend class GameplayAbility;

    public:
        AbilityTask();
        virtual ~AbilityTask() = default;

        AbilityTask( const AbilityTask& )            = delete;
        AbilityTask& operator=( const AbilityTask& ) = delete;

        /** @brief 붙은 직후에 한 번 불립니다. */
        virtual void onActivate();
        /** @brief 어빌리티가 실행되는 동안 매 틱 불립니다. */
        virtual void onTick( float32 deltaTime );
        /** @brief 작업이 끝났을 때 불립니다. 어빌리티가 끝나서 함께 끝났으면 @p bAbilityEnded 가 true 입니다. 등록해 둔 구독은 여기서 해제합니다. */
        virtual void onDestroy( bool bAbilityEnded );

        /** @brief 이 작업을 끝냅니다. 이미 끝났으면 아무것도 하지 않습니다. */
        void endTask();
        /** @brief 끝났으면 true 입니다. 아직 지워지지 않고 대기 중일 수 있습니다. */
        bool isFinished() const { return _bFinished == SW_TRUE; }
        /** @brief 이 작업이 붙은 어빌리티입니다. */
        GameplayAbility* getAbility() const { return _pAbility; }

    private:
        /** @brief 어빌리티가 끝나면서 부릅니다. `onDestroy( bAbilityEnded )` 를 한 번 부릅니다. */
        void finish( bool bAbilityEnded );

        GameplayAbility*       _pAbility;
        uint8                  _bFinished : 1;
        [[maybe_unused]] uint8 _reserved  : 7;
    };
} // namespace sw

namespace sw
{
    // ------------------------------------------------------------------------------
    // 2) 기본 작업 두 가지
    // ------------------------------------------------------------------------------
    /** @brief 정한 시간 뒤에 콜백을 한 번 부르고 끝납니다(언리얼의 `UAbilityTask_WaitDelay` 에 해당). 시간은 컴포넌트의 시간입니다. */
    class SW_GF_API AbilityTaskWaitDelay final : public AbilityTask
    {
    public:
        AbilityTaskWaitDelay( float32 seconds, const Delegate<void()>& onFinished );

        void onActivate() override;
        void onTick( float32 deltaTime ) override;

        /** @brief 콜백까지 남은 시간입니다. */
        float32 getRemainingTime() const { return _delay.getRemaining(); }

    private:
        Delegate<void()> _onFinished;
        Countdown        _delay;
    };
} // namespace sw

namespace sw
{
    /**
     * @brief 주인에게 정한 태그나 그 하위 태그의 게임플레이 이벤트가 오면 콜백을 부릅니다(언리얼의 `UAbilityTask_WaitGameplayEvent` 에 해당).
     * @details 작업이 붙을 때 구독을 등록하고(`AbilitySystemComponent::registerGameplayEvent`), 끝날 때 해제한다.
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
        /** @brief 컴포넌트가 이벤트를 넘겨 주는 함수입니다. */
        void handleEvent( const GameplayEventData& payload );

        Delegate<void( const GameplayEventData& )> _onEvent;
        TagID                                      _eventTag;
        Subscription                               _subscription; ///< 주인 컴포넌트의 이벤트 구독. 컴포넌트가 먼저 사라지면 무효가 된다
        uint32                                     _receivedCount;
        uint8                                      _bOnlyTriggerOnce : 1;
        [[maybe_unused]] uint8                     _reserved         : 7;
    };
} // namespace sw
