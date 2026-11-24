/**
 * @file OnlinePeople.cpp
 * @brief 허브 사람들(친구 · 거래)의 개발 명령과 감시 값입니다. 규칙은 서버 키트(소셜 · 거래)가 정하고, 흐름(`PlaygroundOnlineFlow`)이 요청과 답을 잇습니다.
 * @details 시나리오: `game/playground/automation/online_people.scenario.xml`(거절 길), 두 계정의 친구 · 거래는 `py -3 -m Scripts online-local people`.
 */
#include "pch.h"

#include "Core/String/StringUtil.h"

#include "Engine/Automation/Watch/AutomationWatch.h"
#include "Engine/Console/DevCommandRegistry.h"

#include "GameFramework/Kits/Feature/Online/Account/Runtime/AccountClient.h"
#include "GameFramework/Kits/Feature/Online/Trade/Runtime/TradeClient.h"
#include "GameFramework/Runtime/Foundation/Service/GameService.h"
#include "GameFramework/Runtime/Online/Service/OnlineSession.h"

#include "Games/Playground/Online/PlaygroundOnlineFlow.h"

namespace sw
{
    namespace
    {
        struct OnlinePeopleInternal
        {
            static constexpr const utf8* kNoGameReply = "playground.online: no Playground game instance";

            [[nodiscard]] static bool findParts( OnlineSession*& pOutSession, PlaygroundOnlineFlow*& pOutFlow )
            {
                pOutSession = game::findSubsystem<OnlineSession>();
                pOutFlow    = game::findSubsystem<PlaygroundOnlineFlow>();
                return pOutSession != nullptr && pOutFlow != nullptr;
            }

            static bool runFriend( const vector<string>& listArgument, string& outReply )
            {
                if ( listArgument.empty() || listArgument.size() > 2 )
                    return false;
                OnlineSession*        pSession = nullptr;
                PlaygroundOnlineFlow* pFlow    = nullptr;
                if ( findParts( pSession, pFlow ) == false )
                {
                    outReply = kNoGameReply;
                    return true;
                }
                if ( listArgument[0] == "request" && listArgument.size() == 2 )
                    pFlow->requestFriend( *pSession, listArgument[1] );
                else if ( listArgument[0] == "accept" && listArgument.size() == 1 )
                    pFlow->requestAcceptFriends( *pSession );
                else if ( listArgument[0] == "list" && listArgument.size() == 1 )
                {
                    outReply = pFlow->getFriends().refresh() ? "playground.online.friend: list sent" : "playground.online.friend: the list is busy";
                    return true;
                }
                else
                    return false;
                outReply = "playground.online.friend: sent";
                return true;
            }

            static bool runTrade( const vector<string>& listArgument, string& outReply )
            {
                if ( listArgument.empty() || listArgument.size() > 2 )
                    return false;
                OnlineSession*        pSession = nullptr;
                PlaygroundOnlineFlow* pFlow    = nullptr;
                if ( findParts( pSession, pFlow ) == false )
                {
                    outReply = kNoGameReply;
                    return true;
                }
                TradeClient&  trade   = pSession->get<TradeClient>();
                const uint64  tradeID = trade.getSnapshot()._tradeID;
                const string& verb    = listArgument[0];
                const bool    bAlone  = listArgument.size() == 1;
                int32         amount  = 0;
                if ( verb == "invite" && bAlone == false )
                    (void)trade.invite( listArgument[1] );
                else if ( verb == "accept" && bAlone )
                    (void)trade.respond( tradeID, true );
                else if ( verb == "offer" && bAlone == false && StringUtil::parseInt( listArgument[1], amount ) && amount >= 0 )
                {
                    vector<TradeLeg> listLeg;
                    if ( amount > 0 )
                        listLeg.push_back( TradeLeg{ PlaygroundOnlineFlow::kAssetGold, amount } );
                    (void)trade.setOffer( tradeID, listLeg );
                }
                else if ( verb == "lock" && bAlone )
                    (void)trade.lock( tradeID );
                else if ( verb == "confirm" && bAlone )
                    (void)trade.confirm( tradeID );
                else if ( verb == "cancel" && bAlone )
                    (void)trade.cancel( tradeID );
                else
                    return false;
                outReply = "playground.online.trade: sent";
                return true;
            }

            /** @brief 흐름에서 감시 값 하나를 읽습니다. */
            template <typename TRead>
            [[nodiscard]] static bool readFlow( float64& outValue, TRead read )
            {
                OnlineSession*        pSession = nullptr;
                PlaygroundOnlineFlow* pFlow    = nullptr;
                if ( findParts( pSession, pFlow ) == false )
                    return false;
                outValue = static_cast<float64>( read( *pSession, *pFlow ) );
                return true;
            }

            [[nodiscard]] static bool readFriendCount( const GameObjectManager* /*pManager*/, float64& outValue )
            {
                return readFlow( outValue, []( OnlineSession& /*session*/, PlaygroundOnlineFlow& flow )
                { return flow.getFriends().getEntries().size(); } );
            }

            [[nodiscard]] static bool readRefusedCount( const GameObjectManager* /*pManager*/, float64& outValue )
            {
                return readFlow( outValue, []( OnlineSession& /*session*/, PlaygroundOnlineFlow& flow )
                { return flow.getPeopleRefusedCount(); } );
            }

            [[nodiscard]] static bool readTradeSettledCount( const GameObjectManager* /*pManager*/, float64& outValue )
            {
                return readFlow( outValue, []( OnlineSession& /*session*/, PlaygroundOnlineFlow& flow )
                { return flow.getTradeSettledCount(); } );
            }
        };
    } // namespace

    SW_DEV_COMMAND( PlaygroundOnlineFriend, "playground.online.friend", "playground.online.friend request <display name> | accept | list", "Ask a player to be a friend, accept every incoming request, or refresh the friend list",
                    &OnlinePeopleInternal::runFriend );
    SW_DEV_COMMAND( PlaygroundOnlineTrade, "playground.online.trade", "playground.online.trade invite <display name> | accept | offer <gold> | lock | confirm | cancel", "Run one step of a gold trade with a player who is online",
                    &OnlinePeopleInternal::runTrade );

    SW_AUTOMATION_WATCH( playgroundFriendCount, "Playground.FriendCount", "Friends in the friend list model", &OnlinePeopleInternal::readFriendCount );
    SW_AUTOMATION_WATCH( playgroundPeopleRefusedCount, "Playground.PeopleRefusedCount", "Friend requests and trade calls the server refused", &OnlinePeopleInternal::readRefusedCount );
    SW_AUTOMATION_WATCH( playgroundTradeSettledCount, "Playground.TradeSettledCount", "Trades the server has settled for this player", &OnlinePeopleInternal::readTradeSettledCount );
} // namespace sw
