/**
 * @file ChatTypes.h
 * @brief 채팅의 공통 타입 — 결과 · 채널 종류 · 채널 id 규칙 · 메시지 · 상한입니다.
 * @details 채널 id 는 버스 주제 · 저장소 키에 그대로 들어간다 — `[0-9a-z_.]`, 48 바이트 이하:
 *          `world.<이름>`(서버 무리 전체), `guild.<16 진>`(길드 — 서버 시스템이 회원을 넣는다), `party.<16 진>`(파티 — 같음), `custom.<이름>`(누구나 들어옴).
 *          귓속말은 채널이 아니라 (보낸 이, 받는 이) 쌍이다 — 기록 키는 `whisper.<작은 계정 16 진>.<큰 계정 16 진>`.
 */
#pragma once
#include "Core/Common/Types.h"
#include "Core/Container/string.h"

#include "GameFramework/Base/Online/Identity/AccountDirectory.h"
#include "GameFramework/GameFrameworkExports.h"

namespace sw
{
    /** @brief 채팅 요청의 결과입니다(키트 오류 코드 = `OnlineMethodRange::kChat` + 값 — 0 은 성공). 와이어 형식이다. */
    enum class ChatResult : uint8
    {
        Ok = 0,
        NotMember,     ///< 들어가지 않은 채널에 말했다
        NotJoinable,   ///< 클라이언트가 들어갈 수 없는 채널(길드 · 파티는 서버가 넣는다)
        Muted,         ///< 채팅 금지(제재)
        RateLimited,   ///< 도배 — 기다릴 ms 가 함께
        Repeated,      ///< 같은 글 반복
        Rejected,      ///< 금칙어(거절 방식일 때)
        TargetOffline, ///< 귓속말 상대가 없다
        Blocked,       ///< 상대가 나를 막았다 — 보낸 이에게는 TargetOffline 으로 답한다(막힌 것을 드러내지 않는다)
        Invalid,       ///< 빈 글 · 상한 · 잘못된 UTF-8 · 규칙 밖 채널 id
        Unavailable,   ///< 저장소(기록 읽기)
        TooManyChannels
    };

    SW_GF_API const utf8* toString( ChatResult result );

    /** @brief 채널 종류입니다(채널 id 의 접두가 정한다). */
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
    /** @brief 채팅의 상한입니다. */
    struct ChatLimit
    {
        static constexpr int32 kMaxTextSize         = 512; ///< UTF-8 바이트
        static constexpr int32 kMaxChannelIdSize    = 48;
        static constexpr int32 kMaxChannelPerMember = 16; ///< 한 계정이 들어가 있는 채널
        static constexpr int32 kMaxHistoryPage      = 50;
    };
} // namespace sw

namespace sw
{
    /** @brief 메시지 하나입니다. (보낸 시각, 서버, 서버 안 순번)이 같은 채널 안의 시간 순 정렬 키다. */
    struct ChatMessage
    {
        string          _channelId{}; ///< 귓속말이면 기록 키(`ChatChannelId::makeWhisper`)
        string          _senderName{};
        string          _text{}; ///< 거르개를 지난 글
        AccountId       _senderId{ kInvalidAccountId };
        AccountId       _recipientId{ kInvalidAccountId }; ///< 귓속말만
        int64           _sentMs{ 0 };
        uint64          _serverId{ 0 };
        uint32          _sequence{ 0 };
        ChatChannelKind _kind{ ChatChannelKind::World };
    };
} // namespace sw

namespace sw
{
    /** @struct ChatChannelId @brief 채널 id 규칙입니다. */
    struct SW_GF_API ChatChannelId
    {
        /** @brief 규칙에 맞으면 종류를 돌려줍니다. 귓속말 기록 키도 읽습니다. 접두만 있고 이름이 빈 id 는 거절합니다. */
        [[nodiscard]] static bool parseKind( string_view channelId, ChatChannelKind& outKind );
        static string             makeGuild( uint64 guildId );
        static string             makeParty( uint64 partyId );
        /** @brief 귓속말 기록 키 — 두 계정의 순서와 무관하게 같은 키입니다. */
        static string makeWhisper( AccountId first, AccountId second );
    };
} // namespace sw
