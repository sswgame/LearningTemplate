#include "pch.h"

#include "Games/Playground/Online/PlaygroundOnlineFlow.h"

#include "Core/Network/Connection/NetEndpointSecurity.h"
#include "Core/String/StringUtil.h"

#include "Engine/Asset/Cache/SharedAssetRef.h"
#include "Engine/Automation/Watch/AutomationWatch.h"
#include "Engine/Console/DevCommandRegistry.h"
#include "Engine/Net/GameNetSession.h"
#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManager.h"
#include "Engine/Scene/SceneManager.h"

#include "GameFramework/Kits/Feature/Online/Economy/Runtime/EconomyClient.h"
#include "GameFramework/Kits/Feature/Online/Economy/Runtime/EconomyShopPurchaseProvider.h"
#include "GameFramework/Kits/Feature/Online/Economy/Runtime/OfferCatalog.h"
#include "GameFramework/Kits/Feature/Online/Leaderboard/Runtime/LeaderboardClient.h"
#include "GameFramework/Kits/Feature/Online/LiveOps/Runtime/LiveOpsClient.h"
#include "GameFramework/Kits/Feature/Online/Mailbox/Runtime/MailboxClient.h"
#include "GameFramework/Kits/Feature/Online/Matchmaking/Runtime/MatchmakingClient.h"
#include "GameFramework/Kits/Feature/Online/ServerDirectory/Runtime/ServerDirectoryClient.h"
#include "GameFramework/Runtime/Character/Control/ControlSystem.h"
#include "GameFramework/Runtime/Character/Control/Pawn/PawnComponent.h"
#include "GameFramework/Runtime/Foundation/Framework/GameService.h"
#include "GameFramework/Runtime/Foundation/Framework/Minigame/MinigameFlow.h"
#include "GameFramework/Runtime/Online/Service/OnlineSession.h"
#include "GameFramework/Runtime/Online/Store/ServiceKeyUtil.h"

namespace sw
{
    namespace
    {
        /** @brief 개발 명령과 감시 값입니다. 세션과 흐름은 게임 범위에서 찾습니다. */
        struct PlaygroundOnlineFlowInternal
        {
            static constexpr const utf8* kDevLoginName = "playground_dev"; ///< `Config/Server/Online/Playground.online.json` 의 시드 계정
            static constexpr const utf8* kDevPassword  = "playground_dev_pw";

            /** @brief 세션 · 흐름이 없을 때의 명령 답입니다. */
            static constexpr const utf8* kNoGameReply = "playground.online: no Playground game instance";

            /** @brief 게임 범위의 세션과 흐름입니다. 하나라도 없으면 false 이고 부르는 쪽이 `kNoGameReply` 로 답한다. */
            [[nodiscard]] static bool findParts( OnlineSession*& outSession, PlaygroundOnlineFlow*& outFlow )
            {
                outSession = game::findSubsystem<OnlineSession>();
                outFlow    = game::findSubsystem<PlaygroundOnlineFlow>();
                return outSession != nullptr && outFlow != nullptr;
            }

            static bool runLogin( const vector<string>& listArgument, string& outReply )
            {
                if ( listArgument.empty() == false )
                    return false;
                OnlineSession*        pSession = nullptr;
                PlaygroundOnlineFlow* pFlow    = nullptr;
                if ( findParts( pSession, pFlow ) == false )
                {
                    outReply = kNoGameReply;
                    return true;
                }
                pFlow->requestLogin( *pSession, kDevLoginName, kDevPassword );
                outReply = "playground.online: login sent";
                return true;
            }

            static bool runListMail( const vector<string>& listArgument, string& outReply )
            {
                if ( listArgument.empty() == false )
                    return false;
                OnlineSession*        pSession = nullptr;
                PlaygroundOnlineFlow* pFlow    = nullptr;
                if ( findParts( pSession, pFlow ) == false )
                {
                    outReply = kNoGameReply;
                    return true;
                }
                pFlow->requestMailList( *pSession );
                outReply = "playground.online: mail list sent";
                return true;
            }

