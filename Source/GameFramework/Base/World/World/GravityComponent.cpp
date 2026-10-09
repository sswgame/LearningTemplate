#include "pch.h"

#include "GameFramework/Base/World/World/GravityComponent.h"

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

        // 땅 높이(`_groundY`) · 속도는 **월드** 값이다 — 월드 자리로 읽고 쓴다. 주의: 로컬로 읽고 쓰면 움직이는 발판 같은 부모 아래에서
        // 부모의 높이만큼 떠 있거나 파묻혀 멈춘다.
        float3 pos = pSceneComp->getWorldPosition();

        // **바닥 위로 올라가 있으면 다시 떨어진다.** 땅을 "붙잡은 기억" 이 아니라 **지금 위치**로 판정한다 —
        // 점프든 리프트든 순간이동이든 무엇이 올려 놓아도 중력이 다시 걸린다.
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
        pSceneComp->setWorldPosition( pos );
    }
} // namespace sw
