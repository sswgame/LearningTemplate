#include "pch.h"

#include "Games/Playground/Online/PlaygroundOnlineFlow.h"

#include "Core/Network/Connection/NetEndpointSecurity.h"
#include "Core/String/StringUtil.h"
#include "Core/Time/MonotonicClock.h"

#include "Engine/Asset/Cache/SharedAssetRef.h"
#include "Engine/Automation/Watch/AutomationWatch.h"
#include "Engine/Console/DevCommandRegistry.h"
#include "Engine/Network/GameNetSession.h"
#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManager.h"
#include "Engine/Reflection/EnumName.h"
#include "Engine/Scene/SceneManager.h"

#include "GameFramework/Kits/Feature/Online/Economy/Runtime/EconomyClient.h"
#include "GameFramework/Kits/Feature/Online/Economy/Runtime/EconomyShopPurchaseProvider.h"
#include "GameFramework/Kits/Feature/Online/Economy/Runtime/OfferCatalog.h"
#include "GameFramework/Kits/Feature/Online/Leaderboard/Runtime/LeaderboardClient.h"
#include "GameFramework/Kits/Feature/Online/LiveOps/Runtime/LiveOpsClient.h"
#include "GameFramework/Kits/Feature/Online/Mailbox/Runtime/MailboxClient.h"
#include "GameFramework/Kits/Feature/Online/Matchmaking/Runtime/MatchmakingClient.h"
#include "GameFramework/Kits/Feature/Online/ServerDirectory/Runtime/ServerDirectoryClient.h"
#include "GameFramework/Kits/Feature/Online/Social/Runtime/SocialClient.h"
#include "GameFramework/Runtime/Character/Control/ControlSystem.h"
#include "GameFramework/Runtime/Character/Control/Pawn/PawnComponent.h"
#include "GameFramework/Runtime/Foundation/Flow/GameInstance.h"
#include "GameFramework/Runtime/Foundation/Minigame/MinigameFlow.h"
#include "GameFramework/Runtime/Foundation/Service/GameService.h"
#include "GameFramework/Runtime/Online/Service/OnlineSession.h"
#include "GameFramework/Runtime/Online/Store/ServiceKeyUtil.h"

namespace sw
{
    namespace
    {
        /** @brief 개발 명령과 감시 값입니다. 세션과 흐름은 게임 범위에서 찾습니다. */
        struct PlaygroundOnlineFlowInternal
        {
            static constexpr const utf8* kArrDevLoginName[2] = { "playground_dev", "playground_dev2" }; ///< `Config/Server/Online/Playground.online.json` 의 시드 계정
            static constexpr const utf8* kArrDevPassword[2]  = { "playground_dev_pw", "playground_dev2_pw" };

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
                int32 number = 1;
                if ( listArgument.size() > 1 || ( listArgument.size() == 1 && StringUtil::parseInt( listArgument[0], number ) == false ) || number < 1 || number > 2 )
                    return false;
                OnlineSession*        pSession = nullptr;
                PlaygroundOnlineFlow* pFlow    = nullptr;
                if ( findParts( pSession, pFlow ) == false )
                {
                    outReply = kNoGameReply;
                    return true;
                }
                pFlow->requestLogin( *pSession, kArrDevLoginName[number - 1], kArrDevPassword[number - 1] );
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

