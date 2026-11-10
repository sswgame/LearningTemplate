#include "pch.h"

#include "Games/Shooter3D/ShooterOnlineFlow.h"

#include "Core/String/StringUtil.h"

#include "Engine/Automation/Watch/AutomationWatch.h"
#include "Engine/Console/DevCommandRegistry.h"

#include "GameFramework/Kits/Feature/Online/Economy/Runtime/API/EconomyClient.h"
#include "GameFramework/Kits/Feature/Online/Leaderboard/Runtime/LeaderboardClient.h"
#include "GameFramework/Kits/Feature/Online/Mailbox/Runtime/MailboxClient.h"
#include "GameFramework/Kits/Feature/Online/Matchmaking/Runtime/MatchmakingClient.h"
#include "GameFramework/Runtime/Foundation/Framework/GameService.h"
#include "GameFramework/Runtime/Online/Service/OnlineSession.h"

namespace sw
{
    namespace
    {
        /** @brief 개발 명령과 감시 값입니다. 세션과 흐름은 게임 범위에서 찾습니다. */
        struct ShooterOnlineFlowInternal
        {
            static constexpr const utf8* kDevLoginName = "shooter_dev"; ///< `Config/Server/Online/Shooter3D.online.json` 의 시드 계정
            static constexpr const utf8* kDevPassword  = "shooter_dev_pw";

            /** @brief 세션 · 흐름이 없을 때의 명령 답입니다. */
            static constexpr const utf8* kNoGameReply = "shooter.online: no Shooter3D game instance";

            /** @brief 게임 범위의 세션과 흐름입니다. 하나라도 없으면 false 이고 부르는 쪽이 `kNoGameReply` 로 답한다. */
            [[nodiscard]] static bool findParts( OnlineSession*& outSession, ShooterOnlineFlow*& outFlow )
            {
                outSession = game::findSubsystem<OnlineSession>();
                outFlow    = game::findSubsystem<ShooterOnlineFlow>();
                return outSession != nullptr && outFlow != nullptr;
            }

            static bool runLogin( const vector<string>& listArgument, string& outReply )
            {
                if ( listArgument.empty() == false )
                    return false;
                OnlineSession*     pSession = nullptr;
                ShooterOnlineFlow* pFlow    = nullptr;
                if ( findParts( pSession, pFlow ) == false )
                {
                    outReply = kNoGameReply;
                    return true;
                }
                pFlow->requestLogin( *pSession, kDevLoginName, kDevPassword );
                outReply = "shooter.online: login sent";
                return true;
            }

            static bool runListMail( const vector<string>& listArgument, string& outReply )
            {
                if ( listArgument.empty() == false )
                    return false;
                OnlineSession*     pSession = nullptr;
                ShooterOnlineFlow* pFlow    = nullptr;
                if ( findParts( pSession, pFlow ) == false )
                {
                    outReply = kNoGameReply;
                    return true;
                }
                pFlow->requestMailList( *pSession );
                outReply = "shooter.online: mail list sent";
                return true;
            }

            static bool runClaimAll( const vector<string>& listArgument, string& outReply )
            {
                if ( listArgument.empty() == false )
                    return false;
                OnlineSession*     pSession = nullptr;
                ShooterOnlineFlow* pFlow    = nullptr;
                if ( findParts( pSession, pFlow ) == false )
                {
                    outReply = kNoGameReply;
                    return true;
                }
                pFlow->requestClaimAll( *pSession );
                outReply = "shooter.online: claim sent";
                return true;
            }

            static bool runMatch( const vector<string>& listArgument, string& outReply )
            {
                if ( listArgument.empty() == false )
                    return false;
                OnlineSession*     pSession = nullptr;
                ShooterOnlineFlow* pFlow    = nullptr;
                if ( findParts( pSession, pFlow ) == false )
                {
                    outReply = kNoGameReply;
                    return true;
                }
                pFlow->requestMatch( *pSession );
                outReply = "shooter.online: queue join sent";
                return true;
            }

            static bool runSubmitKills( const vector<string>& listArgument, string& outReply )
            {
                int32 kills = 0;
                if ( listArgument.size() != 1 || StringUtil::parseInt( listArgument[0], kills ) == false || kills < 0 )
                    return false;
                OnlineSession*     pSession = nullptr;
                ShooterOnlineFlow* pFlow    = nullptr;
                if ( findParts( pSession, pFlow ) == false )
                {
                    outReply = kNoGameReply;
                    return true;
                }
                pFlow->requestSubmitKills( *pSession, kills );
                outReply = "shooter.online: score sent";
                return true;
            }

