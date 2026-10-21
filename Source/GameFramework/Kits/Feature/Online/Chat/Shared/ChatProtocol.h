/**
 * @file ChatProtocol.h
 * @brief 채팅 키트의 와이어 형식입니다. 메서드와 알림 번호(영역 `OnlineMethodRange::kChat`), 요청과 응답 본문, 메시지 형식, 서버 버스 주제를 정의하며 클라이언트와 서버가 함께 씁니다.
 * @details - 요청과 응답은 `REFLECT` 구조체 하나씩이고 본문은 고정 배치형(B)이다. 바이트 배치는 `PROPERTY` 선언 순서와 키가 정한다.
 *            응답 본문(`ChatReply`)은 업무 결과(`ChatResult`, 거래와 경제 키트도 같다), 기다릴 시간(ms), 메서드별 필드로 이루어진다.
 *            말하기와 귓속말은 필터를 거친 메시지를 되울림으로 돌려주고, 기록은 메시지 목록과 다음 커서를 돌려준다.
 *            공통 오류(`OnlineError`)만 오류 코드로 오며 로그인 없음, 손상된 본문, 시한 초과가 해당한다. 클라이언트는 `fromErrorCode` 로 이를 결과에 맞춘다.
 *          - 메시지 형식(`ChatMessage`) 하나를 알림, 서버 간 버스, 기록 레코드가 함께 쓴다. 알림은 B 이고,
 *            버스(배포 중 빌드가 섞인다)와 기록 레코드(저장소에 남아 다른 빌드가 읽는다)는 자기 기술형(A, `BinarySerializer`)이다.
 *            버스의 의미 버전은 `ChatMessage::kBusVersion`, 기록 레코드의 버전은 `kChatRecordVersion` 으로 따로다(버스는 이관을 보지 않는다).
 */
#pragma once
#include "Core/Common/Types.h"
#include "Core/Container/string.h"
#include "Core/Container/vector.h"

#include "Engine/Reflection/ReflectionMacros.h"

#include "GameFramework/Base/Online/Bus/ServerBusTopic.h"
#include "GameFramework/Base/Online/Service/OnlineProtocol.h"
#include "GameFramework/Base/Online/Service/OnlineProtocolRegistry.h"
#include "GameFramework/GameFrameworkExports.h"
#include "GameFramework/Kits/Feature/Online/Chat/Shared/ChatTypes.h"

namespace sw
{
    /** @brief 채팅 메서드 이름입니다. 열거자 순서가 와이어 번호(`OnlineMethodTable`)입니다. 새 메서드는 `Count` 앞에만 더합니다. */
    ENUM( Count = Count )
    enum class ChatMethodName : uint8
    {
        Join,
        Leave,
        Send,
        Whisper,
        History,
        Report,
        Count,
    };

    /** @brief 채팅 알림 이름입니다. 열거자 순서가 와이어 번호(`OnlineMethodTable`)입니다. 새 알림은 `Count` 앞에만 더합니다. */
    ENUM( Count = Count )
    enum class ChatPushName : uint8
    {
        Message,
        Count,
    };
} // namespace sw

