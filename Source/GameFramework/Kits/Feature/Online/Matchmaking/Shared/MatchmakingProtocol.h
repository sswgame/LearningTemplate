/**
 * @file MatchmakingProtocol.h
 * @brief 매칭 서비스의 와이어 형식입니다. 메서드와 알림 번호(영역 `OnlineMethodRange::kMatchmaking`), 본문, 캐시 기록, 버스 메시지의 형식을 정의하며 클라이언트와 서버가 함께 씁니다.
 * @details 응답 본문은 `MatchmakingResult` 뒤에 필드가 이어지는 형식이다. 업무 결과는 본문에 담으며 거래와 경제 키트도 같다.
 *          공통 오류(`OnlineError`)만 오류 코드로 오며, 로그인 없음, 손상된 본문, 시한 초과가 해당한다. 클라이언트는 `fromErrorCode` 로 이를 결과에 맞춘다.
 *          캐시 기록은 `[버전 1][본문]` 형식이며 다른 버전은 읽지 않는다.
 */
#pragma once
#include "Core/Common/Types.h"
#include "Core/Container/vector.h"

#include "GameFramework/Base/Online/Service/OnlineProtocol.h"
#include "GameFramework/GameFrameworkExports.h"
#include "GameFramework/Kits/Feature/Online/Matchmaking/Shared/MatchmakingTypes.h"

namespace sw
{
    class BitReader;
    class BitWriter;

    /** @brief 매칭 서비스의 메서드와 알림 번호입니다. 와이어 값이므로 바꾸면 `MatchmakingProtocol::kVersion` 을 올려야 합니다. */
    struct MatchmakingMethod
    {
        static constexpr uint16 kPartyCreate = OnlineMethodRange::kMatchmaking + 0x01; ///< 본문은 없고 파티를 돌려준다
        static constexpr uint16 kPartyInvite = OnlineMethodRange::kMatchmaking + 0x02; ///< 계정을 받아 파티를 돌려준다. 파티장만 부를 수 있다
        static constexpr uint16 kPartyAccept = OnlineMethodRange::kMatchmaking + 0x03; ///< 파티 id 를 받아 파티를 돌려준다
        static constexpr uint16 kPartyLeave  = OnlineMethodRange::kMatchmaking + 0x04;
        static constexpr uint16 kPartyKick   = OnlineMethodRange::kMatchmaking + 0x05; ///< 계정을 받는다. 파티장만 부를 수 있다
        static constexpr uint16 kLobbyCreate = OnlineMethodRange::kMatchmaking + 0x06; ///< 로비(이름, 모드, 정원, 설정)를 받아 로비를 돌려준다
        static constexpr uint16 kLobbyList   = OnlineMethodRange::kMatchmaking + 0x07; ///< 모드를 받아 로비 목록을 돌려준다. 새 로비부터 나온다
        static constexpr uint16 kLobbyJoin   = OnlineMethodRange::kMatchmaking + 0x08; ///< 로비 id 를 받아 로비를 돌려준다
        static constexpr uint16 kLobbyLeave  = OnlineMethodRange::kMatchmaking + 0x09; ///< 로비 id
        static constexpr uint16 kLobbyReady  = OnlineMethodRange::kMatchmaking + 0x0A; ///< 로비 id 와 bool 을 받는다
        static constexpr uint16 kLobbyStart  = OnlineMethodRange::kMatchmaking + 0x0B; ///< 로비 id 를 받으며 방장만 부를 수 있다. 전용 서버 배정은 결과 알림으로 전달한다
        static constexpr uint16 kQueueJoin   = OnlineMethodRange::kMatchmaking + 0x0C; ///< 모드와 지역을 받아 티켓 id 를 돌려준다. 파티장이면 파티 전체의 티켓이다
        static constexpr uint16 kQueueLeave  = OnlineMethodRange::kMatchmaking + 0x0D;
        static constexpr int32  kCount       = 13;

        static constexpr uint16 kPushParty       = OnlineMethodRange::kMatchmaking + 0x80; ///< 파티 알림이다. 멤버가 비어 있으면 해산, 나감, 내보내짐을 뜻한다
        static constexpr uint16 kPushPartyInvite = OnlineMethodRange::kMatchmaking + 0x81; ///< 초대한 계정과 파티 id 를 보낸다
        static constexpr uint16 kPushLobby       = OnlineMethodRange::kMatchmaking + 0x82; ///< 로비 알림이다. 멤버가 비어 있으면 나감을 뜻한다
        static constexpr uint16 kPushMatch       = OnlineMethodRange::kMatchmaking + 0x83; ///< `MatchAssignment`

