#include "pch.h"

#include "GameFramework/Kits/Feature/Network/NetDestruction/DestructionReplication.h"

#include "Core/Algorithm/VectorUtil.h"
#include "Core/Container/pair.h"
#include "Core/Math/MathUtil.h"
#include "Core/Network/Connection/NetConnection.h"
#include "Core/Network/Connection/NetEndpoint.h"
#include "Core/Network/Message/NetSendBudget.h"
#include "Core/Serialization/BitStream.h"

#include "Engine/Destruction/Fracture/FractureBaseComponent.h"
#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManagerResolve.inl"
#include "Engine/Reflection/ReflectionCast.h"
#include "Engine/Serialization/Fixed/ReplicatedLayout.h"

namespace sw
{
    namespace
    {
        struct DestructionReplicationInternal
        {
            static constexpr int32 kPoseMessageBudget = 900; ///< 자세 메시지 하나를 이 크기에서 끊는다(한도 1024)
            static constexpr uint8 kRestRepeatCount   = 5;   ///< 멈춘 자세를 비신뢰로 되풀이해 보내는 횟수(자세 주기마다 한 번)
            static constexpr int32 kMaxPoseSample     = 32;  ///< 덩어리마다 보관하는 자세 표본 수

            /** @brief 프로필의 자세 간격(초)입니다. */
            static float32 computePosePeriod( const FractureBaseComponent& component ) { return 1.0f / MathUtil::max( 0.1f, component.getProfile()._networkPoseRate ); }

            static_assert( static_cast<uint8>( DestructionDamageKind::Count ) == 3, "DestructionEventNetState::_kind 의 Max 를 열거의 끝 값과 맞춘다" );

            /** @brief 이벤트 하나를 씁니다 — 칸은 `DestructionEventNetState` 의 선언이다. 값이 선언 범위 밖(NaN · 무한대)이면 false 이고 쓰다 만 비트가 남습니다. */
            [[nodiscard]] static bool writeEvent( BitWriter& writer, const DestructionDamageEvent& event )
            {
                DestructionEventNetState state;
                state._kind      = static_cast<uint8>( event._kind );
                state._leafHint  = event._leafHint;
                state._groupID   = event._groupID;
                state._position  = event._position;
                state._direction = event._direction;
                state._strain    = event._strain;
                state._radius    = event._radius;
                state._impulse   = event._impulse;
                return ReplicatedLayout::writeFull( state, writer );
            }

            [[nodiscard]] static bool readEvent( BitReader& reader, DestructionDamageEvent& outEvent )
            {
                DestructionEventNetState state;
                if ( ReplicatedLayout::readFull( reader, state, nullptr ) != FixedReadResult::Ok )
                    return false;
                outEvent._kind      = static_cast<DestructionDamageKind>( state._kind );
                outEvent._leafHint  = state._leafHint;
                outEvent._groupID   = state._groupID;
                outEvent._position  = state._position;
                outEvent._direction = state._direction;
                outEvent._strain    = state._strain;
                outEvent._radius    = state._radius;
                outEvent._impulse   = state._impulse;
                return true;
            }

            /** @brief 자세를 비트 그대로 씁니다(`DestructionExactPoseNetState`). 유한하지 않으면 false 입니다. */
            [[nodiscard]] static bool writeExactPose( BitWriter& writer, const float3& position, const quaternion& rotation )
            {
                DestructionExactPoseNetState state;
                state._position = position;
                state._rotation = rotation;
                return ReplicatedLayout::writeFull( state, writer );
            }

            [[nodiscard]] static bool readExactPose( BitReader& reader, float3& outPosition, quaternion& outRotation )
            {
                DestructionExactPoseNetState state;
                if ( ReplicatedLayout::readFull( reader, state, nullptr ) != FixedReadResult::Ok )
                    return false;
                outPosition = state._position;
                outRotation = state._rotation;
                return true;
            }

            /** @brief 움직이는 자세를 양자화 범위에 실을 수 있는가입니다. 양자화는 범위 밖을 경계로 자르므로 쓰기 전에 거른다(NaN 도 여기서 걸린다). */
            static bool canWriteMovingPose( const float3& position )
            {
                const float32 range = DestructionMovingPoseNetState::kPositionRange;
                return MathUtil::abs( position._x ) <= range && MathUtil::abs( position._y ) <= range && MathUtil::abs( position._z ) <= range;
            }

            /** @brief 움직이는 자세를 양자화해 씁니다(`DestructionMovingPoseNetState`). 회전이 유한하지 않으면 false 입니다. */
            [[nodiscard]] static bool writeMovingPose( BitWriter& writer, const float3& position, const quaternion& rotation )
            {
                DestructionMovingPoseNetState state;
                state._position = position;
                state._rotation = rotation;
                return ReplicatedLayout::writeFull( state, writer );
            }

            [[nodiscard]] static bool readMovingPose( BitReader& reader, float3& outPosition, quaternion& outRotation )
            {
                DestructionMovingPoseNetState state;
                if ( ReplicatedLayout::readFull( reader, state, nullptr ) != FixedReadResult::Ok )
                    return false;
                outPosition = state._position;
                outRotation = state._rotation;
                outRotation.normalize();
                return true;
            }

            static FractureBaseComponent* resolveFracture( GameObjectManager* pManager, TComponentHandle<FractureBaseComponent> handle )
            {
                if ( pManager == nullptr )
                    return nullptr;
                return pManager->resolveComponent( handle );
            }
        };
    } // namespace
} // namespace sw

