#include "pch.h"

#include "Games/Playground/Hub/HubPawnReplicationComponent.h"

#include "Core/Algorithm/VectorUtil.h"
#include "Core/Foundation/Constants.h"
#include "Core/Log/LogOnce.h"
#include "Core/Math/MathUtil.h"
#include "Core/Network/Connection/NetEndpoint.h"
#include "Core/Serialization/BitStream.h"
#include "Core/Time/MonotonicClock.h"

#include "Engine/Asset/AssetManager.h"
#include "Engine/Automation/Watch/AutomationComponentWatch.h"
#include "Engine/Automation/Watch/AutomationWatch.h"
#include "Engine/Object/Component/SceneComponent.h"
#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManager.h"
#include "Engine/Object/GameObject/GameObjectManager.inl"
#include "Engine/Object/GameObject/Registry/ComponentRegistry.h"
#include "Engine/Object/GameObject/Service/SceneNetLink.h"
#include "Engine/Object/GameObject/Tick/TickContext.h"
#include "Engine/Object/Prefab/PrefabCache.h"
#include "Engine/Physics/Component/CharacterControllerComponent.h"
#include "Engine/Serialization/Fixed/ReplicatedLayout.h"

#include "GameFramework/Runtime/Character/Control/ControlSystem.h"
#include "GameFramework/Runtime/Character/Control/Controller/RemoteControllerComponent.h"
#include "GameFramework/Runtime/Character/Control/Pawn/PawnComponent.h"
#include "GameFramework/Runtime/Foundation/Framework/GameService.h"
#include "GameFramework/Runtime/Gameplay/Vehicle/MountInteractionComponent.h"
#include "GameFramework/Runtime/Gameplay/Vehicle/VehicleExitComponent.h"
#include "GameFramework/Runtime/Gameplay/Vehicle/VehicleSeatComponent.h"

#include "Games/Playground/Hub/HubWorldComponent.h"

namespace sw
{
    namespace
    {
        static_assert( HubPawnReplicationComponent::kMaxMount == 8, "HubPawnState · HubPawnPose 의 _mountNumber Max 를 kMaxMount 와 맞춘다" );
        static_assert( static_cast<uint8>( MountGait::Gallop ) == 4, "HubPawnPose::_gait 의 Max 를 열거의 끝 값과 맞춘다" );

        struct HubPawnReplicationComponentInternal
        {
            /** @brief 한 프레임에 따라잡는 고정 틱 상한입니다. 멈춘 뒤의 긴 프레임이 틱을 몰아 돌리지 않게 한다(남은 시간은 버린다). */
            static constexpr int32 kMaxTicksPerFrame = 4;
            /** @brief 남의 폰 모습 오브젝트의 이름입니다(시나리오가 이 이름으로 자리를 읽는다). */
            static constexpr const utf8* kRemotePawnName = "HubRemotePawn";
            static constexpr const utf8* kServerPawnName = "HubPawn";

            /** @brief 다른 폰의 의도이거나 낡은 의도일 때 쓰는 입력 — 시선만 남기고 움직임 · 버튼을 지운다. */
            static ControlIntent makeNeutralIntent( const ControlIntent& intent )
            {
                ControlIntent neutral;
                neutral._controlYaw   = intent._controlYaw;
                neutral._controlPitch = intent._controlPitch;
                return neutral;
            }

            static const float3& getPosition( const HubPawnState& state ) { return state._mountNumber != 0 ? state._ride._position : state._walk._motion._position; }

            /** @brief 자리를 @p position 에 두고 이어 가던 운동을 지웁니다(규칙이 받지 않은 걸음을 되돌린다). */
            static void placeAtRest( HubPawnState& inoutState, const float3& position )
            {
                if ( inoutState._mountNumber != 0 )
                {
                    inoutState._ride._position     = position;
                    inoutState._ride._forwardSpeed = 0.0f;
                    return;
                }
                CharacterMotionState rest;
                rest._position             = position;
                rest._movementMode         = inoutState._walk._motion._movementMode;
                rest._bGrounded            = inoutState._walk._motion._bGrounded;
                inoutState._walk._motion   = rest;
                inoutState._walk._velocity = float3{};
            }
        };
    } // namespace
} // namespace sw

namespace sw
{
    HubPawnReplicationComponent::HubPawnReplicationComponent()
        : _pawnPrefab{}
        , _localPawn{}
        , _listMount{}
        , _tickInterval{ 1.0f / 30.0f }
        , _smoothingHalfLife{ 0.05f }
        , _smoothingSnapDistance{ 2.0f }
        , _reconcileTolerance{ 0.01f }
        , _interestCellSize{ 16.0f }
        , _interestEnterRadius{ 60.0f }
        , _interestLeaveRadius{ 70.0f }
        , _server{}
        , _client{}
        , _interest{}
        , _routerSubscription{}
        , _prediction{}
        , _smoother{}
        , _listServerPawn{}
        , _listRemotePawn{}
        , _listSession{}
        , _listClaimScratch{}
        , _listEntityScratch{}
        , _bytesScratch{}
        , _previousPosition{}
        , _lastFrameNanoseconds{ 0 }
        , _accumulator{ 0.0f }
        , _pendingTriggerYaw{ 0.0f }
        , _pendingTriggerPitch{ 0.0f }
        , _pendingTriggered{ 0 }
        , _pendingPawnEntityID{ 0 }
        , _tick{ 0 }
        , _reconciledTick{ 0 }
        , _localAvatarID{ 0 }
        , _role{ Role::None }
        , _bStartTried{ SW_FALSE }
        , _bPendingTriggerLook{ SW_FALSE }
        , _reserved{ 0 }
    {
        setCanEverTick( true );
    }

    HubPawnReplicationComponent::~HubPawnReplicationComponent() = default;

    void HubPawnReplicationComponent::onEndPlay()
    {
        // 세운 오브젝트는 씬과 함께 사라진다 — 등록만 뗀다(끝나는 중의 씬에서 오브젝트를 지우지 않는다).
        _routerSubscription.reset();
        _listServerPawn.clear();
        _listRemotePawn.clear();
        _localAvatarID = 0;
        _role          = Role::None;
        _bStartTried   = SW_FALSE;
        Component::onEndPlay();
    }

