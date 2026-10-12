#include "pch.h"

#include "Core/File/FileUtil.h"

#include "Editor/Common/Commands/EditorFolderListing.h"

#include "TestFramework/TestFramework.h"

using namespace sw;
using namespace sw::editor;

namespace
{
    struct TestEditorFolderListingInternal
    {
        /** @brief `sub/b.txt` · `a.txt` · `a.txt.meta` 가 든 임시 폴더를 새로 만듭니다. */
        static string makeFixtureFolder()
        {
            const string folder = FileUtil::joinPath( FileUtil::getTempDirectory(), "sw_editor_folder_listing" );
            (void)FileUtil::removeDirectory( folder ); // 앞 실행이 남긴 것 — 없으면 그대로다
            const string subFolder = FileUtil::joinPath( folder, "sub" );
            SW_EXPECT_TRUE( FileUtil::ensureDirectoryExists( subFolder ) );
            SW_EXPECT_TRUE( FileUtil::writeTextFile( FileUtil::joinPath( folder, "a.txt" ), "a" ) );
            SW_EXPECT_TRUE( FileUtil::writeTextFile( FileUtil::joinPath( folder, "a.txt.meta" ), "meta" ) );
            SW_EXPECT_TRUE( FileUtil::writeTextFile( FileUtil::joinPath( subFolder, "b.txt" ), "b" ) );
            return folder;
        }

        static const EditorFolderListingEntry* findEntry( const vector<EditorFolderListingEntry>& listEntry, const utf8* pName )
        {
            for ( const EditorFolderListingEntry& entry : listEntry )
            {
                if ( entry._name == pName )
                    return &entry;
            }
            return nullptr;
        }
    };
} // namespace

/**
 * @brief [EditorFolderListingTest] 목록은 폴더와 파일을 함께 담고 `.meta` 는 뺀다. 재귀면 파일만 하위 폴더까지 모은다
 */
SW_TEST_CASE( EditorFolderListingTest, ListingSkipsMetaAndRecursesOnlyIntoFiles )
{
    const string folder = TestEditorFolderListingInternal::makeFixtureFolder();

    vector<EditorFolderListingEntry> listEntry;
    EditorFolderListing::collectFolderListing( folder, listEntry );
    SW_EXPECT_EQUAL( size_t{ 2 }, listEntry.size() );
    const EditorFolderListingEntry* pSub = TestEditorFolderListingInternal::findEntry( listEntry, "sub" );
    const EditorFolderListingEntry* pA   = TestEditorFolderListingInternal::findEntry( listEntry, "a.txt" );
    SW_ASSERT_TRUE( pSub != nullptr && pA != nullptr );
    SW_EXPECT_TRUE( pSub->_bIsDirectory );
    SW_EXPECT_FALSE( pA->_bIsDirectory );
    SW_EXPECT_FALSE( pA->_extension.empty() );
    SW_EXPECT_FALSE( pA->_relativePath.empty() );
    SW_EXPECT_TRUE( TestEditorFolderListingInternal::findEntry( listEntry, "a.txt.meta" ) == nullptr );

    EditorFolderListing::collectFolderListing( folder, listEntry, true );
    SW_EXPECT_EQUAL( size_t{ 3 }, listEntry.size() );
    SW_EXPECT_TRUE( TestEditorFolderListingInternal::findEntry( listEntry, "b.txt" ) != nullptr );

    EditorFolderListing::collectFolderListing( FileUtil::joinPath( folder, "missing" ), listEntry );
    SW_EXPECT_TRUE( listEntry.empty() );
    (void)FileUtil::removeDirectory( folder ); // 임시 폴더라 남아도 다음 실행이 지운다
}

/**
 * @brief [EditorFolderListingTest] 하위 폴더 읽기는 부를 때마다 횟수를 하나씩 올리고 경로 구분자를 `/` 로 맞춘다
 */
SW_TEST_CASE( EditorFolderListingTest, ChildFoldersCountEachDiskScan )
{
    const string folder = TestEditorFolderListingInternal::makeFixtureFolder();

    const uint64   scanCountBefore = EditorFolderListing::getChildFolderScanCount();
    vector<string> listChild;
    EditorFolderListing::collectChildFolders( folder, listChild );
    SW_EXPECT_EQUAL( scanCountBefore + 1, EditorFolderListing::getChildFolderScanCount() );
    SW_ASSERT_EQUAL( size_t{ 1 }, listChild.size() );
    SW_EXPECT_TRUE( listChild[0].find( '\\' ) == string::npos );
    SW_EXPECT_STREQ( "sub", FileUtil::getFileNamePart( listChild[0] ).c_str() );

    EditorFolderListing::collectChildFolders( "", listChild );
    SW_EXPECT_TRUE( listChild.empty() );
    SW_EXPECT_EQUAL( scanCountBefore + 2, EditorFolderListing::getChildFolderScanCount() );
    (void)FileUtil::removeDirectory( folder ); // 임시 폴더라 남아도 다음 실행이 지운다
}
