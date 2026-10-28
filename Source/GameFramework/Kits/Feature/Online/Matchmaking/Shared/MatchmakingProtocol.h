/**
 * @file MatchmakingProtocol.h
 * @brief 매칭 서비스의 와이어 형식입니다. 메서드와 알림 번호(영역 `OnlineMethodRange::kMatchmaking`), 본문, 캐시 기록, 버스 메시지의 형식을 정의하며 클라이언트와 서버가 함께 씁니다.
 * @details 요청, 응답, 알림, 서버 버스 본문은 `REFLECT` 구조체 하나씩이고 고정 배치형(B)이다. 바이트 배치는 `PROPERTY` 선언 순서와 키가 정한다.
 *          업무 결과는 응답 본문(`MatchmakingReply::_result`)에 담으며 거래와 경제 키트도 같다.
 *          공통 오류(`OnlineError`)만 오류 코드로 오며, 로그인 없음, 손상된 본문, 시한 초과가 해당한다. 클라이언트는 `fromErrorCode` 로 이를 결과에 맞춘다.
 *          캐시 기록(파티, 로비)은 자기 기술형(A)이며 기록 버전이 다르면 읽지 않는다.
 */
#pragma once
#include "Core/Common/Types.h"
#include "Core/Container/string.h"
#include "Core/Container/vector.h"

#include "Engine/Reflection/ReflectionMacros.h"

#include "GameFramework/Base/Online/Service/OnlineProtocol.h"
#include "GameFramework/Base/Online/Service/OnlineProtocolRegistry.h"
#include "GameFramework/GameFrameworkExports.h"
#include "GameFramework/Kits/Feature/Online/Matchmaking/Shared/MatchmakingTypes.h"

namespace sw
{
    /** @brief 매칭 서비스 메서드 이름입니다. 열거자 순서가 와이어 번호(`OnlineMethodTable`)이고, 이름의 snake_case 가 지표 라벨입니다. 새 메서드는 `Count` 앞에만 더합니다. */
    ENUM( Count = Count )
    enum class MatchmakingMethodName : uint8
    {
        PartyCreate,
        PartyInvite,
        PartyAccept,
        PartyLeave,
        PartyKick,
        LobbyCreate,
        LobbyList,
        LobbyJoin,
        LobbyLeave,
        LobbyReady,
        LobbyStart,
        QueueJoin,
        QueueLeave,
        Count,
    };

    /** @brief 매칭 서비스 알림 이름입니다. 열거자 순서가 와이어 번호(`OnlineMethodTable`)입니다. 새 알림은 `Count` 앞에만 더합니다. */
    ENUM( Count = Count )
    enum class MatchmakingPushName : uint8
    {
        Party,
        PartyInvite,
        Lobby,
        Match,
        Count,
    };
} // namespace sw

namespace sw
{
    /** @brief 매칭 서비스의 메서드와 알림 번호입니다. 번호는 열거자 순서에서 나오며 그 이름 순서가 온라인 스키마 해시에 섞입니다(`OnlineMethodTable`). */
    struct MatchmakingMethod : OnlineMethodTable<MatchmakingMethodName, OnlineMethodRange::kMatchmaking, MatchmakingPushName>
    {
        static constexpr uint16 kPartyCreate = toMethod( MatchmakingMethodName::PartyCreate ); ///< 본문은 없고 파티를 돌려준다
        static constexpr uint16 kPartyInvite = toMethod( MatchmakingMethodName::PartyInvite ); ///< 계정을 받아 파티를 돌려준다. 파티장만 부를 수 있다
        static constexpr uint16 kPartyAccept = toMethod( MatchmakingMethodName::PartyAccept ); ///< 파티 id 를 받아 파티를 돌려준다
        static constexpr uint16 kPartyLeave  = toMethod( MatchmakingMethodName::PartyLeave );
        static constexpr uint16 kPartyKick   = toMethod( MatchmakingMethodName::PartyKick );   ///< 계정을 받는다. 파티장만 부를 수 있다
        static constexpr uint16 kLobbyCreate = toMethod( MatchmakingMethodName::LobbyCreate ); ///< 로비(이름, 모드, 정원, 설정)를 받아 로비를 돌려준다
        static constexpr uint16 kLobbyList   = toMethod( MatchmakingMethodName::LobbyList );   ///< 모드를 받아 로비 목록을 돌려준다. 새 로비부터 나온다
        static constexpr uint16 kLobbyJoin   = toMethod( MatchmakingMethodName::LobbyJoin );   ///< 로비 id 를 받아 로비를 돌려준다
        static constexpr uint16 kLobbyLeave  = toMethod( MatchmakingMethodName::LobbyLeave );  ///< 로비 id
        static constexpr uint16 kLobbyReady  = toMethod( MatchmakingMethodName::LobbyReady );  ///< 로비 id 와 bool 을 받는다
        static constexpr uint16 kLobbyStart  = toMethod( MatchmakingMethodName::LobbyStart );  ///< 로비 id 를 받으며 방장만 부를 수 있다. 전용 서버 배정은 결과 알림으로 전달한다
        static constexpr uint16 kQueueJoin   = toMethod( MatchmakingMethodName::QueueJoin );   ///< 모드와 지역을 받아 티켓 id 를 돌려준다. 파티장이면 파티 전체의 티켓이다
        static constexpr uint16 kQueueLeave  = toMethod( MatchmakingMethodName::QueueLeave );

