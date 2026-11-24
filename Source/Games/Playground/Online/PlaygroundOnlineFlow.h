/**
 * @file PlaygroundOnlineFlow.h
 * @brief Playground 의 온라인 흐름입니다. 세션(`OnlineSession`)으로 로그인 → 우편 목록 → 모두 받기 → 지갑 맞추기, 그리고 로그인 → 매칭 → 순위표를 잇는 게임 쪽 코드이자,
 *        게임이 온라인 키트를 어떻게 쓰는지 보여 주는 견본입니다.
 * @details 게임이 쓰는 것은 세션 하나와 키트 클라이언트의 `call<>` 뿐입니다. 연결 · 틱 · 키트 묶기는 세션이 맡습니다.
 *          우편 수령으로 바뀐 잔액은 서버가 잔액 변경 알림으로 보내 지갑(`EconomyClient`)이 스스로 맞는다 — 게임은 응답의 잔액을 옮기지 않습니다.
 *          **매치 세션**: 매치형 미니게임(`minigames/shooter3d_match`)의 매칭 창구(`IMinigameMatchmaking`)다. 입구가 열면 정의의 매칭 모드(`playground.matchmodes.xml`)에 줄을 서고, 배정 알림
 *          (`MatchAssignment`)을 받으면 배정된 게임 서버의 접속 티켓을 청해 허브 세션을 떠나 그 서버에 붙은 뒤 그 미니게임으로 들어간다. 혼자 하는 허브도 같다 — 판은 늘 매치 서버가 돈다.
 *          붙지 못하거나 판 도중 서버를 잃으면 허브로 돌아온다. 판이 끝나면 점수는 매치 서버가 정의의 순위표에 내고(`ILeaderboardScoreSender` — 클라이언트 제출은 그 보드가 거절한다),
 *          접속했던 허브로 새 티켓을 받아 돌아온다. 줄 · 배정 · 접속 허가(티켓 · 명단) · 점수는 서버가 판정한다 — 이 흐름은 요청하고 옮길 뿐이다.
 *          화면은 아직 없고 개발 명령(`playground.online.*`)과 감시 값(`Playground.Online*`)으로 시나리오가 끝단을 판정합니다(`automation/shooter3d_onlinesession.scenario.xml` · `shooter3d_onlinematch.scenario.xml`).
 */
#pragma once
#include "Core/Container/string.h"
#include "Core/Container/vector.h"
#include "Core/Event/ConsumerLock.h"
#include "Core/Foundation/Types.h"
#include "Core/Memory/Memory.h"
#include "Core/Name/hashed_string.h"
#include "Core/Network/NetTypes.h"

#include "Engine/Net/GameNetSession.h"

#include "GameFramework/Kits/Feature/Online/Account/Runtime/AccountClient.h"
#include "GameFramework/Kits/Feature/Online/Leaderboard/Runtime/LeaderboardProtocol.h"
#include "GameFramework/Kits/Feature/Online/Mailbox/Runtime/MailboxProtocol.h"
#include "GameFramework/Kits/Feature/Online/Matchmaking/Runtime/MatchmakingProtocol.h"
#include "GameFramework/Kits/Feature/Online/Matchmaking/Runtime/MatchmakingTypes.h"
#include "GameFramework/Kits/Feature/Online/Social/Runtime/FriendList.h"
#include "GameFramework/Kits/Feature/Online/Trade/Runtime/TradeClient.h"
#include "GameFramework/Runtime/Foundation/Framework/Minigame/IMinigameMatchmaking.h"
#include "GameFramework/Runtime/Online/Service/OnlineProtocolClient.h"

namespace sw
{
    class EconomyShopPurchaseProvider;
    class GameInstance;
    class OfferCatalog;
    class OnlineSession;

    /** @brief 매치 세션 하나의 걸음입니다. 매치는 한 번에 하나다. */
    enum class PlaygroundMatchState : uint8
    {
        None = 0,     ///< 매치가 없다
        Queued,       ///< 줄에 섰다 — 배정을 기다린다(허브에 그대로 있다)
        MatchTicket,  ///< 배정됐다 — 매치 서버의 접속 티켓을 기다린다
        MatchConnect, ///< 허브 세션을 떠났다 — 매치 서버에 붙기를 기다린다
        Playing,      ///< 매치 미니게임 안이다
        Leaving,      ///< 판이 끝났다 — 허브 씬이 서기를 기다린다
        HubLost,      ///< 허브 서버를 잃었다 — 씬의 처리기가 내려간 다음 프레임에 다시 붙거나 세션 전 자리로 돌아간다
        HubTicket,    ///< 허브 씬이 섰다 — 허브 서버의 접속 티켓을 기다린다
    };

