#include "pch.h"

#include "GameFramework/Kits/Feature/Network/NetDestruction/DestructionReplication.h"

#include "Core/Container/VectorUtil.h"
#include "Core/Math/MathUtil.h"
#include "Core/Network/BitStream.h"
#include "Core/Network/Connection/NetConnection.h"
#include "Core/Network/Connection/NetHost.h"

#include "Engine/Destruction/FractureComponentBase.h"
#include "Engine/Object/GameObject/GameObjectManager.h"
#include "Engine/Reflection/ReflectionCast.h"

namespace sw
{
    namespace
    {
        struct DestructionReplicationInternal
        {
            static constexpr float32 kQuaternionResolution = 1.0f / 32767.0f;
            static constexpr int32   kPoseMessageBudget    = 900; ///< 자세 메시지 하나를 이 크기에서 끊는다(한도 1024)
            static constexpr uint8   kRestRepeatCount      = 5;   ///< 멈춘 자세를 비신뢰로 되풀이해 보내는 횟수(자세 주기마다 한 번)
            static constexpr int32   kMaxPoseSample        = 32;  ///< 덩어리마다 보관하는 자세 표본 수

            /** @brief 프로필의 자세 간격(초)입니다. */
            static float32 computePosePeriod( const FractureComponentBase& component ) { return 1.0f / MathUtil::max( 0.1f, component.getProfile()._networkPoseRate ); }

            static void writeEvent( BitWriter& writer, const DestructionDamageEvent& event )
            {
                writer.writeBits( static_cast<uint32>( event._kind ), 2 );
                writer.writeVarInt( event._leafHint );
                writer.writeVarUint( event._groupID );
                // 실수는 비트 그대로 보낸다. 받는 쪽 상태가 같아야 하고, 양자화하면 다른 리프가 깨질 수 있다.
                writer.writeFloat( event._position._x );
                writer.writeFloat( event._position._y );
                writer.writeFloat( event._position._z );
                writer.writeFloat( event._direction._x );
                writer.writeFloat( event._direction._y );
                writer.writeFloat( event._direction._z );
                writer.writeFloat( event._strain );
                writer.writeFloat( event._radius );
                writer.writeFloat( event._impulse );
            }

            [[nodiscard]] static bool readEvent( BitReader& reader, DestructionDamageEvent& outEvent )
            {
                const uint32 kind = reader.readBits( 2 );
                if ( kind >= static_cast<uint32>( DestructionDamageKind::Count ) )
                    return false;
                outEvent._kind         = static_cast<DestructionDamageKind>( kind );
                outEvent._leafHint     = static_cast<int32>( reader.readVarInt() );
                outEvent._groupID      = static_cast<uint32>( reader.readVarUint() );
                outEvent._position._x  = reader.readFloat();
                outEvent._position._y  = reader.readFloat();
                outEvent._position._z  = reader.readFloat();
                outEvent._direction._x = reader.readFloat();
                outEvent._direction._y = reader.readFloat();
                outEvent._direction._z = reader.readFloat();
                outEvent._strain       = reader.readFloat();
                outEvent._radius       = reader.readFloat();
                outEvent._impulse      = reader.readFloat();
                return reader.hasOverflowed() == false;
            }

            static void writeExactPose( BitWriter& writer, const float3& position, const quaternion& rotation )
            {
                writer.writeFloat( position._x );
                writer.writeFloat( position._y );
                writer.writeFloat( position._z );
                writer.writeFloat( rotation._x );
                writer.writeFloat( rotation._y );
                writer.writeFloat( rotation._z );
                writer.writeFloat( rotation._w );
            }

            static void readExactPose( BitReader& reader, float3& outPosition, quaternion& outRotation )
            {
                outPosition._x = reader.readFloat();
                outPosition._y = reader.readFloat();
                outPosition._z = reader.readFloat();
                outRotation._x = reader.readFloat();
                outRotation._y = reader.readFloat();
                outRotation._z = reader.readFloat();
                outRotation._w = reader.readFloat();
            }

