// VFXPreviewViewModelTest — 미리보기 자동 갱신의 내용 해시 비교(nogpu, 임시 파일).
#include "pch.h"

#include "Core/File/FileUtil.h"
#include "Core/Math/MathUtil.h"
#include "Core/Testing/TestFramework.h"

#include "Editor/Panels/VFX/Model/VFXPreviewViewModel.h"

using namespace sw;
using namespace sw::editor;

/**
 * @brief [VFXPreviewViewModelTest] 처음 본 내용은 바뀐 것이고, 그대로면 아니며, 저장으로 내용이 바뀌면 다시 바뀐 것이다 — 같은 크기로 곧바로 다시 쓴 저장도(도장이 같을 수 있다).
 *        같은 내용으로 다시 쓰면 바뀐 것이 아니다. 지운 파일은 다시 읽을 것이 없다
 */
SW_TEST_CASE( VFXPreviewViewModelTest, ContentHashDetectsASave )
{
    const string path = test::makeScratchPath( "fire.vfx.xml" );
    SW_REQUIRE_TRUE( FileUtil::writeTextFile( path, "<VFXEffectDef />" ) );
    uint64 hash = 0;
    SW_EXPECT_TRUE( VFXPreviewViewModel::updateContentHash( path, hash ) );
    SW_EXPECT_FALSE( VFXPreviewViewModel::updateContentHash( path, hash ) );

    SW_REQUIRE_TRUE( FileUtil::writeTextFile( path, "<VFXEffectDef _note=\"saved by the catalog editor\" />" ) );
    SW_EXPECT_TRUE( VFXPreviewViewModel::updateContentHash( path, hash ) );
    SW_EXPECT_FALSE( VFXPreviewViewModel::updateContentHash( path, hash ) );

    // 같은 크기 — 같은 틱이면 (크기, 쓰기 시각) 도장이 같다.
    SW_REQUIRE_TRUE( FileUtil::writeTextFile( path, "<VFXEffectDef _note=\"saved by the catalog editoR\" />" ) );
    SW_EXPECT_TRUE( VFXPreviewViewModel::updateContentHash( path, hash ) );
    SW_REQUIRE_TRUE( FileUtil::writeTextFile( path, "<VFXEffectDef _note=\"saved by the catalog editoR\" />" ) );
    SW_EXPECT_FALSE( VFXPreviewViewModel::updateContentHash( path, hash ) );

    // 없는 파일의 해시는 0(못 읽음)이다 — 스트림 해시(ContentHash::computeFileStream)는 크기를 조용히 물어 로그 없이 NotFound 값을 돌려준다.
    SW_EXPECT_FALSE( VFXPreviewViewModel::updateContentHash( test::makeScratchPath( "missing.vfx.xml" ), hash ) );
}

/** @brief [VFXPreviewViewModelTest] 격리는 토글이다 — 한 방출기만 켜고, 같은 방출기를 다시 누르거나 음수면 모두 켠다 */
SW_TEST_CASE( VFXPreviewViewModelTest, SoloTogglesOneEmitter )
{
    VFXPreviewViewModel model;
    SW_EXPECT_EQUAL( VFXPreviewViewModel::kNoSolo, model.getSoloEmitter() );
    SW_EXPECT_TRUE( VFXPreviewViewModel::computeEmitterEnabled( model.getSoloEmitter(), 0 ) );
    SW_EXPECT_TRUE( VFXPreviewViewModel::computeEmitterEnabled( model.getSoloEmitter(), 3 ) );

    model.toggleSolo( 1 );
    SW_EXPECT_EQUAL( 1, model.getSoloEmitter() );
    SW_EXPECT_FALSE( VFXPreviewViewModel::computeEmitterEnabled( model.getSoloEmitter(), 0 ) );
    SW_EXPECT_TRUE( VFXPreviewViewModel::computeEmitterEnabled( model.getSoloEmitter(), 1 ) );

    model.toggleSolo( 0 ); // 다른 방출기 — 그 방출기로 옮긴다
    SW_EXPECT_EQUAL( 0, model.getSoloEmitter() );
    model.toggleSolo( 0 ); // 같은 방출기 — 푼다
    SW_EXPECT_EQUAL( VFXPreviewViewModel::kNoSolo, model.getSoloEmitter() );
    model.toggleSolo( 2 );
    model.toggleSolo( -5 );
    SW_EXPECT_EQUAL( VFXPreviewViewModel::kNoSolo, model.getSoloEmitter() );
}

/** @brief [VFXPreviewViewModelTest] 캐릭터 타깃은 바닥 위 반지름 kTargetOrbitRadius 의 원을 돈다(트레일이 보이게 움직인다) */
SW_TEST_CASE( VFXPreviewViewModelTest, TargetOrbitStaysOnTheCircle )
{
    const float3 start = VFXPreviewViewModel::computeOrbitPosition( 0.0f );
    SW_EXPECT_NEAR( VFXPreviewViewModel::kTargetOrbitRadius, start._x, 1e-5f );
    SW_EXPECT_NEAR( 0.0f, start._z, 1e-5f );
    const float3 quarter = VFXPreviewViewModel::computeOrbitPosition( MathUtil::kHalfPi );
    SW_EXPECT_NEAR( 0.0f, quarter._x, 1e-4f );
    SW_EXPECT_NEAR( VFXPreviewViewModel::kTargetOrbitRadius, quarter._z, 1e-4f );
    for ( float32 angle = 0.0f; angle < MathUtil::kTwoPi; angle += 0.7f )
    {
        const float3 point = VFXPreviewViewModel::computeOrbitPosition( angle );
        SW_EXPECT_NEAR( 0.0f, point._y, 1e-6f );
        SW_EXPECT_NEAR( VFXPreviewViewModel::kTargetOrbitRadius, MathUtil::sqrt( point._x * point._x + point._z * point._z ), 1e-4f );
    }
}
