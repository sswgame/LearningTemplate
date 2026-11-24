#include "pch.h"

#include "Games/Playground/Hub/HubPawnReplicationComponent.h"

#include "Core/Algorithm/VectorUtil.h"
#include "Core/Foundation/Constants.h"
#include "Core/Log/LogOnce.h"
#include "Core/Math/MathUtil.h"
#include "Core/Network/Connection/NetEndpoint.h"
#include "Core/Serialization/BitStream.h"
#include "Core/String/StringBuilder.h"
#include "Core/Time/MonotonicClock.h"

#include "Engine/Asset/AssetManager.h"
#include "Engine/Automation/Watch/AutomationComponentWatch.h"
#include "Engine/Automation/Watch/AutomationWatch.h"
#include "Engine/Console/DevCommandRegistry.h"
#include "Engine/Foundation/WorldOrigin.h"
#include "Engine/Object/Component/SceneComponent.h"
#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManager.h"
#include "Engine/Object/GameObject/GameObjectManager.inl"
#include "Engine/Object/GameObject/GameObjectManagerResolve.inl"
#include "Engine/Object/GameObject/Registry/ComponentRegistry.h"
#include "Engine/Object/GameObject/Service/SceneNetLink.h"
#include "Engine/Object/GameObject/Tick/TickContext.h"
#include "Engine/Object/Prefab/PrefabCache.h"
#include "Engine/Physics/Component/CharacterControllerComponent.h"
#include "Engine/Physics/Component/RigidBodyComponent.h"
#include "Engine/Scene/Scene.h"
#include "Engine/Scene/SceneManager.h"
#include "Engine/Serialization/Fixed/ReplicatedLayout.h"

#include "GameFramework/Runtime/Character/Control/ControlSystem.h"
#include "GameFramework/Runtime/Character/Control/Controller/RemoteControllerComponent.h"
#include "GameFramework/Runtime/Character/Control/Pawn/PawnComponent.h"
#include "GameFramework/Runtime/Foundation/Minigame/MinigameFlow.h"
#include "GameFramework/Runtime/Foundation/Service/GameService.h"
#include "GameFramework/Runtime/Gameplay/Gimmick/Movement/InterpToMovementComponent.h"
#include "GameFramework/Runtime/Gameplay/Gimmick/Movement/TickedFloorStep.h"
#include "GameFramework/Runtime/Gameplay/Vehicle/VehicleSeatComponent.h"

#include "Games/Playground/Hub/HubWorldComponent.h"

namespace sw
{
    namespace
    {
        static_assert( HubPawnReplicationComponent::kMaxFloor == HubPawnReplicationComponent::kMaxMount * 2u, "HubPawnPose::_floorNumber 의 Max 를 kMaxFloor 와 맞춘다" );
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
                neutral._viewTick     = intent._viewTick;
                return neutral;
            }

            static AbsolutePosition getPosition( const HubPawnState& state ) { return AbsolutePosition{ state._mountNumber != 0 ? state._ride._position : state._walk._motion._position }; }

            /** @brief 자리를 @p position 에 두고 이어 가던 운동을 지웁니다(규칙이 받지 않은 걸음을 되돌린다). */
            static void placeAtRest( HubPawnState& inoutState, const AbsolutePosition& position )
            {
                if ( inoutState._mountNumber != 0 )
                {
                    inoutState._ride._position     = position._value;
                    inoutState._ride._forwardSpeed = 0.0f;
                    return;
                }
                CharacterMotionState rest;
                rest._position             = position._value;
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
        , _spawnSearchRadius{ 3.0f }
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
        , _pFloorStep{ nullptr }
        , _floorLocalPosition{}
        , _floorSlip{ 0.0f }
        , _floorTickOffset{ 0 }
        , _previousPosition{}
        , _lastFrameNanoseconds{ 0 }
        , _accumulator{ 0.0f }
        , _listPendingTrigger{}
        , _pendingPawnEntityID{ 0 }
        , _stride{ 1 }
        , _tick{ 0 }
        , _reconciledTick{ 0 }
        , _localAvatarID{ 0 }
        , _role{ Role::None }
        , _floorNumber{ 0 }
        , _bFloorTickKnown{ SW_FALSE }
        , _bStartTried{ SW_FALSE }
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
        if ( _role != Role::None && _routerSubscription.isActive() == false )
            stopRole( manager ); // 틱 사이에 세션이 닫히고 새로 열렸다 — 새 세션에 다시 건다
        if ( _role == Role::None )
            startRole( *pLink, *pHub );
        // 명령마다 바닥을 그 명령의 틱 자세에 둔다 — 프레임이 끝나면 물리 바디를 되돌린다.
        TickedFloorStep floorStep{ manager, _tickInterval };
        _pFloorStep = &floorStep;
        if ( _role == Role::Server )
            runServerFrame( manager, *pHub, *pLink, seconds );
        else if ( _role == Role::Client )
            runClientFrame( manager, *pHub, seconds );
        _pFloorStep = nullptr;
    }

