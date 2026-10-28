/**
 * @file HTTP2Frame.h
 * @brief HTTP/2(RFC 9113) 프레임의 읽기 · 쓰기입니다. 연결 서문, 프레임 머리 9 바이트, SETTINGS · WINDOW_UPDATE · PING · HEADERS · DATA · GOAWAY · RST_STREAM 을 만들고 읽습니다.
 * @details APNs 제공자가 필요로 하는 HTTP/2 클라이언트의 첫 단계다(온라인 키트 G4). 이 파일은 바이트만 다루고 연결 · 흐름 제어 · 스트림 상태는 모른다 — 다음 단계(`HTTP2Connection`)가 얹는다.
 *          상태가 없고 입력은 부르는 쪽이 준 바이트뿐이라 네트워크 없이 테스트한다. 범위 밖 값(프레임 길이 24 비트 초과, 스트림 id 31 비트 초과)은 만들지 않고 false 로 거절한다.
 *          HTTP/2 의 TLS 협상(ALPN `h2`)은 TLS 계층(`TLSContextConfig::_alpnProtocol`)이 하고, 연결 · 스트림 상태는 `HTTP2Connection` 이 얹는다.
 */
#pragma once
#include "Core/Container/string.h"
#include "Core/Container/vector.h"
#include "Core/Foundation/Types.h"

#include "GameFramework/GameFrameworkExports.h"

namespace sw
{
    /** @brief 프레임 종류입니다(RFC 9113 6절의 번호 그대로). */
    enum class HTTP2FrameType : uint8
    {
        Data         = 0,
        Headers      = 1,
        Priority     = 2,
        RstStream    = 3,
        Settings     = 4,
        PushPromise  = 5,
        Ping         = 6,
        GoAway       = 7,
        WindowUpdate = 8,
        Continuation = 9
    };

    /** @brief 프레임 머리입니다. */
    struct HTTP2FrameHeader
    {
        uint32 _length{ 0 };   ///< 본문 길이(24 비트)
        uint32 _streamID{ 0 }; ///< 31 비트. 0 은 연결 전체
        uint8  _type{ 0 };     ///< `HTTP2FrameType` 의 번호(모르는 종류도 그대로 담는다 — 무시는 부르는 쪽이 정한다)
        uint8  _flags{ 0 };
    };
} // namespace sw

namespace sw
{
    /** @brief SETTINGS 항목 하나입니다. */
    struct HTTP2Setting
    {
        uint16 _id{ 0 };
        uint32 _value{ 0 };
    };
} // namespace sw

namespace sw
{
    /** @brief GOAWAY 프레임의 내용입니다. */
    struct HTTP2GoAway
    {
        uint32 _lastStreamID{ 0 };
        uint32 _errorCode{ 0 };
    };
} // namespace sw

namespace sw
{
    /** @struct HTTP2FrameUtil @brief 프레임 바이트를 만들고 읽습니다. 상태가 없습니다. */
    struct SW_GF_API HTTP2FrameUtil
    {
        static constexpr size_t      kHeaderSize          = 9;
        static constexpr uint32      kMaxFrameLength      = 0xFFFFFFu; ///< 24 비트
        static constexpr uint32      kMaxStreamID         = 0x7FFFFFFFu;
        static constexpr uint32      kDefaultMaxFrameSize = 16384; ///< 상대가 SETTINGS 로 올리기 전의 프레임 본문 상한
        static constexpr string_view kConnectionPreface   = "PRI * HTTP/2.0\r\n\r\nSM\r\n\r\n";

        static constexpr uint8 kFlagEndStream  = 0x1; ///< DATA · HEADERS
        static constexpr uint8 kFlagAck        = 0x1; ///< SETTINGS · PING
        static constexpr uint8 kFlagEndHeaders = 0x4; ///< HEADERS · CONTINUATION
        static constexpr uint8 kFlagPadded     = 0x8;
        static constexpr uint8 kFlagPriority   = 0x20; ///< HEADERS

        static constexpr uint16 kSettingsHeaderTableSize      = 0x1;
        static constexpr uint16 kSettingsEnablePush           = 0x2;
        static constexpr uint16 kSettingsMaxConcurrentStreams = 0x3;
        static constexpr uint16 kSettingsInitialWindowSize    = 0x4;
        static constexpr uint16 kSettingsMaxFrameSize         = 0x5;

        /** @brief 빅 엔디언 32 비트 정수 하나를 읽습니다(@p pBytes 는 4 바이트 이상). */
        [[nodiscard]] static uint32 readUint32( const uint8* pBytes );
        /** @brief @p header 를 9 바이트로 @p outBytes 뒤에 붙입니다. 길이가 24 비트를, 스트림 id 가 31 비트를 넘으면 쓰지 않고 false 입니다. */
        [[nodiscard]] static bool writeHeader( const HTTP2FrameHeader& header, vector<uint8>& outBytes );
        /** @brief 앞 9 바이트를 읽습니다. 바이트가 모자라면 false 입니다(아직 다 오지 않았다). 예약 비트(스트림 id 의 최상위)는 버린다. */
        [[nodiscard]] static bool readHeader( const uint8* pBytes, size_t size, HTTP2FrameHeader& outHeader );

        /** @brief SETTINGS 프레임(스트림 0)입니다. @p bAck 면 본문이 없는 확인입니다(@p listSetting 은 비어 있어야 한다). */
        [[nodiscard]] static bool writeSettings( const vector<HTTP2Setting>& listSetting, bool bAck, vector<uint8>& outBytes );
        /** @brief SETTINGS 본문(6 바이트 항목의 나열)을 읽습니다. 길이가 6 의 배수가 아니면 false 입니다. */
        [[nodiscard]] static bool readSettings( const uint8* pBody, size_t size, vector<HTTP2Setting>& outListSetting );
        /** @brief WINDOW_UPDATE 입니다. 증가량은 1..2^31-1 이어야 한다(0 이거나 넘으면 false). @p streamID 0 은 연결 창이다. */
        [[nodiscard]] static bool writeWindowUpdate( uint32 streamID, uint32 increment, vector<uint8>& outBytes );
        /** @brief PING 입니다(본문 8 바이트). */
        [[nodiscard]] static bool writePing( const uint8 ( &arrOpaque )[8], bool bAck, vector<uint8>& outBytes );
        /** @brief HEADERS 프레임 하나(헤더 블록 전부를 한 프레임에 싣는다)입니다. 블록이 프레임 상한(@p maxFrameSize)을 넘으면 false — 나눠 보내는 CONTINUATION 은 아직 만들지 않는다. */
        [[nodiscard]] static bool writeHeaders( uint32 streamID, const vector<uint8>& listHeaderBlock, bool bEndStream, uint32 maxFrameSize, vector<uint8>& outBytes );
        /** @brief DATA 프레임 하나입니다. 본문이 @p maxFrameSize 를 넘으면 false 입니다. */
        [[nodiscard]] static bool writeData( uint32 streamID, const uint8* pData, size_t size, bool bEndStream, uint32 maxFrameSize, vector<uint8>& outBytes );
        [[nodiscard]] static bool writeRstStream( uint32 streamID, uint32 errorCode, vector<uint8>& outBytes );
        /** @brief GOAWAY 본문(마지막 스트림 id 4 바이트 + 오류 코드 4 바이트 + 디버그 데이터)에서 앞 두 값을 읽습니다. 8 바이트보다 짧으면 false 입니다. */
        [[nodiscard]] static bool readGoAway( const uint8* pBody, size_t size, HTTP2GoAway& outGoAway );
    };
} // namespace sw