    void HubPawnReplicationComponent::onTick( TickContext& context )
    {
        Component::onTick( context );
        // 폰마다의 물리 이동 · 스폰 · 소켓 전송이라 틱 워커에서 돌지 않는다. 씬에 하나뿐인 컴포넌트라 이 한 일만 큐에 든다.
        context.deferPostTickOnComponent( *this, []( HubPawnReplicationComponent& component, GameObjectManager& manager )
        { component.runNetFrame( manager ); } );
    }

    void HubPawnReplicationComponent::runNetFrame( GameObjectManager& manager )
    {
        const float32            seconds = consumeRealSeconds();
        const NetLinkValue*      pLink   = SceneNetLink::findBoundFor( *this );
        const HubWorldComponent* pHub    = getOwner() != nullptr ? getOwner()->getComponent<HubWorldComponent>() : nullptr;
        if ( pLink == nullptr || pHub == nullptr )
        {
            stopRole( manager );
            return;
        }
        if ( _role == Role::None )
            startRole( *pLink, *pHub );
        if ( _role == Role::Server )
            runServerFrame( manager, *pHub, seconds );
        else if ( _role == Role::Client )
            runClientFrame( manager, *pHub, seconds );
    }

    void HubPawnReplicationComponent::startRole( const NetLinkValue& link, const HubWorldComponent& hub )
    {
        const bool bServer   = link._role == NetLinkRole::Server;
        const bool bHubReady = bServer ? hub.findServer() != nullptr : hub.getClient().isActive();
        if ( _bStartTried == SW_TRUE || bHubReady == false )
            return; // 티켓 없는 서버 세션에는 허브 서버가 없다 — 폰도 세우지 않는다
        _bStartTried = SW_TRUE;
        PredictionSmootherSettings smoothing;
        smoothing._halfLifeSeconds = _smoothingHalfLife;
        smoothing._snapDistance    = _smoothingSnapDistance;
        if ( _listMount.size() > kMaxMount || _smoother.initialize( smoothing ) == false )
        {
            SW_LOG_ERROR_ID( "Hub.PawnReplicationSettings", "[Hub] pawn replication settings are not usable (%# mounts of at most %#, smoothing half life %# s, snap %# m) - pawns stay local",
                             static_cast<uint32>( _listMount.size() ), kMaxMount, _smoothingHalfLife, _smoothingSnapDistance );
            return;
        }
        if ( bServer )
        {
            ReplicationServerConfig settings;
            settings._tickInterval = _tickInterval;
            if ( _interest.initialize( _interestCellSize, _interestEnterRadius, _interestLeaveRadius ) && _server.initialize( link._pPeers, settings, &_interest ) )
                _routerSubscription = link._pRouter->registerHandler( &_server );
        }
        else
        {
            ReplicationClientConfig settings;
            settings._tickInterval = _tickInterval;
            if ( _client.initialize( link._pPeers, settings ) )
                _routerSubscription = link._pRouter->registerHandler( &_client );
        }
        if ( _routerSubscription.isActive() == false )
        {
            SW_LOG_ERROR_ID( "Hub.PawnReplicationNotStarted", "[Hub] the pawn replication did not start (reason above) - pawns stay local" );
            return;
        }
        _accumulator = 0.0f;
        _role        = bServer ? Role::Server : Role::Client;
        if ( bServer == false )
            return;
        // 서버 씬의 플레이어 오브젝트는 아무도 조종하지 않는다 — 나는 자리에 선 캡슐이 접속한 폰을 막지 않게 물리 몸만 끈다(튜닝의 원본으로는 남는다).
        const GameObjectManager*      pManager    = getOwner()->getManager();
        const GameObject*             pLocal      = pManager != nullptr ? pManager->resolveGameObject( _localPawn ) : nullptr;
        CharacterControllerComponent* pController = pLocal != nullptr ? pLocal->getComponent<CharacterControllerComponent>() : nullptr;
        if ( pController != nullptr )
            pController->setActive( false );
        SW_LOG_INFO_ID( "Hub.PawnReplicationStarted", "[Hub] a server-authoritative pawn stands for each connected account (%# Hz)", static_cast<int32>( MathUtil::round( 1.0f / _tickInterval ) ) );
    }

    void HubPawnReplicationComponent::stopRole( GameObjectManager& manager )
    {
        _bStartTried = SW_FALSE;
        if ( _role == Role::None )
            return;
        _routerSubscription.reset();
        for ( const ServerPawn& pawn : _listServerPawn )
        {
            removeServerPawn( manager, pawn );
        }
        _listServerPawn.clear();
        for ( const RemotePawn& remote : _listRemotePawn )
        {
            GameObject* pObject = manager.resolveGameObject( remote._object );
            if ( pObject != nullptr )
                manager.destroyGameObject( pObject );
        }
        _listRemotePawn.clear();
        GameObject* pLocal = manager.resolveGameObject( _localPawn );
        if ( pLocal != nullptr && _role == Role::Server )
        {
            CharacterControllerComponent* pController = pLocal->getComponent<CharacterControllerComponent>();
            if ( pController != nullptr )
                pController->setActive( true );
        }
        else if ( pLocal != nullptr && _localAvatarID != 0 )
        {
            CharacterMovementComponent* pMovement = pLocal->getComponent<CharacterMovementComponent>();
            if ( pMovement != nullptr )
                pMovement->setNetDriven( false );
            MountInteractionComponent* pInteraction = pLocal->getComponent<MountInteractionComponent>();
            if ( pInteraction != nullptr )
                pInteraction->setActive( true );
        }
        for ( size_t mountIndex = 0; mountIndex < _listMount.size(); ++mountIndex )
        {
            MountMovementComponent* pMount = findMount( manager, static_cast<uint8>( mountIndex + 1u ) );
            if ( pMount == nullptr )
                continue;
            (void)pMount->setNetDriven( false ); // 끄기는 실패하지 않는다
            VehicleExitComponent* pExit = pMount->getOwner()->getComponent<VehicleExitComponent>();
            if ( pExit != nullptr )
                pExit->setActive( true );
        }
        _localAvatarID = 0;
        _role          = Role::None;
    }

    float32 HubPawnReplicationComponent::consumeRealSeconds()
    {
        const int64   now     = MonotonicClock::nowNanoseconds();
        const float32 seconds = _lastFrameNanoseconds != 0 ? static_cast<float32>( static_cast<float64>( now - _lastFrameNanoseconds ) / static_cast<float64>( constant::time::kNanosecondsPerSecond ) ) : 0.0f;
        _lastFrameNanoseconds = now;
        return seconds;
    }