namespace sw
{
    DestructionReplicationServer::DestructionReplicationServer()
        : _listEntry{}
        , _listRequest{}
        , _settings{}
        , _stats{}
        , _writer{}
        , _pHost{ nullptr }
        , _pManager{ nullptr }
        , _hashTime{ 0.0f }
        , _lastTick{ 0 }
    {
    }

    void DestructionReplicationServer::initialize( INetPeers* pHost, GameObjectManager* pManager, const DestructionReplicationConfig& settings )
    {
        _pHost    = pHost;
        _pManager = pManager;
        _settings = settings;
        _listEntry.clear();
        _listRequest.clear();
        _stats    = DestructionReplicationStats{};
        _hashTime = 0.0f;
        _lastTick = 0;
    }

    void DestructionReplicationServer::registerObject( NetObjectID netID, FractureBaseComponent& component )
    {
        component.setAuthority( true );
        Entry* pEntry = findEntry( netID );
        if ( pEntry == nullptr )
        {
            _listEntry.emplace_back();
            pEntry = &_listEntry.back();
        }
        *pEntry                 = Entry{};
        pEntry->_netID          = netID;
        pEntry->_component      = TComponentHandle<FractureBaseComponent>{ &component };
        pEntry->_sentEventCount = component.getEventLog()._firstEvent + static_cast<uint32>( component.getEventLog()._listEvent.size() );
    }

    DestructionReplicationServer::Entry* DestructionReplicationServer::findEntry( NetObjectID netID )
    {
        return VectorUtil::findBy<&Entry::_netID>( _listEntry, netID );
    }

    FractureBaseComponent* DestructionReplicationServer::resolve( const Entry& entry ) const
    {
        return DestructionReplicationInternal::resolveFracture( _pManager, entry._component );
    }

    void DestructionReplicationServer::onPeerEnteredWorld( int32 connectionID )
    {
        for ( const Entry& entry : _listEntry )
        {
            queueRequest( connectionID, entry._netID );
        }
    }

    void DestructionReplicationServer::onPeerLeftWorld( int32 connectionID )
    {
        for ( size_t index = _listRequest.size(); index > 0; --index )
        {
            if ( _listRequest[index - 1]._connectionID == connectionID )
                _listRequest.erase( _listRequest.begin() + static_cast<std::ptrdiff_t>( index - 1 ) );
        }
    }

    NetHandleResult DestructionReplicationServer::receive( NetDestructionMessage::SnapshotRequest, const NetMessageContext& context, const DestructionSnapshotRequestBody& body )
    {
        const NetObjectID netID = body._netID;
        if ( context._connectionID < 0 )
            return NetHandleResult::Malformed;
        if ( findEntry( netID ) != nullptr )
            queueRequest( context._connectionID, netID );
        return NetHandleResult::Handled;
    }

    void DestructionReplicationServer::queueRequest( int32 connectionID, NetObjectID netID )
    {
        // (연결, netID) 하나에 요청 하나 — 같은 요청을 거듭 보내도 줄은 늘지 않는다. 예전에는 받은 요청마다 쌓아 1 만 개(약 40 KB)면 창이 빌 때까지
        // 매 틱 1 만 번 직렬화했다(감사 N-5, 언리얼 RPC DoS 감지 자리). 줄의 길이는 연결 수 × 파괴 엔티티 수로 묶인다.
        for ( const Request& request : _listRequest )
        {
            if ( request._connectionID == connectionID && request._netID == netID )
                return;
        }
        _listRequest.push_back( Request{ connectionID, netID } );
    }

    int32 DestructionReplicationServer::broadcastMessage()
    {
        const int32 peerCount = _pHost->getInWorldCount();
        return countBroadcast( peerCount, _writer.broadcastMessage( *_pHost ) );
    }

    int32 DestructionReplicationServer::countBroadcast( int32 connectedCount, int32 sentCount )
    {
        _stats._sentBytes += static_cast<uint64>( _writer.getByteCount() ) * static_cast<uint64>( MathUtil::max( 0, sentCount ) );
        // 신뢰 창이 찬 연결은 거절한다(`NetConnection::kReliableWindow`). 그 클라이언트는 이벤트가 빠지지만 다음 해시 비교에서 스냅숏으로 바로잡힌다.
        if ( sentCount < connectedCount )
            _stats._sendRejectedCount += static_cast<uint32>( connectedCount - sentCount );
        return sentCount;
    }