            /**
             * @brief 움직이는 자세를 양자화 범위(`_positionRange`, 사원수 ±1)에 실을 수 있는가입니다.
             * @details 범위 밖 덩어리는 움직이는 자세로 보내지 않는다(예전에는 경계로 잘려 받는 쪽에서 경계에 붙어 보였다). 멈추면 비트 그대로 보내므로 맞는 자리로 간다.
             */
            static bool canWriteQuantizedPose( const float3& position, const quaternion& rotation, const DestructionReplicationConfig& settings )
            {
                const float32 range      = settings._positionRange;
                const float32 resolution = settings._positionResolution;
                const bool    bPosition  = BitWriter::canWriteQuantizedFloat( position._x, -range, range, resolution ) &&
                                       BitWriter::canWriteQuantizedFloat( position._y, -range, range, resolution ) &&
                                       BitWriter::canWriteQuantizedFloat( position._z, -range, range, resolution );
                const bool bRotation = BitWriter::canWriteQuantizedFloat( rotation._x, -1.0f, 1.0f, kQuaternionResolution ) &&
                                       BitWriter::canWriteQuantizedFloat( rotation._y, -1.0f, 1.0f, kQuaternionResolution ) &&
                                       BitWriter::canWriteQuantizedFloat( rotation._z, -1.0f, 1.0f, kQuaternionResolution ) &&
                                       BitWriter::canWriteQuantizedFloat( rotation._w, -1.0f, 1.0f, kQuaternionResolution );
                return bPosition && bRotation;
            }

            [[nodiscard]] static bool writeQuantizedPose( BitWriter& writer, const float3& position, const quaternion& rotation, const DestructionReplicationConfig& settings )
            {
                const float32 range = settings._positionRange;
                return writer.writeQuantizedFloat3( position, float3( -range, -range, -range ), float3( range, range, range ), settings._positionResolution ) &&
                       writer.writeQuantizedFloat( rotation._x, -1.0f, 1.0f, kQuaternionResolution ) &&
                       writer.writeQuantizedFloat( rotation._y, -1.0f, 1.0f, kQuaternionResolution ) &&
                       writer.writeQuantizedFloat( rotation._z, -1.0f, 1.0f, kQuaternionResolution ) &&
                       writer.writeQuantizedFloat( rotation._w, -1.0f, 1.0f, kQuaternionResolution );
            }

            [[nodiscard]] static bool readQuantizedPose( BitReader& reader, float3& outPosition, quaternion& outRotation, const DestructionReplicationConfig& settings )
            {
                const float32 range = settings._positionRange;
                const bool    bRead = reader.readQuantizedFloat3( float3( -range, -range, -range ), float3( range, range, range ), settings._positionResolution, outPosition ) &&
                                   reader.readQuantizedFloat( -1.0f, 1.0f, kQuaternionResolution, outRotation._x ) &&
                                   reader.readQuantizedFloat( -1.0f, 1.0f, kQuaternionResolution, outRotation._y ) &&
                                   reader.readQuantizedFloat( -1.0f, 1.0f, kQuaternionResolution, outRotation._z ) &&
                                   reader.readQuantizedFloat( -1.0f, 1.0f, kQuaternionResolution, outRotation._w );
                if ( bRead == false )
                    return false;
                outRotation.normalize();
                return true;
            }

            static FractureComponentBase* resolveFracture( GameObjectManager* pManager, TComponentHandle<FractureComponentBase> handle )
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
    }

    void DestructionReplicationServer::registerObject( uint32 netID, FractureComponentBase& component )
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
        pEntry->_component      = TComponentHandle<FractureComponentBase>{ &component };
        pEntry->_sentEventCount = static_cast<uint32>( component.getEventLog()._listEvent.size() );
    }

    DestructionReplicationServer::Entry* DestructionReplicationServer::findEntry( uint32 netID )
    {
        return VectorUtil::findBy<&Entry::_netID>( _listEntry, netID );
    }

