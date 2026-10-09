#include "pch.h"

#include "GameFramework/Base/Actor/Combat/HealthSourceComponent.h"

#include "Core/Math/MathUtil.h"

#include "Engine/Object/GameObject/GameObject.h"

#include "GameFramework/Base/Actor/Combat/HealthListenerComponent.h"

namespace sw
{
    namespace
    {
        struct HealthSourceComponentInternal
        {
            /** @brief 읽기의 비율 0..1 입니다. 최대가 0 이하이거나 체력이 없는 주인이면 0 입니다. */
            static float32 computeRatio( const HealthReading& reading )
            {
                if ( reading._bHasHealth == SW_FALSE || reading._maxHealth <= 0.0f )
                    return 0.0f;
                return MathUtil::saturate( reading._health / reading._maxHealth );
            }
        };
    } // namespace
} // namespace sw

namespace sw
{
    HealthSourceComponent::HealthSourceComponent() = default;

    HealthSourceComponent::~HealthSourceComponent() = default;

    float32 HealthSourceComponent::getHealthRatio() const
    {
        return HealthSourceComponentInternal::computeRatio( getHealthReading() );
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
        event._ratio = HealthSourceComponentInternal::computeRatio( reading );
        if ( bReset )
            event._kind = HealthChangeKind::Reset;
        else if ( reading._bDead == SW_TRUE )
            event._kind = HealthChangeKind::Died;
        else
            event._kind = HealthChangeKind::Changed;
        HealthListenerComponent::broadcast( *pOwner, event );
    }
} // namespace sw
