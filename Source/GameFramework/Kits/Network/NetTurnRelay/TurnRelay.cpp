#include "pch.h"

#include "GameFramework/Kits/Network/NetTurnRelay/TurnRelay.h"

#include "Core/Math/MathUtil.h"
#include "Core/Network/BitStream.h"
#include "Core/Network/NetHost.h"
#include "Core/Network/NetTypes.h"

namespace sw
{
    namespace
    {
        struct TurnRelayInternal
        {
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
        , _listEvent{}
        , _defaultPolicy{}
        , _pHost{ nullptr }
        , _pPolicy{ nullptr }
        , _seatCount{ 2 }
        , _tokenState{ 1 }
        , _messageWriter{}
    {
    }

    void TurnRelayServer::initialize( NetHost* pHost, int32 seatCount, const ITurnPolicy* pPolicy, uint32 tokenSeed )
    {
        _pHost      = pHost;
        _seatCount  = MathUtil::max( 1, seatCount );
        _pPolicy    = pPolicy != nullptr ? pPolicy : &_defaultPolicy;
        _tokenState = tokenSeed != 0 ? tokenSeed : 1u;
        _listRoom.clear();
        _listEvent.clear();
    }

    uint32 TurnRelayServer::nextToken()
    {
        _tokenState ^= _tokenState << 13;
        _tokenState ^= _tokenState >> 17;
        _tokenState ^= _tokenState << 5;
        return _tokenState != 0 ? _tokenState : 1u;
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

    void TurnRelayServer::broadcastRoom( const TurnRoom& room, const vector<uint8>& buffer )
    {
        for ( const TurnSeat& seat : room._listSeat )
        {
            if ( seat._connectionId >= 0 )
                (void)_pHost->sendMessage( seat._connectionId, NetChannelType::ReliableOrdered, buffer );
        }
    }

    void TurnRelayServer::sendApplied( const TurnRoom& room, int32 index, int32 connectionId )
    {
        const TurnAction& action = room._listAction[static_cast<size_t>( index )];
        BitWriter&        writer = _messageWriter.begin( NetTurnRelayMessage::kApplied );
        writer.writeVarUint( room._roomId );
        writer.writeVarUint( static_cast<uint64>( index ) );
        writer.writeVarUint( static_cast<uint64>( action._seat ) );
        TurnRelayInternal::writeBlob( writer, action._buffer );
        if ( connectionId >= 0 )
            (void)_pHost->sendMessage( connectionId, NetChannelType::ReliableOrdered, writer.getBytes() );
        else
            broadcastRoom( room, writer.getBytes() );
    }

    void TurnRelayServer::handleJoin( int32 connectionId, const uint8* pData, int32 size )
    {
        BitReader    reader( pData + 1, size - 1 );
        const uint32 roomId     = static_cast<uint32>( reader.readVarUint() );
        const int64  wantedSeat = reader.readVarInt();
        const uint32 token      = reader.readUint32();
        const uint32 knownCount = static_cast<uint32>( reader.readVarUint() ); // 이미 가진 행동 수
        if ( reader.hasOverflowed() )
            return;
        TurnRoom* pRoom = findRoomMutable( roomId );
        if ( pRoom == nullptr )
        {
            TurnRoom room;
            room._roomId = roomId;
            room._listSeat.resize( static_cast<size_t>( _seatCount ) );
            _listRoom.push_back( room );
            pRoom = &_listRoom.back();
        }
        // 표가 맞는 자리면 돌아온 것, 아니면 빈 자리.
        int32 seatIndex = -1;
        for ( size_t index = 0; index < pRoom->_listSeat.size() && token != 0; ++index )
        {
            if ( pRoom->_listSeat[index]._bTaken && pRoom->_listSeat[index]._token == token )
                seatIndex = static_cast<int32>( index );
        }
        const bool bReturning = seatIndex >= 0;
        if ( seatIndex < 0 )
        {
            if ( wantedSeat >= 0 && wantedSeat < static_cast<int64>( pRoom->_listSeat.size() ) && pRoom->_listSeat[static_cast<size_t>( wantedSeat )]._bTaken == SW_FALSE )
                seatIndex = static_cast<int32>( wantedSeat );
            for ( size_t index = 0; index < pRoom->_listSeat.size() && seatIndex < 0; ++index )
            {
                if ( pRoom->_listSeat[index]._bTaken == SW_FALSE )
                    seatIndex = static_cast<int32>( index );
            }
        }
        if ( seatIndex < 0 )
        {
            BitWriter& writer = _messageWriter.begin( NetTurnRelayMessage::kDenied );
            writer.writeVarUint( roomId );
            writer.writeBits( static_cast<uint32>( TurnRejectReason::RoomFull ), 8 );
            (void)_pHost->sendMessage( connectionId, NetChannelType::ReliableOrdered, writer.getBytes() );
            return;
        }
        TurnSeat& seat     = pRoom->_listSeat[static_cast<size_t>( seatIndex )];
        seat._bTaken       = SW_TRUE;
        seat._connectionId = connectionId;
        if ( bReturning == false )
            seat._token = nextToken();

        BitWriter& writer = _messageWriter.begin( NetTurnRelayMessage::kJoined );
        writer.writeVarUint( roomId );
        writer.writeVarUint( static_cast<uint64>( seatIndex ) );
        writer.writeUint32( seat._token );
        (void)_pHost->sendMessage( connectionId, NetChannelType::ReliableOrdered, writer.getBytes() );

        TurnRelayEvent event;
        event._kind   = bReturning ? TurnRelayEvent::Kind::SeatReturned : TurnRelayEvent::Kind::Joined;
        event._roomId = roomId;
        event._seat   = seatIndex;
        _listEvent.push_back( event );

        // 시작했으면 시작 알림과 놓친 행동을, 다 찼으면 시작을 모두에게.
        bool bFull = true;
        for ( const TurnSeat& other : pRoom->_listSeat )
            bFull = bFull && other._bTaken;
        BitWriter started;
        started.writeBits( NetTurnRelayMessage::kStarted, 8 );
        started.writeVarUint( roomId );
        started.writeVarUint( static_cast<uint64>( pRoom->_currentSeat ) );
        if ( pRoom->_bStarted )
        {
            (void)_pHost->sendMessage( connectionId, NetChannelType::ReliableOrdered, started.getBytes() );
            for ( int32 index = static_cast<int32>( knownCount ); index < static_cast<int32>( pRoom->_listAction.size() ); ++index )
                sendApplied( *pRoom, index, connectionId );
        }
        else if ( bFull )
        {
            pRoom->_bStarted = SW_TRUE;
            broadcastRoom( *pRoom, started.getBytes() );
            TurnRelayEvent startEvent;
            startEvent._kind   = TurnRelayEvent::Kind::Started;
            startEvent._roomId = roomId;
            startEvent._seat   = pRoom->_currentSeat;
            _listEvent.push_back( startEvent );
        }
    }

    void TurnRelayServer::handleAction( int32 connectionId, const uint8* pData, int32 size )
    {
        BitReader     reader( pData + 1, size - 1 );
        const uint32  roomId   = static_cast<uint32>( reader.readVarUint() );
        const int32   submitId = static_cast<int32>( reader.readVarUint() );
        vector<uint8> actionBuffer;
        if ( TurnRelayInternal::readBlob( reader, actionBuffer ) == false )
            return;
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
                const int32 index = static_cast<int32>( pRoom->_listAction.size() ) - 1;
                sendApplied( *pRoom, index, -1 );
                TurnRelayEvent event;
                event._kind   = TurnRelayEvent::Kind::ActionApplied;
                event._roomId = roomId;
                event._seat   = seat;
                event._index  = index;
                event._buffer = actionBuffer;
                _listEvent.push_back( event );
                return;
            }
        }
        BitWriter& writer = _messageWriter.begin( NetTurnRelayMessage::kRejected );
        writer.writeVarUint( roomId );
        writer.writeVarUint( static_cast<uint64>( submitId ) );
        writer.writeBits( static_cast<uint32>( reason ), 8 );
        (void)_pHost->sendMessage( connectionId, NetChannelType::ReliableOrdered, writer.getBytes() );
    }

