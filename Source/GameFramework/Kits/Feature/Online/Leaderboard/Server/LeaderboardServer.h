/**
 * @file LeaderboardServer.h
 * @brief 순위표의 스트림 바인딩 — `IOnlineService`(영역 `kLeaderboard`). 요청을 로직에 꼬리표로 맡기고, 완료를 응답으로, 업적 달성을 그 계정에게 알립니다.
 * @details - 응답 몸은 언제나 `LeaderboardReply`(업무 결과가 첫 값) — 오류 코드는 깨진 몸(`kInvalidRequest`)뿐이다.
 *          - 점수 제출은 표 정의의 `_bClientSubmit` 이 켜진 표만 — 아니면 로직에 넘기지 않고 NotAllowed(사용자 결정: 점수는 서버가 낸다).
 *            표시 이름은 이 프로세스의 계정 창구(`IAccountDirectory`)에서.
 *          - 업적 달성 알림은 받는 계정이 이 프로세스에 없으면 접속 상태 창구(`IAccountPresence::sendRemotePush`)로.
 *          - 호스트에 올리는 것은 부르는 쪽이(`host.registerService( &server )` — 호스트 `initialize` 전에).
 */
#pragma once
#include "Core/Common/Types.h"
#include "Core/Container/vector.h"

#include "GameFramework/Base/Online/Service/OnlineProtocol.h"
#include "GameFramework/Base/Online/Service/OnlineServiceHost.h"
#include "GameFramework/Base/Online/Service/ServicePendingTable.h"
#include "GameFramework/GameFrameworkExports.h"
#include "GameFramework/Kits/Feature/Online/Leaderboard/Server/LeaderboardService.h"

namespace sw
{
    class IAccountDirectory;
    class IAccountPresence;

    /**
     * @class LeaderboardServer
     * @brief 순위표 바인딩입니다(호스트 `tick` 스레드).
     */
    class SW_GF_API LeaderboardServer final : public IOnlineService
    {
    public:
        LeaderboardServer();

        /** @brief 넘긴 것은 빌려 쓴다. @p pDirectory 는 점수 제출의 표시 이름(없으면 빈 이름), @p pPresence 는 서버 여럿일 때만. */
        void initialize( LeaderboardService* pService, const IAccountDirectory* pDirectory, IAccountPresence* pPresence );
        void shutdown();

        // IOnlineService
        uint16 getMethodRange() const override { return OnlineMethodRange::kLeaderboard; }
        uint32 getProtocolVersion() const override;
        void   onServiceRequest( OnlineServiceHost& host, const OnlineCallContext& context, BitReader& body ) override;
        void   onServiceTick( OnlineServiceHost& host, int64 nowMs ) override;

    private:
        ServicePendingTable           _pendingTable;
        vector<LeaderboardCompletion> _listCompletionScratch;
        vector<AchievementUnlock>     _listUnlockScratch;
        LeaderboardService*           _pService;
        const IAccountDirectory*      _pDirectory;
        IAccountPresence*             _pPresence;
    };
} // namespace sw