            static bool runClaimAll( const vector<string>& listArgument, string& outReply )
            {
                if ( listArgument.empty() == false )
                    return false;
                OnlineSession*        pSession = nullptr;
                PlaygroundOnlineFlow* pFlow    = nullptr;
                if ( findParts( pSession, pFlow ) == false )
                {
                    outReply = kNoGameReply;
                    return true;
                }
                pFlow->requestClaimAll( *pSession );
                outReply = "playground.online: claim sent";
                return true;
            }

            static bool runSubmitKills( const vector<string>& listArgument, string& outReply )
            {
                int32 kills = 0;
                if ( listArgument.size() != 1 || StringUtil::parseInt( listArgument[0], kills ) == false || kills < 0 )
                    return false;
                OnlineSession*        pSession = nullptr;
                PlaygroundOnlineFlow* pFlow    = nullptr;
                if ( findParts( pSession, pFlow ) == false )
                {
                    outReply = kNoGameReply;
                    return true;
                }
                pFlow->requestSubmitKills( *pSession, kills );
                outReply = "playground.online: score sent";
                return true;
            }

            static bool runTopKills( const vector<string>& listArgument, string& outReply )
            {
                if ( listArgument.empty() == false )
                    return false;
                OnlineSession*        pSession = nullptr;
                PlaygroundOnlineFlow* pFlow    = nullptr;
                if ( findParts( pSession, pFlow ) == false )
                {
                    outReply = kNoGameReply;
                    return true;
                }
                pFlow->requestTopKills( *pSession );
                outReply = "playground.online: top scores sent";
                return true;
            }

            static bool runLiveEvents( const vector<string>& listArgument, string& outReply )
            {
                if ( listArgument.empty() == false )
                    return false;
                OnlineSession*        pSession = nullptr;
                PlaygroundOnlineFlow* pFlow    = nullptr;
                if ( findParts( pSession, pFlow ) == false )
                {
                    outReply = kNoGameReply;
                    return true;
                }
                PlaygroundOnlineFlow::requestLiveEvents( *pSession );
                outReply = "playground.online: live events sent";
                return true;
            }

            static bool runServiceStatus( const vector<string>& listArgument, string& outReply )
            {
                if ( listArgument.empty() == false )
                    return false;
                OnlineSession*        pSession = nullptr;
                PlaygroundOnlineFlow* pFlow    = nullptr;
                if ( findParts( pSession, pFlow ) == false )
                {
                    outReply = kNoGameReply;
                    return true;
                }
                PlaygroundOnlineFlow::requestServiceStatus( *pSession );
                outReply = "playground.online: service status sent";
                return true;
            }

            static void onLiveEvents( const OnlineResult<LiveOpsStateReply>& result )
            {
                if ( result.isOk() )
                    SW_LOG_INFO_ID( "Playground.OnlineLiveEvents", "[Playground] online live events: %# events", static_cast<uint32>( result._reply._listEvent.size() ) );
                else
                    SW_LOG_WARNING_ID( "Playground.OnlineLiveEventsFailed", "[Playground] online live events failed (error %#)", result._errorCode );
            }

            static void onServiceStatus( const OnlineResult<ServerDirectoryStatus>& result )
            {
                if ( result.isOk() )
                    SW_LOG_INFO_ID( "Playground.OnlineServiceStatus", "[Playground] online service status: %# announcements, %# maintenance windows", static_cast<uint32>( result._reply._listAnnouncement.size() ),
                                    static_cast<uint32>( result._reply._listMaintenance.size() ) );
                else
                    SW_LOG_WARNING_ID( "Playground.OnlineServiceStatusFailed", "[Playground] online service status failed (error %#)", result._errorCode );
            }

            [[nodiscard]] static bool readMatchFound( const GameObjectManager* pManager, float64& outValue )
            {
                (void)pManager;
                const PlaygroundOnlineFlow* const pFlow = game::findSubsystem<PlaygroundOnlineFlow>();
                if ( pFlow == nullptr )
                    return false;
                outValue = pFlow->getMatchID() != 0 ? 1.0 : 0.0;
                return true;
            }

            [[nodiscard]] static bool readTopKillsScore( const GameObjectManager* pManager, float64& outValue )
            {
                (void)pManager;
                const PlaygroundOnlineFlow* const pFlow = game::findSubsystem<PlaygroundOnlineFlow>();
                if ( pFlow == nullptr || pFlow->getTopKills().empty() )
                    return false;
                outValue = static_cast<float64>( pFlow->getTopKills().front()._score );
                return true;
            }