    FractureComponentBase* DestructionReplicationServer::resolve( const Entry& entry ) const
    {
        return DestructionReplicationInternal::resolveFracture( _pManager, entry._component );
    }

    void DestructionReplicationServer::onConnectionOpened( int32 connectionID )
    {
        for ( const Entry& entry : _listEntry )
        {
            _listRequest.push_back( Request{ connectionID, entry._netID } );
        }
    }

    void DestructionReplicationServer::onConnectionClosed( int32 connectionID, NetDisconnectReason reason )
    {
        (void)reason;
        for ( size_t index = _listRequest.size(); index > 0; --index )
        {
            if ( _listRequest[index - 1]._connectionID == connectionID )
                _listRequest.erase( _listRequest.begin() + static_cast<std::ptrdiff_t>( index - 1 ) );
        }
    }

    NetHandleResult DestructionReplicationServer::handleNetMessage( const NetMessageContext& context, BitReader& body )
    {
        const uint32 netID = static_cast<uint32>( body.readVarUint() );
        if ( body.hasOverflowed() || context._connectionID < 0 )
            return NetHandleResult::Malformed;
        if ( findEntry( netID ) != nullptr )
            _listRequest.push_back( Request{ context._connectionID, netID } );
        return NetHandleResult::Handled;
    }

    int32 DestructionReplicationServer::broadcast( NetChannelType channel )
    {
        const int32 connectedCount = _pHost->getConnectedCount();
        const int32 sentCount      = _writer.broadcast( *_pHost, channel );
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
        for ( Entry& entry : _listEntry )
        {
            FractureComponentBase* pComponent = resolve( entry );
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
            FractureComponentBase* pComponent = pEntry != nullptr ? resolve( *pEntry ) : nullptr;
            if ( pComponent != nullptr && sendSnapshot( *pEntry, *pComponent, request._connectionID ) == false )
                _listRequest.push_back( request ); // 신뢰 창이 찼으므로 다음 틱에 다시 보낸다. 연결이 닫히면 onConnectionClosed 가 지운다
        }
        for ( Entry& entry : _listEntry )
        {
            FractureComponentBase* pComponent = resolve( entry );
            if ( pComponent != nullptr )
                sendPoses( entry, *pComponent, serverTick );
        }
        _hashTime += _settings._tickInterval;
        if ( _hashTime + 1.0e-4f >= _settings._hashInterval )
        {
            _hashTime = 0.0f;
            for ( const Entry& entry : _listEntry )
            {
                const FractureComponentBase* pComponent = resolve( entry );
                if ( pComponent != nullptr )
                    sendHash( entry, *pComponent );
            }
        }
    }

    void DestructionReplicationServer::sendEvents( Entry& entry, FractureComponentBase& component, uint32 serverTick )
    {
        // 서버는 스냅숏을 받지 않으므로 기록의 인덱스가 곧 상태의 이벤트 번호이다.
        const vector<DestructionDamageEvent>& listEvent     = component.getEventLog()._listEvent;
        const vector<FractureGroupPose>&      listGroupPose = component.getEventGroupPoses();
        for ( uint32 index = entry._sentEventCount; index < static_cast<uint32>( listEvent.size() ); ++index )
        {
            BitWriter& writer = _writer.begin( NetDestructionMessage::kEvent );
            writer.writeVarUint( entry._netID );
            writer.writeVarUint( serverTick );
            writer.writeVarUint( index );
            DestructionReplicationInternal::writeEvent( writer, listEvent[index] );
            // 떨어진 덩어리를 맞힌 이벤트이면 맞기 직전의 그 덩어리 자세를 비트 그대로 싣는다. 신뢰 채널이 밀려 자세가 늦어도 받는 쪽이 그 위치에서 덩어리를 가를 수 있다.
            const bool bHasPose = index < listGroupPose.size() && listGroupPose[index]._groupID != 0;
            writer.writeBool( bHasPose );
            if ( bHasPose )
                DestructionReplicationInternal::writeExactPose( writer, listGroupPose[index]._position, listGroupPose[index]._rotation );
            (void)broadcast( NetChannelType::ReliableUnordered ); // 받는 쪽이 번호로 순서를 맞춘다. 앞 이벤트를 잃어도 뒤 이벤트가 기다리지 않는다
            ++_stats._eventMessageCount;
        }
        entry._sentEventCount = static_cast<uint32>( listEvent.size() );
    }