    /**
     * @class PlaygroundOnlineFlow
     * @brief 온라인 흐름 하나입니다. 게임이 멤버로 들고 게임 범위에 겁니다. 게임 스레드에서 씁니다.
     */
    class PlaygroundOnlineFlow final : public IMinigameMatchmaking
    {
    public:
        static constexpr const utf8* kAssetGold          = "cur.gold";
        static constexpr const utf8* kAssetGem           = "cur.gem";
        static constexpr const utf8* kMatchRegion        = "local";                                      ///< 서버 온라인 설정의 `_region` 기본값
        static constexpr const utf8* kBoardKills         = "arena_kills";                                ///< `game/playground/data/playground.leaderboards.xml`
        static constexpr const utf8* kOfferCatalog       = "game/playground/data/playground.offers.xml"; ///< 상점 화면의 상품(서버 경제 키트와 같은 파일 — `Config/Server/Online/Playground.online.json`)
        static constexpr int32       kTopCount           = 10;
        static constexpr const utf8* kResultDisconnected = "disconnected"; ///< 판 도중 서버를 잃고 나온 결과 이름

        PlaygroundOnlineFlow();
        ~PlaygroundOnlineFlow() override;

        /** @brief 계정 응답을 꺼내 실패를 알리고, 매칭 배정과 매치 세션의 걸음을 돌립니다. 게임이 프레임마다(틱 밖) 부릅니다. */
        void update( GameInstance& instance );

        // IMinigameMatchmaking
        [[nodiscard]] bool requestMatch( const MinigameDefinition& definition ) override;
        [[nodiscard]] bool isLocalPlayer( const GameObject& activator ) const override;

        void requestLogin( OnlineSession& session, string_view loginName, string_view password );
        // 허브 사람들 — 친구(소셜 키트)와 거래(거래 키트). 개발 명령은 `OnlinePeople.cpp` 다.
        /** @brief 친구 목록 모델입니다 — 게임이 게임 범위에 걸어 친구 화면(`social.friendlist`)이 빌린다. 세션의 친구 클라이언트가 서면 `update` 가 붙인다. */
        FriendList&       getFriends() { return _friends; }
        const FriendList& getFriends() const { return _friends; }
        /** @brief 표시 이름 @p displayName 에게 친구를 신청합니다. 이름은 서버가 찾는다 — 없으면 거절(`getPeopleRefusedCount`). */
        void requestFriend( OnlineSession& session, string_view displayName );
        /** @brief 받은 친구 신청을 모두 받습니다. */
        void requestAcceptFriends( OnlineSession& session );
        /** @brief 친구 신청 · 거래 요청이 거절된 수입니다. */
        int32 getPeopleRefusedCount() const { return _peopleRefusedCount; }
        /** @brief 정산된 거래 수입니다(서버 원장이 한 번에 옮겼다). */
        int32 getTradeSettledCount() const { return _tradeSettledCount; }
        void  requestMailList( OnlineSession& session );
        void  requestClaimAll( OnlineSession& session );
        /** @brief 허브 서버 @p address 에 접속 티켓(gamesettings `onlineHubServerID`)으로 붙습니다 — 티켓을 청하고 답이 오면 세션을 엽니다. 허브 이름이 없거나 매치 중이면 알리고 false 입니다. */
        [[nodiscard]] bool requestJoinHub( OnlineSession& session, const NetAddress& address );
        /** @brief 처치 수를 순위표 `kBoardKills` 에 클라이언트로서 냅니다 — 그 보드는 서버 제출만 받아 거절된다(거절을 보는 개발 명령이 부른다). */
        void requestSubmitKills( OnlineSession& session, int64 kills );
        /** @brief 순위표 `kBoardKills` 의 상위 `kTopCount` 를 받습니다. */
        void requestTopKills( OnlineSession& session );
        /** @brief 열린 기간 이벤트를 받습니다(로그인 뒤 — 출시 비율이 계정을 본다). 목록은 세션의 라이브 운영 클라이언트가 든다. */
        static void requestLiveEvents( OnlineSession& session );
        /** @brief 서비스 공지와 점검 창을 받습니다(로그인 전에도 된다). 상태는 세션의 서버 디렉터리 클라이언트가 든다. */
        static void requestServiceStatus( OnlineSession& session );
        /** @brief 프로세스 안 서버의 저장소에 넣은 금화 우편이 끝났을 때 부릅니다. 넣기는 서버 코드(`Server/OnlineSelfMail.cpp`)의 개발 명령이라 클라이언트 구성에는 없습니다. */
        void  recordSelfMailSent() { ++_selfMailSentCount; }
        int32 getSelfMailSentCount() const { return _selfMailSentCount; }
        /**
         * @brief 상점 화면(`ShopScreen`)에 꽂을 온라인 구매 공급자입니다. 처음 부를 때 세션의 경제 클라이언트와 상품 카탈로그(`kOfferCatalog`)로 세운다. 카탈로그를 읽지 못하면 nullptr 입니다.
         * @details 공급자는 이 흐름(게임 범위)이 든다 — 화면은 빌려 쓰고, 게임이 내려가면 늦게 온 답은 공급자의 약한 참조가 버린다.
         */
        EconomyShopPurchaseProvider* findShopProvider( OnlineSession& session );
        /** @brief 받은 배정의 경기 id 입니다. 아직 없으면 0 입니다. */
        uint64 getMatchID() const { return _assignment._matchID; }
        /** @brief 매치 세션의 걸음입니다. */
        PlaygroundMatchState getMatchState() const { return _matchState; }
        /** @brief 마지막으로 받은 순위표 상위 목록입니다. */
        const vector<LeaderboardEntry>& getTopKills() const { return _listTopKills; }
        /** @brief 서버가 적용했다고 답한 점수 제출 수입니다. */
        int32 getSubmittedScoreCount() const { return _submittedScoreCount; }
        /** @brief 순위표가 클라이언트 제출이라 거절한 수입니다. */
        int32 getRefusedScoreCount() const { return _refusedScoreCount; }

