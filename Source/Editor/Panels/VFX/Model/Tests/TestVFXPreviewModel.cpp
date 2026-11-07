// VFXPreviewModelTest — 미리보기 자동 갱신의 도장 비교(nogpu, 임시 파일).
#include "pch.h"

#include "Core/File/FileUtil.h"
#include "Core/Testing/TestFramework.h"

#include "Editor/Panels/VFX/Model/VFXPreviewModel.h"

using namespace sw;
using namespace sw::editor;

/** @brief [VFXPreviewModelTest] 처음 본 도장은 바뀐 것이고, 그대로면 아니며, 저장으로 크기가 바뀌면 다시 바뀐 것이다. 지운 파일은 다시 읽을 것이 없다 */
SW_TEST_CASE( VFXPreviewModelTest, FileStampDetectsASave )
{
    const string path = test::makeScratchPath( "fire.vfx.xml" );
    SW_REQUIRE_TRUE( FileUtil::writeTextFile( path, "<VFXEffectDef />" ) );
    FileStamp stamp{};
    SW_EXPECT_TRUE( VFXPreviewModel::updateFileStamp( path, stamp ) );
    SW_EXPECT_FALSE( VFXPreviewModel::updateFileStamp( path, stamp ) );

    SW_REQUIRE_TRUE( FileUtil::writeTextFile( path, "<VFXEffectDef _note=\"saved by the catalog editor\" />" ) );
    SW_EXPECT_TRUE( VFXPreviewModel::updateFileStamp( path, stamp ) );
    SW_EXPECT_FALSE( VFXPreviewModel::updateFileStamp( path, stamp ) );

    SW_EXPECT_FALSE( VFXPreviewModel::updateFileStamp( test::makeScratchPath( "missing.vfx.xml" ), stamp ) );
}
