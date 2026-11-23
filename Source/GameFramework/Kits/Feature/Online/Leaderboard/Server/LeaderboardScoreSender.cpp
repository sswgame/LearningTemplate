#include "pch.h"

#include "GameFramework/Kits/Feature/Online/Leaderboard/Server/LeaderboardScoreSender.h"

#include "Core/Network/Message/NetRequest.h"

#include "GameFramework/Kits/Feature/Online/Leaderboard/Server/LeaderboardService.h"

namespace sw
{
    LeaderboardScoreSender::LeaderboardScoreSender( LeaderboardService& service )
        : _service{ service }
    {
    }

    LeaderboardResult LeaderboardScoreSender::submitScore( string_view boardID, AccountID accountID, int64 score, const NetIdempotencyKey& idempotencyKey, UnixMilliseconds nowMs )
    {
        if ( accountID == kInvalidAccountID || idempotencyKey.isValid() == false )
            return LeaderboardResult::NotAllowed;
        if ( _service.findBoard( boardID ) == nullptr )
            return LeaderboardResult::UnknownBoard;
        _service.submitScore( boardID, accountID, string_view{}, score, idempotencyKey, nowMs, kRequestTag ); // 표시 이름은 로직이 계정 이름 색인에서 채운다
        return LeaderboardResult::Ok;
    }
} // namespace sw