    bool DestructionReplicationServer::sendSnapshot( Entry& entry, FractureComponentBase& component, int32 connectionID )
    {
        const uint32 eventCount = component.getState().getEventCount();
        if ( component.isStateReady() == false || eventCount == 0 )
            return true; // 처음 상태 그대로이므로 이벤트는 0번부터 보낸다
        vector<uint8> bytes;
        component.makeNetworkSnapshot( bytes );
        BitWriter& writer = _writer.begin( NetDestructionMessage::kSnapshot );
        writer.writeVarUint( entry._netID );
        writer.writeVarUint( eventCount );
        writer.writeBlob( bytes.data(), static_cast<int32>( bytes.size() ) );
        if ( _writer.getByteCount() > NetConnection::kMaxReliableMessageSize )
        {
            // 다시 만들어도 같은 결과이므로 큐에 되돌려 놓지 않는다. 이 오브젝트는 늦은 참가와 복구를 받지 못하며, 해시 비교가 계속 어긋남을 알린다.
            SW_LOG_ERROR( "DestructionReplication: the snapshot of object %# is %# bytes, over the reliable message limit %# - it cannot be sent",
                          entry._netID, _writer.getByteCount(), NetConnection::kMaxReliableMessageSize );
            ++_stats._sendRejectedCount;
            return true;
        }
        if ( _writer.send( *_pHost, connectionID, NetChannelType::ReliableOrdered ) == false )
        {
            ++_stats._sendRejectedCount;
            return false;
        }
        _stats._sentBytes += static_cast<uint64>( _writer.getByteCount() );
        ++_stats._snapshotCount;
        return true;
    }

    void DestructionReplicationServer::sendPoses( Entry& entry, FractureComponentBase& component, uint32 serverTick )
    {
        using Internal = DestructionReplicationInternal;
        if ( component.isStateReady() == false || component.isFractured() == false )
            return;
        const float32 period = Internal::computePosePeriod( component );
        entry._poseTime += _settings._tickInterval;
        const bool bDue = entry._poseTime + 1.0e-4f >= period;
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
                if ( bDue && Internal::canWriteQuantizedPose( pose._center, pose._rotation, _settings ) )
                {
                    listMoving.push_back( &pose );
                }
                else if ( bDue )
                {
                    // 이 서버에서 처음 넘는 순간만 알린다. 넘은 덩어리는 틱마다 다시 걸리므로 매번 알리면 로그가 넘친다. 그 뒤는 `_outOfRangePoseCount` 가 센다.
                    if ( _stats._outOfRangePoseCount == 0 )
                        SW_LOG_WARNING( "DestructionReplication: chunk %# of object %# moved outside the position range +-%# m - its moving pose is not sent until it rests "
                                        "(raise DestructionReplicationConfig::_positionRange)",
                                        pose._groupID, entry._netID, _settings._positionRange );
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
            BitWriter& writer = _writer.begin( NetDestructionMessage::kPose );
            writer.writeBool( bRest );
            writer.writeVarUint( entry._netID );
            writer.writeVarUint( serverTick );
            // 개수는 끝에서야 알 수 있으므로 몇 개를 실을지 먼저 정한다. 예산 안에서 남은 최대 개수로 정한다.
            const int32  perPoseBytes = bRest ? 30 : 15;
            const size_t room         = static_cast<size_t>( MathUtil::max( 1, ( Internal::kPoseMessageBudget - 16 ) / perPoseBytes ) );
            const size_t count        = MathUtil::min( room, listPose.size() - start );
            writer.writeVarUint( count );
            bool bPosesWritten = true;
            for ( size_t index = start; index < start + count; ++index )
            {
                const FractureGroupPose& pose = *listPose[index];
                writer.writeVarUint( pose._groupID );
                // 질량 중심과 회전을 보낸다. 원점은 덩어리에서 멀 수 있어 회전하면 크게 움직이고, 보간이 그 움직임을 직선으로 연결하게 되기 때문이다.
                if ( bRest )
                    Internal::writeExactPose( writer, pose._center, pose._rotation );
                else
                    bPosesWritten = bPosesWritten && Internal::writeQuantizedPose( writer, pose._center, pose._rotation, _settings );
            }
            start += count;
            // 움직이는 자세는 위에서 범위로 걸렀다. 그래도 하나라도 쓰지 못했으면 메시지가 어긋났으므로 보내지 않는다.
            if ( bPosesWritten == false )
                continue;
            (void)broadcast( bReliable ? NetChannelType::ReliableOrdered : NetChannelType::Unreliable );
            ++_stats._poseMessageCount;
            _stats._restPoseCount += bReliable ? count : 0u;
        }
    }

