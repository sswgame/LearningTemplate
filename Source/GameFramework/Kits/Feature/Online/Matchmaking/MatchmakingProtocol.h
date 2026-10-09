/**
 * @file MatchmakingProtocol.h
 * @brief 매칭 서비스의 와이어 — 메서드 · 알림 번호(영역 `OnlineMethodRange::kMatchmaking`)와 몸 · 캐시 기록 · 버스 메시지 형식입니다(클라이언트 · 서버가 같이 쓴다).
 * @details 응답 몸 = `MatchmakingResult` + 칸(업무 결과는 몸에 — 거래 · 경제 키트와 같다). 공통 오류(`OnlineError` — 로그인 없음 · 깨진 몸 · 시한)만 오류 코드로 오고,
 *          클라이언트는 `fromErrorCode` 로 결과에 맞춘다. 캐시 기록은 `[판 1][몸]` — 다른 판은 읽지 않는다.
 */
#pragma once
#include "Core/Common/Types.h"
#include "Core/Container/vector.h"

#include "GameFramework/Base/Online/Service/OnlineProtocol.h"
#include "GameFramework/GameFrameworkExports.h"
#include "GameFramework/Kits/Feature/Online/Matchmaking/MatchmakingTypes.h"

namespace sw
{
    class BitReader;
    class BitWriter;

    /** @brief 매칭 서비스 메서드 · 알림입니다. 와이어 값 — 바꾸면 `MatchmakingProtocol::kVersion` 을 올린다. */
    struct MatchmakingMethod
    {
        static constexpr uint16 kPartyCreate = OnlineMethodRange::kMatchmaking + 0x01; ///< 몸 없음 → 파티
        static constexpr uint16 kPartyInvite = OnlineMethodRange::kMatchmaking + 0x02; ///< 계정 → 파티(장만)
        static constexpr uint16 kPartyAccept = OnlineMethodRange::kMatchmaking + 0x03; ///< 파티 id → 파티
        static constexpr uint16 kPartyLeave  = OnlineMethodRange::kMatchmaking + 0x04;
        static constexpr uint16 kPartyKick   = OnlineMethodRange::kMatchmaking + 0x05; ///< 계정(장만)
        static constexpr uint16 kLobbyCreate = OnlineMethodRange::kMatchmaking + 0x06; ///< 로비(이름 · 모드 · 정원 · 설정) → 로비
        static constexpr uint16 kLobbyList   = OnlineMethodRange::kMatchmaking + 0x07; ///< 모드 → 로비들(새것부터)
        static constexpr uint16 kLobbyJoin   = OnlineMethodRange::kMatchmaking + 0x08; ///< 로비 id → 로비
        static constexpr uint16 kLobbyLeave  = OnlineMethodRange::kMatchmaking + 0x09; ///< 로비 id
        static constexpr uint16 kLobbyReady  = OnlineMethodRange::kMatchmaking + 0x0A; ///< 로비 id · bool
        static constexpr uint16 kLobbyStart  = OnlineMethodRange::kMatchmaking + 0x0B; ///< 로비 id(방장) — 전용 서버 배정은 결과 알림으로
        static constexpr uint16 kQueueJoin   = OnlineMethodRange::kMatchmaking + 0x0C; ///< 모드 · 지역 → 표 id(파티 장이면 파티 전체)
        static constexpr uint16 kQueueLeave  = OnlineMethodRange::kMatchmaking + 0x0D;
        static constexpr int32  kCount       = 13;

        static constexpr uint16 kPushParty       = OnlineMethodRange::kMatchmaking + 0x80; ///< 파티(빈 회원 = 해산 · 나감 · 내보내짐)
        static constexpr uint16 kPushPartyInvite = OnlineMethodRange::kMatchmaking + 0x81; ///< 초대한 계정 · 파티 id
        static constexpr uint16 kPushLobby       = OnlineMethodRange::kMatchmaking + 0x82; ///< 로비(빈 회원 = 나감)
        static constexpr uint16 kPushMatch       = OnlineMethodRange::kMatchmaking + 0x83; ///< `MatchAssignment`