    void HubPawnReplicationComponent::startRole( const NetLinkValue& link, const HubWorldComponent& hub )
    {
        const bool bServer   = link._role == NetLinkRole::Server;
        const bool bHubReady = bServer ? hub.findServer() != nullptr : hub.getClient().isActive();
        if ( _bStartTried == SW_TRUE || bHubReady == false )
            return; // 티켓 없는 서버 세션에는 허브 서버가 없다 — 폰도 세우지 않는다
        _bStartTried                  = SW_TRUE;
        const optional<uint32> stride = NetClock::computeStride( _tickInterval );
        if ( stride.has_value() == false )
        {
            SW_LOG_ERROR_ID( "Hub.PawnTickIntervalNotMultiple", "[Hub] pawn replication tick interval %# s is not a multiple of the server tick (%# s) - pawns stay local", _tickInterval, NetClock::kTickSeconds );
            return;
        }
        _stride = *stride;
        PredictionSmootherSettings smoothing;
        smoothing._halfLifeSeconds = _smoothingHalfLife;
        smoothing._snapDistance    = _smoothingSnapDistance;
        if ( _listMount.size() > kMaxMount || _listFloor.size() > kMaxFloor || _smoother.initialize( smoothing ) == false )
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
            if ( _client.initialize( link._pPeers, link._pRouter->getWorldClock(), settings ) )
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
        _tick = link._pRouter->getWorldClock().getWholeTick( _stride );
        // 서버 씬의 플레이어 오브젝트는 아무도 조종하지 않는다 — 나는 자리에 선 캡슐이 접속한 폰을 막지 않게 물리 몸만 멈춘다(튜닝의 원본으로는 남는다).
        const GameObjectManager*      pManager    = getOwner()->getManager();
        const GameObject*             pLocal      = pManager != nullptr ? pManager->resolveGameObject( _localPawn ) : nullptr;
        CharacterControllerComponent* pController = pLocal != nullptr ? pLocal->getComponent<CharacterControllerComponent>() : nullptr;
        if ( pController != nullptr )
            _localBodySuspension = pController->suspend();
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
        _localBodySuspension.reset();
        if ( pLocal != nullptr && _localAvatarID != 0 )
        {
            CharacterMovementComponent* pMovement = pLocal->getComponent<CharacterMovementComponent>();
            if ( pMovement != nullptr )
                pMovement->setNetDriven( false );
        }
        for ( size_t mountIndex = 0; mountIndex < _listMount.size(); ++mountIndex )
        {
            MountMovementComponent* pMount = findMount( manager, static_cast<uint8>( mountIndex + 1u ) );
            if ( pMount != nullptr )
                (void)pMount->setNetDriven( false ); // 끄기는 실패하지 않는다
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
    void HubPawnReplicationComponent::runServerFrame( GameObjectManager& manager, const HubWorldComponent& hub, const NetLinkValue& link, float32 seconds )
    {
        if ( hub.findServer() == nullptr )
            return;
        syncServerPawnList( manager, hub );
        syncServerMountList( manager );
        _server.accumulateInputBudget( seconds );
        // 월드의 서버 시계가 지난 이 컴포넌트의 틱을 차례로 돈다 — 번호를 따로 세지 않는다. 바닥의 자세도 같은 시계다.
        const uint32 presentTick = link._pRouter->getWorldClock().getWholeTick( _stride );
        while ( _tick < presentTick )
        {
            ++_tick;
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
            if ( bStanding )
                continue;
            spawnServerPawn( manager, hub, session );
            return; // 프레임마다 하나 — 막 세운 폰의 캡슐은 이번 물리 프레임에 서므로, 다음 폰은 그것을 보고 빈 자리를 고른다
        }
    }

    void HubPawnReplicationComponent::spawnServerPawn( GameObjectManager& manager, const HubWorldComponent& hub, const HubAvatarSession& session )
    {
        ServerPawn pawn;
        pawn._acceptedPosition     = session._position;
        pawn._avatarID             = session._avatarID;
        pawn._connectionID         = session._connectionID;
        pawn._bSeen                = SW_TRUE;
        const WorldOrigin& origin  = manager.getWorldOrigin();
        GameObject*        pObject = spawnPawnObject( manager, HubPawnReplicationComponentInternal::kServerPawnName, origin.toLocal( session._position ) );
        PawnComponent*     pPawn   = pObject != nullptr ? pObject->getComponent<PawnComponent>() : nullptr;
        // 언리얼 서버가 접속마다 PlayerController 를 세워 폰을 쥐게 하는 자리다 — 소유 연결(`PawnComponent::findInputConnection`)이 여기서 잡힌다.
        RemoteControllerComponent* pRemote =
            pPawn != nullptr ? RemoteControllerComponent::spawnForConnection( manager, hashed_string( "HubRemoteController" ), session._connectionID, *pPawn ) : nullptr;
        if ( pRemote != nullptr )
        {
            pawn._object     = pObject->getHandle();
            pawn._controller = pRemote->getOwner()->getHandle();
            pRemote->getOwner()->setAlwaysLoaded( true );
            // 저장된 자리 · 시작 자리에 다른 몸이 서 있으면 곁의 빈 자리에 세운다(언리얼 `FindTeleportSpot`). 빈 자리가 없거나 규칙이 받지 않으면 그 자리 그대로다(서로 밀려난다).
            const optional<float3> spot = pObject->getComponent<CharacterControllerComponent>()->findFreeSpot( origin.toLocal( session._position ), _spawnSearchRadius );
            if ( spot.has_value() && hub.findServer()->moveAvatar( session._avatarID, AbsolutePosition{ origin.toAbsolute( *spot ) }, 0.0f, false ) == HubActionResult::Ok )
            {
                pawn._acceptedPosition = AbsolutePosition{ origin.toAbsolute( *spot ) };
                pObject->getPrimarySceneComponent()->teleportTo( *spot );
            }
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
        state._floorTickOffset                       = pawn._floorTickOffset;
        state._bFloorTickKnown                       = pawn._bFloorTickKnown;
        HubPawnInput     input;
        optional<uint32> lastTick{};
        optional<uint32> inputTick = _server.popNextIntent( pawn._connectionID, input._intent, input._pawnEntityID, _listClaimScratch );
        while ( inputTick.has_value() )
        {
            lastTick         = inputTick;
            input._inputTick = *inputTick;
            TickedFloorOffset::update( _tick, *inputTick, state._floorTickOffset, state._bFloorTickKnown );
            pawn._floorTickOffset = state._floorTickOffset;
            pawn._bFloorTickKnown = state._bFloorTickKnown;
            pawn._floorTick       = TickedFloorOffset::findTick( state._floorTickOffset, state._bFloorTickKnown, *inputTick ).value_or( 0 );
            const bool bOwnPawn   = input._pawnEntityID == makePawnEntityID( pawn._avatarID, state._mountNumber );
            if ( pRemote != nullptr )
                pRemote->receiveIntent( *inputTick, bOwnPawn ? input._intent : Internal::makeNeutralIntent( input._intent ) ); // 상호작용(타기 · 내리기)은 이 의도로 서버의 폰이 한다
            state = simulateTick( manager, hub, *pObject, state, input, pawn._avatarID, CharacterMotionPass::Live );
            if ( hub.findServer()->moveAvatar( pawn._avatarID, Internal::getPosition( state ), _tickInterval, state._mountNumber != 0 ) == HubActionResult::Ok )
                pawn._acceptedPosition = Internal::getPosition( state );
            else
            {
                Internal::placeAtRest( state, pawn._acceptedPosition ); // 이동기 밖에서 옮겨졌다(순간이동 · 발사) — 규칙이 받은 마지막 자리로 되돌린다
                ++pawn._teleportCount;
            }
            applyState( manager, *pObject, state );
            inputTick = _server.popNextIntent( pawn._connectionID, input._intent, input._pawnEntityID, _listClaimScratch );
        }
        if ( lastTick.has_value() == false )
            return; // 입력이 오지 않았다 — 언리얼 서버처럼 원격 폰은 입력이 올 때까지 그 자리다
        if ( writeState( state, _bytesScratch ) )
            (void)_server.setOwnerState( pawn._connectionID, *lastTick, _bytesScratch ); // 상태는 상한(작은 고정 크기) 안이다
        else
            SW_LOG_ERROR_ID( "Hub.PawnStateOutOfRange", "[Hub] the pawn of avatar %# is outside the replicated range (%#, %#, %#) - not sent this tick", pawn._avatarID,
                             Internal::getPosition( state )._value._x, Internal::getPosition( state )._value._y, Internal::getPosition( state )._value._z );
    }

    void HubPawnReplicationComponent::publishPoses( GameObjectManager& manager )
    {
        const WorldOrigin& origin = manager.getWorldOrigin();
        for ( ServerPawn& pawn : _listServerPawn )
        {
            const GameObject*                 pObject   = manager.resolveGameObject( pawn._object );
            const SceneComponent*             pRoot     = pObject != nullptr ? pObject->getPrimarySceneComponent() : nullptr;
            const CharacterMovementComponent* pMovement = pObject != nullptr ? pObject->getComponent<CharacterMovementComponent>() : nullptr;
            if ( pRoot == nullptr || pMovement == nullptr )
                continue;
            HubPawnPose pose;
            pose._mountNumber = findMountNumber( manager, *pObject );
            if ( pose._mountNumber != pawn._mountNumber )
                ++pawn._teleportCount; // 타고 내림은 자리를 옮긴다 — 선 자리와 안장을 잇지 않는다
            pawn._mountNumber                    = pose._mountNumber;
            pose._teleportCount                  = pawn._teleportCount;
            const MountMovementComponent* pMount = pose._mountNumber != 0 ? findMount( manager, pose._mountNumber ) : nullptr;
            pose._position                       = origin.toAbsolute( pMount != nullptr ? pRoot->getWorldPosition() : pMovement->getState()._motion._position );
            pose._yaw                            = MathUtil::wrapAngle( pMount != nullptr ? pMount->getFacingYaw() : pMovement->getFacingYaw() );
            _interest.setEntityPosition( pawn._avatarID, AbsolutePosition{ pose._position } );
            _interest.setObserver( pawn._connectionID, AbsolutePosition{ pose._position } );
            // 움직이는 바닥 위의 폰은 바닥 기준으로 싣는다 — 받는 쪽의 바닥은 다른 틱(예측)에 있어 절대 자리면 바닥에서 떨어져 보인다(언리얼 `FBasedMovementInfo`).
            float3      floorPosition;
            quaternion  floorRotation;
            const uint8 floorNumber = pose._mountNumber == 0 ? findFloorNumber( manager, *pObject ) : uint8{ 0 };
            if ( computeFloorPose( manager, floorNumber, static_cast<float64>( pawn._floorTick ), floorPosition, floorRotation ) )
            {
                const quaternion toFloor = std::as_const( floorRotation ).inverse();
                pose._position           = float3::transform( origin.toLocal( pose._position ) - floorPosition, toFloor );
                pose._yaw                = MathUtil::wrapAngle( pose._yaw - floorRotation.getEulerAngles()._y );
                pose._floorNumber        = floorNumber;
            }
            if ( writePose( pose, _bytesScratch ) )
                _server.setEntity( pawn._avatarID, kPawnTypeID, _bytesScratch );
        }
        for ( size_t mountIndex = 0; mountIndex < _listMount.size(); ++mountIndex )
        {
            const MountMovementComponent* pMount = findMount( manager, static_cast<uint8>( mountIndex + 1u ) );
            if ( pMount == nullptr )
                continue;
            const MountMoveState state    = pMount->getState();
            const uint32         entityID = kMountEntityIDBase + static_cast<uint32>( mountIndex );
            HubPawnPose          pose;
            pose._position = origin.toAbsolute( state._position );
            pose._yaw      = state._facingYaw;
            pose._gait     = state._gait;
            if ( writePose( pose, _bytesScratch ) )
                _server.setEntity( entityID, kMountTypeID, _bytesScratch );
            _interest.setEntityPosition( entityID, AbsolutePosition{ pose._position } );
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
        _smoother.update( seconds );
        applyDisplay( manager, *pPawnObject );
        updateRemotePawnList( manager );
        // 이번 프레임에 그린 시각이다 — 다음 프레임의 입력은 이 화면을 보고 낸 것이다.
        ControlSystem* const pControl = ControlSystem::find( manager );
        if ( pControl != nullptr )
            pControl->publishViewTick( _client.getViewTick() );
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
        const MinigameFlow* const pMinigames = game::findSubsystem<MinigameFlow>();
        if ( pMinigames != nullptr && pMinigames->isServerWorldReady() == false )
            return false; // 서버가 들어간 하위 레벨이 아직 다 서지 않았다 — 서버에는 있는 바닥 · 벽 없이 걷지 않는다
        pMovement->setNetDriven( true );
        for ( size_t mountIndex = 0; mountIndex < _listMount.size(); ++mountIndex )
        {
            MountMovementComponent* pMount = findMount( manager, static_cast<uint8>( mountIndex + 1u ) );
            if ( pMount != nullptr )
                (void)pMount->setNetDriven( true ); // 켤 수 없는 탈것이면 오류를 남기고 틱이 그대로 몬다
        }
        _prediction       = ClientPrediction<HubPawnState, HubPawnInput>{};
        _previousPosition = AbsolutePosition{ manager.getWorldOrigin().toAbsolute( pController->getMotionState()._position ) };
        _localAvatarID    = avatarID; // 입력 틱은 이어 센다 — 줄이면 서버의 입력 창이 낡은 틱으로 본다
        SW_LOG_INFO_ID( "Hub.PawnPredicted", "[Hub] this client predicts the pawn of avatar %# (%# Hz)", avatarID, static_cast<int32>( MathUtil::round( 1.0f / _tickInterval ) ) );
        return true;
    }

    void HubPawnReplicationComponent::stepClient( GameObjectManager& manager, const HubWorldComponent& hub, GameObject& pawnObject )
    {
        using Internal = HubPawnReplicationComponentInternal;
        ++_tick;
        // 이 입력 틱의 바닥은 서버가 정해 준 차이만큼 앞의 틱이다(소유자 상태) — 서버가 그 명령을 푸는 바닥과 같다. 물리 프레임의 바닥도 그 틱을 따른다.
        ControlSystem* const pControl = ControlSystem::find( manager );
        if ( _bFloorTickKnown == SW_TRUE && pControl != nullptr )
            publishFloorTick( *pControl, static_cast<int64>( _tick ) + _floorTickOffset );

        HubPawnState before              = captureState( manager, pawnObject );
        before._floorTickOffset          = _floorTickOffset;
        before._bFloorTickKnown          = _bFloorTickKnown;
        const uint32         entityID    = makePawnEntityID( _localAvatarID, before._mountNumber );
        const GameObject*    pControlled = before._mountNumber != 0 ? manager.resolveGameObject( _listMount[before._mountNumber - 1u] ) : &pawnObject;
        const PawnComponent* pPawn       = pControlled != nullptr ? pControlled->getComponent<PawnComponent>() : nullptr;
        HubPawnInput         input       = pPawn != nullptr ? takeInput( *pPawn, entityID ) : HubPawnInput{};
        input._inputTick                 = _tick;
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
            _floorTickOffset                       = serverState._floorTickOffset;
            _bFloorTickKnown                       = serverState._bFloorTickKnown;
            const float3                    shown  = Internal::getPosition( current )._value;
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
                _smoother.addCorrection( shown, Internal::getPosition( current )._value ); // 보정한 만큼 화면에서 녹인다
                _previousPosition = _previousPosition + ( Internal::getPosition( current )._value - shown );
            }
        }
        syncLocalMount( manager, pawnObject, current._mountNumber );
        applyState( manager, pawnObject, current );
        updateFloorSlip( manager, pawnObject, input._intent._move.getLengthSquared() > 0.0f );
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
            _listPendingTrigger.clear();
        }
        // 줄에는 상한이 있다 — 입력 틱이 오래 멈춘 동안(끊김)의 누름을 끝없이 쌓지 않는다.
        constexpr size_t kMaxPendingTrigger = static_cast<size_t>( ControlIntent::kMaxTriggers ) * 4u;
        for ( const ControlTrigger& trigger : controlledPawn.getIntent()._listTrigger )
        {
            if ( _listPendingTrigger.size() < kMaxPendingTrigger )
                _listPendingTrigger.push_back( trigger );
        }
    }

    HubPawnInput HubPawnReplicationComponent::takeInput( const PawnComponent& controlledPawn, uint32 pawnEntityID )
    {
        const bool   bSamePawn = _pendingPawnEntityID == pawnEntityID;
        HubPawnInput input;
        input._pawnEntityID = pawnEntityID;
        input._intent       = controlledPawn.getIntent();
        input._intent._listTrigger.clear();
        if ( bSamePawn == false )
            return input;
        // 선에 싣는 상한을 넘는 사건은 다음 입력 틱이 싣는다(사건마다 보던 시각을 든다).
        const size_t triggerCount = MathUtil::min( _listPendingTrigger.size(), static_cast<size_t>( ControlIntent::kMaxTriggers ) );
        input._intent._listTrigger.assign( _listPendingTrigger.begin(), _listPendingTrigger.begin() + static_cast<std::ptrdiff_t>( triggerCount ) );
        _listPendingTrigger.erase( _listPendingTrigger.begin(), _listPendingTrigger.begin() + static_cast<std::ptrdiff_t>( triggerCount ) );
        return input;
    }

    void HubPawnReplicationComponent::applyDisplay( GameObjectManager& manager, GameObject& pawnObject )
    {
        for ( const GameObjectHandle floor : _listFloor )
        {
            GameObject*                pFloorObject = _bFloorTickKnown == SW_TRUE ? manager.resolveGameObject( floor ) : nullptr;
            InterpToMovementComponent* pFloor       = pFloorObject != nullptr ? pFloorObject->getComponent<InterpToMovementComponent>() : nullptr;
            if ( pFloor != nullptr )
                pFloor->applySeconds( computeDisplayFloorTick() * static_cast<float64>( _tickInterval ) ); // 위에 선 자기 폰과 같은 시각에 그린다
        }
        const float32 alpha       = MathUtil::saturate( _accumulator / _tickInterval );
        const float3  previous    = manager.getWorldOrigin().toLocal( _previousPosition ); // 틱 사이에 원점이 옮겨져도 지금 씬의 자리다
        const uint8   mountNumber = findMountNumber( manager, pawnObject );
        if ( mountNumber != 0 )
        {
            MountMovementComponent* const pMount = findMount( manager, mountNumber );
            if ( pMount == nullptr )
                return;
            const MountMoveState state = pMount->getState();
            pMount->setState( state, float3::lerp( previous, state._position, alpha ) + _smoother.getOffset() );
            return;
        }
        CharacterControllerComponent* const pController = pawnObject.getComponent<CharacterControllerComponent>();
        if ( pController != nullptr )
            pController->setMotionDisplay( previous, alpha, _smoother.getOffset() );
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
            if ( entityID == _localAvatarID )
                continue;
            const bool                                  bMount = entityID >= kMountEntityIDBase && entityID - kMountEntityIDBase < kMaxMount;
            const optional<NetStateSample<HubPawnPose>> sample = _client.sampleState<HubPawnPose>( entityID, bMount ? kMountTypeID : kPawnTypeID );
            if ( sample.has_value() == false )
                continue;
            const HubPawnPose& from  = sample->_from;
            const HubPawnPose& to    = sample->_to;
            NetSampleRatio     ratio = sample->_ratio;
            ratio.breakAt( from._floorNumber, to._floorNumber ); // 바닥을 옮겨 탄 두 자리는 기준이 다르다
            float3      position    = ratio.interpolate( from._position, to._position );
            float32     yaw         = ratio.interpolateAngle( from._yaw, to._yaw );
            const uint8 floorNumber = ratio.choose( from._floorNumber, to._floorNumber );
            float3      floorPosition;
            quaternion  floorRotation;
            // 바닥 위의 남은 이 화면의 바닥에 얹는다 — 바닥은 예측 시각에 그려진다.
            if ( computeFloorPose( manager, floorNumber, computeDisplayFloorTick(), floorPosition, floorRotation ) )
            {
                position = floorPosition + float3::transform( position, floorRotation );
                yaw      = MathUtil::wrapAngle( yaw + floorRotation.getEulerAngles()._y );
            }
            else if ( floorNumber == 0 )
                position = manager.getWorldOrigin().toLocal( position );
            else
                continue; // 이 씬에 없는 바닥이다 — 데이터가 어긋났다(번호는 씬의 같은 목록이다)
            if ( bMount )
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
                state._gait          = ratio.choose( from._gait, to._gait );
                pMount->setState( state, position );
                continue;
            }
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
            }
            pRemote->_floorNumber                     = floorNumber;
            pRemote->_floorLocalPosition              = floorNumber != 0 ? float3::transform( position - floorPosition, std::as_const( floorRotation ).inverse() ) : float3{};
            pRemote->_bSeen                           = SW_TRUE;
            GameObject*                   pObject     = manager.resolveGameObject( pRemote->_object );
            SceneComponent*               pRoot       = pObject != nullptr ? pObject->getPrimarySceneComponent() : nullptr;
            CharacterMovementComponent*   pMovement   = pObject != nullptr ? pObject->getComponent<CharacterMovementComponent>() : nullptr;
            CharacterControllerComponent* pController = pObject != nullptr ? pObject->getComponent<CharacterControllerComponent>() : nullptr;
            if ( pRoot == nullptr || pMovement == nullptr || pController == nullptr )
                continue;
            // 탄 사람의 캡슐은 서버에도 없다(`VehicleSeatComponent::mount`).
            if ( ratio.choose( from._mountNumber, to._mountNumber ) == 0 )
                pRemote->_seatSuspension.reset();
            else if ( pRemote->_seatSuspension.isActive() == false )
                pRemote->_seatSuspension = pController->suspend();
            pRoot->setWorldPosition( position ); // 손으로 돌리는 몸이라 옮긴 자리에 캡슐만 따라간다(걸음은 돌지 않는다)
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
        const WorldOrigin&    origin    = manager.getWorldOrigin();
        const optional<int64> floorTick = TickedFloorOffset::findTick( state._floorTickOffset, state._bFloorTickKnown, input._inputTick );
        if ( floorTick.has_value() && _pFloorStep != nullptr )
            _pFloorStep->beginTick( *floorTick );
        const HubPawnState  local    = toLocalState( origin, state );
        HubPawnState        next     = local;
        const bool          bOwnPawn = input._pawnEntityID == makePawnEntityID( avatarID, state._mountNumber );
        const ControlIntent intent   = bOwnPawn ? input._intent : HubPawnReplicationComponentInternal::makeNeutralIntent( input._intent );
        if ( state._mountNumber != 0 )
        {
            const MountMovementComponent* const pMount = findMount( manager, state._mountNumber );
            if ( pMount != nullptr )
                next._ride = pMount->simulate( local._ride, intent, _tickInterval );
            next                 = toAbsoluteState( origin, next );
            next._ride._position = hub.getRules().clampInside( AbsolutePosition{ next._ride._position } )._value;
            if ( floorTick.has_value() && _pFloorStep != nullptr )
                _pFloorStep->endTick( *floorTick );
            return next;
        }
        CharacterMovementComponent* const pMovement = pawnObject.getComponent<CharacterMovementComponent>();
        if ( pMovement != nullptr )
            next._walk = pMovement->simulate( local._walk, intent, _tickInterval, pass );
        next                         = toAbsoluteState( origin, next );
        next._walk._motion._position = hub.getRules().clampInside( AbsolutePosition{ next._walk._motion._position } )._value;
        if ( floorTick.has_value() && _pFloorStep != nullptr )
            _pFloorStep->endTick( *floorTick );
        return next;
    }

    void HubPawnReplicationComponent::publishFloorTick( ControlSystem& control, int64 netTick ) const
    {
        control.publishServerTick( netTick * static_cast<int64>( _stride ) ); // 씬의 바닥은 월드의 서버 틱으로 돈다
    }

    float64 HubPawnReplicationComponent::computeDisplayFloorTick() const
    {
        // 자기 폰은 지난 틱과 이 틱 사이를 그린다(`applyDisplay`) — 바닥도 같은 사이에 둔다.
        return static_cast<float64>( static_cast<int64>( _tick ) + _floorTickOffset ) - 1.0 + static_cast<float64>( MathUtil::saturate( _accumulator / _tickInterval ) );
    }

    uint8 HubPawnReplicationComponent::findFloorNumber( const GameObjectManager& manager, const GameObject& pawnObject ) const
    {
        const CharacterControllerComponent* pController = pawnObject.getComponent<CharacterControllerComponent>();
        const PhysicsBodyHandle             ground      = pController != nullptr && pController->isGrounded() ? pController->getCharacterState()._groundBody : PhysicsBodyHandle{};
        if ( ground.isValid() == false )
            return 0;
        for ( size_t floorIndex = 0; floorIndex < _listFloor.size(); ++floorIndex )
        {
            const GameObject*         pFloor = manager.resolveGameObject( _listFloor[floorIndex] );
            const RigidBodyComponent* pBody  = pFloor != nullptr ? pFloor->getComponent<RigidBodyComponent>() : nullptr;
            if ( pBody != nullptr && pBody->getBodyHandle() == ground )
                return static_cast<uint8>( floorIndex + 1u );
        }
        return 0;
    }

    bool HubPawnReplicationComponent::computeFloorPose( const GameObjectManager& manager, uint8 floorNumber, float64 tick, float3& outPosition, quaternion& outRotation ) const
    {
        if ( floorNumber == 0 || floorNumber > _listFloor.size() )
            return false;
        const GameObject*                pObject = manager.resolveGameObject( _listFloor[floorNumber - 1u] );
        const InterpToMovementComponent* pFloor  = pObject != nullptr ? pObject->getComponent<InterpToMovementComponent>() : nullptr;
        return pFloor != nullptr && pFloor->computePose( tick * static_cast<float64>( _tickInterval ), outPosition, outRotation );
    }

    void HubPawnReplicationComponent::updateFloorSlip( GameObjectManager& manager, const GameObject& pawnObject, bool bWalking )
    {
        const CharacterMovementComponent* pMovement   = pawnObject.getComponent<CharacterMovementComponent>();
        const uint8                       floorNumber = pMovement != nullptr ? findFloorNumber( manager, pawnObject ) : uint8{ 0 };
        float3                            floorPosition;
        quaternion                        floorRotation;
        const bool                        bOnFloor = computeFloorPose( manager, floorNumber, static_cast<float64>( static_cast<int64>( _tick ) + _floorTickOffset ), floorPosition, floorRotation );
        if ( bOnFloor == false || bWalking || floorNumber != _floorNumber )
            _floorSlip = 0.0f;
        const float3 local = bOnFloor ? float3::transform( pMovement->getState()._motion._position - floorPosition, std::as_const( floorRotation ).inverse() ) : float3{};
        if ( bOnFloor && bWalking == false && floorNumber == _floorNumber )
            _floorSlip += float3::getDistance( local, _floorLocalPosition );
        _floorNumber        = bOnFloor ? floorNumber : uint8{ 0 };
        _floorLocalPosition = local;
    }

    const HubPawnReplicationComponent::RemotePawn* HubPawnReplicationComponent::findRemoteRider() const
    {
        for ( const RemotePawn& remote : _listRemotePawn )
        {
            if ( remote._floorNumber != 0 )
                return &remote;
        }
        return nullptr;
    }

    int32 HubPawnReplicationComponent::getRemoteFloorNumber() const
    {
        const RemotePawn* pRemote = findRemoteRider();
        return pRemote != nullptr ? static_cast<int32>( pRemote->_floorNumber ) : 0;
    }

    float3 HubPawnReplicationComponent::getRemoteFloorLocalPosition() const
    {
        const RemotePawn* pRemote = findRemoteRider();
        return pRemote != nullptr ? pRemote->_floorLocalPosition : float3{};
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
        return toAbsoluteState( manager.getWorldOrigin(), state );
    }

    HubPawnState HubPawnReplicationComponent::toLocalState( const WorldOrigin& origin, const HubPawnState& state )
    {
        HubPawnState local            = state;
        local._walk._motion._position = origin.toLocal( state._walk._motion._position );
        if ( state._mountNumber != 0 )
            local._ride._position = origin.toLocal( state._ride._position );
        return local;
    }

    HubPawnState HubPawnReplicationComponent::toAbsoluteState( const WorldOrigin& origin, const HubPawnState& state )
    {
        HubPawnState absolute            = state;
        absolute._walk._motion._position = origin.toAbsolute( state._walk._motion._position );
        if ( state._mountNumber != 0 )
            absolute._ride._position = origin.toAbsolute( state._ride._position );
        return absolute;
    }

    void HubPawnReplicationComponent::applyState( GameObjectManager& manager, GameObject& pawnObject, const HubPawnState& state ) const
    {
        const HubPawnState local = toLocalState( manager.getWorldOrigin(), state );
        if ( local._mountNumber != 0 )
        {
            MountMovementComponent* const pMount = findMount( manager, local._mountNumber );
            if ( pMount != nullptr && pMount->isNetDriven() )
                pMount->setState( local._ride, local._ride._position );
            return;
        }
        CharacterMovementComponent* const pMovement = pawnObject.getComponent<CharacterMovementComponent>();
        if ( pMovement != nullptr && findMountNumber( manager, pawnObject ) == 0 )
            pMovement->setState( local._walk );
    }

    GameObject* HubPawnReplicationComponent::spawnPawnObject( GameObjectManager& manager, const utf8* pName, const float3& localPosition ) const
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
        pObject->setTransient( true );    // 같은 까닭으로 세이브 · 스냅샷에도 싣지 않는다 — 복원한 씬에 주인 없는 폰이 서고, 복원한 이 컴포넌트가 접속마다 다시 세운다
        pRoot->teleportTo( localPosition );
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

    bool HubPawnReplicationComponent::isClose( const HubPawnState& left, const HubPawnState& right, float32 tolerance )
    {
        if ( left._mountNumber != right._mountNumber )
            return false;
        const float3 delta = HubPawnReplicationComponentInternal::getPosition( left )._value - HubPawnReplicationComponentInternal::getPosition( right )._value;
        if ( delta.getLength() > tolerance )
            return false;
        if ( left._mountNumber != 0 )
            return left._ride._gait == right._ride._gait && left._ride._bGallopAllowed == right._ride._bGallopAllowed && left._ride._maxGait == right._ride._maxGait;
        return left._walk._motion._movementMode == right._walk._motion._movementMode && left._walk._bClimbing == right._walk._bClimbing;
    }
} // namespace sw

