#include "pch.h"

#include "Games/Shooter3D/ShooterEnemyComponent.h"

#include "Core/Math/MathUtil.h"

#include "Engine/Object/Component/3D/SkeletalAnimatorComponent.h"
#include "Engine/Object/Component/SceneComponent.h"
#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManager.h"

#include "GameFramework/Combat/HealthListenerComponent.h"
#include "GameFramework/Utility/OrientationUtil.h"

#include "Games/Shooter3D/ShooterDirectorComponent.h"
#include "Games/Shooter3D/ShooterPlayerComponent.h"

namespace sw
{
    namespace
    {
        struct ShooterEnemyComponentInternal
        {
            static constexpr float32 kMoveIdle = 0.0f;
            static constexpr float32 kMoveWalk = 1.0f;
            static constexpr float32 kMoveRun  = 2.0f;
        };
    } // namespace
} // namespace sw

namespace sw
{
    ShooterEnemyComponent::ShooterEnemyComponent()
        : _director{}
        , _radius{ 0.45f }
        , _height{ 2.0f }
        , _reach{ 1.7f }
        , _damage{ 9.0f }
        , _attackInterval{ 1.5f }
        , _attackHitTime{ 0.45f }
        , _attackLength{ 1.0f }
        , _riseTime{ 1.6f }
        , _staggerTime{ 0.4f }
        , _corpseTime{ 3.0f }
        , _runSpeed{ 3.2f }
        , _turnRate{ 6.0f }
        , _position{ 0.0f, 0.0f, 0.0f }
        , _yaw{ 0.0f }
        , _health{ 30.0f }
        , _maxHealth{ 30.0f }
        , _speed{ 3.0f }
        , _phaseTime{ 0.0f }
        , _attackCooldown{ 0.0f }
        , _phase{ ShooterEnemyPhase::Rising }
        , _bLaunched{ SW_FALSE }
        , _bHitPending{ SW_FALSE }
        , _bAttackLanded{ SW_FALSE }
        , _bAttackStarting{ SW_FALSE }
        , _reserved{ 0 }
    {
        setCanEverTick( true );
    }

    void ShooterEnemyComponent::launch( GameObjectHandle director, const float3& position, float32 yaw, float32 health, float32 speed )
    {
        _director       = director;
        _position       = position;
        _yaw            = yaw;
        _health         = health;
        _maxHealth      = health;
        _speed          = speed;
        _attackCooldown = _attackInterval * 0.5f;
        _bLaunched      = SW_TRUE;
        enterPhase( ShooterEnemyPhase::Rising );
        GameObject*     pOwner = getOwner();
        SceneComponent* pScene = pOwner != nullptr ? pOwner->getPrimarySceneComponent() : nullptr;
        if ( pScene != nullptr )
        {
            pScene->setLocalPosition( _position );
            pScene->setLocalRotation( float3{ 0.0f, _yaw, 0.0f } );
        }
        if ( pOwner != nullptr )
        {
            HealthChangedEvent event;
            event._ratio = 1.0f;
            event._kind  = HealthChangeKind::Reset;
            HealthListenerComponent::broadcast( *pOwner, event );
        }
    }

    void ShooterEnemyComponent::applyDamage( float32 amount )
    {
        if ( isAlive() == false )
            return;
        _health -= amount;
        // 체력 신호 — HP 바가 보이기 · 숨기기를 스스로 정한다(프리팹의 `_bShowWhenHurt` · `_bHideWhenDead`).
        const GameObject*  pOwner = getOwner();
        HealthChangedEvent event;
        event._ratio = MathUtil::max( 0.0f, _health / MathUtil::max( 1.0f, _maxHealth ) );
        event._kind  = _health <= 0.0f ? HealthChangeKind::Died : HealthChangeKind::Changed;
        if ( pOwner != nullptr )
            HealthListenerComponent::broadcast( *pOwner, event );
        if ( _health <= 0.0f )
        {
            enterPhase( ShooterEnemyPhase::Dying );
            return;
        }
        // 일어나는 중 · 휘두르는 중에는 클립을 끊지 않는다(맞은 표시는 HP 바).
        if ( _phase == ShooterEnemyPhase::Chasing || _phase == ShooterEnemyPhase::Staggered )
        {
            _bHitPending = SW_TRUE;
            enterPhase( ShooterEnemyPhase::Staggered );
        }
    }

    void ShooterEnemyComponent::enterPhase( ShooterEnemyPhase phase )
    {
        _phase     = phase;
        _phaseTime = 0.0f;
        if ( phase == ShooterEnemyPhase::Attacking )
        {
            _bAttackLanded   = SW_FALSE;
            _bAttackStarting = SW_TRUE;
        }
    }

