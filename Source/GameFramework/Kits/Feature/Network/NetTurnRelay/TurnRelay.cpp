#include "pch.h"

#include "GameFramework/Kits/Feature/Network/NetTurnRelay/TurnRelay.h"

#include "Core/Algorithm/VectorUtil.h"
#include "Core/Foundation/Constants.h"
#include "Core/Foundation/Expected.h"
#include "Core/Hash/HashUtil.h"
#include "Core/Hash/SecureUtil.h"
#include "Core/Math/MathUtil.h"
#include "Core/Network/Connection/NetEndpoint.h"
#include "Core/Network/NetTypes.h"
#include "Core/Serialization/BitStream.h"
#include "Core/Serialization/LittleEndianUtil.h"
#include "Core/UUID/UUID.h"

#include "GameFramework/Runtime/Foundation/GameData/ConfigRangeUtil.h"

namespace sw
{
    namespace
    {
        struct TurnRelayInternal
        {
            /** @brief 자리 토큰 키입니다. @p tokenSeed 가 0 이면 운영체제 난수(`UUID::generate` 의 16 바이트), 아니면 시드에서 만든다(테스트 — 키를 맞힐 수 있다). */
            static SipHashKey makeTokenKey( uint64 tokenSeed )
            {
                if ( tokenSeed != 0 )
                    return SipHashKey{ HashUtil::mix64( tokenSeed ), HashUtil::mix64( ~tokenSeed ) };
                const UUID uuid = UUID::generate();
                return SipHashKey{ LittleEndianUtil::readUint64( uuid._arrBytes ), LittleEndianUtil::readUint64( uuid._arrBytes + 8 ) };
            }

            /** @brief @p token 이 맞는 차지된 자리를 반환합니다. 토큰이 0 이거나 맞는 자리가 없으면 비어 있습니다. */
            static optional<int32> findReturningSeat( const TurnRoom& room, uint32 token )
            {
                optional<int32> seatIndex;
                for ( size_t index = 0; index < room._listSeat.size() && token != 0; ++index )
                {
                    if ( room._listSeat[index]._bTaken && room._listSeat[index]._token == token )
                        seatIndex = static_cast<int32>( index );
                }
                return seatIndex;
            }

