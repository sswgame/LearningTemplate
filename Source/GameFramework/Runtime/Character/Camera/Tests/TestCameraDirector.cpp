// CameraDirectorTest — 디렉터의 후처리 덧칠(출처마다 하나, 가중치로 섞음, 0 이면 뺌). 지역의 화면 반응(ScreenReactionComponent)이 이 길을 쓴다(nogpu).
#include "pch.h"

#include "Core/Testing/TestFramework.h"

#include "GameFramework/Runtime/Character/Camera/CameraDirector.h"

using namespace sw;

/** @brief [CameraDirectorTest] 후처리 덧칠: 가중치만큼 섞인 포즈의 후처리 위에 얹고, 같은 출처는 바꾸며, 가중치 0 이면 빠져 프리셋 값으로 돌아온다 */
SW_TEST_CASE( CameraDirectorTest, PostProcessOverlayBlendsByWeight )
{
    CameraDirector director;
    BlendCurveDef  cut;
    cut._curve = BlendCurve::Cut;
    director.activatePreset( CameraPresetDef{}, cut );
    const CameraTarget target{};
    SW_EXPECT_NEAR( 0.0f, director.step( 1.0f / 60.0f, target )._postProcess._vignetteIntensity, 1.0e-6f ); // 프리셋은 중립

    CameraPostProcessValues look;
    look._vignetteIntensity    = 0.8f;
    look._saturation           = 0.2f;
    const GameObjectHandle gas = GameObjectHandle::make( 7 );
    director.setPostProcessOverlay( gas, look, 0.5f );
    SW_EXPECT_EQUAL( 1u, director.getPostProcessOverlayCount() );
    const CameraPose& half = director.step( 1.0f / 60.0f, target );
    SW_EXPECT_NEAR( 0.4f, half._postProcess._vignetteIntensity, 1.0e-5f );
    SW_EXPECT_NEAR( 0.6f, half._postProcess._saturation, 1.0e-5f ); // 1 → 0.2 의 절반

    director.setPostProcessOverlay( gas, look, 2.0f ); // 같은 출처는 바꾸고, 가중치는 1 로 자른다
    SW_EXPECT_EQUAL( 1u, director.getPostProcessOverlayCount() );
    SW_EXPECT_NEAR( 0.8f, director.step( 1.0f / 60.0f, target )._postProcess._vignetteIntensity, 1.0e-5f );

    director.setPostProcessOverlay( gas, look, 0.0f );
    SW_EXPECT_EQUAL( 0u, director.getPostProcessOverlayCount() );
    SW_EXPECT_NEAR( 0.0f, director.step( 1.0f / 60.0f, target )._postProcess._vignetteIntensity, 1.0e-6f );
}