    void DestructionReplicationServer::sendHash( const Entry& entry, const FractureComponentBase& component )
    {
        const uint32 eventCount = component.getState().getEventCount();
        // 쌓였지만 아직 적용하지 않은 피해가 있어도 된다. (이벤트 수, 해시)는 그 순간의 상태 그대로이고, 쌓인 피해는 뒤 번호로 보낸다.
        if ( component.isStateReady() == false || eventCount == 0 )
            return;
        BitWriter& writer = _writer.begin( NetDestructionMessage::kHash );
        writer.writeVarUint( entry._netID );
        writer.writeVarUint( eventCount );
        const uint64 hash = component.getState().computeStateHash();
        writer.writeUint32( static_cast<uint32>( hash & 0xFFFFFFFFull ) );
        writer.writeUint32( static_cast<uint32>( hash >> 32 ) );
        (void)broadcast( NetChannelType::ReliableOrdered );
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
        , _clock{}
    {
    }

    void DestructionReplicationClient::initialize( INetPeers* pHost, GameObjectManager* pManager, const DestructionReplicationConfig& settings )
    {
        _pHost    = pHost;
        _pManager = pManager;
        _settings = settings;
        _listEntry.clear();
        _stats = DestructionReplicationStats{};
        NetClockConfig clockSettings;
        clockSettings._tickInterval       = settings._tickInterval;
        clockSettings._interpolationDelay = settings._interpolationDelay;
        clockSettings._sampleInterval     = 0.0f; // 등록하는 오브젝트의 자세 간격으로 키운다(registerObject)
        _clock.initialize( clockSettings );
    }

    void DestructionReplicationClient::registerObject( uint32 netID, FractureComponentBase& component )
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
        pEntry->_component = TComponentHandle<FractureComponentBase>{ &component };
        _clock.setSampleInterval( MathUtil::max( _clock.getSampleInterval(), DestructionReplicationInternal::computePosePeriod( component ) ) );
    }

    void DestructionReplicationClient::skipNextEvent( uint32 netID )
    {
        Entry* pEntry = findEntry( netID );
        if ( pEntry != nullptr )
            ++pEntry->_skipCount;
    }

    DestructionReplicationClient::Entry* DestructionReplicationClient::findEntry( uint32 netID )
    {
        return VectorUtil::findBy<&Entry::_netID>( _listEntry, netID );
    }

    FractureComponentBase* DestructionReplicationClient::resolve( const Entry& entry ) const
    {
        return DestructionReplicationInternal::resolveFracture( _pManager, entry._component );
    }