    void DestructionReplicationServer::update( uint32 serverTick )
    {
        if ( _pHost == nullptr )
            return;
        // 간격은 흐른 서버 틱으로 잰다 — 서버의 프레임 길이가 자세 · 해시의 주기를 바꾸지 않는다.
        const float32 elapsedSeconds = serverTick > _lastTick ? static_cast<float32>( serverTick - _lastTick ) * NetClock::kTickSeconds : 0.0f;
        _lastTick                    = serverTick;
        for ( Entry& entry : _listEntry )
        {
            FractureBaseComponent* pComponent = resolve( entry );
            if ( pComponent != nullptr )
                sendEvents( entry, *pComponent, serverTick );
        }
        // 스냅숏은 이벤트 뒤에 보낸다. 그래야 이번 틱 이벤트까지 이벤트 수에 포함된다.
        // 이벤트(순서 없음)와 스냅숏(순서 있음)은 받는 쪽에서 도착 순서가 바뀔 수 있으므로 클라이언트가 번호로 맞춘다.
        vector<Request> listRequest;
        listRequest.swap( _listRequest );
        for ( const Request& request : listRequest )
        {
            Entry*                 pEntry     = findEntry( request._netID );
            FractureBaseComponent* pComponent = pEntry != nullptr ? resolve( *pEntry ) : nullptr;
            const bool             bBlocked   = VectorUtil::containsIf( _listRequest, [&request]( const Request& waiting )
                          { return waiting._connectionID == request._connectionID; } ); // 이 연결은 이번 틱에 이미 막혔다 — 스냅숏을 헛되이 만들지 않는다
            if ( pComponent != nullptr && ( bBlocked || sendSnapshot( *pEntry, *pComponent, request._connectionID ) == false ) )
                _listRequest.push_back( request ); // 신뢰 창이 찼으므로 다음 틱에 다시 보낸다. 연결이 닫히면 onConnectionClosed 가 지운다
        }
        for ( Entry& entry : _listEntry )
        {
            FractureBaseComponent* pComponent = resolve( entry );
            if ( pComponent != nullptr )
                sendPoses( entry, *pComponent, serverTick, elapsedSeconds );
        }
        _hashTime += elapsedSeconds;
        if ( _hashTime + MathUtil::kLooseEpsilon >= _settings._hashInterval )
        {
            _hashTime = 0.0f;
            for ( const Entry& entry : _listEntry )
            {
                const FractureBaseComponent* pComponent = resolve( entry );
                if ( pComponent != nullptr )
                    sendHash( entry, *pComponent );
            }
        }
    }

    void DestructionReplicationServer::sendEvents( Entry& entry, FractureBaseComponent& component, uint32 serverTick )
    {
        // 이벤트 번호는 상태의 번호다 — 기록은 되돌린 상태(세이브 · 셀) 뒤부터라 첫 번호를 더한다. 그 앞 이벤트를 못 받은 쪽은 해시 대조에서 어긋나 스냅숏으로 바로잡는다.
        const uint32                               firstEvent    = component.getEventLog()._firstEvent;
        const vector<DestructionDamageEvent>&      listEvent     = component.getEventLog()._listEvent;
        const vector<optional<FractureGroupPose>>& listGroupPose = component.getEventGroupPoses();
        const uint32                               eventCount    = firstEvent + static_cast<uint32>( listEvent.size() );
        for ( uint32 eventIndex = MathUtil::max( entry._sentEventCount, firstEvent ); eventIndex < eventCount; ++eventIndex )
        {
            const uint32 index  = eventIndex - firstEvent;
            BitWriter&   writer = _writer.beginMessage<NetDestructionMessage::Table, NetDestructionMessage::Event>();
            writer.writeVarUint( entry._netID );
            writer.writeVarUint( serverTick );
            writer.writeVarUint( eventIndex );
            bool bWritten = DestructionReplicationInternal::writeEvent( writer, listEvent[index] );
            // 떨어진 덩어리를 맞힌 이벤트이면 맞기 직전의 그 덩어리 자세를 비트 그대로 싣는다. 신뢰 채널이 밀려 자세가 늦어도 받는 쪽이 그 위치에서 덩어리를 가를 수 있다.
            const bool bHasPose = index < listGroupPose.size() && listGroupPose[index].has_value();
            writer.writeBool( bHasPose );
            if ( bWritten && bHasPose )
                bWritten = DestructionReplicationInternal::writeExactPose( writer, listGroupPose[index]->_position, listGroupPose[index]->_rotation );
            if ( bWritten == false )
            {
                // 유한하지 않은 이벤트 — 보내지 않는다. 받는 쪽은 번호가 비어 해시 대조에서 어긋나고 스냅숏으로 바로잡는다.
                SW_LOG_ERROR_ID( "DestructionReplication.EventOutOfRange", "DestructionReplication: event %# of object %# has a value outside the replicated range - not sent", eventIndex, entry._netID );
                continue;
            }
            (void)broadcastMessage(); // 서술자 채널(신뢰 순서 없음) — 받는 쪽이 번호로 순서를 맞춘다. 앞 이벤트를 잃어도 뒤 이벤트가 기다리지 않는다
            ++_stats._eventMessageCount;
        }
        entry._sentEventCount = eventCount;
    }

    bool DestructionReplicationServer::sendSnapshot( Entry& entry, FractureBaseComponent& component, int32 connectionID )
    {
        const uint32 eventCount = component.getState().getEventCount();
        if ( component.isStateReady() == false || eventCount == 0 )
            return true; // 처음 상태 그대로이므로 이벤트는 0번부터 보낸다
        DestructionSnapshotBody body;
        body._netID      = entry._netID;
        body._eventCount = eventCount;
        component.makeNetworkSnapshot( body._bytes );
        // 상한은 본문 형식(`MaxCount`)이 아니라 머리까지 든 메시지 크기로 본다 — 바이트가 상한 근처면 머리가 넘긴다. 넘는 바이트는 쓰지 않는다(쓰기 실패 오류 대신 아래 오류 하나).
        const bool bFits = body._bytes.size() <= static_cast<size_t>( NetConnection::kMaxReliableMessageSize ) &&
                           _writer.writeMessage<NetDestructionMessage::Table, NetDestructionMessage::Snapshot>( body ) && _writer.getByteCount() <= NetConnection::kMaxReliableMessageSize;
        if ( bFits == false )
        {
            // 다시 만들어도 같은 결과이므로 큐에 되돌려 놓지 않는다. 이 오브젝트는 늦은 참가와 복구를 받지 못하며, 해시 비교가 계속 어긋남을 알린다.
            SW_LOG_ERROR_ID( "DestructionReplication.DestructionReplicationSnapshotObjectBytes", "DestructionReplication: the snapshot of object %# is %# bytes, over the reliable message limit %# - it cannot be sent",
                             entry._netID, body._bytes.size(), NetConnection::kMaxReliableMessageSize );
            ++_stats._sendRejectedCount;
            return true;
        }
        // 늦은 참가는 부서진 오브젝트마다 하나씩 받는다 — 창이 차면 닫지 않고 다음 틱에 잇는다.
        if ( _writer.trySendMessage( *_pHost, connectionID ) == false )
        {
            ++_stats._sendRejectedCount;
            return false;
        }
        _stats._sentBytes += static_cast<uint64>( _writer.getByteCount() );
        ++_stats._snapshotCount;
        return true;
    }