namespace sw
{
    namespace
    {
        struct HubPawnReplicationDevInternal
        {
            /** @brief 바닥 위의 자리를 로그에 남깁니다 — 두 클라이언트의 줄을 `Scripts net-pair mmo` 가 견줍니다(한쪽의 자기 자리와 다른 쪽이 본 자리). */
            static bool runFloorStatus( const vector<string>& listArgument, string& outReply )
            {
                if ( listArgument.empty() == false )
                    return false;
                const SceneManager* const pScenes  = game::findSubsystem<SceneManager>();
                const Scene* const        pScene   = pScenes != nullptr ? pScenes->getActiveScene() : nullptr;
                GameObjectManager* const  pManager = pScene != nullptr ? pScene->getObjectManager() : nullptr;
                // 예측하는 클라이언트는 씬에 하나다 — 둘이면 오류로 명령을 보내지 않는다.
                const HubPawnReplicationComponent* pComponent = pManager != nullptr ? pManager->getComponentRegistry().findUnique<HubPawnReplicationComponent>( []( const HubPawnReplicationComponent& component )
                { return component.getPredictedValue() != 0; } )
                                                                                    : nullptr;
                if ( pComponent == nullptr )
                {
                    outReply = "no hub pawn replication in the scene";
                    return true;
                }
                const float3                             own  = pComponent->getFloorLocalPosition();
                const float3                             seen = pComponent->getRemoteFloorLocalPosition();
                StringBuilder<constant::buffer::kMax256> reply;
                reply.appendFormat( "floor self=%# x=%# z=%# seen=%# x=%# z=%#", pComponent->getFloorNumber(), own._x, own._z, pComponent->getRemoteFloorNumber(), seen._x, seen._z );
                outReply = string( reply.view() );
                SW_LOG_INFO_ID( "Hub.FloorStatus", "[Hub] %#", outReply.c_str() );
                return true;
            }
        };
    } // namespace