namespace sw
{
    /** @brief 채팅 메서드와 알림 번호입니다. 번호는 열거자 순서에서 나오며 그 이름 순서가 온라인 스키마 해시에 섞입니다(`OnlineMethodTable`). */
    struct ChatMethod : OnlineMethodTable<ChatMethodName, OnlineMethodRange::kChat, ChatPushName>
    {
        static constexpr uint16 kJoin        = toMethod( ChatMethodName::Join );    ///< `ChatChannelRequest`
        static constexpr uint16 kLeave       = toMethod( ChatMethodName::Leave );   ///< `ChatChannelRequest`
        static constexpr uint16 kSend        = toMethod( ChatMethodName::Send );    ///< 채널 id 와 글을 받아 거른 메시지(되울림)를 돌려준다
        static constexpr uint16 kWhisper     = toMethod( ChatMethodName::Whisper ); ///< 받는 계정과 글을 받아 거른 메시지를 돌려준다
        static constexpr uint16 kHistory     = toMethod( ChatMethodName::History ); ///< 채널 id(귓속말은 기록 키), 커서, 개수를 받아 최근 메시지부터 목록과 다음 커서를 돌려준다
        static constexpr uint16 kReport      = toMethod( ChatMethodName::Report );  ///< `ChatReportRequest`. 메시지와 그 앞 기록을 스냅숏으로 신고 테이블에 남긴다
        static constexpr uint16 kPushMessage = toPush( ChatPushName::Message );     ///< 메시지 하나이다. 채널 메시지나 귓속말이다
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
        static constexpr ServerBusTopicFamily<ChatMessage> kChannelTopic{ "chat.channel." }; ///< 뒤에 채널 id 를 붙인다. 채널 말하기이다
        static constexpr ServerBusTopicFamily<ChatMessage> kServerTopic{ "chat.server." };   ///< 뒤에 서버 16진 문자열을 붙인다. 그 서버에 접속한 계정에게 가는 귓속말이다
    };
} // namespace sw

namespace sw
{
    /** @brief 채널에 들어가기와 나오기의 요청입니다. */
    REFLECT()
    struct ChatChannelRequest
    {
        REFLECT_BODY();
        PROPERTY( MaxCount = ChatLimit::kMaxChannelIDSize )
        string _channelID{}; ///< 상한은 `ChatLimit::kMaxChannelIDSize` 와 같다
    };
} // namespace sw

namespace sw
{
    /** @brief 채널에 말하기의 요청입니다. */
    REFLECT()
    struct ChatSendRequest
    {
        REFLECT_BODY();
        PROPERTY( MaxCount = ChatLimit::kMaxChannelIDSize )
        string _channelID{}; ///< 상한은 `ChatLimit::kMaxChannelIDSize` 와 같다
        PROPERTY( MaxCount = ChatLimit::kMaxTextSize )
        string _text{}; ///< 상한은 `ChatLimit::kMaxTextSize` 와 같다
    };
} // namespace sw

namespace sw
{
    /** @brief 귓속말의 요청입니다. */
    REFLECT()
    struct ChatWhisperRequest
    {
        REFLECT_BODY();
        PROPERTY()
        AccountID _recipientID{ kInvalidAccountID };
        PROPERTY( MaxCount = ChatLimit::kMaxTextSize )
        string _text{}; ///< 상한은 `ChatLimit::kMaxTextSize` 와 같다
    };
} // namespace sw

namespace sw
{
    /** @brief 기록 조회의 요청입니다. 1 보다 작은 개수는 서비스가 Invalid 로 끝냅니다. */
    REFLECT()
    struct ChatHistoryRequest
    {
        REFLECT_BODY();
        PROPERTY( MaxCount = ChatLimit::kMaxChannelIDSize )
        string _channelID{}; ///< 채널 id 이거나 귓속말 기록 키이다
        PROPERTY( MaxCount = ChatProtocol::kMaxCursorSize )
        string _cursor{}; ///< 상한은 `ChatProtocol::kMaxCursorSize` 와 같다
        PROPERTY( Min = 0, Max = 50 )
        int32 _maxCount{ 0 }; ///< 상한은 `ChatLimit::kMaxHistoryPage` 와 같다
    };
} // namespace sw

namespace sw
{
    /**
     * @brief 메시지 신고의 요청입니다. 메시지는 (채널, 보낸 시각, 서버, 순번)으로 고르며 받은 `ChatMessage` 의 값을 그대로 넘깁니다.
     * @details 신고자는 그 채널의 멤버이거나 귓속말의 당사자여야 합니다. 자기 메시지는 신고할 수 없습니다.
     */
    REFLECT()
    struct ChatReportRequest
    {
        REFLECT_BODY();
        PROPERTY( MaxCount = ChatLimit::kMaxChannelIDSize )
        string _channelID{}; ///< 채널 id 이거나 귓속말 기록 키이다
        PROPERTY( MaxCount = ChatLimit::kMaxReasonSize )
        string _reason{}; ///< 사유 코드 `[0-9a-z_.]` 이다. 상한은 `ChatLimit::kMaxReasonSize` 와 같다
        PROPERTY()
        int64 _sentMs{ 0 };
        PROPERTY()
        uint64 _serverID{ 0 };
        PROPERTY()
        uint32 _sequence{ 0 };
    };
} // namespace sw

