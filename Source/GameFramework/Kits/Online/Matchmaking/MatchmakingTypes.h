/**
 * @file MatchmakingTypes.h
 * @brief 매칭 키트의 타입 — 결과 · 모드 정의 · 표 · 만든 경기입니다.
 */
#pragma once
#include "Core/Common/Types.h"
#include "Core/Container/string.h"
#include "Core/Container/vector.h"

#include "GameFramework/Base/Online/Identity/AccountDirectory.h"
#include "GameFramework/GameFrameworkExports.h"

namespace sw
{
    /** @brief 매칭 요청의 결과입니다(키트 오류 코드 = `OnlineMethodRange::kMatchmaking` + 값 — 0 은 성공). 와이어 형식이다. */
    enum class MatchmakingResult : uint8
    {
        Ok = 0,
        UnknownMode,
        AlreadyQueued,
        NotQueued,
        PartyTooLarge, ///< 표의 인원이 0 이거나 팀 인원을 넘는다
        NotPartyLeader,
        // 파티 · 로비
        PartyFull,
        NotInParty,
        AlreadyInParty,
        LobbyFull,
        NotInLobby,
        NotLobbyOwner,
        LobbyNotReady,
        InviteMissing,
        // 전용 서버 배정
        NoServer,
        Invalid,
        Unavailable,
        Conflict
    };

    SW_GF_API const utf8* toString( MatchmakingResult result );
} // namespace sw

namespace sw
{
    /** @brief 모드 하나의 규칙입니다(데이터 — 게임이 올린다). */
    struct MatchModeDefinition
    {
        string _modeId{};               ///< `[0-9a-z_]`, 32 바이트 이하 — 버스 주제 · 캐시 키에 들어간다
        string _serverKind{ "game" };   ///< 전용 서버 종류(기반 서버 고르기)
        int64  _regionRelaxMs{ 30000 }; ///< 닻이나 후보가 이만큼 기다렸으면 지역을 가리지 않는다
        int64  _maxWaitMs{ 120000 };    ///< 넘으면 표를 뺀다(매칭 실패 — 다시 줄 선다)
        int32  _teamCount{ 2 };
        int32  _teamSize{ 3 };
        int32  _baseRatingWindow{ 100 }; ///< 실력 창 = 기본 + 초당 넓힘 × 기다린 초(상한)
        int32  _ratingWindowPerSecond{ 25 };
        int32  _maxRatingWindow{ 1000 };
        uint32 _buildVersion{ 0 }; ///< 전용 서버 빌드 판(0 = 무관)
    };
} // namespace sw

namespace sw
{
    /** @brief 표의 한 사람입니다. */
    struct MatchMember
    {
        AccountId _accountId{ kInvalidAccountId };
        int32     _rating{ 1500 };
    };
} // namespace sw

namespace sw
{
    /** @brief 대기열 표 하나 — 혼자 또는 파티(한 팀에 같이 간다). */
    struct MatchTicket
    {
        vector<MatchMember> _listMember{};
        string              _region{};
        uint64              _ticketId{ 0 };
        uint64              _partyId{ 0 };        ///< 0 = 혼자
        uint64              _originServerId{ 0 }; ///< 표를 낸 서버(결과를 그 서버로 돌려보낸다)
        int64               _enqueuedMs{ 0 };

        /** @brief 멤버 실력의 평균입니다. 비면 0. */
        SW_GF_API int32 computeAverageRating() const;
    };
} // namespace sw

namespace sw
{
    /** @brief 만든 경기 하나입니다. */
    struct MatchFormed
    {
        vector<vector<MatchMember>> _listTeam{};
        vector<MatchTicket>         _listTicket{};
        string                      _modeId{};
        string                      _region{}; ///< 닻(가장 오래 기다린 표)의 지역 — 전용 서버 배정이 먼저 본다
        uint64                      _matchId{ 0 };
        int32                       _averageRating{ 0 };
    };
} // namespace sw

namespace sw
{
    /** @brief 매칭의 상한입니다. */
    struct MatchmakingLimit
    {
        static constexpr int32 kMaxTeamCount = 16;
        static constexpr int32 kMaxTeamSize  = 16;
        static constexpr int32 kMaxIdSize    = 32;
    };
} // namespace sw
