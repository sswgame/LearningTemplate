#include "pch.h"

#include "GameFramework/Runtime/Gameplay/Gimmick/Movement/LaunchPadComponent.h"

#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectEvents.h"
#include "Engine/Physics/Component/CharacterController2DComponent.h"
#include "Engine/Physics/Component/CharacterControllerComponent.h"
#include "Engine/Physics/Component/RigidBody2DComponent.h"
#include "Engine/Physics/Component/RigidBodyComponent.h"

namespace sw
{
    LaunchPadComponent::LaunchPadComponent()
        : _launchVelocity{ 0.0f, 12.0f, 0.0f }
        , _requiredTags{}
        , _launchCount{ 0 }
    {
    }

    void LaunchPadComponent::onRegister( GameObjectManager& manager )
    {
        Component::onRegister( manager );
        _overlapSubscription = GameObjectEvents::listen<&LaunchPadComponent::handleOverlapBegin>( *this, &GameObjectEvents::_overlapBegin );
    }

    void LaunchPadComponent::handleOverlapBegin( const OverlapInfo& overlap )
    {
        const bool bAccepted = overlap._pOther != nullptr && overlap._bOtherTrigger == SW_FALSE &&
                               ( _requiredTags.getTagCount() == 0 || overlap._pOther->getTags().hasAllTags( _requiredTags ) );
        if ( bAccepted )
            launch( *overlap._pOther );
    }

    void LaunchPadComponent::launch( GameObject& target )
    {
        // 받는 쪽은 쏜 대상 하나라 이벤트를 거치지 않고 그 몸을 직접 부른다(언리얼 LaunchCharacter · AddImpulse(bVelChange) 자리). 쏜 몸만 센다.
        // 캐릭터 컨트롤러는 쏜 속도를 그대로 받는다. 수직 속도는 덮어쓰고, 수평 속도는 다시 디딜 때까지 유지한다.
        CharacterControllerComponent* pController = target.getComponent<CharacterControllerComponent>();
        if ( pController != nullptr )
        {
            ++_launchCount;
            pController->launch( _launchVelocity );
            return;
        }
        CharacterController2DComponent* pController2D = target.getComponent<CharacterController2DComponent>();
        if ( pController2D != nullptr )
        {
            ++_launchCount;
            pController2D->launch( float2{ _launchVelocity._x, _launchVelocity._y } );
            return;
        }
        // 동적 강체는 질량과 상관없이 같은 속도로 쏜다(속도 변경 — 언리얼 AddImpulse 의 bVelChange). 정적 · 키네마틱은 움직이지 않는다.
        RigidBodyComponent* pBody = target.getComponent<RigidBodyComponent>();
        if ( pBody != nullptr && pBody->getBodyType() == PhysicsBodyType::Dynamic )
        {
            ++_launchCount;
            pBody->setLinearVelocity( _launchVelocity );
            pBody->wake();
            return;
        }
        RigidBody2DComponent* pBody2D = target.getComponent<RigidBody2DComponent>();
        if ( pBody2D != nullptr && pBody2D->getBodyType() == PhysicsBodyType::Dynamic )
        {
            ++_launchCount;
            pBody2D->setLinearVelocity( float2{ _launchVelocity._x, _launchVelocity._y } );
            pBody2D->wake();
        }
    }
} // namespace sw
