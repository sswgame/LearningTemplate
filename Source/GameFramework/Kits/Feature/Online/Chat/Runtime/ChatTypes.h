/**
 * @file ChatTypes.h
 * @brief 채팅의 공통 타입입니다. 결과, 채널 종류, 채널 id 규칙, 메시지, 상한을 정의합니다.
 * @details 채널 id 는 버스 주제와 저장소 키에 그대로 들어가므로 `[0-9a-z_.]` 형식이고 48바이트 이하이다.
 *          `world.<이름>` 은 서버 전체가 쓰는 채널이고, `guild.<16진>` 은 길드 채널이며 서버 시스템이 멤버를 넣는다.
 *          `party.<16진>` 은 파티 채널이며 길드와 같고, `custom.<이름>` 은 누구나 들어올 수 있는 채널이다.
 *          귓속말은 채널이 아니라 (보낸 사람, 받는 사람) 쌍이다. 기록 키는 `whisper.<작은 계정 16진>.<큰 계정 16진>` 이다.
 */
#pragma once
#include "Core/Container/string.h"
#include "Core/Foundation/Types.h"

#include "Engine/Reflection/ReflectionMacros.h"

#include "GameFramework/GameFrameworkExports.h"
#include "GameFramework/Runtime/Online/Identity/AccountIdentity.h"
#include "GameFramework/Runtime/Online/Report/ServiceReport.h"
#include "GameFramework/Runtime/Online/Service/OnlineLimit.h"
#include "GameFramework/Runtime/Online/Service/OnlineProtocol.h"
#include "GameFramework/Runtime/Online/Store/ServiceRecordUnknownFields.h"

namespace sw
{
    /** @brief 채팅 요청의 결과입니다. 응답 본문의 첫 값이며 0은 성공입니다. 와이어 형식이므로 순서를 바꾸지 않고 새 값은 끝에만 추가합니다. */
    ENUM( Count = Count )
    enum class ChatResult : uint8
    {
        Ok = 0,
        NotMember,     ///< 들어가지 않은 채널에 말했다
        NotJoinable,   ///< 클라이언트가 들어갈 수 없는 채널이다. 길드와 파티 채널은 서버가 멤버를 넣는다
        Muted,         ///< 채팅 금지(제재) 상태이다
        RateLimited,   ///< 도배이다. 기다릴 시간(ms)이 함께 온다
        Repeated,      ///< 같은 글을 반복했다
        Rejected,      ///< 금칙어이다. 거절 방식일 때 나온다
        TargetOffline, ///< 귓속말 상대가 없다
        TooManyChannels,
        NotFound, ///< 신고한 메시지가 기록에 없다(아직 쓰이지 않았거나 보존 기간이 지났다)
        Count
    };

    /**
     * @brief 채팅 결과입니다. 업무 결과(`ChatResult`) 또는 공통 범주(`OnlineError`)입니다.
     * @details 빈 글 · 상한 초과 · 잘못된 UTF-8 · 규칙 밖 채널 id 는 `kInvalidRequest`, 저장소 · 캐시 · 전송 장애는 `kUnavailable`, 로그인 없음은 `kUnauthenticated` 이다.
     *          서버 로직 · 클라이언트 모델(`ChatLogModel`)이 같은 형식을 쓴다.
     */
    using ChatServiceResult = OnlineServiceResult<ChatResult>;

    /** @brief 채널 종류입니다. 채널 id 의 접두어가 정합니다. */
    ENUM( Count = Count )
    enum class ChatChannelKind : uint8
    {
        World = 0,
        Guild,
        Party,
        Custom,
        Whisper,
        Count
    };
} // namespace sw

