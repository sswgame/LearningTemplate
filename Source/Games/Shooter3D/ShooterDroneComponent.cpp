#include "pch.h"

#include "Games/Shooter3D/ShooterDroneComponent.h"

#include "Core/Math/MathUtil.h"

#include "Engine/Graphics/Material/MaterialInstance.h"
#include "Engine/Object/Component/3D/MeshComponent.h"
#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManager.h"

#include "GameFramework/UI/HealthBarComponent.h"

#include "Games/Shooter3D/ShooterDirectorComponent.h"
#include "Games/Shooter3D/ShooterPlayerComponent.h"

namespace sw
{
    ShooterDroneComponent::ShooterDroneComponent()
        : _director{}
        , _radius{ 0.5f }
        , _reach{ 1.4f }
        , _damage{ 8.0f }
        , _attackInterval{ 0.8f }
        , _hoverHeight{ 1.4f }
        , _bobAmplitude{ 0.25f }
        , _modelYawOffset{ -MathUtil::HalfPi }
        , _position{ 0.0f, 0.0f, 0.0f }
        , _health{ 30.0f }
        , _maxHealth{ 30.0f }
        , _speed{ 3.0f }
        , _attackCooldown{ 0.0f }
        , _flashTimer{ 0.0f }
        , _bobPhase{ 0.0f }
        , _bLaunched{ SW_FALSE }
        , _bFlashing{ SW_FALSE }
        , _reserved{ 0 }
    {
        setCanEverTick( true );
    }

    void ShooterDroneComponent::launch( GameObjectHandle director, const float3& position, float32 health, float32 speed, float32 bobPhase )
    {
        _director             = director;
        _position             = position;
        _health               = health;
        _maxHealth            = health;
        _speed                = speed;
        _bobPhase             = bobPhase;
        _attackCooldown       = 0.0f;
        _flashTimer           = 0.0f;
        _bLaunched            = SW_TRUE;
        GameObject*    pOwner = getOwner();
        MeshComponent* pMesh  = pOwner != nullptr ? pOwner->getComponent<MeshComponent>() : nullptr;
        if ( pMesh != nullptr )
            pMesh->setLocalPosition( _position );
    }

    void ShooterDroneComponent::applyDamage( float32 amount )
    {
        _health -= amount;
        _flashTimer = 0.08f;
    }

    void ShooterDroneComponent::onTick( float32 deltaTime )
    {
        Component::onTick( deltaTime );
        if ( _bLaunched == SW_FALSE || isDead() || deltaTime <= 0.0f )
            return;
        GameObject*                     pOwner    = getOwner();
        GameObjectManager*              pManager  = pOwner != nullptr ? pOwner->getManager() : nullptr;
        const ShooterDirectorComponent* pDirector = pManager != nullptr ? ShooterDirectorComponent::resolveDirector( *pManager, _director ) : nullptr;
        if ( pDirector == nullptr )
            return;
        const float32 step = MathUtil::min( deltaTime, 0.1f );

        // 다가온다 — 이웃 드론과 떨어지고 상자를 돌아간다(밀려난다). 이웃은 디렉터가 적은 이번 프레임의 자리다(같은 그룹이라 첨자 대신 포인터로).
        const float3                    target   = pDirector->getPlayerEye() + float3{ 0.0f, -0.3f, 0.0f };
        const float3                    toTarget = target - _position;
        const float32                   distance = toTarget.getLength();
        float3                          steer    = distance > 1.0e-4f ? toTarget * ( 1.0f / distance ) : float3{ 0.0f };
        const GameObjectHandle          self     = pOwner->getHandle();
        const vector<ShooterDroneView>& listView = pDirector->getDroneViews();
        const ShooterDroneView*         pView    = listView.data();
        for ( size_t viewIndex = 0; viewIndex < listView.size(); ++viewIndex )
        {
            const float3  away       = _position - pView[viewIndex]._position;
            const float32 awayLength = away.getLength();
            if ( pView[viewIndex]._object != self && awayLength < 1.4f && awayLength > 1.0e-4f )
                steer = steer + away * ( 0.8f / awayLength );
        }
        _bobPhase += step * 3.0f;
        if ( distance > _reach * 0.8f )
        {
            const float3 flat{ steer._x, 0.0f, steer._z };
            float3       next     = _position + flat * ( _speed * step );
            next._y               = _hoverHeight + MathUtil::sin( _bobPhase ) * _bobAmplitude;
            const float3 resolved = ShooterArenaMath::resolveCircle( pDirector->getBoxes(), float3{ next._x, 0.0f, next._z }, _radius );
            _position             = float3{ resolved._x, next._y, resolved._z };
        }
        _attackCooldown -= step;
        if ( distance < _reach && _attackCooldown <= 0.0f )
        {
            _attackCooldown = _attackInterval;
            requestPlayerDamage( pDirector->getPlayer() );
        }

        _flashTimer -= step;
        MeshComponent* pMesh = pOwner->getComponent<MeshComponent>();
        if ( pMesh != nullptr )
        {
            // 판의 앞(+X)이 플레이어를 보게 돈다.
            pMesh->setLocalPosition( _position );
            pMesh->setLocalRotation( float3{ 0.0f, MathUtil::atan2( toTarget._x, toTarget._z ) + _modelYawOffset, 0.0f } );
            const uint8                         bFlashing = _flashTimer > 0.0f ? SW_TRUE : SW_FALSE;
            const shared_ptr<MaterialInstance>& look      = pDirector->getDroneLook( bFlashing == SW_TRUE );
            if ( bFlashing != _bFlashing && look != nullptr )
            {
                _bFlashing = bFlashing;
                pMesh->setMaterialInstance( look );
            }
        }
        HealthBarComponent* pBar = pOwner->getComponent<HealthBarComponent>();
        if ( pBar != nullptr )
            pBar->setTargetRatio( MathUtil::max( 0.0f, _health / MathUtil::max( 1.0f, _maxHealth ) ) );
    }

    void ShooterDroneComponent::requestPlayerDamage( GameObjectHandle player ) const
    {
        GameObject*        pOwner   = getOwner();
        GameObjectManager* pManager = pOwner != nullptr ? pOwner->getManager() : nullptr;
        if ( pManager == nullptr )
            return;
        // 플레이어는 다른 워커가 틱하고 있을 수 있다 — 틱 뒤(게임 스레드)에 깎는다.
        const float32 damage = _damage;
        pManager->executeOrDeferPostTick( [pManager, player, damage]()
        {
            GameObject*             pPlayer    = pManager->resolveGameObject( player );
            ShooterPlayerComponent* pComponent = pPlayer != nullptr ? pPlayer->getComponent<ShooterPlayerComponent>() : nullptr;
            if ( pComponent != nullptr )
                pComponent->takeDamage( damage );
        } );
    }
} // namespace sw