    int32 HubPawnReplicationComponent::consumeTicks( float32 seconds )
    {
        _accumulator += seconds;
        const int32 elapsedTicks = static_cast<int32>( _accumulator / _tickInterval );
        _accumulator             = elapsedTicks > HubPawnReplicationComponentInternal::kMaxTicksPerFrame ? 0.0f : _accumulator - static_cast<float32>( elapsedTicks ) * _tickInterval;
        return MathUtil::min( elapsedTicks, HubPawnReplicationComponentInternal::kMaxTicksPerFrame );
    }

    // ------------------------------------------------------------------------------
    // 서버
    // ------------------------------------------------------------------------------
    void HubPawnReplicationComponent::runServerFrame( GameObjectManager& manager, const HubWorldComponent& hub, float32 seconds )
    {
        if ( hub.findServer() == nullptr )
            return;
        syncServerPawnList( manager, hub );
        syncServerMountList( manager );
        _server.accumulateInputBudget( seconds );
        ControlSystem* const pControl  = ControlSystem::find( manager );
        const int32          tickCount = consumeTicks( seconds );
        for ( int32 tickIndex = 0; tickIndex < tickCount; ++tickIndex )
        {
            ++_tick;
            if ( pControl != nullptr )
                pControl->publishServerTick( static_cast<int64>( _tick ) );
            _server.beginTick( _tick );
            for ( ServerPawn& pawn : _listServerPawn )
            {
                stepServerPawn( manager, hub, pawn );
            }
            publishPoses( manager );
            _server.endTick();
            _interest.updateRelevancy();
            _server.sendSnapshots();
        }
    }

    void HubPawnReplicationComponent::syncServerPawnList( GameObjectManager& manager, const HubWorldComponent& hub )
    {
        hub.findServer()->collectAvatars( _listSession );
        for ( ServerPawn& pawn : _listServerPawn )
        {
            pawn._bSeen = SW_FALSE;
            for ( const HubAvatarSession& session : _listSession )
            {
                if ( session._avatarID != pawn._avatarID || session._connectionID != pawn._connectionID )
                    continue;
                pawn._bSeen            = SW_TRUE;
                pawn._acceptedPosition = session._position;
            }
            if ( pawn._bSeen == SW_FALSE )
                removeServerPawn( manager, pawn );
        }
        VectorUtil::removeIf( _listServerPawn, []( const ServerPawn& pawn )
        { return pawn._bSeen == SW_FALSE; } );
        for ( const HubAvatarSession& session : _listSession )
        {
            bool bStanding = false;
            for ( const ServerPawn& pawn : _listServerPawn )
            {
                bStanding = bStanding || pawn._avatarID == session._avatarID;
            }
            if ( bStanding == false )
                spawnServerPawn( manager, session );
        }
    }

    void HubPawnReplicationComponent::spawnServerPawn( GameObjectManager& manager, const HubAvatarSession& session )
    {
        ServerPawn pawn;
        pawn._acceptedPosition = session._position;
        pawn._avatarID         = session._avatarID;
        pawn._connectionID     = session._connectionID;
        pawn._bSeen            = SW_TRUE;
        GameObject*    pObject = spawnPawnObject( manager, HubPawnReplicationComponentInternal::kServerPawnName, session._position );
        PawnComponent* pPawn   = pObject != nullptr ? pObject->getComponent<PawnComponent>() : nullptr;
        // 언리얼 서버가 접속마다 PlayerController 를 세워 폰을 쥐게 하는 자리다 — 소유 연결(`PawnComponent::findInputConnection`)이 여기서 잡힌다.
        RemoteControllerComponent* pRemote =
            pPawn != nullptr ? RemoteControllerComponent::spawnForConnection( manager, hashed_string( "HubRemoteController" ), session._connectionID, *pPawn ) : nullptr;
        if ( pRemote != nullptr )
        {
            pawn._object     = pObject->getHandle();
            pawn._controller = pRemote->getOwner()->getHandle();
            pRemote->getOwner()->setAlwaysLoaded( true );
        }
        // 세우지 못한 접속도 목록에 둔다 — 프레임마다 다시 시도해 같은 오류를 되풀이하지 않는다(오류는 위에서 알렸다). 그 접속은 나는 자리에 서 있기만 한다.
        _listServerPawn.push_back( pawn );
    }

    void HubPawnReplicationComponent::removeServerPawn( GameObjectManager& manager, const ServerPawn& pawn )
    {
        _interest.removeEntity( pawn._avatarID );
        _interest.removeObserver( pawn._connectionID );
        GameObject*    pObject = manager.resolveGameObject( pawn._object );
        PawnComponent* pPawn   = pObject != nullptr ? pObject->getComponent<PawnComponent>() : nullptr;
        if ( pPawn != nullptr )
            (void)VehicleSeatComponent::dismount( *pPawn, false ); // 타고 있지 않으면 할 일이 없다 — 말은 말 AI 에게 돌아간다
        GameObject* pController = manager.resolveGameObject( pawn._controller );
        if ( pController != nullptr )
            manager.destroyGameObject( pController );
        if ( pObject != nullptr )
            manager.destroyGameObject( pObject );
    }

    void HubPawnReplicationComponent::syncServerMountList( GameObjectManager& manager )
    {
        for ( size_t mountIndex = 0; mountIndex < _listMount.size(); ++mountIndex )
        {
            const uint8             mountNumber = static_cast<uint8>( mountIndex + 1u );
            MountMovementComponent* pMount      = findMount( manager, mountNumber );
            if ( pMount == nullptr )
                continue;
            bool bRidden = false;
            for ( const ServerPawn& pawn : _listServerPawn )
            {
                const GameObject* pObject = manager.resolveGameObject( pawn._object );
                bRidden                   = bRidden || ( pObject != nullptr && findMountNumber( manager, *pObject ) == mountNumber );
            }
            if ( pMount->isNetDriven() != bRidden )
                (void)pMount->setNetDriven( bRidden ); // 켤 수 없는 탈것이면 오류를 남기고 틱이 그대로 몬다
        }
    }

