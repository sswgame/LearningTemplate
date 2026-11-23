/**
 * @file PlaygroundOnlineFlow.h
 * @brief Playground 의 온라인 흐름입니다. 세션(`OnlineSession`)으로 로그인 → 우편 목록 → 모두 받기 → 지갑 맞추기, 그리고 로그인 → 매칭 → 순위표를 잇는 게임 쪽 코드이자,
 *        게임이 온라인 키트를 어떻게 쓰는지 보여 주는 견본입니다.
 * @details 게임이 쓰는 것은 세션 하나와 키트 클라이언트의 `call<>` 뿐입니다. 연결 · 틱 · 키트 묶기는 세션이 맡습니다.
 *          우편 수령으로 바뀐 잔액은 서버가 잔액 변경 알림으로 보내 지갑(`EconomyClient`)이 스스로 맞는다 — 게임은 응답의 잔액을 옮기지 않습니다.
 *          **매치 세션**: 매치형 미니게임(`minigames/shooter3d_match`)의 매칭 창구(`IMinigameMatchmaking`)다. 입구가 열면 정의의 매칭 모드(`playground.matchmodes.xml`)에 줄을 서고, 배정 알림
 *          (`MatchAssignment`)을 받으면 허브 세션을 떠나 매치 세션으로 옮긴 뒤 그 미니게임으로 들어간다. 접속한 허브의 클라이언트는 배정된 게임 서버의 접속 티켓을 받아 그 서버에 붙고,
 *          혼자 하는 허브는 옮길 세션이 없어 이 프로세스에서 판을 돈다(혼자 서는 모드). 판이 끝나면 점수를 정의의 순위표에 내고, 접속했던 허브로 새 티켓을 받아 돌아온다.
 *          줄 · 배정 · 접속 허가(티켓 · 명단)는 서버가 판정한다 — 이 흐름은 요청하고 옮길 뿐이다.
 *          화면은 아직 없고 개발 명령(`playground.online.*`)과 감시 값(`Playground.Online*`)으로 시나리오가 끝단을 판정합니다(`automation/shooter3d_onlinesession.scenario.xml` · `shooter3d_onlinematch.scenario.xml`).
 */
#pragma once
#include "Core/Container/string.h"
#include "Core/Container/vector.h"
#include "Core/Delegate/Subscription.h"
#include "Core/Event/ConsumerLock.h"
#include "Core/Foundation/Types.h"
#include "Core/Memory/Memory.h"
#include "Core/Name/hashed_string.h"
#include "Core/Network/NetTypes.h"

#include "GameFramework/Kits/Feature/Online/Account/Runtime/AccountClient.h"
#include "GameFramework/Kits/Feature/Online/Leaderboard/Runtime/LeaderboardProtocol.h"
#include "GameFramework/Kits/Feature/Online/Mailbox/Runtime/MailboxProtocol.h"
#include "GameFramework/Kits/Feature/Online/Matchmaking/Runtime/MatchmakingProtocol.h"
#include "GameFramework/Kits/Feature/Online/Matchmaking/Runtime/MatchmakingTypes.h"
#include "GameFramework/Runtime/Foundation/Framework/Minigame/IMinigameMatchmaking.h"
#include "GameFramework/Runtime/Online/Service/OnlineProtocolClient.h"

namespace sw
{
    class EconomyShopPurchaseProvider;
    class GameNetSession;
    class MinigameFlow;
    class OfferCatalog;
    class OnlineSession;
    struct MinigameResult;
    struct NetGameTicket;

    /** @brief 매치 세션 하나의 걸음입니다. 매치는 한 번에 하나다. */
    enum class PlaygroundMatchState : uint8
    {
        None = 0,    ///< 매치가 없다
        Queued,      ///< 줄에 섰다 — 배정을 기다린다(허브에 그대로 있다)
        MatchTicket, ///< 배정됐다 — 매치 서버의 접속 티켓을 기다린다(접속한 허브에서만)
        Playing,     ///< 매치 미니게임 안이다
        Leaving,     ///< 판이 끝났다 — 허브 씬이 서기를 기다린다
        HubTicket,   ///< 허브 씬이 섰다 — 허브 서버의 접속 티켓을 기다린다
    };

    /**
     * @class PlaygroundOnlineFlow
     * @brief 온라인 흐름 하나입니다. 게임이 멤버로 들고 게임 범위에 겁니다. 게임 스레드에서 씁니다.
     */
    class PlaygroundOnlineFlow final : public IMinigameMatchmaking
    {
    public:
        static constexpr const utf8* kAssetGold    = "cur.gold";
        static constexpr const utf8* kAssetGem     = "cur.gem";
        static constexpr const utf8* kMatchRegion  = "local";                                      ///< 서버 온라인 설정의 `_region` 기본값
        static constexpr const utf8* kBoardKills   = "arena_kills";                                ///< `game/playground/data/playground.leaderboards.xml`
        static constexpr const utf8* kOfferCatalog = "game/playground/data/playground.offers.xml"; ///< 상점 화면의 상품(서버 경제 키트와 같은 파일 — `Config/Server/Online/Playground.online.json`)
        static constexpr int32       kTopCount     = 10;

