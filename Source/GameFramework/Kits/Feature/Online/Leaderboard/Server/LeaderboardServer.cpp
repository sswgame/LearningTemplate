#include "pch.h"

#include "GameFramework/Kits/Feature/Online/Leaderboard/Server/LeaderboardServer.h"

#include "Core/Network/BitStream.h"

#include "GameFramework/Base/Online/Identity/AccountDirectory.h"
#include "GameFramework/Base/Online/Identity/AccountPresence.h"
#include "GameFramework/Base/Online/Identity/OnlinePushRelay.h"
#include "GameFramework/Kits/Feature/Online/Leaderboard/Shared/LeaderboardProtocol.h"

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

    const OnlineProtocolEntry& LeaderboardServer::getProtocol() const
    {
        return LeaderboardProtocol::kOnlineEntry;
    }

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
        if ( LeaderboardProtocol::MessageTypes::read( body, request ) != FixedReadResult::Ok )
        {
            (void)host.respondError( context._token, OnlineError::kInvalidRequest );
            return;
        }
        if ( context._method == LeaderboardMethod::kSubmitScore )
        {
            const LeaderboardDefinition* pBoard = _pService->findBoard( request._boardID );
            if ( pBoard == nullptr || pBoard->_bClientSubmit == SW_FALSE )
            {
                LeaderboardReply reply;
                reply._result = pBoard == nullptr ? LeaderboardResult::UnknownBoard : LeaderboardResult::NotAllowed; // 점수는 서버가 낸다. 클라이언트가 낼 수 있는 보드가 아니면 거절한다
                (void)host.respondMessage<LeaderboardProtocol::MessageTypes>( context._token, reply );
                return;
            }
        }
        const uint64    requestTag = _pendingTable.add( context._token );
        const AccountID selfID     = context._accountID;
        switch ( context._method )
        {
            case LeaderboardMethod::kGetTop:
            {
                _pService->readTop( request._boardID, request._offset, request._count, context._nowMs, requestTag );
                break;
            }
            case LeaderboardMethod::kGetAround:
            {
                _pService->readAround( request._boardID, selfID, request._count, context._nowMs, requestTag );
                break;
            }
            case LeaderboardMethod::kGetStats:
            {
                _pService->readStats( selfID, requestTag );
                break;
            }
            case LeaderboardMethod::kSubmitScore:
            {
                AccountIdentity identity;
                if ( _pDirectory == nullptr || _pDirectory->findIdentity( selfID, identity ) == false )
                    identity._displayName.clear();
                _pService->submitScore( request._boardID, selfID, identity._displayName, request._score, context._nowMs, requestTag );
                break;
            }
            case LeaderboardMethod::kGetAchievements:
            {
                _pService->readAchievements( selfID, requestTag );
                break;
            }
            default:
            {
                _pService->readSeasonResult( request._boardID, request._seasonID, selfID, requestTag );
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
                continue; // 요청 태그가 0이거나(내부에서 건 작업) 이미 응답했다
            LeaderboardReply reply;
            reply._result          = completion._result;
            reply._periodID        = completion._periodID;
            reply._score           = completion._score;
            reply._listEntry       = std::move( completion._listEntry );
            reply._listStat        = std::move( completion._listStat );
            reply._listAchievement = std::move( completion._listAchievement );
            (void)host.respondMessage<LeaderboardProtocol::MessageTypes>( token, reply ); // 연결이 닫혔으면 호스트가 버린다
        }

        _listUnlockScratch.clear();
        _pService->drainAchievementUnlocks( _listUnlockScratch );
        for ( const AchievementUnlock& unlock : _listUnlockScratch )
        {
            BitWriter body;
            if ( LeaderboardProtocol::MessageTypes::write( body, unlock._state ) == false )
                continue; // write 가 범위 밖 값을 로그에 남겼다. 알림은 다음 조회로 대신한다
            if ( host.sendPush( unlock._accountID, LeaderboardMethod::kPushAchievement, body ) == false && _pPresence != nullptr )
                (void)_pPresence->sendRemotePush( unlock._accountID, LeaderboardPushName::Achievement, unlock._state ); // 다른 서버에 접속해 있다
        }
    }
} // namespace sw

namespace sw
{
    // 다른 서버에 접속한 계정에게 가는 알림이다. 받는 서버가 이 항목으로 자기 빌드의 번호와 B 를 만든다(OnlinePushRelay.h).
    const OnlinePushRelayEntry kLeaderboardAchievementRelay{ "Leaderboard.Achievement", LeaderboardMethod{}, LeaderboardPushName::Achievement, LeaderboardProtocol::MessageTypes{}, OnlinePushBody<AchievementState>{} };
} // namespace sw
