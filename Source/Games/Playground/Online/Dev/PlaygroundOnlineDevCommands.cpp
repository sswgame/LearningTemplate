/**
 * @file PlaygroundOnlineDevCommands.cpp
 * @brief 온라인 흐름(`PlaygroundOnlineFlow`)의 개발 명령(`playground.online.*`)과 감시 값(`Playground.Online*`)입니다. `Dev/` 라 배포 구성은 짓지 않는다.
 */
#include "pch.h"

#include "Core/Network/NetAddress.h"
#include "Core/String/StringUtil.h"

#include "Engine/Automation/Watch/AutomationWatch.h"
#include "Engine/Console/DevCommandRegistry.h"
#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManager.h"

#include "GameFramework/Kits/Feature/Online/Economy/Runtime/EconomyClient.h"
#include "GameFramework/Kits/Feature/Online/Leaderboard/Runtime/LeaderboardClient.h"
#include "GameFramework/Kits/Feature/Online/Mailbox/Runtime/MailboxClient.h"
#include "GameFramework/Runtime/Foundation/GameData/GameSettings.h"
#include "GameFramework/Runtime/Foundation/Service/GameSubsystems.h"
#include "GameFramework/Runtime/Online/Service/OnlineSession.h"

#include "Games/Playground/Online/PlaygroundOnlineFlow.h"

namespace sw
{
    namespace
    {
        /** @brief 개발 명령과 감시 값입니다. 세션과 흐름은 게임 범위에서 찾습니다. */
        struct PlaygroundOnlineDevCommandsInternal
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
                outValue = static_cast<float64>( pSession->get<EconomyClient>().getBalance( pSession->getGameSettings()._onlineSoftCurrency ) );
                return true;
            }

            [[nodiscard]] static bool readGem( const GameObjectManager* pManager, float64& outValue )
            {
                (void)pManager;
                const OnlineSession* const pSession = game::findSubsystem<OnlineSession>();
                if ( pSession == nullptr )
                    return false;
                outValue = static_cast<float64>( pSession->get<EconomyClient>().getBalance( pSession->getGameSettings()._onlineHardCurrency ) );
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
        };
    } // namespace

    SW_DEV_COMMAND( PlaygroundOnlineLogin, "playground.online.login", "playground.online.login [1|2]", "Log the online session in with a development account (first by default) of Config/Server/Online/Playground.online.json", &PlaygroundOnlineDevCommandsInternal::runLogin );
    SW_DEV_COMMAND( PlaygroundOnlineListMail, "playground.online.listmail", "playground.online.listmail", "Ask the mailbox kit for the first mail page", &PlaygroundOnlineDevCommandsInternal::runListMail );
    SW_DEV_COMMAND( PlaygroundOnlineClaimAll, "playground.online.claimall", "playground.online.claimall", "Claim all mail and hand the balances to the economy client", &PlaygroundOnlineDevCommandsInternal::runClaimAll );
    SW_DEV_COMMAND( PlaygroundOnlineSubmitKills, "playground.online.submitkills", "playground.online.submitkills <kills>", "Submit kills to the arena_kills leaderboard as a client (the board refuses it - the match server submits)", &PlaygroundOnlineDevCommandsInternal::runSubmitKills );
    SW_DEV_COMMAND( PlaygroundOnlineJoinHub, "playground.online.joinhub", "playground.online.joinhub <hubHost:port>", "Ask for the hub server ticket (GameSettings onlineHubServerID) and join the hub with it", &PlaygroundOnlineDevCommandsInternal::runJoinHub );
    SW_DEV_COMMAND( PlaygroundOnlineTopKills, "playground.online.topkills", "playground.online.topkills", "Ask the arena_kills leaderboard for its top entries", &PlaygroundOnlineDevCommandsInternal::runTopKills );

    SW_DEV_COMMAND( PlaygroundOnlineLiveEvents, "playground.online.liveevents", "playground.online.liveevents", "Ask the live-ops kit for the open events of this account", &PlaygroundOnlineDevCommandsInternal::runLiveEvents );
    SW_DEV_COMMAND( PlaygroundOnlineServiceStatus, "playground.online.servicestatus", "playground.online.servicestatus", "Ask the server directory kit for announcements and maintenance windows", &PlaygroundOnlineDevCommandsInternal::runServiceStatus );

    SW_AUTOMATION_WATCH( playgroundOnlineLoggedIn, "Playground.OnlineLoggedIn", "1 while the account kit client is logged in", &PlaygroundOnlineDevCommandsInternal::readLoggedIn );
    SW_AUTOMATION_WATCH( playgroundOnlineMailCount, "Playground.OnlineMailCount", "Mails in the mailbox kit client's first page cache", &PlaygroundOnlineDevCommandsInternal::readMailCount );
    SW_AUTOMATION_WATCH( playgroundOnlineGold, "Playground.OnlineGold", "Gold balance in the economy kit client's cache", &PlaygroundOnlineDevCommandsInternal::readGold );
    SW_AUTOMATION_WATCH( playgroundOnlineGem, "Playground.OnlineGem", "Gem balance in the economy kit client's cache", &PlaygroundOnlineDevCommandsInternal::readGem );
    SW_AUTOMATION_WATCH( playgroundOnlineMatchFound, "Playground.OnlineMatchFound", "1 once the matchmaking kit assigned this player a match", &PlaygroundOnlineDevCommandsInternal::readMatchFound );
    SW_AUTOMATION_WATCH( playgroundMatchState, "Playground.MatchState", "Match session step: 0 none, 1 queued, 2 match ticket, 3 match connect, 4 playing, 5 leaving, 6 hub ticket", &PlaygroundOnlineDevCommandsInternal::readMatchState );
    SW_AUTOMATION_WATCH( playgroundOnlineTopKillsScore, "Playground.OnlineTopKillsScore", "Best score in the last arena_kills top list", &PlaygroundOnlineDevCommandsInternal::readTopKillsScore );
    SW_AUTOMATION_WATCH( playgroundOnlineSubmittedScoreCount, "Playground.OnlineSubmittedScoreCount", "Leaderboard score submits the server has applied", &PlaygroundOnlineDevCommandsInternal::readSubmittedCount );
    SW_AUTOMATION_WATCH( playgroundOnlineRefusedScoreCount, "Playground.OnlineRefusedScoreCount", "Client score submits the board refused (server submits only)", &PlaygroundOnlineDevCommandsInternal::readRefusedCount );
    SW_AUTOMATION_WATCH( playgroundOnlineTopKillsCount, "Playground.OnlineTopKillsCount", "Entries in the last arena_kills top list", &PlaygroundOnlineDevCommandsInternal::readTopKillsCount );
    SW_AUTOMATION_WATCH( playgroundOnlineSelfMailSent, "Playground.OnlineSelfMailSent", "Development mails the in-process server's store has written", &PlaygroundOnlineDevCommandsInternal::readSelfMailSent );
} // namespace sw
