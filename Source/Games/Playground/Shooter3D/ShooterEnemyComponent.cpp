#include "pch.h"

#include "Games/Playground/Shooter3D/ShooterEnemyComponent.h"

#include "Core/Math/MathUtil.h"
#include "Core/Math/OrientationUtil.h"

#include "Engine/Animation/Component/SkeletalAnimatorComponent.h"
#include "Engine/Character/Hit/RagdollComponent.h"
#include "Engine/Character/Socket/SocketBindingComponent.h"
#include "Engine/Navigation/Component/NavMeshAgentComponent.h"
#include "Engine/Object/Component/SceneComponent.h"
#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectEvents.h"
#include "Engine/Object/GameObject/GameObjectManager.inl"
#include "Engine/Physics/Component/RigidBodyComponent.h"

#include "GameFramework/Runtime/Character/Control/Pawn/PawnComponent.h"
#include "GameFramework/Runtime/Gameplay/Appearance/CharacterAppearanceComponent.h"

#include "Games/Playground/Shooter3D/ShooterAnimationParameter.h"
#include "Games/Playground/Shooter3D/ShooterBodyMovementComponent.h"
#include "Games/Playground/Shooter3D/ShooterGameModeComponent.h"
#include "Games/Playground/Shooter3D/ShooterPlayerComponent.h"

namespace sw
{
    namespace
    {
        struct ShooterEnemyComponentInternal
        {
            static constexpr const utf8* kAttackButton = "Attack"; ///< 폰 스키마의 휘두르기 버튼(AI 조종자가 누른다)

            /** @brief 최대 체력만 정한 체력 설정을 만듭니다. 실드 · 회복 · 기절은 기본값(꺼짐)입니다. */
            static HealthSettings makeHealthSettings( float32 maxHealth )
            {
                HealthSettings settings;
                settings._maxHealth = maxHealth;
                return settings;
            }
        };
    } // namespace
} // namespace sw

namespace sw
{
    ShooterEnemyComponent::ShooterEnemyComponent()
        : _gameMode{}
        , _radius{ 0.45f }
        , _height{ 2.0f }
        , _reach{ 1.7f }
        , _stopFraction{ 0.85f }
        , _damage{ 9.0f }
        , _attackInterval{ 1.5f }
        , _attackHitEvent{}
        , _attackLength{ 1.0f }
        , _riseTime{ 1.6f }
        , _staggerTime{ 0.4f }
        , _corpseTime{ 3.0f }
        , _runSpeed{ 3.2f }
        , _turnRate{ 6.0f }
        , _walkClipSpeed{ 0.3f }
        , _position{ 0.0f, 0.0f, 0.0f }
        , _yaw{ 0.0f }
        , _replicaVelocity{ 0.0f, 0.0f, 0.0f }
        , _healthMachine{}
        , _speed{ 3.0f }
        , _phaseTime{ 0.0f }
        , _attackCooldown{}
        , _netEntityID{ 0 }
        , _phase{ ShooterEnemyPhase::Rising }
        , _autoPossessBeforeReplica{ PawnAutoPossess::None }
        , _bLaunched{ SW_FALSE }
        , _bHitPending{ SW_FALSE }
        , _bAttackLanded{ SW_FALSE }
        , _bAttackStarting{ SW_FALSE }
        , _bAttackHitEvent{ SW_FALSE }
        , _bHitEventMissingReported{ SW_FALSE }
        , _bReplica{ SW_FALSE }
        , _reserved{ 0 }
    {
        setCanEverTick( true );
    }

