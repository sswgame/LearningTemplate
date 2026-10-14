/**
 * @file LeaderboardClient.h
 * @brief 순위표 키트의 클라이언트입니다. `OnlineServiceClient` 에 등록하는 `IOnlineClientService` 이며, 상위 순위, 내 주변 순위, 통계, 점수 제출, 업적, 시즌 결과를 요청하고 업적 달성 알림을 쌓아 둡니다.
 * @details - 요청마다 결과 델리게이트를 `OnlineServiceClient::tick` 스레드에서 정확히 한 번 부른다. 업무 결과는 `_reply._result` 에, 전송 오류와 공통 오류는 `_errorCode` 에 있다.
 *          - 점수 제출은 서버가 `_bClientSubmit` 이 켜진 보드만 받으며 아니면 NotAllowed 이다. 보통 점수는 서버 게임 로직이 낸다.
 */
#pragma once
#include "Core/Common/Types.h"
#include "Core/Container/EventBuffer.h"
#include "Core/Container/vector.h"
#include "Core/Delegate/Delegate.h"

#include "GameFramework/Base/Online/Service/OnlineProtocol.h"
#include "GameFramework/Base/Online/Service/OnlineServiceClient.h"
#include "GameFramework/Base/Online/Service/ServiceClientCallTable.h"
#include "GameFramework/GameFrameworkExports.h"
#include "GameFramework/Kits/Feature/Online/Leaderboard/Shared/LeaderboardProtocol.h"

namespace sw
{
    /** @brief 클라이언트 응답 하나입니다. */
    struct LeaderboardClientReply
    {
        LeaderboardReply _reply{};
        uint64           _requestID{ 0 };
        uint16           _method{ 0 };
        uint16           _errorCode{ 0 }; ///< 전송 오류나 공통 오류(`OnlineError`)이다
    };

    using LeaderboardReplyDelegate = Delegate<void( const LeaderboardClientReply& )>;
} // namespace sw

namespace sw
{
    /**
     * @class LeaderboardClient
     * @brief 연결 하나의 순위표 클라이언트입니다.
     */
    class SW_KIT_API LeaderboardClient final : public IOnlineClientService
    {
    public:
        LeaderboardClient();

        /** @brief @p pClient 는 빌려 씁니다. `registerClientService( this )` 는 호출하는 쪽이 초기화하기 전에 부릅니다. */
        void initialize( OnlineServiceClient* pClient );

        uint64 requestTop( string_view boardID, int32 offset, int32 count, const LeaderboardReplyDelegate& onReply );
        uint64 requestAround( string_view boardID, int32 radius, const LeaderboardReplyDelegate& onReply );
        uint64 requestStats( const LeaderboardReplyDelegate& onReply );
        uint64 submitScore( string_view boardID, int64 score, const LeaderboardReplyDelegate& onReply );
        uint64 requestAchievements( const LeaderboardReplyDelegate& onReply );
        uint64 requestSeasonResult( string_view boardID, uint32 seasonID, const LeaderboardReplyDelegate& onReply );

        /** @brief 쌓인 업적 달성 알림을 꺼냅니다. */
        void drainAchievementUnlocks( vector<AchievementState>& outListAchievement ) { _unlockBuffer.drainTo( outListAchievement ); }

        // IOnlineClientService
        uint16 getMethodRange() const override { return OnlineMethodRange::kLeaderboard; }
        uint32 getProtocolVersion() const override { return LeaderboardProtocol::kVersion; }
        void   onServicePush( uint16 kind, BitReader& body ) override;

    private:
        struct PendingCall
        {
            LeaderboardReplyDelegate _onReply{};
            uint16                   _method{ 0 };
        };

        uint64 send( uint16 method, const LeaderboardRequest& request, const LeaderboardReplyDelegate& onReply );
        void   onResponse( const OnlineResponse& response );

        ServiceClientCallTable<PendingCall> _callTable;
        EventBuffer<AchievementState>       _unlockBuffer;
        OnlineServiceClient*                _pClient;
    };
} // namespace sw
