/**
 * @file ChatProtocol.h
 * @brief 채팅 키트의 와이어 형식입니다. 메서드와 알림 번호(영역 `OnlineMethodRange::kChat`), 응답 본문, 메시지 형식, 서버 버스 주제를 정의하며 클라이언트와 서버가 함께 씁니다.
 * @details - 응답 본문은 `ChatResult`(업무 결과, 거래와 경제 키트도 같다), 기다릴 시간(ms), 메서드별 필드로 이루어진다.
 *            말하기와 귓속말은 필터를 거친 메시지를 되울림으로 돌려주고, 기록은 메시지 목록과 다음 커서를 돌려준다.
 *            공통 오류(`OnlineError`)만 오류 코드로 오며 로그인 없음, 손상된 본문, 시한 초과가 해당한다. 클라이언트는 `fromErrorCode` 로 이를 결과에 맞춘다.
 *          - 메시지 형식 하나를 알림, 서버 간 버스, 기록 레코드가 함께 쓴다. 레코드는 앞에 형식 버전 1바이트가 붙는다.
 */
#pragma once
#include "Core/Common/Types.h"
#include "Core/Container/string.h"
#include "Core/Container/vector.h"

#include "GameFramework/Base/Online/Service/OnlineProtocol.h"
#include "GameFramework/GameFrameworkExports.h"
#include "GameFramework/Kits/Feature/Online/Chat/Shared/ChatTypes.h"

namespace sw
{
    class BitReader;
    class BitWriter;

    /** @brief 채팅 메서드와 알림 번호입니다. 와이어 값이므로 바꾸면 `ChatProtocol::kVersion` 을 올려야 합니다. */
    struct ChatMethod
    {
        static constexpr uint16 kJoin        = OnlineMethodRange::kChat + 0x01; ///< 채널 id
        static constexpr uint16 kLeave       = OnlineMethodRange::kChat + 0x02; ///< 채널 id
        static constexpr uint16 kSend        = OnlineMethodRange::kChat + 0x03; ///< 채널 id 와 글을 받아 거른 메시지(되울림)를 돌려준다
        static constexpr uint16 kWhisper     = OnlineMethodRange::kChat + 0x04; ///< 받는 계정과 글을 받아 거른 메시지를 돌려준다
        static constexpr uint16 kHistory     = OnlineMethodRange::kChat + 0x05; ///< 채널 id(귓속말은 기록 키), 커서, 개수를 받아 최근 메시지부터 목록과 다음 커서를 돌려준다
        static constexpr uint16 kPushMessage = OnlineMethodRange::kChat + 0x80; ///< 메시지 하나이다. 채널 메시지나 귓속말이다

        static_assert( OnlineMethodRange::isInRange( kHistory, OnlineMethodRange::kChat ) && OnlineMethodRange::isMethod( kHistory ) );
        static_assert( OnlineMethodRange::isMethod( kPushMessage ) == false );
    };
} // namespace sw

namespace sw
{
    /**
     * @struct ChatBus
     * @brief 서버 간 버스 주제입니다(`[0-9a-z_.]`). 채널 주제는 그 채널에 이 서버의 멤버가 있는 동안만 구독합니다.
     */
    struct ChatBus
    {
        static constexpr const utf8* kChannelTopicPrefix = "chat.channel."; ///< 뒤에 채널 id 를 붙인다. 채널 말하기이며 본문은 메시지이다
        static constexpr const utf8* kServerTopicPrefix  = "chat.server.";  ///< 뒤에 서버 16진 문자열을 붙인다. 그 서버에 접속한 계정에게 가는 귓속말이며 본문은 메시지이다
    };
} // namespace sw

namespace sw
{
    /** @brief 모든 메서드의 응답입니다. 메서드마다 쓰는 필드만 채웁니다. 실패하면 `_result` 와 `_retryAfterMs` 만 의미가 있습니다. */
    struct ChatReply
    {
        vector<ChatMessage> _listHistory{};     ///< History 의 결과이며 최근 것부터 담는다
        ChatMessage         _message{};         ///< Send 와 Whisper 의 결과이다. 필터를 거친 메시지(되울림)이다
        string              _nextCursor{};      ///< History 의 다음 커서이다. 비어 있으면 끝이다
        int64               _retryAfterMs{ 0 }; ///< Muted, RateLimited, Repeated 일 때 기다릴 시간(ms)이다
        ChatResult          _result{ ChatResult::Ok };
    };
} // namespace sw

namespace sw
{
    /** @brief 형식을 읽고 씁니다. 읽을 때 상한을 넘거나 모자라면 false 를 반환합니다. */
    struct SW_GF_API ChatProtocol
    {
        static constexpr uint32 kVersion       = 1;
        static constexpr int32  kMaxCursorSize = 128; ///< 기록 키의 상한이다. 채널 id 뒤에 `/` 와 16진 문자열 셋이 붙는다

        static void               writeMessage( BitWriter& outWriter, const ChatMessage& message );
        [[nodiscard]] static bool readMessage( BitReader& reader, ChatMessage& outMessage );
        /** @brief 기록 레코드를 읽고 씁니다. 형식 버전 1바이트 뒤에 메시지가 오며, 다른 버전은 읽지 않습니다. */
        static vector<uint8>      encodeRecord( const ChatMessage& message );
        [[nodiscard]] static bool decodeRecord( const vector<uint8>& bytes, ChatMessage& outMessage );

        /** @brief 응답 본문을 읽고 씁니다. 결과와 기다릴 시간(ms)이 먼저 오고, 성공이면 @p method 의 필드가 이어집니다. */
        static void               writeReply( BitWriter& outWriter, uint16 method, const ChatReply& reply );
        [[nodiscard]] static bool readReply( BitReader& reader, uint16 method, ChatReply& outReply );

        /** @brief 공통 오류 코드(`OnlineError`)를 결과로 바꿉니다. 로그인 없음, 손상된 본문, 도배 제한은 각각 대응하는 결과로, 나머지(전송, 저장소, 시한)는 Unavailable 로 바꿉니다. */
        static ChatResult fromErrorCode( uint16 errorCode );
        static string     makeChannelTopic( string_view channelID );
        static string     makeServerTopic( uint64 serverID );
    };
} // namespace sw