    bool ShooterEnemyComponent::launch( GameObjectHandle gameMode, const AbsolutePosition& position, float32 yaw, float32 health, float32 speed )
    {
        if ( _healthMachine.initialize( ShooterEnemyComponentInternal::makeHealthSettings( health ) ) == false )
            return false;
        _gameMode = gameMode;
        _position = position._value;
        _yaw      = yaw;
        _speed    = speed;
        _attackCooldown.start( _attackInterval * 0.5f );
        _bLaunched = SW_TRUE;
        enterPhase( ShooterEnemyPhase::Rising );
        GameObject*     pOwner = getOwner();
        SceneComponent* pScene = pOwner != nullptr ? pOwner->getPrimarySceneComponent() : nullptr;
        if ( pScene != nullptr )
        {
            pScene->setZeroOriginPosition( _position );
            pScene->setLocalRotation( float3{ 0.0f, _yaw, 0.0f } );
        }
        IHealthSource::notifyHealthChanged( _healthSourcePort, getOwner(), true );
        // 걷기는 몸 이동(플레이어와 같은 코드)이 의도로 한다 — 빠르기는 웨이브마다 달라 걷기 빠르기로 넣는다. 내비메시 에이전트(SteerOnly)는 속도만 낸다:
        // 최고 속도가 걷기 빠르기와 같아야 AI 조종자가 낸 이동 축(속도 / 최고 속도)이 그 빠르기로 걷는다.
        ShooterBodyMovementComponent* pMovement = pOwner != nullptr ? pOwner->getComponent<ShooterBodyMovementComponent>() : nullptr;
        if ( pMovement != nullptr )
        {
            pMovement->teleport( position );
            pMovement->setWalkSpeed( speed );
            _bodySuspension = pMovement->suspend();
        }
        NavMeshAgentComponent* pAgent = pOwner != nullptr ? pOwner->getComponent<NavMeshAgentComponent>() : nullptr;
        if ( pAgent != nullptr )
        {
            pAgent->setMaxSpeed( speed );
            pAgent->setStoppingDistance( _reach * _stopFraction );
            pAgent->setRadius( _radius );
            pAgent->warp( pOwner->getManager()->getWorldOrigin().toLocal( position ) ); // 내비 에이전트는 씬 자리로 돈다
        }
        // 조종 회전도 처음 바라보는 쪽에서 시작한다(AI 조종자가 이어 받는다).
        PawnComponent* pPawn = pOwner != nullptr ? pOwner->getComponent<PawnComponent>() : nullptr;
        if ( pPawn != nullptr )
            pPawn->requestControlRotation( yaw, 0.0f );
        return true;
    }

    void ShooterEnemyComponent::launchReplica( GameObjectHandle gameMode, const AbsolutePosition& position, float32 yaw )
    {
        (void)_healthMachine.initialize( ShooterEnemyComponentInternal::makeHealthSettings( 1.0f ) ); // 1 은 범위 안이다 — 체력은 서버 몫이고 살아 있음만 뜻한다
        _gameMode        = gameMode;
        _position        = position._value;
        _replicaVelocity = float3{ 0.0f, 0.0f, 0.0f };
        _yaw             = yaw;
        _bLaunched       = SW_TRUE;
        _bReplica        = SW_TRUE;
        enterPhase( ShooterEnemyPhase::Rising );
        GameObject*     pOwner = getOwner();
        SceneComponent* pScene = pOwner != nullptr ? pOwner->getPrimarySceneComponent() : nullptr;
        if ( pOwner != nullptr )
            pOwner->setTransient( true ); // 서버의 적을 비춘 모습이다 — 받은 복제에서 다시 세운다
        if ( pScene != nullptr )
        {
            pScene->setZeroOriginPosition( _position );
            pScene->setLocalRotation( float3{ 0.0f, _yaw, 0.0f } );
        }
        PawnComponent* pPawn = pOwner != nullptr ? pOwner->getComponent<PawnComponent>() : nullptr;
        if ( pPawn != nullptr )
        {
            _autoPossessBeforeReplica = pPawn->getAutoPossess();
            pPawn->setAutoPossess( PawnAutoPossess::None );
        }
    }

    SpawnStateReset ShooterEnemyComponent::resetToSpawnState()
    {
        if ( _bReplica == SW_TRUE )
        {
            GameObject*    pOwner = getOwner();
            PawnComponent* pPawn  = pOwner != nullptr ? pOwner->getComponent<PawnComponent>() : nullptr;
            if ( pPawn != nullptr )
                pPawn->setAutoPossess( _autoPossessBeforeReplica );
            if ( pOwner != nullptr )
                pOwner->setTransient( false );
        }
        (void)_healthMachine.initialize( HealthSettings{} ); // 기본 설정은 범위 안이다 — 생성자의 가득 찬 상태와 같다
        _healthMachine.discardEvents();
        _phaseTime                = 0.0f;
        _attackCooldown           = Countdown{};
        _netEntityID              = 0;
        _phase                    = ShooterEnemyPhase::Rising;
        _bLaunched                = SW_FALSE;
        _bHitPending              = SW_FALSE;
        _bAttackLanded            = SW_FALSE;
        _bAttackStarting          = SW_FALSE;
        _bAttackHitEvent          = SW_FALSE;
        _bHitEventMissingReported = SW_FALSE;
        _bReplica                 = SW_FALSE;
        _bodySuspension.reset();
        return SpawnStateReset::Reset;
    }

