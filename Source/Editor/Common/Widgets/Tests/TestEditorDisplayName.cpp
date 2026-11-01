// EditorDisplayNameUtil — 멤버 이름을 인스펙터 · 환경설정 표시 이름으로: 접두어를 떼고 낱말을 나누고, 약어는 대문자로 둔다.
#include "pch.h"

#include "Core/Testing/TestFramework.h"

#include "Editor/Common/Widgets/Model/EditorDisplayNameUtil.h"

using namespace sw;
using namespace sw::editor;

SW_TEST_CASE( EditorDisplayNameTest, SplitsWordsAndDropsMemberPrefixes )
{
    SW_EXPECT_EQUAL( string( "Startup Scene" ), EditorDisplayNameUtil::makeDisplayName( "_startupScene" ) );
    SW_EXPECT_EQUAL( string( "Visible" ), EditorDisplayNameUtil::makeDisplayName( "_bVisible" ) );
    SW_EXPECT_EQUAL( string( "Max Count 2" ), EditorDisplayNameUtil::makeDisplayName( "_maxCount2" ) );
}

SW_TEST_CASE( EditorDisplayNameTest, KeepsAcronymsUpperCase )
{
    SW_EXPECT_EQUAL( string( "UI Scale" ), EditorDisplayNameUtil::makeDisplayName( "_uiScale" ) );
    SW_EXPECT_EQUAL( string( "IDE Open Command" ), EditorDisplayNameUtil::makeDisplayName( "_ideOpenCommand" ) );
    SW_EXPECT_EQUAL( string( "Object ID" ), EditorDisplayNameUtil::makeDisplayName( "_objectId" ) );
    SW_EXPECT_EQUAL( string( "Show HUD" ), EditorDisplayNameUtil::makeDisplayName( "_bShowHUD" ) );
    SW_EXPECT_EQUAL( string( "Idle Time" ), EditorDisplayNameUtil::makeDisplayName( "_idleTime" ) ); // 약어는 낱말 전체가 같을 때만
}