    void DestructionReplicationServer::sendPoses( Entry& entry, FractureBaseComponent& component, uint32 serverTick, float32 elapsedSeconds )
    {
        using Internal = DestructionReplicationInternal;
        if ( component.isStateReady() == false || component.isFractured() == false )
            return;
        const float32 period = Internal::computePosePeriod( component );
        entry._poseTime += elapsedSeconds;
        const bool bDue = entry._poseTime + MathUtil::kLooseEpsilon >= period;
        if ( bDue )
            entry._poseTime = 0.0f;

        vector<FractureGroupPose> listPose;
        component.collectGroupPoses( listPose );
        vector<const FractureGroupPose*> listMoving;
        vector<const FractureGroupPose*> listRest;
        vector<const FractureGroupPose*> listRestRepeat;
        vector<ChunkSendState>           listKeep;
        for ( const FractureGroupPose& pose : listPose )
        {
            if ( pose._bGone == SW_TRUE || component.isChunkVolume( pose._volume ) == false )
                continue;
            ChunkSendState state;
            state._groupID = pose._groupID;
            for ( const ChunkSendState& previous : entry._listChunk )
            {
                if ( previous._groupID == pose._groupID )
                    state = previous;
            }
            if ( pose._bResting == SW_TRUE )
            {
                if ( state._bRestSent == SW_FALSE )
                {
                    listRest.push_back( &pose );
                    state._restRepeatLeft = Internal::kRestRepeatCount;
                }
                state._bRestSent = SW_TRUE;
                // 손실이 크면 신뢰 채널은 앞 메시지를 기다리느라 몇 초 밀린다. 멈춘 위치를 비신뢰로도 몇 번 보내서 그동안 덩어리가 공중에 멈춰 있지 않게 한다.
                if ( bDue && state._restRepeatLeft > 0 )
                {
                    listRestRepeat.push_back( &pose );
                    --state._restRepeatLeft;
                }
            }
            else
            {
                state._bRestSent = SW_FALSE;
                if ( bDue && Internal::canWriteMovingPose( pose._center ) )
                {
                    listMoving.push_back( &pose );
                }
                else if ( bDue )
                {
                    // 이 서버에서 처음 넘는 순간만 알린다. 넘은 덩어리는 틱마다 다시 걸리므로 매번 알리면 로그가 넘친다. 그 뒤는 `_outOfRangePoseCount` 가 센다.
                    if ( _stats._outOfRangePoseCount == 0 )
                        SW_LOG_WARNING_ID( "DestructionReplication.DestructionReplicationChunkObjectMoved", "DestructionReplication: chunk %# of object %# moved outside the position range +-%# m - its moving pose is not sent until it rests "
                                                                                                            "(raise DestructionMovingPoseNetState::kPositionRange)",
                                           pose._groupID, entry._netID, DestructionMovingPoseNetState::kPositionRange );
                    ++_stats._outOfRangePoseCount;
                }
            }
            listKeep.push_back( state );
        }
        entry._listChunk.swap( listKeep );

        // 자세는 세 종류로 보낸다. 움직이는 덩어리(비신뢰, 양자화), 멈춤 확정(신뢰, 비트 그대로), 멈춤 반복(비신뢰, 비트 그대로)이다. 메시지 하나가 예산을 넘으면 나눠 보낸다.
        // 비신뢰를 쓰는 이유: 받는 쪽이 틱으로 순서를 맞추므로 늦게 온 자세도 보간에 쓸 수 있다.
        // 반면 순서 전용 채널은 같은 종류의 메시지를 한 간격에 하나만 보내므로 오브젝트와 조각 메시지가 서로 지워 버린다.
        sendPoseList( entry, listMoving, false, false, serverTick );
        sendPoseList( entry, listRest, true, true, serverTick );
        sendPoseList( entry, listRestRepeat, true, false, serverTick );
    }

