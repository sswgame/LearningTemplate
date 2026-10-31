// RichTextTest(입력 몫) — 리치 텍스트의 행동 태그 [action=이름] 이 입력 맵의 글리프 글로 바뀐다. 글 층(2)은 입력(3)을 모르므로 이 케이스는 입력 쪽에 둔다(nogpu).
#include "pch.h"

#include "Core/Testing/TestFramework.h"

#include "Engine/Input/InputManager.h"
#include "Engine/Input/Map/InputGlyphResolver.h"
#include "Engine/Input/Map/InputMap.h"
#include "Engine/Text/RichTextParser.h"

/**
 * @brief [RichTextTest] 행동 태그 [action=이름] 은 지금 장치의 글리프 글로 바뀐다 — 키보드면 "[ E ]", 패드면 패드 버튼. 다른 태그 · [[ 는 그대로
 * @details 변이: `expandActionTags` 가 태그를 그대로 두면 키보드 글이 "[action=Interact] open" 으로 남아 진다.
 */
SW_TEST_CASE( RichTextTest, ActionTagUsesCurrentInputGlyph )
{
    sw::InputMap inputMap;
    inputMap.bind( "Interact", sw::Key::E );
    inputMap.bind( "Interact", sw::GamepadButton::X );
    sw::InputGlyphStyle                   style    = sw::InputGlyphStyle::KeyboardMouse;
    const sw::RichTextActionGlyphResolver resolver = SW_DELEGATE_LAMBDA( sw::RichTextActionGlyphResolver, [&inputMap, &style]( sw::string_view action )
    {
        return sw::InputGlyphResolver::findActionGlyph( inputMap, sw::hashed_string( action ), style );
    } );
    sw::string                            text;
    SW_EXPECT_EQUAL( 1u, sw::RichTextParser::expandActionTags( "[action=Interact] [b]open[/b] [[x]", resolver, text ) );
    SW_EXPECT_EQUAL( sw::string( "[ E ] [b]open[/b] [[x]" ), text );

    style = sw::InputGlyphStyle::GamepadXbox;
    SW_EXPECT_EQUAL( 1u, sw::RichTextParser::expandActionTags( "[action=Interact] [b]open[/b] [[x]", resolver, text ) );
    SW_EXPECT_TRUE( text.find( "[ E ]" ) == sw::string::npos );
    SW_EXPECT_EQUAL( sw::InputGlyphResolver::findActionGlyph( inputMap, "Interact", style ) + " [b]open[/b] [[x]", text );

    SW_EXPECT_EQUAL( 0u, sw::RichTextParser::expandActionTags( "no tags [[action=Interact]", resolver, text ) );
    SW_EXPECT_EQUAL( sw::string( "no tags [[action=Interact]" ), text );
}
