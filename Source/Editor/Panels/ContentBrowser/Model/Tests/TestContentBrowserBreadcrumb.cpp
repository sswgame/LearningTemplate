#include "pch.h"

#include "Core/File/FileUtil.h"
#include "Core/File/ProcessRoots.h"
#include "Core/String/StringBuilder.h"
#include "Core/String/fixed_string.h"
#include "Core/String/formatString.h"
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

        static AbsolutePath makePath( string_view relative ) { return ProcessRoots::get()._saved.join( relative ); }

        static AbsolutePath withTrailingSlash( const AbsolutePath& path )
        {
            StringBuilder<constant::buffer::kMax512> text;
            text.appendFormat( "%#/", path.str() );
            return FileUtil::toDiskPath( text.view() );
        }

        static void scanFolders( const AbsolutePath& folderAbs, vector<AbsolutePath>& outListChild )
        {
            (void)folderAbs;
            ++s_scanCount;
            outListChild = { makePath( "zeta" ), makePath( "alpha" ) };
        }
    };
    using Util = ContentBrowserLogicTestUtil;
} // namespace

// 결함: 대기 칸이 하나라 역색인을 기다리는 사이 두 에셋을 옮기면 앞 옮기기가 말없이 사라졌다 - 온 순서대로 돈다.
SW_TEST_CASE( ContentBrowserMoveQueueTest, MovesWaitInOrder )
{
    ContentBrowserMoveQueue queue;
    SW_EXPECT_TRUE( queue.isEmpty() );
    queue.addMove( Util::makePath( "c/a.mesh" ), Util::makePath( "c/x" ) );
    queue.addMove( Util::makePath( "c/b.mesh" ), Util::makePath( "c/y" ) );
    SW_EXPECT_FALSE( queue.isEmpty() );

    ContentBrowserMoveQueue::Move move;
    SW_REQUIRE_TRUE( queue.takeNext( move ) );
    SW_EXPECT_TRUE( move._sourceAbs == Util::makePath( "c/a.mesh" ) );
    SW_EXPECT_TRUE( move._folderAbs == Util::makePath( "c/x" ) );
    SW_EXPECT_FALSE( move._bRename );
    SW_REQUIRE_TRUE( queue.takeNext( move ) );
    SW_EXPECT_TRUE( move._sourceAbs == Util::makePath( "c/b.mesh" ) );
    SW_EXPECT_FALSE( queue.takeNext( move ) );
    SW_EXPECT_TRUE( queue.isEmpty() );
}

// 한 에셋을 또 옮기면 나중 목적지만 남는다 - 먼저 옮기면 나중 옮기기의 원본이 사라진다.
SW_TEST_CASE( ContentBrowserMoveQueueTest, SameAssetKeepsTheLastFolder )
{
    ContentBrowserMoveQueue queue;
    queue.addMove( Util::makePath( "c/a.mesh" ), Util::makePath( "c/x" ) );
    queue.addMove( Util::makePath( "/c//a.mesh" ), Util::makePath( "c/y" ) );

    ContentBrowserMoveQueue::Move move;
    SW_REQUIRE_TRUE( queue.takeNext( move ) );
    SW_EXPECT_TRUE( move._folderAbs == Util::makePath( "c/y" ) );
    SW_EXPECT_TRUE( queue.isEmpty() );
}

// 결함: 기다리던 새 이름이 취소 뒤에 시작한 다른 에셋에 붙을 수 있었다 - 이름 바꾸기는 하나뿐이고 버릴 수 있다. 폴더 옮기기는 남는다.
SW_TEST_CASE( ContentBrowserMoveQueueTest, RenameIsSingleAndDiscardable )
{
    ContentBrowserMoveQueue queue;
    queue.addMove( Util::makePath( "c/a.mesh" ), Util::makePath( "c/x" ) );
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
    queue.addMove( Util::makePath( "c/b.mesh" ), Util::makePath( "c/x" ) );
    queue.discardRename();
    SW_REQUIRE_TRUE( queue.takeNext( move ) );
    SW_EXPECT_FALSE( move._bRename );
    SW_EXPECT_TRUE( move._sourceAbs == Util::makePath( "c/b.mesh" ) );
    SW_EXPECT_TRUE( queue.isEmpty() );
}

