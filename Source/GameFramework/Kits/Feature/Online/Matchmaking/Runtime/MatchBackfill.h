/**
 * @file MatchBackfill.h
 * @brief 전용 게임 서버가 경기 중 빈 슬롯(백필)을 맡기는 창구입니다. 게임 모듈이 include 하므로 공유 키트(Runtime)에 둡니다 — 구현은 서버 키트(`MatchBackfillSender`)입니다.
 * @details - 게임 서버 코드는 `IServerKitLocator`(ModuleAPI)로 이 인터페이스를 찾는다(`ServerKitDependencies<…, ServerKitOptional<IMatchBackfillSender>>`). 매칭 키트가 `gameServer` 로 조립된 전용 서버에만 있다.
 *          - 게임이 아는 것은 "누가 나갔다" 뿐이다 — 경기 id · 모드 · 팀은 게임 서버가 읽은 명단 기록(`MatchServerAgent`)이 안다(`submitPlayerLeft`). 게임이 팀 · 경기를 적어 다른 경기를 채우지 못한다.
 *          PlayFab Matchmaking 의 `CreateServerBackfillTicket`, GameLift FlexMatch 의 `StartMatchBackfill` 에 해당한다.
 */
#pragma once
#include "Core/Container/string.h"
#include "Core/Container/vector.h"
#include "Core/Foundation/Types.h"
#include "Core/Time/TimePoint.h"

#include "GameFramework/Kits/Feature/Online/Matchmaking/Runtime/MatchmakingTypes.h"
#include "GameFramework/Runtime/Online/Service/OnlineServerKitName.h"

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
        static constexpr const utf8* kProviderKit = OnlineServerKitName::kMatchmaking; ///< 이 인터페이스를 내는 키트다. 받는 쪽의 의존 이름이 여기서 나온다

        IMatchBackfillSender()          = default;
        virtual ~IMatchBackfillSender() = default;

        IMatchBackfillSender( const IMatchBackfillSender& )            = delete;
        IMatchBackfillSender& operator=( const IMatchBackfillSender& ) = delete;

        /** @brief 모르는 모드는 UnknownMode, 모드의 모양에 맞지 않으면 kInvalidRequest 입니다. Ok 이면 권한 매처가 채운 사람에게 Found 를 보내고 이 서버가 명단 기록으로 받아들입니다. */
        [[nodiscard]] virtual MatchmakingServiceResult submitBackfill( const MatchBackfillSlots& slots, UnixMilliseconds nowMs ) = 0;
        /**
         * @brief 경기에서 나간 계정 @p leftAccountID 의 자리 하나를 채워 달라고 맡깁니다. 경기 · 모드 · 팀 · 남은 사람의 평균 실력은 이 서버가 읽은 명단에서 찾습니다.
         * @return 이 서버의 명단에 없는 계정(배정 없이 들어온 개발 접속, 이미 맡긴 자리)이면 NotQueued 이고 아무것도 맡기지 않습니다. 같은 계정의 자리는 한 번만 맡깁니다.
         */
        [[nodiscard]] virtual MatchmakingServiceResult submitPlayerLeft( AccountID leftAccountID, UnixMilliseconds nowMs ) = 0;
    };
} // namespace sw
