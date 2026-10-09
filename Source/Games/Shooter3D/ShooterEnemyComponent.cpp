#include "pch.h"

#include "Games/Shooter3D/ShooterEnemyComponent.h"

#include "Core/Math/MathUtil.h"

#include "Engine/Object/Component/3D/SkeletalAnimatorComponent.h"
#include "Engine/Object/Component/Navigation/NavMeshAgentComponent.h"
#include "Engine/Object/Component/SceneComponent.h"
#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManager.h"

#include "GameFramework/Base/Actor/Control/PawnComponent.h"
#include "GameFramework/Base/Foundation/Utility/OrientationUtil.h"

#include "Games/Shooter3D/ShooterAnimParameter.h"
#include "Games/Shooter3D/ShooterBodyMovementComponent.h"
#include "Games/Shooter3D/ShooterDirectorComponent.h"
#include "Games/Shooter3D/ShooterPlayerComponent.h"

namespace sw
{
    namespace
    {
        struct ShooterEnemyComponentInternal
        {
            static constexpr const utf8* kAttackButton = "Attack"; ///< 폰 스키마의 휘두르기 버튼(AI 조종자가 누른다)
            static constexpr float32     kStopFraction = 0.85f;    ///< 손 닿는 거리의 이 비율 — 내비메시 에이전트가 멈추는 거리
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
        , _walkClipSpeed{ 0.3f }
        , _position{ 0.0f, 0.0f, 0.0f }
        , _yaw{ 0.0f }
        , _health{ 30.0f }
        , _maxHealth{ 30.0f }
        , _speed{ 3.0f }
        , _phaseTime{ 0.0f }
        , _attackCooldown{}
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
        _director  = director;
        _position  = position;
        _yaw       = yaw;
        _health    = health;
        _maxHealth = health;
        _speed     = speed;
        _attackCooldown.start( _attackInterval * 0.5f );
        _bLaunched = SW_TRUE;
        enterPhase( ShooterEnemyPhase::Rising );
        GameObject*     pOwner = getOwner();
        SceneComponent* pScene = pOwner != nullptr ? pOwner->getPrimarySceneComponent() : nullptr;
        if ( pScene != nullptr )
        {
            pScene->setLocalPosition( _position );
            pScene->setLocalRotation( float3{ 0.0f, _yaw, 0.0f } );
        }
        notifyHealthChanged( true );
        // 걷기는 몸 이동(플레이어와 같은 코드)이 의도로 한다 — 빠르기는 웨이브마다 달라 걷기 빠르기로 넣는다. 내비메시 에이전트(SteerOnly)는 속도만 낸다:
        // 최고 속도가 걷기 빠르기와 같아야 AI 조종자가 낸 이동 축(속도 / 최고 속도)이 그 빠르기로 걷는다.
        ShooterBodyMovementComponent* pMovement = pOwner != nullptr ? pOwner->getComponent<ShooterBodyMovementComponent>() : nullptr;
        if ( pMovement != nullptr )
        {
            pMovement->teleport( position );
            pMovement->setWalkSpeed( speed );
            pMovement->setSuspended( true );
        }
        NavMeshAgentComponent* pAgent = pOwner != nullptr ? pOwner->getComponent<NavMeshAgentComponent>() : nullptr;
        if ( pAgent != nullptr )
        {
            pAgent->setMaxSpeed( speed );
            pAgent->setStoppingDistance( _reach * ShooterEnemyComponentInternal::kStopFraction );
            pAgent->setRadius( _radius );
            pAgent->warp( position );
        }
        // 조종 회전도 처음 바라보는 쪽에서 시작한다(AI 조종자가 이어 받는다).
        PawnComponent* pPawn = pOwner != nullptr ? pOwner->getComponent<PawnComponent>() : nullptr;
        if ( pPawn != nullptr )
            pPawn->requestControlRotation( yaw, 0.0f );
    }

    void ShooterEnemyComponent::applyDamage( float32 amount )
    {
        if ( isAlive() == false )
            return;
        _health -= amount;
        // 체력 신호 — HP 바가 보이기 · 숨기기를 스스로 정한다(프리팹의 `_bShowWhenHurt` · `_bHideWhenDead`). 쓰러짐은 읽기(`isDead`)가 정한다.
        notifyHealthChanged( false );
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

    HealthReading ShooterEnemyComponent::getHealthReading() const
    {
        HealthReading reading;
        reading._health    = _health;
        reading._maxHealth = _maxHealth;
        reading._bDead     = isDead() ? SW_TRUE : SW_FALSE;
        return reading;
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

        // 판단은 AI 조종자가 했다 — 여기는 폰의 의도(이동 축 · 조종 요 · Attack)만 읽는다.
        const PawnComponent* pPawn       = pOwner->getComponent<PawnComponent>();
        const bool           bAttackHeld = pPawn != nullptr && pPawn->isButtonDown( pPawn->findButton( hashed_string( ShooterEnemyComponentInternal::kAttackButton ) ) );
        const float32        controlYaw  = pPawn != nullptr ? pPawn->getIntent()._controlYaw : _yaw;
        const float3         target      = pDirector->getPlayerFeet();
        const float3         toTarget    = float3{ target._x - _position._x, 0.0f, target._z - _position._z };
        const float32        distance    = toTarget.getLength();
        // 휘두르기 간격 — 쫓는 동안 Attack 이 눌려 있으면 간격마다 한 번, 지나친 몫을 잇는다(`Countdown::tickRepeat`). 쿨다운은 어느 단계에서도 흐른다.
        const bool bWantSwing = _phase == ShooterEnemyPhase::Chasing && bAttackHeld;
        const bool bSwing     = _attackCooldown.tickRepeat( step, _attackInterval, bWantSwing );
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
                if ( bSwing )
                    enterPhase( ShooterEnemyPhase::Attacking );
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
                break;
            }
        }

        // 쫓는 동안만 걷는다(일어남 · 휘두름 · 움찔 · 쓰러짐은 제자리) — 플레이어와 같은 몸 이동.
        float32                       moveCode  = ShooterAnimParameter::kMoveIdle;
        ShooterBodyMovementComponent* pMovement = pOwner->getComponent<ShooterBodyMovementComponent>();
        if ( pMovement != nullptr )
        {
            pMovement->setSuspended( _phase != ShooterEnemyPhase::Chasing );
            (void)pMovement->stepMovement( pDirector->getBoxes(), step );
            _position              = pMovement->getFeetPosition();
            const float3& velocity = pMovement->getMoveVelocity();
            const float32 speed    = MathUtil::sqrt( velocity._x * velocity._x + velocity._z * velocity._z );
            if ( speed > _walkClipSpeed )
                moveCode = speed >= _runSpeed ? ShooterAnimParameter::kMoveRun : ShooterAnimParameter::kMoveWalk;
        }
        // 몸을 조종 요 쪽으로 돌린다(일어나는 중 · 움찔 · 쓰러지는 중은 그대로).
        const bool bTurning = _phase == ShooterEnemyPhase::Chasing || _phase == ShooterEnemyPhase::Attacking;
        if ( bTurning )
            _yaw = OrientationUtil::turnTowardAngle( _yaw, controlYaw, _turnRate * step );
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
        parameters.setFloat( hashed_string( ShooterAnimParameter::kMove ), moveCode );
        parameters.setBool( hashed_string( ShooterAnimParameter::kDead ), isDead() );
        if ( _bAttackStarting == SW_TRUE )
            parameters.setTrigger( hashed_string( "Attack" ) );
        if ( _bHitPending == SW_TRUE )
            parameters.setTrigger( hashed_string( ShooterAnimParameter::kHit ) );
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