SW_TEST_CASE( ContentBrowserLogicTest, FolderTrailHasOneBreadcrumbPerFolderBelowTheRoot )
{
    vector<ContentBrowserBreadcrumb> listBreadcrumb;
    const AbsolutePath               content = Util::makePath( "Content" );
    ContentBrowserBreadcrumb::makeFolderTrail( "Content", Util::withTrailingSlash( content ), content.join( "game/maps" ), listBreadcrumb );
    SW_REQUIRE_EQUAL( size_t{ 3 }, listBreadcrumb.size() );
    SW_EXPECT_TRUE( listBreadcrumb[0]._label == "Content" );
    SW_EXPECT_TRUE( listBreadcrumb[0]._absolutePath == content );
    SW_EXPECT_TRUE( listBreadcrumb[1]._label == "game" );
    SW_EXPECT_TRUE( listBreadcrumb[1]._absolutePath == content.join( "game" ) );
    SW_EXPECT_TRUE( listBreadcrumb[2]._label == "maps" );
    SW_EXPECT_TRUE( listBreadcrumb[2]._absolutePath == content.join( "game/maps" ) );
}

SW_TEST_CASE( ContentBrowserLogicTest, TheRootItselfIsOneBreadcrumb )
{
    vector<ContentBrowserBreadcrumb> listBreadcrumb;
    const AbsolutePath               content = Util::makePath( "Content" );
    ContentBrowserBreadcrumb::makeFolderTrail( "Content", content, Util::withTrailingSlash( content ), listBreadcrumb );
    SW_REQUIRE_EQUAL( size_t{ 1 }, listBreadcrumb.size() );
    SW_EXPECT_TRUE( listBreadcrumb[0]._label == "Content" );
}

// 이름 앞부분만 같은 형제 폴더는 루트 아래가 아니다.
SW_TEST_CASE( ContentBrowserLogicTest, FolderOutsideTheRootIsASingleBreadcrumb )
{
    vector<ContentBrowserBreadcrumb> listBreadcrumb;
    const AbsolutePath               outside = Util::makePath( "Content2/x" );
    ContentBrowserBreadcrumb::makeFolderTrail( "Content", Util::makePath( "Content" ), outside, listBreadcrumb );
    SW_REQUIRE_EQUAL( size_t{ 1 }, listBreadcrumb.size() );
    SW_EXPECT_TRUE( listBreadcrumb[0]._label == "x" );
    SW_EXPECT_TRUE( listBreadcrumb[0]._absolutePath == outside );

    ContentBrowserBreadcrumb::makeFolderTrail( "Content", AbsolutePath{}, outside, listBreadcrumb );
    SW_REQUIRE_EQUAL( size_t{ 1 }, listBreadcrumb.size() );
    SW_EXPECT_TRUE( listBreadcrumb[0]._label == "x" );
}

SW_TEST_CASE( ContentBrowserLogicTest, FavoriteTrailStartsWithAnInertGroup )
{
    vector<ContentBrowserBreadcrumb> listBreadcrumb;
    const AbsolutePath               maps = Util::makePath( "maps" );
    ContentBrowserBreadcrumb::makeFavoriteTrail( "Maps", Util::withTrailingSlash( maps ), listBreadcrumb );
    SW_REQUIRE_EQUAL( size_t{ 2 }, listBreadcrumb.size() );
    SW_EXPECT_TRUE( listBreadcrumb[0]._absolutePath.empty() ); // 누를 수 없다
    SW_EXPECT_TRUE( listBreadcrumb[1]._label == "Maps" );
    SW_EXPECT_TRUE( listBreadcrumb[1]._absolutePath == maps );

    ContentBrowserBreadcrumb::appendChildBreadcrumb( Util::withTrailingSlash( maps.join( "town" ) ), listBreadcrumb );
    SW_REQUIRE_EQUAL( size_t{ 3 }, listBreadcrumb.size() );
    SW_EXPECT_TRUE( listBreadcrumb[2]._label == "town" );
    SW_EXPECT_TRUE( listBreadcrumb[2]._absolutePath == maps.join( "town" ) );
}

