/**
 * @file MatchmakingTypes.h
 * @brief 매칭 키트의 타입 — 결과 · 모드 정의 · 표 · 만든 경기 · 파티 · 로비 · 배정 결과입니다.
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
        Conflict,
        Count
    };

    SW_GF_API const utf8* toString( MatchmakingResult result );
} // namespace sw

namespace sw
{
    /** @brief 모드 하나의 규칙입니다(데이터 — 게임이 올린다). */
    struct MatchModeDefinition
    {
        string _modeID{};               ///< `[0-9a-z_]`, 32 바이트 이하 — 버스 주제 · 캐시 키에 들어간다
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
        AccountID _accountID{ kInvalidAccountID };
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
        uint64              _ticketID{ 0 };
        uint64              _partyID{ 0 };        ///< 0 = 혼자
        uint64              _originServerID{ 0 }; ///< 표를 낸 서버(결과를 그 서버로 돌려보낸다)
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
        string                      _modeID{};
        string                      _region{}; ///< 닻(가장 오래 기다린 표)의 지역 — 전용 서버 배정이 먼저 본다
        uint64                      _matchID{ 0 };
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
        static constexpr int32 kMaxIDSize    = 32;
    };
} // namespace sw

namespace sw
{
    /** @brief 모드 id 규칙(`[0-9a-z_]`, 1..32 바이트)인가입니다 — 캐시 키 · 버스 주제에 그대로 들어간다. */
    SW_GF_API bool isValidMatchModeID( string_view modeID );
} // namespace sw

namespace sw
{
    /** @brief 파티 하나의 스냅숏입니다(캐시 기록 · 알림 · 응답이 같은 모양). 실력은 두지 않는다 — 줄 설 때 `IMatchRatingSource` 가 모드마다 준다. */
    struct PartySnapshot
    {
        vector<AccountID> _listMemberID{}; ///< 첫째가 장
        uint64            _partyID{ 0 };
        uint64            _queuedTicketID{ 0 }; ///< 줄 서 있으면 그 표(낸 서버 id << 32 | 순번)
        int32             _maxMemberCount{ 4 };

        AccountID      getLeaderID() const { return _listMemberID.empty() ? kInvalidAccountID : _listMemberID.front(); }
        SW_GF_API bool hasMember( AccountID accountID ) const;
    };
} // namespace sw

namespace sw
{
    /** @brief 로비 회원 한 사람입니다. */
    struct LobbyMember
    {
        AccountID _accountID{ kInvalidAccountID };
        int32     _team{ 0 };
        uint8     _bReady{ SW_FALSE };
    };
} // namespace sw

namespace sw
{
    /** @brief 로비 설정 한 줄(맵 · 규칙)입니다. */
    struct LobbySetting
    {
        string _key{};   ///< `[0-9a-z_.]`, 16 바이트 이하
        string _value{}; ///< 64 바이트 이하
    };
} // namespace sw

namespace sw
{
    /** @brief 로비 상태입니다. 와이어 값이다. */
    enum class LobbyState : uint8
    {
        Open = 0,
        Starting, ///< 방장이 시작했다 — 전용 서버 배정 중(실패하면 Open 으로 돌아온다)
        Count
    };
} // namespace sw

namespace sw
{
    /** @brief 로비 하나의 스냅숏입니다. */
    struct LobbySnapshot
    {
        vector<LobbyMember>  _listMember{}; ///< 첫째가 방장
        vector<LobbySetting> _listSetting{};
        string               _name{};
        string               _modeID{};
        uint64               _lobbyID{ 0 };
        int64                _createdMs{ 0 };
        int32                _maxMemberCount{ 8 };
        LobbyState           _state{ LobbyState::Open };

        AccountID      getOwnerID() const { return _listMember.empty() ? kInvalidAccountID : _listMember.front()._accountID; }
        SW_GF_API bool hasMember( AccountID accountID ) const;
        /** @brief 방장을 빼고 모두 준비했고 둘 이상인가입니다. */
        SW_GF_API bool isEveryoneReady() const;
    };
} // namespace sw

namespace sw
{
    /** @brief 파티 · 로비의 상한입니다. */
    struct PartyLobbyLimit
    {
        static constexpr int32 kMaxPartySize        = 8;
        static constexpr int32 kMaxLobbySize        = 32;
        static constexpr int32 kMinLobbySize        = 2;
        static constexpr int32 kLobbyTeamCount      = 2; ///< 로비는 두 편(들어오면 인원이 적은 편)
        static constexpr int32 kMaxLobbySetting     = 8;
        static constexpr int32 kMaxSettingKeySize   = 16;
        static constexpr int32 kMaxSettingValueSize = 64;
        static constexpr int32 kMaxLobbyNameSize    = 48;
        static constexpr int32 kMaxLobbyList        = 50;
        static constexpr int64 kPartyTtlMs          = 1800000; ///< 30 분 — 바뀔 때마다 연장
        static constexpr int64 kLobbyTtlMs          = 7200000; ///< 2 시간
        static constexpr int64 kInviteTtlMs         = 300000;  ///< 5 분
    };
} // namespace sw

namespace sw
{
    /** @brief 줄 선 표의 끝입니다. 와이어 값이다. */
    enum class MatchQueueOutcome : uint8
    {
        Found = 0,
        Timeout,   ///< 모드 시한 · 권한 서버가 넘어가 표를 잃었다 — 다시 줄 선다
        NoServer,  ///< 경기는 만들었지만 전용 서버 자리가 끝내 없었다
        Cancelled, ///< 빠졌다(본인 · 파티가 바뀜)
        Count
    };
} // namespace sw

namespace sw
{
    /** @brief 클라이언트가 받는 매칭 결과입니다(`Found` 면 게임은 계정 클라이언트로 접속 표를 받아 그 서버에 UDP 로 붙는다). */
    struct MatchAssignment
    {
        vector<AccountID> _listTeammate{};
        string            _address{};
        string            _modeID{};
        uint64            _matchID{ 0 };
        uint64            _serverID{ 0 }; ///< 계정 키트 접속 표의 서버 id(16 진 글로)
        uint64            _ticketID{ 0 }; ///< 로비 시작이면 0
        int32             _team{ 0 };
        uint16            _port{ 0 };
        MatchQueueOutcome _outcome{ MatchQueueOutcome::Found };
    };
} // namespace sw