            [[nodiscard]] static bool readSubmittedCount( const GameObjectManager* pManager, float64& outValue )
            {
                (void)pManager;
                const PlaygroundOnlineFlow* const pFlow = game::findSubsystem<PlaygroundOnlineFlow>();
                if ( pFlow == nullptr )
                    return false;
                outValue = static_cast<float64>( pFlow->getSubmittedScoreCount() );
                return true;
            }

            [[nodiscard]] static bool readTopKillsCount( const GameObjectManager* pManager, float64& outValue )
            {
                (void)pManager;
                const PlaygroundOnlineFlow* const pFlow = game::findSubsystem<PlaygroundOnlineFlow>();
                if ( pFlow == nullptr )
                    return false;
                outValue = static_cast<float64>( pFlow->getTopKills().size() );
                return true;
            }

            [[nodiscard]] static bool readMatchState( const GameObjectManager* pManager, float64& outValue )
            {
                (void)pManager;
                const PlaygroundOnlineFlow* const pFlow = game::findSubsystem<PlaygroundOnlineFlow>();
                if ( pFlow == nullptr )
                    return false;
                outValue = static_cast<float64>( pFlow->getMatchState() );
                return true;
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
                outValue = static_cast<float64>( pSession->get<EconomyClient>().getBalance( PlaygroundOnlineFlow::kAssetGold ) );
                return true;
            }

            [[nodiscard]] static bool readGem( const GameObjectManager* pManager, float64& outValue )
            {
                (void)pManager;
                const OnlineSession* const pSession = game::findSubsystem<OnlineSession>();
                if ( pSession == nullptr )
                    return false;
                outValue = static_cast<float64>( pSession->get<EconomyClient>().getBalance( PlaygroundOnlineFlow::kAssetGem ) );
                return true;
            }

            [[nodiscard]] static bool readSelfMailSent( const GameObjectManager* pManager, float64& outValue )
            {
                (void)pManager;
                const PlaygroundOnlineFlow* const pFlow = game::findSubsystem<PlaygroundOnlineFlow>();
                if ( pFlow == nullptr )
                    return false;
                outValue = static_cast<float64>( pFlow->getSelfMailSentCount() );
                return true;
            }

            static void onMailList( const OnlineResult<MailboxListReply>& result )
            {
                if ( result.isOk() )
                    SW_LOG_INFO_ID( "Playground.OnlineMailList", "[Playground] online mail list: %# mails", result._reply._listMail.size() );
                else
                    SW_LOG_WARNING_ID( "Playground.OnlineMailListFailed", "[Playground] online mail list failed (error %#)", result._errorCode );
            }
        };
    } // namespace
} // namespace sw

namespace sw
{
    PlaygroundOnlineFlow::PlaygroundOnlineFlow()
        : _listReplyScratch{}
        , _listAssignmentScratch{}
        , _listTopKills{}
        , _offers{}
        , _shopProvider{}
        , _matchLease{}
        , _finishedSubscription{}
        , _assignment{}
        , _matchMinigame{}
        , _hubAddress{}
        , _submittedScoreCount{ 0 }
        , _selfMailSentCount{ 0 }
        , _matchState{ PlaygroundMatchState::None }
        , _bHubTicket{ SW_FALSE }
    {
    }

    PlaygroundOnlineFlow::~PlaygroundOnlineFlow() = default;

    void PlaygroundOnlineFlow::initialize( MinigameFlow& minigames )
    {
        _finishedSubscription = minigames.onFinished().add( SW_DELEGATE_METHOD( Delegate<void( const MinigameResult& )>, &PlaygroundOnlineFlow::onMinigameFinished, this ) );
    }

    bool PlaygroundOnlineFlow::isLocalPlayer( const GameObject& activator ) const
    {
        const PawnComponent* const     pPawn    = activator.getComponent<PawnComponent>();
        const GameObjectManager* const pManager = activator.getManager();
        return pPawn != nullptr && pManager != nullptr && ControlSystem::isLocallyControlled( *pManager, *pPawn );
    }