            static bool runTopKills( const vector<string>& listArgument, string& outReply )
            {
                if ( listArgument.empty() == false )
                    return false;
                OnlineSession*     pSession = nullptr;
                ShooterOnlineFlow* pFlow    = nullptr;
                if ( findParts( pSession, pFlow ) == false )
                {
                    outReply = kNoGameReply;
                    return true;
                }
                pFlow->requestTopKills( *pSession );
                outReply = "shooter.online: top scores sent";
                return true;
            }

            [[nodiscard]] static bool readMatchFound( const GameObjectManager* pManager, float64& outValue )
            {
                (void)pManager;
                const ShooterOnlineFlow* const pFlow = game::findSubsystem<ShooterOnlineFlow>();
                if ( pFlow == nullptr )
                    return false;
                outValue = pFlow->getMatchID() != 0 ? 1.0 : 0.0;
                return true;
            }

            [[nodiscard]] static bool readTopKillsScore( const GameObjectManager* pManager, float64& outValue )
            {
                (void)pManager;
                const ShooterOnlineFlow* const pFlow = game::findSubsystem<ShooterOnlineFlow>();
                if ( pFlow == nullptr || pFlow->getTopKills().empty() )
                    return false;
                outValue = static_cast<float64>( pFlow->getTopKills().front()._score );
                return true;
            }

            [[nodiscard]] static bool readSubmittedCount( const GameObjectManager* pManager, float64& outValue )
            {
                (void)pManager;
                const ShooterOnlineFlow* const pFlow = game::findSubsystem<ShooterOnlineFlow>();
                if ( pFlow == nullptr )
                    return false;
                outValue = static_cast<float64>( pFlow->getSubmittedScoreCount() );
                return true;
            }

            [[nodiscard]] static bool readTopKillsCount( const GameObjectManager* pManager, float64& outValue )
            {
                (void)pManager;
                const ShooterOnlineFlow* const pFlow = game::findSubsystem<ShooterOnlineFlow>();
                if ( pFlow == nullptr )
                    return false;
                outValue = static_cast<float64>( pFlow->getTopKills().size() );
                return true;
            }

            static void onQueueJoined( const OnlineResult<MatchmakingTicketReply>& result )
            {
                if ( result.isOk() )
                    SW_LOG_INFO_ID( "Shooter.OnlineQueued", "[Shooter] online queue joined (ticket %#)", result._reply._ticketID );
                else
                    SW_LOG_WARNING_ID( "Shooter.OnlineQueueFailed", "[Shooter] online queue join failed (error %#, result %#)", result._errorCode, static_cast<int32>( result._reply._result ) );
            }

            [[nodiscard]] static bool readLoggedIn( const GameObjectManager* pManager, float64& outValue )
            {
                (void)pManager;
                const OnlineSession* const pSession = game::findSubsystem<OnlineSession>();
                if ( pSession == nullptr )
                    return false;
                outValue = pSession->get<AccountClient>().isLoggedIn() ? 1.0 : 0.0;
                return true;
            }

            [[nodiscard]] static bool readMailCount( const GameObjectManager* pManager, float64& outValue )
            {
                (void)pManager;
                const OnlineSession* const pSession = game::findSubsystem<OnlineSession>();
                if ( pSession == nullptr )
                    return false;
                outValue = static_cast<float64>( pSession->get<MailboxClient>().getMails().size() );
                return true;
            }

            [[nodiscard]] static bool readGold( const GameObjectManager* pManager, float64& outValue )
            {
                (void)pManager;
                const OnlineSession* const pSession = game::findSubsystem<OnlineSession>();
                if ( pSession == nullptr )
                    return false;
                outValue = static_cast<float64>( pSession->get<EconomyClient>().getBalance( ShooterOnlineFlow::kAssetGold ) );
                return true;
            }

            [[nodiscard]] static bool readSelfMailSent( const GameObjectManager* pManager, float64& outValue )
            {
                (void)pManager;
                const ShooterOnlineFlow* const pFlow = game::findSubsystem<ShooterOnlineFlow>();
                if ( pFlow == nullptr )
                    return false;
                outValue = static_cast<float64>( pFlow->getSelfMailSentCount() );
                return true;
            }

            static void onMailList( const OnlineResult<MailboxListReply>& result )
            {
                if ( result.isOk() )
                    SW_LOG_INFO_ID( "Shooter.OnlineMailList", "[Shooter] online mail list: %# mails", result._reply._listMail.size() );
                else
                    SW_LOG_WARNING_ID( "Shooter.OnlineMailListFailed", "[Shooter] online mail list failed (error %#)", result._errorCode );
            }
        };
    } // namespace
} // namespace sw

namespace sw
{
    ShooterOnlineFlow::ShooterOnlineFlow()
        : _listReplyScratch{}
        , _listAssignmentScratch{}
        , _listTopKills{}
        , _matchLease{}
        , _matchID{ 0 }
        , _submittedScoreCount{ 0 }
        , _selfMailSentCount{ 0 }
    {
    }