    void HubPawnReplicationComponent::stepServerPawn( GameObjectManager& manager, const HubWorldComponent& hub, ServerPawn& pawn )
    {
        using Internal      = HubPawnReplicationComponentInternal;
        GameObject* pObject = manager.resolveGameObject( pawn._object );
        if ( pObject == nullptr )
            return;
        const GameObject*          pControllerObject = manager.resolveGameObject( pawn._controller );
        RemoteControllerComponent* pRemote           = pControllerObject != nullptr ? pControllerObject->getComponent<RemoteControllerComponent>() : nullptr;
        HubPawnState               state             = captureState( manager, *pObject );
        HubPawnInput               input;
        optional<uint32>           lastTick{};
        optional<uint32>           inputTick = _server.popNextIntent( pawn._connectionID, input._intent, input._pawnEntityID, _listClaimScratch );
        while ( inputTick.has_value() )
        {
            lastTick            = inputTick;
            const bool bOwnPawn = input._pawnEntityID == makePawnEntityID( pawn._avatarID, state._mountNumber );
            if ( pRemote != nullptr )
                pRemote->receiveIntent( *inputTick, bOwnPawn ? input._intent : Internal::makeNeutralIntent( input._intent ) ); // 상호작용(타기 · 내리기)은 이 의도로 서버의 폰이 한다
            state = simulateTick( manager, hub, *pObject, state, input, pawn._avatarID, CharacterMotionPass::Live );
            if ( hub.findServer()->moveAvatar( pawn._avatarID, Internal::getPosition( state ), state._mountNumber != 0 ) == HubActionResult::Ok )
                pawn._acceptedPosition = Internal::getPosition( state );
            else
                Internal::placeAtRest( state, pawn._acceptedPosition ); // 이동기 밖에서 옮겨졌다(순간이동 · 발사) — 규칙이 받은 마지막 자리로 되돌린다
            applyState( manager, *pObject, state );
            inputTick = _server.popNextIntent( pawn._connectionID, input._intent, input._pawnEntityID, _listClaimScratch );
        }
        if ( lastTick.has_value() == false )
            return; // 입력이 오지 않았다 — 언리얼 서버처럼 원격 폰은 입력이 올 때까지 그 자리다
        if ( writeState( state, _bytesScratch ) )
            (void)_server.setOwnerState( pawn._connectionID, *lastTick, _bytesScratch ); // 상태는 상한(작은 고정 크기) 안이다
        else
            SW_LOG_ERROR_ID( "Hub.PawnStateOutOfRange", "[Hub] the pawn of avatar %# is outside the replicated range (%#, %#, %#) - not sent this tick", pawn._avatarID,
                             Internal::getPosition( state )._x, Internal::getPosition( state )._y, Internal::getPosition( state )._z );
    }

    void HubPawnReplicationComponent::publishPoses( GameObjectManager& manager )
    {
        for ( const ServerPawn& pawn : _listServerPawn )
        {
            const GameObject*                 pObject   = manager.resolveGameObject( pawn._object );
            const SceneComponent*             pRoot     = pObject != nullptr ? pObject->getPrimarySceneComponent() : nullptr;
            const CharacterMovementComponent* pMovement = pObject != nullptr ? pObject->getComponent<CharacterMovementComponent>() : nullptr;
            if ( pRoot == nullptr || pMovement == nullptr )
                continue;
            HubPawnPose pose;
            pose._mountNumber                    = findMountNumber( manager, *pObject );
            const MountMovementComponent* pMount = pose._mountNumber != 0 ? findMount( manager, pose._mountNumber ) : nullptr;
            pose._position                       = pMount != nullptr ? pRoot->getWorldPosition() : pMovement->getState()._motion._position;
            pose._yaw                            = MathUtil::wrapAngle( pMount != nullptr ? pMount->getFacingYaw() : pMovement->getFacingYaw() );
            if ( writePose( pose, _bytesScratch ) )
                _server.setEntity( pawn._avatarID, kPawnTypeID, _bytesScratch );
            _interest.setEntityPosition( pawn._avatarID, pose._position );
            _interest.setObserver( pawn._connectionID, pose._position );
        }
        for ( size_t mountIndex = 0; mountIndex < _listMount.size(); ++mountIndex )
        {
            const MountMovementComponent* pMount = findMount( manager, static_cast<uint8>( mountIndex + 1u ) );
            if ( pMount == nullptr )
                continue;
            const MountMoveState state    = pMount->getState();
            const uint32         entityID = kMountEntityIDBase + static_cast<uint32>( mountIndex );
            HubPawnPose          pose;
            pose._position = state._position;
            pose._yaw      = state._facingYaw;
            pose._gait     = state._gait;
            if ( writePose( pose, _bytesScratch ) )
                _server.setEntity( entityID, kMountTypeID, _bytesScratch );
            _interest.setEntityPosition( entityID, pose._position );
        }
    }

    // ------------------------------------------------------------------------------
    // 클라이언트
    // ------------------------------------------------------------------------------
    void HubPawnReplicationComponent::runClientFrame( GameObjectManager& manager, const HubWorldComponent& hub, float32 seconds )
    {
        const uint32 avatarID = hub.getClient().getAgent()._avatarID;
        if ( avatarID == 0 )
            return; // 서버가 아직 계정 진행을 읽는 중이다
        if ( _localAvatarID != avatarID && startPrediction( manager, avatarID ) == false )
            return;
        GameObject* pPawnObject = manager.resolveGameObject( _localPawn );
        if ( pPawnObject == nullptr )
            return;
        // 발동은 한 프레임만 참이다 — 입력 틱이 없던 프레임의 발동이 사라지지 않게 매 프레임 모은다.
        const uint8          mountNumber = findMountNumber( manager, *pPawnObject );
        const GameObject*    pControlled = mountNumber != 0 ? manager.resolveGameObject( _listMount[mountNumber - 1u] ) : pPawnObject;
        const PawnComponent* pPawn       = pControlled != nullptr ? pControlled->getComponent<PawnComponent>() : nullptr;
        if ( pPawn != nullptr )
            collectPendingTriggers( *pPawn, makePawnEntityID( _localAvatarID, mountNumber ) );
        const int32 tickCount = consumeTicks( seconds );
        for ( int32 tickIndex = 0; tickIndex < tickCount; ++tickIndex )
        {
            stepClient( manager, hub, *pPawnObject );
        }
        _client.update( seconds );
        _smoother.update( seconds );
        applyDisplay( manager, *pPawnObject );
        updateRemotePawnList( manager );
    }