    void DestructionReplicationServer::sendPoseList( const Entry& entry, const vector<const FractureGroupPose*>& listPose, bool bRest, bool bReliable, uint32 serverTick )
    {
        using Internal = DestructionReplicationInternal;
        size_t start   = 0;
        while ( start < listPose.size() )
        {
            BitWriter& writer = _writer.beginMessage<NetDestructionMessage::Table, NetDestructionMessage::Pose>( bReliable ? NetChannelKind::ReliableOrdered : NetChannelKind::Unreliable );
            writer.writeBool( bRest );
            writer.writeVarUint( entry._netID );
            writer.writeVarUint( serverTick );
            // 자세마다 이어짐 비트 · 덩어리 번호 · 자세를 써 보고 예산을 넘으면 물려 다음 메시지로 넘긴다 — 크기를 어림하지 않는다.
            NetSendBudget budget( Internal::kPoseMessageBudget );
            budget.reserveBits( writer.getBitCount() + 1 ); // 머리와 끝 표시
            size_t count = 0;
            while ( start < listPose.size() )
            {
                const FractureGroupPose& pose = *listPose[start];
                const int32              mark = writer.getBitCount();
                writer.writeBool( true );
                writer.writeVarUint( pose._groupID );
                // 질량 중심과 회전을 보낸다. 원점은 덩어리에서 멀 수 있어 회전하면 크게 움직이고, 보간이 그 움직임을 직선으로 연결하게 되기 때문이다.
                const bool bWritten = bRest ? Internal::writeExactPose( writer, pose._center, pose._rotation ) : Internal::writeMovingPose( writer, pose._center, pose._rotation );
                if ( bWritten == false )
                {
                    writer.truncate( mark );
                    ++_stats._outOfRangePoseCount; // 유한하지 않은 자세 — 이 덩어리만 뺀다
                    ++start;
                    continue;
                }
                if ( budget.tryReserveBits( writer.getBitCount() - mark ) == false )
                {
                    writer.truncate( mark );
                    if ( count == 0 )
                        return; // 빈 메시지에도 들지 않는 자세 — 자세 하나는 수십 바이트라 일어나지 않는다. 같은 자리를 다시 돌지 않는다
                    break;      // 다음 메시지로 넘긴다
                }
                ++count;
                ++start;
            }
            writer.writeBool( false );
            if ( count == 0 )
                continue;
            (void)broadcastMessage(); // 채널은 시작에서 골랐다
            ++_stats._poseMessageCount;
            _stats._restPoseCount += bReliable ? count : 0u;
        }
    }

    void DestructionReplicationServer::sendHash( const Entry& entry, const FractureBaseComponent& component )
    {
        const uint32 eventCount = component.getState().getEventCount();
        // 쌓였지만 아직 적용하지 않은 피해가 있어도 된다. (이벤트 수, 해시)는 그 순간의 상태 그대로이고, 쌓인 피해는 뒤 번호로 보낸다.
        if ( component.isStateReady() == false || eventCount == 0 )
            return;
        DestructionHashBody body;
        body._netID      = entry._netID;
        body._eventCount = eventCount;
        body._hash       = component.getState().computeStateHash();
        if ( _writer.writeMessage<NetDestructionMessage::Table, NetDestructionMessage::Hash>( body ) == false )
            return;
        (void)broadcastMessage();
        ++_stats._hashMessageCount;
    }
} // namespace sw

namespace sw
{
    DestructionReplicationClient::DestructionReplicationClient()
        : _listEntry{}
        , _settings{}
        , _stats{}
        , _writer{}
        , _pHost{ nullptr }
        , _pManager{ nullptr }
        , _pWorldClock{ nullptr }
        , _renderClock{}
    {
    }

    void DestructionReplicationClient::initialize( INetPeers* pHost, GameObjectManager* pManager, NetClock& worldClock, const DestructionReplicationConfig& settings )
    {
        _pHost    = pHost;
        _pManager = pManager;
        _settings = settings;
        _listEntry.clear();
        _stats       = DestructionReplicationStats{};
        _pWorldClock = &worldClock;
        NetRenderClockConfig clockSettings;
        clockSettings._interpolationDelay = settings._interpolationDelay; // 표본 간격은 등록하는 오브젝트의 자세 간격으로 키운다(registerObject)
        _renderClock.initialize( clockSettings );
    }

    void DestructionReplicationClient::registerObject( NetObjectID netID, FractureBaseComponent& component )
    {
        component.setAuthority( false );
        Entry* pEntry = findEntry( netID );
        if ( pEntry == nullptr )
        {
            _listEntry.emplace_back();
            pEntry = &_listEntry.back();
        }
        *pEntry            = Entry{};
        pEntry->_netID     = netID;
        pEntry->_component = TComponentHandle<FractureBaseComponent>{ &component };
        _renderClock.setSampleInterval( MathUtil::max( _renderClock.getSampleInterval(), DestructionReplicationInternal::computePosePeriod( component ) ) );
    }

    void DestructionReplicationClient::skipNextEvent( NetObjectID netID )
    {
        Entry* pEntry = findEntry( netID );
        if ( pEntry != nullptr )
            ++pEntry->_skipCount;
    }

    DestructionReplicationClient::Entry* DestructionReplicationClient::findEntry( NetObjectID netID )
    {
        return VectorUtil::findBy<&Entry::_netID>( _listEntry, netID );
    }

    FractureBaseComponent* DestructionReplicationClient::resolve( const Entry& entry ) const
    {
        return DestructionReplicationInternal::resolveFracture( _pManager, entry._component );
    }

    NetHandleResult DestructionReplicationClient::receive( NetDestructionMessage::Pose, const NetMessageContext& context, BitReader& body )
    {
        (void)context;
        const bool bRest = body.readBool();
        // 깨진 자세(모자란 비트, 유한하지 않은 값, 범위 밖 칸, 남는 비트)는 하나도 적용하지 않고 깨진 메시지로 돌려준다.
        return handlePose( body, bRest ) ? NetHandleResult::Handled : NetHandleResult::Malformed;
    }