    void ShooterOnlineFlow::update( OnlineSession& session )
    {
        AccountClient* const pAccount = session.find<AccountClient>();
        if ( pAccount == nullptr )
            return;
        _listReplyScratch.clear();
        (void)pAccount->pollReplies( _listReplyScratch );
        for ( const AccountClientReply& reply : _listReplyScratch )
        {
            if ( reply._errorCode != OnlineError::kOk || reply._result != LoginResult::Ok )
                SW_LOG_WARNING_ID( "Shooter.OnlineAccountFailed", "[Shooter] online account call failed (error %#, result %#)", reply._errorCode, static_cast<int32>( reply._result._result ) );
            else if ( reply._operation == AccountClientOperation::Login )
                SW_LOG_INFO_ID( "Shooter.OnlineLoggedIn", "[Shooter] online login done" );
        }
        pollMatches( session );
    }

    void ShooterOnlineFlow::pollMatches( OnlineSession& session )
    {
        MatchmakingClient* const pMatchmaking = session.find<MatchmakingClient>();
        if ( pMatchmaking == nullptr || pMatchmaking->claimNotifications( _matchLease ) == false )
            return;
        _listAssignmentScratch.clear();
        pMatchmaking->pollMatchResults( _matchLease, _listAssignmentScratch );
        for ( const MatchAssignment& assignment : _listAssignmentScratch )
        {
            if ( assignment._result != MatchQueueResult::Found )
            {
                SW_LOG_WARNING_ID( "Shooter.OnlineMatchNotFound", "[Shooter] online match not found (result %#)", static_cast<int32>( assignment._result ) );
                continue;
            }
            // 배정된 게임 서버에 붙는 일(접속 티켓 → UDP)은 두 프로세스 접속(`NetPairRun`)이 본다. 여기서는 경기를 받아 둔다.
            _matchID = assignment._matchID;
            SW_LOG_INFO_ID( "Shooter.OnlineMatchFound", "[Shooter] online match found (match %#, mode %#, server %#:%#)", assignment._matchID, assignment._modeID.c_str(), assignment._address.c_str(),
                            assignment._port );
        }
    }

    void ShooterOnlineFlow::requestMatch( OnlineSession& session )
    {
        MatchmakingModeRequest request;
        request._modeID = kMatchMode;
        request._region = kMatchRegion;
        (void)session.get<MatchmakingClient>().call<MatchmakingMethod::QueueJoin>( request, OnlineReplyDelegate<MatchmakingMethod::QueueJoin>::create<&ShooterOnlineFlowInternal::onQueueJoined>() );
    }

    void ShooterOnlineFlow::requestSubmitKills( OnlineSession& session, int64 kills )
    {
        LeaderboardSubmitRequest request;
        request._boardID = kBoardKills;
        request._score   = kills;
        (void)session.get<LeaderboardClient>().call<LeaderboardMethod::SubmitScore>( request, OnlineReplyDelegate<LeaderboardMethod::SubmitScore>::create<&ShooterOnlineFlow::onScoreSubmitted>( this ) );
    }

    void ShooterOnlineFlow::onScoreSubmitted( const OnlineResult<LeaderboardSubmitReply>& result )
    {
        if ( result.isOk() == false )
        {
            SW_LOG_WARNING_ID( "Shooter.OnlineScoreFailed", "[Shooter] online score submit failed (error %#, result %#)", result._errorCode, static_cast<int32>( result._reply._result ) );
            return;
        }
        ++_submittedScoreCount;
        SW_LOG_INFO_ID( "Shooter.OnlineScoreSubmitted", "[Shooter] online score submitted - best %#", result._reply._score );
    }

    void ShooterOnlineFlow::requestTopKills( OnlineSession& session )
    {
        LeaderboardTopRequest request;
        request._boardID = kBoardKills;
        request._offset  = 0;
        request._count   = kTopCount;
        (void)session.get<LeaderboardClient>().call<LeaderboardMethod::GetTop>( request, OnlineReplyDelegate<LeaderboardMethod::GetTop>::create<&ShooterOnlineFlow::onTopKills>( this ) );
    }

    void ShooterOnlineFlow::onTopKills( const OnlineResult<LeaderboardEntriesReply>& result )
    {
        if ( result.isOk() == false )
        {
            SW_LOG_WARNING_ID( "Shooter.OnlineTopFailed", "[Shooter] online top scores failed (error %#, result %#)", result._errorCode, static_cast<int32>( result._reply._result ) );
            return;
        }
        _listTopKills = result._reply._listEntry;
        SW_LOG_INFO_ID( "Shooter.OnlineTopScores", "[Shooter] online top scores: %# entries", static_cast<uint32>( _listTopKills.size() ) );
    }

