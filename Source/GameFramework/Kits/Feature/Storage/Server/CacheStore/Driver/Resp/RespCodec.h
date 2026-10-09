/**
 * @file RespCodec.h
 * @brief RESP2 인코더 · 증분 파서 — Valkey · Garnet 이 함께 말하는 다섯 종류(`+` `-` `:` `$` `*`)만 다룹니다.
 * @details - 명령은 늘 벌크 문자열 배열이다(`*3\r\n$3\r\nSET\r\n…`) — 인라인 명령은 쓰지 않는다.
 *          - 파서는 바이트가 몇 조각으로 와도 같은 값을 낸다(한 바이트씩 와도). 끝나지 않은 값은 버퍼에 두고 `NeedMore` 다.
 *          - 상한: 벌크 1 MiB · 줄 64 KiB · 배열 원소 2^20 · 중첩 8 — 넘으면 `Error`(연결을 닫는다). 계약 값 상한 64 KiB 의 넉넉한 위다.
 */
#pragma once
#include "Core/Common/Types.h"
#include "Core/Container/string.h"
#include "Core/Container/vector.h"

#include "GameFramework/GameFrameworkExports.h"

namespace sw
{
    /** @brief RESP2 값의 종류입니다. `$-1` · `*-1` 은 둘 다 `Null` 입니다. */
    enum class RespType : uint8
    {
        SimpleString = 0,
        Error,
        Integer,
        BulkString,
        Array,
        Null
    };
} // namespace sw

namespace sw
{
    /** @brief RESP2 값 하나입니다. */
    struct SW_GF_API RespValue
    {
        vector<uint8>     _bytes{};       ///< SimpleString · Error · BulkString 의 내용
        vector<RespValue> _listElement{}; ///< Array
        int64             _integer{ 0 };  ///< Integer
        RespType          _type{ RespType::Null };

        bool isNull() const { return _type == RespType::Null; }
        bool isError() const { return _type == RespType::Error; }
        /** @brief SimpleString · BulkString · Error 의 내용을 글로 봅니다. */
        string_view getText() const;
        /** @brief 내용이 @p text 와 같은 SimpleString · BulkString 인가입니다(`OK` · `QUEUED` · `message`). */
        bool isText( string_view text ) const;
        /** @brief BulkString 의 내용을 정수로 읽습니다(`ZSCORE` 의 "20" · "1e+16"). 정수가 아니거나 ±2^53 밖이면 false. */
        [[nodiscard]] bool tryReadIntegerText( int64& outValue ) const;
    };
} // namespace sw

namespace sw
{
    /**
     * @class RespCommand
     * @brief 명령 하나(벌크 문자열 배열)를 짓습니다 — 인자를 더하고 `appendTo` 로 선 바이트를 붙입니다.
     */
    class SW_GF_API RespCommand
    {
    public:
        explicit RespCommand( string_view name );

        RespCommand& addText( string_view text );
        RespCommand& addBytes( const uint8* pData, size_t size );
        RespCommand& addBytes( const vector<uint8>& bytes ) { return addBytes( bytes.data(), bytes.size() ); }
        RespCommand& addInteger( int64 value );

        /** @brief 인자 수 머리 줄(`*N` + CRLF)과 인자들을 @p outBytes 뒤에 붙입니다. */
        void appendTo( vector<uint8>& outBytes ) const;

        int32 getArgumentCount() const { return _argumentCount; }

    private:
        vector<uint8> _argumentBytes;
        int32         _argumentCount;
    };
} // namespace sw

namespace sw
{
    /** @brief 파서 한 걸음의 결과입니다. */
    enum class RespParseResult : uint8
    {
        Complete = 0, ///< 값 하나를 꺼냈다
        NeedMore,     ///< 아직 끝나지 않았다 — 더 받아야 한다
        Error         ///< 깨진 프레임 · 상한 — 연결을 닫는다(이후 `next` 도 Error)
    };
} // namespace sw

namespace sw
{
    /**
     * @class RespParser
     * @brief 증분 파서입니다. 받은 바이트를 `append` 하고 `next` 를 `NeedMore` 까지 부릅니다. 한 스레드가 씁니다.
     */
    class SW_GF_API RespParser
    {
    public:
        static constexpr int64 kMaxBulkSize    = 1024ll * 1024;
        static constexpr int64 kMaxLineSize    = 64ll * 1024;
        static constexpr int64 kMaxArrayCount  = 1 << 20;
        static constexpr int32 kMaxNestedDepth = 8;

        RespParser();

        void            append( const uint8* pData, size_t size );
        RespParseResult next( RespValue& outValue );
        /** @brief 버퍼와 오류 상태를 비웁니다(다시 연결한 새 스트림). */
        void reset();

        bool        hasFailed() const { return _bFailed == SW_TRUE; }
        const utf8* getFailureText() const { return _pFailureText; }

    private:
        /** @brief @p inoutOffset 에서 값 하나를 읽습니다. 끝나지 않았으면 NeedMore 이고 @p inoutOffset 은 그대로입니다. */
        RespParseResult parseValue( size_t& inoutOffset, int32 depth, RespValue& outValue );
        /** @brief CRLF 로 끝나는 줄 하나를 [@p outLineBegin, @p outLineEnd) 로 찾습니다. */
        RespParseResult findLine( size_t offset, size_t& outLineBegin, size_t& outLineEnd );
        RespParseResult fail( const utf8* pFailureText );

        vector<uint8> _buffer;
        const utf8*   _pFailureText;
        size_t        _readOffset;
        uint8         _bFailed;
    };
} // namespace sw
