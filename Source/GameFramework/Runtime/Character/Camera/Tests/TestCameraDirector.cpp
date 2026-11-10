// CameraDirectorTest — 디렉터의 후처리 덧칠(출처마다 하나, 가중치로 섞음, 0 이면 뺌). 지역의 화면 반응(ScreenReactionComponent)이 이 길을 쓴다(nogpu).
#include "pch.h"

#include "Core/Testing/TestFramework.h"

#include "Engine/Serialization/Format/Archive.h"

#include "GameFramework/Runtime/Character/Camera/CameraDirector.h"
#include "GameFramework/Runtime/Character/Camera/CameraPreset.h"

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

    PostProcessValues look;
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

namespace CameraDirectorTestUtil
{
    /** @brief 블렌드로 넘어가는 프리셋 둘(기본 블렌드 0.5 초) — 엔진 예시 카탈로그(`engine/cameras/default.cameras.xml`)에서 옮겼다. */
    constexpr const utf8* kCatalogXML = R"(<CameraPresetCatalog>
    <DefaultBlend curve="SmoothStep" duration="0.5"/>
    <Preset id="orbit">
        <View mode="Orbit" pitch="0.43633" yaw="0" distance="12" offset="0,1.5,0"/>
        <Lens fieldOfViewY="0.7854" nearPlane="0.1" farPlane="500"/>
        <Damping positionTime="0.2" orientationTime="0.1"/>
    </Preset>
    <Preset id="follow">
        <View mode="Follow" pitch="0.2618" yaw="0" distance="6" offset="0,1.6,0"/>
        <Lens fieldOfViewY="1.0472" nearPlane="0.1" farPlane="500"/>
        <Damping positionTime="0.25" orientationTime="0.15"/>
    </Preset>
</CameraPresetCatalog>)";
    constexpr float32     kStep       = 1.0f / 60.0f;
} // namespace CameraDirectorTestUtil

/**
 * @brief [CameraDirectorTest] 상태 왕복: 블렌드 중인 스택(층 · 진행 · 감쇠 포즈)을 쓰고 읽으면 다음 프레임 포즈가 같고, 잘린 바이트는 거절해 지금 상태를 둔다
 * @details 카메라 디렉터 컴포넌트의 세이브 · 핫 리로드 계약(`CameraDirectorComponent::serializeState`)이 이 본문(`CameraDirector::serializeState`)을 부른다.
 */
SW_TEST_CASE( CameraDirectorTest, StateRoundTripKeepsTheBlendStack )
{
    namespace Util = CameraDirectorTestUtil;
    CameraPresetCatalog catalog;
    SW_REQUIRE_TRUE( catalog.loadFromXMLText( Util::kCatalogXML, "test.cameras.xml" ) );

    CameraTarget target{};
    target._focus = float3{ 1.0f, 0.0f, 2.0f };
    CameraDirector director;
    SW_REQUIRE_TRUE( director.activatePreset( catalog, hashed_string( "orbit" ) ) );
    for ( uint32 frame = 0; frame < 10; ++frame )
    {
        (void)director.step( Util::kStep, target );
    }
    SW_REQUIRE_TRUE( director.activatePreset( catalog, hashed_string( "follow" ) ) );
    for ( uint32 frame = 0; frame < 6; ++frame )
    {
        (void)director.step( Util::kStep, target );
    }
    SW_REQUIRE_TRUE( director.isBlending() ); // 0.1 초 — 0.5 초 블렌드의 중간이다

    Archive written;
    director.writeState( written );
    CameraDirector restored;
    Archive        reading( written.getData(), written.getSize() );
    SW_REQUIRE_TRUE( restored.readState( reading, catalog ) );
    SW_EXPECT_TRUE( restored.getActivePresetID() == director.getActivePresetID() );
    SW_EXPECT_EQUAL( director.getLayerCount(), restored.getLayerCount() );

    const CameraPose expected = director.step( Util::kStep, target );
    const CameraPose actual   = restored.step( Util::kStep, target );
    SW_EXPECT_NEAR( expected._position._x, actual._position._x, 1.0e-5f );
    SW_EXPECT_NEAR( expected._position._y, actual._position._y, 1.0e-5f );
    SW_EXPECT_NEAR( expected._position._z, actual._position._z, 1.0e-5f );

    CameraDirector untouched;
    SW_REQUIRE_TRUE( untouched.activatePreset( catalog, hashed_string( "orbit" ) ) );
    Archive truncated( written.getData(), written.getSize() - 1 );
    SW_EXPECT_FALSE( untouched.readState( truncated, catalog ) );
    SW_EXPECT_TRUE( untouched.getActivePresetID() == hashed_string( "orbit" ) );
    SW_EXPECT_EQUAL( 1u, untouched.getLayerCount() );
}
