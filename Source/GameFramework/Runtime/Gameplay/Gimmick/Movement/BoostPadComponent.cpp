#include "pch.h"

#include "GameFramework/Runtime/Gameplay/Gimmick/Movement/BoostPadComponent.h"

#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectEvents.h"

#include "GameFramework/Runtime/Gameplay/Vehicle/KinematicVehicleComponent.h"

namespace sw
{
    namespace
    {
        struct BoostPadComponentInternal
        {
            static bool canAccept( const OverlapInfo& overlap, const TagContainer& requiredTags )
            {
                return overlap._pOther != nullptr && overlap._bOtherTrigger == SW_FALSE &&
                       ( requiredTags.getTagCount() == 0 || overlap._pOther->getTags().hasAllTags( requiredTags ) );
            }
        };
    } // namespace
} // namespace sw

namespace sw
{
    BoostPadComponent::BoostPadComponent()
        : _duration{ 1.0f }
        , _requiredTags{}
        , _boostCount{ 0 }
    {
    }

    void BoostPadComponent::onRegister( GameObjectManager& manager )
    {
        Component::onRegister( manager );
        _overlapSubscription = GameObjectEvents::listen<&BoostPadComponent::handleOverlapBegin>( *this, &GameObjectEvents::_overlapBegin );
    }

    void BoostPadComponent::handleOverlapBegin( const OverlapInfo& overlap )
    {
        if ( BoostPadComponentInternal::canAccept( overlap, _requiredTags ) )
            boost( *overlap._pOther );
    }

    void BoostPadComponent::boost( GameObject& target )
    {
        // 받는 쪽은 밟은 탈것 하나라 이벤트를 거치지 않고 그 모터를 직접 부른다. 세기(최고 속도 보너스 · 밀기)는 탈것 튜닝의 부스트 모듈이 정한다(마리오 카트 대시 판).
        KinematicVehicleComponent* pVehicle = target.getComponent<KinematicVehicleComponent>();
        if ( pVehicle == nullptr )
            return;
        ++_boostCount;
        pVehicle->getMotor().startBoost( _duration );
    }
} // namespace sw
