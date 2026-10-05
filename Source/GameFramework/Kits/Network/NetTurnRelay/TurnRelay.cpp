#include "pch.h"

#include "GameFramework/Kits/Network/NetTurnRelay/TurnRelay.h"

#include "Core/Math/MathUtil.h"
#include "Core/Network/BitStream.h"
#include "Core/Network/Connection/NetHost.h"
#include "Core/Network/NetTypes.h"
#include "Core/Uuid/Uuid.h"

namespace sw
{
    SW_LOG_CALLER( "TurnRelay" );

    namespace
    {
        struct TurnRelayInternal
        {
            /** @brief splitmix64 의 마무리 섞기입니다. */
            static uint64 mix64( uint64 value )
            {
                value = ( value ^ ( value >> 30 ) ) * 0xBF58476D1CE4E5B9ull;
                value = ( value ^ ( value >> 27 ) ) * 0x94D049BB133111EBull;
                return value ^ ( value >> 31 );
            }

            /** @brief 운영체제 난수 64 비트(`Uuid::generate`) — 실행마다 · 서버마다 다르고 미리 알 수 없다. */
            static uint64 makeRandomSeed()
            {
                const Uuid uuid  = Uuid::generate();
                uint64     value = 0;
                for ( const uint8 byte : uuid._arrBytes )
                    value = mix64( value ^ byte );
                return value != 0 ? value : 0x9E3779B97F4A7C15ull;
            }

            static void writeBlob( BitWriter& writer, const vector<uint8>& buffer )
            {
                writer.writeVarUint( buffer.size() );
                if ( buffer.empty() == false )
                    writer.writeBytes( buffer.data(), static_cast<int32>( buffer.size() ) );
            }

            [[nodiscard]] static bool readBlob( BitReader& reader, vector<uint8>& outByte )
            {
                const uint64 size = reader.readVarUint();
                if ( reader.hasOverflowed() || size > 900 )
                    return false;
                outByte.resize( static_cast<size_t>( size ) );
                return size == 0 || reader.readBytes( outByte.data(), static_cast<int32>( size ) );
            }
        };
    } // namespace
} // namespace sw

namespace sw
{
    // ------------------------------------------------------------------------------
    // TurnRelayServer
    // ------------------------------------------------------------------------------
    TurnRelayServer::TurnRelayServer()
        : _listRoom{}
        , _listPending{}
        , _listBlockedScratch{}
        , _eventBuffer{}
        , _defaultPolicy{}
        , _pHost{ nullptr }
        , _pPolicy{ nullptr }
        , _tokenSecret{ 0 }
        , _tokenCount{ 0 }
        , _seatCount{ 2 }
        , _messageWriter{}
    {
    }

    void TurnRelayServer::initialize( NetHost* pHost, int32 seatCount, const ITurnPolicy* pPolicy, uint64 tokenSeed )
    {
        _pHost       = pHost;
        _seatCount   = MathUtil::max( 1, seatCount );
        _pPolicy     = pPolicy != nullptr ? pPolicy : &_defaultPolicy;
        _tokenSecret = tokenSeed != 0 ? tokenSeed : TurnRelayInternal::makeRandomSeed();
        _tokenCount  = 0;
        _listRoom.clear();
        _listPending.clear();
        _eventBuffer.clear();
    }

    uint32 TurnRelayServer::nextToken()
    {
        // 비밀 키에 센 값을 섞는다 — 받은 표에서 다음 표를 셈할 수 없다(암호학적 MAC 은 아니다).
        ++_tokenCount;
        const uint32 token = static_cast<uint32>( TurnRelayInternal::mix64( _tokenSecret + _tokenCount * 0x9E3779B97F4A7C15ull ) );
        return token != 0 ? token : 1u;
    }

    TurnRoom* TurnRelayServer::findRoomMutable( uint32 roomId )
    {
        for ( TurnRoom& room : _listRoom )
        {
            if ( room._roomId == roomId )
                return &room;
        }
        return nullptr;
    }

    const TurnRoom* TurnRelayServer::findRoom( uint32 roomId ) const { return const_cast<TurnRelayServer*>( this )->findRoomMutable( roomId ); }

    TurnRoom* TurnRelayServer::findRoomOfConnection( int32 connectionId )
    {
        for ( TurnRoom& room : _listRoom )
        {
            for ( const TurnSeat& seat : room._listSeat )
            {
                if ( seat._connectionId == connectionId )
                    return &room;
            }
        }
        return nullptr;
    }

