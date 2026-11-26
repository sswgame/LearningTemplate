// AssetWriterTest — 에디터가 파일을 쓰는 문 하나: 읽기 전용 · 밖에서 바뀐 파일 · 없는 폴더 · 여러 파일 저장 · 되돌리기 쓰기(nogpu). 읽기 전용은 버전 관리가 잠그지 않은 파일(git LFS lockable)이다.
#include "pch.h"

#include "Core/Container/span.h"
#include "Core/File/FileUtil.h"
#include "Core/Path/AbsolutePath.h"
#include "Core/TestFramework/TestFramework.h"

#include "Editor/Common/Asset/AssetWriter.h"

#define SW_TEST_INTERNAL // 임시 리소스 도메인 픽스처(`ScopedScratchDomain`)가 엔진 서비스를 직접 쓴다
#include "Engine/Content/Tests/ScopedScratchDomain.h"

using namespace sw;
using namespace sw::editor;

namespace
{
    string readText( const DiskPath& path )
    {
        string text;
        SW_EXPECT_TRUE( FileUtil::readTextFile( path, text ) );
        return text;
    }

    /** @brief `saveToFile` 만 있는 에셋 흉내입니다. 쓸지 정한다. */
    struct FakeAsset
    {
        bool _bWrites{ true };

        bool saveToFile( const AbsolutePath& path ) const { return _bWrites && FileUtil::writeTextFile( path, "asset" ); }
    };
} // namespace

// 리소스 id 를 푼 경로는 쓰는 함수에 들어가지 않는다 — 문을 지나지 않은 쓰기가 컴파일되지 않는다.
static_assert( std::is_convertible_v<DiskPath, AbsolutePath> == false, "a content path must not turn into a writable path" );
static_assert( std::is_invocable_v<decltype( &FileUtil::writeTextFile ), const DiskPath&, string_view> == false, "FileUtil must not write a content path" );
static_assert( std::is_invocable_v<decltype( &FileUtil::removeFile ), const DiskPath&> == false, "FileUtil must not remove a content path" );

/** @brief [AssetWriterTest] 읽기 전용 파일은 쓰기 앞에서 거절하고(알림 · 경고) 파일 내용 · 속성은 그대로다. 쓰기를 다시 허락하면 쓴다 */
SW_TEST_CASE( AssetWriterTest, ReadOnlyFileIsRefusedAndKept )
{
    const test::ScopedScratchDomain domain;
    const AbsolutePath              path = domain.makeDiskPath( "locked.material" );
    AssetWriter                     writer;
    SW_REQUIRE_TRUE( writer.writeText( path, "kept" ) );
    SW_REQUIRE_TRUE( FileUtil::setWritable( path, false ) );

    SW_EXPECT_LOG( "AssetWriter.ReadOnly" );
    SW_EXPECT_TRUE( AssetWriter::warnIfReadOnly( path, AssetWriteKind::Save ) );
    SW_EXPECT_FALSE( writer.writeText( path, "overwritten" ) );
    SW_EXPECT_TRUE( FileUtil::isReadOnlyFile( path ) );
    SW_EXPECT_EQUAL( readText( path ), string( "kept" ) );

    SW_REQUIRE_TRUE( FileUtil::setWritable( path, true ) ); // 케이스 폴더 정리가 Windows 에서 읽기 전용 파일을 지우지 못한다
    SW_EXPECT_FALSE( AssetWriter::warnIfReadOnly( path, AssetWriteKind::Save ) );
    SW_EXPECT_TRUE( writer.writeText( path, "overwritten" ) );
}

/** @brief [AssetWriterTest] 폴더는 그 아래 모든 파일을 본다 — 깊은 곳의 읽기 전용 파일 하나가 폴더 전체를 막는다. 없는 자리(새로 만들 파일)는 막지 않는다 */
SW_TEST_CASE( AssetWriterTest, FolderIsRefusedByAReadOnlyFileUnderIt )
{
    const AbsolutePath folder     = test::makeScratchPath( "pack" );
    const AbsolutePath writable   = folder.join( "a.material" );
    const AbsolutePath readOnly   = folder.join( "deep/b.material" );
    const AbsolutePath missingNew = folder.join( "new.material" );
    SW_REQUIRE_TRUE( FileUtil::writeTextFile( writable, "a" ) );
    SW_REQUIRE_TRUE( FileUtil::writeTextFile( readOnly, "b" ) );
    SW_EXPECT_FALSE( AssetWriter::findReadOnlyAtOrUnder( folder ).has_value() );

    SW_REQUIRE_TRUE( FileUtil::setWritable( readOnly, false ) );
    const optional<string> found = AssetWriter::findReadOnlyAtOrUnder( folder );
    SW_REQUIRE_TRUE( found.has_value() );
    SW_EXPECT_EQUAL( *found, FileUtil::normalizeSeparators( readOnly.str() ) );
    SW_EXPECT_FALSE( AssetWriter::findReadOnlyAtOrUnder( writable ).has_value() );
    SW_EXPECT_FALSE( AssetWriter::findReadOnlyAtOrUnder( missingNew ).has_value() );

    SW_REQUIRE_TRUE( FileUtil::setWritable( readOnly, true ) );
}

