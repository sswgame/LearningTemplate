/**
 * @file MatchmakingTypes.h
 * @brief 매칭 키트의 타입입니다. 결과, 모드 정의, 대기열 티켓, 만들어진 경기, 파티, 로비, 배정 결과를 정의합니다.
 */
#pragma once
#include "Core/Common/Types.h"
#include "Core/Container/string.h"
#include "Core/Container/vector.h"

#include "Engine/Reflection/ReflectionMacros.h"

#include "GameFramework/Base/Online/Directory/ServerRecord.h"
#include "GameFramework/Base/Online/Identity/AccountDirectory.h"
#include "GameFramework/GameFrameworkExports.h"

namespace sw
{
    /** @brief 매칭 요청의 결과입니다. 키트 오류 코드는 `OnlineMethodRange::kMatchmaking` 에 값을 더한 것이며 0은 성공입니다. 와이어 형식입니다. */
    ENUM( Count = Count )
    enum class MatchmakingResult : uint8
    {
        Ok = 0,
        UnknownMode,
        AlreadyQueued,
        NotQueued,
        PartyTooLarge, ///< 티켓의 인원이 0이거나 팀 인원을 넘는다
        NotPartyLeader,
        // 파티와 로비
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
        UpdateRequired,    ///< 온라인 스키마 해시가 다르다(`kVersionMismatch`). 클라이언트만 만들고 서버는 보내지 않는다. 다시 접속하지 않으므로 재시도하지 않는다
        RatingUnavailable, ///< 실력 모드인데 이 서버가 아직 멤버의 통계를 읽지 않았다. 잠시 뒤 다시 줄을 선다
        Count
    };
} // namespace sw

namespace sw
{
    /**
     * @brief 모드 하나의 규칙입니다. 게임이 등록하는 데이터입니다. 범위는 칸의 `PROPERTY( Min · Max )` 이고, 벗어난 모드는 `MatchMaker::initialize` 가 오류로 거절합니다(`ConfigRangeUtil`).
     * @details 예전에는 팀 수와 팀 인원을 조용히 1 ~ 16 으로 맞췄다(폴백 금지 원칙). `Max` 는 리터럴만 받으므로 `MatchmakingLimit` 과 같은 값을 적는다.
     *          경계 값 테스트(`MatchMakerTest.RefusesOutOfRangeMode`)가 둘이 어긋나지 않는지 본다.
     */
    REFLECT()
    struct SW_KIT_API MatchModeDefinition
    {
        REFLECT_BODY();
        PROPERTY()
        string _modeID{}; ///< `[0-9a-z_]` 형식이며 32바이트 이하이다. 버스 주제와 캐시 키에 들어간다
        PROPERTY()
        string _serverKind{ "game" }; ///< 전용 서버 종류이다. 기반 계층의 서버 선택에 쓴다
        PROPERTY()
        int64 _regionRelaxMs{ 30000 }; ///< 앵커나 후보가 이만큼 기다렸으면 지역을 따지지 않는다
        PROPERTY()
        int64 _maxWaitMs{ 120000 }; ///< 넘으면 티켓을 뺀다. 매칭에 실패한 것이며 다시 줄을 서야 한다
        PROPERTY( Min = 1, Max = 16 )
        int32 _teamCount{ 2 }; ///< 1 ~ `MatchmakingLimit::kMaxTeamCount`
        PROPERTY( Min = 1, Max = 16 )
        int32 _teamSize{ 3 }; ///< 1 ~ `MatchmakingLimit::kMaxTeamSize`
        PROPERTY()
        int32 _baseRatingWindow{ 100 }; ///< 실력 허용 범위는 기본값에 초당 넓힘을 기다린 초만큼 곱해 더한 값이며 상한이 있다
        PROPERTY()
        int32 _ratingWindowPerSecond{ 25 };
        PROPERTY()
        int32 _maxRatingWindow{ 1000 };
        PROPERTY()
        uint32 _buildVersion{ 0 }; ///< 전용 서버 빌드 버전이다. 0이면 상관없다
        PROPERTY()
        string _ratingStat{}; ///< 실력으로 쓰는 계정 통계 이름(순위표 키트 통계)이다. 비우면 실력을 보지 않는 모드이고 모두 `_initialRating` 이다
        PROPERTY()
        int32 _initialRating{ 1500 }; ///< 통계가 한 번도 바뀌지 않은 계정의 실력이고, 실력을 보지 않는 모드와 로비 경기의 실력이다
    };
} // namespace sw

