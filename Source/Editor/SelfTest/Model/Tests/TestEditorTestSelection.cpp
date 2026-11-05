#include "pch.h"

#include "Core/Testing/TestFramework.h"

#include "Editor/SelfTest/Model/EditorTestSelection.h"

using namespace sw;
using namespace sw::editor;

namespace
{
    struct EditorTestSelectionTestUtil
    {
        /** @brief 묶음에 든 케이스 이름을 쉼표로 잇습니다. */
        static string joinGroup( const EditorTestSelection& selection, const EditorTestGroup& group )
        {
            string text;
            for ( const uint32 index : group._listCaseIndex )
            {
                text += ( text.empty() ? "" : "," ) + selection.getCases()[index];
            }
            return text;
        }
    };
    using Util = EditorTestSelectionTestUtil;
} // namespace

// ---------------------------------------------------------------------- 묶음

SW_TEST_CASE( EditorTestSelectionTest, GroupsBySuiteInListOrder )
{
    EditorTestSelection selection;
    selection.setCases( { "theme.palette", "theme.fonts", "asset.import" } );
    SW_REQUIRE_EQUAL( 2u, static_cast<uint32>( selection.getGroups().size() ) );
    SW_EXPECT_TRUE( selection.getGroups()[0]._name == "theme" );
    SW_EXPECT_TRUE( Util::joinGroup( selection, selection.getGroups()[0] ) == "theme.palette,theme.fonts" );
    SW_EXPECT_TRUE( selection.getGroups()[1]._name == "asset" );
}

// 결함: 목록에서 떨어진 같은 스위트가 같은 ImGui id 의 나무 줄을 둘 만들었다(자체 테스트는 `_order` 순이라 스위트가 붙어 있다는 보장이 없다).
SW_TEST_CASE( EditorTestSelectionTest, SplitSuiteIsOneGroup )
{
    EditorTestSelection selection;
    selection.setCases( { "a.one", "b.two", "a.three" } );
    SW_REQUIRE_EQUAL( 2u, static_cast<uint32>( selection.getGroups().size() ) );
    SW_EXPECT_TRUE( Util::joinGroup( selection, selection.getGroups()[0] ) == "a.one,a.three" );
    SW_EXPECT_TRUE( Util::joinGroup( selection, selection.getGroups()[1] ) == "b.two" );
}

SW_TEST_CASE( EditorTestSelectionTest, NameWithoutDotIsItsOwnGroup )
{
    EditorTestSelection selection;
    selection.setCases( { "solo" } );
    SW_REQUIRE_EQUAL( 1u, static_cast<uint32>( selection.getGroups().size() ) );
    SW_EXPECT_TRUE( selection.getGroups()[0]._name == "solo" );
}

SW_TEST_CASE( EditorTestSelectionTest, LeafNameDropsTheSuiteOnly )
{
    SW_EXPECT_TRUE( EditorTestSelection::findLeafName( "theme.palette" ) == "palette" );
    SW_EXPECT_TRUE( EditorTestSelection::findLeafName( "a.b.c" ) == "b.c" );
    SW_EXPECT_TRUE( EditorTestSelection::findLeafName( "solo" ) == "solo" );
    SW_EXPECT_TRUE( EditorTestSelection::findLeafName( "dangling." ) == "dangling." );
}

// ---------------------------------------------------------------------- 검색

SW_TEST_CASE( EditorTestSelectionTest, FilterNarrowsGroupsAndVisiblePattern )
{
    EditorTestSelection selection;
    selection.setCases( { "theme.palette", "theme.fonts", "asset.import" } );
    SW_EXPECT_TRUE( selection.getVisiblePattern() == "theme.palette,theme.fonts,asset.import" );

    selection.setFilter( "FONT" ); // 대소문자를 가리지 않는다
    SW_REQUIRE_EQUAL( 1u, static_cast<uint32>( selection.getGroups().size() ) );
    SW_EXPECT_TRUE( selection.getVisiblePattern() == "theme.fonts" );

    selection.setFilter( "nothing matches" );
    SW_EXPECT_TRUE( selection.getGroups().empty() );
    SW_EXPECT_TRUE( selection.getVisiblePattern().empty() );

    selection.setFilter( "" );
    SW_EXPECT_EQUAL( 2u, static_cast<uint32>( selection.getGroups().size() ) );
}

// 결함: 패널이 검색 글을 그대로 견줘 앞뒤 공백 하나로 목록이 모두 사라졌다 — 다른 패널의 `EditorListFilter` 규칙과 갈라져 있었다.
SW_TEST_CASE( EditorTestSelectionTest, FilterIgnoresSurroundingSpaces )
{
    EditorTestSelection selection;
    selection.setCases( { "theme.fonts", "asset.import" } );
    selection.setFilter( " theme " );
    SW_EXPECT_TRUE( selection.getVisiblePattern() == "theme.fonts" );
    selection.setFilter( "   " );
    SW_EXPECT_TRUE( selection.getVisiblePattern() == "theme.fonts,asset.import" );
}