    int32 TurnRelayServer::countPending( int32 connectionId ) const
    {
        int32 count = 0;
        for ( const PendingMessage& pending : _listPending )
            count += pending._connectionId == connectionId ? 1 : 0;
        return count;
    }

    void TurnRelayServer::sendOrQueue( int32 connectionId )
    {
        const int32 pendingCount = countPending( connectionId );
        if ( pendingCount == 0 && _messageWriter.send( *_pHost, connectionId, NetChannelType::ReliableOrdered ) )
            return;
        if ( _pHost->getConnectionState( connectionId ) != NetConnectionState::Connected )
            return; // 닫힌 연결 — 돌아오면 표로 다시 받는다
        if ( pendingCount >= kMaxPendingPerConnection )
        {
            SW_LOG_WARNING( "connection %# does not acknowledge - %# messages are waiting, disconnecting it", connectionId, pendingCount );
            _pHost->disconnect( connectionId );
            return;
        }
        _listPending.push_back( PendingMessage{ _messageWriter.getBytes(), connectionId } );
    }

    void TurnRelayServer::flushPending()
    {
        // 연결마다 순서를 지킨다 — 한 번 막힌 연결의 뒤 알림은 이번에 보내지 않는다.
        _listBlockedScratch.clear();
        size_t keepCount = 0;
        for ( size_t index = 0; index < _listPending.size(); ++index )
        {
            PendingMessage& pending  = _listPending[index];
            bool            bBlocked = false;
            for ( const int32 blocked : _listBlockedScratch )
                bBlocked = bBlocked || blocked == pending._connectionId;
            if ( bBlocked == false && _pHost->sendMessage( pending._connectionId, NetChannelType::ReliableOrdered, pending._buffer ) )
                continue;
            if ( bBlocked == false )
                _listBlockedScratch.push_back( pending._connectionId );
            if ( keepCount != index )
                _listPending[keepCount] = std::move( pending );
            ++keepCount;
        }
        _listPending.resize( keepCount );
    }

    void TurnRelayServer::flushSeat( const TurnRoom& room, TurnSeat& seat )
    {
        if ( seat._connectionId < 0 )
            return;
        while ( seat._sentActionCount < static_cast<int32>( room._listAction.size() ) )
        {
            const int32       index  = seat._sentActionCount;
            const TurnAction& action = room._listAction[static_cast<size_t>( index )];
            BitWriter&        writer = _messageWriter.begin( NetTurnRelayMessage::kApplied );
            writer.writeVarUint( room._roomId );
            writer.writeVarUint( static_cast<uint64>( index ) );
            writer.writeVarUint( static_cast<uint64>( action._seat ) );
            TurnRelayInternal::writeBlob( writer, action._buffer );
            if ( _messageWriter.send( *_pHost, seat._connectionId, NetChannelType::ReliableOrdered ) == false )
                return; // 창이 찼다 — `update` 가 이어 보낸다
            ++seat._sentActionCount;
        }
    }

    void TurnRelayServer::update()
    {
        if ( _pHost == nullptr )
            return;
        flushPending();
        for ( TurnRoom& room : _listRoom )
        {
            for ( TurnSeat& seat : room._listSeat )
                flushSeat( room, seat );
        }
    }

    void TurnRelayServer::sendJoined( int32 connectionId, uint32 roomId, int32 seat, uint32 token )
    {
        BitWriter& writer = _messageWriter.begin( NetTurnRelayMessage::kJoined );
        writer.writeVarUint( roomId );
        writer.writeVarUint( static_cast<uint64>( seat ) );
        writer.writeUint32( token );
        sendOrQueue( connectionId );
    }

    void TurnRelayServer::sendDenied( int32 connectionId, uint32 roomId, TurnRejectReason reason )
    {
        BitWriter& writer = _messageWriter.begin( NetTurnRelayMessage::kDenied );
        writer.writeVarUint( roomId );
        writer.writeBits( static_cast<uint32>( reason ), 8 );
        sendOrQueue( connectionId );
    }