        static constexpr uint16 kPushParty       = toPush( MatchmakingPushName::Party );       ///< 파티 알림이다. 멤버가 비어 있으면 해산, 나감, 내보내짐을 뜻한다
        static constexpr uint16 kPushPartyInvite = toPush( MatchmakingPushName::PartyInvite ); ///< 초대한 계정과 파티 id 를 보낸다
        static constexpr uint16 kPushLobby       = toPush( MatchmakingPushName::Lobby );       ///< 로비 알림이다. 멤버가 비어 있으면 나감을 뜻한다
        static constexpr uint16 kPushMatch       = toPush( MatchmakingPushName::Match );       ///< `MatchAssignment`
    };
} // namespace sw

namespace sw
{
    /** @brief 파티 초대 알림입니다. */
    REFLECT()
    struct SW_KIT_API PartyInvite
    {
        REFLECT_BODY();
        static constexpr uint32 kBusVersion = 1; ///< push 중계(`OnlinePushRelay`)의 의미 버전
        PROPERTY()
        uint64 _partyID{ 0 };
        PROPERTY()
        AccountID _inviterID{ kInvalidAccountID };
    };
} // namespace sw

namespace sw
{
    /** @brief id 하나를 받는 요청입니다. 파티 초대, 수락, 내보내기는 계정이나 파티 id 이고, 로비 들어가기, 나오기, 시작은 로비 id 입니다. */
    REFLECT()
    struct MatchmakingIDRequest
    {
        REFLECT_BODY();
        PROPERTY()
        uint64 _id{ 0 };
    };
} // namespace sw

namespace sw
{
    /** @brief 로비 준비(`kLobbyReady`)의 요청입니다. */
    REFLECT()
    struct MatchmakingReadyRequest
    {
        REFLECT_BODY();
        PROPERTY()
        uint64 _lobbyID{ 0 };
        PROPERTY( Min = 0, Max = 1 )
        uint8 _bReady{ SW_FALSE };
    };
} // namespace sw

namespace sw
{
    /** @brief 로비 목록(`kLobbyList`)과 줄서기(`kQueueJoin`)의 요청입니다. 목록은 지역을 비웁니다. */
    REFLECT()
    struct MatchmakingModeRequest
    {
        REFLECT_BODY();
        PROPERTY( MaxCount = MatchmakingLimit::kMaxIDSize )
        string _modeID{}; ///< 상한은 `MatchmakingLimit::kMaxIDSize` 와 같다
        PROPERTY( MaxCount = ServerRecord::kMaxNameSize )
        string _region{}; ///< 상한은 `ServerRecord::kMaxNameSize` 와 같다
    };
} // namespace sw