    void ShooterEnemyComponent::onTick( float32 deltaTime )
    {
        using Internal = ShooterEnemyComponentInternal;
        Component::onTick( deltaTime );
        if ( _bLaunched == SW_FALSE || deltaTime <= 0.0f )
            return;
        GameObject*                     pOwner    = getOwner();
        GameObjectManager*              pManager  = pOwner != nullptr ? pOwner->getManager() : nullptr;
        const ShooterDirectorComponent* pDirector = pManager != nullptr ? GameDirectorComponent::resolve<ShooterDirectorComponent>( *pManager, _director ) : nullptr;
        if ( pDirector == nullptr )
            return;
        const float32 step = MathUtil::min( deltaTime, 0.1f );
        _phaseTime += step;
        _attackCooldown -= step;

        const float3  target   = pDirector->getPlayerFeet();
        const float3  toTarget = float3{ target._x - _position._x, 0.0f, target._z - _position._z };
        const float32 distance = toTarget.getLength();
        float32       moveCode = Internal::kMoveIdle;
        float32       wantYaw  = distance > 1.0e-4f ? MathUtil::atan2( toTarget._x, toTarget._z ) : _yaw;
        switch ( _phase )
        {
            case ShooterEnemyPhase::Rising:
            {
                if ( _phaseTime >= _riseTime )
                    enterPhase( ShooterEnemyPhase::Chasing );
                break;
            }
            case ShooterEnemyPhase::Chasing:
            {
                if ( distance < _reach && _attackCooldown <= 0.0f && pDirector->isPlayerAlive() )
                {
                    _attackCooldown = _attackInterval;
                    enterPhase( ShooterEnemyPhase::Attacking );
                    break;
                }
                if ( distance <= _reach * 0.85f )
                    break;
                // 다가온다 — 이웃과 떨어지고 상자를 돌아간다(밀려난다). 이웃은 디렉터가 적은 이번 프레임의 자리다.
                float3                          steer    = distance > 1.0e-4f ? toTarget * ( 1.0f / distance ) : float3{ 0.0f };
                const GameObjectHandle          self     = pOwner->getHandle();
                const vector<ShooterEnemyView>& listView = pDirector->getEnemyViews();
                const ShooterEnemyView*         pView    = listView.data();
                for ( size_t viewIndex = 0; viewIndex < listView.size(); ++viewIndex )
                {
                    const float3  away       = float3{ _position._x - pView[viewIndex]._position._x, 0.0f, _position._z - pView[viewIndex]._position._z };
                    const float32 awayLength = away.getLength();
                    if ( pView[viewIndex]._object != self && awayLength < 1.2f && awayLength > 1.0e-4f )
                        steer = steer + away * ( 0.9f / awayLength );
                }
                const float32 steerLength = steer.getLength();
                if ( steerLength > 1.0e-4f )
                {
                    steer   = steer * ( 1.0f / steerLength );
                    wantYaw = MathUtil::atan2( steer._x, steer._z );
                }
                const float3 next = ShooterArenaMath::resolveCircle( pDirector->getBoxes(), _position + steer * ( _speed * step ), _radius );
                _position         = float3{ next._x, 0.0f, next._z };
                moveCode          = _speed >= _runSpeed ? Internal::kMoveRun : Internal::kMoveWalk;
                break;
            }
            case ShooterEnemyPhase::Attacking:
            {
                // 클립의 맞는 시각에 손이 닿는 거리면 한 번 맞힌다.
                if ( _bAttackLanded == SW_FALSE && _phaseTime >= _attackHitTime )
                {
                    _bAttackLanded = SW_TRUE;
                    if ( distance < _reach * 1.25f )
                        requestPlayerDamage( pDirector->getPlayer() );
                }
                if ( _phaseTime >= _attackLength )
                    enterPhase( ShooterEnemyPhase::Chasing );
                break;
            }
            case ShooterEnemyPhase::Staggered:
            {
                if ( _phaseTime >= _staggerTime )
                    enterPhase( ShooterEnemyPhase::Chasing );
                break;
            }
            case ShooterEnemyPhase::Dying:
            {
                wantYaw = _yaw;
                break;
            }
        }

        // 몸을 진행 방향 쪽으로 돌린다(일어나는 중 · 쓰러지는 중은 그대로).
        const bool bTurning = _phase == ShooterEnemyPhase::Chasing || _phase == ShooterEnemyPhase::Attacking;
        if ( bTurning )
            _yaw = OrientationUtil::turnTowardAngle( _yaw, wantYaw, _turnRate * step );
        SceneComponent* pScene = pOwner->getPrimarySceneComponent();
        if ( pScene != nullptr )
        {
            pScene->setLocalPosition( _position );
            pScene->setLocalRotation( float3{ 0.0f, _yaw, 0.0f } );
        }
        updateAnimator( moveCode );
    }

    void ShooterEnemyComponent::updateAnimator( float32 moveCode )
    {
        GameObject*                pOwner    = getOwner();
        SkeletalAnimatorComponent* pAnimator = pOwner != nullptr ? pOwner->getComponent<SkeletalAnimatorComponent>() : nullptr;
        if ( pAnimator == nullptr )
            return;
        AnimParameterSet& parameters = pAnimator->getParameters();
        parameters.setFloat( hashed_string( "Move" ), moveCode );
        parameters.setBool( hashed_string( "Dead" ), isDead() );
        if ( _bAttackStarting == SW_TRUE )
            parameters.setTrigger( hashed_string( "Attack" ) );
        if ( _bHitPending == SW_TRUE )
            parameters.setTrigger( hashed_string( "Hit" ) );
        _bAttackStarting = SW_FALSE;
        _bHitPending     = SW_FALSE;
    }

    void ShooterEnemyComponent::requestPlayerDamage( GameObjectHandle player ) const
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