    bool TurnRelayServer::handleJoin( int32 connectionId, BitReader& reader )
    {
        const uint32 roomId     = static_cast<uint32>( reader.readVarUint() );
        const int64  wantedSeat = reader.readVarInt();
        const uint32 token      = reader.readUint32();
        const uint64 knownCount = reader.readVarUint(); // 이미 가진 행동 수
        if ( reader.hasOverflowed() )
            return false;
        // 한 연결은 자리 하나 — 같은 방이면 가진 자리를 다시 알리고, 다른 방이면 거절한다.
        const TurnRoom* pHeldRoom = findRoomOfConnection( connectionId );
        if ( pHeldRoom != nullptr )
        {
            if ( pHeldRoom->_roomId != roomId )
            {
                sendDenied( connectionId, roomId, TurnRejectReason::AlreadySeated );
                return true;
            }
            for ( size_t index = 0; index < pHeldRoom->_listSeat.size(); ++index )
            {
                if ( pHeldRoom->_listSeat[index]._connectionId == connectionId )
                    sendJoined( connectionId, roomId, static_cast<int32>( index ), pHeldRoom->_listSeat[index]._token );
            }
            return true;
        }
        TurnRoom* pRoom = findRoomMutable( roomId );
        if ( pRoom == nullptr )
        {
            TurnRoom room;
            room._roomId = roomId;
            room._listSeat.resize( static_cast<size_t>( _seatCount ) );
            _listRoom.push_back( room );
            pRoom = &_listRoom.back();
        }
        // 표가 맞는 자리면 돌아온 것 — 그 자리 연결이 아직 살아 있으면 빼앗지 않는다. 아니면 빈 자리.
        int32 seatIndex = -1;
        for ( size_t index = 0; index < pRoom->_listSeat.size() && token != 0; ++index )
        {
            if ( pRoom->_listSeat[index]._bTaken && pRoom->_listSeat[index]._token == token )
                seatIndex = static_cast<int32>( index );
        }
        const bool bReturning = seatIndex >= 0;
        if ( bReturning && pRoom->_listSeat[static_cast<size_t>( seatIndex )]._connectionId >= 0 )
        {
            sendDenied( connectionId, roomId, TurnRejectReason::SeatInUse );
            return true;
        }
        if ( seatIndex < 0 )
        {
            const bool bWantedFree = 0 <= wantedSeat && wantedSeat < static_cast<int64>( pRoom->_listSeat.size() ) &&
                                     pRoom->_listSeat[static_cast<size_t>( wantedSeat )]._bTaken == SW_FALSE;
            if ( bWantedFree )
                seatIndex = static_cast<int32>( wantedSeat );
            for ( size_t index = 0; index < pRoom->_listSeat.size() && seatIndex < 0; ++index )
            {
                if ( pRoom->_listSeat[index]._bTaken == SW_FALSE )
                    seatIndex = static_cast<int32>( index );
            }
        }
        if ( seatIndex < 0 )
        {
            sendDenied( connectionId, roomId, TurnRejectReason::RoomFull );
            return true;
        }
        TurnSeat& seat        = pRoom->_listSeat[static_cast<size_t>( seatIndex )];
        seat._bTaken          = SW_TRUE;
        seat._connectionId    = connectionId;
        seat._sentActionCount = static_cast<int32>( MathUtil::min<uint64>( knownCount, pRoom->_listAction.size() ) );
        if ( bReturning == false )
            seat._token = nextToken();
        sendJoined( connectionId, roomId, seatIndex, seat._token );

        TurnRelayEvent event;
        event._kind   = bReturning ? TurnRelayEvent::Kind::SeatReturned : TurnRelayEvent::Kind::Joined;
        event._roomId = roomId;
        event._seat   = seatIndex;
        _eventBuffer.push( event );

        // 시작했으면 들어온 사람에게 시작 알림과 놓친 행동을, 이제 다 찼으면 모두에게 시작을.
        bool bFull = true;
        for ( const TurnSeat& other : pRoom->_listSeat )
            bFull = bFull && other._bTaken;
        if ( pRoom->_bStarted == SW_FALSE && bFull == false )
            return true;
        const bool bStartsNow = pRoom->_bStarted == SW_FALSE;
        pRoom->_bStarted      = SW_TRUE;
        for ( const TurnSeat& other : pRoom->_listSeat )
        {
            const bool bTold = other._connectionId >= 0 && ( bStartsNow || other._connectionId == connectionId );
            if ( bTold == false )
                continue;
            BitWriter& writer = _messageWriter.begin( NetTurnRelayMessage::kStarted );
            writer.writeVarUint( roomId );
            writer.writeVarUint( static_cast<uint64>( pRoom->_currentSeat ) );
            sendOrQueue( other._connectionId );
        }
        flushSeat( *pRoom, seat );
        if ( bStartsNow )
        {
            TurnRelayEvent startEvent;
            startEvent._kind   = TurnRelayEvent::Kind::Started;
            startEvent._roomId = roomId;
            startEvent._seat   = pRoom->_currentSeat;
            _eventBuffer.push( startEvent );
        }
        return true;
    }