    DestructionReplicationClient::Entry* DestructionReplicationClient::readEntry( BitReader& reader, NetHandleResult& outEarly )
    {
        const NetObjectID netID = reader.readVarUint();
        if ( reader.hasOverflowed() )
        {
            outEarly = NetHandleResult::Malformed;
            return nullptr;
        }
        Entry* pEntry = findEntry( netID );
        if ( pEntry == nullptr )
            outEarly = NetHandleResult::Handled; // 이 클라이언트에 없는 오브젝트 — 소비하고 버린다
        return pEntry;
    }

    NetHandleResult DestructionReplicationClient::receive( NetDestructionMessage::Event, const NetMessageContext& context, BitReader& body )
    {
        (void)context;
        BitReader&      reader = body;
        NetHandleResult early  = NetHandleResult::Handled;
        Entry*          pEntry = readEntry( reader, early );
        if ( pEntry == nullptr )
            return early;
        const uint32 serverTick = reader.readVarUint32();
        const uint32 index      = reader.readVarUint32();
        observeTick( serverTick );
        BufferedEvent buffered;
        buffered._index = index;
        // 예전에는 등록 밖 피해 종류가 넘침 없이 false 를 돌려 Handled 로 조용히 버려졌다 — 깨진 메시지로 센다.
        if ( DestructionReplicationInternal::readEvent( reader, buffered._event ) == false )
            return NetHandleResult::Malformed;
        buffered._bHasPose = reader.readBool() ? SW_TRUE : SW_FALSE;
        if ( buffered._bHasPose == SW_TRUE )
        {
            buffered._groupPose._groupID = buffered._event._groupID;
            const bool bPoseRead         = DestructionReplicationInternal::readExactPose( reader, buffered._groupPose._position, buffered._groupPose._rotation );
            if ( bPoseRead == false )
                return NetHandleResult::Malformed;
        }
        if ( reader.readEnd() == false )
            return NetHandleResult::Malformed;
        handleEvent( *pEntry, buffered );
        return NetHandleResult::Handled;
    }

    NetHandleResult DestructionReplicationClient::receive( NetDestructionMessage::Snapshot, const NetMessageContext& context, const DestructionSnapshotBody& body )
    {
        (void)context;
        if ( body._bytes.empty() )
            return NetHandleResult::Malformed; // 서버는 이벤트가 없으면 스냅숏을 보내지 않는다
        Entry* pEntry = findEntry( body._netID );
        if ( pEntry != nullptr ) // 이 클라이언트에 없는 오브젝트이면 메시지를 소비하고 버린다
            handleSnapshot( *pEntry, body );
        return NetHandleResult::Handled;
    }

    NetHandleResult DestructionReplicationClient::receive( NetDestructionMessage::Hash, const NetMessageContext& context, const DestructionHashBody& body )
    {
        (void)context;
        Entry* pEntry = findEntry( body._netID );
        if ( pEntry == nullptr )
            return NetHandleResult::Handled;
        HashCheck check;
        check._eventCount = body._eventCount;
        check._hash       = body._hash;
        pEntry->_listHashCheck.push_back( check );
        return NetHandleResult::Handled;
    }

    void DestructionReplicationClient::handleEvent( Entry& entry, const BufferedEvent& received )
    {
        const uint32 index = received._index;
        if ( index < entry._nextEventIndex )
        {
            ++_stats._staleEventCount; // 스냅숏이 이미 담은 이벤트이다
            return;
        }
        if ( entry._bAwaitingSnapshot == SW_TRUE || index > entry._nextEventIndex )
        {
            // 앞 번호를 기다리는 중이거나 요청한 스냅숏을 기다리는 중이다. 앞 번호를 기다리는 경우는 늦게 들어와 스냅숏이 아직 안 왔거나 앞 이벤트를 잃은 때이다.
            // 지금 적용하면 나중에 도착한 스냅숏이 그 상태를 덮고 번호를 되돌려서 이 이벤트가 영영 빠진다. 이벤트는 순서 없는 채널이라 스냅숏보다 먼저 올 수 있다.
            // 그래서 쌓아 두었다가 스냅숏 뒤에 번호 순서로 적용한다.
            bufferEvent( entry, received );
            return;
        }
        FractureBaseComponent* pComponent = resolve( entry );
        if ( entry._skipCount > 0 )
        {
            --entry._skipCount;
            ++_stats._skippedEventCount;
        }
        else if ( pComponent != nullptr )
        {
            applyToComponent( *pComponent, received );
        }
        ++entry._nextEventIndex;
        // 기다리던 다음 번호의 이벤트를 이어서 적용한다.
        bool bProgress = true;
        while ( bProgress )
        {
            bProgress = false;
            for ( size_t buffered = 0; buffered < entry._listFutureEvent.size(); ++buffered )
            {
                if ( entry._listFutureEvent[buffered]._index != entry._nextEventIndex )
                    continue;
                if ( pComponent != nullptr )
                    applyToComponent( *pComponent, entry._listFutureEvent[buffered] );
                ++entry._nextEventIndex;
                entry._listFutureEvent.erase( entry._listFutureEvent.begin() + static_cast<std::ptrdiff_t>( buffered ) );
                bProgress = true;
                break;
            }
        }
    }

    void DestructionReplicationClient::bufferEvent( Entry& entry, const BufferedEvent& received )
    {
        for ( const BufferedEvent& buffered : entry._listFutureEvent )
        {
            if ( buffered._index == received._index )
                return;
        }
        entry._listFutureEvent.push_back( received );
    }