    bool HubPawnReplicationComponent::startPrediction( GameObjectManager& manager, uint32 avatarID )
    {
        GameObject*                   pObject     = manager.resolveGameObject( _localPawn );
        PawnComponent*                pPawn       = pObject != nullptr ? pObject->getComponent<PawnComponent>() : nullptr;
        CharacterMovementComponent*   pMovement   = pObject != nullptr ? pObject->getComponent<CharacterMovementComponent>() : nullptr;
        CharacterControllerComponent* pController = pObject != nullptr ? pObject->getComponent<CharacterControllerComponent>() : nullptr;
        if ( pPawn == nullptr || pMovement == nullptr || pController == nullptr )
        {
            SW_LOG_ERROR_ONCE_ID( "Hub.LocalPawnMissing", "[Hub] Local Pawn has no pawn, character movement and controller - this client's pawn is not predicted" );
            return false;
        }
        (void)VehicleSeatComponent::dismount( *pPawn, false ); // 접속 전에 혼자 탄 말에서 내린다 — 타고 내리는 것은 이제 서버가 정한다
        if ( pController->getCharacterHandle().isValid() == false )
            return false; // 몸은 다음 물리 프레임에 선다
        pMovement->setNetDriven( true );
        MountInteractionComponent* pInteraction = pObject->getComponent<MountInteractionComponent>();
        if ( pInteraction != nullptr )
            pInteraction->setActive( false ); // 버튼은 의도로 서버의 폰에 닿는다
        for ( size_t mountIndex = 0; mountIndex < _listMount.size(); ++mountIndex )
        {
            MountMovementComponent* pMount = findMount( manager, static_cast<uint8>( mountIndex + 1u ) );
            if ( pMount == nullptr || pMount->setNetDriven( true ) == false )
                continue;
            VehicleExitComponent* pExit = pMount->getOwner()->getComponent<VehicleExitComponent>();
            if ( pExit != nullptr )
                pExit->setActive( false );
        }
        _prediction       = ClientPrediction<HubPawnState, HubPawnInput>{};
        _previousPosition = pController->getMotionState()._position;
        _localAvatarID    = avatarID; // 입력 틱은 이어 센다 — 줄이면 서버의 입력 창이 낡은 틱으로 본다
        SW_LOG_INFO_ID( "Hub.PawnPredicted", "[Hub] this client predicts the pawn of avatar %# (%# Hz)", avatarID, static_cast<int32>( MathUtil::round( 1.0f / _tickInterval ) ) );
        return true;
    }

    void HubPawnReplicationComponent::stepClient( GameObjectManager& manager, const HubWorldComponent& hub, GameObject& pawnObject )
    {
        using Internal = HubPawnReplicationComponentInternal;
        ++_tick;
        // 예측하는 이 입력 틱은 서버에서 (입력 틱 + 서버 차이) 틱쯤에 시뮬레이션된다 — 틱의 함수로 움직이는 바닥을 그 자리에 둔다.
        const optional<int64> serverTickOffset = _client.findServerTickOffset();
        ControlSystem* const  pControl         = ControlSystem::find( manager );
        if ( serverTickOffset.has_value() && pControl != nullptr )
            pControl->publishServerTick( static_cast<int64>( _tick ) + *serverTickOffset );

        HubPawnState         before      = captureState( manager, pawnObject );
        const uint32         entityID    = makePawnEntityID( _localAvatarID, before._mountNumber );
        const GameObject*    pControlled = before._mountNumber != 0 ? manager.resolveGameObject( _listMount[before._mountNumber - 1u] ) : &pawnObject;
        const PawnComponent* pPawn       = pControlled != nullptr ? pControlled->getComponent<PawnComponent>() : nullptr;
        const HubPawnInput   input       = pPawn != nullptr ? takeInput( *pPawn, entityID ) : HubPawnInput{};
        if ( _client.sendIntent( _tick, input._intent, {}, input._pawnEntityID ) == false )
            SW_LOG_WARNING_ID( "Hub.PawnInputNotSent", "[Hub] the intent of tick %# was not sent", _tick );
        _previousPosition    = Internal::getPosition( before );
        HubPawnState current = simulateTick( manager, hub, pawnObject, before, input, _localAvatarID, CharacterMotionPass::Live );
        if ( _prediction.record( _tick, input, current ) == false )
            SW_LOG_WARNING_ID( "Hub.PawnPredictionGap", "[Hub] prediction tick %# does not follow %# - not recorded", _tick, _prediction.getLatestTick() );

        // 서버가 새 소유자 상태(그 입력 틱까지 처리한 내 폰)를 보냈으면 예측을 맞춘다.
        const NetOwnerState* pOwner = _client.findOwnerState();
        HubPawnState         serverState;
        const bool           bNewOwnerState = pOwner != nullptr && pOwner->_bPresent == SW_TRUE && pOwner->_inputTick > _reconciledTick && readState( pOwner->_buffer, serverState );
        if ( bNewOwnerState )
        {
            _reconciledTick                        = pOwner->_inputTick;
            const float3                    shown  = Internal::getPosition( current );
            const PredictionReconcileResult result = _prediction.reconcile(
                pOwner->_inputTick, serverState, current,
                [this, &manager, &hub, &pawnObject]( const HubPawnState& state, const HubPawnInput& tickInput )
            { return simulateTick( manager, hub, pawnObject, state, tickInput, _localAvatarID, CharacterMotionPass::Replay ); },
                [this]( const HubPawnState& left, const HubPawnState& right )
            { return isClose( left, right, _reconcileTolerance ); } );
            if ( result == PredictionReconcileResult::TickOverwritten )
                current = serverState; // 예측이 기록보다 앞서 있다 — 서버 상태로 바로 맞춘다(남은 입력은 기록에 없다)
            const bool bCorrected = result == PredictionReconcileResult::Corrected || result == PredictionReconcileResult::TickOverwritten;
            if ( bCorrected )
            {
                _smoother.addCorrection( shown, Internal::getPosition( current ) ); // 보정한 만큼 화면에서 녹인다
                _previousPosition += Internal::getPosition( current ) - shown;
            }
        }
        syncLocalMount( manager, pawnObject, current._mountNumber );
        applyState( manager, pawnObject, current );
    }

