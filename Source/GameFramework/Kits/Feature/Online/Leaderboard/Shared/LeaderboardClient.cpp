#include "pch.h"

#include "GameFramework/Kits/Feature/Online/Leaderboard/Shared/LeaderboardClient.h"

#include "Core/Serialization/BitStream.h"

namespace sw
{
    LeaderboardClient::LeaderboardClient()
        : _unlockBuffer{}
    {
    }

    uint64 LeaderboardClient::requestTop( string_view boardID, int32 offset, int32 count, const ReplyDelegate& onReply )
    {
        LeaderboardRequest request;
        request._boardID = string( boardID );
        request._offset  = offset;
        request._count   = count;
        return send( LeaderboardMethod::kGetTop, request, onReply );
    }

    uint64 LeaderboardClient::requestAround( string_view boardID, int32 radius, const ReplyDelegate& onReply )
    {
        LeaderboardRequest request;
        request._boardID = string( boardID );
        request._count   = radius;
        return send( LeaderboardMethod::kGetAround, request, onReply );
    }

    uint64 LeaderboardClient::requestStats( const ReplyDelegate& onReply )
    {
        return send( LeaderboardMethod::kGetStats, LeaderboardRequest{}, onReply );
    }

    uint64 LeaderboardClient::submitScore( string_view boardID, int64 score, const ReplyDelegate& onReply )
    {
        LeaderboardRequest request;
        request._boardID = string( boardID );
        request._score   = score;
        return send( LeaderboardMethod::kSubmitScore, request, onReply );
    }

    uint64 LeaderboardClient::requestAchievements( const ReplyDelegate& onReply )
    {
        return send( LeaderboardMethod::kGetAchievements, LeaderboardRequest{}, onReply );
    }

    uint64 LeaderboardClient::requestSeasonResult( string_view boardID, uint32 seasonID, const ReplyDelegate& onReply )
    {
        LeaderboardRequest request;
        request._boardID  = string( boardID );
        request._seasonID = seasonID;
        return send( LeaderboardMethod::kGetSeasonResult, request, onReply );
    }

    uint64 LeaderboardClient::requestScoresOf( string_view boardID, const vector<AccountID>& listAccountID, const ReplyDelegate& onReply )
    {
        LeaderboardRequest request;
        request._boardID       = string( boardID );
        request._listAccountID = listAccountID;
        return send( LeaderboardMethod::kGetScoresOf, request, onReply );
    }

    uint64 LeaderboardClient::requestAchievementProgress( const ReplyDelegate& onReply )
    {
        return send( LeaderboardMethod::kGetAchievementProgress, LeaderboardRequest{}, onReply );
    }

    void LeaderboardClient::onServicePush( uint16 kind, BitReader& body )
    {
        if ( kind != LeaderboardMethod::kPushAchievement )
            return;
        AchievementState achievement;
        if ( LeaderboardProtocol::MessageTypes::read( body, achievement ) == FixedReadResult::Ok )
            _unlockBuffer.push( std::move( achievement ) );
    }

    uint64 LeaderboardClient::send( uint16 method, const LeaderboardRequest& request, const ReplyDelegate& onReply )
    {
        // 다시 보내도 합산 보드에 두 번 들어가지 않게 점수 제출에는 멱등 키를 붙인다.
        const NetIdempotencyKey key = method == LeaderboardMethod::kSubmitScore ? NetIdempotencyKey::makeRandom() : NetIdempotencyKey{};
        return sendRequest( method, request, key, onReply );
    }
} // namespace sw
