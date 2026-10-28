#include "pch.h"

#include "Games/Shooter3D/ShooterNetPawnComponent.h"

#include "Core/Math/MathUtil.h"
#include "Core/Network/Connection/NetHost.h"
#include "Core/Serialization/BitStream.h"

#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManager.h"
#include "Engine/Object/GameObject/Service/SceneNetLink.h"

#include "GameFramework/Base/Character/Control/ControlSystem.h"
#include "GameFramework/Base/Foundation/Framework/GameDirectorComponent.h"
#include "GameFramework/Base/Foundation/Framework/GameService.h"
#include "GameFramework/Base/Gameplay/Gimmick/TickedPathMoverComponent.h"

#include "Games/Shooter3D/ShooterDirectorComponent.h"
#include "Games/Shooter3D/ShooterPlayerComponent.h"

namespace sw
{
    SW_LOG_CALLER( "ShooterNetPawn" );

    namespace
    {
        struct ShooterNetPawnComponentInternal
        {
            /** @brief 한 프레임에 따라잡는 고정 틱 상한입니다. 멈춘 뒤의 긴 프레임이 틱을 몰아 돌리지 않게 한다(남은 시간은 버린다). */
            static constexpr int32 kMaxTicksPerFrame = 4;

            /** @brief 같은 오브젝트 몸 이동 하나의 한 틱입니다 — 서버 시뮬레이션과 클라이언트 재시뮬레이션이 같은 이 함수를 쓴다. */
            static ShooterMoveState simulateTick( const ShooterBodyMovementComponent& movement, const vector<ShooterArenaBox>& listBox, const ShooterMoveState& state,
                                                  const ShooterMoveInput& input, float32 tickInterval )
            {
                bool bLanded = false;
                return movement.simulate( listBox, state, input, tickInterval, bLanded );
            }

            /**
             * @brief 씬의 틱 함수 바닥(`TickedPathMoverComponent`)을 서버 틱 @p serverTick 의 자리에 둔다 — 바닥은 조종 시스템 틱에 차이를 더해 움직인다.
             *        조종 시스템이 없는 씬이면 바닥도 움직이지 않으므로 둘 것이 없다.
             */
            static void alignTickedMovers( const GameObject* pOwner, int64 serverTick )
            {
                GameObjectManager*   pManager = pOwner != nullptr ? pOwner->getManager() : nullptr;
                const ControlSystem* pControl = pManager != nullptr ? ControlSystem::find( *pManager ) : nullptr;
                if ( pControl != nullptr )
                    TickedPathMoverComponent::setTickOffsetInScene( *pManager, serverTick - static_cast<int64>( pControl->getTick() ) );
            }
        };
    } // namespace
} // namespace sw

namespace sw
{
    ShooterNetPawnComponent::ShooterNetPawnComponent()
        : _tickInterval{ 1.0f / 30.0f }
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
        , _bytesScratch{}
        , _accumulator{ 0.0f }
        , _tick{ 0 }
        , _nextInputTick{ 1 }
        , _reconciledTick{ 0 }
        , _controllerID{ -1 }
        , _role{ Role::None }
        , _bJumpPending{ SW_FALSE }
    {
        setCanEverTick( true );
    }

    ShooterNetPawnComponent::~ShooterNetPawnComponent() = default;

