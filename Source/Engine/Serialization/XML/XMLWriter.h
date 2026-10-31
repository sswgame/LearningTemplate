/**
 * @file XMLWriter.h
 * @brief XML 을 앞에서부터 차례로 쓰는 쓰기기입니다. 트리를 만들지 않고 문자열 버퍼에 바로 씁니다(`JSONWriter` 와 같은 자리).
 * @details 속성 값과 글은 늘 이스케이프되고 `<` · `="` · `/>` · 닫는 태그는 쓰기기가 놓는다 — 부르는 쪽은 XML 글을 이어 붙이지 않는다.
 *          읽은 문서를 고쳐 다시 쓸 때는 `XMLDocument`, 사람이 읽는 줄 배치를 지켜야 하는 출력(녹화한 시나리오 · 만든 프리팹)은 이것을 쓴다.
 */
#pragma once
#include "Core/Container/string.h"
#include "Core/Foundation/Macros.h"
#include "Core/Foundation/Types.h"

namespace sw
{
    /**
     * @class XMLWriter
     * @brief `beginElement` · `attribute` · `text` · `endElement` 로 XML 을 스트림처럼 씁니다.
     * @details
     * - 속성 값: `&` · `<` · `>` · `"` 와 탭 · 줄바꿈(`&#9;` `&#10;` `&#13;`, 파서가 공백으로 바꾸지 않게)을 이스케이프한다.
     * - 글: `&` · `<` · `>` 를 이스케이프한다. 둘 다 XML 1.0 이 받지 않는 제어 문자와 잘못된 UTF-8 바이트는 U+FFFD 로 바꾼다.
     * - 원소 · 속성 이름은 XML 이름 글자만 받는다. 아니면 단언이고 실패로 남긴다(이름에 `"` 를 넣어 속성을 끼워 넣지 못한다).
     * - 순서 위반(원소 밖의 속성, 글 뒤의 속성, 짝 없는 `endElement`)도 단언이고 실패로 남긴다. 루트가 여럿인 조각을 써도 된다.
     * - 내용이 없는 원소는 `<Name/>` 로 닫는다. 줄 배치는 `whitespace` 로만 넣는다(공백 · 탭 · 줄바꿈만 받는다).
     * - 닫는 태그 이름은 버퍼 안의 위치로 기억한다 — 쓰는 동안 부르는 쪽이 버퍼를 직접 고치지 않는다.
     */
    class SW_API XMLWriter
    {
    public:
        /** @brief 중첩 깊이 한도입니다. 고정 배열이라 할당하지 않는다. */
        static constexpr uint32 kMaxDepth = 32;

        /** @brief 쓰기기가 가진 버퍼에 씁니다. 다 쓴 조각을 다른 쓰기기에 `fragment` 로 옮길 때 쓴다. */
        XMLWriter();
        /** @brief @p outXML 끝에 덧붙여 씁니다. */
        explicit XMLWriter( string& outXML );

        XMLWriter( const XMLWriter& )            = delete;
        XMLWriter& operator=( const XMLWriter& ) = delete;

        /** @brief `<?xml version="1.0" encoding="utf-8"?>` 와 줄바꿈입니다. 이 쓰기기의 첫 출력이어야 한다. */
        void declaration();
        /** @brief `<!-- text -->` 입니다. 글에 `--` 가 있거나 `-` 로 끝나면 단언이고 실패로 남긴다. */
        void comment( string_view text );
        /** @brief 줄 배치(공백 · 탭 · 줄바꿈)입니다. 그 밖의 글자가 있으면 단언이고 실패로 남긴다. */
        void whitespace( string_view layout );

        void beginElement( string_view name );
        /** @brief 지금 연 원소의 속성 하나입니다. `beginElement` 바로 뒤(글 · 자식 전)에만 쓴다. */
        void attribute( string_view name, string_view value );
        void attributeInt( string_view name, int64 value );
        /** @brief 지금 원소의 글 내용입니다. */
        void text( string_view value );
        void endElement();
        /**
         * @brief 다른 쓰기기가 다 쓴 조각(@p piece 가 이 쓰기기를 만든 뒤 쓴 글)을 지금 자리에 옮깁니다. 조각이 완성되지 않았으면 단언이고 실패로 남긴다.
         * @details 나중에 채우는 자리(녹화의 스틱 구간은 앞 프레임에 늦게 닫힌다)를 따로 쓴 뒤 합칠 때 쓴다. 글을 받지 않고 쓰기기를 받아 이어 붙인 글이 들어올 자리가 없다.
         */
        void fragment( const XMLWriter& piece );

        /** @brief 연 원소를 모두 닫았고 실패(이름 · 순서 · 주석 · 배치)가 없으면 true 입니다. */
        [[nodiscard]] bool isComplete() const;
        /** @brief 이 쓰기기가 아무것도 쓰지 않았으면 true 입니다. */
        bool isEmpty() const { return _pString->size() == _startSize; }

        /**
         * @brief @p text 를 @p bAttribute 면 속성 값(큰따옴표 안) 규칙, 아니면 글 규칙으로 이스케이프해 @p inoutText 뒤에 붙입니다.
         * @details XML 이스케이프 규칙은 이것 하나다 — 쓰기기 밖에서 글을 고치는 쪽(에디터 카탈로그 글 편집)도 이것을 쓴다(같은 값이 저장 경로마다 다른 바이트가 되지 않게).
         */
        static void appendEscaped( string& inoutText, string_view text, bool bAttribute );

    private:
        /** @brief 연 원소 하나의 이름 위치(버퍼 안)입니다. */
        struct OpenElement
        {
            size_t _nameOffset{ 0 };
            uint32 _nameSize{ 0 };
        };

        void append( string_view text );
        /** @brief 연 시작 태그를 `>` 로 닫습니다(자식 · 글 앞). */
        void closeStartTag();
        void fail();

        string      _ownedText; ///< `XMLWriter()` 가 쓰는 버퍼
        string*     _pString;
        OpenElement _arrOpen[kMaxDepth];
        size_t      _startSize;
        uint32      _depth;
        bool        _bStartTagOpen;
        bool        _bFailed;
    };
} // namespace sw