namespace sw
{
    /** @brief 티켓에 포함된 한 사람입니다. */
    REFLECT()
    struct MatchMember
    {
        REFLECT_BODY();
        PROPERTY()
        AccountID _accountID{ kInvalidAccountID };
        PROPERTY()
        int32 _rating{ 1500 };
    };
} // namespace sw

namespace sw
{
    /** @brief 대기열 티켓 하나입니다. 혼자이거나 파티이며, 파티는 같은 팀에 함께 배정합니다. 서버 버스(대기열 주제)의 본문입니다. */
    REFLECT()
    struct SW_KIT_API MatchTicket
    {
        REFLECT_BODY();
        static constexpr uint32 kBusVersion = 1; ///< 서버 버스의 의미 버전(docs/09_Decisions.md 5-9 서버 버스 의미 버전). 새 필드는 옛 빌드 매처를 지나면 기본값이 된다(3.6)
        PROPERTY( MaxCount = MatchmakingLimit::kMaxMemberCount )
        vector<MatchMember> _listMember{}; ///< 상한은 팀 수 상한 곱하기 팀 인원 상한(`MatchmakingLimit`)이다
        PROPERTY( MaxCount = ServerRecord::kMaxNameSize )
        string _region{}; ///< 상한은 `ServerRecord::kMaxNameSize` 와 같다
        PROPERTY()
        uint64 _ticketID{ 0 };
        PROPERTY()
        uint64 _partyID{ 0 }; ///< 0이면 혼자이다
        PROPERTY()
        uint64 _originServerID{ 0 }; ///< 티켓을 낸 서버이다. 결과를 그 서버로 돌려보낸다
        PROPERTY()
        int64 _enqueuedMs{ 0 };

        /** @brief 멤버 실력의 평균입니다. 멤버가 없으면 0입니다. */
        int32 computeAverageRating() const;
    };
} // namespace sw

namespace sw
{
    /** @brief 만들어진 경기의 팀 하나입니다. */
    REFLECT()
    struct MatchFormedTeam
    {
        REFLECT_BODY();
        PROPERTY( MaxCount = MatchmakingLimit::kMaxTeamSize )
        vector<MatchMember> _listMember{}; ///< 상한은 `MatchmakingLimit::kMaxTeamSize` 와 같다
    };
} // namespace sw

namespace sw
{
    /** @brief 만들어진 경기 하나입니다. 서버 버스(배정 주제)로 전용 서버에 보내는 본문입니다. */
    REFLECT()
    struct SW_KIT_API MatchFormed
    {
        REFLECT_BODY();
        static constexpr uint32 kBusVersion = 1; ///< 서버 버스의 의미 버전(docs/09_Decisions.md 5-9 서버 버스 의미 버전)
        PROPERTY( MaxCount = MatchmakingLimit::kMaxTeamCount )
        vector<MatchFormedTeam> _listTeam{}; ///< 상한은 `MatchmakingLimit::kMaxTeamCount` 와 같다
        PROPERTY( MaxCount = MatchmakingLimit::kMaxMemberCount )
        vector<MatchTicket> _listTicket{};
        PROPERTY( MaxCount = MatchmakingLimit::kMaxIDSize )
        string _modeID{}; ///< 상한은 `MatchmakingLimit::kMaxIDSize` 와 같다
        PROPERTY( MaxCount = ServerRecord::kMaxNameSize )
        string _region{}; ///< 앵커(가장 오래 기다린 티켓)의 지역이다. 전용 서버 배정이 먼저 확인한다
        PROPERTY()
        uint64 _matchID{ 0 };
        PROPERTY()
        int32 _averageRating{ 0 };
    };
} // namespace sw

