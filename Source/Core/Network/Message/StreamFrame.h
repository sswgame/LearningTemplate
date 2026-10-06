/**
 * @file StreamFrame.h
 * @brief 스트림 위의 길이 접두 프레임 — `[u32 길이(LE, 뒤따르는 바이트)][u8 종류][u8 깃발][몸]`. 상한을 넘는 길이 · 모르는 종류 · 모르는 깃발은 거절합니다.
 * @details 길이 칸의 상한 검사는 **몸을 받기 전에** 한다 — 4 바이트만 와도 4 GB 를 잡지 않는다.
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"
#include "Core/Container/vector.h"

namespace sw
{
    /** @brief 프레임 종류입니다. 끊는 까닭은 프레임 종류가 아니라 키트 메시지로 보낸다(중복 로그인 등) — 그 뒤 우아하게 닫는다. */
    enum class StreamFrameKind : uint8
    {
        Message = 0, ///< 키트 메시지(몸의 해석은 키트 — 보통 첫 2 바이트가 메시지 id)
        Request,     ///< `NetRequestClient` → 서버
        Response,    ///< 서버 → `NetRequestClient`
        Cancel,      ///< 요청 취소
        Ping,        ///< 끝점이 스스로 답한다(키트는 보지 않는다)
        Pong,
        Count
    };

    struct StreamFrameFlag
    {
        static constexpr uint8 kCompressed = 0x01;        ///< 몸이 압축 봉투다(`NetCompressionUtil`) — 받는 쪽은 자기 설정과 상관없이 푼다
        static constexpr uint8 kKnownMask  = kCompressed; ///< 모르는 깃발 비트는 거절한다
    };
} // namespace sw

namespace sw
{
    struct StreamFrameConstant
    {
        static constexpr int32 kHeaderSize          = 6;           ///< 길이 4 + 종류 1 + 깃발 1
        static constexpr int32 kDefaultMaxFrameSize = 1024 * 1024; ///< 몸 상한
    };
} // namespace sw

namespace sw
{
    /** @brief 해독한 프레임 하나 — 몸은 해독기 버퍼를 가리키며 다음 `append` 전까지 유효합니다. */
    struct StreamFrameView
    {
        const uint8*    _pBody{ nullptr };
        int32           _bodySize{ 0 };
        StreamFrameKind _kind{ StreamFrameKind::Count };
        uint8           _flags{ 0 };
    };

    enum class StreamFrameDecodeResult : uint8
    {
        Frame = 0, ///< `outFrame` 에 하나
        NeedMore,  ///< 더 받아야 한다
        Malformed  ///< 상한 초과 · 모르는 종류 · 모르는 깃발 — 연결을 닫는다(ProtocolError)
    };
} // namespace sw

namespace sw
{
    /** @brief 받은 바이트를 붙여 프레임을 하나씩 꺼냅니다(연결마다 하나, I/O 스레드). */
    class SW_API StreamFrameDecoder
    {
    public:
        explicit StreamFrameDecoder( int32 maxBodySize = StreamFrameConstant::kDefaultMaxFrameSize );

        void                    append( const uint8* pData, int32 size );
        StreamFrameDecodeResult next( StreamFrameView& outFrame );
        int32                   getBufferedBytes() const;

    private:
        vector<uint8> _bytes;
        int32         _readOffset;
        int32         _maxBodySize;
    };
} // namespace sw

namespace sw
{
    struct SW_API StreamFrameEncoder
    {
        /** @brief @p outBytes 끝에 프레임 하나를 붙입니다. 몸이 @p maxBodySize 를 넘으면 붙이지 않고 false 입니다. */
        [[nodiscard]] static bool appendFrame( vector<uint8>& outBytes, StreamFrameKind kind, uint8 flags, const uint8* pBody, int32 bodySize,
                                               int32 maxBodySize = StreamFrameConstant::kDefaultMaxFrameSize );
    };
} // namespace sw