    bool TurnRelayServer::handleAction( int32 connectionId, BitReader& reader )
    {
        const uint32  roomId   = static_cast<uint32>( reader.readVarUint() );
        const int32   submitId = static_cast<int32>( reader.readVarUint() );
        vector<uint8> actionBuffer;
        if ( TurnRelayInternal::readBlob( reader, actionBuffer ) == false )
            return false;
        TurnRoom*        pRoom  = findRoomMutable( roomId );
        int32            seat   = -1;
        TurnRejectReason reason = TurnRejectReason::UnknownRoom;
        if ( pRoom != nullptr )
        {
            for ( size_t index = 0; index < pRoom->_listSeat.size(); ++index )
            {
                if ( pRoom->_listSeat[index]._connectionId == connectionId )
                    seat = static_cast<int32>( index );
            }
            if ( seat < 0 )
                reason = TurnRejectReason::UnknownRoom;
            else if ( pRoom->_bStarted == SW_FALSE )
                reason = TurnRejectReason::NotStarted;
            else if ( _pPolicy->isActionAllowed( *pRoom, seat, actionBuffer ) == false )
                reason = seat != pRoom->_currentSeat ? TurnRejectReason::NotYourTurn : TurnRejectReason::InvalidAction;
            else
            {
                pRoom->_listAction.push_back( TurnAction{ actionBuffer, seat } );
                _pPolicy->applyAction( *pRoom, seat, actionBuffer );
                for ( TurnSeat& other : pRoom->_listSeat )
                    flushSeat( *pRoom, other );
                TurnRelayEvent event;
                event._kind   = TurnRelayEvent::Kind::ActionApplied;
                event._roomId = roomId;
                event._seat   = seat;
                event._index  = static_cast<int32>( pRoom->_listAction.size() ) - 1;
                event._buffer = std::move( actionBuffer );
                _eventBuffer.push( event );
                return true;
            }
        }
        BitWriter& writer = _messageWriter.begin( NetTurnRelayMessage::kRejected );
        writer.writeVarUint( roomId );
        writer.writeVarUint( static_cast<uint64>( submitId ) );
        writer.writeBits( static_cast<uint32>( reason ), 8 );
        sendOrQueue( connectionId );
        return true;
    }

    NetHandleResult TurnRelayServer::handleNetMessage( const NetMessageContext& context, BitReader& body )
    {
        if ( _pHost == nullptr )
            return NetHandleResult::Handled;
        const bool bWellFormed = context._kind == NetTurnRelayMessage::kJoin ? handleJoin( context._connectionId, body )
                                                                             : handleAction( context._connectionId, body );
        return bWellFormed ? NetHandleResult::Handled : NetHandleResult::Malformed;
    }

    void TurnRelayServer::onConnectionClosed( int32 connectionId, NetDisconnectReason reason )
    {
        (void)reason;
        // 연결 id 는 다음 연결이 다시 쓴다 — 그 연결에 줄 선 알림도 버린다.
        size_t keepCount = 0;
        for ( size_t index = 0; index < _listPending.size(); ++index )
        {
            if ( _listPending[index]._connectionId == connectionId )
                continue;
            if ( keepCount != index )
                _listPending[keepCount] = std::move( _listPending[index] );
            ++keepCount;
        }
        _listPending.resize( keepCount );
        for ( TurnRoom& room : _listRoom )
        {
            for ( size_t index = 0; index < room._listSeat.size(); ++index )
            {
                TurnSeat& seat = room._listSeat[index];
                if ( seat._connectionId != connectionId )
                    continue;
                seat._connectionId = -1; // 자리 · 표는 남긴다
                TurnRelayEvent event;
                event._kind   = TurnRelayEvent::Kind::SeatLeft;
                event._roomId = room._roomId;
                event._seat   = static_cast<int32>( index );
                _eventBuffer.push( event );
            }
        }
    }