namespace sw
{
    /**
     * @brief 경기 중 빈 슬롯(백필) 요청입니다. 전용 게임 서버가 권한 매처에 보내는 서버 버스(`mm.backfill.<모드>`)의 본문입니다.
     * @details 팀마다 빈 슬롯 수를 싣습니다. 매처는 대기 티켓을 실력 허용 범위와 지역 규칙으로 골라 채우고, 채운 사람을 같은 경기 id 의 배정으로 게임 서버에 알립니다.
     *          빈 슬롯이 모두 차거나 모드의 `_maxWaitMs` 가 지나면 요청이 끝납니다. 같은 경기 id 로 다시 보내면 빈 슬롯 수를 바꿉니다.
     */
    REFLECT()
    struct SW_KIT_API MatchBackfillRequest
    {
        REFLECT_BODY();
        static constexpr uint32 kBusVersion = 1; ///< 서버 버스의 의미 버전(docs/09_Decisions.md 5-9 서버 버스 의미 버전)
        PROPERTY( MaxCount = MatchmakingLimit::kMaxTeamCount )
        vector<int32> _listOpenSlot{}; ///< 팀마다 빈 슬롯 수이다. 상한은 `MatchmakingLimit::kMaxTeamCount` 와 같다
        PROPERTY( MaxCount = MatchmakingLimit::kMaxIDSize )
        string _modeID{};
        PROPERTY( MaxCount = ServerRecord::kMaxNameSize )
        string _region{}; ///< 경기의 지역이다
        PROPERTY( MaxCount = ServerRecord::kMaxAddressSize )
        string _address{}; ///< 채운 사람이 접속할 게임 서버 주소이다. 상한은 `ServerRecord::kMaxAddressSize` 와 같다
        PROPERTY()
        uint64 _matchID{ 0 };
        PROPERTY()
        uint64 _serverID{ 0 }; ///< 요청한 게임 서버이다. 채운 사람의 배정을 이 서버의 배정 주제로 보낸다
        PROPERTY()
        int64 _requestedMs{ 0 };
        PROPERTY()
        int32 _averageRating{ 0 }; ///< 남은 사람들의 평균 실력이다
        PROPERTY()
        uint16 _port{ 0 }; ///< 게임 서버의 첫 게임 포트이다(`ServerDescriptor::_port`)
        PROPERTY( Min = 1, Max = 64 )
        uint8 _portCount{ 1 }; ///< 게임 포트 수(포트 샤딩, `ServerDescriptor::_portCount`). 채운 사람의 포트는 `ServerDescriptor::pickShardPort` 가 계정마다 고른다
    };
} // namespace sw

namespace sw
{
    /** @brief 백필로 채운 티켓 하나입니다. 매처가 내고 대기열 서비스가 배정으로 보냅니다. */
    struct MatchBackfilled
    {
        MatchBackfillRequest _request{}; ///< 채운 뒤의 남은 빈 슬롯이 아니라 요청의 서버 정보를 쓴다
        MatchTicket          _ticket{};
        int32                _team{ 0 };
    };
} // namespace sw

namespace sw
{
    /** @brief 매칭의 상한 값들입니다. */
    struct MatchmakingLimit
    {
        static constexpr int32 kMaxTeamCount   = 16;
        static constexpr int32 kMaxTeamSize    = 16;
        static constexpr int32 kMaxIDSize      = 32;
        static constexpr int32 kMaxMemberCount = kMaxTeamCount * kMaxTeamSize; ///< 경기 하나의 인원 상한이며 경기에 든 티켓 수의 상한이다
    };
} // namespace sw

namespace sw
{
    /** @brief 모드 id 규칙(`[0-9a-z_]`, 1~32바이트)에 맞으면 true 입니다. 캐시 키와 버스 주제에 그대로 들어가기 때문입니다. */
    SW_KIT_API bool isValidMatchModeID( string_view modeID );
} // namespace sw

