/**
 * @file LeaderboardServer.h
 * @brief 순위표의 스트림 바인딩입니다. `IOnlineServerService`(영역 `kLeaderboard`)를 구현합니다.
 * @details 요청은 요청 태그와 함께 로직에 맡기고, 완료는 응답으로 보내며, 업적 달성은 그 계정에 알린다.
 *          - 응답 본문은 항상 `LeaderboardReply` 이며 업무 결과가 첫 값이다. 오류 코드는 손상된 본문(`kInvalidRequest`)뿐이다.
 *          - 점수 제출은 보드 정의의 `_bClientSubmit` 이 켜진 보드만 받는다. 아니면 로직에 넘기지 않고 NotAllowed 로 끝낸다. 점수는 서버가 낸다는 것이 사용자가 정한 정책이다.
 *            표시 이름은 이 프로세스의 계정 디렉터리(`IAccountDirectory`)에서 가져온다.
 *          - 업적 달성 알림은 받는 계정이 이 프로세스에 접속해 있지 않으면 접속 상태 관리자(`IAccountPresence::sendRemotePush`)로 보낸다.
 *          - 호스트에 등록하는 일은 호출하는 쪽이 한다(`host.registerServerService( &server )`, 호스트 `initialize` 전에).
 */
#pragma once
#include "Core/Container/vector.h"
#include "Core/Foundation/Types.h"
#include "Core/Time/TimePoint.h"

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
     * @brief 순위표 바인딩입니다. 호스트 `tick` 스레드에서 씁니다.
     */
    class SW_KIT_SERVER_API LeaderboardServer final : public IOnlineServerService
    {
    public:
        LeaderboardServer();

        /** @brief 넘긴 객체는 빌려 씁니다. @p pDirectory 는 점수 제출의 표시 이름에 쓰며 없으면 빈 이름이고, @p pPresence 는 서버가 여러 대일 때만 넘깁니다. */
        void initialize( LeaderboardService* pService, const IAccountDirectory* pDirectory, IAccountPresence* pPresence );
        void shutdown();

        // IOnlineServerService
        const OnlineProtocolEntry& getProtocol() const override;
        void                       onServiceRequest( OnlineServiceHost& host, const OnlineCallContext& context, BitReader& body ) override;
        void                       onServiceTick( OnlineServiceHost& host, UnixMilliseconds nowMs ) override;
        void                       onAccountJoined( OnlineServiceHost& host, AccountID accountID ) override;
        void                       onAccountLeft( OnlineServiceHost& host, AccountID accountID ) override;

    private:
        ServicePendingTable           _pendingTable;
        vector<LeaderboardCompletion> _listCompletionScratch;
        vector<AchievementUnlock>     _listUnlockScratch;
        LeaderboardService*           _pService;
        const IAccountDirectory*      _pDirectory;
        IAccountPresence*             _pPresence;
    };
} // namespace sw