// ---------------------------------------------------------------------- 체크 · 패턴

SW_TEST_CASE( EditorTestSelectionTest, SelectedPatternFollowsChecks )
{
    EditorTestSelection selection;
    selection.setCases( { "a.one", "a.two", "b.three" } );
    SW_EXPECT_TRUE( selection.getSelectedPattern().empty() );

    selection.setSelected( "b.three", true );
    selection.setSelected( "a.one", true );
    SW_EXPECT_TRUE( selection.isSelected( "a.one" ) );
    SW_EXPECT_FALSE( selection.isSelected( "a.two" ) );
    SW_EXPECT_TRUE( selection.getSelectedPattern() == "a.one,b.three" ); // 체크한 순서가 아니라 목록 순서다

    selection.setSelected( "a.one", false );
    SW_EXPECT_FALSE( selection.isSelected( "a.one" ) );
    SW_EXPECT_TRUE( selection.getSelectedPattern() == "b.three" );
}

SW_TEST_CASE( EditorTestSelectionTest, FilteredOutChecksDoNotRunButStayChecked )
{
    EditorTestSelection selection;
    selection.setCases( { "a.one", "b.two" } );
    selection.setSelected( "a.one", true );
    selection.setSelected( "b.two", true );

    selection.setFilter( "a." );
    SW_EXPECT_TRUE( selection.getSelectedPattern() == "a.one" );
    SW_EXPECT_TRUE( selection.isSelected( "b.two" ) );

    selection.setFilter( "" );
    SW_EXPECT_TRUE( selection.getSelectedPattern() == "a.one,b.two" );
}

// 결함: 단추는 체크 집합이 비었는지만 봐서, 목록에서 없어진 이름만 체크되어 있으면 켜진 채 빈 거르개를 만들었다 — 빈 `--test-filter` 는 모든 케이스를 돌린다.
SW_TEST_CASE( EditorTestSelectionTest, ChecksOfRemovedCasesLeaveNothingToRun )
{
    EditorTestSelection selection;
    selection.setCases( { "a.one", "a.two" } );
    selection.setSelected( "a.one", true );

    selection.setCases( { "a.two" } );
    SW_EXPECT_TRUE( selection.getSelectedPattern().empty() );

    selection.setCases( { "a.one", "a.two" } ); // 모듈이 돌아오면 체크도 돌아온다
    SW_EXPECT_TRUE( selection.getSelectedPattern() == "a.one" );
}

// ---------------------------------------------------------------------- 시나리오

SW_TEST_CASE( EditorScenarioListTest, KeepsOnlyScenariosUnderAutomationSorted )
{
    EditorScenarioList list;
    list.setFromFiles( { "C:/Content/game/a/automation/z.scenario.xml", "C:/Content/engine/automation/editor/b.scenario.xml", "C:/Content/engine/automation/plain.xml",
                         "C:/Content/engine/other/c.scenario.xml", "C:/Content/automation/rootless.scenario.xml" },
                       "C:/Content" );
    SW_REQUIRE_EQUAL( 2u, static_cast<uint32>( list.getPaths().size() ) );
    SW_EXPECT_TRUE( list.getPaths()[0] == "engine/automation/editor/b.scenario.xml" );
    SW_EXPECT_TRUE( list.getPaths()[1] == "game/a/automation/z.scenario.xml" );
}

// 결함: 뿌리를 글자 그대로 앞에서 잘라, 구분자나 끝 빗금이 파일 경로와 다르면 절대 경로가 리소스 id 로 나갔다.
SW_TEST_CASE( EditorScenarioListTest, RootSpellingDoesNotChangeTheID )
{
    for ( const string_view root : { string_view( "C:\\Content" ), string_view( "C:/Content/" ), string_view( "C:\\Content\\" ) } )
    {
        EditorScenarioList list;
        list.setFromFiles( { "C:/Content/engine/automation/a.scenario.xml" }, root );
        SW_REQUIRE_EQUAL( 1u, static_cast<uint32>( list.getPaths().size() ) );
        SW_EXPECT_TRUE( list.getPaths()[0] == "engine/automation/a.scenario.xml" );
    }
}

// 결함: 번호로 들어 Refresh 뒤 번호가 다른 시나리오를 가리켰다 — 고르지 않은 시나리오가 돈다.
SW_TEST_CASE( EditorScenarioListTest, ChoiceFollowsThePathAcrossRefresh )
{
    EditorScenarioList list;
    list.setFromFiles( { "C:/C/x/automation/b.scenario.xml" }, "C:/C" );
    list.choose( "x/automation/b.scenario.xml" );

    list.setFromFiles( { "C:/C/x/automation/a.scenario.xml", "C:/C/x/automation/b.scenario.xml" }, "C:/C" );
    SW_EXPECT_TRUE( list.getChosen() == "x/automation/b.scenario.xml" );

    list.setFromFiles( { "C:/C/x/automation/a.scenario.xml" }, "C:/C" );
    SW_EXPECT_TRUE( list.getChosen().empty() );
}