    bool TurnRelayServer::handleNetMessage( int32 connectionId, const uint8* pData, int32 size )
    {
        if ( size <= 0 || NetMessageRange::isInRange( pData[0], NetMessageRange::kTurnRelay ) == false )
            return false;
        if ( _pHost == nullptr )
            return true;
        if ( pData[0] == NetTurnRelayMessage::kJoin )
            handleJoin( connectionId, pData, size );
        else if ( pData[0] == NetTurnRelayMessage::kAction )
            handleAction( connectionId, pData, size );
        return true;
    }

    void TurnRelayServer::onDisconnected( int32 connectionId )
    {
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
                _listEvent.push_back( event );
            }
        }
    }

    void TurnRelayServer::drainEvents( vector<TurnRelayEvent>& outListEvent )
    {
        outListEvent.insert( outListEvent.end(), _listEvent.begin(), _listEvent.end() );
        _listEvent.clear();
    }

    // ------------------------------------------------------------------------------
    // TurnRelayClient
    // ------------------------------------------------------------------------------
    TurnRelayClient::TurnRelayClient()
        : _listAction{}
        , _listEvent{}
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

    bool TurnRelayClient::handleNetMessage( int32 connectionId, const uint8* pData, int32 size )
    {
        (void)connectionId; // 클라이언트 — 받는 쪽은 서버 하나
        if ( size <= 0 || NetMessageRange::isInRange( pData[0], NetMessageRange::kTurnRelay ) == false )
            return false;
        BitReader      reader( pData + 1, size - 1 );
        TurnRelayEvent event;
        event._roomId = static_cast<uint32>( reader.readVarUint() );
        if ( event._roomId != _roomId )
            return true;
        switch ( pData[0] )
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
                    return true;
                if ( event._index != static_cast<int32>( _listAction.size() ) )
                    return true; // 이미 가진 것(다시 들어올 때 겹친 것)
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
                return true;
        }
        if ( reader.hasOverflowed() == false )
            _listEvent.push_back( event );
        return true;
    }

    void TurnRelayClient::drainEvents( vector<TurnRelayEvent>& outListEvent )
    {
        outListEvent.insert( outListEvent.end(), _listEvent.begin(), _listEvent.end() );
        _listEvent.clear();
    }
} // namespace sw
