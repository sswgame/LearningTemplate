#include "pch.h"

#include "GameFramework/Kits/Online/Server/Leaderboard/LeaderboardServer.h"

#include "Core/Network/BitStream.h"

#include "GameFramework/Base/Online/Identity/AccountDirectory.h"
#include "GameFramework/Base/Online/Identity/AccountPresence.h"
#include "GameFramework/Kits/Online/Leaderboard/LeaderboardProtocol.h"

namespace sw
{
    LeaderboardServer::LeaderboardServer()
        : _pendingTable{}
        , _listCompletionScratch{}
        , _listUnlockScratch{}
        , _pService{ nullptr }
        , _pDirectory{ nullptr }
        , _pPresence{ nullptr }
    {
    }

    void LeaderboardServer::initialize( LeaderboardService* pService, const IAccountDirectory* pDirectory, IAccountPresence* pPresence )
    {
        _pService   = pService;
        _pDirectory = pDirectory;
        _pPresence  = pPresence;
    }

    void LeaderboardServer::shutdown()
    {
        _pendingTable.clear();
        _pService   = nullptr;
        _pDirectory = nullptr;
        _pPresence  = nullptr;
    }

    uint32 LeaderboardServer::getProtocolVersion() const { return LeaderboardProtocol::kVersion; }

    void LeaderboardServer::onServiceRequest( OnlineServiceHost& host, const OnlineCallContext& context, BitReader& body )
    {
        if ( _pService == nullptr )
        {
            (void)host.respondError( context._token, OnlineError::kUnavailable );
            return;
        }
        const bool bKnownMethod = LeaderboardMethod::kGetTop <= context._method && context._method <= LeaderboardMethod::kGetSeasonResult;
        if ( bKnownMethod == false )
        {
            (void)host.respondError( context._token, OnlineError::kNotFound );
            return;
        }
        LeaderboardRequest request;
        if ( LeaderboardProtocol::readRequest( body, request ) == false )
        {
            (void)host.respondError( context._token, OnlineError::kInvalidRequest );
            return;
        }
        if ( context._method == LeaderboardMethod::kSubmitScore )
        {
            const LeaderboardDefinition* pBoard = _pService->findBoard( request._boardId );
            if ( pBoard == nullptr || pBoard->_bClientSubmit == SW_FALSE )
            {
                LeaderboardReply reply;
                reply._result = pBoard == nullptr ? LeaderboardResult::UnknownBoard : LeaderboardResult::NotAllowed; // 점수는 서버가 낸다
                BitWriter replyBody;
                LeaderboardProtocol::writeReply( replyBody, reply );
                (void)host.respondOk( context._token, replyBody );
                return;
            }
        }
        const uint64    requestTag = _pendingTable.add( context._token );
        const AccountId selfId     = context._accountId;
        switch ( context._method )
        {
            case LeaderboardMethod::kGetTop:
            {
                _pService->readTop( request._boardId, request._offset, request._count, context._nowMs, requestTag );
                break;
            }
            case LeaderboardMethod::kGetAround:
            {
                _pService->readAround( request._boardId, selfId, request._count, context._nowMs, requestTag );
                break;
            }
            case LeaderboardMethod::kGetStats:
            {
                _pService->readStats( selfId, requestTag );
                break;
            }
            case LeaderboardMethod::kSubmitScore:
            {
                AccountIdentity identity;
                if ( _pDirectory == nullptr || _pDirectory->findIdentity( selfId, identity ) == false )
                    identity._displayName.clear();
                _pService->submitScore( request._boardId, selfId, identity._displayName, request._score, context._nowMs, requestTag );
                break;
            }
            case LeaderboardMethod::kGetAchievements:
            {
                _pService->readAchievements( selfId, requestTag );
                break;
            }
            default:
            {
                _pService->readSeasonResult( request._boardId, request._seasonId, selfId, requestTag );
                break;
            }
        }
    }

    void LeaderboardServer::onServiceTick( OnlineServiceHost& host, int64 nowMs )
    {
        (void)nowMs;
        if ( _pService == nullptr )
            return;
        _listCompletionScratch.clear();
        _pService->drainCompletions( _listCompletionScratch );
        for ( LeaderboardCompletion& completion : _listCompletionScratch )
        {
            NetRequestToken token;
            if ( _pendingTable.take( completion._requestTag, token ) == false )
                continue; // 꼬리표 0(안에서 건 일) · 이미 답함
            LeaderboardReply reply;
            reply._result          = completion._result;
            reply._periodId        = completion._periodId;
            reply._score           = completion._score;
            reply._listEntry       = std::move( completion._listEntry );
            reply._listStat        = std::move( completion._listStat );
            reply._listAchievement = std::move( completion._listAchievement );
            BitWriter body;
            LeaderboardProtocol::writeReply( body, reply );
            (void)host.respondOk( token, body ); // 닫힌 연결이면 호스트가 버린다
        }

        _listUnlockScratch.clear();
        _pService->drainAchievementUnlocks( _listUnlockScratch );
        for ( const AchievementUnlock& unlock : _listUnlockScratch )
        {
            BitWriter body;
            LeaderboardProtocol::writeAchievement( body, unlock._state );
            if ( host.sendPush( unlock._accountId, LeaderboardMethod::kPushAchievement, body ) == false && _pPresence != nullptr )
                (void)_pPresence->sendRemotePush( unlock._accountId, LeaderboardMethod::kPushAchievement, body ); // 다른 서버에 붙어 있다
        }
    }
} // namespace sw