    SW_DEV_COMMAND( HubFloor, "hub.floor", "hub.floor", "Log where this pawn and the pawn it sees stand on the moving floor", &HubPawnReplicationDevInternal::runFloorStatus );

    SW_AUTOMATION_GETTER_WATCH( hubPawnFloorSlip, "Hub.PawnFloorSlip", "Distance the own pawn slid on the moving floor it stands on (metres)", HubPawnReplicationComponent, &HubPawnReplicationComponent::getFloorSlip );
    SW_AUTOMATION_GETTER_WATCH( hubPawnFloor, "Hub.PawnFloor", "Number of the moving floor the own pawn stands on (0 none)", HubPawnReplicationComponent, &HubPawnReplicationComponent::getFloorNumber );
    SW_AUTOMATION_GETTER_WATCH( hubRemotePawnFloor, "Hub.RemotePawnFloor", "Number of the moving floor another player's pawn is shown standing on (0 none)", HubPawnReplicationComponent, &HubPawnReplicationComponent::getRemoteFloorNumber );
    SW_AUTOMATION_GETTER_WATCH( hubPawnPredicted, "Hub.PawnPredicted", "1 while this client predicts its own pawn", HubPawnReplicationComponent, &HubPawnReplicationComponent::getPredictedValue );
    SW_AUTOMATION_GETTER_WATCH( hubPawnCorrections, "Hub.PawnCorrections", "Times the server state rewound the predicted pawn", HubPawnReplicationComponent,
                                &HubPawnReplicationComponent::getCorrectionCount );
} // namespace sw