    void ShooterNetPawnComponent::onBeginPlay()
    {
        Component::onBeginPlay();
        _role = Role::None;
        // 넷 링크는 이 컴포넌트의 월드(오브젝트 매니저)에 올라 있다 — 전역 세션을 찾지 않는다. 한 프로세스에 월드가 여럿이어도(가상 서버 하니스) 각자 자기 링크를 본다.
        const SceneNetLink*           pLink      = findLink();
        ShooterBodyMovementComponent* pMovement  = findMovement();
        const bool                    bNoSession = pLink == nullptr || pLink->isBound() == false || pMovement == nullptr;
        if ( bNoSession )
            return; // 혼자 하는 게임 — 몸 이동은 프레임마다 예전 길로 돈다
        const NetLinkValue& link = pLink->get();

        PredictionSmootherSettings smoothing;
        smoothing._halfLifeSeconds = _smoothingHalfLife;
        smoothing._snapDistance    = _smoothingSnapDistance;
        if ( _smoother.initialize( smoothing ) == false )
        {
            SW_LOG_ERROR( "Shooter net pawn: smoothing half life %# s is not usable - the networked movement stays off", _smoothingHalfLife );
            return;
        }
        if ( link._role == NetLinkRole::Server )
        {
            ReplicationServerConfig settings;
            settings._tickInterval  = _tickInterval;
            const bool bInitialized = _interest.initialize( _interestCellSize, _interestEnterRadius, _interestLeaveRadius ) &&
                                      _server.initialize( link._pPeers, settings, &_interest );
            if ( bInitialized )
                _routerSubscription = link._pRouter->registerHandler( &_server );
            if ( _routerSubscription.isActive() == false )
            {
                SW_LOG_ERROR( "Shooter net pawn: the replication server did not start (reason above) - the networked movement stays off" );
                return;
            }
            _role = Role::Server;
        }
        else
        {
            ReplicationClientConfig settings;
            settings._tickInterval = _tickInterval;
            if ( _client.initialize( link._pPeers, settings ) )
                _routerSubscription = link._pRouter->registerHandler( &_client );
            if ( _routerSubscription.isActive() == false )
            {
                SW_LOG_ERROR( "Shooter net pawn: the replication client did not start (reason above) - the networked movement stays off" );
                return;
            }
            _role = Role::Client;
        }
        _accumulator    = 0.0f;
        _tick           = 0;
        _nextInputTick  = 1;
        _reconciledTick = 0;
        _controllerID   = -1;
        pMovement->setNetDriven( true );
        SW_LOG_INFO( "Shooter net pawn: %# movement at %# Hz", _role == Role::Server ? "server-authoritative" : "predicted", static_cast<int32>( MathUtil::round( 1.0f / _tickInterval ) ) );
    }

    void ShooterNetPawnComponent::onEndPlay()
    {
        _routerSubscription.reset();
        ShooterBodyMovementComponent* pMovement = findMovement();
        if ( pMovement != nullptr )
        {
            pMovement->setNetDriven( false );
            pMovement->setDisplayOffset( float3{ 0.0f, 0.0f, 0.0f } );
        }
        _role = Role::None;
        Component::onEndPlay();
    }

    void ShooterNetPawnComponent::onTick( float32 deltaTime )
    {
        Component::onTick( deltaTime );
        ShooterBodyMovementComponent*  pMovement = findMovement();
        const vector<ShooterArenaBox>* pListBox  = findArenaBoxes();
        const bool                     bIdle     = _role == Role::None || pMovement == nullptr || pListBox == nullptr || deltaTime <= 0.0f;
        if ( bIdle )
            return;
        // 점프 눌림은 한 프레임만 참이다 — 입력 틱(30 Hz)이 없던 프레임의 눌림이 사라지지 않게 매 프레임 쌓는다.
        if ( _role == Role::Client && pMovement->makeInput()._bJump == SW_TRUE )
            _bJumpPending = SW_TRUE;
        _accumulator += deltaTime;
        int32 tickCount = 0;
        while ( _accumulator >= _tickInterval && tickCount < ShooterNetPawnComponentInternal::kMaxTicksPerFrame )
        {
            _accumulator -= _tickInterval;
            ++tickCount;
            if ( _role == Role::Server )
                stepServer( *pMovement, *pListBox );
            else
                stepClient( *pMovement, *pListBox );
        }
        if ( _accumulator >= _tickInterval )
            _accumulator = 0.0f; // 따라잡지 못한 시간은 버린다 — 몰아 돌린 틱이 다음 프레임을 또 밀지 않게
        if ( _role == Role::Client )
        {
            _client.update( deltaTime );
            _smoother.update( deltaTime );
            pMovement->setDisplayOffset( _smoother.getOffset() );
        }
    }