            /** @brief 비었으면 원하는 자리(@p wantedSeat), 아니면 앞에서부터 첫 빈 자리를 반환합니다. 다 찼으면 비어 있습니다. */
            static optional<int32> findFreeSeat( const TurnRoom& room, int64 wantedSeat )
            {
                const bool bWantedFree = 0 <= wantedSeat && wantedSeat < static_cast<int64>( room._listSeat.size() ) &&
                                         room._listSeat[static_cast<size_t>( wantedSeat )]._bTaken == SW_FALSE;
                if ( bWantedFree )
                    return static_cast<int32>( wantedSeat );
                for ( size_t index = 0; index < room._listSeat.size(); ++index )
                {
                    if ( room._listSeat[index]._bTaken == SW_FALSE )
                        return static_cast<int32>( index );
                }
                return nullopt;
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
        , _tokenKey{}
        , _tokenCount{ 0 }
        , _seatCount{ 2 }
        , _maxRoomCount{ 0 }
        , _maxActionsPerRoom{ 0 }
        , _messageWriter{}
    {
    }

    bool TurnRelayServer::initialize( INetPeers* pHost, const TurnRelayConfig& settings, const ITurnPolicy* pPolicy, uint64 tokenSeed )
    {
        // 실패하면 호스트 없는 중계기로 남긴다. 메시지와 `update` 가 아무것도 하지 않는다. 값을 범위 안으로 맞춰 돌지 않는다.
        _pHost                  = nullptr;
        const string rangeError = ConfigRangeUtil::describeRangeErrors( settings );
        if ( rangeError.empty() == false )
        {
            SW_LOG_ERROR_ID( "TurnRelay.TurnRelayServerRefusedStartSettings", "TurnRelayServer: refused to start - settings '%#'", rangeError.c_str() );
            return false;
        }
        _pHost             = pHost;
        _seatCount         = settings._seatCount;
        _maxRoomCount      = settings._maxRoomCount;
        _maxActionsPerRoom = settings._maxActionsPerRoom;
        _pPolicy           = pPolicy != nullptr ? pPolicy : &_defaultPolicy;
        _tokenKey          = TurnRelayInternal::makeTokenKey( tokenSeed );
        _tokenCount        = 0;
        _listRoom.clear();
        _listPending.clear();
        _eventBuffer.clear();
        return true;
    }

    uint32 TurnRelayServer::nextToken()
    {
        // 키 있는 PRF(SipHash-2-4)에 카운터를 넣는다. 옛 식(비밀 + n × 황금비 의 mix64)은 splitmix 수열이라 받은 토큰에서 키를 되찾아 남의 자리 토큰을 맞힐 수 있었다.
        ++_tokenCount;
        uint8 arrCounter[8];
        LittleEndianUtil::writeUint64( arrCounter, _tokenCount );
        const uint32 token = static_cast<uint32>( SecureUtil::sipHash24( _tokenKey, arrCounter, sizeof( arrCounter ) ) );
        return token != 0 ? token : 1u;
    }

    TurnRoom* TurnRelayServer::findRoomMutable( uint32 roomID )
    {
        return VectorUtil::findBy<&TurnRoom::_roomID>( _listRoom, roomID );
    }

    const TurnRoom* TurnRelayServer::findRoom( uint32 roomID ) const
    {
        return const_cast<TurnRelayServer*>( this )->findRoomMutable( roomID );
    }

    TurnRoom* TurnRelayServer::findRoomOfConnection( int32 connectionID )
    {
        for ( TurnRoom& room : _listRoom )
        {
            for ( const TurnSeat& seat : room._listSeat )
            {
                if ( seat._connectionID == connectionID )
                    return &room;
            }
        }
        return nullptr;
    }

    int32 TurnRelayServer::countPending( int32 connectionID ) const
    {
        int32 count = 0;
        for ( const PendingMessage& pending : _listPending )
        {
            count += pending._connectionID == connectionID ? 1 : 0;
        }
        return count;
    }

    void TurnRelayServer::sendOrQueue( int32 connectionID )
    {
        const int32                    pendingCount = countPending( connectionID );
        const optional<NetChannelKind> channel      = _messageWriter.getMessageChannel();
        if ( channel.has_value() == false )
            return; // 서술자가 있는 메시지만 줄에 선다 — 쓰지 못한 메시지는 오류 로그가 남았다
        if ( pendingCount == 0 && _messageWriter.sendMessage( *_pHost, connectionID ) )
            return;
        if ( _pHost->getConnectionState( connectionID ) != NetConnectionState::Connected )
            return; // 닫힌 연결이다. 돌아오면 토큰으로 다시 받는다
        if ( pendingCount >= kMaxPendingPerConnection )
        {
            SW_LOG_WARNING_ID( "TurnRelay.ConnectionDoesNotAcknowledge", "connection %# does not acknowledge - %# messages are waiting, disconnecting it", connectionID, pendingCount );
            _pHost->disconnect( connectionID );
            return;
        }
        _listPending.push_back( PendingMessage{ _messageWriter.getBytes(), connectionID, *channel } );
    }

    void TurnRelayServer::flushPending()
    {
        // 연결마다 순서를 지킨다. 한 번 막힌 연결의 뒤쪽 알림은 이번에 보내지 않는다.
        _listBlockedScratch.clear();
        size_t keepCount = 0;
        for ( size_t index = 0; index < _listPending.size(); ++index )
        {
            PendingMessage& pending  = _listPending[index];
            bool            bBlocked = false;
            for ( const int32 blocked : _listBlockedScratch )
            {
                bBlocked = bBlocked || blocked == pending._connectionID;
            }
            if ( bBlocked == false && _pHost->trySendMessage( pending._connectionID, pending._channel, pending._buffer ) )
                continue;
            if ( bBlocked == false )
                _listBlockedScratch.push_back( pending._connectionID );
            if ( keepCount != index )
                _listPending[keepCount] = std::move( pending );
            ++keepCount;
        }
        _listPending.resize( keepCount );
    }

    void TurnRelayServer::flushSeat( const TurnRoom& room, TurnSeat& seat )
    {
        if ( seat._connectionID < 0 || countPending( seat._connectionID ) > 0 )
            return; // 줄 선 알림은 `update` 가 먼저 보내고 그 뒤에 이어서 보낸다
        while ( seat._sentActionCount < static_cast<int32>( room._listAction.size() ) )
        {
            const int32       index  = seat._sentActionCount;
            const TurnAction& action = room._listAction[static_cast<size_t>( index )];
            TurnAppliedBody   body;
            body._roomID = room._roomID;
            body._index  = index;
            body._seat   = action._seat;
            body._action = action._buffer;
            if ( _messageWriter.writeMessage<NetTurnRelayMessage::Table, NetTurnRelayMessage::Applied>( body ) == false )
                return; // 받아들일 때 상한을 봤다 — 여기 오면 결함이고 오류 로그가 남았다
            if ( _messageWriter.sendMessage( *_pHost, seat._connectionID ) == false )
                return; // 창이 찼으므로 `update` 가 이어서 보낸다
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
            {
                flushSeat( room, seat );
            }
        }
    }

    void TurnRelayServer::sendJoined( int32 connectionID, uint32 roomID, int32 seat, uint32 token )
    {
        TurnJoinedBody body;
        body._roomID = roomID;
        body._seat   = seat;
        body._token  = token;
        if ( _messageWriter.writeMessage<NetTurnRelayMessage::Table, NetTurnRelayMessage::Joined>( body ) )
            sendOrQueue( connectionID );
    }

    void TurnRelayServer::sendDenied( int32 connectionID, uint32 roomID, TurnRejectReason reason )
    {
        TurnDeniedBody body;
        body._roomID = roomID;
        body._reason = reason;
        if ( _messageWriter.writeMessage<NetTurnRelayMessage::Table, NetTurnRelayMessage::Denied>( body ) )
            sendOrQueue( connectionID );
    }

    void TurnRelayServer::handleJoin( int32 connectionID, const TurnJoinBody& body )
    {
        const uint32 roomID     = body._roomID;
        const int64  wantedSeat = body._seat;
        const uint32 token      = body._token;
        const uint64 knownCount = body._knownActionCount;
        // 한 연결은 자리를 하나만 가진다. 같은 방이면 가진 자리를 다시 알리고, 다른 방이면 거절한다.
        const TurnRoom* pHeldRoom = findRoomOfConnection( connectionID );
        if ( pHeldRoom != nullptr )
        {
            if ( pHeldRoom->_roomID != roomID )
            {
                sendDenied( connectionID, roomID, TurnRejectReason::AlreadySeated );
                return;
            }
            for ( size_t index = 0; index < pHeldRoom->_listSeat.size(); ++index )
            {
                if ( pHeldRoom->_listSeat[index]._connectionID == connectionID )
                    sendJoined( connectionID, roomID, static_cast<int32>( index ), pHeldRoom->_listSeat[index]._token );
            }
            return;
        }
        TurnRoom* pRoom = findRoomMutable( roomID );
        if ( pRoom == nullptr )
        {
            if ( static_cast<int32>( _listRoom.size() ) >= _maxRoomCount )
            {
                sendDenied( connectionID, roomID, TurnRejectReason::ServerFull );
                return;
            }
            TurnRoom room;
            room._roomID = roomID;
            room._listSeat.resize( static_cast<size_t>( _seatCount ) );
            _listRoom.push_back( room );
            pRoom = &_listRoom.back();
        }
        // 토큰이 맞는 자리는 돌아온 것으로 본다. 그 자리의 연결이 아직 살아 있으면 빼앗지 않는다. 토큰이 없으면 빈 자리를 준다.
        optional<int32> seatIndex  = TurnRelayInternal::findReturningSeat( *pRoom, token );
        const bool      bReturning = seatIndex.has_value();
        if ( bReturning && pRoom->_listSeat[static_cast<size_t>( *seatIndex )]._connectionID >= 0 )
        {
            sendDenied( connectionID, roomID, TurnRejectReason::SeatInUse );
            return;
        }
        if ( seatIndex.has_value() == false )
            seatIndex = TurnRelayInternal::findFreeSeat( *pRoom, wantedSeat );
        if ( seatIndex.has_value() == false )
        {
            sendDenied( connectionID, roomID, TurnRejectReason::RoomFull );
            return;
        }
        TurnSeat& seat        = pRoom->_listSeat[static_cast<size_t>( *seatIndex )];
        seat._bTaken          = SW_TRUE;
        seat._connectionID    = connectionID;
        seat._sentActionCount = static_cast<int32>( MathUtil::min<uint64>( knownCount, pRoom->_listAction.size() ) );
        if ( bReturning == false )
            seat._token = nextToken();
        sendJoined( connectionID, roomID, *seatIndex, seat._token );

        TurnRelayEvent event;
        event._kind   = bReturning ? TurnRelayEvent::Kind::SeatReturned : TurnRelayEvent::Kind::Joined;
        event._roomID = roomID;
        event._seat   = *seatIndex;
        _eventBuffer.push( event );
        announceStart( *pRoom, seat, connectionID );
    }

    void TurnRelayServer::announceStart( TurnRoom& room, TurnSeat& seat, int32 connectionID )
    {
        bool bFull = true;
        for ( const TurnSeat& other : room._listSeat )
        {
            bFull = bFull && other._bTaken;
        }
        if ( room._bStarted == SW_FALSE && bFull == false )
            return;
        const bool bStartsNow = room._bStarted == SW_FALSE;
        room._bStarted        = SW_TRUE;
        for ( const TurnSeat& other : room._listSeat )
        {
            const bool bNotified = other._connectionID >= 0 && ( bStartsNow || other._connectionID == connectionID );
            if ( bNotified == false )
                continue;
            TurnStartedBody body;
            body._roomID      = room._roomID;
            body._currentSeat = room._currentSeat;
            if ( _messageWriter.writeMessage<NetTurnRelayMessage::Table, NetTurnRelayMessage::Started>( body ) )
                sendOrQueue( other._connectionID );
        }
        flushSeat( room, seat );
        if ( bStartsNow )
        {
            TurnRelayEvent startEvent;
            startEvent._kind   = TurnRelayEvent::Kind::Started;
            startEvent._roomID = room._roomID;
            startEvent._seat   = room._currentSeat;
            _eventBuffer.push( startEvent );
        }
    }

    void TurnRelayServer::handleAction( int32 connectionID, const TurnActionBody& body )
    {
        const uint32     roomID       = body._roomID;
        const int32      submitID     = body._submitID;
        vector<uint8>    actionBuffer = body._action; // 길이 상한은 본문 읽기가 봤다(`TurnRelayLimit::kMaxActionBytes`)
        TurnRoom*        pRoom        = findRoomMutable( roomID );
        int32            seat         = constant::invalid_index::kInt32;
        TurnRejectReason reason       = TurnRejectReason::UnknownRoom;
        if ( pRoom != nullptr )
        {
            for ( size_t index = 0; index < pRoom->_listSeat.size(); ++index )
            {
                if ( pRoom->_listSeat[index]._connectionID == connectionID )
                    seat = static_cast<int32>( index );
            }
            if ( seat < 0 )
                reason = TurnRejectReason::UnknownRoom;
            else if ( pRoom->_bStarted == SW_FALSE )
                reason = TurnRejectReason::NotStarted;
            else if ( _pPolicy->isActionAllowed( *pRoom, seat, actionBuffer ) == false )
                reason = seat != pRoom->_currentSeat ? TurnRejectReason::NotYourTurn : TurnRejectReason::InvalidAction;
            else if ( static_cast<int32>( pRoom->_listAction.size() ) >= _maxActionsPerRoom )
                reason = TurnRejectReason::HistoryFull;
            else
            {
                pRoom->_listAction.push_back( TurnAction{ actionBuffer, seat } );
                _pPolicy->applyAction( *pRoom, seat, actionBuffer );
                for ( TurnSeat& other : pRoom->_listSeat )
                {
                    flushSeat( *pRoom, other );
                }
                TurnRelayEvent event;
                event._kind   = TurnRelayEvent::Kind::ActionApplied;
                event._roomID = roomID;
                event._seat   = seat;
                event._index  = static_cast<int32>( pRoom->_listAction.size() ) - 1;
                event._buffer = std::move( actionBuffer );
                _eventBuffer.push( event );
                return;
            }
        }
        TurnRejectedBody rejected;
        rejected._roomID   = roomID;
        rejected._submitID = submitID;
        rejected._reason   = reason;
        if ( _messageWriter.writeMessage<NetTurnRelayMessage::Table, NetTurnRelayMessage::Rejected>( rejected ) )
            sendOrQueue( connectionID );
    }

    NetHandleResult TurnRelayServer::receive( NetTurnRelayMessage::Join, const NetMessageContext& context, const TurnJoinBody& body )
    {
        if ( _pHost != nullptr )
            handleJoin( context._connectionID, body );
        return NetHandleResult::Handled;
    }

    NetHandleResult TurnRelayServer::receive( NetTurnRelayMessage::Action, const NetMessageContext& context, const TurnActionBody& body )
    {
        if ( _pHost != nullptr )
            handleAction( context._connectionID, body );
        return NetHandleResult::Handled;
    }

    void TurnRelayServer::onConnectionClosed( int32 connectionID, NetDisconnectReason reason )
    {
        (void)reason;
        // 연결 id 는 다음 연결이 다시 쓰므로 그 연결의 큐에 쌓인 알림도 버린다.
        size_t keepCount = 0;
        for ( size_t index = 0; index < _listPending.size(); ++index )
        {
            if ( _listPending[index]._connectionID == connectionID )
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
                if ( seat._connectionID != connectionID )
                    continue;
                seat._connectionID = constant::invalid_index::kInt32; // 자리와 토큰은 남긴다
                TurnRelayEvent event;
                event._kind   = TurnRelayEvent::Kind::SeatLeft;
                event._roomID = room._roomID;
                event._seat   = static_cast<int32>( index );
                _eventBuffer.push( event );
            }
        }
    }

