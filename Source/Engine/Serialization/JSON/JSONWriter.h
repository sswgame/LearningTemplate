/**
 * @file JSONWriter.h
 * @brief JSON 을 앞에서부터 차례로 쓰는 쓰기기입니다. 트리를 만들지 않고 문자열 버퍼나 고정 버퍼에 바로 씁니다.
 * @details 문자열 값과 키는 늘 이스케이프되고, 쉼표 · 콜론 · 괄호는 쓰기기가 놓는다 — 부르는 쪽은 JSON 글을 이어 붙이지 않는다.
 *          트리를 고쳐 가며 만들 때는 `JSONDocument`, 크기가 큰 출력(트레이스)이나 할당이 없어야 하는 출력(행 감시 보고)은 이것을 쓴다.
 */
#pragma once
#include "Core/Container/string.h"
#include "Core/Foundation/Macros.h"
#include "Core/Foundation/Types.h"

#include <optional>

namespace sw
{
    class JSONValue;

    /**
     * @class JSONText
     * @brief `JSONWriter::finish` 가 돌려주는 다 쓴 JSON 글입니다. 쓰기기만 만들 수 있다.
     * @details JSON 을 받아 서명하거나 보내는 API(`JSONWebToken::makeSigned`)가 `string_view` 대신 이것을 받으면 이어 붙인 글이 들어올 자리가 없다.
     */
    class JSONText
    {
    public:
        string_view view() const { return _text; }

    private:
        friend class JSONWriter;
        explicit JSONText( string&& text )
            : _text{ std::move( text ) }
        {
        }

        string _text;
    };
} // namespace sw

namespace sw
{
    /**
     * @class JSONWriter
     * @brief `beginObject` · `key` · `value*` · `beginArray` 로 JSON 을 스트림처럼 씁니다.
     * @details
     * - 문자열: `"` · `\` · 제어 문자(`\b \f \n \r \t`, 나머지는 `\u00xx`)를 이스케이프하고 올바른 UTF-8 은 그대로 둔다. 잘못된 UTF-8 바이트는
     *   U+FFFD 이스케이프로 바꾼다(출력이 늘 올바른 JSON 이다). 이 규칙은 `JSONDocument::escapeString`(nlohmann `dump`)과 같은 바이트를 낸다.
     * - 실수: 가장 짧은 왕복 표기이고 정수처럼 보이면 `.0` 을 붙인다. NaN · 무한대는 JSON 에 없으므로 `null` 을 쓰고 실패로 남긴다(`isComplete` 가 false).
     * - 순서 위반(배열 안의 `key`, 객체 안의 키 없는 값, 짝이 안 맞는 `end*`, 두 번째 루트 값)은 단언이고 실패로 남긴다.
     * - 고정 버퍼 대상은 할당하지 않는다(`valueJSON` 제외). 넘치면 자르고 실패로 남기며 늘 0 으로 끝낸다.
     * - indent 가 음수이면 한 줄, 0 이상이면 `JSONDocument::dump( indent )` 와 같은 줄바꿈 · 들여쓰기(`"key": value`, 빈 컨테이너는 `{}` · `[]`)다.
     */
    class SW_API JSONWriter
    {
    public:
        /** @brief 중첩 깊이 한도입니다. 고정 배열이라 할당하지 않는다. */
        static constexpr uint32 kMaxDepth = 32;

        /** @brief 쓰기기가 가진 버퍼에 씁니다. 다 쓰면 `finish` 로 `JSONText` 를 꺼낸다(서명 · 전송할 페이로드). */
        explicit JSONWriter( int32 indent = -1 );
        /** @brief @p outJSON 끝에 덧붙여 씁니다. 큰 출력은 부르는 쪽이 `reserve` 해 둔다. */
        explicit JSONWriter( string& outJSON, int32 indent = -1 );
        /** @brief 고정 버퍼 @p pBuffer ( @p capacity 바이트, 끝 0 포함)에 처음부터 씁니다. 할당하지 않는다. */
        JSONWriter( utf8* pBuffer, uint32 capacity, int32 indent = -1 );

        JSONWriter( const JSONWriter& )            = delete;
        JSONWriter& operator=( const JSONWriter& ) = delete;

        void beginObject();
        void endObject();
        void beginArray();
        void endArray();
        /** @brief 객체 멤버 이름입니다. 다음 값 하나가 이 이름의 값이다. */
        void key( string_view name );

        void valueString( string_view value );
        void valueBool( bool value );
        void valueInt( int64 value );
        void valueUint( uint64 value );
        void valueFloat( float64 value );
        void valueNull();
        /**
         * @brief @p scaled / 10^@p fractionDigits 를 소수 @p fractionDigits 자리까지 늘 채워 씁니다(나노초 → 마이크로초 `1.500`).
         * @details 실수를 거치지 않아 반올림이 없다. @p fractionDigits 가 0 이면 정수다. 19 이상은 단언이다.
         */
        void valueFixedPoint( uint64 scaled, uint32 fractionDigits );
        /** @brief 이미 파싱된 값 하나를 그대로 씁니다(`JSONValue::dump`, 한 줄). 할당한다. */
        void valueJSON( const JSONValue& value );

        /** @brief 루트 값 하나를 다 닫았고 실패(순서 위반 · NaN · 넘침)가 없으면 true 입니다. */
        [[nodiscard]] bool isComplete() const;
        /**
         * @brief 가진 버퍼로 쓴 글을 `JSONText` 로 꺼냅니다. 완성되지 않았으면(`isComplete` 가 false) nullopt 이고, 꺼낸 뒤에는 더 쓰지 않는다.
         * @details 버퍼를 받은 쓰기기(문자열 · 고정 버퍼)에서 부르면 단언이고 nullopt 다.
         */
        [[nodiscard]] std::optional<JSONText> finish();
        /** @brief 이 쓰기기가 쓴 바이트 수입니다(고정 버퍼면 0 앞까지의 길이). */
        size_t getWrittenSize() const { return _writtenSize; }

    private:
        /** @brief 열린 객체 · 배열 하나입니다. */
        struct Scope
        {
            bool _bObject{ false };
            bool _bHasItem{ false };
        };

        void append( string_view text );
        void appendChar( utf8 character );
        void appendEscaped( string_view text );
        void appendNewLine( uint32 depth );
        /** @brief 배열 원소나 객체 키 앞의 쉼표와 줄바꿈입니다. */
        void writeItemSeparator();
        /** @brief 값 하나를 쓸 자리인지 확인하고 앞 구분자를 씁니다. 자리가 아니면 실패로 남기고 false 입니다. */
        [[nodiscard]] bool prepareValue();
        void               closeScope( bool bObject, utf8 closing );
        void               fail();

        string  _ownedText; ///< `JSONWriter( indent )` 가 쓰는 버퍼(`finish` 가 꺼낸다)
        string* _pString;
        utf8*   _pBuffer;
        uint32  _capacity;
        int32   _indent;
        size_t  _writtenSize;
        Scope   _arrScope[kMaxDepth];
        uint32  _depth;
        bool    _bAfterKey;
        bool    _bRootWritten;
        bool    _bFailed;
    };
} // namespace sw
