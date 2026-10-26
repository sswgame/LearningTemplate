#include "pch.h"

#include "Editor/Common/Widgets/Model/EditorDisplayNameUtil.h"

#include "TestFramework/TestFramework.h"

using sw::editor::EditorDisplayNameUtil;

/** @brief [EditorDisplayNameUtilTest] 앞 밑줄 · 멤버 접두어 · 불리언 · 포인터 접두어를 떼고 단어마다 첫 글자를 대문자로 쓴다 */
SW_TEST_CASE( EditorDisplayNameUtilTest, StripsPrefixesAndCapitalizesWords )
{
    SW_EXPECT_EQUAL( sw::string( "Color" ), EditorDisplayNameUtil::makeDisplayName( "_color" ) );
    SW_EXPECT_EQUAL( sw::string( "Is Visible" ), EditorDisplayNameUtil::makeDisplayName( "bIsVisible" ) );
    SW_EXPECT_EQUAL( sw::string( "Open" ), EditorDisplayNameUtil::makeDisplayName( "_bOpen" ) );
    SW_EXPECT_EQUAL( sw::string( "Speed" ), EditorDisplayNameUtil::makeDisplayName( "m_speed" ) );
    SW_EXPECT_EQUAL( sw::string( "Count" ), EditorDisplayNameUtil::makeDisplayName( "_s_count" ) );
    SW_EXPECT_EQUAL( sw::string( "Owner" ), EditorDisplayNameUtil::makeDisplayName( "_pOwner" ) );
    // 바로 뒤가 소문자면 접두어가 아니라 단어다
    SW_EXPECT_EQUAL( sw::string( "Bounds Radius" ), EditorDisplayNameUtil::makeDisplayName( "_boundsRadius" ) );
    SW_EXPECT_EQUAL( sw::string( "Point Count" ), EditorDisplayNameUtil::makeDisplayName( "_pointCount" ) );
}

/** @brief [EditorDisplayNameUtilTest] camelCase · snake_case · 약어 · 숫자에서 단어를 나눈다 */
SW_TEST_CASE( EditorDisplayNameUtilTest, SplitsWordsAtCaseDigitAndUnderscore )
{
    SW_EXPECT_EQUAL( sw::string( "Max HP" ), EditorDisplayNameUtil::makeDisplayName( "maxHP" ) );
    SW_EXPECT_EQUAL( sw::string( "HP Max" ), EditorDisplayNameUtil::makeDisplayName( "HPMax" ) );
    SW_EXPECT_EQUAL( sw::string( "Max Speed" ), EditorDisplayNameUtil::makeDisplayName( "max_speed" ) );
    SW_EXPECT_EQUAL( sw::string( "Texture 2D" ), EditorDisplayNameUtil::makeDisplayName( "_texture2D" ) );
    SW_EXPECT_EQUAL( sw::string( "Texture 2D Array" ), EditorDisplayNameUtil::makeDisplayName( "texture2DArray" ) );
    SW_EXPECT_EQUAL( sw::string( "Lod 0 Distance" ), EditorDisplayNameUtil::makeDisplayName( "_lod0Distance" ) );
    SW_EXPECT_EQUAL( sw::string( "List Item" ), EditorDisplayNameUtil::makeDisplayName( "_listItem" ) );
}

/** @brief [EditorDisplayNameUtilTest] 이미 다듬은 이름은 그대로, 접두어만인 이름은 원래 이름이다 */
SW_TEST_CASE( EditorDisplayNameUtilTest, KeepsReadableNamesAndDegenerateInput )
{
    SW_EXPECT_EQUAL( sw::string( "Max Speed" ), EditorDisplayNameUtil::makeDisplayName( "Max Speed" ) );
    SW_EXPECT_EQUAL( sw::string( "Max Speed" ), EditorDisplayNameUtil::makeDisplayName( EditorDisplayNameUtil::makeDisplayName( "_maxSpeed" ) ) );
    SW_EXPECT_EQUAL( sw::string( "HP" ), EditorDisplayNameUtil::makeDisplayName( "HP" ) );
    SW_EXPECT_EQUAL( sw::string( "_" ), EditorDisplayNameUtil::makeDisplayName( "_" ) );
    SW_EXPECT_EQUAL( sw::string( "" ), EditorDisplayNameUtil::makeDisplayName( "" ) );
}

/** @brief [EditorDisplayNameUtilTest] 적힌 표시 이름이 늘 이긴다 — 번역할 이름은 그쪽에서 나온다 */
SW_TEST_CASE( EditorDisplayNameUtilTest, ExplicitDisplayNameWins )
{
    SW_EXPECT_EQUAL( sw::string( "Tint" ), EditorDisplayNameUtil::chooseLabel( "Tint", "_color" ) );
    SW_EXPECT_EQUAL( sw::string( "Color" ), EditorDisplayNameUtil::chooseLabel( "", "_color" ) );
}