    bool PlaygroundOnlineFlow::requestMatch( const MinigameDefinition& definition )
    {
        OnlineSession* const       pSession = game::findSubsystem<OnlineSession>();
        const AccountClient* const pAccount = pSession != nullptr ? pSession->find<AccountClient>() : nullptr;
        if ( pAccount == nullptr || pAccount->isLoggedIn() == false )
        {
            SW_LOG_WARNING_ID( "Playground.MatchNeedsLogin", "[Playground] match '%#' needs an online login - the player stays in the hub", definition._name.c_str() );
            return false;
        }
        if ( _matchState != PlaygroundMatchState::None )
        {
            SW_LOG_WARNING_ID( "Playground.MatchBusy", "[Playground] match '%#' is refused - another match is queued or running (step %#)", definition._name.c_str(), static_cast<int32>( _matchState ) );
            return false;
        }
        const GameNetSession* const pNetSession = game::findSubsystem<GameNetSession>();
        const bool                  bTicketHub  = pNetSession != nullptr && pNetSession->getRole() == GameNetRole::Client && pNetSession->isTicketBound();
        if ( bTicketHub && pSession->getHubServerID().empty() )
        {
            // 돌아올 길 없이 허브를 떠나지 않는다 — 티켓은 되쓰지 않으므로 허브 서버 이름이 있어야 새 티켓을 청한다.
            SW_LOG_ERROR_ID( "Playground.MatchNoHubServerID", "[Playground] match '%#' is refused - GameSettings onlineHubServerID is empty, the client could not return to the hub server", definition._name.c_str() );
            return false;
        }
        MatchmakingModeRequest request;
        request._modeID = definition._matchMode;
        request._region = kMatchRegion;
        _matchMinigame  = definition._name;
        _matchState     = PlaygroundMatchState::Queued;
        (void)pSession->get<MatchmakingClient>().call<MatchmakingMethod::QueueJoin>( request, SW_DELEGATE_METHOD( OnlineReplyDelegate<MatchmakingMethod::QueueJoin>, &PlaygroundOnlineFlow::onQueueJoined, this ) );
        return true;
    }

    void PlaygroundOnlineFlow::onQueueJoined( const OnlineResult<MatchmakingTicketReply>& result )
    {
        if ( result.isOk() )
        {
            SW_LOG_INFO_ID( "Playground.OnlineQueued", "[Playground] online queue joined (ticket %#)", result._reply._ticketID );
            return;
        }
        SW_LOG_WARNING_ID( "Playground.OnlineQueueFailed", "[Playground] online queue join failed (error %#, result %#)", result._errorCode, static_cast<int32>( result._reply._result ) );
        if ( _matchState == PlaygroundMatchState::Queued )
            clearMatch();
    }

    EconomyShopPurchaseProvider* PlaygroundOnlineFlow::findShopProvider( OnlineSession& session )
    {
        if ( _shopProvider != nullptr )
            return _shopProvider.get();
        _offers = SharedAssetCache<OfferCatalog>::acquire( kOfferCatalog ).snapshot();
        if ( _offers == nullptr )
        {
            SW_LOG_ERROR_ID( "Playground.OnlineShopNoOffers", "[Playground] the shop offer catalog '%#' could not be read", kOfferCatalog );
            return nullptr;
        }
        _shopProvider = make_unique<EconomyShopPurchaseProvider>( session.get<EconomyClient>(), *_offers );
        return _shopProvider.get();
    }

    void PlaygroundOnlineFlow::update( OnlineSession& session )
    {
        AccountClient* const pAccount = session.find<AccountClient>();
        if ( pAccount == nullptr )
            return;
        _listReplyScratch.clear();
        (void)pAccount->pollReplies( _listReplyScratch );
        for ( const AccountClientReply& reply : _listReplyScratch )
        {
            if ( reply._errorCode != OnlineError::kOk || reply._result != LoginResult::Ok )
            {
                SW_LOG_WARNING_ID( "Playground.OnlineAccountFailed", "[Playground] online account call failed (error %#, result %#)", reply._errorCode, static_cast<int32>( reply._result._result ) );
                if ( reply._operation == AccountMethodName::IssueGameTicket )
                    onGameTicketRefused();
            }
            else if ( reply._operation == AccountMethodName::Login )
                SW_LOG_INFO_ID( "Playground.OnlineLoggedIn", "[Playground] online login done" );
            else if ( reply._operation == AccountMethodName::IssueGameTicket )
                onGameTicket( session, reply._ticket );
        }
        pollMatches( session );
        updateMatchExit( session );
    }