    NetHandleResult DestructionReplicationClient::handleNetMessage( const NetMessageContext& context, BitReader& body )
    {
        const uint8 kind   = context._kind;
        BitReader&  reader = body;
        if ( kind == NetDestructionMessage::kPose )
        {
            const bool bRest = reader.readBool();
            handlePose( reader, bRest );
            return NetHandleResult::Handled;
        }
        const uint32 netID = static_cast<uint32>( reader.readVarUint() );
        if ( reader.hasOverflowed() )
            return NetHandleResult::Malformed;
        Entry* pEntry = findEntry( netID );
        if ( pEntry == nullptr )
            return NetHandleResult::Handled; // 이 클라이언트에 없는 오브젝트이므로 메시지를 소비하고 버린다
        if ( kind == NetDestructionMessage::kEvent )
        {
            const uint32 serverTick = static_cast<uint32>( reader.readVarUint() );
            const uint32 index      = static_cast<uint32>( reader.readVarUint() );
            _clock.observeServerTick( serverTick );
            BufferedEvent buffered;
            buffered._index = index;
            if ( DestructionReplicationInternal::readEvent( reader, buffered._event ) )
            {
                buffered._bHasPose = reader.readBool() ? SW_TRUE : SW_FALSE;
                if ( buffered._bHasPose == SW_TRUE )
                {
                    buffered._groupPose._groupID = buffered._event._groupID;
                    DestructionReplicationInternal::readExactPose( reader, buffered._groupPose._position, buffered._groupPose._rotation );
                }
                if ( reader.hasOverflowed() == false )
                    handleEvent( *pEntry, buffered );
            }
        }
        else if ( kind == NetDestructionMessage::kSnapshot )
        {
            if ( handleSnapshot( *pEntry, reader ) == false )
                return NetHandleResult::Malformed;
        }
        else
        {
            HashCheck check;
            check._eventCount = static_cast<uint32>( reader.readVarUint() );
            const uint64 low  = reader.readUint32();
            const uint64 high = reader.readUint32();
            check._hash       = low | ( high << 32 );
            if ( reader.hasOverflowed() == false )
                pEntry->_listHashCheck.push_back( check );
        }
        return reader.hasOverflowed() ? NetHandleResult::Malformed : NetHandleResult::Handled;
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
        FractureComponentBase* pComponent = resolve( entry );
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

    void DestructionReplicationClient::applyToComponent( FractureComponentBase& component, const BufferedEvent& buffered )
    {
        if ( buffered._bHasPose == SW_TRUE )
            component.applyDamage( buffered._event, buffered._groupPose );
        else
            component.applyDamage( buffered._event );
    }

    bool DestructionReplicationClient::handleSnapshot( Entry& entry, BitReader& reader )
    {
        const uint32  eventCount = static_cast<uint32>( reader.readVarUint() );
        vector<uint8> bytes;
        if ( reader.readBlob( bytes, NetConnection::kMaxReliableMessageSize ) == false || bytes.empty() )
            return false;

        FractureComponentBase* pComponent = resolve( entry );
        if ( pComponent != nullptr )
            pComponent->applyNetworkSnapshot( bytes.data(), bytes.size() );
        ++_stats._snapshotAppliedCount;
        entry._bAwaitingSnapshot = SW_FALSE;
        entry._nextEventIndex    = eventCount;
        entry._skipCount         = 0;
        entry._listHashCheck.clear();
        // 스냅숏보다 앞 번호는 버리고, 이어지는 번호는 적용한다.
        vector<BufferedEvent> listFuture;
        listFuture.swap( entry._listFutureEvent );
        std::sort( listFuture.begin(), listFuture.end(), []( const BufferedEvent& lhs, const BufferedEvent& rhs )
        { return lhs._index < rhs._index; } );
        for ( const BufferedEvent& buffered : listFuture )
        {
            if ( buffered._index >= entry._nextEventIndex )
                handleEvent( entry, buffered );
        }
        return true;
    }

    void DestructionReplicationClient::handlePose( BitReader& reader, bool bRest )
    {
        using Internal     = DestructionReplicationInternal;
        const uint32 netID = static_cast<uint32>( reader.readVarUint() );
        const uint32 tick  = static_cast<uint32>( reader.readVarUint() );
        const uint64 count = reader.readVarUint();
        if ( reader.hasOverflowed() )
            return;
        Entry* pEntry = findEntry( netID );
        _clock.observeServerTick( tick );
        for ( uint64 index = 0; index < count && reader.hasOverflowed() == false; ++index )
        {
            ChunkPose    pose;
            const uint32 groupID = static_cast<uint32>( reader.readVarUint() );
            if ( bRest )
                Internal::readExactPose( reader, pose._position, pose._rotation );
            else if ( Internal::readQuantizedPose( reader, pose._position, pose._rotation, _settings ) == false )
                return; // 범위 밖의 칸은 깨진 메시지다. 뒤 자세의 위치도 믿을 수 없다
            if ( pEntry == nullptr || reader.hasOverflowed() )
                continue;
            ChunkTrack* pTrack = nullptr;
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
            pTrack->_poseBuffer.insert( tick, pose, bRest );
        }
    }

    void DestructionReplicationClient::update( float32 deltaTime )
    {
        // 서버 틱 추정값은 틱마다 흐르고 받은 틱보다 뒤처지지 않는다. 그래서 자세가 오지 않는 동안(모두 멈춘 상태)에도 렌더 틱이 서버 시각을 따라간다(NetClock, 받은 가장 새 틱까지).
        // 지연은 설정값과 가장 긴 자세 간격 × 2 중 큰 값이다. 간격 하나만 두면 자세 하나를 잃거나 지터로 늦을 때마다 뒤 자세가 없어 덩어리가 멈춰 선다.
        // 빨리 떨어지는 덩어리는 이때 미터 단위로 어긋난다.
        _clock.advance( deltaTime );
        for ( Entry& entry : _listEntry )
        {
            FractureComponentBase* pComponent = resolve( entry );
            if ( pComponent == nullptr )
                continue;
            compareHashes( entry, *pComponent );
            driveChunks( entry, *pComponent );
        }
    }

    void DestructionReplicationClient::compareHashes( Entry& entry, FractureComponentBase& component )
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
        BitWriter& writer = _writer.begin( NetDestructionMessage::kSnapshotRequest );
        writer.writeVarUint( entry._netID );
        if ( _writer.send( *_pHost, 0, NetChannelType::ReliableOrdered ) )
            _stats._sentBytes += static_cast<uint64>( _writer.getByteCount() );
    }