namespace sw
{
    /** @brief 채팅의 상한 값들입니다. */
    struct ChatLimit
    {
        static constexpr int32 kMaxTextSize         = 512; ///< UTF-8 바이트 수이다
        static constexpr int32 kMaxChannelIDSize    = 48;
        static constexpr int32 kMaxChannelPerMember = 16; ///< 한 계정이 들어가 있을 수 있는 채널 수이다
        static constexpr int32 kMaxHistoryPage      = 50;
        static constexpr int32 kReportContextCount  = 10; ///< 신고 스냅숏에 담는 메시지 수(신고한 메시지와 그 앞)이다. `ServiceReportConstant::kMaxLineCount` 이하이다
        static constexpr int32 kMaxReasonSize       = 64; ///< 신고 사유 코드의 상한이며 `ServiceReportConstant::kMaxReasonSize` 와 같다
    };

    static_assert( ChatLimit::kMaxTextSize == ServiceReportConstant::kMaxLineTextSize && ChatLimit::kMaxReasonSize == ServiceReportConstant::kMaxReasonSize,
                   "the chat text limit and the report context line limit, and the chat report reason limit and the report reason limit, are one wire value each" );
} // namespace sw

namespace sw
{
    /**
     * @brief 메시지 하나입니다. (보낸 시각, 서버, 서버 안 순번)이 같은 채널 안의 시간 순 정렬 키입니다.
     * @details 알림, 응답, 서버 버스에는 고정 배치형(B)으로, 저장소의 기록 레코드에는 자기 기술형(A, `ChatProtocol::encodeRecord`)으로 실립니다.
     *          문자열 상한은 `ChatLimit` 와 `OnlineLimit::kMaxDisplayNameSize` 와 같은 값입니다. 가린 글은 코드 포인트마다 '*' 하나이므로 원문보다 길지 않습니다.
     */
    REFLECT()
    struct SW_KIT_API ChatMessage
    {
        REFLECT_BODY();
        static constexpr uint32 kBusVersion = 1; ///< 서버 버스(`ChatBus`)의 의미 버전. 기록 레코드의 판과 따로다
        PROPERTY( MaxCount = ChatLimit::kMaxChannelIDSize )
        string _channelID{}; ///< 귓속말이면 기록 키(`ChatChannelID::makeWhisper`)이다
        PROPERTY( MaxCount = OnlineLimit::kMaxDisplayNameSize )
        string _senderName{};
        PROPERTY( MaxCount = ChatLimit::kMaxTextSize )
        string _text{}; ///< 필터를 거친 글이다
        PROPERTY()
        AccountID _senderID{ kInvalidAccountID };
        PROPERTY()
        AccountID _recipientID{ kInvalidAccountID }; ///< 귓속말일 때만 쓴다
        PROPERTY()
        int64 _sentMs{ 0 };
        PROPERTY()
        uint64 _serverID{ 0 };
        PROPERTY()
        uint32 _sequence{ 0 };
        PROPERTY()
        ChatChannelKind _kind{ ChatChannelKind::World };
        /// 이 빌드가 모르는 칸(배포 중 새 빌드가 더한 칸)이다. PROPERTY 가 아니다 — 영속 읽기가 담고 쓰기가 그대로 싣는다(`ServiceRecordUtil`).
        ServiceRecordUnknownFields _unknownProperties{};
    };
} // namespace sw

namespace sw
{
    /** @struct ChatChannelID @brief 채널 id 규칙입니다. */
    struct SW_KIT_API ChatChannelID
    {
        /** @brief 규칙에 맞으면 종류를 반환합니다. 귓속말 기록 키도 읽습니다. 접두어만 있고 이름이 빈 id 는 거절합니다. */
        [[nodiscard]] static bool parseKind( string_view channelID, ChatChannelKind& outKind );
        static string             makeGuild( uint64 guildID );
        static string             makeParty( uint64 partyID );
        /** @brief 귓속말 기록 키를 만듭니다. 두 계정의 순서와 무관하게 같은 키가 나옵니다. */
        static string makeWhisper( AccountID first, AccountID second );
    };
} // namespace sw