    void PlaygroundOnlineFlow::pollMatches( OnlineSession& session )
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
                SW_LOG_WARNING_ID( "Playground.OnlineMatchNotFound", "[Playground] online match not found (result %#)", static_cast<int32>( assignment._result ) );
                if ( _matchState == PlaygroundMatchState::Queued )
                    clearMatch();
                continue;
            }
            SW_LOG_INFO_ID( "Playground.OnlineMatchFound", "[Playground] online match found (match %#, mode %#, server %#:%#)", assignment._matchID, assignment._modeID.c_str(),
                            assignment._address.c_str(), assignment._port );
            if ( _matchState != PlaygroundMatchState::Queued )
                continue; // 이 흐름이 서지 않은 줄(다시 연결 뒤 늦게 온 배정)이다 — 옮기지 않는다
            _assignment = assignment;
            beginMatch( session );
        }
    }

    void PlaygroundOnlineFlow::beginMatch( OnlineSession& session )
    {
        const GameNetSession& netSession = game::getSubsystem<GameNetSession>();
        if ( netSession.getRole() != GameNetRole::Client )
        {
            _hubAddress = NetAddress{};
            enterMatch( session ); // 혼자 하는 허브 — 옮길 세션이 없다
            return;
        }
        _hubAddress = netSession.getServerAddress();
        _bHubTicket = netSession.isTicketBound() ? SW_TRUE : SW_FALSE;
        _matchState = PlaygroundMatchState::MatchTicket;
        (void)session.get<AccountClient>().issueGameTicket( ServiceKeyUtil::makeHex64( _assignment._serverID ) ); // 답은 `update` 가 받는다
    }

    void PlaygroundOnlineFlow::onGameTicket( OnlineSession& session, const NetGameTicket& ticket )
    {
        const bool bMatch = _matchState == PlaygroundMatchState::MatchTicket;
        if ( bMatch == false && _matchState != PlaygroundMatchState::HubTicket )
            return; // 매치와 상관없는 티켓(개발 명령)이다
        NetAddress address = _hubAddress;
        if ( bMatch && NetAddress::parse( _assignment._address, _assignment._port, address ) == false )
        {
            SW_LOG_ERROR_ID( "Playground.MatchAddressInvalid", "[Playground] match %# names the server '%#:%#', which is not an address - the player stays in the hub", _assignment._matchID,
                             _assignment._address.c_str(), _assignment._port );
            clearMatch();
            return;
        }
        GameNetSession&       netSession   = game::getSubsystem<GameNetSession>();
        SceneManager&         sceneManager = game::getSubsystem<SceneManager>();
        NetConnectCredentials credentials;
        GameNetSessionParams  params;
        AccountClient::makeConnectCredentials( ticket, credentials );
        // 세션을 여는 것은 앞 세션(허브 · 매치)을 닫는 일이다 — 앞 세션의 링크를 먼저 떼어 씬의 컴포넌트가 닫힌 세션을 보지 않게 한다.
        sceneManager.setNetLink( NetLinkValue{} );
        const bool bOpened = netSession.makeTicketClientParams( address, credentials, params ) && netSession.initialize( params );
        if ( bOpened )
            sceneManager.setNetLink( netSession.makeNetLink() );
        else
            SW_LOG_ERROR_ID( "Playground.SessionNotOpened", "[Playground] the session with %# could not be opened (see the messages above)", address.toString() );
        if ( bMatch == false )
        {
            clearMatch(); // 허브로 돌아왔다(열지 못했으면 혼자 하는 허브로 남는다 — 위에서 알렸다)
            return;
        }
        if ( bOpened )
            enterMatch( session );
        else
            reconnectHub( session );
    }

    void PlaygroundOnlineFlow::onGameTicketRefused()
    {
        if ( _matchState == PlaygroundMatchState::MatchTicket )
            clearMatch(); // 아직 허브를 떠나지 않았다
        else if ( _matchState == PlaygroundMatchState::HubTicket )
        {
            SW_LOG_ERROR_ID( "Playground.HubTicketRefused", "[Playground] the hub server ticket was refused - the player stays in an offline hub" );
            clearMatch();
        }
    }

    void PlaygroundOnlineFlow::enterMatch( OnlineSession& session )
    {
        _matchState = PlaygroundMatchState::Playing;
        if ( game::getSubsystem<MinigameFlow>().enterMatch( _matchMinigame.view(), _assignment._matchID ) )
            return;
        SW_LOG_ERROR_ID( "Playground.MatchNotEntered", "[Playground] match %# could not be entered (see the messages above) - returning to the hub session", _assignment._matchID );
        reconnectHub( session );
    }

    void PlaygroundOnlineFlow::updateMatchExit( OnlineSession& session )
    {
        const MinigameFlow& minigames = game::getSubsystem<MinigameFlow>();
        if ( _matchState == PlaygroundMatchState::Playing && minigames.getStack().findMatchID() == 0 )
        {
            // 나갔다(판 끝 · 나가기 · 장치 밖 씬 이동). 곧 서는 허브 씬이 매치 세션에 붙지 않게 링크를 먼저 뗀다 — 세션은 매치 씬이 내려간 뒤에 닫는다.
            if ( _hubAddress.isValid() )
                game::getSubsystem<SceneManager>().setNetLink( NetLinkValue{} );
            _matchState = PlaygroundMatchState::Leaving;
        }
        if ( _matchState == PlaygroundMatchState::Leaving && minigames.isLeaving() == false )
            reconnectHub( session );
    }

    void PlaygroundOnlineFlow::reconnectHub( OnlineSession& session )
    {
        if ( _hubAddress.isValid() == false )
        {
            clearMatch();
            return;
        }
        GameNetSession& netSession = game::getSubsystem<GameNetSession>();
        game::getSubsystem<SceneManager>().setNetLink( NetLinkValue{} );
        netSession.shutdown();
        if ( _bHubTicket == SW_TRUE )
        {
            _matchState = PlaygroundMatchState::HubTicket;
            (void)session.get<AccountClient>().issueGameTicket( session.getHubServerID() );
            return;
        }
        GameNetSessionParams params;
        params._role          = GameNetRole::Client;
        params._serverAddress = _hubAddress;
        if ( netSession.initialize( params ) )
            game::getSubsystem<SceneManager>().setNetLink( netSession.makeNetLink() );
        else
            SW_LOG_ERROR_ID( "Playground.SessionNotOpened", "[Playground] the session with %# could not be opened (see the messages above)", _hubAddress.toString() );
        clearMatch();
    }

    void PlaygroundOnlineFlow::clearMatch()
    {
        _matchState    = PlaygroundMatchState::None;
        _matchMinigame = hashed_string{};
        _hubAddress    = NetAddress{};
        _bHubTicket    = SW_FALSE;
    }

    void PlaygroundOnlineFlow::onMinigameFinished( const MinigameResult& result )
    {
        OnlineSession* const pSession = game::findSubsystem<OnlineSession>();
        if ( result._matchID == 0 || result._matchScore.has_value() == false || pSession == nullptr )
            return;
        // 접속한 매치의 점수는 서버가 센 값이다(`ShooterOwnerNetState::_killCount`). 혼자 선 판은 클라이언트가 세고, 순위표의 clientSubmit 이 그 제출을 받을지 정한다.
        LeaderboardSubmitRequest request;
        request._boardID = result._matchBoard;
        request._score   = *result._matchScore;
        (void)pSession->get<LeaderboardClient>().call<LeaderboardMethod::SubmitScore>( request, SW_DELEGATE_METHOD( OnlineReplyDelegate<LeaderboardMethod::SubmitScore>, &PlaygroundOnlineFlow::onScoreSubmitted, this ) );
        SW_LOG_INFO_ID( "Playground.MatchReported", "[Playground] match %# reported: %# = %#", result._matchID, result._matchBoard.c_str(), *result._matchScore );
    }

    void PlaygroundOnlineFlow::requestSubmitKills( OnlineSession& session, int64 kills )
    {
        LeaderboardSubmitRequest request;
        request._boardID = kBoardKills;
        request._score   = kills;
        (void)session.get<LeaderboardClient>().call<LeaderboardMethod::SubmitScore>( request, SW_DELEGATE_METHOD( OnlineReplyDelegate<LeaderboardMethod::SubmitScore>, &PlaygroundOnlineFlow::onScoreSubmitted, this ) );
    }

    void PlaygroundOnlineFlow::onScoreSubmitted( const OnlineResult<LeaderboardSubmitReply>& result )
    {
        if ( result.isOk() == false )
        {
            SW_LOG_WARNING_ID( "Playground.OnlineScoreFailed", "[Playground] online score submit failed (error %#, result %#)", result._errorCode, static_cast<int32>( result._reply._result ) );
            return;
        }
        ++_submittedScoreCount;
        SW_LOG_INFO_ID( "Playground.OnlineScoreSubmitted", "[Playground] online score submitted - best %#", result._reply._score );
    }

    void PlaygroundOnlineFlow::requestTopKills( OnlineSession& session )
    {
        LeaderboardTopRequest request;
        request._boardID = kBoardKills;
        request._offset  = 0;
        request._count   = kTopCount;
        (void)session.get<LeaderboardClient>().call<LeaderboardMethod::GetTop>( request, SW_DELEGATE_METHOD( OnlineReplyDelegate<LeaderboardMethod::GetTop>, &PlaygroundOnlineFlow::onTopKills, this ) );
    }

    void PlaygroundOnlineFlow::onTopKills( const OnlineResult<LeaderboardEntriesReply>& result )
    {
        if ( result.isOk() == false )
        {
            SW_LOG_WARNING_ID( "Playground.OnlineTopFailed", "[Playground] online top scores failed (error %#, result %#)", result._errorCode, static_cast<int32>( result._reply._result ) );
            return;
        }
        _listTopKills = result._reply._listEntry;
        SW_LOG_INFO_ID( "Playground.OnlineTopScores", "[Playground] online top scores: %# entries", static_cast<uint32>( _listTopKills.size() ) );
    }

    void PlaygroundOnlineFlow::requestLiveEvents( OnlineSession& session )
    {
        (void)session.get<LiveOpsClient>().requestLiveState( kMatchRegion, 0, SW_DELEGATE_FUNCTION( OnlineReplyDelegate<LiveOpsMethod::GetLiveState>, &PlaygroundOnlineFlowInternal::onLiveEvents ) );
    }

    void PlaygroundOnlineFlow::requestServiceStatus( OnlineSession& session )
    {
        (void)session.get<ServerDirectoryClient>().call<ServerDirectoryMethod::GetStatus>( SW_DELEGATE_FUNCTION( OnlineReplyDelegate<ServerDirectoryMethod::GetStatus>, &PlaygroundOnlineFlowInternal::onServiceStatus ) );
    }

    void PlaygroundOnlineFlow::requestLogin( OnlineSession& session, string_view loginName, string_view password )
    {
        (void)session.get<AccountClient>().login( loginName, password );
    }

    void PlaygroundOnlineFlow::requestMailList( OnlineSession& session )
    {
        (void)session.get<MailboxClient>().call<MailboxMethod::List>( MailboxListRequest{}, SW_DELEGATE_FUNCTION( OnlineReplyDelegate<MailboxMethod::List>, &PlaygroundOnlineFlowInternal::onMailList ) );
    }

    void PlaygroundOnlineFlow::requestClaimAll( OnlineSession& session )
    {
        (void)session.get<MailboxClient>().call<MailboxMethod::ClaimAll>( SW_DELEGATE_METHOD( OnlineReplyDelegate<MailboxMethod::ClaimAll>, &PlaygroundOnlineFlow::onClaimAll, this ) );
    }

    void PlaygroundOnlineFlow::onClaimAll( const OnlineResult<MailboxClaimReply>& result )
    {
        if ( result.isOk() == false )
        {
            SW_LOG_WARNING_ID( "Playground.OnlineClaimFailed", "[Playground] online mail claim failed (error %#, result %#)", result._errorCode, static_cast<int32>( result._reply._result ) );
            return;
        }
    }
} // namespace sw

