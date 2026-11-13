#include "pch.h"

#include "Core/Testing/TestFramework.h"

#include "Editor/Panels/ContentBrowser/Model/ContentBrowserBreadcrumb.h"
#include "Editor/Panels/ContentBrowser/Model/ContentBrowserFileActions.h"

using namespace sw;
using namespace sw::editor;

namespace
{
    struct ContentBrowserLogicTestUtil
    {
        static inline uint32 s_scanCount = 0;

        static void scanFolders( string_view folderAbs, vector<string>& outListChild )
        {
            (void)folderAbs;
            ++s_scanCount;
            outListChild = { "zeta", "alpha" };
        }
    };
    using Util = ContentBrowserLogicTestUtil;
} // namespace

// 결함: 대기 칸이 하나라 역색인을 기다리는 사이 두 에셋을 옮기면 앞 옮기기가 말없이 사라졌다 - 온 순서대로 돈다.
SW_TEST_CASE( ContentBrowserMoveQueueTest, MovesWaitInOrder )
{
    ContentBrowserMoveQueue queue;
    SW_EXPECT_TRUE( queue.isEmpty() );
    queue.addMove( "D:/c/a.mesh", "D:/c/x" );
    queue.addMove( "D:/c/b.mesh", "D:/c/y" );
    SW_EXPECT_FALSE( queue.isEmpty() );

    ContentBrowserMoveQueue::Move move;
    SW_REQUIRE_TRUE( queue.takeNext( move ) );
    SW_EXPECT_EQUAL( move._sourceAbs, string( "D:/c/a.mesh" ) );
    SW_EXPECT_EQUAL( move._folderAbs, string( "D:/c/x" ) );
    SW_EXPECT_FALSE( move._bRename );
    SW_REQUIRE_TRUE( queue.takeNext( move ) );
    SW_EXPECT_EQUAL( move._sourceAbs, string( "D:/c/b.mesh" ) );
    SW_EXPECT_FALSE( queue.takeNext( move ) );
    SW_EXPECT_TRUE( queue.isEmpty() );
}

// 한 에셋을 또 옮기면 나중 목적지만 남는다 - 먼저 옮기면 나중 옮기기의 원본이 사라진다.
SW_TEST_CASE( ContentBrowserMoveQueueTest, SameAssetKeepsTheLastFolder )
{
    ContentBrowserMoveQueue queue;
    queue.addMove( "D:/c/a.mesh", "D:/c/x" );
    queue.addMove( "D:\\c\\a.mesh", "D:/c/y" );

    ContentBrowserMoveQueue::Move move;
    SW_REQUIRE_TRUE( queue.takeNext( move ) );
    SW_EXPECT_EQUAL( move._folderAbs, string( "D:/c/y" ) );
    SW_EXPECT_TRUE( queue.isEmpty() );
}

// 결함: 기다리던 새 이름이 취소 뒤에 시작한 다른 에셋에 붙을 수 있었다 - 이름 바꾸기는 하나뿐이고 버릴 수 있다. 폴더 옮기기는 남는다.
SW_TEST_CASE( ContentBrowserMoveQueueTest, RenameIsSingleAndDiscardable )
{
    ContentBrowserMoveQueue queue;
    queue.addMove( "D:/c/a.mesh", "D:/c/x" );
    queue.addRename( "first" );
    queue.addRename( "second" );

    ContentBrowserMoveQueue::Move move;
    SW_REQUIRE_TRUE( queue.takeNext( move ) );
    SW_EXPECT_FALSE( move._bRename );
    SW_REQUIRE_TRUE( queue.takeNext( move ) );
    SW_EXPECT_TRUE( move._bRename );
    SW_EXPECT_EQUAL( move._newName, string( "second" ) );
    SW_EXPECT_TRUE( queue.isEmpty() );

    queue.addRename( "third" );
    queue.addMove( "D:/c/b.mesh", "D:/c/x" );
    queue.discardRename();
    SW_REQUIRE_TRUE( queue.takeNext( move ) );
    SW_EXPECT_FALSE( move._bRename );
    SW_EXPECT_EQUAL( move._sourceAbs, string( "D:/c/b.mesh" ) );
    SW_EXPECT_TRUE( queue.isEmpty() );
}