    void ShooterEnemyComponent::setReplicatedPose( const AbsolutePosition& position, const float3& velocity, float32 yaw, ShooterEnemyPhase phase )
    {
        _position        = position._value;
        _replicaVelocity = velocity;
        _yaw             = yaw;
        if ( phase == _phase )
            return;
        _bHitPending = phase == ShooterEnemyPhase::Staggered ? SW_TRUE : SW_FALSE;
        enterPhase( phase );
    }

    void ShooterEnemyComponent::updateReplica( float32 step )
    {
        _phaseTime += step;
        const float32 speed    = float3{ _replicaVelocity._x, 0.0f, _replicaVelocity._z }.getLength();
        float32       moveCode = ShooterAnimationParameter::kMoveIdle;
        if ( speed > _walkClipSpeed )
            moveCode = speed >= _runSpeed ? ShooterAnimationParameter::kMoveRun : ShooterAnimationParameter::kMoveWalk;
        GameObject*     pOwner = getOwner();
        SceneComponent* pScene = pOwner != nullptr ? pOwner->getPrimarySceneComponent() : nullptr;
        if ( pScene != nullptr )
        {
            pScene->setZeroOriginPosition( _position );
            pScene->setLocalRotation( float3{ 0.0f, _yaw, 0.0f } );
        }
        updateAnimator( moveCode );
    }

    bool ShooterEnemyComponent::applyDamage( float32 amount )
    {
        if ( isAlive() == false )
            return false;
        (void)_healthMachine.applyDamage( amount ); // 결과는 아래 isAlive 로 읽는다
        _healthMachine.discardEvents();             // 피해 알림은 notifyHealthChanged 가 맡는다
        // 체력 신호 — HP 바가 보이기 · 숨기기를 스스로 정한다(프리팹의 `_bShowWhenHurt` · `_bHideWhenDead`). 쓰러짐은 읽기(`isDead`)가 정한다.
        IHealthSource::notifyHealthChanged( _healthSourcePort, getOwner(), false );
        if ( _healthMachine.isAlive() == false )
        {
            enterPhase( ShooterEnemyPhase::Dying );
            requestDropSocketParts();
            return true;
        }
        // 일어나는 중 · 휘두르는 중에는 클립을 끊지 않는다(맞은 표시는 HP 바).
        if ( _phase == ShooterEnemyPhase::Chasing || _phase == ShooterEnemyPhase::Staggered )
        {
            _bHitPending = SW_TRUE;
            enterPhase( ShooterEnemyPhase::Staggered );
        }
        return false;
    }

    void ShooterEnemyComponent::onRegister( GameObjectManager& manager )
    {
        Component::onRegister( manager );
        _notifySubscription = GameObjectEvents::listen<&ShooterEnemyComponent::handleAnimationNotify>( *this, &GameObjectEvents::_animationNotify );
    }

    void ShooterEnemyComponent::handleHit( const HitInfo& hit )
    {
        if ( hit._bFatal )
            return; // 쓰러질 때 다시 보낸 치명 사본이다(`deliverFatalHit`)
        if ( applyDamage( hit._damage * hit._damageMultiplier ) == false )
            return;
        ShooterPlayerComponent* const pKiller = hit._pInstigator != nullptr ? hit._pInstigator->getComponent<ShooterPlayerComponent>() : nullptr;
        if ( pKiller != nullptr )
            pKiller->recordKill();
        // 쓰러졌다 — 치명 신호를 같은 오브젝트에 다시 보낸다. 래그돌(`_bRagdollOnFatalHit`)이 맞은 바디에 맞은 방향의 충격량을 주며 넘어가고,
        // 절단이 있는 적(해골)은 맞은 영역을 잘라 낸다. 래그돌이 없는 프리팹은 데이터 오류다(쓰러짐 클립만 돈다).
        const GameObject*       pOwner   = getOwner();
        const RagdollComponent* pRagdoll = pOwner != nullptr ? pOwner->getComponent<RagdollComponent>() : nullptr;
        if ( pRagdoll == nullptr || pRagdoll->isRagdollOnFatalHit() == false )
            SW_LOG_ERROR_ID( "ShooterEnemyComponent.EnemyNoRagdollComponentCannot", "Enemy '%#' has no RagdollComponent with _bRagdollOnFatalHit - it cannot collapse into a ragdoll",
                             pOwner != nullptr ? pOwner->getName().c_str() : "(no owner)" );
        IHealthSource::deliverFatalHit( getOwner(), hit );
    }

