// EditorContentWriteTest — 저장 · 옮기기 · 삭제가 지나는 읽기 전용 확인 하나(감사 E-11, nogpu). 버전 관리가 잠그지 않은 파일(git LFS lockable = 읽기 전용)을 흉내 낸다.
#include "pch.h"

#include "Core/File/FileUtil.h"
#include "Core/Testing/TestFramework.h"

#include "Editor/Common/Commands/Asset/EditorAssetFileCommands.h"
#include "Editor/Common/Commands/Asset/EditorContentWrite.h"

using namespace sw;
using namespace sw::editor;

/** @brief [EditorContentWriteTest] 읽기 전용 파일은 저장 확인이 거절하고(알림 · 경고) 파일 내용 · 속성은 그대로다. 쓰기를 다시 허락하면 지나간다 */
SW_TEST_CASE( EditorContentWriteTest, ReadOnlyFileIsRefusedAndKept )
{
    SW_EXPECT_LOG( "EditorContentWrite.ReadOnly" );
    const string path = test::makeScratchPath( "locked.material" );
    SW_REQUIRE_TRUE( FileUtil::ensureParentDirectoryExists( path ) );
    SW_REQUIRE_TRUE( FileUtil::writeTextFile( path, "kept" ) );
    SW_EXPECT_FALSE( EditorContentWrite::refuseReadOnly( path, EditorContentWriteKind::Save ) );

    SW_REQUIRE_TRUE( FileUtil::setWritable( path, false ) );
    SW_EXPECT_TRUE( EditorContentWrite::refuseReadOnly( path, EditorContentWriteKind::Save ) );
    SW_EXPECT_TRUE( FileUtil::isReadOnlyFile( path ) );
    string text;
    SW_REQUIRE_TRUE( FileUtil::readTextFile( path, text ) );
    SW_EXPECT_EQUAL( text, string( "kept" ) );

    SW_REQUIRE_TRUE( FileUtil::setWritable( path, true ) ); // 케이스 폴더 정리가 Windows 에서 읽기 전용 파일을 지우지 못한다
    SW_EXPECT_FALSE( EditorContentWrite::refuseReadOnly( path, EditorContentWriteKind::Save ) );
}

/** @brief [EditorContentWriteTest] 폴더는 그 아래 모든 파일을 본다 — 깊은 곳의 읽기 전용 파일 하나가 폴더 전체를 막는다. 없는 자리(새로 만들 파일)는 막지 않는다 */
SW_TEST_CASE( EditorContentWriteTest, FolderIsRefusedByAReadOnlyFileUnderIt )
{
    const string folder     = test::makeScratchPath( "pack" );
    const string writable   = folder + "/a.material";
    const string readOnly   = folder + "/deep/b.material";
    const string missingNew = folder + "/new.material";
    SW_REQUIRE_TRUE( FileUtil::ensureParentDirectoryExists( readOnly ) );
    SW_REQUIRE_TRUE( FileUtil::writeTextFile( writable, "a" ) );
    SW_REQUIRE_TRUE( FileUtil::writeTextFile( readOnly, "b" ) );
    SW_EXPECT_FALSE( EditorContentWrite::findReadOnlyAtOrUnder( folder ).has_value() );

    SW_REQUIRE_TRUE( FileUtil::setWritable( readOnly, false ) );
    const optional<string> found = EditorContentWrite::findReadOnlyAtOrUnder( folder );
    SW_REQUIRE_TRUE( found.has_value() );
    SW_EXPECT_EQUAL( *found, FileUtil::normalizeSeparators( readOnly ) );
    SW_EXPECT_FALSE( EditorContentWrite::findReadOnlyAtOrUnder( writable ).has_value() );
    SW_EXPECT_FALSE( EditorContentWrite::findReadOnlyAtOrUnder( missingNew ).has_value() );

    SW_REQUIRE_TRUE( FileUtil::setWritable( readOnly, true ) );
}

/** @brief [EditorContentWriteTest] 삭제도 같은 확인을 지난다 — 읽기 전용 파일은 지우지 않고 false, 파일이 남는다(POSIX 는 폴더 권한만으로 지울 수 있어 잠금이 뚫렸다) */
SW_TEST_CASE( EditorContentWriteTest, DeleteRefusesAReadOnlyFile )
{
    SW_EXPECT_LOG( "EditorContentWrite.ReadOnly" );
    const string path = test::makeScratchPath( "locked.scene.xml" );
    SW_REQUIRE_TRUE( FileUtil::ensureParentDirectoryExists( path ) );
    SW_REQUIRE_TRUE( FileUtil::writeTextFile( path, "<Scene/>" ) );
    SW_REQUIRE_TRUE( FileUtil::setWritable( path, false ) );

    SW_EXPECT_FALSE( EditorAssetFileCommands::deleteAsset( path ) );
    SW_EXPECT_TRUE( FileUtil::exists( path ) );

    SW_REQUIRE_TRUE( FileUtil::setWritable( path, true ) );
    SW_EXPECT_TRUE( EditorAssetFileCommands::deleteAsset( path ) );
    SW_EXPECT_FALSE( FileUtil::exists( path ) );
}
