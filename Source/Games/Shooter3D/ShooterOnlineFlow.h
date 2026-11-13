/**
 * @file ShooterOnlineFlow.h
 * @brief Shooter3D 의 온라인 흐름입니다. 세션(`OnlineSession`)으로 로그인 → 우편 목록 → 모두 받기 → 지갑 맞추기, 그리고 로그인 → 매칭 → 순위표를 잇는 게임 쪽 코드이자,
 *        게임이 온라인 키트를 어떻게 쓰는지 보여 주는 견본입니다.
 * @details 게임이 쓰는 것은 세션 하나와 키트 클라이언트의 `call<>` 뿐입니다. 연결 · 틱 · 키트 묶기는 세션이 맡습니다.
 *          우편 수령으로 바뀐 잔액은 서버가 잔액 변경 알림으로 보내 지갑(`EconomyClient`)이 스스로 맞는다 — 게임은 응답의 잔액을 옮기지 않습니다.
 *          매칭은 혼자 서는 모드(`arena_solo`, `shooter3d.matchmodes.xml`)에 줄을 서고 배정 알림(`MatchAssignment`)을 받는다. 배정된 게임 서버 접속(계정 키트의 접속 티켓 → UDP)은
 *          두 프로세스 접속 시나리오(`Scripts/net/NetPairRun.py`)의 몫이라 여기서는 배정까지 본다. 한 판의 처치 수는 순위표 `arena_kills` 에 내고 상위 목록을 받는다.
 *          화면은 아직 없고 개발 명령(`shooter.online.*`)과 감시 값(`Shooter3D.Online*`)으로 시나리오가 끝단을 판정합니다(`automation/onlinesession.scenario.xml` · `onlinematch.scenario.xml`).
 */
#pragma once
#include "Core/Container/string.h"
#include "Core/Container/vector.h"
#include "Core/Event/ConsumerLock.h"
#include "Core/Foundation/Types.h"

#include "GameFramework/Kits/Feature/Online/Account/Runtime/API/AccountClient.h"
#include "GameFramework/Kits/Feature/Online/Leaderboard/Runtime/LeaderboardProtocol.h"
#include "GameFramework/Kits/Feature/Online/Mailbox/Runtime/MailboxProtocol.h"
#include "GameFramework/Kits/Feature/Online/Matchmaking/Runtime/MatchmakingProtocol.h"
#include "GameFramework/Runtime/Online/Service/OnlineProtocolClient.h"

namespace sw
{
    class OnlineSession;

    /**
     * @class ShooterOnlineFlow
     * @brief 온라인 흐름 하나입니다. 게임이 멤버로 들고 게임 범위에 겁니다. 게임 스레드에서 씁니다.
     */
    class ShooterOnlineFlow final
    {
    public:
        static constexpr const utf8* kAssetGold   = "cur.gold";
        static constexpr const utf8* kMatchMode   = "arena_solo";  ///< `game/shooter3d/data/shooter3d.matchmodes.xml`
        static constexpr const utf8* kMatchRegion = "local";       ///< 서버 온라인 설정의 `_region` 기본값
        static constexpr const utf8* kBoardKills  = "arena_kills"; ///< `game/shooter3d/data/shooter3d.leaderboards.xml`
        static constexpr int32       kTopCount    = 10;

        ShooterOnlineFlow();

        /** @brief 계정 응답을 꺼내 실패를 알리고 매칭 배정 알림을 받습니다. 게임이 프레임마다 부릅니다. */
        void update( OnlineSession& session );

        void requestLogin( OnlineSession& session, string_view loginName, string_view password );
        void requestMailList( OnlineSession& session );
        void requestClaimAll( OnlineSession& session );
        /** @brief 매칭 모드 `kMatchMode` 에 줄을 섭니다. 배정은 알림으로 와서 `update` 가 받습니다. */
        void requestMatch( OnlineSession& session );
        /** @brief 한 판의 처치 수를 순위표 `kBoardKills` 에 냅니다(가장 좋은 기록이 남는다). */
        void requestSubmitKills( OnlineSession& session, int64 kills );
        /** @brief 순위표 `kBoardKills` 의 상위 `kTopCount` 를 받습니다. */
        void requestTopKills( OnlineSession& session );
        /** @brief 프로세스 안 서버의 저장소에 넣은 금화 우편이 끝났을 때 부릅니다. 넣기는 서버 코드(`Server/ShooterSelfMail.cpp`)의 개발 명령이라 클라이언트 구성에는 없습니다. */
        void  recordSelfMailSent() { ++_selfMailSentCount; }
        int32 getSelfMailSentCount() const { return _selfMailSentCount; }
        /** @brief 받은 배정의 경기 id 입니다. 아직 없으면 0 입니다. */
        uint64 getMatchID() const { return _matchID; }
        /** @brief 마지막으로 받은 순위표 상위 목록입니다. */
        const vector<LeaderboardEntry>& getTopKills() const { return _listTopKills; }
        /** @brief 서버가 적용했다고 답한 점수 제출 수입니다. */
        int32 getSubmittedScoreCount() const { return _submittedScoreCount; }

    private:
        void onClaimAll( const OnlineResult<MailboxClaimReply>& result );
        void onScoreSubmitted( const OnlineResult<LeaderboardSubmitReply>& result );
        void onTopKills( const OnlineResult<LeaderboardEntriesReply>& result );
        /** @brief 매칭 클라이언트의 배정 알림을 꺼냅니다. 처음 부를 때 알림 표를 받습니다. */
        void pollMatches( OnlineSession& session );

    private:
        vector<AccountClientReply> _listReplyScratch;
        vector<MatchAssignment>    _listAssignmentScratch;
        vector<LeaderboardEntry>   _listTopKills;
        ConsumerLease              _matchLease; ///< 매칭 클라이언트의 알림 표(세션의 클라이언트가 먼저 사라지면 게이트가 표를 놓는다)
        uint64                     _matchID;
        int32                      _submittedScoreCount;
        int32                      _selfMailSentCount;
    };
} // namespace sw
