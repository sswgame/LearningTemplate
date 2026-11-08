// PseudoLocalizer — 의사 문화권 변환: 악센트 · 길이 늘림 · 괄호, 메시지 구문과 리치 텍스트 표기는 그대로, 거울 방식의 RLO · PDF,
// 그리고 문화권 데이터의 글꼴 대체 목록(nogpu).
#include "pch.h"

#include "Core/String/MarkupToken.h"
#include "Core/String/StringUtil.h"
#include "Core/Testing/TestFramework.h"

#include "Engine/Content/AssetID.h"
#include "Engine/Content/Tests/ScopedScratchDomain.h"
#include "Engine/Localization/LocalizationManager.h"
#include "Engine/Localization/PseudoLocalizer.h"
#include "Engine/Localization/Tests/LocalizationTestUtil.h"
#include "Engine/Localization/TextFormatter.h"
#include "Engine/Text/RichTextParser.h"

namespace
{
    uint32 countCodepoints( sw::string_view text )
    {
        uint32 count{ 0 };
        for ( size_t offset = 0; offset < text.size(); )
        {
            (void)sw::StringUtil::decodeUTF8( text, offset );
            ++count;
        }
        return count;
    }
} // namespace

/**
 * @brief [PseudoLocalizerTest] 악센트 · 길이 늘림(30 % 이상) · 괄호 — ASCII 글자는 하나도 남지 않는다
 * @details 늘린 길이가 짧으면 독일어 · 러시아어 번역에서 잘릴 칸을 의사 문화권이 찾지 못한다(업계 관례가 30~40 %).
 */
SW_TEST_CASE( PseudoLocalizerTest, AccentsLengthensAndBrackets )
{
    for ( const sw::string_view source : { sw::string_view( "Settings" ), sw::string_view( "Start the game" ), sw::string_view( "Quit" ) } )
    {
        const sw::string pseudo = sw::PseudoLocalizer::transform( source, sw::PseudoLocaleMode::Accented );
        SW_EXPECT_TRUE( sw::PseudoLocalizer::isPseudoText( pseudo ) );
        SW_REQUIRE_TRUE( pseudo.size() >= 2 );
        SW_EXPECT_TRUE( pseudo.front() == '[' && pseudo.back() == ']' );
        const uint32 sourceLength = countCodepoints( source );
        const uint32 pseudoLength = countCodepoints( pseudo );
        SW_EXPECT_TRUE_MSG( pseudoLength * 10 >= sourceLength * 13, pseudo.c_str() );
        for ( size_t index = 1; index + 1 < pseudo.size(); ++index )
        {
            const utf8 character    = pseudo[index];
            const bool bASCIILetter = ( 'a' <= character && character <= 'z' ) || ( 'A' <= character && character <= 'Z' );
            SW_EXPECT_FALSE_MSG( bASCIILetter, pseudo.c_str() );
        }
    }
    SW_EXPECT_FALSE( sw::PseudoLocalizer::isPseudoText( "Settings" ) );
}

/**
 * @brief [PseudoLocalizerTest] 메시지 구문은 그대로다 — 인자 이름 · plural 키워드 · `#` 이 살아 있어 의사 문화권에서도 포맷이 풀린다
 */
SW_TEST_CASE( PseudoLocalizerTest, MessageSyntaxSurvives )
{
    const sw::string pseudo = sw::PseudoLocalizer::transform( "Hello {name}, {n, plural, one {# item} other {# items}} left", sw::PseudoLocaleMode::Accented );
    SW_EXPECT_TRUE( sw::TextFormatter::validatePattern( pseudo ) );

    sw::vector<sw::string> listName;
    sw::TextFormatter::collectArgumentNames( pseudo, listName );
    SW_REQUIRE_EQUAL( size_t( 2 ), listName.size() );
    SW_EXPECT_EQUAL( sw::string( "n" ), listName[0] );

    sw::string formatted;
    SW_EXPECT_TRUE( sw::TextFormatter::format( pseudo, sw::TextArgumentList().addText( "name", "Bob" ).addInteger( "n", 3 ), sw::CultureInfo{}, formatted ) );
    SW_EXPECT_TRUE( formatted.find( "Bob" ) != sw::string::npos );
    SW_EXPECT_TRUE( formatted.find( "3 " ) != sw::string::npos );
    SW_EXPECT_TRUE( formatted.find( "item" ) == sw::string::npos ); // 글자 조각은 바뀌었다
}

/**
 * @brief [PseudoLocalizerTest] 거울 방식은 오른쪽→왼쪽 표시 문자(RLO … PDF)로 감싼다
 */
SW_TEST_CASE( PseudoLocalizerTest, MirroredModeWrapsWithRightToLeftOverride )
{
    const sw::string pseudo = sw::PseudoLocalizer::transform( "Back", sw::PseudoLocaleMode::Mirrored );
    size_t           offset{ 0 };
    SW_EXPECT_EQUAL( sw::PseudoLocalizer::kRightToLeftOverride, sw::StringUtil::decodeUTF8( pseudo, offset ) );
    SW_EXPECT_TRUE( sw::PseudoLocalizer::isPseudoText( pseudo ) );
    sw::string ending;
    sw::StringUtil::appendUTF8( ending, sw::PseudoLocalizer::kPopDirectionalFormat );
    SW_EXPECT_TRUE( sw::StringUtil::endsWith( pseudo, ending ) );
}

