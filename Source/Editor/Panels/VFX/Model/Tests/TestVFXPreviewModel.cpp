// VFXPreviewModelTest — 미리보기 자동 갱신의 내용 해시 비교(nogpu, 임시 파일).
#include "pch.h"

#include "Core/File/FileUtil.h"
#include "Core/Math/MathUtil.h"
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

    SW_EXPECT_LOG( "File.NotFound" ); // 지운 파일의 해시 읽기는 읽기 실패로 알린다(ContentHash)
    SW_EXPECT_FALSE( VFXPreviewModel::updateContentHash( test::makeScratchPath( "missing.vfx.xml" ), hash ) );
}

/** @brief [VFXPreviewModelTest] 격리는 토글이다 — 한 방출기만 켜고, 같은 방출기를 다시 누르거나 음수면 모두 켠다 */
SW_TEST_CASE( VFXPreviewModelTest, SoloTogglesOneEmitter )
{
    VFXPreviewModel model;
    SW_EXPECT_EQUAL( VFXPreviewModel::kNoSolo, model.getSoloEmitter() );
    SW_EXPECT_TRUE( VFXPreviewModel::computeEmitterEnabled( model.getSoloEmitter(), 0 ) );
    SW_EXPECT_TRUE( VFXPreviewModel::computeEmitterEnabled( model.getSoloEmitter(), 3 ) );

    model.toggleSolo( 1 );
    SW_EXPECT_EQUAL( 1, model.getSoloEmitter() );
    SW_EXPECT_FALSE( VFXPreviewModel::computeEmitterEnabled( model.getSoloEmitter(), 0 ) );
    SW_EXPECT_TRUE( VFXPreviewModel::computeEmitterEnabled( model.getSoloEmitter(), 1 ) );

    model.toggleSolo( 0 ); // 다른 방출기 — 그 방출기로 옮긴다
    SW_EXPECT_EQUAL( 0, model.getSoloEmitter() );
    model.toggleSolo( 0 ); // 같은 방출기 — 푼다
    SW_EXPECT_EQUAL( VFXPreviewModel::kNoSolo, model.getSoloEmitter() );
    model.toggleSolo( 2 );
    model.toggleSolo( -5 );
    SW_EXPECT_EQUAL( VFXPreviewModel::kNoSolo, model.getSoloEmitter() );
}

/** @brief [VFXPreviewModelTest] 캐릭터 타깃은 바닥 위 반지름 kTargetOrbitRadius 의 원을 돈다(트레일이 보이게 움직인다) */
SW_TEST_CASE( VFXPreviewModelTest, TargetOrbitStaysOnTheCircle )
{
    const float3 start = VFXPreviewModel::computeOrbitPosition( 0.0f );
    SW_EXPECT_NEAR( VFXPreviewModel::kTargetOrbitRadius, start._x, 1e-5f );
    SW_EXPECT_NEAR( 0.0f, start._z, 1e-5f );
    const float3 quarter = VFXPreviewModel::computeOrbitPosition( MathUtil::kHalfPi );
    SW_EXPECT_NEAR( 0.0f, quarter._x, 1e-4f );
    SW_EXPECT_NEAR( VFXPreviewModel::kTargetOrbitRadius, quarter._z, 1e-4f );
    for ( float32 angle = 0.0f; angle < MathUtil::kTwoPi; angle += 0.7f )
    {
        const float3 point = VFXPreviewModel::computeOrbitPosition( angle );
        SW_EXPECT_NEAR( 0.0f, point._y, 1e-6f );
        SW_EXPECT_NEAR( VFXPreviewModel::kTargetOrbitRadius, MathUtil::sqrt( point._x * point._x + point._z * point._z ), 1e-4f );
    }
}
