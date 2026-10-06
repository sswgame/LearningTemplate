#include "pch.h"

#include "Editor/Panels/ContentBrowserLogic.h"

#include "TestFramework/TestFramework.h"

// ContentBrowserLogicTest — 콘텐츠 브라우저의 판단(패널 점검 D12 · D15): 경로 줄 조각이 저마다 절대 경로를 든다. ImGui 없음.

/**
 * @brief [ContentBrowserLogicTest] 즐겨찾기에서 들어간 폴더의 경로 줄은 조각마다 절대 경로를 든다 — "Favorites / Shaders" 의 Shaders 를 누르면 셰이더 폴더로 간다
 * @details 경로 줄을 글 하나로 들고 누를 때 다시 쪼개면, 루트가 아닌 첫 조각("Favorites")에서 절대 경로가 끊겨 둘째 조각이 상대 경로 "Shaders" 가 되어
 *          빈 폴더("0 items")로 갔다.
 */
SW_TEST_CASE( ContentBrowserLogicTest, BreadcrumbKeepsTheAbsolutePathOfEverySegment )
{
    using sw::editor::ContentBrowserCrumb;
    using sw::editor::ContentBrowserLogic;
    sw::vector<ContentBrowserCrumb> listCrumb;
    ContentBrowserLogic::makeFavoriteTrail( "Shaders", "D:/Res/engine/shaders", listCrumb );
    ContentBrowserLogic::appendChildCrumb( "D:/Res/engine/shaders/bin", listCrumb );

    SW_ASSERT_EQUAL( 3u, static_cast<uint32>( listCrumb.size() ) );
    SW_EXPECT_STREQ( "Favorites", listCrumb[0]._label.c_str() );
    SW_EXPECT_TRUE( listCrumb[0]._absolutePath.empty() ); // 누를 수 없는 묶음 이름
    SW_EXPECT_STREQ( "Shaders", listCrumb[1]._label.c_str() );
    SW_EXPECT_STREQ( "D:/Res/engine/shaders", listCrumb[1]._absolutePath.c_str() );
    SW_EXPECT_STREQ( "bin", listCrumb[2]._label.c_str() );
    SW_EXPECT_STREQ( "D:/Res/engine/shaders/bin", listCrumb[2]._absolutePath.c_str() );
}

/**
 * @brief [ContentBrowserLogicTest] 루트 아래 폴더의 경로 줄은 루트 조각 + 폴더마다 한 조각이고, 조각의 경로는 원래 경로의 그 자리까지다(대소문자 그대로)
 */
SW_TEST_CASE( ContentBrowserLogicTest, FolderTrailHasOneSegmentPerFolderBelowTheRoot )
{
    using sw::editor::ContentBrowserCrumb;
    using sw::editor::ContentBrowserLogic;
    sw::vector<ContentBrowserCrumb> listCrumb;
    ContentBrowserLogic::makeFolderTrail( "engine", "D:\\Res\\Engine\\", "D:/Res/Engine/Shaders/bin", listCrumb );
    SW_ASSERT_EQUAL( 3u, static_cast<uint32>( listCrumb.size() ) );
    SW_EXPECT_STREQ( "engine", listCrumb[0]._label.c_str() );
    SW_EXPECT_STREQ( "D:/Res/Engine", listCrumb[0]._absolutePath.c_str() );
    SW_EXPECT_STREQ( "Shaders", listCrumb[1]._label.c_str() );
    SW_EXPECT_STREQ( "D:/Res/Engine/Shaders", listCrumb[1]._absolutePath.c_str() );
    SW_EXPECT_STREQ( "bin", listCrumb[2]._label.c_str() );
    SW_EXPECT_STREQ( "D:/Res/Engine/Shaders/bin", listCrumb[2]._absolutePath.c_str() );

    // 루트 자신은 조각 하나.
    ContentBrowserLogic::makeFolderTrail( "engine", "D:/Res/Engine", "D:/Res/Engine", listCrumb );
    SW_ASSERT_EQUAL( 1u, static_cast<uint32>( listCrumb.size() ) );
    SW_EXPECT_STREQ( "engine", listCrumb[0]._label.c_str() );

    // 이름 앞부분만 같은 형제 폴더는 루트 아래가 아니다 — 그 폴더 한 조각.
    ContentBrowserLogic::makeFolderTrail( "engine", "D:/Res/Engine", "D:/Res/EngineExtra/x", listCrumb );
    SW_ASSERT_EQUAL( 1u, static_cast<uint32>( listCrumb.size() ) );
    SW_EXPECT_STREQ( "x", listCrumb[0]._label.c_str() );
    SW_EXPECT_STREQ( "D:/Res/EngineExtra/x", listCrumb[0]._absolutePath.c_str() );
}