    void TurnRelayServer::pollEvents( vector<TurnRelayEvent>& outListEvent )
    {
        _eventBuffer.moveAllTo( outListEvent );
    }

    // ------------------------------------------------------------------------------
    // TurnRelayClient
    // ------------------------------------------------------------------------------
    TurnRelayClient::TurnRelayClient()
        : _listAction{}
        , _eventBuffer{}
        , _pHost{ nullptr }
        , _roomID{ 0 }
        , _token{ 0 }
        , _seat{ constant::invalid_index::kInt32 }
        , _nextSubmitID{ 0 }
        , _bStarted{ SW_FALSE }
        , _messageWriter{}
    {
    }

    void TurnRelayClient::initialize( INetPeers* pHost )
    {
        _pHost = pHost;
    }

    void TurnRelayClient::join( uint32 roomID, int32 seat )
    {
        if ( _roomID != roomID )
        {
            _listAction.clear();
            _token    = 0;
            _seat     = constant::invalid_index::kInt32;
            _bStarted = SW_FALSE;
        }
        _roomID = roomID;
        TurnJoinBody body;
        body._roomID           = roomID;
        body._seat             = seat;
        body._token            = _token;
        body._knownActionCount = static_cast<uint32>( _listAction.size() );
        if ( _messageWriter.writeMessage<NetTurnRelayMessage::Table, NetTurnRelayMessage::Join>( body ) )
            (void)_messageWriter.sendMessage( *_pHost, 0 ); // 못 보내면 다시 join 한다
    }

