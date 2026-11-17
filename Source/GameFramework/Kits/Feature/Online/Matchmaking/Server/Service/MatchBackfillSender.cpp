#include "pch.h"

#include "GameFramework/Kits/Feature/Online/Matchmaking/Server/Service/MatchBackfillSender.h"

#include "GameFramework/Kits/Feature/Online/Matchmaking/Server/Service/MatchQueueService.h"

namespace sw
{
    MatchBackfillSender::MatchBackfillSender()
        : _descriptor{}
        , _pQueue{ nullptr }
    {
    }

    void MatchBackfillSender::initialize( MatchQueueService* pQueue, const ServerDescriptor& descriptor )
    {
        SW_ASSERT( pQueue != nullptr );
        _pQueue     = pQueue;
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
} // namespace sw