namespace sw
{
    /**
     * @brief 파티 하나의 스냅샷입니다. 캐시 기록, 알림, 응답이 같은 형식을 씁니다. 실력은 두지 않으며, 줄을 설 때 모드의 실력 통계(`_ratingStat`)를 `IAccountStatSource` 에서 읽습니다.
     * @details 알림과 응답은 고정 배치형(B)이고, 캐시 기록은 배포 중에 다른 빌드의 서버가 읽을 수 있으므로 자기 기술형(A, `MatchmakingProtocol::encodeParty`)입니다.
     */
    REFLECT()
    struct SW_KIT_API PartySnapshot
    {
        REFLECT_BODY();
        static constexpr uint32 kBusVersion = 1; ///< push 중계(`OnlinePushRelay`)의 의미 버전(docs/09_Decisions.md 5-9 서버 버스 의미 버전)
        /// 첫 항목이 파티장이다. 상한은 `PartyLobbyLimit::kMaxPartySize` 와 같다.
        PROPERTY( MaxCount = PartyLobbyLimit::kMaxPartySize )
        vector<AccountID> _listMemberID{};
        PROPERTY()
        uint64 _partyID{ 0 };
        PROPERTY()
        uint64 _queuedTicketID{ 0 }; ///< 줄을 서 있으면 그 티켓이다. 낸 서버 id 를 32비트 왼쪽으로 민 값에 순번을 합친 것이다
        PROPERTY( Min = 0, Max = 8 )
        int32 _maxMemberCount{ 4 };

        AccountID getLeaderID() const { return _listMemberID.empty() ? kInvalidAccountID : _listMemberID.front(); }
        bool      hasMember( AccountID accountID ) const;
    };
} // namespace sw

namespace sw
{
    /** @brief 로비 멤버 한 명입니다. */
    REFLECT()
    struct LobbyMember
    {
        REFLECT_BODY();
        PROPERTY()
        AccountID _accountID{ kInvalidAccountID };
        PROPERTY()
        int32 _team{ 0 };
        PROPERTY( Min = 0, Max = 1 )
        uint8 _bReady{ SW_FALSE };
    };
} // namespace sw

namespace sw
{
    /** @brief 로비 설정 한 줄입니다. 맵이나 규칙을 담습니다. 상한은 `PartyLobbyLimit` 와 같은 값입니다. */
    REFLECT()
    struct LobbySetting
    {
        REFLECT_BODY();
        PROPERTY( MaxCount = PartyLobbyLimit::kMaxSettingKeySize )
        string _key{}; ///< `[0-9a-z_.]` 형식이며 16바이트 이하이다
        PROPERTY( MaxCount = PartyLobbyLimit::kMaxSettingValueSize )
        string _value{}; ///< 64바이트 이하이다
    };
} // namespace sw

namespace sw
{
    /** @brief 로비 상태입니다. 와이어 값입니다. */
    ENUM( Count = Count )
    enum class LobbyState : uint8
    {
        Open = 0,
        Starting, ///< 방장이 시작했고 전용 서버를 배정하는 중이다. 실패하면 Open 으로 돌아온다
        Count
    };
} // namespace sw

namespace sw
{
    /**
     * @brief 로비 하나의 스냅샷입니다. 알림, 응답, 만들기 요청은 고정 배치형(B)이고 캐시 기록은 자기 기술형(A)입니다.
     * @details 상한은 `PartyLobbyLimit` 와 `MatchmakingLimit` 와 같은 값입니다.
     */
    REFLECT()
    struct SW_KIT_API LobbySnapshot
    {
        REFLECT_BODY();
        static constexpr uint32 kBusVersion = 1; ///< push 중계(`OnlinePushRelay`)의 의미 버전(docs/09_Decisions.md 5-9 서버 버스 의미 버전)
        PROPERTY( MaxCount = PartyLobbyLimit::kMaxLobbySize )
        vector<LobbyMember> _listMember{}; ///< 첫 항목이 방장이다
        PROPERTY( MaxCount = PartyLobbyLimit::kMaxLobbySetting )
        vector<LobbySetting> _listSetting{};
        PROPERTY( MaxCount = PartyLobbyLimit::kMaxLobbyNameSize )
        string _name{};
        PROPERTY( MaxCount = MatchmakingLimit::kMaxIDSize )
        string _modeID{};
        PROPERTY()
        uint64 _lobbyID{ 0 };
        PROPERTY()
        int64 _createdMs{ 0 };
        PROPERTY( Min = 0, Max = 32 )
        int32 _maxMemberCount{ 8 };
        PROPERTY()
        LobbyState _state{ LobbyState::Open };