    void ShooterNetPawnComponent::stepServer( ShooterBodyMovementComponent& movement, const vector<ShooterArenaBox>& listBox )
    {
        ++_tick;
        ShooterNetPawnComponentInternal::alignTickedMovers( getOwner(), static_cast<int64>( _tick ) );
        const SceneNetLink* pLink = findLink();
        if ( pLink == nullptr || pLink->isBound() == false )
            return;
        INetPeers& peers = *pLink->get()._pPeers;
        // 조종 연결 — 처음 붙은 클라이언트가 이 몸을 맡고, 끊기면 다음 클라이언트가 처음부터 맡는다.
        vector<int32> listConnection;
        peers.collectConnected( listConnection );
        const bool bControllerGone = _controllerID < 0 || peers.getConnectionState( _controllerID ) != NetConnectionState::Connected;
        if ( bControllerGone )
        {
            _controllerID  = listConnection.empty() ? -1 : listConnection.front();
            _nextInputTick = 1;
        }
        ShooterMoveInput input;
        vector<uint8>&   inputBytes = _bytesScratch;
        bool             bExact     = false;
        uint32           inputTick  = 0;
        const bool       bHasInput  = _controllerID >= 0 && _server.popInput( _controllerID, _nextInputTick, inputBytes, bExact ) && readInput( inputBytes, input );
        if ( bHasInput )
        {
            inputTick = _nextInputTick;
            ++_nextInputTick;
        }
        const ShooterMoveState before = movement.getState();
        const ShooterMoveState next   = ShooterNetPawnComponentInternal::simulateTick( movement, listBox, before, input, _tickInterval );
        movement.setState( next, float3{ next._position._x - before._position._x, 0.0f, next._position._z - before._position._z } * ( 1.0f / _tickInterval ) );

        writeState( next, _bytesScratch );
        _server.beginTick( _tick );
        _server.setEntity( kPawnEntityID, kPawnTypeID, _bytesScratch );
        _server.endTick();
        _interest.setEntityPosition( kPawnEntityID, next._position );
        if ( _controllerID >= 0 )
        {
            _interest.setObserver( _controllerID, next._position );
            if ( bHasInput )
                (void)_server.setOwnerState( _controllerID, inputTick, _bytesScratch ); // 상태는 상한(작은 고정 크기) 안이다
        }
        _interest.updateRelevancy();
        _server.sendSnapshots();
    }

    void ShooterNetPawnComponent::stepClient( ShooterBodyMovementComponent& movement, const vector<ShooterArenaBox>& listBox )
    {
        ++_tick;
        // 예측하는 이 입력 틱은 서버에서 (입력 틱 + 서버 차이) 틱에 시뮬레이션된다 — 바닥을 그 자리에 둬야 바닥 위 예측이 서버와 맞는다.
        const std::optional<int64> serverTickOffset = _client.findServerTickOffset();
        if ( serverTickOffset.has_value() )
            ShooterNetPawnComponentInternal::alignTickedMovers( getOwner(), static_cast<int64>( _tick ) + *serverTickOffset );
        ShooterMoveInput input = movement.makeInput();
        if ( _bJumpPending == SW_TRUE )
        {
            input._bJump  = SW_TRUE; // 이 틱 전에 쌓인 눌림을 이 입력 틱에 싣고 비운다
            _bJumpPending = SW_FALSE;
        }
        writeInput( input, _bytesScratch );
        if ( _client.sendInput( _tick, _bytesScratch ) == false )
            SW_LOG_WARNING( "Shooter net pawn: input of tick %# was not sent (too large)", _tick );
        const ShooterMoveState before  = movement.getState();
        ShooterMoveState       current = ShooterNetPawnComponentInternal::simulateTick( movement, listBox, before, input, _tickInterval );
        if ( _prediction.record( _tick, input, current ) == false )
            SW_LOG_WARNING( "Shooter net pawn: prediction tick %# does not follow %# - not recorded", _tick, _prediction.getLatestTick() );

        // 서버가 새 소유자 상태(그 입력 틱까지 처리한 내 몸)를 보냈으면 예측을 맞춘다.
        const NetOwnerState* pOwner = _client.findOwnerState();
        ShooterMoveState     serverState;
        const bool           bNewOwnerState = pOwner != nullptr && pOwner->_bPresent == SW_TRUE && pOwner->_inputTick > _reconciledTick && readState( pOwner->_buffer, serverState );
        if ( bNewOwnerState )
        {
            _reconciledTick                        = pOwner->_inputTick;
            const float3                    shown  = current._position;
            const PredictionReconcileResult result = _prediction.reconcile(
                pOwner->_inputTick, serverState, current,
                [&movement, &listBox, this]( const ShooterMoveState& state, const ShooterMoveInput& tickInput )
            { return ShooterNetPawnComponentInternal::simulateTick( movement, listBox, state, tickInput, _tickInterval ); },
                [this]( const ShooterMoveState& left, const ShooterMoveState& right )
            { return isClose( left, right ); } );
            switch ( result )
            {
                case PredictionReconcileResult::Corrected:
                {
                    _smoother.addCorrection( shown, current._position ); // 보정한 만큼 화면에서 녹인다
                    break;
                }
                case PredictionReconcileResult::TickOverwritten:
                {
                    // 예측이 기록보다 앞서 있다 — 서버 상태로 바로 맞춘다(남은 입력은 기록에 없다).
                    _smoother.addCorrection( shown, serverState._position );
                    current = serverState;
                    break;
                }
                case PredictionReconcileResult::Matched:
                case PredictionReconcileResult::TickAhead:
                {
                    break;
                }
            }
        }
        movement.setState( current, float3{ current._position._x - before._position._x, 0.0f, current._position._z - before._position._z } * ( 1.0f / _tickInterval ) );
    }