    void DestructionReplicationClient::applyToComponent( FractureBaseComponent& component, const BufferedEvent& buffered )
    {
        if ( buffered._bHasPose == SW_TRUE )
            component.applyDamage( buffered._event, buffered._groupPose );
        else
            component.applyDamage( buffered._event );
    }

    void DestructionReplicationClient::handleSnapshot( Entry& entry, const DestructionSnapshotBody& body )
    {
        const uint32 eventCount = body._eventCount;

        FractureBaseComponent* pComponent = resolve( entry );
        if ( pComponent != nullptr )
            pComponent->applyNetworkSnapshot( body._bytes.data(), body._bytes.size() );
        ++_stats._snapshotAppliedCount;
        entry._bAwaitingSnapshot = SW_FALSE;
        entry._nextEventIndex    = eventCount;
        entry._skipCount         = 0;
        entry._listHashCheck.clear();
        // 스냅숏보다 앞 번호는 버리고, 이어지는 번호는 적용한다.
        vector<BufferedEvent> listFuture;
        listFuture.swap( entry._listFutureEvent );
        VectorUtil::sort( listFuture, []( const BufferedEvent& lhs, const BufferedEvent& rhs )
        { return lhs._index < rhs._index; } );
        for ( const BufferedEvent& buffered : listFuture )
        {
            if ( buffered._index >= entry._nextEventIndex )
                handleEvent( entry, buffered );
        }
    }

    bool DestructionReplicationClient::handlePose( BitReader& reader, bool bRest )
    {
        using Internal                        = DestructionReplicationInternal;
        const NetObjectID               netID = reader.readVarUint();
        const uint32                    tick  = reader.readVarUint32();
        vector<pair<uint32, ChunkPose>> listPose;
        while ( reader.hasOverflowed() == false && reader.readBool() )
        {
            // 보내는 쪽은 예산(`kPoseMessageBudget` 바이트)에서 끊고 자세 하나가 1 바이트를 넘는다 — 그보다 많으면 깨진 것이다.
            if ( listPose.size() >= static_cast<size_t>( Internal::kPoseMessageBudget ) )
                return false;
            ChunkPose    pose;
            const uint32 groupID = reader.readVarUint32();
            const bool   bRead   = bRest ? Internal::readExactPose( reader, pose._position, pose._rotation ) : Internal::readMovingPose( reader, pose._position, pose._rotation );
            if ( bRead == false || reader.hasOverflowed() )
                return false; // 유한하지 않은 자세 · 범위 밖 칸 — 뒤 자세의 위치도 믿을 수 없다
            listPose.emplace_back( groupID, pose );
        }
        if ( reader.readEnd() == false )
            return false;
        observeTick( tick );
        Entry* pEntry = findEntry( netID );
        if ( pEntry == nullptr )
            return true; // 이 클라이언트에 없는 오브젝트 — 소비하고 버린다
        for ( const pair<uint32, ChunkPose>& received : listPose )
        {
            const uint32 groupID = received.first;
            ChunkTrack*  pTrack  = nullptr;
            for ( ChunkTrack& track : pEntry->_listChunk )
            {
                if ( track._groupID == groupID )
                    pTrack = &track;
            }
            if ( pTrack == nullptr )
            {
                pEntry->_listChunk.emplace_back();
                pTrack           = &pEntry->_listChunk.back();
                pTrack->_groupID = groupID;
                pTrack->_poseBuffer.initialize( Internal::kMaxPoseSample );
            }
            // 틱 순서로 끼워 넣는다. 멈춤 확정은 다른 채널로 오므로 앞질러 도착할 수 있다. 같은 틱이면 멈춤 쪽이 이기고, 넘치면 가장 오래된 표본을 버린다.
            pTrack->_poseBuffer.insert( tick, received.second, bRest );
        }
        return true;
    }

    void DestructionReplicationClient::observeTick( uint32 serverTick )
    {
        if ( _pWorldClock == nullptr )
            return; // 시작하지 않은 클라이언트다(등록한 오브젝트도 없다)
        _pWorldClock->observe( serverTick );
        _renderClock.observeSample( serverTick );
    }

    void DestructionReplicationClient::onWorldOpened( uint32 generation )
    {
        (void)generation;
        _renderClock.reset(); // 새 월드의 틱은 앞 월드보다 작다(월드의 시계는 라우터가 되돌린다)
        for ( Entry& entry : _listEntry )
        {
            const bool bTouched = entry._nextEventIndex != 0;
            entry._listFutureEvent.clear();
            entry._listChunk.clear();
            entry._listHashCheck.clear();
            entry._nextEventIndex    = 0;
            entry._bAwaitingSnapshot = SW_FALSE;
            if ( bTouched )
                requestSnapshot( entry ); // 이쪽에는 앞 월드의 파편이 남아 있다
        }
    }

    void DestructionReplicationClient::update()
    {
        // 서버 틱 추정값은 틱마다 흐르고 받은 틱보다 뒤처지지 않는다. 그래서 자세가 오지 않는 동안(모두 멈춘 상태)에도 렌더 틱이 서버 시각을 따라간다(NetClock, 받은 가장 새 틱까지).
        // 지연은 설정값과 가장 긴 자세 간격 × 2 중 큰 값이다. 간격 하나만 두면 자세 하나를 잃거나 지터로 늦을 때마다 뒤 자세가 없어 덩어리가 멈춰 선다.
        // 빨리 떨어지는 덩어리는 이때 미터 단위로 어긋난다.
        if ( _pWorldClock != nullptr )
            _renderClock.follow( *_pWorldClock );
        for ( Entry& entry : _listEntry )
        {
            FractureBaseComponent* pComponent = resolve( entry );
            if ( pComponent == nullptr )
                continue;
            compareHashes( entry, *pComponent );
            driveChunks( entry, *pComponent );
        }
    }

