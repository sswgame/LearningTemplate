// VFXPreviewModelTest — 미리보기 자동 갱신의 내용 해시 비교(nogpu, 임시 파일).
#include "pch.h"

#include "Core/File/FileUtil.h"
#include "Core/Testing/TestFramework.h"

#include "Editor/Panels/VFX/Model/VFXPreviewModel.h"

using namespace sw;
using namespace sw::editor;

/**
 * @brief [VFXPreviewModelTest] 처음 본 내용은 바뀐 것이고, 그대로면 아니며, 저장으로 내용이 바뀌면 다시 바뀐 것이다 — 같은 크기로 곧바로 다시 쓴 저장도(도장이 같을 수 있다).
 *        같은 내용으로 다시 쓰면 바뀐 것이 아니다. 지운 파일은 다시 읽을 것이 없다
 */
SW_TEST_CASE( VFXPreviewModelTest, ContentHashDetectsASave )
{
    const string path = test::makeScratchPath( "fire.vfx.xml" );
    SW_REQUIRE_TRUE( FileUtil::writeTextFile( path, "<VFXEffectDef />" ) );
    uint64 hash = 0;
    SW_EXPECT_TRUE( VFXPreviewModel::updateContentHash( path, hash ) );
    SW_EXPECT_FALSE( VFXPreviewModel::updateContentHash( path, hash ) );

    SW_REQUIRE_TRUE( FileUtil::writeTextFile( path, "<VFXEffectDef _note=\"saved by the catalog editor\" />" ) );
    SW_EXPECT_TRUE( VFXPreviewModel::updateContentHash( path, hash ) );
    SW_EXPECT_FALSE( VFXPreviewModel::updateContentHash( path, hash ) );

    // 같은 크기 — 같은 틱이면 (크기, 쓰기 시각) 도장이 같다.
    SW_REQUIRE_TRUE( FileUtil::writeTextFile( path, "<VFXEffectDef _note=\"saved by the catalog editoR\" />" ) );
    SW_EXPECT_TRUE( VFXPreviewModel::updateContentHash( path, hash ) );
    SW_REQUIRE_TRUE( FileUtil::writeTextFile( path, "<VFXEffectDef _note=\"saved by the catalog editoR\" />" ) );
    SW_EXPECT_FALSE( VFXPreviewModel::updateContentHash( path, hash ) );

    SW_EXPECT_FALSE( VFXPreviewModel::updateContentHash( test::makeScratchPath( "missing.vfx.xml" ), hash ) );
}
