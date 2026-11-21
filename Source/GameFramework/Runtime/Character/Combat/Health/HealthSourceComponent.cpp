#include "pch.h"

#include "GameFramework/Runtime/Character/Combat/Health/HealthSourceComponent.h"

#include "Core/Math/MathUtil.h"

#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectEvents.h"

#include "GameFramework/Runtime/Character/Combat/Health/HealthListenerComponent.h"

namespace sw
{
    namespace
    {
        /** @brief 읽기의 비율 0..1 입니다. 최대가 0 이하이거나 체력이 없는 주인이면 0 입니다. */
        float32 computeRatio( const HealthReading& reading )
        {
            if ( reading._bHasHealth == SW_FALSE || reading._maxHealth <= 0.0f )
                return 0.0f;
            return MathUtil::saturate( reading._health / reading._maxHealth );
        }
    } // namespace
} // namespace sw

namespace sw
{
    HealthSourceComponent::HealthSourceComponent() = default;

    HealthSourceComponent::~HealthSourceComponent() = default;

    float32 HealthSourceComponent::getHealthRatio() const
    {
        return computeRatio( getHealthReading() );
    }

    void HealthSourceComponent::notifyHealthChanged( bool bReset ) const
    {
        const GameObject* pOwner = getOwner();
        if ( pOwner == nullptr )
            return;
        const HealthReading reading = getHealthReading();
        if ( reading._bHasHealth == SW_FALSE )
            return;

        HealthChangedEvent event;
        event._ratio = computeRatio( reading );
        if ( bReset )
            event._kind = HealthChangeKind::Reset;
        else if ( reading._bDead == SW_TRUE )
            event._kind = HealthChangeKind::Died;
        else
            event._kind = HealthChangeKind::Changed;
        HealthListenerComponent::broadcast( *pOwner, event );
    }

    void HealthSourceComponent::deliverFatalHit( const HitInfo& hit )
    {
        // 이미 치명 표시가 달린 맞음이면 다른 컴포넌트도 그 표시로 받았다 — 두 번 보내지 않는다.
        GameObject* pOwner = getOwner();
        if ( pOwner == nullptr || hit._bFatal )
            return;
        HitInfo fatal = hit;
        fatal._bFatal = true;
        // 같은 맞음 사건으로 다시 낸다 — 체력 원천(이 컴포넌트 포함)은 치명 표시가 달린 맞음을 건너뛴다(피해는 이미 깎였다).
        GameObjectEvents::broadcast( *pOwner, &GameObjectEvents::_hitReceived, fatal );
    }
} // namespace sw
