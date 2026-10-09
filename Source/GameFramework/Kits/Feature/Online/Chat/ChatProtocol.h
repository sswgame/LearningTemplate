/**
 * @file ChatProtocol.h
 * @brief 채팅 키트의 와이어 — 메서드 · 알림 번호(영역 `OnlineMethodRange::kChat`) · 응답 몸 · 메시지 형식 · 서버 버스 주제입니다(클라이언트 · 서버가 같이 쓴다).
 * @details - 응답 몸 = `ChatResult`(업무 결과 — 거래 · 경제 키트와 같다) + 기다릴 ms + 메서드마다의 칸(말하기 · 귓속말: 거른 메시지 되울림, 기록: 메시지들 · 다음 커서).
 *            공통 오류(`OnlineError` — 로그인 없음 · 깨진 몸 · 시한)만 오류 코드로 오고, 클라이언트는 `fromErrorCode` 로 결과에 맞춘다.
 *          - 메시지 형식 하나를 알림 · 서버 간 버스 · 기록 레코드가 같이 쓴다(레코드는 앞에 형식 판 1 바이트).
 */
#pragma once
#include "Core/Common/Types.h"
#include "Core/Container/string.h"
#include "Core/Container/vector.h"

#include "GameFramework/Base/Online/Service/OnlineProtocol.h"
#include "GameFramework/GameFrameworkExports.h"
#include "GameFramework/Kits/Feature/Online/Chat/ChatTypes.h"

namespace sw
{
    class BitReader;
    class BitWriter;

    /** @brief 채팅 메서드 · 알림 번호입니다. 와이어 값 — 바꾸면 `ChatProtocol::kVersion` 을 올린다. */
    struct ChatMethod
    {
        static constexpr uint16 kJoin        = OnlineMethodRange::kChat + 0x01; ///< 채널 id
        static constexpr uint16 kLeave       = OnlineMethodRange::kChat + 0x02; ///< 채널 id
        static constexpr uint16 kSend        = OnlineMethodRange::kChat + 0x03; ///< 채널 id · 글 → 거른 메시지(되울림)
        static constexpr uint16 kWhisper     = OnlineMethodRange::kChat + 0x04; ///< 받는 계정 · 글 → 거른 메시지
        static constexpr uint16 kHistory     = OnlineMethodRange::kChat + 0x05; ///< 채널 id(귓속말은 기록 키) · 커서 · 개수 → 메시지들(최근 것부터) · 다음 커서
        static constexpr uint16 kPushMessage = OnlineMethodRange::kChat + 0x80; ///< 메시지 하나(채널 · 귓속말)

        static_assert( OnlineMethodRange::isInRange( kHistory, OnlineMethodRange::kChat ) && OnlineMethodRange::isMethod( kHistory ) );
        static_assert( OnlineMethodRange::isMethod( kPushMessage ) == false );
    };
} // namespace sw

namespace sw
{
    /**
     * @struct ChatBus
     * @brief 서버 간 버스 주제입니다(`[0-9a-z_.]`). 채널 주제는 그 채널에 이 서버 회원이 있는 동안만 구독한다.
     */
    struct ChatBus
    {
        static constexpr const utf8* kChannelTopicPrefix = "chat.channel."; ///< + 채널 id — 채널 말하기(몸 = 메시지)
        static constexpr const utf8* kServerTopicPrefix  = "chat.server.";  ///< + 서버 16 진 — 그 서버에 붙은 계정에게 가는 귓속말(몸 = 메시지)
    };
} // namespace sw

namespace sw
{
    /** @brief 모든 메서드의 응답(메서드마다 쓰는 칸만 찬다)입니다. 실패면 `_result` · `_retryAfterMs` 만 뜻이 있다. */
    struct ChatReply
    {
        vector<ChatMessage> _listHistory{};     ///< History — 최근 것부터
        ChatMessage         _message{};         ///< Send · Whisper — 거른 메시지(되울림)
        string              _nextCursor{};      ///< History — 비면 끝
        int64               _retryAfterMs{ 0 }; ///< Muted · RateLimited · Repeated — 기다릴 ms
        ChatResult          _result{ ChatResult::Ok };
    };
} // namespace sw

namespace sw
{
    /** @brief 형식 쓰기 · 읽기입니다. 읽기는 상한을 넘거나 모자라면 false 입니다. */
    struct SW_GF_API ChatProtocol
    {
        static constexpr uint32 kVersion       = 1;
        static constexpr int32  kMaxCursorSize = 128; ///< 기록 키(채널 id + `/` + 16 진 셋)

        static void               writeMessage( BitWriter& outWriter, const ChatMessage& message );
        [[nodiscard]] static bool readMessage( BitReader& reader, ChatMessage& outMessage );
        /** @brief 기록 레코드 — 형식 판 1 바이트 + 메시지입니다. 다른 판은 읽지 않는다. */
        static vector<uint8>      encodeRecord( const ChatMessage& message );
        [[nodiscard]] static bool decodeRecord( const vector<uint8>& bytes, ChatMessage& outMessage );

        /** @brief 응답 몸입니다(결과 · 기다릴 ms + 성공이면 @p method 의 칸). */
        static void               writeReply( BitWriter& outWriter, uint16 method, const ChatReply& reply );
        [[nodiscard]] static bool readReply( BitReader& reader, uint16 method, ChatReply& outReply );

        /** @brief 공통 오류 코드(`OnlineError`) → 결과입니다 — 로그인 없음 · 깨진 몸 · 도배 제한은 그 결과, 나머지(전송 · 저장소 · 시한)는 Unavailable 입니다. */
        static ChatResult fromErrorCode( uint16 errorCode );
        static string     makeChannelTopic( string_view channelId );
        static string     makeServerTopic( uint64 serverId );
    };
} // namespace sw
