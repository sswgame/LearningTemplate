// EditorAssetDeleteTest — 에셋 삭제도 저장과 같은 읽기 전용 확인을 지난다(감사 E-11, nogpu).
#include "pch.h"

#include "Core/File/FileUtil.h"
#include "Core/TestFramework/TestFramework.h"

#include "Editor/Common/Commands/Asset/EditorAssetFileCommands.h"

using namespace sw;
using namespace sw::editor;

/** @brief [EditorAssetDeleteTest] 삭제도 같은 확인을 지난다 — 읽기 전용 파일은 지우지 않고 false, 파일이 남는다(POSIX 는 폴더 권한만으로 지울 수 있어 잠금이 뚫렸다) */
SW_TEST_CASE( EditorAssetDeleteTest, DeleteRefusesAReadOnlyFile )
{
    SW_EXPECT_LOG( "AssetWriter.ReadOnly" );
    const AbsolutePath path = test::makeScratchPath( "locked.scene.xml" );
    SW_REQUIRE_TRUE( FileUtil::writeTextFile( path, "<Scene/>" ) );
    SW_REQUIRE_TRUE( FileUtil::setWritable( path, false ) );

    SW_EXPECT_FALSE( EditorAssetFileCommands::deleteAsset( path ) );
    SW_EXPECT_TRUE( FileUtil::exists( path ) );

    SW_REQUIRE_TRUE( FileUtil::setWritable( path, true ) );
    SW_EXPECT_TRUE( EditorAssetFileCommands::deleteAsset( path ) );
    SW_EXPECT_FALSE( FileUtil::exists( path ) );
}