namespace sw
{
    /** @brief 모든 메서드의 응답입니다. 메서드마다 쓰는 필드만 채웁니다. 실패하면 `_result` 와 `_retryAfterMs` 만 의미가 있습니다. */
    REFLECT()
    struct ChatReply
    {
        REFLECT_BODY();
        PROPERTY( MaxCount = ChatLimit::kMaxHistoryPage )
        vector<ChatMessage> _listHistory{}; ///< History 의 결과이며 최근 것부터 담는다. 상한은 `ChatLimit::kMaxHistoryPage` 와 같다
        PROPERTY()
        ChatMessage _message{}; ///< Send 와 Whisper 의 결과이다. 필터를 거친 메시지(되울림)이다
        PROPERTY( MaxCount = ChatProtocol::kMaxCursorSize )
        string _nextCursor{}; ///< History 의 다음 커서이다. 비어 있으면 끝이다
        PROPERTY()
        int64 _retryAfterMs{ 0 }; ///< Muted, RateLimited, Repeated 일 때 기다릴 시간(ms)이다
        PROPERTY()
        ChatResult _result{ ChatResult::Ok };
    };
} // namespace sw

namespace sw
{
    /**
     * @struct ChatProtocol
     * @brief 채팅 키트의 메시지 타입 목록, 등록 항목, 기록 레코드 코덱입니다. 본문은 고정 배치형(B)이며 `MessageTypes::write` · `read` 로만 쓰고 읽습니다.
     * @details 알림과 서버 버스의 본문은 `ChatMessage` 하나입니다. 버스는 B 가 아니라 주제 정의(`ChatBus`)로 씁니다.
     */
    struct SW_KIT_API ChatProtocol
    {
        using MessageTypes = OnlineMessageTypes<ChatChannelRequest, ChatSendRequest, ChatWhisperRequest, ChatHistoryRequest, ChatReportRequest, ChatReply, ChatMessage>;

        static const OnlineProtocolEntry kOnlineEntry;         ///< 온라인 스키마 해시에 섞이는 이 키트의 등록 항목. 정의가 `.cpp` 에 있어 모듈이 오를 때 등록된다
        static constexpr int32           kMaxCursorSize = 128; ///< 기록 키의 상한이다. 채널 id 뒤에 `/` 와 16진 문자열 셋이 붙는다

        /**
         * @brief 기록 레코드를 읽고 씁니다. 저장소에 남는 영속 레코드이므로 자기 기술형(A)이며 레코드 버전을 머리에 둡니다.
         * @details 다른 버전이거나 모르는 필드가 있거나 `ChatMessage` 에 선언된 범위를 벗어나면 손상으로 보고 오류 로그를 남기며 false 입니다(`ServiceRecordUtil::readDurable`).
         *          필드를 더하면 옛 레코드의 그 필드는 기본값으로 채워집니다. 필드를 빼거나 뜻을 바꾸면 버전을 올리고 데이터를 다시 씁니다.
         */
        [[nodiscard]] static bool encodeRecord( const ChatMessage& message, vector<uint8>& outBytes );
        [[nodiscard]] static bool decodeRecord( string_view key, const vector<uint8>& bytes, ChatMessage& outMessage );

        /** @brief 공통 오류 코드(`OnlineError`)를 결과로 바꿉니다. 로그인 없음, 손상된 본문, 도배 제한은 각각 대응하는 결과로, 나머지(전송, 저장소, 시한)는 Unavailable 로 바꿉니다. */
        static ChatResult fromErrorCode( uint16 errorCode );
        static string     makeChannelTopic( string_view channelID );
        static string     makeServerTopic( uint64 serverID );
    };
} // namespace sw
