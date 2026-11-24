#include "pch.h"

#include "GameFramework/Kits/Feature/Online/Matchmaking/Server/Service/MatchBackfillSender.h"

#include "GameFramework/Kits/Feature/Online/Matchmaking/Server/Service/MatchQueueService.h"
#include "GameFramework/Kits/Feature/Online/Matchmaking/Server/Service/MatchServerAgent.h"

namespace sw
{
    MatchBackfillSender::MatchBackfillSender()
        : _pDescriptor{ nullptr }
        , _pQueue{ nullptr }
        , _pAgent{ nullptr }
    {
    }

    void MatchBackfillSender::initialize( MatchQueueService* pQueue, MatchServerAgent* pAgent, ServerDescriptor& descriptor )
    {
        SW_ASSERT( pQueue != nullptr && pAgent != nullptr );
        _pQueue      = pQueue;
        _pAgent      = pAgent;
        _pDescriptor = &descriptor;
    }

    MatchmakingServiceResult MatchBackfillSender::submitBackfill( const MatchBackfillSlots& slots, UnixMilliseconds nowMs )
    {
        SW_ASSERT( _pQueue != nullptr ); // `initialize` 전에 부르지 않는다
        MatchBackfillRequest request;
        request._listOpenSlot  = slots._listOpenSlot;
        request._modeID        = slots._modeID;
        request._matchID       = slots._matchID;
        request._averageRating = slots._averageRating;
        request._requestedMs   = nowMs.count();
        request._serverID      = _pDescriptor->_serverID;
        request._region        = _pDescriptor->_region;
        request._address       = _pDescriptor->_address;
        request._port          = _pDescriptor->_port;
        request._portCount     = _pDescriptor->_portCount;
        return _pQueue->submitBackfill( request );
    }

    MatchmakingServiceResult MatchBackfillSender::submitPlayerLeft( AccountID leftAccountID, UnixMilliseconds nowMs )
    {
        SW_ASSERT( _pAgent != nullptr ); // `initialize` 전에 부르지 않는다
        MatchSeat seat;
        if ( _pAgent->takeSeat( leftAccountID, seat ) == false )
            return MatchmakingResult::NotQueued; // 명단에 없거나 이미 맡긴 자리다
        if ( _pAgent->getSeatCount( seat._matchID ) == 0 )
            return MatchmakingResult::NotQueued; // 경기가 비었다 — 이을 판이 없다. 백필로 한 사람, 새 경기로 또 한 사람이 같은 자리에 오지 않게 새 경기만 받는다
        MatchBackfillSlots slots;
        slots._listOpenSlot.assign( static_cast<size_t>( seat._teamCount ), 0 );
        slots._listOpenSlot[static_cast<size_t>( seat._team )] = 1;
        slots._modeID                                          = std::move( seat._modeID );
        slots._matchID                                         = seat._matchID;
        slots._averageRating                                   = seat._averageRating;
        return submitBackfill( slots, nowMs );
    }

    void MatchBackfillSender::setSeatCapacity( int32 seatCount )
    {
        SW_ASSERT( _pAgent != nullptr && seatCount > 0 ); // `initialize` 전에 부르지 않는다
        _pDescriptor->_capacity = seatCount;              // 런타임이 다음 갱신에 디렉터리에 올린다
        _pAgent->setSeatCapacity( seatCount );
    }
} // namespace sw