    int32 TurnRelayClient::submitAction( const vector<uint8>& buffer )
    {
        if ( static_cast<int32>( buffer.size() ) > NetTurnRelayMessage::kMaxActionBytes )
        {
            SW_LOG_ERROR_ID( "TurnRelay.TurnActionBytesExceeds", "Turn action of %# bytes exceeds the relay limit of %# bytes - not sent", buffer.size(), NetTurnRelayMessage::kMaxActionBytes );
            return constant::invalid_index::kInt32;
        }
        const int32    submitID = _nextSubmitID++;
        TurnActionBody body;
        body._roomID   = _roomID;
        body._submitID = submitID;
        body._action   = buffer;
        if ( _messageWriter.writeMessage<NetTurnRelayMessage::Table, NetTurnRelayMessage::Action>( body ) )
            (void)_messageWriter.sendMessage( *_pHost, 0 ); // 못 보낸 행동은 거절 알림도 오지 않는다 — 예전과 같다
        return submitID;
    }

    NetHandleResult TurnRelayClient::receive( NetTurnRelayMessage::Joined, const NetMessageContext& context, const TurnJoinedBody& body )
    {
        (void)context;
        if ( isCurrentRoom( body._roomID ) == false )
            return NetHandleResult::Handled; // 다른 방의 알림이다
        _seat  = body._seat;
        _token = body._token;
        TurnRelayEvent event;
        event._kind   = TurnRelayEvent::Kind::Joined;
        event._roomID = body._roomID;
        event._seat   = _seat;
        _eventBuffer.push( event );
        return NetHandleResult::Handled;
    }