    HealthReading ShooterEnemyComponent::getHealthReading() const
    {
        HealthReading reading;
        reading._health    = _healthMachine.getHealth();
        reading._maxHealth = _healthMachine.getSettings()._maxHealth;
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
            _bAttackHitEvent = SW_FALSE;
        }
    }

    void ShooterEnemyComponent::handleAnimationNotify( const AnimationNotifyInfo& notify )
    {
        const bool bSwingLands = _phase == ShooterEnemyPhase::Attacking && _attackHitEvent.empty() == false && notify._event == _attackHitEvent;
        if ( bSwingLands )
            _bAttackHitEvent = SW_TRUE;
    }

    void ShooterEnemyComponent::onTick( TickContext& context )
    {
        Component::onTick( context );
        if ( _bLaunched == SW_FALSE || context.getDeltaTime() <= 0.0f )
            return;
        if ( _bReplica == SW_TRUE )
        {
            updateReplica( MathUtil::min( context.getDeltaTime(), 0.1f ) );
            return;
        }
        GameObject* pOwner = getOwner();
        // 디렉터는 틱 뒤에만 쓰는 형식이다(`kTickReadSafe`) — 판 상태(플레이어 자리 · 상자)를 남 읽기 길로 읽는다.
        const ShooterGameModeComponent* pGameMode = context.readOther<ShooterGameModeComponent>( _gameMode );
        if ( pGameMode == nullptr )
            return;
        const float32 step = MathUtil::min( context.getDeltaTime(), 0.1f );
        _phaseTime += step;

        // 판단은 AI 조종자가 했다 — 여기는 폰의 의도(이동 축 · 조종 요 · Attack)만 읽는다.
        const PawnComponent*     pPawn       = pOwner->getComponent<PawnComponent>();
        const bool               bAttackHeld = pPawn != nullptr && pPawn->isButtonDown( pPawn->findButton( hashed_string( ShooterEnemyComponentInternal::kAttackButton ) ) );
        const float32            controlYaw  = pPawn != nullptr ? pPawn->getIntent()._controlYaw : _yaw;
        const ShooterPlayerView* pTarget     = pGameMode->findNearestPlayer( AbsolutePosition{ _position } );
        const float3             target      = pTarget != nullptr ? pTarget->_feet._value : _position;
        const float3             toTarget    = float3{ target._x - _position._x, 0.0f, target._z - _position._z };
        const float32            distance    = toTarget.getLength();
        // 휘두르기 간격 — 쫓는 동안 Attack 이 눌려 있으면 간격마다 한 번, 지나친 몫을 잇는다(`Countdown::updateRepeat`). 쿨다운은 어느 단계에서도 흐른다.
        const bool bWantSwing = _phase == ShooterEnemyPhase::Chasing && bAttackHeld;
        const bool bSwing     = _attackCooldown.updateRepeat( step, _attackInterval, bWantSwing );
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
                // 클립의 맞는 알림이 왔고 손이 닿는 거리면 한 번 맞힌다 — 시각의 기준은 클립 데이터다.
                if ( _bAttackLanded == SW_FALSE && _bAttackHitEvent == SW_TRUE )
                {
                    _bAttackLanded = SW_TRUE;
                    if ( pTarget != nullptr && distance < _reach * 1.25f )
                        requestPlayerDamage( pTarget->_object );
                }
                if ( _phaseTime >= _attackLength )
                {
                    // 맞는 알림 없이 휘두름이 끝났다 — 클립에 알림이 없거나 알림 표 · 이벤트 이름이 어긋난 데이터 오류다(조용히 시각으로 대신하지 않는다).
                    if ( _bAttackLanded == SW_FALSE && _bHitEventMissingReported == SW_FALSE )
                    {
                        _bHitEventMissingReported = SW_TRUE;
                        SW_LOG_ERROR_ID( "ShooterEnemyComponent.EnemyFinishedSwingWithout", "[Shooter] enemy '%#' finished a swing without the '%#' notify event - check the attack clip's notifies and the notify table",
                                         pOwner->getName().c_str(), _attackHitEvent.c_str() );
                    }
                    enterPhase( ShooterEnemyPhase::Chasing );
                }
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
        float32                       moveCode  = ShooterAnimationParameter::kMoveIdle;
        ShooterBodyMovementComponent* pMovement = pOwner->getComponent<ShooterBodyMovementComponent>();
        if ( pMovement != nullptr )
        {
            if ( _phase == ShooterEnemyPhase::Chasing )
                _bodySuspension.reset();
            else if ( _bodySuspension.isActive() == false )
                _bodySuspension = pMovement->suspend();
            (void)pMovement->stepMovement( pGameMode->getBoxes(), pGameMode->getFloorSteps(), step );
            _position              = pMovement->getFeetPosition()._value;
            const float3& velocity = pMovement->getMoveVelocity();
            const float32 speed    = MathUtil::sqrt( velocity._x * velocity._x + velocity._z * velocity._z );
            if ( speed > _walkClipSpeed )
                moveCode = speed >= _runSpeed ? ShooterAnimationParameter::kMoveRun : ShooterAnimationParameter::kMoveWalk;
        }
        // 몸을 조종 요 쪽으로 돌린다(일어나는 중 · 움찔 · 쓰러지는 중은 그대로).
        const bool bTurning = _phase == ShooterEnemyPhase::Chasing || _phase == ShooterEnemyPhase::Attacking;
        if ( bTurning )
            _yaw = OrientationUtil::turnTowardAngle( _yaw, controlYaw, _turnRate * step );
        SceneComponent* pScene = pOwner->getPrimarySceneComponent();
        if ( pScene != nullptr )
        {
            pScene->setZeroOriginPosition( _position );
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
        AnimationParameterSet& parameters = pAnimator->getParameters();
        parameters.setFloat( hashed_string( ShooterAnimationParameter::kMove ), moveCode );
        parameters.setBool( hashed_string( ShooterAnimationParameter::kDead ), isDead() );
        if ( _bAttackStarting == SW_TRUE )
            parameters.setTrigger( hashed_string( "Attack" ) );
        if ( _bHitPending == SW_TRUE )
            parameters.setTrigger( hashed_string( ShooterAnimationParameter::kHit ) );
        _bAttackStarting = SW_FALSE;
        _bHitPending     = SW_FALSE;
    }

    void ShooterEnemyComponent::requestPlayerDamage( GameObjectHandle player ) const
    {
        const GameObject*        pOwner   = getOwner();
        const GameObjectManager* pManager = pOwner != nullptr ? pOwner->getManager() : nullptr;
        if ( pManager == nullptr )
            return;
        // 플레이어는 다른 워커가 틱하고 있을 수 있다 — 틱 뒤(게임 스레드)에 깎는다.
        const float32 damage = _damage;
        pManager->executeOrDeferPostTickOnScene( [player, damage]( GameObjectManager& sceneManager )
        {
            GameObject*             pPlayer    = sceneManager.resolveGameObject( player );
            ShooterPlayerComponent* pComponent = pPlayer != nullptr ? pPlayer->getComponent<ShooterPlayerComponent>() : nullptr;
            if ( pComponent != nullptr )
                pComponent->takeDamage( damage );
        } );
    }

    void ShooterEnemyComponent::requestDropSocketParts() const
    {
        const GameObject*                   pOwner      = getOwner();
        const GameObjectManager*            pManager    = pOwner != nullptr ? pOwner->getManager() : nullptr; // 틱(피해)에서 불린다 — 쓰기는 틱 뒤 작업이 받은 매니저로
        const CharacterAppearanceComponent* pAppearance = pOwner != nullptr ? pOwner->getComponent<CharacterAppearanceComponent>() : nullptr;
        if ( pManager == nullptr || pAppearance == nullptr )
            return;
        vector<GameObjectHandle> listPart;
        pAppearance->collectPartObjects( listPart );
        // 강체를 동적으로 바꾸는 일은 물리 단계와 겹치지 않게 틱 뒤에 한다.
        pManager->executeOrDeferPostTickOnScene( [listPart]( GameObjectManager& sceneManager )
        {
            for ( const GameObjectHandle part : listPart )
            {
                GameObject*             pPart      = sceneManager.resolveGameObject( part );
                SocketBindingComponent* pBinding   = pPart != nullptr ? pPart->getComponent<SocketBindingComponent>() : nullptr;
                const bool              bDroppable = pBinding != nullptr && pPart->getComponent<RigidBodyComponent>() != nullptr;
                if ( bDroppable && pBinding->release( SocketReleaseMode::Physics ) == false )
                    SW_LOG_ERROR_ID( "ShooterEnemyComponent.EnemyPartRigidBody", "Enemy part '%#' has a rigid body but could not be released to physics", pPart->getName().c_str() );
            }
        } );
    }

    void ShooterEnemyComponent::HitReceiverPort::receiveHit( const HitInfo& hit )
    {
        getPortOwner<ShooterEnemyComponent>( *this, offsetof( ShooterEnemyComponent, _hitReceiverPort ) ).handleHit( hit );
    }

    HealthReading ShooterEnemyComponent::HealthSourcePort::getHealthReading() const
    {
        return getPortOwner<const ShooterEnemyComponent>( *this, offsetof( ShooterEnemyComponent, _healthSourcePort ) ).getHealthReading();
    }
} // namespace sw
