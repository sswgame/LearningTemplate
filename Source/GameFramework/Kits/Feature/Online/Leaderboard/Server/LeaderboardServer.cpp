#include "pch.h"

#include "GameFramework/Kits/Feature/Online/Leaderboard/Server/LeaderboardServer.h"

#include "Core/Serialization/BitStream.h"

#include "GameFramework/Kits/Feature/Online/Leaderboard/Runtime/LeaderboardProtocol.h"
#include "GameFramework/Kits/Feature/Online/OnlineCore/Server/Identity/AccountPresence.h"
#include "GameFramework/Kits/Feature/Online/OnlineCore/Server/Identity/OnlinePushRelay.h"
#include "GameFramework/Runtime/Online/Identity/Server/AccountDirectory.h"

namespace sw
{
    LeaderboardServer::LeaderboardServer()
        : _listCompletionScratch{}
        , _listUnlockScratch{}
        , _pService{ nullptr }
        , _pDirectory{ nullptr }
        , _pPresence{ nullptr }
    {
        setReady( false );
    }

    void LeaderboardServer::initialize( LeaderboardService* pService, const IAccountDirectory* pDirectory, IAccountPresence* pPresence, MetricRegistry* pMetricRegistry )
    {
        _pService   = pService;
        _pDirectory = pDirectory;
        _pPresence  = pPresence;
        initializeCalls( pMetricRegistry, "leaderboard" );
        setReady( pService != nullptr );
    }

    void LeaderboardServer::shutdown()
    {
        _calls.clear();
        _pService   = nullptr;
        _pDirectory = nullptr;
        _pPresence  = nullptr;
        setReady( false );
    }

    void LeaderboardServer::handleCall( OnlineServerCall<LeaderboardMethod::GetTop> call, const OnlineCallContext& context, const LeaderboardTopRequest& request )
    {
        _pService->readTop( request._boardID, request._offset, request._count, context._nowMs, call._callID );
    }

    void LeaderboardServer::handleCall( OnlineServerCall<LeaderboardMethod::GetAround> call, const OnlineCallContext& context, const LeaderboardAroundRequest& request )
    {
        _pService->readAround( request._boardID, context._accountID, request._radius, context._nowMs, call._callID );
    }

    void LeaderboardServer::handleCall( OnlineServerCall<LeaderboardMethod::GetStats> call, const OnlineCallContext& context, const OnlineNoBody& request )
    {
        (void)request;
        _pService->readStats( context._accountID, call._callID );
    }

    void LeaderboardServer::handleCall( OnlineServerCall<LeaderboardMethod::SubmitScore> call, const OnlineCallContext& context, const LeaderboardSubmitRequest& request )
    {
        const LeaderboardDef* pBoard = _pService->findBoard( request._boardID );
        if ( pBoard == nullptr || pBoard->_bClientSubmit == false )
        {
            LeaderboardSubmitReply reply;
            reply._result = pBoard == nullptr ? LeaderboardResult::UnknownBoard : LeaderboardResult::NotAllowed; // 점수는 서버가 낸다. 클라이언트가 낼 수 있는 보드가 아니면 거절한다
            (void)respond( call, reply );
            return;
        }
        AccountIdentity identity;
        if ( _pDirectory == nullptr || _pDirectory->findIdentity( context._accountID, identity ) == false )
            identity._displayName.clear();
        _pService->submitScore( request._boardID, context._accountID, identity._displayName, request._score, context._nowMs, call._callID );
    }

    void LeaderboardServer::handleCall( OnlineServerCall<LeaderboardMethod::GetAchievements> call, const OnlineCallContext& context, const OnlineNoBody& request )
    {
        (void)request;
        _pService->readAchievements( context._accountID, call._callID );
    }

    void LeaderboardServer::handleCall( OnlineServerCall<LeaderboardMethod::GetSeasonResult> call, const OnlineCallContext& context, const LeaderboardSeasonRequest& request )
    {
        _pService->readSeasonResult( request._boardID, request._seasonID, context._accountID, call._callID );
    }

    void LeaderboardServer::handleCall( OnlineServerCall<LeaderboardMethod::GetScoresOf> call, const OnlineCallContext& context, const LeaderboardScoresRequest& request )
    {
        _pService->readScoresOf( request._boardID, request._listAccountID, context._nowMs, call._callID );
    }

