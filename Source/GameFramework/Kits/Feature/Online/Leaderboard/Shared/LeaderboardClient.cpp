#include "pch.h"

#include "GameFramework/Kits/Feature/Online/Leaderboard/Shared/LeaderboardClient.h"

#include "Core/Network/BitStream.h"

namespace sw
{
    LeaderboardClient::LeaderboardClient()
        : _callTable{}
        , _unlockBuffer{}
        , _pClient{ nullptr }
    {
    }

    void LeaderboardClient::initialize( OnlineServiceClient* pClient ) { _pClient = pClient; }

    uint64 LeaderboardClient::requestTop( string_view boardId, int32 offset, int32 count, const LeaderboardReplyDelegate& onReply )
    {
        LeaderboardRequest request;
        request._boardId = string( boardId );
        request._offset  = offset;
        request._count   = count;
        return send( LeaderboardMethod::kGetTop, request, onReply );
    }

    uint64 LeaderboardClient::requestAround( string_view boardId, int32 radius, const LeaderboardReplyDelegate& onReply )
    {
        LeaderboardRequest request;
        request._boardId = string( boardId );
        request._count   = radius;
        return send( LeaderboardMethod::kGetAround, request, onReply );
    }

    uint64 LeaderboardClient::requestStats( const LeaderboardReplyDelegate& onReply ) { return send( LeaderboardMethod::kGetStats, LeaderboardRequest{}, onReply ); }

    uint64 LeaderboardClient::submitScore( string_view boardId, int64 score, const LeaderboardReplyDelegate& onReply )
    {
        LeaderboardRequest request;
        request._boardId = string( boardId );
        request._score   = score;
        return send( LeaderboardMethod::kSubmitScore, request, onReply );
    }

    uint64 LeaderboardClient::requestAchievements( const LeaderboardReplyDelegate& onReply )
    {
        return send( LeaderboardMethod::kGetAchievements, LeaderboardRequest{}, onReply );
    }

    uint64 LeaderboardClient::requestSeasonResult( string_view boardId, uint32 seasonId, const LeaderboardReplyDelegate& onReply )
    {
        LeaderboardRequest request;
        request._boardId  = string( boardId );
        request._seasonId = seasonId;
        return send( LeaderboardMethod::kGetSeasonResult, request, onReply );
    }

    void LeaderboardClient::onServicePush( uint16 kind, BitReader& body )
    {
        if ( kind != LeaderboardMethod::kPushAchievement )
            return;
        AchievementState achievement;
        if ( LeaderboardProtocol::readAchievement( body, achievement ) )
            _unlockBuffer.push( std::move( achievement ) );
    }

    uint64 LeaderboardClient::send( uint16 method, const LeaderboardRequest& request, const LeaderboardReplyDelegate& onReply )
    {
        if ( _pClient == nullptr )
        {
            LeaderboardClientReply reply;
            reply._method        = method;
            reply._errorCode     = OnlineError::kUnavailable;
            reply._reply._result = LeaderboardResult::Unavailable;
            if ( onReply.isBound() )
                onReply( reply );
            return 0;
        }
        BitWriter body;
        LeaderboardProtocol::writeRequest( body, request );
        NetRequestOptions options;
        if ( method == LeaderboardMethod::kSubmitScore )
            options._idempotencyKey = NetIdempotencyKey::makeRandom(); // 다시 보내도 합산 표에 두 번 들어가지 않게
        return _callTable.send( *_pClient, method, body, options, OnlineResponseDelegate::create<&LeaderboardClient::onResponse>( this ), PendingCall{ onReply, method } );
    }

    void LeaderboardClient::onResponse( const OnlineResponse& response )
    {
        PendingCall call;
        if ( _callTable.take( response._requestId, call ) == false )
            return;
        LeaderboardClientReply reply;
        reply._requestId = response._requestId;
        reply._method    = call._method;
        reply._errorCode = response._errorCode;
        if ( response._errorCode != OnlineError::kOk )
        {
            reply._reply._result = LeaderboardProtocol::fromErrorCode( response._errorCode );
        }
        else
        {
            BitReader reader( response._pBody, response._bodySize );
            if ( LeaderboardProtocol::readReply( reader, reply._reply ) == false )
            {
                reply._reply         = LeaderboardReply{};
                reply._reply._result = LeaderboardResult::Unavailable;
            }
        }
        if ( call._onReply.isBound() )
            call._onReply( reply );
    }
} // namespace sw