    void TurnRelayServer::drainEvents( vector<TurnRelayEvent>& outListEvent )
    {
        _eventBuffer.drainTo( outListEvent );
    }

    // ------------------------------------------------------------------------------
    // TurnRelayClient
    // ------------------------------------------------------------------------------
    TurnRelayClient::TurnRelayClient()
        : _listAction{}
        , _eventBuffer{}
        , _pHost{ nullptr }
        , _roomId{ 0 }
        , _token{ 0 }
        , _seat{ -1 }
        , _nextSubmitId{ 0 }
        , _bStarted{ SW_FALSE }
        , _messageWriter{}
    {
    }

    void TurnRelayClient::initialize( NetHost* pHost ) { _pHost = pHost; }

    void TurnRelayClient::join( uint32 roomId, int32 seat )
    {
        if ( _roomId != roomId )
        {
            _listAction.clear();
            _token = 0;
        }
        _roomId           = roomId;
        BitWriter& writer = _messageWriter.begin( NetTurnRelayMessage::kJoin );
        writer.writeVarUint( roomId );
        writer.writeVarInt( seat );
        writer.writeUint32( _token );
        writer.writeVarUint( _listAction.size() );
        (void)_pHost->sendMessage( 0, NetChannelType::ReliableOrdered, writer.getBytes() );
    }

    int32 TurnRelayClient::submitAction( const vector<uint8>& buffer )
    {
        const int32 submitId = _nextSubmitId++;
        BitWriter&  writer   = _messageWriter.begin( NetTurnRelayMessage::kAction );
        writer.writeVarUint( _roomId );
        writer.writeVarUint( static_cast<uint64>( submitId ) );
        TurnRelayInternal::writeBlob( writer, buffer );
        (void)_pHost->sendMessage( 0, NetChannelType::ReliableOrdered, writer.getBytes() );
        return submitId;
    }

    NetHandleResult TurnRelayClient::handleNetMessage( const NetMessageContext& context, BitReader& body )
    {
        BitReader&     reader = body;
        TurnRelayEvent event;
        event._roomId = static_cast<uint32>( reader.readVarUint() );
        if ( reader.hasOverflowed() )
            return NetHandleResult::Malformed;
        if ( event._roomId != _roomId )
            return NetHandleResult::Handled;
        switch ( context._kind )
        {
            case NetTurnRelayMessage::kJoined:
            {
                _seat       = static_cast<int32>( reader.readVarUint() );
                _token      = reader.readUint32();
                event._kind = TurnRelayEvent::Kind::Joined;
                event._seat = _seat;
                break;
            }
            case NetTurnRelayMessage::kStarted:
            {
                _bStarted   = SW_TRUE;
                event._kind = TurnRelayEvent::Kind::Started;
                event._seat = static_cast<int32>( reader.readVarUint() );
                break;
            }
            case NetTurnRelayMessage::kApplied:
            {
                event._kind  = TurnRelayEvent::Kind::ActionApplied;
                event._index = static_cast<int32>( reader.readVarUint() );
                event._seat  = static_cast<int32>( reader.readVarUint() );
                if ( TurnRelayInternal::readBlob( reader, event._buffer ) == false )
                    return NetHandleResult::Malformed;
                if ( event._index != static_cast<int32>( _listAction.size() ) )
                    return NetHandleResult::Handled; // 이미 가진 것(다시 들어올 때 겹친 것)
                _listAction.push_back( TurnAction{ event._buffer, event._seat } );
                break;
            }
            case NetTurnRelayMessage::kRejected:
            {
                event._kind   = TurnRelayEvent::Kind::ActionRejected;
                event._index  = static_cast<int32>( reader.readVarUint() );
                event._reason = static_cast<TurnRejectReason>( reader.readBits( 8 ) );
                break;
            }
            case NetTurnRelayMessage::kDenied:
            {
                event._kind   = TurnRelayEvent::Kind::Denied;
                event._reason = static_cast<TurnRejectReason>( reader.readBits( 8 ) );
                break;
            }
            default:
            {
                return NetHandleResult::Handled;
            }
        }
        if ( reader.hasOverflowed() )
            return NetHandleResult::Malformed;
        _eventBuffer.push( event );
        return NetHandleResult::Handled;
    }

    void TurnRelayClient::drainEvents( vector<TurnRelayEvent>& outListEvent )
    {
        _eventBuffer.drainTo( outListEvent );
    }
} // namespace sw