    void HubPawnReplicationComponent::syncLocalMount( GameObjectManager& manager, GameObject& pawnObject, uint8 mountNumber )
    {
        PawnComponent* const pPawn = pawnObject.getComponent<PawnComponent>();
        if ( pPawn == nullptr || findMountNumber( manager, pawnObject ) == mountNumber )
            return;
        (void)VehicleSeatComponent::dismount( *pPawn, false ); // 앉아 있지 않으면 할 일이 없다. 자리는 뒤의 `applyState` 가 서버 상태로 정한다
        const MountMovementComponent* pMount = mountNumber != 0 ? findMount( manager, mountNumber ) : nullptr;
        if ( pMount == nullptr )
            return;
        // 말은 보간 지연만큼 과거에 그려진다 — 서버가 이미 태운 것이라 거리를 다시 보지 않게 말 곁에 둔다.
        SceneComponent* const pRoot = pawnObject.getPrimarySceneComponent();
        if ( pRoot != nullptr )
            pRoot->teleportTo( pMount->getState()._position );
        for ( VehicleSeatComponent* pSeat : manager.getComponentRegistry().getAll<VehicleSeatComponent>() )
        {
            if ( pSeat == nullptr || pSeat->getOwner() != pMount->getOwner() || pSeat->isDriverSeat() == false )
                continue;
            const MountResult result = VehicleSeatComponent::mount( *pPawn, *pSeat );
            if ( result != MountResult::Mounted )
                SW_LOG_ERROR_ID( "Hub.LocalMountFailed", "[Hub] the server mounted horse %# but this client could not (result %#)", static_cast<uint32>( mountNumber ), static_cast<int32>( result ) );
            return;
        }
    }

    void HubPawnReplicationComponent::collectPendingTriggers( const PawnComponent& controlledPawn, uint32 pawnEntityID )
    {
        if ( _pendingPawnEntityID != pawnEntityID )
        {
            // 폰이 바뀌었다 — 앞 폰 스키마의 발동을 새 폰의 버튼으로 보내지 않는다.
            _pendingPawnEntityID = pawnEntityID;
            _pendingTriggered    = 0;
            _bPendingTriggerLook = SW_FALSE;
        }
        const ControlIntent& intent = controlledPawn.getIntent();
        _pendingTriggered |= intent._buttonTriggered;
        if ( intent._bTriggerLook )
        {
            _bPendingTriggerLook = SW_TRUE;
            _pendingTriggerYaw   = intent._triggerYaw;
            _pendingTriggerPitch = intent._triggerPitch;
        }
    }

    HubPawnInput HubPawnReplicationComponent::takeInput( const PawnComponent& controlledPawn, uint32 pawnEntityID )
    {
        const bool   bSamePawn = _pendingPawnEntityID == pawnEntityID;
        HubPawnInput input;
        input._pawnEntityID            = pawnEntityID;
        input._intent                  = controlledPawn.getIntent();
        input._intent._buttonTriggered = bSamePawn ? _pendingTriggered : 0u;
        input._intent._bTriggerLook    = bSamePawn && _bPendingTriggerLook == SW_TRUE;
        input._intent._triggerYaw      = input._intent._bTriggerLook ? _pendingTriggerYaw : 0.0f;
        input._intent._triggerPitch    = input._intent._bTriggerLook ? _pendingTriggerPitch : 0.0f;
        _pendingTriggered              = 0;
        _bPendingTriggerLook           = SW_FALSE;
        return input;
    }

    void HubPawnReplicationComponent::applyDisplay( GameObjectManager& manager, GameObject& pawnObject )
    {
        const float32 alpha       = MathUtil::saturate( _accumulator / _tickInterval );
        const uint8   mountNumber = findMountNumber( manager, pawnObject );
        if ( mountNumber != 0 )
        {
            MountMovementComponent* const pMount = findMount( manager, mountNumber );
            if ( pMount == nullptr )
                return;
            const MountMoveState state = pMount->getState();
            pMount->setState( state, float3::lerp( _previousPosition, state._position, alpha ) + _smoother.getOffset() );
            return;
        }
        CharacterControllerComponent* const pController = pawnObject.getComponent<CharacterControllerComponent>();
        if ( pController != nullptr )
            pController->setMotionDisplay( _previousPosition, alpha, _smoother.getOffset() );
    }