    void DestructionReplicationClient::compareHashes( Entry& entry, FractureBaseComponent& component )
    {
        if ( entry._bAwaitingSnapshot == SW_TRUE || component.isStateReady() == false )
            return;
        const bool   bPending   = component.hasPendingDamage();
        const uint32 stateCount = component.getState().getEventCount();
        for ( size_t index = 0; index < entry._listHashCheck.size(); )
        {
            const HashCheck check = entry._listHashCheck[index];
            if ( entry._nextEventIndex < check._eventCount )
            {
                ++index; // 그 번호까지의 이벤트가 아직 오는 중이다
                continue;
            }
            // 그 이벤트 수에 이르렀을 때의 해시와 비교한다. 해시는 이벤트마다 적어 둔 값이다.
            // 이벤트를 모두 넘겨받고 쌓인 것까지 적용했는데도 상태가 그 이벤트 수에 이르지 못했다면 이벤트가 빠진 것이다.
            uint64     localHash = 0;
            const bool bFound    = component.findRecentStateHash( check._eventCount, localHash );
            if ( bFound == false && bPending )
            {
                ++index; // 넘겨받은 이벤트가 다음 물리 프레임에 적용된다
                continue;
            }
            entry._listHashCheck.erase( entry._listHashCheck.begin() + static_cast<std::ptrdiff_t>( index ) );
            if ( bFound )
            {
                if ( localHash == check._hash )
                {
                    ++_stats._hashMatchCount;
                    continue;
                }
            }
            else if ( stateCount >= check._eventCount )
            {
                continue; // 너무 오래된 비교이므로 다음 해시로 넘어간다
            }
            requestSnapshot( entry );
            return;
        }
    }

    void DestructionReplicationClient::requestSnapshot( Entry& entry )
    {
        ++_stats._hashMismatchCount;
        entry._bAwaitingSnapshot = SW_TRUE;
        entry._listHashCheck.clear();
        if ( _pHost == nullptr )
            return;
        DestructionSnapshotRequestBody body;
        body._netID = entry._netID;
        if ( _writer.writeMessage<NetDestructionMessage::Table, NetDestructionMessage::SnapshotRequest>( body ) &&
             _writer.sendMessage( *_pHost, 0 ) )
            _stats._sentBytes += static_cast<uint64>( _writer.getByteCount() );
    }

    void DestructionReplicationClient::driveChunks( Entry& entry, FractureBaseComponent& component )
    {
        const float64 renderTick = _renderClock.getRenderTick();
        if ( renderTick < 0.0 )
            return;
        // 그룹 번호는 늘어나기만 한다. 지금 가장 큰 번호보다 작은데 없는 그룹은 갈라져 사라진 것이고, 큰 번호는 그 이벤트가 아직 오지 않은 것이다. 자세를 먼저 받은 경우이다.
        uint32 maxGroupID = 0;
        for ( const DestructionGroup& group : component.getState().getGroups() )
        {
            maxGroupID = MathUtil::max( maxGroupID, group._id );
        }
        for ( size_t trackIndex = 0; trackIndex < entry._listChunk.size(); )
        {
            ChunkTrack& track     = entry._listChunk[trackIndex];
            const bool  bHasGroup = component.getState().findGroup( track._groupID ) != nullptr;
            const bool  bRemoved  = bHasGroup == false && track._groupID < maxGroupID && component.hasPendingDamage() == false;
            if ( bRemoved )
            {
                entry._listChunk.erase( entry._listChunk.begin() + static_cast<std::ptrdiff_t>( trackIndex ) );
                continue;
            }
            ++trackIndex;
            if ( bHasGroup == false )
                continue;
            // 렌더 틱을 사이에 둔 두 자세를 고른다. 뒤 자세가 없으면 마지막 것을 쓰고 미래를 예측하지 않는다.
            const InterpolationBuffer<ChunkPose>::Sample* pFrom = nullptr;
            const InterpolationBuffer<ChunkPose>::Sample* pTo   = nullptr;
            NetSampleRatio                                ratio = NetSampleRatio::makeHeld();
            const InterpolationBracketKind                kind  = track._poseBuffer.findBracket( renderTick, pFrom, pTo, ratio );
            // 첫 자세보다 이전 시점을 그리는 동안은 바디를 움직이지 않는다. 그 덩어리는 같은 이벤트와 같은 시드로 서버와 같은 속도로 생성되어 혼자 날아간다.
            const bool bBeforeFirst = kind == InterpolationBracketKind::Empty || kind == InterpolationBracketKind::BeforeFirst;
            if ( bBeforeFirst )
                continue;
            const float3     position = ratio.interpolate( pFrom->_value._position, pTo->_value._position );
            const quaternion rotation = ratio.interpolate( pFrom->_value._rotation, pTo->_value._rotation );
            component.driveGroup( track._groupID, position, rotation );
            // 지난 표본은 하나만 남긴다. 보간의 앞쪽 표본으로 쓴다.
            track._poseBuffer.removeConsumed( renderTick );
        }
    }
} // namespace sw
