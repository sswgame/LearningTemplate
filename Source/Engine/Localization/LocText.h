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
#include "Core/Container/vector.h"
#include "Core/Foundation/Macros.h"
#include "Core/Foundation/Types.h"

namespace sw
{
    class LocalizationManager;
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
        /**
         * @brief 화면에 넣은 글 @p text 를 @p pLocalization 의 문화권으로 풉니다 — 현지화 메시지(`LocalizedMessage`)면 인자까지 넣어 포맷하고, 키면 그 글, 둘 다 아니면 글 그대로입니다.
         * @details 그리는 쪽(글 위젯 · 자막 · 스크린 리더)이 그릴 때 부른다. 푼 글을 값으로 들면 언어를 바꿔도 그대로다. @p pLocalization 이 nullptr 이면 키 그대로 포맷한다.
         */
        static string resolve( const LocalizationManager* pLocalization, string_view text );
        /** @brief 지금 문화권(엔진 서비스)으로 풉니다. 서비스가 없으면 키 그대로입니다. 진단 · 시나리오 감시처럼 글을 그 자리에서 쓰고 버리는 곳이 씁니다. */
        static string resolveNow( string_view text );
    };

    /**
     * @class LocalizedMessage
     * @brief 인자가 있는 현지화 글 하나를 **키와 인자로** 담은 글을 만듭니다. 화면 글 · 뷰모델 칸 · 툴팁 · 알림 · 세이브가 들던 `string` 에 그대로 넣고, 그릴 때 `LocalizedText::resolve` 가 지금 문화권으로 풉니다.
     * @details 포맷한 글을 값으로 들면 언어를 바꿔도 그대로이고 저장하면 그 언어로 굳는다(언리얼 FText 가 키 · 인자 이력을 들고 `FTextStringHelper` 로 문자열에 적는 것과 같다).
     *          적은 글은 제어 문자로 나뉜다 — 글 인자에서 그 문자는 지운다(플레이어가 친 글이 다른 메시지가 되지 않게).
     *
     *          text.setText( SW_LOCALIZED_MESSAGE( "Shop", "ConfirmMessage" ).addKey( "item", offer._displayName ).addInteger( "price", total ).str() );
     */
    class SW_API LocalizedMessage
    {
    public:
        /** @brief `Namespace.Key` 의 글을 인자로 포맷하는 메시지입니다. */
        explicit LocalizedMessage( string_view fullKey );

        /** @brief 글 그대로인 인자입니다(플레이어 이름 · 숫자 글). 키로 풀지 않습니다. */
        LocalizedMessage& addText( string_view name, string_view value );
        /** @brief 현지화 키 · 메시지 · 원문인 인자입니다(아이템 이름). 그릴 때 함께 풉니다. */
        LocalizedMessage& addKey( string_view name, string_view keyOrText );
        LocalizedMessage& addInteger( string_view name, int64 value );
        /** @brief 소수 자리를 고정한 소수 인자입니다(`TextArgumentList::addDecimal`). */
        LocalizedMessage& addDecimal( string_view name, float64 value, uint32 fractionDigits );
        /** @brief 화면 글 칸에 넣을 글입니다. */
        string str() const;

        /** @brief 키 · 메시지 · 원문인 글 여럿을 @p separator 로 잇는 글입니다(툴팁의 여러 줄). 빈 것은 건너뜁니다. 하나뿐이면 그 글 그대로입니다. */
        static string join( string_view separator, const vector<string>& listText );
        /** @brief 키로 풀지 않는 글입니다(잇는 글 안의 플레이어 글). */
        static string literal( string_view text );
        /** @brief @p text 가 이 클래스가 적은 글이면 true 입니다. */
        static bool isMessage( string_view text );

    private:
        LocalizedMessage& addValue( string_view name, utf8 kind, string_view value );

    private:
        string _body; ///< 닫는 문자를 뺀 글
    };
} // namespace sw

/** @brief 코드의 사용자에게 보이는 글입니다. 세 인자 모두 문자열 리터럴이어야 수집됩니다. */
#define SW_LOCTEXT( NamespaceLiteral, KeyLiteral, SourceLiteral ) ::sw::LocalizedText::find( NamespaceLiteral "." KeyLiteral, SourceLiteral )
/** @brief 코드는 키만 가리키는 텍스트입니다. 원문은 원문 테이블에만 있고, 테이블에 없으면 키 자체를 돌려줍니다(화면에서 빠진 텍스트가 보입니다). 두 인자 모두 문자열 리터럴이어야 수집됩니다. */
#define SW_LOCKEY( NamespaceLiteral, KeyLiteral ) ::sw::LocalizedText::find( NamespaceLiteral "." KeyLiteral, NamespaceLiteral "." KeyLiteral )
/**
 * @brief 키를 풀지 않고 `"Namespace.Key"` 리터럴로 남깁니다. 정적 등록 표(시각화 · 도구 줄)처럼 현지화가 서기 전에 만들어지는 곳이 키를 들고, 그리는 곳이
 *        `LocalizedText::find( 키, 키 )` 로 지금 언어로 풉니다(언리얼 `LOCTABLE` 참조). 수집기는 `SW_LOCKEY` 처럼 "테이블에 있어야 하는 참조" 로 모은다.
 */
#define SW_LOCKEYREF( NamespaceLiteral, KeyLiteral ) NamespaceLiteral "." KeyLiteral
/** @brief `SW_LOCKEY` 의 포맷 판입니다(`TextFormatter` 문법의 글을 표에서 읽는다). */
#define SW_LOCKEYFORMAT( NamespaceLiteral, KeyLiteral, Arguments ) ::sw::LocalizedText::format( NamespaceLiteral "." KeyLiteral, NamespaceLiteral "." KeyLiteral, Arguments )
/** @brief 키를 풀지 않은 현지화 메시지(`LocalizedMessage`)입니다 — 인자를 잇고 `str()` 을 화면 글에 넣는다. 그릴 때 풀린다. 두 인자 모두 문자열 리터럴이어야 수집됩니다. */
#define SW_LOCALIZED_MESSAGE( NamespaceLiteral, KeyLiteral ) ::sw::LocalizedMessage( NamespaceLiteral "." KeyLiteral )
/** @brief 코드의 사용자에게 보이는 메시지를 인자로 포맷합니다(`TextFormatter` 문법). */
#define SW_LOCFORMAT( NamespaceLiteral, KeyLiteral, SourceLiteral, Arguments ) ::sw::LocalizedText::format( NamespaceLiteral "." KeyLiteral, SourceLiteral, Arguments )