    void HubPawnReplicationComponent::updateRemotePawnList( GameObjectManager& manager )
    {
        for ( RemotePawn& remote : _listRemotePawn )
        {
            remote._bSeen = SW_FALSE;
        }
        const GameObject* pLocal         = manager.resolveGameObject( _localPawn );
        const uint8       ownMountNumber = pLocal != nullptr ? findMountNumber( manager, *pLocal ) : 0;
        _listEntityScratch.clear();
        _client.collectVisibleEntities( _listEntityScratch );
        for ( const uint32 entityID : _listEntityScratch )
        {
            const NetEntityState* pFrom = nullptr;
            const NetEntityState* pTo   = nullptr;
            float32               alpha = 0.0f;
            HubPawnPose           from;
            HubPawnPose           to;
            const bool            bSampled = entityID != _localAvatarID && _client.sampleEntity( entityID, pFrom, pTo, alpha ) && readPose( pFrom->_buffer, from ) && readPose( pTo->_buffer, to );
            if ( bSampled == false )
                continue;
            const float3  position = float3::lerp( from._position, to._position, alpha );
            const float32 yaw      = MathUtil::wrapAngle( MathUtil::lerpAngle( from._yaw, to._yaw, alpha ) );
            if ( pTo->_typeID == kMountTypeID )
            {
                const uint32            mountIndex = entityID - kMountEntityIDBase;
                const bool              bListed    = mountIndex < static_cast<uint32>( _listMount.size() );
                MountMovementComponent* pMount     = bListed ? findMount( manager, static_cast<uint8>( mountIndex + 1u ) ) : nullptr;
                const bool              bOwnRide   = bListed && mountIndex + 1u == ownMountNumber;
                if ( pMount == nullptr || bOwnRide || pMount->isNetDriven() == false )
                    continue; // 내가 탄 말은 예측한다
                MountMoveState state = pMount->getState();
                state._position      = position;
                state._facingYaw     = yaw;
                state._gait          = to._gait;
                pMount->setState( state, position );
                continue;
            }
            if ( pTo->_typeID != kPawnTypeID )
                continue;
            RemotePawn* pRemote = nullptr;
            for ( RemotePawn& remote : _listRemotePawn )
            {
                if ( remote._entityID == entityID )
                    pRemote = &remote;
            }
            if ( pRemote == nullptr )
            {
                GameObject* pSpawned = spawnPawnObject( manager, HubPawnReplicationComponentInternal::kRemotePawnName, position );
                pRemote              = &_listRemotePawn.emplace_back(); // 세우지 못해도 목록에 둔다 — 프레임마다 다시 시도하지 않는다
                pRemote->_entityID   = entityID;
                pRemote->_object     = pSpawned != nullptr ? pSpawned->getHandle() : GameObjectHandle{};
                // 남의 폰은 서버의 폰이 타고 내린다 — 모습은 상호작용을 하지 않는다.
                MountInteractionComponent* pInteraction = pSpawned != nullptr ? pSpawned->getComponent<MountInteractionComponent>() : nullptr;
                if ( pInteraction != nullptr )
                    pInteraction->setActive( false );
            }
            pRemote->_bSeen                           = SW_TRUE;
            GameObject*                   pObject     = manager.resolveGameObject( pRemote->_object );
            SceneComponent*               pRoot       = pObject != nullptr ? pObject->getPrimarySceneComponent() : nullptr;
            CharacterMovementComponent*   pMovement   = pObject != nullptr ? pObject->getComponent<CharacterMovementComponent>() : nullptr;
            CharacterControllerComponent* pController = pObject != nullptr ? pObject->getComponent<CharacterControllerComponent>() : nullptr;
            if ( pRoot == nullptr || pMovement == nullptr || pController == nullptr )
                continue;
            pController->setActive( to._mountNumber == 0 ); // 탄 사람의 캡슐은 서버에도 없다(`VehicleSeatComponent::mount`)
            pRoot->setWorldPosition( position );            // 손으로 돌리는 몸이라 옮긴 자리에 캡슐만 따라간다(걸음은 돌지 않는다)
            if ( yaw != pMovement->getFacingYaw() )
                pMovement->setFacingYaw( yaw );
        }
        for ( const RemotePawn& remote : _listRemotePawn )
        {
            GameObject* pGone = remote._bSeen == SW_FALSE ? manager.resolveGameObject( remote._object ) : nullptr;
            if ( pGone != nullptr )
                manager.destroyGameObject( pGone );
        }
        VectorUtil::removeIf( _listRemotePawn, []( const RemotePawn& remote )
        { return remote._bSeen == SW_FALSE; } );
    }

    // ------------------------------------------------------------------------------
    // 공용
    // ------------------------------------------------------------------------------
    HubPawnState HubPawnReplicationComponent::simulateTick( GameObjectManager& manager, const HubWorldComponent& hub, GameObject& pawnObject, const HubPawnState& state, const HubPawnInput& input,
                                                            uint32 avatarID, CharacterMotionPass pass ) const
    {
        HubPawnState        next     = state;
        const bool          bOwnPawn = input._pawnEntityID == makePawnEntityID( avatarID, state._mountNumber );
        const ControlIntent intent   = bOwnPawn ? input._intent : HubPawnReplicationComponentInternal::makeNeutralIntent( input._intent );
        if ( state._mountNumber != 0 )
        {
            const MountMovementComponent* const pMount = findMount( manager, state._mountNumber );
            if ( pMount != nullptr )
                next._ride = pMount->simulate( state._ride, intent, _tickInterval );
            next._ride._position = hub.getRules().clampInside( next._ride._position );
            return next;
        }
        CharacterMovementComponent* const pMovement = pawnObject.getComponent<CharacterMovementComponent>();
        if ( pMovement != nullptr )
            next._walk = pMovement->simulate( state._walk, intent, _tickInterval, pass );
        next._walk._motion._position = hub.getRules().clampInside( next._walk._motion._position );
        return next;
    }

    HubPawnState HubPawnReplicationComponent::captureState( GameObjectManager& manager, const GameObject& pawnObject ) const
    {
        HubPawnState                            state;
        const CharacterMovementComponent* const pMovement = pawnObject.getComponent<CharacterMovementComponent>();
        state._mountNumber                                = findMountNumber( manager, pawnObject );
        if ( pMovement != nullptr )
            state._walk = pMovement->getState();
        const MountMovementComponent* const pMount = state._mountNumber != 0 ? findMount( manager, state._mountNumber ) : nullptr;
        if ( pMount != nullptr )
            state._ride = pMount->getState();
        return state;
    }

    void HubPawnReplicationComponent::applyState( GameObjectManager& manager, GameObject& pawnObject, const HubPawnState& state ) const
    {
        if ( state._mountNumber != 0 )
        {
            MountMovementComponent* const pMount = findMount( manager, state._mountNumber );
            if ( pMount != nullptr && pMount->isNetDriven() )
                pMount->setState( state._ride, state._ride._position );
            return;
        }
        CharacterMovementComponent* const pMovement = pawnObject.getComponent<CharacterMovementComponent>();
        if ( pMovement != nullptr && findMountNumber( manager, pawnObject ) == 0 )
            pMovement->setState( state._walk );
    }