        static_assert( OnlineMethodRange::isInRange( kQueueLeave, OnlineMethodRange::kMatchmaking ) && OnlineMethodRange::isMethod( kQueueLeave ) );
        static_assert( OnlineMethodRange::isInRange( kPushMatch, OnlineMethodRange::kMatchmaking ) && OnlineMethodRange::isMethod( kPushMatch ) == false );

        /** @brief 0부터 시작하는 번호입니다. 지표와 테이블의 인덱스로 씁니다. 모르는 메서드이면 -1 입니다. */
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
        uint64    _partyID{ 0 };
        AccountID _inviterID{ kInvalidAccountID };
    };
} // namespace sw

namespace sw
{
    /** @brief 모든 메서드의 응답입니다. 메서드마다 쓰는 필드만 채웁니다. 실패하면 `_result` 만 의미가 있습니다. */
    struct MatchmakingReply
    {
        vector<LobbySnapshot> _listLobby{}; ///< LobbyList
        PartySnapshot         _party{};
        LobbySnapshot         _lobby{};
        uint64                _ticketID{ 0 }; ///< QueueJoin 과 QueueLeave 의 티켓 id 이다
        MatchmakingResult     _result{ MatchmakingResult::Ok };
    };
} // namespace sw

namespace sw
{
    /**
     * @struct MatchmakingProtocol
     * @brief 본문 코덱입니다. 읽을 때 상한(`PartyLobbyLimit`, `MatchmakingLimit`)을 넘으면 false 를 반환합니다.
     */
    struct SW_KIT_API MatchmakingProtocol
    {
        static constexpr uint32 kVersion      = 1;
        static constexpr uint8  kRecordFormat = 1;

        static void               writeParty( BitWriter& outWriter, const PartySnapshot& party );
        [[nodiscard]] static bool readParty( BitReader& reader, PartySnapshot& outParty );
        static void               writeLobby( BitWriter& outWriter, const LobbySnapshot& lobby );
        [[nodiscard]] static bool readLobby( BitReader& reader, LobbySnapshot& outLobby );
        static void               writeInvite( BitWriter& outWriter, const PartyInvite& invite );
        [[nodiscard]] static bool readInvite( BitReader& reader, PartyInvite& outInvite );

        /** @brief 캐시 기록(`[버전][본문]`)을 읽고 씁니다. */
        static vector<uint8>      encodeParty( const PartySnapshot& party );
        [[nodiscard]] static bool decodeParty( const vector<uint8>& bytes, PartySnapshot& outParty );
        static vector<uint8>      encodeLobby( const LobbySnapshot& lobby );
        [[nodiscard]] static bool decodeLobby( const vector<uint8>& bytes, LobbySnapshot& outLobby );
        /** @brief 캐시 값 하나에 든 id(16진 문자열)입니다. 계정에서 파티로 가는 인덱스와 임대 주인이 쓰는 값입니다. */
        static vector<uint8>      encodeID( uint64 id );
        [[nodiscard]] static bool decodeID( const vector<uint8>& bytes, uint64& outID );

        // 서버 사이(버스)에서 주고받는 티켓, 만들어진 경기, 결과이다
        static void               writeTicket( BitWriter& outWriter, const MatchTicket& ticket );
        [[nodiscard]] static bool readTicket( BitReader& reader, MatchTicket& outTicket );
        static void               writeFormed( BitWriter& outWriter, const MatchFormed& match );
        [[nodiscard]] static bool readFormed( BitReader& reader, MatchFormed& outMatch );
        static void               writeAssignment( BitWriter& outWriter, const MatchAssignment& assignment );
        [[nodiscard]] static bool readAssignment( BitReader& reader, MatchAssignment& outAssignment );

        /** @brief 응답 본문을 읽고 씁니다. 결과 뒤에 필드가 이어집니다. */
        static void               writeReply( BitWriter& outWriter, const MatchmakingReply& reply );
        [[nodiscard]] static bool readReply( BitReader& reader, MatchmakingReply& outReply );

        /** @brief 공통 오류 코드(`OnlineError`)를 결과로 바꿉니다. 손상된 본문은 Invalid, 충돌은 Conflict, 나머지(로그인, 전송, 저장소, 시한)는 Unavailable 입니다. */
        static MatchmakingResult fromErrorCode( uint16 errorCode );
    };
} // namespace sw