/** @brief [AssetWriterTest] 기준이 없거나 같은 파일은 막지 않고, 밖에서 바뀐 파일은 한 번 막는다. 알린 내용이 또 바뀌면 다시 막는다 */
SW_TEST_CASE( AssetWriterTest, OutsideChangeIsRefusedOncePerContent )
{
    const test::ScopedScratchDomain domain;
    const AbsolutePath              path = domain.makeDiskPath( "doc.xml" );
    AssetWriter                     writer;
    SW_EXPECT_TRUE( writer.validateSave( path ) ); // 아직 없는 새 자리

    string text;
    SW_REQUIRE_TRUE( FileUtil::writeTextFile( path, "opened" ) );
    SW_REQUIRE_TRUE( writer.readText( path, text ) );
    SW_EXPECT_TRUE( writer.validateSave( path ) );
    SW_EXPECT_FALSE( writer.isChangedOnDisk( path ) );

    SW_REQUIRE_TRUE( FileUtil::writeTextFile( path, "changed outside" ) );
    SW_EXPECT_LOG( "AssetWriter.ChangedOnDisk" );
    SW_EXPECT_TRUE( writer.isChangedOnDisk( path ) );
    SW_EXPECT_FALSE( writer.writeText( path, "mine" ) );
    SW_EXPECT_EQUAL( readText( path ), string( "changed outside" ) );

    SW_REQUIRE_TRUE( FileUtil::writeTextFile( path, "changed outside once more" ) );
    SW_EXPECT_FALSE( writer.writeText( path, "mine" ) );
    SW_EXPECT_TRUE( writer.writeText( path, "mine" ) ); // 알고 다시 저장한다
    SW_EXPECT_EQUAL( readText( path ), string( "mine" ) );
    SW_EXPECT_TRUE( writer.writeText( path, "mine again" ) ); // 쓴 내용이 새 기준이다

    SW_REQUIRE_TRUE( FileUtil::removeFile( path ) );
    SW_EXPECT_TRUE( writer.validateSave( path ) ); // 지워진 파일은 다시 만든다
}

/** @brief [AssetWriterTest] 부모 폴더가 없으면 만들고, 쓰기가 지면 false 이고 기준은 그대로다(다음 저장이 밖에서 바뀐 것으로 보지 않는다) */
SW_TEST_CASE( AssetWriterTest, MissingFolderIsMadeAndAFailedSaveKeepsTheBaseline )
{
    const test::ScopedScratchDomain domain;
    const AbsolutePath              path = domain.makeDiskPath( "deep/new/asset.xml" );
    AssetWriter                     writer;
    SW_EXPECT_TRUE( writer.saveAsset( path, FakeAsset{} ) );
    SW_EXPECT_EQUAL( readText( path ), string( "asset" ) );

    SW_EXPECT_FALSE( writer.saveAsset( path, FakeAsset{ false } ) );
    SW_EXPECT_EQUAL( readText( path ), string( "asset" ) );
    SW_EXPECT_TRUE( writer.validateSave( path ) );
    SW_EXPECT_TRUE( writer.writeText( path, "next" ) );
}

/** @brief [AssetWriterTest] 여러 파일 저장은 첫 파일을 쓰기 전에 모두 본다 — 하나라도 바뀌었으면 아무것도 쓰지 않고, 바뀐 파일을 한 번에 알려 다시 저장 한 번이면 모두 쓴다 */
SW_TEST_CASE( AssetWriterTest, EveryFileIsCheckedBeforeTheFirstWrite )
{
    const test::ScopedScratchDomain domain;
    const DiskPath                  arrPath[] = { domain.makeDiskPath( "a.xml" ), domain.makeDiskPath( "b.xml" ), domain.makeDiskPath( "c.xml" ) };
    AssetWriter                     writer;
    for ( const DiskPath& path : arrPath )
        SW_REQUIRE_TRUE( writer.writeText( path, "opened" ) );
    SW_EXPECT_TRUE( writer.validateSave( arrPath ) );

    SW_REQUIRE_TRUE( FileUtil::writeTextFile( domain.makeDiskPath( "a.xml" ), "outside" ) );
    SW_REQUIRE_TRUE( FileUtil::writeTextFile( domain.makeDiskPath( "c.xml" ), "outside" ) );
    SW_EXPECT_LOG( "AssetWriter.ChangedOnDisk" );
    SW_EXPECT_FALSE( writer.validateSave( arrPath ) );
    SW_EXPECT_TRUE( writer.validateSave( arrPath ) );
    for ( const DiskPath& path : arrPath )
        SW_EXPECT_TRUE( writer.writeText( path, "saved" ) );
}

/** @brief [AssetWriterTest] 되돌리기 쓰기는 자기가 쓴 글만 바꾼다 — 그 사이 밖에서 바뀐 파일은 알리고 남긴다 */
SW_TEST_CASE( AssetWriterTest, UndoWriteReplacesOnlyTheTextItWrote )
{
    const test::ScopedScratchDomain domain;
    const AbsolutePath              path = domain.makeDiskPath( "rig.xml" );
    SW_REQUIRE_TRUE( FileUtil::writeTextFile( path, "after" ) );
    SW_EXPECT_TRUE( AssetWriter::replaceText( path, "after", "before" ) ); // 되돌리기
    SW_EXPECT_EQUAL( readText( path ), string( "before" ) );
    SW_EXPECT_TRUE( AssetWriter::replaceText( path, "before", "after" ) ); // 다시 하기

    SW_REQUIRE_TRUE( FileUtil::writeTextFile( path, "edited outside" ) );
    SW_EXPECT_LOG( "AssetWriter.ChangedOnDisk" );
    SW_EXPECT_FALSE( AssetWriter::replaceText( path, "after", "before" ) );
    SW_EXPECT_EQUAL( readText( path ), string( "edited outside" ) );
}