    void DestructionReplicationClient::driveChunks( Entry& entry, FractureComponentBase& component )
    {
        const float32 renderTick = _clock.getRenderTick();
        if ( renderTick < 0.0f )
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
            float32                                       alpha = 0.0f;
            const InterpolationBracketKind                kind  = track._poseBuffer.findBracket( renderTick, pFrom, pTo, alpha );
            // 첫 자세보다 이전 시점을 그리는 동안은 바디를 움직이지 않는다. 그 덩어리는 같은 이벤트와 같은 시드로 서버와 같은 속도로 생성되어 혼자 날아간다.
            const bool bBeforeFirst = kind == InterpolationBracketKind::Empty || kind == InterpolationBracketKind::BeforeFirst;
            if ( bBeforeFirst )
                continue;
            float3     position = pFrom->_value._position;
            quaternion rotation = pFrom->_value._rotation;
            if ( kind == InterpolationBracketKind::Between )
            {
                position = pFrom->_value._position + ( pTo->_value._position - pFrom->_value._position ) * alpha;
                rotation = quaternion::lerp( pFrom->_value._rotation, pTo->_value._rotation, alpha );
            }
            component.driveGroup( track._groupID, position, rotation );
            // 지난 표본은 하나만 남긴다. 보간의 앞쪽 표본으로 쓴다.
            track._poseBuffer.removeConsumed( renderTick );
        }
    }
} // namespace sw