namespace sw
{
    SW_DEV_COMMAND( PlaygroundOnlineLogin, "playground.online.login", "playground.online.login", "Log the online session in with the development account of Config/Server/Online/Playground.online.json", &PlaygroundOnlineFlowInternal::runLogin );
    SW_DEV_COMMAND( PlaygroundOnlineListMail, "playground.online.listmail", "playground.online.listmail", "Ask the mailbox kit for the first mail page", &PlaygroundOnlineFlowInternal::runListMail );
    SW_DEV_COMMAND( PlaygroundOnlineClaimAll, "playground.online.claimall", "playground.online.claimall", "Claim all mail and hand the balances to the economy client", &PlaygroundOnlineFlowInternal::runClaimAll );
    SW_DEV_COMMAND( PlaygroundOnlineSubmitKills, "playground.online.submitkills", "playground.online.submitkills <kills>", "Submit one round's kills to the arena_kills leaderboard", &PlaygroundOnlineFlowInternal::runSubmitKills );
    SW_DEV_COMMAND( PlaygroundOnlineTopKills, "playground.online.topkills", "playground.online.topkills", "Ask the arena_kills leaderboard for its top entries", &PlaygroundOnlineFlowInternal::runTopKills );

    SW_DEV_COMMAND( PlaygroundOnlineLiveEvents, "playground.online.liveevents", "playground.online.liveevents", "Ask the live-ops kit for the open events of this account", &PlaygroundOnlineFlowInternal::runLiveEvents );
    SW_DEV_COMMAND( PlaygroundOnlineServiceStatus, "playground.online.servicestatus", "playground.online.servicestatus", "Ask the server directory kit for announcements and maintenance windows", &PlaygroundOnlineFlowInternal::runServiceStatus );