    GameObject* HubPawnReplicationComponent::spawnPawnObject( GameObjectManager& manager, const utf8* pName, const float3& position ) const
    {
        AssetManager* const pAssetManager = game::findSubsystem<AssetManager>();
        GameObject* const   pObject       = pAssetManager != nullptr ? pAssetManager->getAssetCache<PrefabCache>().spawn( &manager, _pawnPrefab.path(), pName ) : nullptr;
        const GameObject*   pLocal        = manager.resolveGameObject( _localPawn );
        SceneComponent*     pRoot         = pObject != nullptr ? pObject->getPrimarySceneComponent() : nullptr;
        PawnComponent*      pPawn         = pObject != nullptr ? pObject->getComponent<PawnComponent>() : nullptr;
        // 튜닝은 씬의 플레이어 몸 하나가 원본이다 — 서버의 폰 · 남의 모습이 클라이언트가 예측하는 몸과 다른 걸음 · 캡슐을 갖지 않는다.
        CharacterMovementComponent*         pMovement         = pObject != nullptr ? pObject->getComponent<CharacterMovementComponent>() : nullptr;
        CharacterControllerComponent*       pController       = pObject != nullptr ? pObject->getComponent<CharacterControllerComponent>() : nullptr;
        const CharacterMovementComponent*   pSourceMovement   = pLocal != nullptr ? pLocal->getComponent<CharacterMovementComponent>() : nullptr;
        const CharacterControllerComponent* pSourceController = pLocal != nullptr ? pLocal->getComponent<CharacterControllerComponent>() : nullptr;
        const PawnComponent*                pSourcePawn       = pLocal != nullptr ? pLocal->getComponent<PawnComponent>() : nullptr;
        const bool                          bBody             = pRoot != nullptr && pPawn != nullptr && pMovement != nullptr && pController != nullptr;
        const bool                          bSource           = pSourceMovement != nullptr && pSourceController != nullptr && pSourcePawn != nullptr;
        // 버튼 비트는 스키마의 차례다 — 다르면 클라이언트가 보낸 상호작용을 서버가 다른 버튼으로 읽는다.
        if ( bBody == false || bSource == false || pPawn->getButtonNames() != pSourcePawn->getButtonNames() )
        {
            SW_LOG_ERROR_ID( "Hub.PawnPrefabInvalid", "[Hub] the pawn prefab '%#' needs a pawn with the buttons of Local Pawn, a character movement and a controller - the pawn is not spawned", _pawnPrefab.path().c_str() );
            if ( pObject != nullptr )
                manager.destroyGameObject( pObject );
            return nullptr;
        }
        pMovement->copyTuning( *pSourceMovement );
        pController->copyTuning( *pSourceController );
        pMovement->setNetDriven( true );
        pObject->setAlwaysLoaded( true ); // 존재는 접속 · 복제가 정한다 — 스트리밍 셀이 내리지 않는다
        pRoot->teleportTo( position );
        return pObject;
    }

    uint8 HubPawnReplicationComponent::findMountNumber( const GameObjectManager& manager, const GameObject& pawnObject ) const
    {
        for ( size_t mountIndex = 0; mountIndex < _listMount.size(); ++mountIndex )
        {
            const GameObject* pMountObject = manager.resolveGameObject( _listMount[mountIndex] );
            if ( pMountObject == nullptr )
                continue;
            for ( const VehicleSeatComponent* pSeat : manager.getComponentRegistry().getAll<VehicleSeatComponent>() )
            {
                const bool bDriving = pSeat != nullptr && pSeat->getOwner() == pMountObject && pSeat->isDriverSeat() && pSeat->getOccupant() == pawnObject.getHandle();
                if ( bDriving )
                    return static_cast<uint8>( mountIndex + 1u );
            }
        }
        return 0;
    }

    MountMovementComponent* HubPawnReplicationComponent::findMount( GameObjectManager& manager, uint8 mountNumber ) const
    {
        const size_t mountIndex = static_cast<size_t>( mountNumber ) - 1u;
        GameObject*  pObject    = mountNumber != 0 && mountIndex < _listMount.size() ? manager.resolveGameObject( _listMount[mountIndex] ) : nullptr;
        return pObject != nullptr ? pObject->getComponent<MountMovementComponent>() : nullptr;
    }

    bool HubPawnReplicationComponent::writeState( const HubPawnState& state, vector<uint8>& outBytes )
    {
        // 칸 선언이 곧 바이트다 — 스키마는 핸드셰이크가 확인했다(`PlaygroundGame::onRegisterReplicatedTypes`).
        BitWriter writer{ std::move( outBytes ) };
        writer.clear();
        const bool bWritten = ReplicatedLayout::writeFull( state, writer );
        outBytes            = writer.releaseBytes();
        return bWritten;
    }

    bool HubPawnReplicationComponent::readState( const vector<uint8>& bytes, HubPawnState& outState )
    {
        BitReader reader( bytes.data(), static_cast<int32>( bytes.size() ) );
        return ReplicatedLayout::readFull( reader, outState, nullptr ) == FixedReadResult::Ok;
    }

    bool HubPawnReplicationComponent::writePose( const HubPawnPose& pose, vector<uint8>& outBytes )
    {
        BitWriter writer{ std::move( outBytes ) };
        writer.clear();
        const bool bWritten = ReplicatedLayout::writeFull( pose, writer );
        outBytes            = writer.releaseBytes();
        return bWritten;
    }

    bool HubPawnReplicationComponent::readPose( const vector<uint8>& bytes, HubPawnPose& outPose )
    {
        BitReader reader( bytes.data(), static_cast<int32>( bytes.size() ) );
        return ReplicatedLayout::readFull( reader, outPose, nullptr ) == FixedReadResult::Ok;
    }

    bool HubPawnReplicationComponent::isClose( const HubPawnState& left, const HubPawnState& right, float32 tolerance )
    {
        if ( left._mountNumber != right._mountNumber )
            return false;
        const float3 delta = HubPawnReplicationComponentInternal::getPosition( left ) - HubPawnReplicationComponentInternal::getPosition( right );
        if ( delta.getLength() > tolerance )
            return false;
        if ( left._mountNumber != 0 )
            return left._ride._gait == right._ride._gait && left._ride._bGallopAllowed == right._ride._bGallopAllowed && left._ride._maxGait == right._ride._maxGait;
        return left._walk._motion._movementMode == right._walk._motion._movementMode && left._walk._bClimbing == right._walk._bClimbing;
    }

    void HubPawnReplicationComponent::onRegister( GameObjectManager& manager )
    {
        Component::onRegister( manager );
        manager.getComponentRegistry().add<HubPawnReplicationComponent>( this );
    }

    void HubPawnReplicationComponent::onUnregister( GameObjectManager& manager )
    {
        manager.getComponentRegistry().remove<HubPawnReplicationComponent>( this );
        Component::onUnregister( manager );
    }
} // namespace sw

namespace sw
{
    SW_AUTOMATION_GETTER_WATCH( hubPawnPredicted, "Hub.PawnPredicted", "1 while this client predicts its own pawn", HubPawnReplicationComponent, &HubPawnReplicationComponent::getPredictedValue );
    SW_AUTOMATION_GETTER_WATCH( hubPawnCorrections, "Hub.PawnCorrections", "Times the server state rewound the predicted pawn", HubPawnReplicationComponent,
                                &HubPawnReplicationComponent::getCorrectionCount );
} // namespace sw