        /**
         * @brief 잃은 허브에 다시 붙을지입니다 — 시간 초과 · 줄 넘침(회선 · 서버가 죽음, 앱이 멈춘 동안 밀림)과 서버 재시작이면 붙고, 점검 · 운영자 종료 · 추방이면 붙지 않습니다.
         * @details 까닭 없이 서버가 닫은 것(`Remote`)은 추방이나 같은 계정의 다른 접속일 수 있다 — 저절로 다시 붙으면 두 쪽이 서로 밀어낸다.
         */
        static bool shouldReconnectHub( NetDisconnectReason reason, const optional<GameShutdownNotice>& notification );

    private:
        void onClaimAll( const OnlineResult<MailboxClaimReply>& result );
        void onScoreSubmitted( const OnlineResult<LeaderboardSubmitReply>& result );
        void onTopKills( const OnlineResult<LeaderboardEntriesReply>& result );
        void onFriendRequested( const OnlineResult<SocialLinkReply>& result );
        void onFriendAnswered( const OnlineResult<SocialLinkReply>& result );
        void onLinksToAccept( const OnlineResult<SocialLinksReply>& result );
        /** @brief 친구 알림을 모델에 반영하고 거래의 답 · 알림을 꺼냅니다. */
        void updatePeople( OnlineSession& session );
        void onQueueJoined( const OnlineResult<MatchmakingTicketReply>& result );
        /** @brief 매칭 클라이언트의 배정 알림을 꺼냅니다. 처음 부를 때 알림 표를 받습니다. */
        void pollMatches( OnlineSession& session );
        /** @brief 배정된 매치로 옮기기 시작합니다 — 지금 허브를 적어 두고 매치 서버의 티켓을 청합니다. */
        void beginMatch( OnlineSession& session );
        /** @brief 매치 서버에 붙었는지 봅니다 — 붙었으면 미니게임으로 들어가고, 붙지 못했으면 허브로 돌아갑니다. */
        void updateMatchConnect( OnlineSession& session );
        /** @brief 티켓이 왔습니다 — 씬의 링크를 떼고 티켓을 맡겨 둡니다(다음 `update` 가 연다). */
        void onGameTicket( OnlineSession& session, const NetGameTicket& ticket );
        /** @brief 맡겨 둔 티켓으로 기다리던 세션(매치 · 허브)을 엽니다. */
        void openTicketSession( OnlineSession& session );
        /** @brief 티켓을 받지 못했습니다 — 매치 전이면 허브에 남고, 돌아오는 길이면 서버의 허브를 떠납니다. */
        void onGameTicketRefused();
        /** @brief 매치 미니게임에서 나왔는지 보고(판 도중 서버를 잃으면 나오게 하고) 허브 세션으로 돌아갑니다. */
        void updateMatchExit( OnlineSession& session );
        /** @brief 허브 서버를 잃었는지(거절당했는지) 봅니다 — 다시 붙거나(`shouldReconnectHub`) 세션 전 자리로 돌아갑니다(`GameInstance::leaveServerWorld`). */
        void updateHubLoss( OnlineSession& session, GameInstance& instance );
        /** @brief 떠나온 허브에 다시 붙을 수 있는지입니다 — 한도(`onlineHubReconnectSeconds`)가 있고, 티켓으로 붙었던 허브는 로그인과 허브 서버 이름이 있어야 새 티켓을 청한다. */
        [[nodiscard]] bool canReturnToHub( OnlineSession& session ) const;
        /** @brief 서버의 허브를 떠나기 시작합니다 — 링크를 먼저 떼고, 씬의 처리기가 내려간 다음 `update` 가 세션을 닫고 세션 전 자리로 보낸다. */
        void leaveHub();
        /** @brief 떠나온 허브에 다시 붙습니다(티켓으로 붙었던 허브면 새 티켓을 청한다). 떠나온 허브가 없으면 매치를 끝낸다. */
        void reconnectHub( OnlineSession& session );
        /** @brief 매치를 잊습니다(줄 · 배정 · 떠나온 허브). */
        void clearMatch();