    ShooterBodyMovementComponent* ShooterNetPawnComponent::findMovement() const
    {
        GameObject* pOwner = getOwner();
        return pOwner != nullptr ? pOwner->getComponent<ShooterBodyMovementComponent>() : nullptr;
    }

    const SceneNetLink* ShooterNetPawnComponent::findLink() const
    {
        const GameObject*  pOwner   = getOwner();
        GameObjectManager* pManager = pOwner != nullptr ? pOwner->getManager() : nullptr;
        return pManager != nullptr ? &pManager->getSubsystem<SceneNetLink>() : nullptr;
    }

    const vector<ShooterArenaBox>* ShooterNetPawnComponent::findArenaBoxes() const
    {
        GameObject*                     pOwner    = getOwner();
        GameObjectManager*              pManager  = pOwner != nullptr ? pOwner->getManager() : nullptr;
        const ShooterPlayerComponent*   pPlayer   = pOwner != nullptr ? pOwner->getComponent<ShooterPlayerComponent>() : nullptr;
        const ShooterDirectorComponent* pDirector = pManager != nullptr && pPlayer != nullptr ? GameDirectorComponent::resolve<ShooterDirectorComponent>( *pManager, pPlayer->getDirector() ) : nullptr;
        return pDirector != nullptr ? &pDirector->getBoxes() : nullptr;
    }

    void ShooterNetPawnComponent::writeState( const ShooterMoveState& state, vector<uint8>& outBytes )
    {
        BitWriter writer{ std::move( outBytes ) };
        writer.clear();
        writer.writeFloat( state._position._x );
        writer.writeFloat( state._position._y );
        writer.writeFloat( state._position._z );
        writer.writeFloat( state._verticalSpeed );
        writer.writeBool( state._bOnGround == SW_TRUE );
        outBytes = writer.releaseBytes();
    }

    bool ShooterNetPawnComponent::readState( const vector<uint8>& bytes, ShooterMoveState& outState )
    {
        BitReader        reader( bytes.data(), static_cast<int32>( bytes.size() ) );
        ShooterMoveState state;
        const bool       bFinite = reader.readFiniteFloat( state._position._x ) && reader.readFiniteFloat( state._position._y ) &&
                             reader.readFiniteFloat( state._position._z ) && reader.readFiniteFloat( state._verticalSpeed );
        if ( bFinite == false )
            return false;
        state._bOnGround = reader.readBool() ? SW_TRUE : SW_FALSE;
        if ( reader.hasOverflowed() )
            return false;
        outState = state;
        return true;
    }

    void ShooterNetPawnComponent::writeInput( const ShooterMoveInput& input, vector<uint8>& outBytes )
    {
        BitWriter writer{ std::move( outBytes ) };
        writer.clear();
        writer.writeFloat( input._move._x );
        writer.writeFloat( input._move._z );
        writer.writeBool( input._bSprint == SW_TRUE );
        writer.writeBool( input._bJump == SW_TRUE );
        outBytes = writer.releaseBytes();
    }

    bool ShooterNetPawnComponent::readInput( const vector<uint8>& bytes, ShooterMoveInput& outInput )
    {
        BitReader        reader( bytes.data(), static_cast<int32>( bytes.size() ) );
        ShooterMoveInput input;
        // NaN 이면 아래 길이 검사(`> 1`)가 거짓이 되어 통과한다 — 유한한 값만 받는다.
        if ( ( reader.readFiniteFloat( input._move._x ) && reader.readFiniteFloat( input._move._z ) ) == false )
            return false;
        input._bSprint = reader.readBool() ? SW_TRUE : SW_FALSE;
        input._bJump   = reader.readBool() ? SW_TRUE : SW_FALSE;
        // 이동 길이는 1 이하다(`makeInput`) — 넘는 입력은 손상이거나 속인 입력이다. 서버가 잘라 받지 않고 버린다.
        // "1 이하인가" 로 묻는다 — "1 을 넘는가" 로 물으면 NaN 이 거짓이 되어 통과한다.
        const bool bLengthOk = input._move.getLength() <= 1.0f + MathUtil::kEpsilon;
        if ( reader.hasOverflowed() || bLengthOk == false )
            return false;
        outInput = input;
        return true;
    }

    bool ShooterNetPawnComponent::isClose( const ShooterMoveState& left, const ShooterMoveState& right ) const
    {
        const float3 delta = left._position - right._position;
        return delta.getLength() <= _reconcileTolerance && left._bOnGround == right._bOnGround;
    }
} // namespace sw
