/**
 * @file ChatTextNormalizer.h
 * @brief 채팅 글의 정규화입니다. 금칙어 거르개(`ChatWordFilter`)와 도배 방지기(`ChatSpamGuard`)가 같은 것을 쓴다.
 * @details 전각 ASCII(U+FF01~FF5E)를 반각으로, ASCII 대문자를 소문자로 바꾸는 것은 유니코드 규칙이라 코드에 있다. 건너뛸 글자(`b.a.d` 의 `.` · 0 폭 글자)는
 *          운영 정책이라 데이터다(`Config/Server/chat_ignored_characters.txt`, 한 줄에 `U+XXXX` 하나, `#` 은 주석).
 */
#pragma once
#include "Core/Container/string.h"
#include "Core/Container/vector.h"
#include "Core/Foundation/Types.h"

#include "GameFramework/GameFrameworkExports.h"

namespace sw
{
    /**
     * @class ChatTextNormalizer
     * @brief 코드 포인트 하나의 정규화와 정규화한 글의 해시입니다. 만든 뒤에는 읽기만 한다.
     */
    class SW_KIT_SERVER_API ChatTextNormalizer
    {
    public:
        /** @brief 건너뛸 글자 파일을 읽습니다. 파일이 없거나 줄이 `U+XXXX` 가 아니면 오류를 남기고 false 이며 목록은 비어 있습니다. */
        [[nodiscard]] bool loadFromFile( string_view path );
        /** @brief 건너뛸 글자를 바로 정합니다(테스트 · 도구). */
        void setIgnored( vector<uint32> listCodepoint );

        /** @brief 정규화한 글자 하나입니다. 건너뛸 글자이면 0 입니다. */
        uint32 normalize( uint32 codepoint ) const;
        /** @brief 정규화한 글의 64비트 해시(코드 포인트 단위 FNV-1a)입니다. 글자가 건너뛰어지거나 바뀌어도(대소문자 · 전각) 같은 값이 된다. */
        uint64 computeTextHash( string_view text ) const;

    private:
        vector<uint32> _listIgnored; ///< 정렬해 둔다(이진 탐색)
    };
} // namespace sw