SW_TEST_CASE( ContentBrowserLogicTest, FolderTrailHasOneBreadcrumbPerFolderBelowTheRoot )
{
    vector<ContentBrowserBreadcrumb> listBreadcrumb;
    ContentBrowserBreadcrumb::makeFolderTrail( "Content", "D:/Project/Content/", "D:\\Project\\Content\\game\\maps", listBreadcrumb );
    SW_REQUIRE_EQUAL( size_t{ 3 }, listBreadcrumb.size() );
    SW_EXPECT_TRUE( listBreadcrumb[0]._label == "Content" );
    SW_EXPECT_TRUE( listBreadcrumb[0]._absolutePath == "D:/Project/Content" );
    SW_EXPECT_TRUE( listBreadcrumb[1]._label == "game" );
    SW_EXPECT_TRUE( listBreadcrumb[1]._absolutePath == "D:/Project/Content/game" );
    SW_EXPECT_TRUE( listBreadcrumb[2]._label == "maps" );
    SW_EXPECT_TRUE( listBreadcrumb[2]._absolutePath == "D:/Project/Content/game/maps" );
}

SW_TEST_CASE( ContentBrowserLogicTest, TheRootItselfIsOneBreadcrumb )
{
    vector<ContentBrowserBreadcrumb> listBreadcrumb;
    ContentBrowserBreadcrumb::makeFolderTrail( "Content", "D:/Project/Content", "D:/Project/Content/", listBreadcrumb );
    SW_REQUIRE_EQUAL( size_t{ 1 }, listBreadcrumb.size() );
    SW_EXPECT_TRUE( listBreadcrumb[0]._label == "Content" );
}

// 이름 앞부분만 같은 형제 폴더는 루트 아래가 아니다.
SW_TEST_CASE( ContentBrowserLogicTest, FolderOutsideTheRootIsASingleBreadcrumb )
{
    vector<ContentBrowserBreadcrumb> listBreadcrumb;
    ContentBrowserBreadcrumb::makeFolderTrail( "Content", "D:/Project/Content", "D:/Project/Content2/x", listBreadcrumb );
    SW_REQUIRE_EQUAL( size_t{ 1 }, listBreadcrumb.size() );
    SW_EXPECT_TRUE( listBreadcrumb[0]._label == "x" );
    SW_EXPECT_TRUE( listBreadcrumb[0]._absolutePath == "D:/Project/Content2/x" );

    ContentBrowserBreadcrumb::makeFolderTrail( "Content", "", "D:/Project/y", listBreadcrumb );
    SW_REQUIRE_EQUAL( size_t{ 1 }, listBreadcrumb.size() );
    SW_EXPECT_TRUE( listBreadcrumb[0]._label == "y" );
}

SW_TEST_CASE( ContentBrowserLogicTest, FavoriteTrailStartsWithAnInertGroup )
{
    vector<ContentBrowserBreadcrumb> listBreadcrumb;
    ContentBrowserBreadcrumb::makeFavoriteTrail( "Maps", "D:/Project/Content/maps/", listBreadcrumb );
    SW_REQUIRE_EQUAL( size_t{ 2 }, listBreadcrumb.size() );
    SW_EXPECT_TRUE( listBreadcrumb[0]._absolutePath.empty() ); // 누를 수 없다
    SW_EXPECT_TRUE( listBreadcrumb[1]._label == "Maps" );
    SW_EXPECT_TRUE( listBreadcrumb[1]._absolutePath == "D:/Project/Content/maps" );

    ContentBrowserBreadcrumb::appendChildBreadcrumb( "D:\\Project\\Content\\maps\\town\\", listBreadcrumb );
    SW_REQUIRE_EQUAL( size_t{ 3 }, listBreadcrumb.size() );
    SW_EXPECT_TRUE( listBreadcrumb[2]._label == "town" );
    SW_EXPECT_TRUE( listBreadcrumb[2]._absolutePath == "D:/Project/Content/maps/town" );
}