    void ShooterOnlineFlow::requestLogin( OnlineSession& session, string_view loginName, string_view password )
    {
        (void)session.get<AccountClient>().login( loginName, password );
    }

    void ShooterOnlineFlow::requestMailList( OnlineSession& session )
    {
        (void)session.get<MailboxClient>().call<MailboxMethod::List>( MailboxListRequest{}, OnlineReplyDelegate<MailboxMethod::List>::create<&ShooterOnlineFlowInternal::onMailList>() );
    }

    void ShooterOnlineFlow::requestClaimAll( OnlineSession& session )
    {
        (void)session.get<MailboxClient>().call<MailboxMethod::ClaimAll>( OnlineReplyDelegate<MailboxMethod::ClaimAll>::create<&ShooterOnlineFlow::onClaimAll>( this ) );
    }

    void ShooterOnlineFlow::onClaimAll( const OnlineResult<MailboxClaimReply>& result )
    {
        if ( result.isOk() == false )
        {
            SW_LOG_WARNING_ID( "Shooter.OnlineClaimFailed", "[Shooter] online mail claim failed (error %#, result %#)", result._errorCode, static_cast<int32>( result._reply._result ) );
            return;
        }
    }
} // namespace sw

namespace sw
{
    SW_DEV_COMMAND( ShooterOnlineLogin, "shooter.online.login", "shooter.online.login", "Log the online session in with the development account of Config/Server/Online/Shooter3D.online.json", &ShooterOnlineFlowInternal::runLogin );
    SW_DEV_COMMAND( ShooterOnlineListMail, "shooter.online.listmail", "shooter.online.listmail", "Ask the mailbox kit for the first mail page", &ShooterOnlineFlowInternal::runListMail );
    SW_DEV_COMMAND( ShooterOnlineClaimAll, "shooter.online.claimall", "shooter.online.claimall", "Claim all mail and hand the balances to the economy client", &ShooterOnlineFlowInternal::runClaimAll );
    SW_DEV_COMMAND( ShooterOnlineMatch, "shooter.online.match", "shooter.online.match", "Join the matchmaking queue of the solo arena mode (shooter3d.matchmodes.xml)", &ShooterOnlineFlowInternal::runMatch );
    SW_DEV_COMMAND( ShooterOnlineSubmitKills, "shooter.online.submitkills", "shooter.online.submitkills <kills>", "Submit one round's kills to the arena_kills leaderboard", &ShooterOnlineFlowInternal::runSubmitKills );
    SW_DEV_COMMAND( ShooterOnlineTopKills, "shooter.online.topkills", "shooter.online.topkills", "Ask the arena_kills leaderboard for its top entries", &ShooterOnlineFlowInternal::runTopKills );

    SW_AUTOMATION_WATCH( shooterOnlineLoggedIn, "Shooter3D.OnlineLoggedIn", "1 while the account kit client is logged in", &ShooterOnlineFlowInternal::readLoggedIn );
    SW_AUTOMATION_WATCH( shooterOnlineMailCount, "Shooter3D.OnlineMailCount", "Mails in the mailbox kit client's first page cache", &ShooterOnlineFlowInternal::readMailCount );
    SW_AUTOMATION_WATCH( shooterOnlineGold, "Shooter3D.OnlineGold", "Gold balance in the economy kit client's cache", &ShooterOnlineFlowInternal::readGold );
    SW_AUTOMATION_WATCH( shooterOnlineMatchFound, "Shooter3D.OnlineMatchFound", "1 once the matchmaking kit assigned this player a match", &ShooterOnlineFlowInternal::readMatchFound );
    SW_AUTOMATION_WATCH( shooterOnlineTopKillsScore, "Shooter3D.OnlineTopKillsScore", "Best score in the last arena_kills top list", &ShooterOnlineFlowInternal::readTopKillsScore );
    SW_AUTOMATION_WATCH( shooterOnlineSubmittedScoreCount, "Shooter3D.OnlineSubmittedScoreCount", "Leaderboard score submits the server has applied", &ShooterOnlineFlowInternal::readSubmittedCount );
    SW_AUTOMATION_WATCH( shooterOnlineTopKillsCount, "Shooter3D.OnlineTopKillsCount", "Entries in the last arena_kills top list", &ShooterOnlineFlowInternal::readTopKillsCount );
    SW_AUTOMATION_WATCH( shooterOnlineSelfMailSent, "Shooter3D.OnlineSelfMailSent", "Development mails the in-process server's store has written", &ShooterOnlineFlowInternal::readSelfMailSent );
} // namespace sw
