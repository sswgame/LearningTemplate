/**
 * @file LocText.h
 * @brief 코드 안의 사용자에게 보이는 글 — `SW_LOCTEXT( "Namespace", "Key", "Source" )` — 입니다(언리얼 `NSLOCTEXT` 와 같은 모양).
 * @details 세 인자는 **문자열 리터럴**이어야 합니다. 수집기(`App --gather-text`)가 소스를 읽어 원문 표에 `Namespace.Key` 줄을 만들기 때문입니다.
 *          실행 중에는 지금 문화권의 글을 돌려주고, 표에 없으면 빠진 키로 알린 뒤 원문을 돌려줍니다(화면이 비지 않는다).
 *          돌려준 포인터는 영구히 유효합니다. 인자가 있는 메시지는 `SW_LOCFORMAT( ns, key, source, arguments )` 로 포맷합니다.
 *
 * @code
 *   label.setText( SW_LOCTEXT( "Menu", "Start", "Start Game" ) );
 *   const string line = SW_LOCFORMAT( "Battle", "Fainted", "{name} fainted!", TextArgumentList().addText( "name", foeName ) );
 * @endcode
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"
#include "Core/Container/string.h"

namespace sw
{
    class TextArgumentList;

    /**
     * @struct LocText
     * @brief `SW_LOCTEXT` · `SW_LOCFORMAT` 이 부르는 조회입니다. 엔진 서비스가 없으면(도구 · 일부 시험) 원문입니다.
     */
    struct SW_API LocText
    {
        /** @brief `Namespace.Key` 의 지금 문화권 글입니다. 없으면 빠진 키로 알리고 @p pSourceText 입니다. */
        static const utf8* find( string_view fullKey, const utf8* pSourceText );
        /** @brief 위의 글을 지금 문화권으로 포맷합니다. */
        static string format( string_view fullKey, const utf8* pSourceText, const TextArgumentList& arguments );
    };
} // namespace sw

/** @brief 코드의 사용자에게 보이는 글입니다. 세 인자 모두 문자열 리터럴이어야 수집됩니다. */
#define SW_LOCTEXT( NamespaceLiteral, KeyLiteral, SourceLiteral ) ::sw::LocText::find( NamespaceLiteral "." KeyLiteral, SourceLiteral )
/** @brief 코드의 사용자에게 보이는 메시지를 인자로 포맷합니다(`TextFormatter` 문법). */
#define SW_LOCFORMAT( NamespaceLiteral, KeyLiteral, SourceLiteral, Arguments ) ::sw::LocText::format( NamespaceLiteral "." KeyLiteral, SourceLiteral, Arguments )
