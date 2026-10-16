#include "pch.h"

#include "GameFramework/Kits/Feature/Online/Leaderboard/Shared/LeaderboardProtocol.h"

namespace sw
{
    LeaderboardResult LeaderboardProtocol::fromErrorCode( uint16 errorCode )
    {
        static constexpr OnlineErrorResult<LeaderboardResult> kArrErrorResult[] = {
            {             OnlineError::kOk,          LeaderboardResult::Ok},
            {OnlineError::kUnauthenticated, LeaderboardResult::NotSignedIn},
            { OnlineError::kInvalidRequest,     LeaderboardResult::Invalid},
            {       OnlineError::kConflict,    LeaderboardResult::Conflict},
        };
        return OnlineError::toResult( errorCode, kArrErrorResult, LeaderboardResult::Unavailable );
    }
} // namespace sw

namespace sw
{
    const OnlineProtocolEntry LeaderboardProtocol::kOnlineEntry{ "Leaderboard", OnlineMethodRange::kLeaderboard, LeaderboardProtocol::MessageTypes{} };
} // namespace sw
