#include "pch.h"

#include "GameFramework/Kits/Feature/Online/Matchmaking/Server/Service/MatchBackfillSender.h"

#include "GameFramework/Kits/Feature/Online/Matchmaking/Server/Service/MatchQueueService.h"
#include "GameFramework/Kits/Feature/Online/Matchmaking/Server/Service/MatchServerAgent.h"

namespace sw
{
    MatchBackfillSender::MatchBackfillSender()
        : _descriptor{}
        , _pQueue{ nullptr }
        , _pAgent{ nullptr }
    {
    }

    void MatchBackfillSender::initialize( MatchQueueService* pQueue, MatchServerAgent* pAgent, const ServerDescriptor& descriptor )
    {
        SW_ASSERT( pQueue != nullptr && pAgent != nullptr );
        _pQueue     = pQueue;
        _pAgent     = pAgent;
        _descriptor = descriptor;
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
        request._serverID      = _descriptor._serverID;
        request._region        = _descriptor._region;
        request._address       = _descriptor._address;
        request._port          = _descriptor._port;
        request._portCount     = _descriptor._portCount;
        return _pQueue->submitBackfill( request );
    }

    MatchmakingServiceResult MatchBackfillSender::submitPlayerLeft( AccountID leftAccountID, UnixMilliseconds nowMs )
    {
        SW_ASSERT( _pAgent != nullptr ); // `initialize` 전에 부르지 않는다
        MatchSeat seat;
        if ( _pAgent->takeSeat( leftAccountID, seat ) == false )
            return MatchmakingResult::NotQueued; // 명단에 없거나 이미 맡긴 자리다
        MatchBackfillSlots slots;
        slots._listOpenSlot.assign( static_cast<size_t>( seat._teamCount ), 0 );
        slots._listOpenSlot[static_cast<size_t>( seat._team )] = 1;
        slots._modeID                                          = std::move( seat._modeID );
        slots._matchID                                         = seat._matchID;
        slots._averageRating                                   = seat._averageRating;
        return submitBackfill( slots, nowMs );
    }
} // namespace sw
