// CameraPresetViewModelTest: 프리셋 파일을 읽어 첫 프리셋을 고르고, 뷰포트 자리는 바뀐 뒤 한 번만 내며, 대상 요가 따르는 프리셋의 자리를 돌린다(엔진 기본 프리셋 파일 — nogpu).
#include "pch.h"

#include "Core/Testing/TestFramework.h"

#include "GameFramework/Plugin/Editor/CameraPresets/Model/CameraPresetViewModel.h"

using namespace sw;
using namespace sw::editor;

namespace
{
    struct CameraPresetViewModelTestUtil
    {
        static constexpr const utf8* kPresetAsset = "engine/cameras/default.cameras.xml";

        /** @brief id 가 @p id 인 프리셋의 차례입니다(없으면 프리셋 수). */
        static size_t findPreset( const CameraPresetViewModel& model, const utf8* pID )
        {
            const CameraPresetCatalog& catalog = model.getCatalog();
            size_t                     index   = 0;
            while ( index < catalog.getCount() && catalog.getAt( index )._id != hashed_string( pID ) )
                ++index;
            return index;
        }
    };
} // namespace

/** @brief [CameraPresetViewModelTest] 읽기 전에는 고른 프리셋도 자리도 없고, 범위 밖 고르기는 무시한다 */
SW_TEST_CASE( CameraPresetViewModelTest, EmptyModelHasNoPose )
{
    CameraPresetViewModel    model;
    CameraPresetViewportPose pose;
    SW_EXPECT_EQUAL( -1, model.getSelectedIndex() );
    SW_EXPECT_NULL( model.findSelectedPreset() );
    model.selectPreset( 0 );
    model.markViewportDirty();
    SW_EXPECT_EQUAL( -1, model.getSelectedIndex() );
    SW_EXPECT_FALSE( model.takeViewportPose( float2{ 1280.0f, 720.0f }, pose ) );
}

/** @brief [CameraPresetViewModelTest] 읽으면 첫 프리셋을 고르고 자리를 한 번 낸다 — 고르기 · 더러움 표시 뒤에만 다시 낸다 */
SW_TEST_CASE( CameraPresetViewModelTest, PoseIsTakenOncePerChange )
{
    using Util = CameraPresetViewModelTestUtil;
    CameraPresetViewModel model;
    SW_REQUIRE_TRUE( model.loadAsset( Util::kPresetAsset ) == ToolAssetLoadResult::Loaded );
    SW_REQUIRE_TRUE( model.getCatalog().getCount() >= 2 );
    SW_EXPECT_EQUAL( 0, model.getSelectedIndex() );
    const float2             imageSize{ 1280.0f, 720.0f };
    CameraPresetViewportPose pose;
    SW_EXPECT_TRUE( model.takeViewportPose( imageSize, pose ) );
    SW_EXPECT_TRUE( pose._fieldOfViewY > 0.0f );
    SW_EXPECT_FALSE( model.takeViewportPose( imageSize, pose ) );

    model.selectPreset( model.getCatalog().getCount() ); // 범위 밖
    SW_EXPECT_FALSE( model.takeViewportPose( imageSize, pose ) );
    model.selectPreset( 1 );
    SW_EXPECT_EQUAL( 1, model.getSelectedIndex() );
    SW_EXPECT_TRUE( model.takeViewportPose( imageSize, pose ) );
    model.markViewportDirty();
    SW_EXPECT_TRUE( model.takeViewportPose( imageSize, pose ) );
}

/** @brief [CameraPresetViewModelTest] 따르는 프리셋(follow)은 대상 요를 반 바퀴 돌리면 카메라가 대상 반대쪽으로 간다 */
SW_TEST_CASE( CameraPresetViewModelTest, TargetYawTurnsTheFollowPose )
{
    using Util = CameraPresetViewModelTestUtil;
    CameraPresetViewModel model;
    SW_REQUIRE_TRUE( model.loadAsset( Util::kPresetAsset ) == ToolAssetLoadResult::Loaded );
    const size_t follow = Util::findPreset( model, "follow" );
    SW_REQUIRE_TRUE( follow < model.getCatalog().getCount() );
    const float2             imageSize{ 1280.0f, 720.0f };
    CameraPresetViewportPose forward;
    CameraPresetViewportPose backward;
    model.selectPreset( follow );
    SW_REQUIRE_TRUE( model.takeViewportPose( imageSize, forward ) );
    model.editTargetYawDegrees() = 180.0f;
    model.markViewportDirty();
    SW_REQUIRE_TRUE( model.takeViewportPose( imageSize, backward ) );
    SW_EXPECT_NEAR( -forward._position._z, backward._position._z, 1.0e-2f );
    SW_EXPECT_NEAR( forward._position._y, backward._position._y, 1.0e-2f );
}