        static_assert( OnlineMethodRange::isInRange( kQueueLeave, OnlineMethodRange::kMatchmaking ) && OnlineMethodRange::isMethod( kQueueLeave ) );
        static_assert( OnlineMethodRange::isInRange( kPushMatch, OnlineMethodRange::kMatchmaking ) && OnlineMethodRange::isMethod( kPushMatch ) == false );

        /** @brief 0 부터의 번호(지표 · 표)입니다. 모르는 메서드면 −1 입니다. */
        static constexpr int32 toIndex( uint16 method )
        {
            return kPartyCreate <= method && method < kPartyCreate + kCount ? static_cast<int32>( method - kPartyCreate ) : -1;
        }
    };
} // namespace sw

namespace sw
{
    /** @brief 파티 초대 알림입니다. */
    struct PartyInvite
    {
        uint64    _partyId{ 0 };
        AccountId _inviterId{ kInvalidAccountId };
    };
} // namespace sw

namespace sw
{
    /** @brief 모든 메서드의 응답(메서드마다 쓰는 칸만 찬다)입니다. 실패면 `_result` 만 뜻이 있다. */
    struct MatchmakingReply
    {
        vector<LobbySnapshot> _listLobby{}; ///< LobbyList
        PartySnapshot         _party{};
        LobbySnapshot         _lobby{};
        uint64                _ticketId{ 0 }; ///< QueueJoin · QueueLeave
        MatchmakingResult     _result{ MatchmakingResult::Ok };
    };
} // namespace sw

namespace sw
{
    /**
     * @struct MatchmakingProtocol
     * @brief 몸 코덱입니다. 읽기는 상한(`PartyLobbyLimit` · `MatchmakingLimit`)을 넘거나 넘치면 false.
     */
    struct SW_GF_API MatchmakingProtocol
    {
        static constexpr uint32 kVersion      = 1;
        static constexpr uint8  kRecordFormat = 1;

        static void               writeParty( BitWriter& outWriter, const PartySnapshot& party );
        [[nodiscard]] static bool readParty( BitReader& reader, PartySnapshot& outParty );
        static void               writeLobby( BitWriter& outWriter, const LobbySnapshot& lobby );
        [[nodiscard]] static bool readLobby( BitReader& reader, LobbySnapshot& outLobby );
        static void               writeInvite( BitWriter& outWriter, const PartyInvite& invite );
        [[nodiscard]] static bool readInvite( BitReader& reader, PartyInvite& outInvite );

        /** @brief 캐시 기록(`[판][몸]`)입니다. */
        static vector<uint8>      encodeParty( const PartySnapshot& party );
        [[nodiscard]] static bool decodeParty( const vector<uint8>& bytes, PartySnapshot& outParty );
        static vector<uint8>      encodeLobby( const LobbySnapshot& lobby );
        [[nodiscard]] static bool decodeLobby( const vector<uint8>& bytes, LobbySnapshot& outLobby );
        /** @brief 캐시 값 하나에 든 id(16 진 글)입니다 — 계정 → 파티 색인 · 임대 주인. */
        static vector<uint8>      encodeId( uint64 id );
        [[nodiscard]] static bool decodeId( const vector<uint8>& bytes, uint64& outId );

        // 서버 사이(버스) — 표 · 만든 경기 · 결과
        static void               writeTicket( BitWriter& outWriter, const MatchTicket& ticket );
        [[nodiscard]] static bool readTicket( BitReader& reader, MatchTicket& outTicket );
        static void               writeFormed( BitWriter& outWriter, const MatchFormed& match );
        [[nodiscard]] static bool readFormed( BitReader& reader, MatchFormed& outMatch );
        static void               writeAssignment( BitWriter& outWriter, const MatchAssignment& assignment );
        [[nodiscard]] static bool readAssignment( BitReader& reader, MatchAssignment& outAssignment );

        /** @brief 응답 몸입니다(결과 + 칸). */
        static void               writeReply( BitWriter& outWriter, const MatchmakingReply& reply );
        [[nodiscard]] static bool readReply( BitReader& reader, MatchmakingReply& outReply );

        /** @brief 공통 오류 코드(`OnlineError`) → 결과입니다 — 깨진 몸은 Invalid, 충돌은 Conflict, 나머지(로그인 · 전송 · 저장소 · 시한)는 Unavailable 입니다. */
        static MatchmakingResult fromErrorCode( uint16 errorCode );
    };
} // namespace sw