namespace sw
{
    /** @brief 서버 버스(대기열 취소 주제)의 본문입니다. 권한 서버가 대기열에서 그 티켓을 뺍니다. */
    REFLECT()
    struct SW_KIT_API MatchTicketCancel
    {
        REFLECT_BODY();
        static constexpr uint32 kBusVersion = 1; ///< 서버 버스의 의미 버전. 버스에만 쓰므로 `MessageTypes` 에 없다
        PROPERTY()
        uint64 _ticketID{ 0 };
    };
} // namespace sw

namespace sw
{
    /** @brief 모든 메서드의 응답입니다. 메서드마다 쓰는 필드만 채웁니다. 실패하면 `_result` 만 의미가 있습니다. */
    REFLECT()
    struct MatchmakingReply
    {
        REFLECT_BODY();
        PROPERTY( MaxCount = PartyLobbyLimit::kMaxLobbyList )
        vector<LobbySnapshot> _listLobby{}; ///< LobbyList. 상한은 `PartyLobbyLimit::kMaxLobbyList` 와 같다
        PROPERTY()
        PartySnapshot _party{};
        PROPERTY()
        LobbySnapshot _lobby{};
        PROPERTY()
        uint64 _ticketID{ 0 }; ///< QueueJoin 과 QueueLeave 의 티켓 id 이다
        PROPERTY()
        MatchmakingResult _result{ MatchmakingResult::Ok };
    };
} // namespace sw

namespace sw
{
    /**
     * @struct MatchmakingProtocol
     * @brief 매칭의 메시지 타입 목록, 등록 항목, 캐시 기록 코덱입니다. 본문은 고정 배치형(B)이며 `MessageTypes::write` · `read` 로만 쓰고 읽습니다.
     * @details 알림 본문은 `PartySnapshot`, `PartyInvite`, `LobbySnapshot`, `MatchAssignment` 입니다. 서버 버스 본문(`MatchTicket`, `MatchTicketCancel`, `MatchFormed`, `MatchAssignment`)은
     *          B 가 아니라 주제 정의(`MatchQueueBus`)로 쓰며, 버스에만 쓰는 타입은 이 목록에 없습니다.
     */
    struct SW_KIT_API MatchmakingProtocol
    {
        using MessageTypes = OnlineMessageTypes<MatchmakingIDRequest, MatchmakingReadyRequest, MatchmakingModeRequest, LobbySnapshot, MatchmakingReply, PartySnapshot, PartyInvite,
                                                MatchAssignment>;

        static const OnlineProtocolEntry kOnlineEntry; ///< 온라인 스키마 해시에 섞이는 이 키트의 등록 항목. 정의가 `.cpp` 에 있어 모듈이 오를 때 등록된다

        /**
         * @brief 캐시 기록을 읽고 씁니다. 배포 중에 다른 빌드의 서버가 읽을 수 있으므로 자기 기술형(A)이며 기록 버전을 머리에 둡니다.
         * @details 다시 만들 수 있는 캐시이므로 `ServiceRecordUtil::readCache` 로 읽습니다. 다른 버전이거나 모르는 필드가 있거나 선언된 범위(멤버 수 같은)를
         *          벗어나면 로그 없이 false 이고, 읽는 쪽은 그 파티나 로비가 없는 것으로 봅니다.
         */
        [[nodiscard]] static bool encodeParty( const PartySnapshot& party, vector<uint8>& outBytes );
        [[nodiscard]] static bool decodeParty( const vector<uint8>& bytes, PartySnapshot& outParty );
        [[nodiscard]] static bool encodeLobby( const LobbySnapshot& lobby, vector<uint8>& outBytes );
        [[nodiscard]] static bool decodeLobby( const vector<uint8>& bytes, LobbySnapshot& outLobby );
        /** @brief 캐시 값 하나에 포함된 id(16진 문자열)입니다. 계정에서 파티로 가는 인덱스와 임대 주인이 쓰는 값입니다. */
        static vector<uint8>      encodeID( uint64 id );
        [[nodiscard]] static bool decodeID( const vector<uint8>& bytes, uint64& outID );

        /** @brief 공통 오류 코드(`OnlineError`)를 결과로 바꿉니다. 손상된 본문은 Invalid, 충돌은 Conflict, 나머지(로그인, 전송, 저장소, 시한)는 Unavailable 입니다. */
        static MatchmakingResult fromErrorCode( uint16 errorCode );
    };
} // namespace sw