    NetHandleResult TurnRelayClient::receive( NetTurnRelayMessage::Started, const NetMessageContext& context, const TurnStartedBody& body )
    {
        (void)context;
        if ( isCurrentRoom( body._roomID ) == false )
            return NetHandleResult::Handled;
        _bStarted = SW_TRUE;
        TurnRelayEvent event;
        event._kind   = TurnRelayEvent::Kind::Started;
        event._roomID = body._roomID;
        event._seat   = body._currentSeat;
        _eventBuffer.push( event );
        return NetHandleResult::Handled;
    }

    NetHandleResult TurnRelayClient::receive( NetTurnRelayMessage::Applied, const NetMessageContext& context, const TurnAppliedBody& body )
    {
        (void)context;
        if ( isCurrentRoom( body._roomID ) == false )
            return NetHandleResult::Handled;
        if ( body._index != static_cast<int32>( _listAction.size() ) )
            return NetHandleResult::Handled; // 이미 가진 행동이다. 다시 들어올 때 겹친 것이다
        _listAction.push_back( TurnAction{ body._action, body._seat } );
        TurnRelayEvent event;
        event._kind   = TurnRelayEvent::Kind::ActionApplied;
        event._roomID = body._roomID;
        event._index  = body._index;
        event._seat   = body._seat;
        event._buffer = body._action;
        _eventBuffer.push( event );
        return NetHandleResult::Handled;
    }

    NetHandleResult TurnRelayClient::receive( NetTurnRelayMessage::Rejected, const NetMessageContext& context, const TurnRejectedBody& body )
    {
        (void)context;
        if ( isCurrentRoom( body._roomID ) == false )
            return NetHandleResult::Handled;
        TurnRelayEvent event;
        event._kind   = TurnRelayEvent::Kind::ActionRejected;
        event._roomID = body._roomID;
        event._index  = body._submitID;
        event._reason = body._reason;
        _eventBuffer.push( event );
        return NetHandleResult::Handled;
    }

    NetHandleResult TurnRelayClient::receive( NetTurnRelayMessage::Denied, const NetMessageContext& context, const TurnDeniedBody& body )
    {
        (void)context;
        if ( isCurrentRoom( body._roomID ) == false )
            return NetHandleResult::Handled;
        TurnRelayEvent event;
        event._kind   = TurnRelayEvent::Kind::Denied;
        event._roomID = body._roomID;
        event._reason = body._reason;
        _eventBuffer.push( event );
        return NetHandleResult::Handled;
    }

    void TurnRelayClient::pollEvents( vector<TurnRelayEvent>& outListEvent )
    {
        _eventBuffer.moveAllTo( outListEvent );
    }
} // namespace sw