    private:
        vector<AccountClientReply>              _listReplyScratch;
        vector<MatchAssignment>                 _listAssignmentScratch;
        vector<LeaderboardEntry>                _listTopKills;
        shared_ptr<const OfferCatalog>          _offers;       ///< 상점 상품(처음 상점을 열 때 읽는다)
        unique_ptr<EconomyShopPurchaseProvider> _shopProvider; ///< 상점 화면의 온라인 공급자(`findShopProvider`)
        FriendList                              _friends;      ///< 친구 목록 모델(`update` 가 친구 클라이언트에 붙인다)
        vector<TradeClientReply>                _listTradeReplyScratch;
        vector<TradeClientUpdate>               _listTradeUpdateScratch;
        ConsumerLease                           _matchLease;          ///< 매칭 클라이언트의 알림 표(세션의 클라이언트가 먼저 사라지면 게이트가 표를 놓는다)
        MatchAssignment                         _assignment;          ///< 받은 배정(`_matchID` 가 0 이면 없다)
        optional<NetGameTicket>                 _pendingTicket;       ///< 받은 접속 티켓 — 앞 세션의 처리기가 내려간 다음 프레임에 연다
        hashed_string                           _matchMinigame;       ///< 줄에 선 매치형 미니게임
        NetAddress                              _hubAddress;          ///< 매치로 옮기며 떠나온 허브 서버(혼자 하는 허브면 빈 주소)
        int64                                   _reconnectDeadline;   ///< 허브에 다시 붙기를 그만둘 시각(단조 시계 ns). 다시 붙는 중이 아니면 0
        uint64                                  _shownFriendRevision; ///< 로그에 남긴 친구 목록의 판
        int32                                   _submittedScoreCount;
        int32                                   _refusedScoreCount;
        int32                                   _peopleRefusedCount;
        int32                                   _tradeSettledCount;
        int32                                   _selfMailSentCount;
        PlaygroundMatchState                    _matchState;
        uint8                                   _bHubTicket    : 1; ///< 떠나온 허브에 접속 티켓으로 붙어 있었다
        uint8                                   _bReconnectHub : 1; ///< 잃은 허브에 다시 붙는다(`HubLost`)
        uint8                                   _bFriendsBound : 1; ///< `_friends` 가 친구 클라이언트에 붙었다
    };
} // namespace sw