            static bool runJoinHub( const vector<string>& listArgument, string& outReply )
            {
                NetAddress address;
                if ( listArgument.size() != 1 || NetAddress::parse( listArgument[0], 0, address ) == false )
                    return false;
                OnlineSession*        pSession = nullptr;
                PlaygroundOnlineFlow* pFlow    = nullptr;
                if ( findParts( pSession, pFlow ) == false )
                {
                    outReply = kNoGameReply;
                    return true;
                }
                outReply = pFlow->requestJoinHub( *pSession, address ) ? "playground.online: hub ticket sent" : "playground.online: joining the hub is refused";
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

            [[nodiscard]] static bool readRefusedCount( const GameObjectManager* pManager, float64& outValue )
            {
                (void)pManager;
                const PlaygroundOnlineFlow* const pFlow = game::findSubsystem<PlaygroundOnlineFlow>();
                if ( pFlow == nullptr )
                    return false;
                outValue = static_cast<float64>( pFlow->getRefusedScoreCount() );
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
        , _friends{}
        , _listTradeReplyScratch{}
        , _listTradeUpdateScratch{}
        , _matchLease{}
        , _assignment{}
        , _pendingTicket{}
        , _matchMinigame{}
        , _hubAddress{}
        , _reconnectDeadline{ 0 }
        , _shownFriendRevision{ 0 }
        , _submittedScoreCount{ 0 }
        , _refusedScoreCount{ 0 }
        , _peopleRefusedCount{ 0 }
        , _tradeSettledCount{ 0 }
        , _selfMailSentCount{ 0 }
        , _matchState{ PlaygroundMatchState::None }
        , _bHubTicket{ SW_FALSE }
        , _bReconnectHub{ SW_FALSE }
        , _bFriendsBound{ SW_FALSE }
    {
    }

    PlaygroundOnlineFlow::~PlaygroundOnlineFlow() = default;

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
        const GameNetSession& netSession = game::getSubsystem<GameNetSession>();
        if ( netSession.getRole() == GameNetRole::Server )
        {
            SW_LOG_WARNING_ID( "Playground.MatchHostCannotLeave", "[Playground] match '%#' is refused - this process hosts the session its players are in", definition._name.c_str() );
            return false;
        }
        const bool bTicketHub = netSession.getRole() == GameNetRole::Client && netSession.isTicketBound();
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

    void PlaygroundOnlineFlow::update( GameInstance& instance )
    {
        OnlineSession&       session  = instance.getSubsystem<OnlineSession>();
        AccountClient* const pAccount = session.find<AccountClient>();
        updateHubLoss( session, instance ); // 계정 없이 붙은 허브(실행 인자 · 개발 명령)도 잃는다
        if ( pAccount == nullptr )
            return;
        openTicketSession( session ); // 앞 프레임에 받은 티켓
        updatePeople( session );
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
        updateMatchConnect( session );
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
        const bool            bInHub     = netSession.getRole() == GameNetRole::Client;
        _hubAddress                      = bInHub ? netSession.getServerAddress() : NetAddress{}; // 혼자 하는 허브면 돌아갈 세션이 없다
        _bHubTicket                      = bInHub && netSession.isTicketBound() ? SW_TRUE : SW_FALSE;
        _matchState                      = PlaygroundMatchState::MatchTicket;
        (void)session.get<AccountClient>().issueGameTicket( ServiceKeyUtil::makeHex64( _assignment._serverID ) ); // 답은 `update` 가 받는다
    }

    bool PlaygroundOnlineFlow::requestJoinHub( OnlineSession& session, const NetAddress& address )
    {
        if ( session.getHubServerID().empty() || _matchState != PlaygroundMatchState::None || address.isValid() == false )
        {
            SW_LOG_WARNING_ID( "Playground.HubJoinRefused", "[Playground] joining the hub %# is refused - it needs GameSettings onlineHubServerID, a valid address and no running match", address.toString() );
            return false;
        }
        _hubAddress = address;
        _bHubTicket = SW_TRUE;
        _matchState = PlaygroundMatchState::HubTicket;
        (void)session.get<AccountClient>().issueGameTicket( session.getHubServerID() );
        return true;
    }

    void PlaygroundOnlineFlow::onGameTicket( OnlineSession& session, const NetGameTicket& ticket )
    {
        const bool bMatch = _matchState == PlaygroundMatchState::MatchTicket;
        if ( bMatch == false && _matchState != PlaygroundMatchState::HubTicket )
            return; // 매치와 상관없는 티켓(개발 명령)이다
        // 세션을 여는 것은 앞 세션(허브)을 닫는 일이다 — 링크를 먼저 떼고 한 프레임을 돌려, 씬의 컴포넌트가 앞 세션의 처리기를 내린 뒤에 연다(`update`).
        game::getSubsystem<SceneManager>().setNetLink( NetLinkValue{} );
        _pendingTicket = ticket;
        (void)session;
    }

    void PlaygroundOnlineFlow::openTicketSession( OnlineSession& session )
    {
        if ( _pendingTicket.has_value() == false )
            return;
        const NetGameTicket ticket = std::move( *_pendingTicket );
        _pendingTicket             = nullopt;
        const bool bMatch          = _matchState == PlaygroundMatchState::MatchTicket;
        NetAddress address         = _hubAddress;
        if ( bMatch && NetAddress::parse( _assignment._address, _assignment._port, address ) == false )
        {
            SW_LOG_ERROR_ID( "Playground.MatchAddressInvalid", "[Playground] match %# names the server '%#:%#', which is not an address - returning to the hub", _assignment._matchID,
                             _assignment._address.c_str(), _assignment._port );
            reconnectHub( session );
            return;
        }
        GameNetSession&       netSession   = game::getSubsystem<GameNetSession>();
        SceneManager&         sceneManager = game::getSubsystem<SceneManager>();
        NetConnectCredentials credentials;
        GameNetSessionParams  params;
        AccountClient::makeConnectCredentials( ticket, credentials );
        const bool bOpened = netSession.makeTicketClientParams( address, credentials, params ) && netSession.initialize( params );
        if ( bOpened == false )
            SW_LOG_ERROR_ID( "Playground.SessionNotOpened", "[Playground] the session with %# could not be opened (see the messages above)", address.toString() );
        if ( bMatch )
        {
            // 매치 서버에 붙은 뒤에 허브 씬을 떠난다 — 링크는 그때 다음 씬에만 건다(허브 씬의 컴포넌트가 매치 세션에 붙지 않는다).
            if ( bOpened )
                _matchState = PlaygroundMatchState::MatchConnect;
            else
                reconnectHub( session );
            return;
        }
        if ( bOpened == false )
        {
            leaveHub();
            return;
        }
        sceneManager.setNetLink( netSession.makeNetLink() );
        clearMatch(); // 허브로 돌아왔다
    }

    void PlaygroundOnlineFlow::onGameTicketRefused()
    {
        if ( _matchState == PlaygroundMatchState::MatchTicket )
            clearMatch(); // 아직 허브를 떠나지 않았다
        else if ( _matchState == PlaygroundMatchState::HubTicket )
        {
            SW_LOG_ERROR_ID( "Playground.HubTicketRefused", "[Playground] the hub server ticket was refused - the player leaves the server's hub" );
            leaveHub();
        }
    }

    void PlaygroundOnlineFlow::updateMatchConnect( OnlineSession& session )
    {
        if ( _matchState != PlaygroundMatchState::MatchConnect )
            return;
        GameNetSession& netSession = game::getSubsystem<GameNetSession>();
        if ( netSession.getConnectedCount() == 0 )
        {
            if ( netSession.hasConnectFailed() == false )
                return; // 붙는 중이다
            SW_LOG_WARNING_ID( "Playground.MatchNotConnected", "[Playground] the server of match %# could not be reached - returning to the hub", _assignment._matchID );
            reconnectHub( session );
            return;
        }
        game::getSubsystem<SceneManager>().setNextSceneNetLink( netSession.makeNetLink() ); // 매치 씬이 받는다
        _matchState = PlaygroundMatchState::Playing;
        if ( game::getSubsystem<MinigameFlow>().enterMatch( _matchMinigame.view(), _assignment._matchID ) )
            return;
        SW_LOG_ERROR_ID( "Playground.MatchNotEntered", "[Playground] match %# could not be entered (see the messages above) - returning to the hub session", _assignment._matchID );
        reconnectHub( session );
    }

    void PlaygroundOnlineFlow::updateMatchExit( OnlineSession& session )
    {
        MinigameFlow& minigames = game::getSubsystem<MinigameFlow>();
        const bool    bPlaying  = _matchState == PlaygroundMatchState::Playing;
        // 판 도중 서버를 잃었다(서버가 내려감 · 시간 초과) — 남은 화면에 세워 두지 않고 나온다. 들어가는 · 나오는 중이면 끝난 뒤에 본다.
        const bool bLost = bPlaying && minigames.getStack().findMatchID() != 0 && minigames.isEntering() == false && minigames.isLeaving() == false &&
                           game::getSubsystem<GameNetSession>().getConnectedCount() == 0;
        if ( bLost )
            (void)minigames.leave( hashed_string( kResultDisconnected ) ); // 실패는 leave 가 알렸다
        if ( bPlaying && minigames.getStack().findMatchID() == 0 )
        {
            // 나갔다(판 끝 · 나가기 · 장치 밖 씬 이동). 곧 서는 허브 씬이 매치 세션에 붙지 않게 링크를 먼저 뗀다 — 세션은 매치 씬이 내려간 뒤에 닫는다.
            game::getSubsystem<SceneManager>().setNetLink( NetLinkValue{} );
            _matchState = PlaygroundMatchState::Leaving;
        }
        if ( _matchState == PlaygroundMatchState::Leaving && minigames.isLeaving() == false )
            reconnectHub( session );
    }

    bool PlaygroundOnlineFlow::shouldReconnectHub( NetDisconnectReason reason, const optional<GameShutdownNotice>& notification )
    {
        if ( notification.has_value() )
            return notification->_reason == GameShutdownReason::ServerRestart;
        return reason == NetDisconnectReason::Timeout || reason == NetDisconnectReason::ReceiveOverflow || reason == NetDisconnectReason::ReliableOverflow;
    }

    bool PlaygroundOnlineFlow::canReturnToHub( OnlineSession& session ) const
    {
        const AccountClient* const pAccount = session.find<AccountClient>();
        return session.getHubReconnectSeconds() > 0.0f &&
               ( _bHubTicket == SW_FALSE || ( pAccount != nullptr && pAccount->isLoggedIn() && session.getHubServerID().empty() == false ) );
    }

    void PlaygroundOnlineFlow::leaveHub()
    {
        game::getSubsystem<SceneManager>().setNetLink( NetLinkValue{} );
        _bReconnectHub     = SW_FALSE;
        _reconnectDeadline = 0;
        _matchState        = PlaygroundMatchState::HubLost;
    }

    void PlaygroundOnlineFlow::updateHubLoss( OnlineSession& session, GameInstance& instance )
    {
        GameNetSession& netSession = game::getSubsystem<GameNetSession>();
        if ( _matchState == PlaygroundMatchState::HubLost )
        {
            if ( _bReconnectHub == SW_TRUE )
            {
                reconnectHub( session );
                return;
            }
            netSession.shutdown();
            clearMatch();
            instance.leaveServerWorld(); // 서버의 사본에 남지 않는다 — 혼자 하던 자리(없으면 입구)로
            return;
        }
        // 미니게임 안이면 허브 씬으로 돌아온 뒤에 본다 — 다시 건 링크가 혼자 하는 미니게임 씬에 닿지 않게.
        const MinigameFlow& minigames = game::getSubsystem<MinigameFlow>();
        const bool          bInHub    = _matchState == PlaygroundMatchState::None && minigames.getStack().isEmpty() && minigames.isEntering() == false && minigames.isLeaving() == false;
        if ( netSession.getConnectedCount() > 0 || ( netSession.getRole() == GameNetRole::None && _matchState == PlaygroundMatchState::None ) )
            _reconnectDeadline = 0; // 다시 붙었거나 세션을 떠났다
        if ( bInHub == false || netSession.getRole() != GameNetRole::Client )
            return;
        // 시간 초과는 세션이 되풀이해 시도한다 — 다시 붙는 길에는 한도가 있다(서버의 사본에 끝없이 세워 두지 않는다).
        const bool bReconnectExpired = _reconnectDeadline != 0 && MonotonicClock::nowNanoseconds() >= _reconnectDeadline;
        if ( netSession.getLostReason().has_value() == false && bReconnectExpired == false )
            return;
        const NetDisconnectReason    reason = netSession.getLostReason().value_or( NetDisconnectReason::Timeout );
        GameShutdownNotice           received;
        optional<GameShutdownNotice> notification;
        if ( netSession.tryGetShutdownNotice( received ) )
            notification = received;
        _hubAddress    = netSession.getServerAddress();
        _bHubTicket    = netSession.isTicketBound() ? SW_TRUE : SW_FALSE;
        _bReconnectHub = bReconnectExpired == false && shouldReconnectHub( reason, notification ) && canReturnToHub( session ) ? SW_TRUE : SW_FALSE;
        SW_LOG_WARNING_ID( "Playground.HubLost", "[Playground] lost the hub server (%#) - %#", toString( reason ),
                           _bReconnectHub == SW_TRUE ? "joining it again" : "the player leaves the server's hub" );
        if ( _bReconnectHub == SW_FALSE )
        {
            leaveHub();
            return;
        }
        _matchState = PlaygroundMatchState::HubLost;
        game::getSubsystem<SceneManager>().setNetLink( NetLinkValue{} ); // 씬의 처리기가 내려간 뒤(다음 프레임)에 세션을 닫는다
    }

    void PlaygroundOnlineFlow::reconnectHub( OnlineSession& session )
    {
        GameNetSession& netSession = game::getSubsystem<GameNetSession>();
        game::getSubsystem<SceneManager>().setNetLink( NetLinkValue{} );
        netSession.shutdown();
        if ( _hubAddress.isValid() == false )
        {
            clearMatch(); // 혼자 하는 허브다
            return;
        }
        if ( canReturnToHub( session ) == false )
        {
            SW_LOG_WARNING_ID( "Playground.HubNoAccount", "[Playground] the hub server is not joined again (no login for its ticket, or GameSettings onlineHubServerID / onlineHubReconnectSeconds is not set) - the player leaves the server's hub" );
            leaveHub();
            return;
        }
        if ( _reconnectDeadline == 0 )
            _reconnectDeadline = MonotonicClock::nowNanoseconds() + static_cast<int64>( static_cast<float64>( session.getHubReconnectSeconds() ) * static_cast<float64>( constant::time::kNanosecondsPerSecond ) );
        if ( _bHubTicket == SW_TRUE )
        {
            _matchState = PlaygroundMatchState::HubTicket;
            (void)session.get<AccountClient>().issueGameTicket( session.getHubServerID() );
            return;
        }
        GameNetSessionParams params;
        params._role          = GameNetRole::Client;
        params._serverAddress = _hubAddress;
        if ( netSession.initialize( params ) == false )
        {
            SW_LOG_ERROR_ID( "Playground.SessionNotOpened", "[Playground] the session with %# could not be opened (see the messages above)", _hubAddress.toString() );
            leaveHub();
            return;
        }
        game::getSubsystem<SceneManager>().setNetLink( netSession.makeNetLink() );
        clearMatch();
    }

    void PlaygroundOnlineFlow::clearMatch()
    {
        _matchState    = PlaygroundMatchState::None;
        _pendingTicket = nullopt;
        _matchMinigame = hashed_string{};
        _hubAddress    = NetAddress{};
        _bHubTicket    = SW_FALSE;
        _bReconnectHub = SW_FALSE;
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
        if ( result._errorCode == OnlineError::kOk && result._reply._result == LeaderboardResult::NotAllowed )
        {
            ++_refusedScoreCount;
            SW_LOG_INFO_ID( "Playground.OnlineScoreRefused", "[Playground] online score refused - the board takes scores from the game server only" );
            return;
        }
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

    void PlaygroundOnlineFlow::requestFriend( OnlineSession& session, string_view displayName )
    {
        SocialTextRequest request;
        request._text = string( displayName );
        (void)session.get<SocialClient>().call<SocialMethod::RequestFriendByName>( request, SW_DELEGATE_METHOD( OnlineReplyDelegate<SocialMethod::RequestFriendByName>, &PlaygroundOnlineFlow::onFriendRequested, this ),
                                                                                   NetIdempotencyKey::makeRandom() );
    }

    void PlaygroundOnlineFlow::requestAcceptFriends( OnlineSession& session )
    {
        (void)session.get<SocialClient>().call<SocialMethod::ListLinks>( SW_DELEGATE_METHOD( OnlineReplyDelegate<SocialMethod::ListLinks>, &PlaygroundOnlineFlow::onLinksToAccept, this ) );
    }

    void PlaygroundOnlineFlow::onFriendRequested( const OnlineResult<SocialLinkReply>& result )
    {
        if ( result.isOk() )
        {
            SW_LOG_INFO_ID( "Playground.FriendRequested", "[Playground] friend request sent" );
            return;
        }
        ++_peopleRefusedCount;
        SW_LOG_INFO_ID( "Playground.FriendRefused", "[Playground] friend request refused (error %#, result %#)", result._errorCode, static_cast<int32>( result._reply._result ) );
    }

    void PlaygroundOnlineFlow::onLinksToAccept( const OnlineResult<SocialLinksReply>& result )
    {
        OnlineSession* const pSession = game::findSubsystem<OnlineSession>();
        if ( result.isOk() == false || pSession == nullptr )
            return;
        for ( const SocialLink& link : result._reply._listLink )
        {
            if ( link._state != SocialLinkState::Incoming )
                continue;
            SocialRespondRequest request;
            request._otherID = link._otherID;
            request._bAccept = SW_TRUE;
            (void)pSession->get<SocialClient>().call<SocialMethod::RespondFriend>( request, SW_DELEGATE_METHOD( OnlineReplyDelegate<SocialMethod::RespondFriend>, &PlaygroundOnlineFlow::onFriendAnswered, this ),
                                                                                   NetIdempotencyKey::makeRandom() );
        }
    }

    void PlaygroundOnlineFlow::onFriendAnswered( const OnlineResult<SocialLinkReply>& result )
    {
        if ( result.isOk() )
            SW_LOG_INFO_ID( "Playground.FriendAccepted", "[Playground] friend request accepted" );
        else
            SW_LOG_WARNING_ID( "Playground.FriendAcceptFailed", "[Playground] accepting a friend failed (error %#, result %#)", result._errorCode, static_cast<int32>( result._reply._result ) );
    }

    void PlaygroundOnlineFlow::updatePeople( OnlineSession& session )
    {
        SocialClient* const pSocial = session.find<SocialClient>();
        if ( pSocial == nullptr )
            return;
        if ( _bFriendsBound == SW_FALSE )
            _bFriendsBound = _friends.initialize( pSocial ) ? SW_TRUE : SW_FALSE;
        if ( _bFriendsBound == SW_TRUE )
            _friends.update();
        if ( _friends.getState() == FriendListState::Loaded && _friends.getRevision() != _shownFriendRevision )
        {
            _shownFriendRevision = _friends.getRevision();
            SW_LOG_INFO_ID( "Playground.FriendList", "[Playground] friend list: %# friends, %# pending requests", static_cast<uint32>( _friends.getEntries().size() ), _friends.getPendingRequestCount() );
        }
        TradeClient* const pTrade = session.find<TradeClient>();
        if ( pTrade == nullptr )
            return;
        pTrade->setAccountID( session.get<AccountClient>().getIdentity()._accountID );
        _listTradeReplyScratch.clear();
        _listTradeUpdateScratch.clear();
        (void)pTrade->pollReplies( _listTradeReplyScratch );
        (void)pTrade->pollUpdates( _listTradeUpdateScratch );
        for ( const TradeClientReply& reply : _listTradeReplyScratch )
        {
            if ( reply._errorCode == OnlineError::kOk && reply._result == TradeResult::Ok )
                continue;
            ++_peopleRefusedCount;
            SW_LOG_INFO_ID( "Playground.TradeRefused", "[Playground] trade call refused (error %#, result %#)", reply._errorCode, static_cast<int32>( reply._result ) );
        }
        for ( const TradeClientUpdate& update : _listTradeUpdateScratch )
        {
            SW_LOG_INFO_ID( "Playground.TradeChanged", "[Playground] trade %# is %#", update._snapshot._tradeID, getEnumName( update._snapshot._state ) );
            if ( update._snapshot._state == TradeState::Settled )
                ++_tradeSettledCount;
        }
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
    SW_DEV_COMMAND( PlaygroundOnlineLogin, "playground.online.login", "playground.online.login [1|2]", "Log the online session in with a development account (first by default) of Config/Server/Online/Playground.online.json", &PlaygroundOnlineFlowInternal::runLogin );
    SW_DEV_COMMAND( PlaygroundOnlineListMail, "playground.online.listmail", "playground.online.listmail", "Ask the mailbox kit for the first mail page", &PlaygroundOnlineFlowInternal::runListMail );
    SW_DEV_COMMAND( PlaygroundOnlineClaimAll, "playground.online.claimall", "playground.online.claimall", "Claim all mail and hand the balances to the economy client", &PlaygroundOnlineFlowInternal::runClaimAll );
    SW_DEV_COMMAND( PlaygroundOnlineSubmitKills, "playground.online.submitkills", "playground.online.submitkills <kills>", "Submit kills to the arena_kills leaderboard as a client (the board refuses it - the match server submits)", &PlaygroundOnlineFlowInternal::runSubmitKills );
    SW_DEV_COMMAND( PlaygroundOnlineJoinHub, "playground.online.joinhub", "playground.online.joinhub <hubHost:port>", "Ask for the hub server ticket (GameSettings onlineHubServerID) and join the hub with it", &PlaygroundOnlineFlowInternal::runJoinHub );
    SW_DEV_COMMAND( PlaygroundOnlineTopKills, "playground.online.topkills", "playground.online.topkills", "Ask the arena_kills leaderboard for its top entries", &PlaygroundOnlineFlowInternal::runTopKills );

    SW_DEV_COMMAND( PlaygroundOnlineLiveEvents, "playground.online.liveevents", "playground.online.liveevents", "Ask the live-ops kit for the open events of this account", &PlaygroundOnlineFlowInternal::runLiveEvents );
    SW_DEV_COMMAND( PlaygroundOnlineServiceStatus, "playground.online.servicestatus", "playground.online.servicestatus", "Ask the server directory kit for announcements and maintenance windows", &PlaygroundOnlineFlowInternal::runServiceStatus );

    SW_AUTOMATION_WATCH( playgroundOnlineLoggedIn, "Playground.OnlineLoggedIn", "1 while the account kit client is logged in", &PlaygroundOnlineFlowInternal::readLoggedIn );
    SW_AUTOMATION_WATCH( playgroundOnlineMailCount, "Playground.OnlineMailCount", "Mails in the mailbox kit client's first page cache", &PlaygroundOnlineFlowInternal::readMailCount );
    SW_AUTOMATION_WATCH( playgroundOnlineGold, "Playground.OnlineGold", "Gold balance in the economy kit client's cache", &PlaygroundOnlineFlowInternal::readGold );
    SW_AUTOMATION_WATCH( playgroundOnlineGem, "Playground.OnlineGem", "Gem balance in the economy kit client's cache", &PlaygroundOnlineFlowInternal::readGem );
    SW_AUTOMATION_WATCH( playgroundOnlineMatchFound, "Playground.OnlineMatchFound", "1 once the matchmaking kit assigned this player a match", &PlaygroundOnlineFlowInternal::readMatchFound );
    SW_AUTOMATION_WATCH( playgroundMatchState, "Playground.MatchState", "Match session step: 0 none, 1 queued, 2 match ticket, 3 match connect, 4 playing, 5 leaving, 6 hub ticket", &PlaygroundOnlineFlowInternal::readMatchState );
    SW_AUTOMATION_WATCH( playgroundOnlineTopKillsScore, "Playground.OnlineTopKillsScore", "Best score in the last arena_kills top list", &PlaygroundOnlineFlowInternal::readTopKillsScore );
    SW_AUTOMATION_WATCH( playgroundOnlineSubmittedScoreCount, "Playground.OnlineSubmittedScoreCount", "Leaderboard score submits the server has applied", &PlaygroundOnlineFlowInternal::readSubmittedCount );
    SW_AUTOMATION_WATCH( playgroundOnlineRefusedScoreCount, "Playground.OnlineRefusedScoreCount", "Client score submits the board refused (server submits only)", &PlaygroundOnlineFlowInternal::readRefusedCount );
    SW_AUTOMATION_WATCH( playgroundOnlineTopKillsCount, "Playground.OnlineTopKillsCount", "Entries in the last arena_kills top list", &PlaygroundOnlineFlowInternal::readTopKillsCount );
    SW_AUTOMATION_WATCH( playgroundOnlineSelfMailSent, "Playground.OnlineSelfMailSent", "Development mails the in-process server's store has written", &PlaygroundOnlineFlowInternal::readSelfMailSent );
} // namespace sw