    SW_AUTOMATION_WATCH( playgroundOnlineLoggedIn, "Playground.OnlineLoggedIn", "1 while the account kit client is logged in", &PlaygroundOnlineFlowInternal::readLoggedIn );
    SW_AUTOMATION_WATCH( playgroundOnlineMailCount, "Playground.OnlineMailCount", "Mails in the mailbox kit client's first page cache", &PlaygroundOnlineFlowInternal::readMailCount );
    SW_AUTOMATION_WATCH( playgroundOnlineGold, "Playground.OnlineGold", "Gold balance in the economy kit client's cache", &PlaygroundOnlineFlowInternal::readGold );
    SW_AUTOMATION_WATCH( playgroundOnlineGem, "Playground.OnlineGem", "Gem balance in the economy kit client's cache", &PlaygroundOnlineFlowInternal::readGem );
    SW_AUTOMATION_WATCH( playgroundOnlineMatchFound, "Playground.OnlineMatchFound", "1 once the matchmaking kit assigned this player a match", &PlaygroundOnlineFlowInternal::readMatchFound );
    SW_AUTOMATION_WATCH( playgroundMatchState, "Playground.MatchState", "Match session step: 0 none, 1 queued, 2 match ticket, 3 playing, 4 leaving, 5 hub ticket", &PlaygroundOnlineFlowInternal::readMatchState );
    SW_AUTOMATION_WATCH( playgroundOnlineTopKillsScore, "Playground.OnlineTopKillsScore", "Best score in the last arena_kills top list", &PlaygroundOnlineFlowInternal::readTopKillsScore );
    SW_AUTOMATION_WATCH( playgroundOnlineSubmittedScoreCount, "Playground.OnlineSubmittedScoreCount", "Leaderboard score submits the server has applied", &PlaygroundOnlineFlowInternal::readSubmittedCount );
    SW_AUTOMATION_WATCH( playgroundOnlineTopKillsCount, "Playground.OnlineTopKillsCount", "Entries in the last arena_kills top list", &PlaygroundOnlineFlowInternal::readTopKillsCount );
    SW_AUTOMATION_WATCH( playgroundOnlineSelfMailSent, "Playground.OnlineSelfMailSent", "Development mails the in-process server's store has written", &PlaygroundOnlineFlowInternal::readSelfMailSent );
} // namespace sw