        PlaygroundOnlineFlow();
        ~PlaygroundOnlineFlow() override;

        /** @brief 매치 결과를 받게 미니게임 흐름에 겁니다. 게임이 초기화에서 한 번 부릅니다. */
        void initialize( MinigameFlow& minigames );
        /** @brief 계정 응답을 꺼내 실패를 알리고, 매칭 배정과 매치 세션의 걸음을 돌립니다. 게임이 프레임마다(틱 밖) 부릅니다. */
        void update( OnlineSession& session );

        // IMinigameMatchmaking
        [[nodiscard]] bool requestMatch( const MinigameDefinition& definition ) override;
        [[nodiscard]] bool isLocalPlayer( const GameObject& activator ) const override;

        void requestLogin( OnlineSession& session, string_view loginName, string_view password );
        void requestMailList( OnlineSession& session );
        void requestClaimAll( OnlineSession& session );
        /** @brief 한 판의 처치 수를 순위표 `kBoardKills` 에 냅니다(가장 좋은 기록이 남는다). */
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

    private:
        void onClaimAll( const OnlineResult<MailboxClaimReply>& result );
        void onScoreSubmitted( const OnlineResult<LeaderboardSubmitReply>& result );
        void onTopKills( const OnlineResult<LeaderboardEntriesReply>& result );
        void onQueueJoined( const OnlineResult<MatchmakingTicketReply>& result );
        /** @brief 미니게임이 끝났습니다(돌아올 자리를 복원한 뒤). 매치면 점수를 정의의 순위표에 냅니다 — 점수 없이 나왔으면 내지 않는다. */
        void onMinigameFinished( const MinigameResult& result );
        /** @brief 매칭 클라이언트의 배정 알림을 꺼냅니다. 처음 부를 때 알림 표를 받습니다. */
        void pollMatches( OnlineSession& session );
        /** @brief 배정된 매치로 옮기기 시작합니다 — 접속한 허브면 매치 서버의 티켓을 청하고, 혼자 하는 허브면 바로 들어갑니다. */
        void beginMatch( OnlineSession& session );
        /** @brief 배정된 매치의 미니게임으로 들어갑니다. 지면 매치를 버리고 허브로 돌아갑니다. */
        void enterMatch( OnlineSession& session );
        /** @brief 티켓이 왔습니다 — 기다리던 세션(매치 · 허브)에 붙습니다. */
        void onGameTicket( OnlineSession& session, const NetGameTicket& ticket );
        /** @brief 티켓을 받지 못했습니다 — 매치 전이면 허브에 남고, 돌아오는 길이면 혼자 하는 허브로 남습니다. */
        void onGameTicketRefused();
        /** @brief 매치 미니게임에서 나왔는지 보고 허브 세션으로 돌아갑니다. */
        void updateMatchExit( OnlineSession& session );
        /** @brief 떠나온 허브에 다시 붙습니다(티켓으로 붙었던 허브면 새 티켓을 청한다). 떠나온 허브가 없으면 매치를 끝낸다. */
        void reconnectHub( OnlineSession& session );
        /** @brief 매치를 잊습니다(줄 · 배정 · 떠나온 허브). */
        void clearMatch();

    private:
        vector<AccountClientReply>              _listReplyScratch;
        vector<MatchAssignment>                 _listAssignmentScratch;
        vector<LeaderboardEntry>                _listTopKills;
        shared_ptr<const OfferCatalog>          _offers;               ///< 상점 상품(처음 상점을 열 때 읽는다)
        unique_ptr<EconomyShopPurchaseProvider> _shopProvider;         ///< 상점 화면의 온라인 공급자(`findShopProvider`)
        ConsumerLease                           _matchLease;           ///< 매칭 클라이언트의 알림 표(세션의 클라이언트가 먼저 사라지면 게이트가 표를 놓는다)
        Subscription                            _finishedSubscription; ///< 미니게임 결과 구독(`initialize`)
        MatchAssignment                         _assignment;           ///< 받은 배정(`_matchID` 가 0 이면 없다)
        hashed_string                           _matchMinigame;        ///< 줄에 선 매치형 미니게임
        NetAddress                              _hubAddress;           ///< 매치로 옮기며 떠나온 허브 서버(혼자 하는 허브면 빈 주소)
        int32                                   _submittedScoreCount;
        int32                                   _selfMailSentCount;
        PlaygroundMatchState                    _matchState;
        uint8                                   _bHubTicket : 1; ///< 떠나온 허브에 접속 티켓으로 붙어 있었다
    };
} // namespace sw
