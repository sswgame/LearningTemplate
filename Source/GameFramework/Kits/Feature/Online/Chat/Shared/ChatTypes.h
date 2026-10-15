/**
 * @file ChatTypes.h
 * @brief 채팅의 공통 타입입니다. 결과, 채널 종류, 채널 id 규칙, 메시지, 상한을 정의합니다.
 * @details 채널 id 는 버스 주제와 저장소 키에 그대로 들어가므로 `[0-9a-z_.]` 형식이고 48바이트 이하이다.
 *          `world.<이름>` 은 서버 전체가 쓰는 채널이고, `guild.<16진>` 은 길드 채널이며 서버 시스템이 멤버를 넣는다.
 *          `party.<16진>` 은 파티 채널이며 길드와 같고, `custom.<이름>` 은 누구나 들어올 수 있는 채널이다.
 *          귓속말은 채널이 아니라 (보낸 사람, 받는 사람) 쌍이다. 기록 키는 `whisper.<작은 계정 16진>.<큰 계정 16진>` 이다.
 */
#pragma once
#include "Core/Common/Types.h"
#include "Core/Container/string.h"

#include "Engine/Reflection/ReflectionMacros.h"

#include "GameFramework/Base/Online/Identity/AccountDirectory.h"
#include "GameFramework/GameFrameworkExports.h"

namespace sw
{
    /** @brief 채팅 요청의 결과입니다. 응답 본문의 첫 값이며 0은 성공입니다. 와이어 형식이므로 순서를 바꾸지 않고 새 값은 끝에만 추가합니다. */
    ENUM()
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
        Invalid,       ///< 빈 글이거나 상한을 넘었거나 잘못된 UTF-8 이거나 규칙에 맞지 않는 채널 id 이다
        Unavailable,   ///< 저장소, 캐시, 전송을 쓸 수 없다
        TooManyChannels,
        NotSignedIn ///< 로그인하지 않은 연결이다(공통 오류 `kUnauthenticated`)
    };

    /** @brief 채널 종류입니다. 채널 id 의 접두어가 정합니다. */
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
    };
} // namespace sw

namespace sw
{
    /** @brief 메시지 하나입니다. (보낸 시각, 서버, 서버 안 순번)이 같은 채널 안의 시간 순 정렬 키입니다. */
    struct ChatMessage
    {
        string          _channelID{}; ///< 귓속말이면 기록 키(`ChatChannelID::makeWhisper`)이다
        string          _senderName{};
        string          _text{}; ///< 필터를 거친 글이다
        AccountID       _senderID{ kInvalidAccountID };
        AccountID       _recipientID{ kInvalidAccountID }; ///< 귓속말일 때만 쓴다
        int64           _sentMs{ 0 };
        uint64          _serverID{ 0 };
        uint32          _sequence{ 0 };
        ChatChannelKind _kind{ ChatChannelKind::World };
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
