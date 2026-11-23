/**
 * @file GameObjectEvents.h
 * @brief 오브젝트 하나에 오는 사건(겹침 · 접촉 · 맞음 · 애니메이션 알림)을 듣는 쪽이 구독하는 멀티캐스트 델리게이트 묶음입니다.
 * @details 듣는 컴포넌트가 등록 자리(`onRegister`)에서 `GameObjectEvents::listen` 으로 구독하고 `Subscription` 을 멤버로 든다 — 컴포넌트가 사라지면 풀린다.
 *          묶음은 첫 구독 때 오브젝트에 한 번 할당한다. 듣는 쪽이 없는 오브젝트는 포인터 하나(8 B)뿐이고 사건은 그 포인터만 보고 끝난다.
 *          언리얼 액터 델리게이트(`OnActorBeginOverlap` · `OnActorHit` · `OnTakeAnyDamage`)와 같은 자리다 — 유니티처럼 모든 컴포넌트에 가상 호출을 훑지 않는다.
 *          부르는 차례는 구독 차례다. 사건 안에서 구독 · 해지 · 스폰 · 파괴를 해도 된다(`MulticastDelegate::broadcast` — 해지는 즉시 무효, 새 구독은 다음 사건부터).
 */
#pragma once
#include "Core/Delegate/Delegate.h"
#include "Core/Delegate/Subscription.h"
#include "Core/Foundation/Macros.h"
#include "Core/Foundation/StdHeaders.h"

#include "Engine/Object/Component/Component.h"
#include "Engine/Object/Component/TagSystem.h"
#include "Engine/Object/GameObject/GameObject.h"

namespace sw
{
    /**
     * @struct GameObjectEvents
     * @brief 오브젝트의 사건 델리게이트입니다. 칸 하나가 사건 하나이고, 내는 곳은 칸 설명에 적힌 시스템 하나다.
     */
    struct GameObjectEvents
    {
        /**
         * @brief 이 오브젝트의 콜라이더가 다른 오브젝트의 콜라이더와 겹치기 시작했다(언리얼 `OnActorBeginOverlap`, 유니티 `OnTriggerEnter`). `ScenePhysics` 가 낸다.
         * @details 틱과 트랜스폼 적용 뒤에 게임 스레드에서 온다. 한 프레임 안에서는 이탈이 먼저, 진입이 다음이다. 트리거 안에서 생긴 것은 다음 물리 스텝에 진입하고,
         *          파괴 · 비활성은 이탈을 받는다(docs/agents/Gameplay.md "트리거 · 물리"). 유지(Stay)는 `ScenePhysics::registerContactStayListener` 다.
         */
        MulticastDelegate<void( const OverlapInfo& )> _overlapBegin;
        /** @brief 겹침이 끝났다(떨어짐, 꺼짐, 사라짐). 상대가 이미 사라졌으면 `OverlapInfo::_pOther` 는 nullptr 다. `ScenePhysics` 가 낸다. */
        MulticastDelegate<void( const OverlapInfo& )> _overlapEnd;
        /**
         * @brief 이 오브젝트의 강체가 다른 바디와 막는 접촉을 시작했다(언리얼 `OnActorHit`, 유니티 `OnCollisionEnter`). `ScenePhysics` 가 낸다.
         * @details 물리 스텝이 끝나고 보간한 위치를 기록한 뒤 게임 스레드에서 온다. 한 프레임에 스텝이 여러 개이면 스텝 순서대로 온다.
         */
        MulticastDelegate<void( const CollisionInfo& )> _collisionBegin;
        /** @brief 막는 접촉이 끝났다(떨어짐, 바디 사라짐). `ScenePhysics` 가 낸다. */
        MulticastDelegate<void( const CollisionInfo& )> _collisionEnd;
        /**
         * @brief 이 오브젝트가 맞았다(언리얼 `OnTakeAnyDamage`). 근접 판정이나 무기 레이캐스트가 `CharacterRayHit::deliverHit` 로 낸다.
         * @details 틱 밖이면 바로, 틱 중이면 틱이 끝난 뒤로 미뤄서 온다.
         */
        MulticastDelegate<void( const HitInfo& )> _hitReceived;
        /** @brief 이 오브젝트의 애니메이션이 게임플레이 이벤트 알림을 울렸다(알림 테이블의 `AbilityEvent`). 애니메이션 알림 처리기가 낸다. */
        MulticastDelegate<void( const AnimationNotifyInfo& )> _animationNotify;
        /** @brief 이 오브젝트의 태그가 바뀌었다(인자는 바뀐 뒤의 태그). `TagComponent` 가 게임 스레드의 틱 밖에서 낸다. */
        MulticastDelegate<void( const TagContainer& )> _tagsChanged;

        /**
         * @brief @p listener 의 멤버 함수 @p Method 를 소유 오브젝트의 사건 @p pEvent 에 겁니다. 돌려받은 구독을 @p listener 의 멤버로 듭니다.
         * @details 켜지지 않았거나 파괴 대기 중인 컴포넌트에는 부르지 않는다(가상 디스패치 때의 규칙). 소유 오브젝트가 없으면 결함이다.
         *          예: `_hitSubscription = GameObjectEvents::listen<&MyComponent::handleHit>( *this, &GameObjectEvents::_hitReceived );`
         */
        template <auto Method, typename TComponent, typename TInfo>
        [[nodiscard]] static Subscription listen( TComponent& listener, MulticastDelegate<void( const TInfo& )> GameObjectEvents::* pEvent )
        {
            static_assert( std::is_base_of_v<Component, TComponent>, "the listener must be a component" );
            GameObject* const pOwner = listener.getOwner();
            SW_ASSERT( pOwner != nullptr );
            TComponent* const pListener = &listener;
            return ( pOwner->getOrCreateEvents().*pEvent ).add( [pListener]( const TInfo& info )
            {
                const bool bCannotReceive = pListener->isPendingDestroy() || pListener->isSelfActive() == false;
                if ( bCannotReceive )
                    return;
                std::invoke( Method, pListener, info );
            } );
        }

        /** @brief @p target 의 사건 @p pEvent 를 냅니다. 듣는 쪽이 없으면(묶음이 없으면) 아무것도 하지 않습니다. 대상의 활성 · 파괴 대기는 내는 쪽이 봅니다. */
        template <typename TInfo>
        static void broadcast( const GameObject& target, MulticastDelegate<void( const TInfo& )> GameObjectEvents::* pEvent, const TInfo& info )
        {
            GameObjectEvents* const pEvents = target.findEvents();
            if ( pEvents != nullptr )
                ( pEvents->*pEvent ).broadcast( info );
        }
    };
} // namespace sw
