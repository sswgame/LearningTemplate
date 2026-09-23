#include "pch.h"

#include "GameFramework/Base/GravityComponent.h"

#include "Engine/Object/Component/TagSystem.h"

namespace sw
{
    GravityComponent::GravityComponent()
        : _gravity{ 0.0f }
        , _velocityY{ 0.0f }
        , _groundY{ 0.0f }
        , _bIsGrounded{ false }
    {
    }

    void GravityComponent::onBeginPlay()
    {
        Component::onBeginPlay();
        setTickGroup( TickGroup::DuringPhysics );

        GameObject* pOwner = getOwner();
        if ( pOwner != nullptr )
            pOwner->addTag( "Physics"_tag );
    }

    void GravityComponent::onEndPlay()
    {
        Component::onEndPlay();
    }

    void GravityComponent::onTick( float32 deltaTime )
    {
        Component::onTick( deltaTime );

        GameObject* pOwner = getOwner();
        if ( pOwner == nullptr )
            return;

        SceneComponent* pSceneComp = pOwner->getPrimarySceneComponent();
        if ( pSceneComp == nullptr )
            return;

        float3 pos = pSceneComp->getLocalPosition();

        // **바닥 위로 올라가 있으면 다시 떨어진다.** 예전에는 한 번 닿으면 `_bIsGrounded` 가
        // 영영 참이었다. 점프든 리프트든 순간이동이든 무엇이 올려 놓아도 중력이 다시는 안
        // 걸렸고, 코드에서 그것을 되돌릴 창구조차 없었다(리플렉션 프로퍼티뿐이었다).
        // 땅을 "붙잡은 기억" 이 아니라 **지금 위치**로 판정한다.
        if ( _bIsGrounded && pos._y > _groundY )
            _bIsGrounded = false;

        if ( _bIsGrounded == false )
        {
            _velocityY += _gravity * deltaTime;
            pos._y += _velocityY * deltaTime;
            if ( pos._y <= _groundY )
            {
                pos._y       = _groundY;
                _velocityY   = 0.0f;
                _bIsGrounded = true;
            }
        }
        pSceneComp->setLocalPosition( pos );
    }
} // namespace sw