        AccountID getOwnerID() const { return _listMember.empty() ? kInvalidAccountID : _listMember.front()._accountID; }
        bool      hasMember( AccountID accountID ) const;
        /** @brief 방장을 뺀 모두가 준비했고 인원이 둘 이상이면 true 입니다. */
        bool isEveryoneReady() const;
    };
} // namespace sw

namespace sw
{
    /** @brief 파티와 로비의 상한 값들입니다. */
    struct PartyLobbyLimit
    {
        static constexpr int32 kMaxPartySize        = 8;
        static constexpr int32 kMinPartySize        = 2; ///< 서버 설정 파티 최대 인원(`PartyLobbyService::initialize`)의 하한
        static constexpr int32 kMaxLobbySize        = 32;
        static constexpr int32 kMinLobbySize        = 2;
        static constexpr int32 kLobbyTeamCount      = 2; ///< 로비는 두 팀이며, 들어오면 인원이 적은 팀에 들어간다
        static constexpr int32 kMaxLobbySetting     = 8;
        static constexpr int32 kMaxSettingKeySize   = 16;
        static constexpr int32 kMaxSettingValueSize = 64;
        static constexpr int32 kMaxLobbyNameSize    = 48;
        static constexpr int32 kMaxLobbyList        = 50;
        static constexpr int64 kPartyTtlMs          = 1800000; ///< 30분이며 바뀔 때마다 연장한다
        static constexpr int64 kLobbyTtlMs          = 7200000; ///< 2시간이다
        static constexpr int64 kInviteTtlMs         = 300000;  ///< 5 분
    };
} // namespace sw

namespace sw
{
    /** @brief 줄을 선 티켓이 끝난 이유입니다. 와이어 값입니다. */
    ENUM( Count = Count )
    enum class MatchQueueOutcome : uint8
    {
        Found = 0,
        Timeout,   ///< 모드 시한을 넘었거나 권한 서버가 바뀌어 티켓을 잃었다. 다시 줄을 선다
        NoServer,  ///< 경기는 만들었지만 전용 서버에 끝내 배정할 슬롯이 없었다
        Cancelled, ///< 대기열에서 빠졌다. 본인이 뺐거나 파티가 바뀐 경우이다
        Count
    };
} // namespace sw

namespace sw
{
    /** @brief 클라이언트가 받는 매칭 결과입니다. `Found` 이면 게임이 계정 클라이언트로 접속 티켓을 받아 그 서버에 UDP 로 접속합니다. 매칭 알림과 서버 버스(결과 주제)의 본문입니다. */
    REFLECT()
    struct SW_KIT_API MatchAssignment
    {
        REFLECT_BODY();
        static constexpr uint32 kBusVersion = 1; ///< 서버 버스와 push 중계의 의미 버전(docs/09_Decisions.md 5-9 서버 버스 의미 버전)
        PROPERTY( MaxCount = MatchmakingLimit::kMaxTeamSize )
        vector<AccountID> _listTeammate{}; ///< 상한은 `MatchmakingLimit::kMaxTeamSize` 와 같다
        PROPERTY( MaxCount = ServerRecord::kMaxAddressSize )
        string _address{}; ///< 상한은 `ServerRecord::kMaxAddressSize` 와 같다
        PROPERTY( MaxCount = MatchmakingLimit::kMaxIDSize )
        string _modeID{};
        PROPERTY()
        uint64 _matchID{ 0 };
        PROPERTY()
        uint64 _serverID{ 0 }; ///< 계정 키트 접속 티켓의 서버 id 이다. 16진 문자열로 쓴다
        PROPERTY()
        uint64 _ticketID{ 0 }; ///< 로비 시작이면 0이다
        PROPERTY()
        int32 _team{ 0 };
        PROPERTY()
        uint16 _port{ 0 };
        PROPERTY()
        MatchQueueOutcome _outcome{ MatchQueueOutcome::Found };
    };
} // namespace sw
