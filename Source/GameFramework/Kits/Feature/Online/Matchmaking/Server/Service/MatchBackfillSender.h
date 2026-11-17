/**
 * @file MatchBackfillSender.h
 * @brief 전용 게임 서버가 경기 중 빈 슬롯(백필)을 권한 매처에 맡기는 입구입니다(감사 O-6).
 * @details - 게임 서버가 아는 것은 경기 · 모드 · 팀마다 빈 슬롯 · 남은 사람의 평균 실력뿐이다. 채운 사람이 접속할 서버 id · 지역 · 주소 · 포트는 이 서버의 설명(`ServerDescriptor`,
 *            디렉터리 등록과 같은 값)에서 채운다 — 호출자가 다른 서버를 적어 사람을 엉뚱한 곳으로 보낼 수 없다.
 *          - 매칭 키트가 `gameServer` 로 조립되면 `IMatchBackfillSender` 로 건다. 다른 키트 · 게임 서버 조립이 `find<IMatchBackfillSender>` 로 찾는다.
 *          PlayFab Matchmaking 의 `CreateServerBackfillTicket`(서버 정보는 게임 서버가 아는 자기 정보), Agones · GameLift FlexMatch 의 `StartMatchBackfill` 에 해당한다.
 */
#pragma once
#include "Core/Container/string.h"
#include "Core/Container/vector.h"
#include "Core/Foundation/Types.h"
#include "Core/Time/TimePoint.h"

#include "GameFramework/GameFrameworkExports.h"
#include "GameFramework/Kits/Feature/Online/Matchmaking/Runtime/MatchmakingTypes.h"
#include "GameFramework/Runtime/Online/Directory/ServerRecord.h"

namespace sw
{
    class MatchQueueService;
} // namespace sw

namespace sw
{
    /** @brief 게임 서버가 채울 빈 슬롯입니다. 서버 정보는 담지 않습니다. */
    struct MatchBackfillSlots
    {
        vector<int32> _listOpenSlot{}; ///< 팀마다 빈 슬롯 수이다. 모드의 팀 수와 같아야 한다
        string        _modeID{};
        uint64        _matchID{ 0 };
        int32         _averageRating{ 0 }; ///< 남은 사람들의 평균 실력이다
    };
} // namespace sw

namespace sw
{
    /**
     * @class IMatchBackfillSender
     * @brief 경기 중 빈 슬롯을 맡기는 인터페이스입니다. 서비스 스레드(서버 루프)에서 부릅니다.
     */
    class IMatchBackfillSender
    {
    public:
        IMatchBackfillSender()          = default;
        virtual ~IMatchBackfillSender() = default;

        IMatchBackfillSender( const IMatchBackfillSender& )            = delete;
        IMatchBackfillSender& operator=( const IMatchBackfillSender& ) = delete;

        /** @brief 모르는 모드는 UnknownMode, 모드의 모양에 맞지 않으면 kInvalidRequest 입니다. Ok 이면 권한 매처가 채운 사람에게 Found 를 보내고 이 서버가 명단 기록으로 받아들입니다. */
        [[nodiscard]] virtual MatchmakingServiceResult submitBackfill( const MatchBackfillSlots& slots, UnixMilliseconds nowMs ) = 0;
    };
} // namespace sw

namespace sw
{
    /**
     * @class MatchBackfillSender
     * @brief 이 게임 서버의 설명으로 백필 요청을 채워 대기열에 맡깁니다.
     */
    class SW_KIT_SERVER_API MatchBackfillSender final : public IMatchBackfillSender
    {
    public:
        MatchBackfillSender();

        /** @brief 대기열(빌려 쓴다, 필수)과 이 서버의 설명을 받습니다. */
        void initialize( MatchQueueService* pQueue, const ServerDescriptor& descriptor );

        [[nodiscard]] MatchmakingServiceResult submitBackfill( const MatchBackfillSlots& slots, UnixMilliseconds nowMs ) override;

    private:
        ServerDescriptor   _descriptor;
        MatchQueueService* _pQueue;
    };
} // namespace sw
