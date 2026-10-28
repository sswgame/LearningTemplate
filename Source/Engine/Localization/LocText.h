/**
 * @file LocText.h
 * @brief 코드 안에서 사용자에게 보이는 텍스트를 정의합니다. `SW_LOCTEXT( "Namespace", "Key", "Source" )` 형태이며 언리얼 `NSLOCTEXT` 와 같은 모양입니다.
 * @details 세 인자는 **문자열 리터럴**이어야 한다. 수집기(`App --gather-text`)가 소스를 읽어 원문 테이블에 `Namespace.Key` 줄을 만들기 때문이다.
 *          실행 중에는 지금 문화권의 텍스트를 돌려주고, 테이블에 없으면 빠진 키로 알린 뒤 원문을 돌려준다(화면이 비지 않는다).
 *          돌려준 포인터는 영구히 유효하다. 인자가 있는 메시지는 `SW_LOCFORMAT( ns, key, source, arguments )` 로 포맷한다.
 *
 *          원문을 코드에 두지 않으려면 `SW_LOCKEY( "Namespace", "Key" )` 나 `SW_LOCKEYFORMAT( ns, key, arguments )` 를 쓴다. 텍스트는 원문 테이블(`*.strings.xml`)에만 있고
 *          코드는 키만 가리킨다(언리얼 `LOCTABLE` 에 해당). 테이블에 없는 키는 수집이 오류로 알리고, 실행 중에는 키 자체가 보인다.
 *
 * @code
 *   label.setText( SW_LOCTEXT( "Menu", "Start", "Start Game" ) );
 *   tooltip = SW_LOCKEY( "EditorTip", "CurveKeyTime" ); // 원문은 editor.strings.xml
 *   const string line = SW_LOCFORMAT( "Battle", "Fainted", "{name} fainted!", TextArgumentList().addText( "name", foeName ) );
 * @endcode
 */
#pragma once
#include "Core/Container/string.h"
#include "Core/Foundation/Macros.h"
#include "Core/Foundation/Types.h"

namespace sw
{
    class TextArgumentList;

    /**
     * @struct LocalizedText
     * @brief `SW_LOCTEXT` 와 `SW_LOCFORMAT` 이 부르는 조회입니다. 엔진 서비스가 없으면(도구, 일부 테스트) 원문을 돌려줍니다.
     */
    struct SW_API LocalizedText
    {
        /** @brief `Namespace.Key` 의 지금 문화권 글입니다. 없으면 빠진 키로 알리고 @p pSourceText 입니다. */
        static const utf8* find( string_view fullKey, const utf8* pSourceText );
        /** @brief 위의 글을 지금 문화권으로 포맷합니다. */
        static string format( string_view fullKey, const utf8* pSourceText, const TextArgumentList& arguments );
    };
} // namespace sw

/** @brief 코드의 사용자에게 보이는 글입니다. 세 인자 모두 문자열 리터럴이어야 수집됩니다. */
#define SW_LOCTEXT( NamespaceLiteral, KeyLiteral, SourceLiteral ) ::sw::LocalizedText::find( NamespaceLiteral "." KeyLiteral, SourceLiteral )
/** @brief 코드는 키만 가리키는 텍스트입니다. 원문은 원문 테이블에만 있고, 테이블에 없으면 키 자체를 돌려줍니다(화면에서 빠진 텍스트가 보입니다). 두 인자 모두 문자열 리터럴이어야 수집됩니다. */
#define SW_LOCKEY( NamespaceLiteral, KeyLiteral ) ::sw::LocalizedText::find( NamespaceLiteral "." KeyLiteral, NamespaceLiteral "." KeyLiteral )
/** @brief `SW_LOCKEY` 의 포맷 판입니다(`TextFormatter` 문법의 글을 표에서 읽는다). */
#define SW_LOCKEYFORMAT( NamespaceLiteral, KeyLiteral, Arguments ) ::sw::LocalizedText::format( NamespaceLiteral "." KeyLiteral, NamespaceLiteral "." KeyLiteral, Arguments )
/** @brief 코드의 사용자에게 보이는 메시지를 인자로 포맷합니다(`TextFormatter` 문법). */
#define SW_LOCFORMAT( NamespaceLiteral, KeyLiteral, SourceLiteral, Arguments ) ::sw::LocalizedText::format( NamespaceLiteral "." KeyLiteral, SourceLiteral, Arguments )
