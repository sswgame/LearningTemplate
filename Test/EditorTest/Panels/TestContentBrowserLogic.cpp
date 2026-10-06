#include "pch.h"

#include "Editor/Panels/ContentBrowserLogic.h"

#include "TestFramework/TestFramework.h"

// ContentBrowserLogicTest — 콘텐츠 브라우저의 판단(패널 점검 D12 · D15): 경로 줄 조각이 저마다 절대 경로를 든다, 폴더 트리는 폴더마다 한 번만
// 디스크를 읽는다. ImGui 없음.

namespace
{
    struct TestContentBrowserLogicInternal
    {
        static uint32& getScanCount()
        {
            static uint32 s_scanCount = 0;
            return s_scanCount;
        }

        /** @brief 디스크 대신 — 부를 때마다 세고, 정렬되지 않은 하위 폴더 둘을 낸다. */
        static void scanTwoChildren( sw::string_view folderAbs, sw::vector<sw::string>& outListChild )
        {
            ++getScanCount();
            outListChild.clear();
            outListChild.push_back( sw::string{ folderAbs } + "/zeta" );
            outListChild.push_back( sw::string{ folderAbs } + "/alpha" );
        }
    };
} // namespace

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

/**
 * @brief [ContentBrowserLogicTest] 폴더 트리 캐시는 폴더마다 처음 물을 때 한 번만 디스크를 읽고(정렬해 둔다), 표가 커져도 먼저 준 참조가 살며, clear 뒤에 다시 읽는다
 * @details 트리를 그릴 때마다 보이는 노드마다 하위 폴더를 디스크에서 읽어 콘텐츠 브라우저 하나가 Debug 16.8 ms 였다(패널 점검 D15).
 */
SW_TEST_CASE( ContentBrowserLogicTest, FolderCacheReadsEachFolderOnce )
{
    using sw::editor::ContentBrowserFolderCache;
    TestContentBrowserLogicInternal::getScanCount() = 0;
    ContentBrowserFolderCache cache{ &TestContentBrowserLogicInternal::scanTwoChildren };

    const sw::vector<sw::string>& listRootChild = cache.getChildFolders( "D:/Res/engine" );
    SW_ASSERT_EQUAL( 2u, static_cast<uint32>( listRootChild.size() ) );
    SW_EXPECT_STREQ( "D:/Res/engine/alpha", listRootChild[0].c_str() ); // 정렬해 둔다
    for ( uint32 frameIndex = 0; frameIndex < 10; ++frameIndex )
    {
        (void)cache.getChildFolders( "D:/Res/engine" );
        (void)cache.getChildFolders( "D:\\Res\\Engine\\" ); // 대소문자 · 구분자 · 끝 슬래시가 달라도 같은 폴더
    }
    SW_EXPECT_EQUAL( 1u, TestContentBrowserLogicInternal::getScanCount() );

    // 다른 폴더를 많이 물어 표가 커져도 먼저 받은 참조는 산다(트리 그리기가 재귀 중에 든다).
    for ( uint32 folderIndex = 0; folderIndex < 64; ++folderIndex )
    {
        sw::string folder{ "D:/Res/f" };
        folder.push_back( static_cast<utf8>( 'a' + folderIndex / 26 ) );
        folder.push_back( static_cast<utf8>( 'a' + folderIndex % 26 ) );
        (void)cache.getChildFolders( folder );
    }
    SW_EXPECT_EQUAL( 65u, TestContentBrowserLogicInternal::getScanCount() );
    SW_EXPECT_EQUAL( 65u, cache.getCachedFolderCount() );
    SW_EXPECT_STREQ( "D:/Res/engine/alpha", listRootChild[0].c_str() );

    cache.clear();
    (void)cache.getChildFolders( "D:/Res/engine" );
    SW_EXPECT_EQUAL( 66u, TestContentBrowserLogicInternal::getScanCount() );
}