    void LeaderboardServer::handleCall( OnlineServerCall<LeaderboardMethod::GetAchievementProgress> call, const OnlineCallContext& context, const OnlineNoBody& request )
    {
        (void)request;
        _pService->readAchievementProgress( context._accountID, call._callID );
    }

    void LeaderboardServer::respondCompletion( LeaderboardCompletion& completion )
    {
        const uint64                    callID = completion._requestTag;
        const LeaderboardServiceResult& result = completion._result;
        switch ( completion._operation )
        {
            case LeaderboardOperation::Submit:
            case LeaderboardOperation::StatChange:
            {
                LeaderboardSubmitReply reply;
                reply._score  = completion._score;
                reply._result = result._result;
                (void)finishCall( callID, result, reply );
                return;
            }
            case LeaderboardOperation::Top:
            case LeaderboardOperation::Around:
            case LeaderboardOperation::ScoresOf:
            {
                LeaderboardEntriesReply reply;
                reply._listEntry = std::move( completion._listEntry );
                reply._periodID  = completion._periodID;
                reply._result    = result._result;
                (void)finishCall( callID, result, reply );
                return;
            }
            case LeaderboardOperation::Stats:
            {
                LeaderboardStatsReply reply;
                reply._listStat = std::move( completion._listStat );
                (void)finishCall( callID, result, reply );
                return;
            }
            case LeaderboardOperation::Achievements:
            {
                LeaderboardAchievementsReply reply;
                reply._listAchievement = std::move( completion._listAchievement );
                (void)finishCall( callID, result, reply );
                return;
            }
            case LeaderboardOperation::AchievementProgress:
            {
                LeaderboardProgressReply reply;
                reply._listProgress = std::move( completion._listProgress );
                (void)finishCall( callID, result, reply );
                return;
            }
            case LeaderboardOperation::SeasonResult:
            {
                LeaderboardSeasonReply reply;
                if ( completion._listEntry.empty() == false )
                    reply._entry = completion._listEntry.front();
                reply._periodID = completion._periodID;
                reply._result   = result._result;
                (void)finishCall( callID, result, reply );
                return;
            }
        }
        SW_ASSERT( false ); // switch 가 모든 작업을 덮는다(-Wswitch)
        (void)failCall( callID, OnlineError::kInternal );
    }

    void LeaderboardServer::onAccountJoined( OnlineServiceHost& host, AccountID accountID )
    {
        (void)host;
        if ( _pService != nullptr )
            _pService->retainStats( accountID ); // 매칭 실력이 메모리에서 읽는다
    }

    void LeaderboardServer::onAccountLeft( OnlineServiceHost& host, AccountID accountID )
    {
        (void)host;
        if ( _pService != nullptr )
            _pService->releaseStats( accountID );
    }

    void LeaderboardServer::onServiceTick( OnlineServiceHost& host, UnixMilliseconds nowMs )
    {
        (void)nowMs;
        if ( _pService == nullptr )
            return;
        _calls.publishPendingCount();
        _listCompletionScratch.clear();
        _pService->takeCompletions( _listCompletionScratch );
        for ( LeaderboardCompletion& completion : _listCompletionScratch )
        {
            respondCompletion( completion ); // 요청 태그가 0이면(내부에서 건 작업) 버린다. 연결이 닫혔으면 호스트가 버린다
        }

        _listUnlockScratch.clear();
        _pService->pollAchievementUnlocks( _listUnlockScratch );
        for ( const AchievementUnlock& unlock : _listUnlockScratch )
        {
            BitWriter body;
            if ( LeaderboardProtocol::MessageTypes::write( body, unlock._state ) == false )
                continue; // write 가 범위 밖 값을 로그에 남겼다. 알림은 다음 조회로 대신한다
            if ( host.sendPush( unlock._accountID, LeaderboardMethod::Achievement::kKind, body ) == false && _pPresence != nullptr )
                (void)_pPresence->sendRemotePush( unlock._accountID, LeaderboardPushName::Achievement, unlock._state ); // 다른 서버에 접속해 있다
        }
    }
} // namespace sw

namespace sw
{
    // 다른 서버에 접속한 계정에게 가는 알림이다. 받는 서버가 이 항목으로 자기 빌드의 번호와 B 를 만든다(OnlinePushRelay.h).
    const OnlinePushRelayEntry kLeaderboardAchievementRelay{ "Leaderboard.Achievement", LeaderboardMethod{}, LeaderboardPushName::Achievement, LeaderboardProtocol::MessageTypes{}, OnlinePushBody<AchievementState>{} };
} // namespace sw
