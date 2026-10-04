#include "pch.h"

#include "GameFramework/Kits/Network/NetDestruction/DestructionReplication.h"

#include "Core/Math/MathUtil.h"
#include "Core/Network/BitStream.h"
#include "Core/Network/NetHost.h"

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
            static constexpr int32   kPoseMessageBudget    = 900; ///< 자세 메시지 하나를 이만큼에서 끊는다(한도 1024)
            static constexpr uint8   kRestRepeatCount      = 5;   ///< 멈춘 자세를 비신뢰로 되풀이하는 횟수(자세 주기마다)
            static constexpr uint32  kMaxPoseSample        = 32;

            static void writeEvent( BitWriter& writer, const DestructionDamageEvent& event )
            {
                writer.writeBits( static_cast<uint32>( event._kind ), 2 );
                writer.writeVarInt( event._leafHint );
                writer.writeVarUint( event._groupId );
                // 실수는 비트 그대로 — 받는 쪽 상태가 같아야 한다(양자화하면 다른 잎이 깨질 수 있다).
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
                outEvent._groupId      = static_cast<uint32>( reader.readVarUint() );
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

            static void writeQuantizedPose( BitWriter& writer, const float3& position, const quaternion& rotation, const DestructionReplicationSettings& settings )
            {
                const float32 range = settings._positionRange;
                writer.writeQuantizedFloat( position._x, -range, range, settings._positionResolution );
                writer.writeQuantizedFloat( position._y, -range, range, settings._positionResolution );
                writer.writeQuantizedFloat( position._z, -range, range, settings._positionResolution );
                writer.writeQuantizedFloat( rotation._x, -1.0f, 1.0f, kQuaternionResolution );
                writer.writeQuantizedFloat( rotation._y, -1.0f, 1.0f, kQuaternionResolution );
                writer.writeQuantizedFloat( rotation._z, -1.0f, 1.0f, kQuaternionResolution );
                writer.writeQuantizedFloat( rotation._w, -1.0f, 1.0f, kQuaternionResolution );
            }

            static void readQuantizedPose( BitReader& reader, float3& outPosition, quaternion& outRotation, const DestructionReplicationSettings& settings )
            {
                const float32 range = settings._positionRange;
                outPosition._x      = reader.readQuantizedFloat( -range, range, settings._positionResolution );
                outPosition._y      = reader.readQuantizedFloat( -range, range, settings._positionResolution );
                outPosition._z      = reader.readQuantizedFloat( -range, range, settings._positionResolution );
                outRotation._x      = reader.readQuantizedFloat( -1.0f, 1.0f, kQuaternionResolution );
                outRotation._y      = reader.readQuantizedFloat( -1.0f, 1.0f, kQuaternionResolution );
                outRotation._z      = reader.readQuantizedFloat( -1.0f, 1.0f, kQuaternionResolution );
                outRotation._w      = reader.readQuantizedFloat( -1.0f, 1.0f, kQuaternionResolution );
                outRotation.normalize();
            }

            static FractureComponentBase* resolveFracture( GameObjectManager* pManager, const ComponentHandle& handle )
            {
                if ( pManager == nullptr )
                    return nullptr;
                Component* pComponent = pManager->resolveComponent( handle );
                return pComponent != nullptr ? castTo<FractureComponentBase>( pComponent ) : nullptr;
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

    void DestructionReplicationServer::initialize( NetHost* pHost, GameObjectManager* pManager, const DestructionReplicationSettings& settings )
    {
        _pHost    = pHost;
        _pManager = pManager;
        _settings = settings;
        _listEntry.clear();
        _listRequest.clear();
        _stats    = DestructionReplicationStats{};
        _hashTime = 0.0f;
    }

    void DestructionReplicationServer::registerObject( uint32 netId, FractureComponentBase& component )
    {
        component.setAuthority( true );
        Entry* pEntry = findEntry( netId );
        if ( pEntry == nullptr )
        {
            _listEntry.emplace_back();
            pEntry = &_listEntry.back();
        }
        *pEntry                 = Entry{};
        pEntry->_netId          = netId;
        pEntry->_component      = component.getHandle();
        pEntry->_sentEventCount = static_cast<uint32>( component.getEventLog()._listEvent.size() );
    }

    DestructionReplicationServer::Entry* DestructionReplicationServer::findEntry( uint32 netId )
    {
        for ( Entry& entry : _listEntry )
        {
            if ( entry._netId == netId )
                return &entry;
        }
        return nullptr;
    }

    FractureComponentBase* DestructionReplicationServer::resolve( const Entry& entry ) const
    {
        return DestructionReplicationInternal::resolveFracture( _pManager, entry._component );
    }

    void DestructionReplicationServer::onConnectionOpened( int32 connectionId )
    {
        for ( const Entry& entry : _listEntry )
            _listRequest.push_back( Request{ connectionId, entry._netId } );
    }

    void DestructionReplicationServer::onConnectionClosed( int32 connectionId, NetDisconnectReason reason )
    {
        (void)reason;
        for ( size_t index = _listRequest.size(); index > 0; --index )
        {
            if ( _listRequest[index - 1]._connectionId == connectionId )
                _listRequest.erase( _listRequest.begin() + static_cast<std::ptrdiff_t>( index - 1 ) );
        }
    }

    NetHandleResult DestructionReplicationServer::handleNetMessage( const NetMessageContext& context, BitReader& body )
    {
        const uint32 netId = static_cast<uint32>( body.readVarUint() );
        if ( body.hasOverflowed() || context._connectionId < 0 )
            return NetHandleResult::Malformed;
        if ( findEntry( netId ) != nullptr )
            _listRequest.push_back( Request{ context._connectionId, netId } );
        return NetHandleResult::Handled;
    }

    int32 DestructionReplicationServer::broadcast( NetChannelType channel )
    {
        const int32 connectedCount = _pHost->getConnectedCount();
        const int32 sentCount      = _writer.broadcast( *_pHost, channel );
        _stats._sentBytes += static_cast<uint64>( _writer.getByteCount() ) * static_cast<uint64>( MathUtil::max( 0, sentCount ) );
        // 신뢰 창이 찬 연결은 거절한다(`NetConnection::kReliableWindow`) — 그 클라이언트는 사건이 빠져 다음 해시 비교에서 스냅숏으로 바로잡힌다.
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
        // 스냅숏은 사건 뒤에 — 스냅숏의 사건 수 앞의 사건은 이미 같은 채널에서 앞서 갔다.
        vector<Request> listRequest;
        listRequest.swap( _listRequest );
        for ( const Request& request : listRequest )
        {
            Entry*                 pEntry     = findEntry( request._netId );
            FractureComponentBase* pComponent = pEntry != nullptr ? resolve( *pEntry ) : nullptr;
            if ( pComponent != nullptr )
                sendSnapshot( *pEntry, *pComponent, request._connectionId );
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
        // 서버는 스냅숏을 받지 않으므로 기록의 자리가 곧 상태의 사건 번호다.
        const vector<DestructionDamageEvent>& listEvent     = component.getEventLog()._listEvent;
        const vector<FractureGroupPose>&      listGroupPose = component.getEventGroupPoses();
        for ( uint32 index = entry._sentEventCount; index < static_cast<uint32>( listEvent.size() ); ++index )
        {
            BitWriter& writer = _writer.begin( NetDestructionMessage::kEvent );
            writer.writeVarUint( entry._netId );
            writer.writeVarUint( serverTick );
            writer.writeVarUint( index );
            DestructionReplicationInternal::writeEvent( writer, listEvent[index] );
            // 떨어진 덩어리를 맞힌 사건이면 맞기 직전 그 덩어리 자세(비트 그대로) — 받는 쪽이 그 자리에서 가른다(신뢰 채널이 밀려 자세가 늦어도).
            const bool bHasPose = index < listGroupPose.size() && listGroupPose[index]._groupId != 0;
            writer.writeBool( bHasPose );
            if ( bHasPose )
                DestructionReplicationInternal::writeExactPose( writer, listGroupPose[index]._position, listGroupPose[index]._rotation );
            (void)broadcast( NetChannelType::ReliableOrdered );
            ++_stats._eventMessageCount;
        }
        entry._sentEventCount = static_cast<uint32>( listEvent.size() );
    }

    void DestructionReplicationServer::sendSnapshot( Entry& entry, FractureComponentBase& component, int32 connectionId )
    {
        const uint32 eventCount = component.getState().getEventCount();
        if ( component.isStateReady() == false || eventCount == 0 )
            return; // 처음 상태 그대로 — 사건이 0 번부터 간다
        vector<uint8> bytes;
        component.makeNetworkSnapshot( bytes );
        const uint32 partBytes = static_cast<uint32>( MathUtil::max( 64, _settings._snapshotPartBytes ) );
        const uint32 partCount = static_cast<uint32>( ( bytes.size() + partBytes - 1 ) / partBytes );
        const uint32 serial    = ++entry._snapshotSerial;
        for ( uint32 part = 0; part < partCount; ++part )
        {
            const uint32 offset = part * partBytes;
            const uint32 length = MathUtil::min( partBytes, static_cast<uint32>( bytes.size() ) - offset );
            BitWriter&   writer = _writer.begin( NetDestructionMessage::kSnapshotPart );
            writer.writeVarUint( entry._netId );
            writer.writeVarUint( serial );
            writer.writeVarUint( eventCount );
            writer.writeVarUint( part );
            writer.writeVarUint( partCount );
            writer.writeVarUint( length );
            writer.writeBytes( bytes.data() + offset, static_cast<int32>( length ) );
            if ( _writer.send( *_pHost, connectionId, NetChannelType::ReliableOrdered ) )
                _stats._sentBytes += static_cast<uint64>( _writer.getByteCount() );
            ++_stats._snapshotPartCount;
        }
        ++_stats._snapshotCount;
    }

    void DestructionReplicationServer::sendPoses( Entry& entry, FractureComponentBase& component, uint32 serverTick )
    {
        using Internal = DestructionReplicationInternal;
        if ( component.isStateReady() == false || component.isFractured() == false )
            return;
        const float32 period = 1.0f / MathUtil::max( 0.1f, component.getProfile()._networkPoseRate );
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
            state._groupId = pose._groupId;
            for ( const ChunkSendState& previous : entry._listChunk )
            {
                if ( previous._groupId == pose._groupId )
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
                // 신뢰 채널은 손실이 크면 앞 메시지를 기다리느라 몇 초 밀린다 — 멈춘 자리를 비신뢰로도 몇 번 보내 그 사이 공중에 멈춰 있지 않게.
                if ( bDue && state._restRepeatLeft > 0 )
                {
                    listRestRepeat.push_back( &pose );
                    --state._restRepeatLeft;
                }
            }
            else
            {
                state._bRestSent = SW_FALSE;
                if ( bDue )
                    listMoving.push_back( &pose );
            }
            listKeep.push_back( state );
        }
        entry._listChunk.swap( listKeep );

        // 셋 — 움직이는 덩어리(비신뢰, 양자화), 멈춤 확정(신뢰, 비트 그대로), 멈춤 되풀이(비신뢰, 비트 그대로). 메시지 하나가 예산을 넘으면 나눈다.
        // 비신뢰를 쓰는 까닭: 받는 쪽이 틱으로 줄 세우므로 늦게 온 것도 보간에 쓰고, 순서만 채널은 같은 종류를 한 간격에 하나만 보내 오브젝트 · 조각
        // 메시지가 서로 지운다.
        for ( uint32 pass = 0; pass < 3; ++pass )
        {
            const bool                              bRest     = pass != 0;
            const bool                              bReliable = pass == 1;
            const vector<const FractureGroupPose*>& list      = pass == 0 ? listMoving : ( pass == 1 ? listRest : listRestRepeat );
            size_t                                  start     = 0;
            while ( start < list.size() )
            {
                BitWriter& writer = _writer.begin( NetDestructionMessage::kPose );
                writer.writeBool( bRest );
                writer.writeVarUint( entry._netId );
                writer.writeVarUint( serverTick );
                // 개수는 끝에서 알 수 있으므로 몇 개를 실을지 먼저 정한다(예산 안 · 최대 남은 것).
                const int32  perPoseBytes = bRest ? 30 : 15;
                const size_t room         = static_cast<size_t>( MathUtil::max( 1, ( Internal::kPoseMessageBudget - 16 ) / perPoseBytes ) );
                const size_t count        = MathUtil::min( room, list.size() - start );
                writer.writeVarUint( count );
                for ( size_t index = start; index < start + count; ++index )
                {
                    const FractureGroupPose& pose = *list[index];
                    writer.writeVarUint( pose._groupId );
                    // 질량 중심 + 회전 — 원점은 덩어리에서 멀 수 있어 돌면 크게 움직인다(보간이 그것을 직선으로 잇게 된다).
                    if ( bRest )
                        Internal::writeExactPose( writer, pose._center, pose._rotation );
                    else
                        Internal::writeQuantizedPose( writer, pose._center, pose._rotation, _settings );
                }
                (void)broadcast( bReliable ? NetChannelType::ReliableOrdered : NetChannelType::Unreliable );
                ++_stats._poseMessageCount;
                _stats._restPoseCount += bReliable ? count : 0u;
                start += count;
            }
        }
    }

    void DestructionReplicationServer::sendHash( const Entry& entry, const FractureComponentBase& component )
    {
        const uint32 eventCount = component.getState().getEventCount();
        // 쌓였지만 아직 적용하지 않은 피해가 있어도 된다 — (사건 수, 해시)는 그 순간의 상태 그대로고, 쌓인 것은 뒤 번호로 간다.
        if ( component.isStateReady() == false || eventCount == 0 )
            return;
        BitWriter& writer = _writer.begin( NetDestructionMessage::kHash );
        writer.writeVarUint( entry._netId );
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
        , _renderTick{ -1.0f }
        , _serverTickEstimate{ 0.0f }
        , _bHasServerTick{ SW_FALSE }
    {
    }

    void DestructionReplicationClient::initialize( NetHost* pHost, GameObjectManager* pManager, const DestructionReplicationSettings& settings )
    {
        _pHost    = pHost;
        _pManager = pManager;
        _settings = settings;
        _listEntry.clear();
        _stats              = DestructionReplicationStats{};
        _renderTick         = -1.0f;
        _serverTickEstimate = 0.0f;
        _bHasServerTick     = SW_FALSE;
    }

    void DestructionReplicationClient::registerObject( uint32 netId, FractureComponentBase& component )
    {
        component.setAuthority( false );
        Entry* pEntry = findEntry( netId );
        if ( pEntry == nullptr )
        {
            _listEntry.emplace_back();
            pEntry = &_listEntry.back();
        }
        *pEntry            = Entry{};
        pEntry->_netId     = netId;
        pEntry->_component = component.getHandle();
    }

    void DestructionReplicationClient::skipNextEvent( uint32 netId )
    {
        Entry* pEntry = findEntry( netId );
        if ( pEntry != nullptr )
            ++pEntry->_skipCount;
    }

    DestructionReplicationClient::Entry* DestructionReplicationClient::findEntry( uint32 netId )
    {
        for ( Entry& entry : _listEntry )
        {
            if ( entry._netId == netId )
                return &entry;
        }
        return nullptr;
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
        const uint32 netId = static_cast<uint32>( reader.readVarUint() );
        if ( reader.hasOverflowed() )
            return NetHandleResult::Malformed;
        Entry* pEntry = findEntry( netId );
        if ( pEntry == nullptr )
            return NetHandleResult::Handled; // 모르는 오브젝트(이 클라이언트에 없다) — 먹고 버린다
        if ( kind == NetDestructionMessage::kEvent )
        {
            const uint32 serverTick = static_cast<uint32>( reader.readVarUint() );
            const uint32 index      = static_cast<uint32>( reader.readVarUint() );
            observeServerTick( serverTick );
            BufferedEvent buffered;
            buffered._index = index;
            if ( DestructionReplicationInternal::readEvent( reader, buffered._event ) )
            {
                buffered._bHasPose = reader.readBool() ? SW_TRUE : SW_FALSE;
                if ( buffered._bHasPose == SW_TRUE )
                {
                    buffered._groupPose._groupId = buffered._event._groupId;
                    DestructionReplicationInternal::readExactPose( reader, buffered._groupPose._position, buffered._groupPose._rotation );
                }
                if ( reader.hasOverflowed() == false )
                    handleEvent( *pEntry, buffered );
            }
        }
        else if ( kind == NetDestructionMessage::kSnapshotPart )
        {
            handleSnapshotPart( *pEntry, reader );
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
            ++_stats._staleEventCount; // 스냅숏이 이미 담았다
            return;
        }
        if ( index > entry._nextEventIndex )
        {
            // 앞 번호를 기다린다(늦게 들어와 스냅숏이 아직 안 왔다).
            for ( const BufferedEvent& buffered : entry._listFutureEvent )
            {
                if ( buffered._index == index )
                    return;
            }
            entry._listFutureEvent.push_back( received );
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
        // 기다리던 다음 번호들을 잇는다.
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

    void DestructionReplicationClient::applyToComponent( FractureComponentBase& component, const BufferedEvent& buffered )
    {
        if ( buffered._bHasPose == SW_TRUE )
            component.applyDamage( buffered._event, buffered._groupPose );
        else
            component.applyDamage( buffered._event );
    }

    void DestructionReplicationClient::handleSnapshotPart( Entry& entry, BitReader& reader )
    {
        const uint32 serial     = static_cast<uint32>( reader.readVarUint() );
        const uint32 eventCount = static_cast<uint32>( reader.readVarUint() );
        const uint32 part       = static_cast<uint32>( reader.readVarUint() );
        const uint32 partCount  = static_cast<uint32>( reader.readVarUint() );
        const uint32 length     = static_cast<uint32>( reader.readVarUint() );
        if ( reader.hasOverflowed() || partCount == 0 || part >= partCount || length > static_cast<uint32>( reader.getBitsRemaining() / 8 ) )
            return;
        if ( part == 0 )
        {
            entry._snapshotBytes.clear();
            entry._snapshotSerial     = serial;
            entry._snapshotEventCount = eventCount;
            entry._snapshotPartCount  = partCount;
            entry._snapshotNextPart   = 0;
        }
        if ( serial != entry._snapshotSerial || part != entry._snapshotNextPart )
            return; // 신뢰 순서 채널이라 빠지지 않는다 — 다른 스냅숏의 조각이면 버린다
        const size_t offset = entry._snapshotBytes.size();
        entry._snapshotBytes.resize( offset + length );
        if ( reader.readBytes( entry._snapshotBytes.data() + offset, static_cast<int32>( length ) ) == false )
            return;
        ++entry._snapshotNextPart;
        if ( entry._snapshotNextPart < entry._snapshotPartCount )
            return;

        FractureComponentBase* pComponent = resolve( entry );
        if ( pComponent != nullptr )
            pComponent->applyNetworkSnapshot( entry._snapshotBytes.data(), entry._snapshotBytes.size() );
        ++_stats._snapshotAppliedCount;
        entry._snapshotBytes.clear();
        entry._bAwaitingSnapshot = SW_FALSE;
        entry._nextEventIndex    = entry._snapshotEventCount;
        entry._skipCount         = 0;
        entry._listHashCheck.clear();
        // 스냅숏보다 앞 번호는 버리고, 이어지는 것은 적용한다.
        vector<BufferedEvent> listFuture;
        listFuture.swap( entry._listFutureEvent );
        std::sort( listFuture.begin(), listFuture.end(), []( const BufferedEvent& lhs, const BufferedEvent& rhs )
        { return lhs._index < rhs._index; } );
        for ( const BufferedEvent& buffered : listFuture )
        {
            if ( buffered._index >= entry._nextEventIndex )
                handleEvent( entry, buffered );
        }
    }

    void DestructionReplicationClient::handlePose( BitReader& reader, bool bRest )
    {
        using Internal     = DestructionReplicationInternal;
        const uint32 netId = static_cast<uint32>( reader.readVarUint() );
        const uint32 tick  = static_cast<uint32>( reader.readVarUint() );
        const uint64 count = reader.readVarUint();
        if ( reader.hasOverflowed() )
            return;
        Entry* pEntry = findEntry( netId );
        observeServerTick( tick );
        for ( uint64 index = 0; index < count && reader.hasOverflowed() == false; ++index )
        {
            PoseSample sample;
            sample._tick         = tick;
            sample._bRest        = bRest ? SW_TRUE : SW_FALSE;
            const uint32 groupId = static_cast<uint32>( reader.readVarUint() );
            if ( bRest )
                Internal::readExactPose( reader, sample._position, sample._rotation );
            else
                Internal::readQuantizedPose( reader, sample._position, sample._rotation, _settings );
            if ( pEntry == nullptr || reader.hasOverflowed() )
                continue;
            ChunkTrack* pTrack = nullptr;
            for ( ChunkTrack& track : pEntry->_listChunk )
            {
                if ( track._groupId == groupId )
                    pTrack = &track;
            }
            if ( pTrack == nullptr )
            {
                pEntry->_listChunk.emplace_back();
                pTrack           = &pEntry->_listChunk.back();
                pTrack->_groupId = groupId;
            }
            // 틱 순으로 끼운다(멈춤 확정은 다른 채널이라 앞질러 올 수 있다). 같은 틱이면 멈춤 쪽이 이긴다.
            auto iter = pTrack->_listSample.begin();
            while ( iter != pTrack->_listSample.end() && iter->_tick < tick )
                ++iter;
            if ( iter != pTrack->_listSample.end() && iter->_tick == tick )
            {
                if ( bRest )
                    *iter = sample;
            }
            else
            {
                pTrack->_listSample.insert( iter, sample );
            }
            while ( pTrack->_listSample.size() > Internal::kMaxPoseSample )
                pTrack->_listSample.pop_front();
        }
    }

    void DestructionReplicationClient::observeServerTick( uint32 serverTick )
    {
        const float32 tick = static_cast<float32>( serverTick );
        if ( _bHasServerTick == SW_FALSE || tick > _serverTickEstimate )
            _serverTickEstimate = tick;
        _bHasServerTick = SW_TRUE;
    }

    void DestructionReplicationClient::update( float32 deltaTime )
    {
        if ( _bHasServerTick == SW_TRUE )
        {
            // 서버 틱 추정은 틱마다 흐르고 받은 틱보다 뒤처지지 않는다 — 자세가 오지 않는 동안(모두 멈춤)에도 렌더 틱이 서버 시각을 따른다.
            const float32 tickInterval = MathUtil::max( 1.0e-4f, _settings._tickInterval );
            _serverTickEstimate += deltaTime / tickInterval;
            _renderTick = MathUtil::max( _renderTick, _serverTickEstimate - _settings._interpolationDelay / tickInterval );
        }
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
                ++index; // 그 번호까지의 사건이 아직 오는 중
                continue;
            }
            // 그 사건 수에 이르렀을 때의 해시(사건마다 적어 둔 것)와 비교한다. 다 넘겨받았는데(쌓인 것도 적용했는데) 상태가 그 수에 이르지 못했으면 사건이 빠졌다.
            uint64     localHash = 0;
            const bool bFound    = component.findRecentStateHash( check._eventCount, localHash );
            if ( bFound == false && bPending )
            {
                ++index; // 넘겨받은 사건이 다음 물리 프레임에 적용된다
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
                continue; // 너무 오래된 비교 — 다음 해시로 본다
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
        writer.writeVarUint( entry._netId );
        if ( _writer.send( *_pHost, 0, NetChannelType::ReliableOrdered ) )
            _stats._sentBytes += static_cast<uint64>( _writer.getByteCount() );
    }

    void DestructionReplicationClient::driveChunks( Entry& entry, FractureComponentBase& component )
    {
        if ( _renderTick < 0.0f )
            return;
        // 그룹 번호는 늘기만 한다 — 지금 가장 큰 번호보다 작은데 없는 그룹은 갈라져 사라진 것, 큰 것은 그 사건이 아직 오지 않은 것(자세를 먼저 받았다).
        uint32 maxGroupId = 0;
        for ( const DestructionGroup& group : component.getState().getGroups() )
            maxGroupId = MathUtil::max( maxGroupId, group._id );
        for ( size_t trackIndex = 0; trackIndex < entry._listChunk.size(); )
        {
            ChunkTrack& track     = entry._listChunk[trackIndex];
            const bool  bHasGroup = component.getState().findGroup( track._groupId ) != nullptr;
            const bool  bRemoved  = bHasGroup == false && track._groupId < maxGroupId && component.hasPendingDamage() == false;
            if ( bRemoved )
            {
                entry._listChunk.erase( entry._listChunk.begin() + static_cast<std::ptrdiff_t>( trackIndex ) );
                continue;
            }
            ++trackIndex;
            // 첫 자세보다 앞을 그리는 동안은 몰지 않는다 — 그 덩어리는 같은 사건 · 같은 씨앗으로 서버와 같은 속도로 태어나 혼자 날아간다.
            if ( bHasGroup == false || track._listSample.empty() || static_cast<float32>( track._listSample.front()._tick ) > _renderTick )
                continue;
            // 렌더 틱을 사이에 둔 두 자세 — 앞이 없으면 첫 것, 뒤가 없으면 마지막 것(앞으로 내다보지 않는다).
            const PoseSample* pFrom = &track._listSample.front();
            const PoseSample* pTo   = pFrom;
            for ( const PoseSample& sample : track._listSample )
            {
                if ( static_cast<float32>( sample._tick ) <= _renderTick )
                    pFrom = &sample;
                pTo = &sample;
                if ( static_cast<float32>( sample._tick ) > _renderTick )
                    break;
            }
            if ( static_cast<float32>( pFrom->_tick ) > _renderTick )
                pTo = pFrom;
            float3     position = pFrom->_position;
            quaternion rotation = pFrom->_rotation;
            if ( pTo != pFrom && pTo->_tick > pFrom->_tick )
            {
                const float32 alpha = MathUtil::clamp( ( _renderTick - static_cast<float32>( pFrom->_tick ) ) / static_cast<float32>( pTo->_tick - pFrom->_tick ), 0.0f, 1.0f );
                position            = pFrom->_position + ( pTo->_position - pFrom->_position ) * alpha;
                rotation            = quaternion::lerp( pFrom->_rotation, pTo->_rotation, alpha );
            }
            component.driveGroup( track._groupId, position, rotation );
            // 지난 것은 하나만 남긴다(보간의 앞).
            while ( track._listSample.size() > 2 && static_cast<float32>( track._listSample[1]._tick ) <= _renderTick )
                track._listSample.pop_front();
        }
    }
} // namespace sw