SW_TEST_CASE( ContentBrowserFolderCacheTest, ScansOncePerFolderAndSortsChildren )
{
    Util::s_scanCount = 0;
    ContentBrowserFolderCache   cache{ &Util::scanFolders };
    const vector<AbsolutePath>& listChild = cache.getOrScanChildFolders( Util::makePath( "a" ) );
    SW_REQUIRE_EQUAL( size_t{ 2 }, listChild.size() );
    SW_EXPECT_TRUE( listChild[0] == Util::makePath( "alpha" ) );
    SW_EXPECT_EQUAL( 1u, Util::s_scanCount );

    (void)cache.getOrScanChildFolders( Util::withTrailingSlash( Util::makePath( "a" ) ) ); // 같은 폴더다
    SW_EXPECT_EQUAL( 1u, Util::s_scanCount );
    SW_EXPECT_EQUAL( 1u, cache.getCachedFolderCount() );

    const vector<AbsolutePath>& listOther = cache.getOrScanChildFolders( Util::makePath( "b" ) );
    SW_EXPECT_EQUAL( 2u, Util::s_scanCount );
    SW_EXPECT_EQUAL( size_t{ 2 }, listChild.size() ); // 표가 늘어도 앞서 받은 참조가 유효하다
    SW_EXPECT_EQUAL( size_t{ 2 }, listOther.size() );

    cache.clear();
    (void)cache.getOrScanChildFolders( Util::makePath( "a" ) );
    SW_EXPECT_EQUAL( 3u, Util::s_scanCount );

    ContentBrowserFolderCache noScan{ nullptr };
    SW_EXPECT_TRUE( noScan.getOrScanChildFolders( Util::makePath( "a" ) ).empty() );
}

SW_TEST_CASE( ContentBrowserHistoryTest, BackAndForwardTraverseTheVisits )
{
    ContentBrowserHistory history;
    SW_EXPECT_FALSE( history.canNavigateBack() );
    SW_EXPECT_TRUE( history.stepBack() == nullptr );
    SW_EXPECT_TRUE( history.stepForward() == nullptr );

    history.record( Util::makePath( "a" ), {} );
    SW_EXPECT_FALSE( history.canNavigateBack() ); // 지금 자리뿐이다
    history.record( Util::makePath( "b" ), {} );
    history.record( Util::makePath( "c" ), {} );
    const ContentBrowserHistoryEntry* pBack = history.stepBack();
    SW_REQUIRE_NOT_NULL( pBack );
    SW_EXPECT_TRUE( pBack->_folderPathAbs == Util::makePath( "b" ) );
    SW_EXPECT_TRUE( history.canNavigateForward() );
    const ContentBrowserHistoryEntry* pForward = history.stepForward();
    SW_REQUIRE_NOT_NULL( pForward );
    SW_EXPECT_TRUE( pForward->_folderPathAbs == Util::makePath( "c" ) );
    SW_EXPECT_FALSE( history.canNavigateForward() );
}

SW_TEST_CASE( ContentBrowserHistoryTest, SameFolderIsNotRecordedTwice )
{
    ContentBrowserHistory history;
    history.record( Util::makePath( "a" ), {} );
    history.record( Util::withTrailingSlash( Util::makePath( "/a/" ) ), {} );
    SW_EXPECT_FALSE( history.canNavigateBack() );
}

// 뒤로 간 뒤 새로 가면 앞으로 갈 길은 사라진다(브라우저와 같다).
SW_TEST_CASE( ContentBrowserHistoryTest, NewVisitDropsTheForwardPath )
{
    ContentBrowserHistory history;
    history.record( Util::makePath( "a" ), {} );
    history.record( Util::makePath( "b" ), {} );
    (void)history.stepBack();
    history.record( Util::makePath( "c" ), {} );
    SW_EXPECT_FALSE( history.canNavigateForward() );
    const ContentBrowserHistoryEntry* pBack = history.stepBack();
    SW_REQUIRE_NOT_NULL( pBack );
    SW_EXPECT_TRUE( pBack->_folderPathAbs == Util::makePath( "a" ) );
}

SW_TEST_CASE( ContentBrowserHistoryTest, OldestVisitsFallOffTheEnd )
{
    ContentBrowserHistory history;
    for ( size_t index = 0; index < ContentBrowserHistory::kMaxEntryCount + 10; ++index )
    {
        fixed_string<constant::buffer::kMax32> folderPath;
        formatstring( folderPath.data(), folderPath.capacity(), "f%#", static_cast<uint64>( index ) );
        history.record( Util::makePath( folderPath ), {} );
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
    SW_EXPECT_TRUE( pEntry->_folderPathAbs == Util::makePath( "f10" ) );
}