/**
 * @brief [PseudoLocalizerTest] 글꼴 대체 목록은 문화권 데이터다 — 지역 코드는 언어로, 적히지 않은 문화권은 폴백 문화권의 목록을 받는다
 */
SW_TEST_CASE( PseudoLocalizerTest, FontFallbackComesFromCultureData )
{
    sw::LocalizationManager loc;
    SW_REQUIRE_TRUE( loc.loadCultureTable( sw::AssetID{ "engine/localization/engine.cultures.xml" } ) );
    const sw::vector<sw::string> listKorean = loc.getFontFallback( "ko-KR" );
    SW_REQUIRE_TRUE( listKorean.empty() == false );
    SW_EXPECT_EQUAL( sw::string( "Noto Sans KR" ), listKorean[0] );
    const sw::vector<sw::string> listJapanese = loc.getFontFallback( "ja" );
    const sw::vector<sw::string> listArabic   = loc.getFontFallback( "ar" );
    const sw::vector<sw::string> listUnknown  = loc.getFontFallback( "xx-unknown" );
    SW_REQUIRE_TRUE( listJapanese.empty() == false && listArabic.empty() == false && listUnknown.empty() == false );
    SW_EXPECT_EQUAL( sw::string( "Noto Sans JP" ), listJapanese[0] );
    SW_EXPECT_EQUAL( sw::string( "Noto Sans Arabic" ), listArabic[0] );
    SW_EXPECT_EQUAL( sw::string( "Noto Sans" ), listUnknown[0] ); // 폴백(en)의 목록
}

/**
 * @brief [PseudoLocalizerTest] 리치 텍스트 표기는 그대로다 — 태그 이름 · 값을 바꾸지 않고, 표기로 시작하는 글도 바깥 괄호와 붙어 `[[` 가 되지 않는다
 */
SW_TEST_CASE( PseudoLocalizerTest, PseudoLocalizerKeepsTags )
{
    const sw::string_view source = "[b]Start[/b] [color=accent]now[/color]";
    const sw::string      pseudo = sw::PseudoLocalizer::transform( source, sw::PseudoLocaleMode::Accented );
    SW_EXPECT_TRUE( sw::PseudoLocalizer::isPseudoText( pseudo ) );
    SW_EXPECT_TRUE_MSG( sw::MarkupToken::hasSameTags( source, pseudo ), pseudo.c_str() );
    SW_EXPECT_TRUE( pseudo.find( "Start" ) == sw::string::npos ); // 글은 바뀐다

    sw::RichTextParseResult parsed{};
    sw::RichTextParser::parse( pseudo, parsed );
    SW_EXPECT_EQUAL( 0u, parsed._problemCount );
    SW_EXPECT_EQUAL( 2u, static_cast<uint32>( parsed._listSpan.size() ) );
    SW_EXPECT_TRUE( parsed._plainText.empty() == false && parsed._plainText.front() == '[' );
}

/**
 * @brief [PseudoLocalizerTest] 언어 설정으로 의사 문화권을 고르면 올린 프로젝트의 모든 원문이 변환되어 나오고, 거울 방식은 오른쪽→왼쪽 문화권이다
 * @details 의사 문화권은 Dev 빌드의 "고를 수 있는 언어" 에 들어 있어야 한다 — 플레이어 설정(`localization.language`)의 선택지가 이 목록이다.
 *          배포 구성은 `Tests/` 를 짓지 않으므로 "배포본에는 없다" 쪽은 여기서 보지 않는다.
 */
SW_TEST_CASE( PseudoLocalizerTest, PseudoCultureIsSelectableAtRuntime )
{
    using sw::test::LocalizationTestUtil;
    const sw::test::ScopedScratchDomain domain;
    const sw::string                    folder = domain.getFolder();
    LocalizationTestUtil::writeSourceTable( folder, "en", R"("menu.start": { "source": "Start" }, "menu.items": { "source": "{n, plural, one {# item} other {# items}}" })" );

    sw::LocalizationManager loc;
    SW_REQUIRE_TRUE( LocalizationTestUtil::loadEngineCultures( loc ) );
    SW_REQUIRE_TRUE( loc.mountProject( sw::test::ScopedScratchDomain::toID( LocalizationTestUtil::writeProject( folder, "en", "[]" ) ), sw::LocalizationScope::Game ) );

    bool bListed{ false };
    for ( const sw::string& language : loc.getAvailableLanguages() )
    {
        bListed = bListed || language == "qps_ploc";
    }
    SW_EXPECT_TRUE( bListed );

    SW_REQUIRE_TRUE( loc.hasLanguage( "qps-ploc" ) );
    SW_REQUIRE_TRUE( loc.setCurrentLanguage( "qps-ploc" ) );
    const sw::string start = loc.getString( sw::hashed_string( "menu.start" ) );
    SW_EXPECT_TRUE( sw::PseudoLocalizer::isPseudoText( start ) );
    SW_EXPECT_FALSE( loc.isRightToLeft() );
    const sw::string items = loc.getFormattedString( sw::hashed_string( "menu.items" ), sw::TextArgumentList().addInteger( "n", 2 ), "menu.items" );
    SW_EXPECT_TRUE( items.find( "2 " ) != sw::string::npos );

    SW_REQUIRE_TRUE( loc.setCurrentLanguage( "qps-plocm" ) );
    SW_EXPECT_TRUE( loc.isRightToLeft() );
}