SW_TEST_CASE( ContentBrowserFolderCacheTest, ScansOncePerFolderAndSortsChildren )
{
    Util::s_scanCount = 0;
    ContentBrowserFolderCache cache{ &Util::scanFolders };
    const vector<string>&     listChild = cache.getOrScanChildFolders( "D:/a" );
    SW_REQUIRE_EQUAL( size_t{ 2 }, listChild.size() );
    SW_EXPECT_TRUE( listChild[0] == "alpha" );
    SW_EXPECT_EQUAL( 1u, Util::s_scanCount );

    (void)cache.getOrScanChildFolders( "D:/a/" ); // 같은 폴더다
    SW_EXPECT_EQUAL( 1u, Util::s_scanCount );
    SW_EXPECT_EQUAL( 1u, cache.getCachedFolderCount() );

    const vector<string>& listOther = cache.getOrScanChildFolders( "D:/b" );
    SW_EXPECT_EQUAL( 2u, Util::s_scanCount );
    SW_EXPECT_EQUAL( size_t{ 2 }, listChild.size() ); // 표가 늘어도 앞서 받은 참조가 유효하다
    SW_EXPECT_EQUAL( size_t{ 2 }, listOther.size() );

    cache.clear();
    (void)cache.getOrScanChildFolders( "D:/a" );
    SW_EXPECT_EQUAL( 3u, Util::s_scanCount );

    ContentBrowserFolderCache noScan{ nullptr };
    SW_EXPECT_TRUE( noScan.getOrScanChildFolders( "D:/a" ).empty() );
}

SW_TEST_CASE( ContentBrowserHistoryTest, BackAndForwardTraverseTheVisits )
{
    ContentBrowserHistory history;
    SW_EXPECT_FALSE( history.canNavigateBack() );
    SW_EXPECT_TRUE( history.stepBack() == nullptr );
    SW_EXPECT_TRUE( history.stepForward() == nullptr );

    history.record( "D:/a", {} );
    SW_EXPECT_FALSE( history.canNavigateBack() ); // 지금 자리뿐이다
    history.record( "D:/b", {} );
    history.record( "D:/c", {} );
    const ContentBrowserHistoryEntry* pBack = history.stepBack();
    SW_REQUIRE_NOT_NULL( pBack );
    SW_EXPECT_TRUE( pBack->_folderPathAbs == "D:/b" );
    SW_EXPECT_TRUE( history.canNavigateForward() );
    const ContentBrowserHistoryEntry* pForward = history.stepForward();
    SW_REQUIRE_NOT_NULL( pForward );
    SW_EXPECT_TRUE( pForward->_folderPathAbs == "D:/c" );
    SW_EXPECT_FALSE( history.canNavigateForward() );
}

SW_TEST_CASE( ContentBrowserHistoryTest, SameFolderIsNotRecordedTwice )
{
    ContentBrowserHistory history;
    history.record( "D:/a", {} );
    history.record( "D:\\a\\", {} );
    SW_EXPECT_FALSE( history.canNavigateBack() );
}

// 뒤로 간 뒤 새로 가면 앞으로 갈 길은 사라진다(브라우저와 같다).
SW_TEST_CASE( ContentBrowserHistoryTest, NewVisitDropsTheForwardPath )
{
    ContentBrowserHistory history;
    history.record( "D:/a", {} );
    history.record( "D:/b", {} );
    (void)history.stepBack();
    history.record( "D:/c", {} );
    SW_EXPECT_FALSE( history.canNavigateForward() );
    const ContentBrowserHistoryEntry* pBack = history.stepBack();
    SW_REQUIRE_NOT_NULL( pBack );
    SW_EXPECT_TRUE( pBack->_folderPathAbs == "D:/a" );
}

SW_TEST_CASE( ContentBrowserHistoryTest, OldestVisitsFallOffTheEnd )
{
    ContentBrowserHistory history;
    for ( size_t index = 0; index < ContentBrowserHistory::kMaxEntryCount + 10; ++index )
    {
        history.record( "D:/f" + to_string( static_cast<uint64>( index ) ), {} );
    }
    size_t                            backCount = 0;
    const ContentBrowserHistoryEntry* pEntry    = nullptr;
    while ( history.canNavigateBack() )
    {
        pEntry = history.stepBack();
        ++backCount;
    }
    SW_EXPECT_EQUAL( ContentBrowserHistory::kMaxEntryCount - 1, backCount );
    SW_REQUIRE_NOT_NULL( pEntry );
    SW_EXPECT_TRUE( pEntry->_folderPathAbs == "D:/f10" );
}
