// CameraDirectorTest — 디렉터의 후처리 덧칠(출처마다 하나, 가중치로 섞음, 0 이면 뺌). 지역의 화면 반응(TagPostProcessComponent)이 이 길을 쓴다(nogpu).
#include "pch.h"

#include "Core/Testing/TestFramework.h"

#include "Engine/Serialization/Format/Archive.h"

#include "GameFramework/Runtime/Character/Camera/CameraDirector.h"
#include "GameFramework/Runtime/Character/Camera/CameraPreset.h"

namespace sw
{
    /** @brief 안을 보는 테스트 창구입니다(`friend`). */
    struct CameraDirectorTestAccess
    {
        static uint32 getPostProcessOverlayCount( const CameraDirector& director ) { return static_cast<uint32>( director._listPostProcessOverlay.size() ); }
    };
} // namespace sw

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
    look._vignetteIntensity   = 0.8f;
    look._saturation          = 0.2f;
    const ComponentHandle gas = ComponentHandle::makeOwned( 7, 1 );
    director.setPostProcessOverlay( gas, look, 0.5f );
    SW_EXPECT_EQUAL( 1u, CameraDirectorTestAccess::getPostProcessOverlayCount( director ) );
    const CameraPose& half = director.step( 1.0f / 60.0f, target );
    SW_EXPECT_NEAR( 0.4f, half._postProcess._vignetteIntensity, 1.0e-5f );
    SW_EXPECT_NEAR( 0.6f, half._postProcess._saturation, 1.0e-5f ); // 1 → 0.2 의 절반

    director.setPostProcessOverlay( gas, look, 2.0f ); // 같은 출처는 바꾸고, 가중치는 1 로 자른다
    SW_EXPECT_EQUAL( 1u, CameraDirectorTestAccess::getPostProcessOverlayCount( director ) );
    SW_EXPECT_NEAR( 0.8f, director.step( 1.0f / 60.0f, target )._postProcess._vignetteIntensity, 1.0e-5f );

    // 한 오브젝트의 다른 컴포넌트가 프레임마다 가중치 0 을 적어도 이 덧칠은 남는다
    director.setPostProcessOverlay( ComponentHandle::makeOwned( 7, 2 ), look, 0.0f );
    SW_EXPECT_EQUAL( 1u, CameraDirectorTestAccess::getPostProcessOverlayCount( director ) );

    director.setPostProcessOverlay( gas, look, 0.0f );
    SW_EXPECT_EQUAL( 0u, CameraDirectorTestAccess::getPostProcessOverlayCount( director ) );
    SW_EXPECT_NEAR( 0.0f, director.step( 1.0f / 60.0f, target )._postProcess._vignetteIntensity, 1.0e-6f );
}

namespace CameraDirectorTestUtil
{
    /** @brief 블렌드로 넘어가는 프리셋 둘(기본 블렌드 0.5 초) — 엔진 예시 카탈로그(`engine/cameras/default.cameras.xml`)에서 옮겼다. */
    constexpr const utf8* kCatalogXML = R"(<CameraPresetCatalog>
    <DefaultBlend curve="SmoothStep" duration="0.5"/>
    <Preset id="orbit">
        <View mode="Orbit" pitch="24.999867" yaw="0" distance="12" offset="0,1.5,0"/>
        <Lens fieldOfViewY="45.000104" nearPlane="0.1" farPlane="500"/>
        <Damping positionTime="0.2" orientationTime="0.1"/>
    </Preset>
    <Preset id="follow">
        <View mode="Follow" pitch="15.000035" yaw="0" distance="6" offset="0,1.6,0"/>
        <Lens fieldOfViewY="60.00014" nearPlane="0.1" farPlane="500"/>
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

/** @brief [CameraCollisionProbeTest] `<Collision>` 에 막는 레이어(layers)가 없으면 그 프리셋을 넣지 않는 오류다 — 코드가 레이어 이름을 짐작하면 없는 이름에서 암 충돌이 조용히 꺼진다(감사 G-11) */
SW_TEST_CASE( CameraCollisionProbeTest, MissingLayersIsAnError )
{
    SW_EXPECT_LOG( "CameraPreset.CollisionNeedsLayers" );
    SW_EXPECT_LOG( "CameraPreset.PresetInvalidFieldsSkipped" );
    CameraPresetCatalog catalog;
    (void)catalog.loadFromXMLText( R"(<CameraPresetCatalog>
    <Preset id="bare">
        <View mode="Orbit" pitch="22.918312" yaw="0" distance="8" offset="0,1,0"/>
        <Collision radius="0.25" minDistance="0.4" recoverTime="0.3"/>
    </Preset>
    <Preset id="layered">
        <View mode="Orbit" pitch="22.918312" yaw="0" distance="8" offset="0,1,0"/>
        <Collision radius="0.25" minDistance="0.4" recoverTime="0.3" layers="Default,Static"/>
    </Preset>
</CameraPresetCatalog>)",
                                   "test.cameras.xml" );
    SW_EXPECT_NULL( catalog.find( hashed_string( "bare" ) ) );
    const CameraPresetDef* pLayered = catalog.find( hashed_string( "layered" ) );
    SW_REQUIRE_NOT_NULL( pLayered );
    SW_EXPECT_EQUAL( size_t( 2 ), pLayered->_collision._listLayer.size() );
}

namespace CameraDirectorTestUtil
{
    /** @brief 선택 프리셋 둘과 조준 · 실내 상황(실내가 우선순위가 높고 `*` 줄을 가진다)입니다. */
    constexpr const utf8* kContextCatalogXML = R"(<CameraPresetCatalog>
    <DefaultBlend curve="SmoothStep" duration="0.5"/>
    <Context name="Aim" priority="10" action="Aim">
        <Map from="orbit" to="orbit_aim"/>
    </Context>
    <Preset id="orbit"><View mode="Orbit" pitch="24.999867" yaw="0" distance="12" offset="0,1.5,0"/></Preset>
    <Preset id="follow"><View mode="Follow" pitch="15.000035" yaw="0" distance="6" offset="0,1.6,0"/></Preset>
    <Preset id="orbit_aim"><View mode="Orbit" pitch="10.000004" yaw="0" distance="4" offset="0,1.5,0"/></Preset>
    <Preset id="indoor"><View mode="Follow" pitch="5.000002" yaw="0" distance="3" offset="0,1.6,0"/></Preset>
    <Context name="Indoor" priority="20">
        <Map from="*" to="indoor"/>
    </Context>
</CameraPresetCatalog>)";
} // namespace CameraDirectorTestUtil

/** @brief [CameraContextTest] 켜진 상황은 선택 프리셋의 줄이 있을 때만 바꾸고, 여럿이면 우선순위가 큰 쪽이 이기며, 상황 프리셋은 순환 대상이 아니다 */
SW_TEST_CASE( CameraContextTest, ResolvesByPriorityAndSelectedPreset )
{
    CameraPresetCatalog catalog;
    SW_REQUIRE_TRUE( catalog.loadFromXMLText( CameraDirectorTestUtil::kContextCatalogXML, "test.cameras.xml" ) ); // 상황이 프리셋보다 앞에 적혀도 된다
    const hashed_string orbit( "orbit" );
    const hashed_string follow( "follow" );
    const hashed_string aim( "Aim" );
    const hashed_string indoor( "Indoor" );

    SW_EXPECT_TRUE( orbit == catalog.resolveContextPreset( orbit, {} ) );
    SW_EXPECT_TRUE( hashed_string( "orbit_aim" ) == catalog.resolveContextPreset( orbit, { aim } ) );
    SW_EXPECT_TRUE( follow == catalog.resolveContextPreset( follow, { aim } ) ); // follow 에는 조준 줄이 없다
    SW_EXPECT_TRUE( hashed_string( "indoor" ) == catalog.resolveContextPreset( orbit, { aim, indoor } ) );
    SW_EXPECT_TRUE( hashed_string( "indoor" ) == catalog.resolveContextPreset( follow, { indoor } ) );

    SW_EXPECT_TRUE( catalog.isContextPreset( hashed_string( "orbit_aim" ) ) );
    SW_EXPECT_FALSE( catalog.isContextPreset( orbit ) );
    SW_REQUIRE_NOT_NULL( catalog.findContext( aim ) );
    SW_EXPECT_TRUE( hashed_string( "Aim" ) == catalog.findContext( aim )->_action );
    SW_EXPECT_NULL( catalog.findContext( hashed_string( "Vehicle" ) ) );
}

/** @brief [CameraContextTest] 없는 프리셋을 부르는 줄 · 줄 없는 상황 · 두 번 적은 상황은 오류이고 넣지 않는다 — 켜는 순간 조용히 아무 일도 없게 두지 않는다 */
SW_TEST_CASE( CameraContextTest, UnknownPresetAndEmptyContextAreErrors )
{
    SW_EXPECT_LOG( "CameraPreset.ContextUnknownPreset" );
    SW_EXPECT_LOG( "CameraPreset.ContextNeedsMaps" );
    SW_EXPECT_LOG( "CameraPreset.ContextDefinedTwice" );
    CameraPresetCatalog catalog;
    (void)catalog.loadFromXMLText( R"(<CameraPresetCatalog>
    <Preset id="orbit"><View mode="Orbit" pitch="24.999867" yaw="0" distance="12" offset="0,1.5,0"/></Preset>
    <Context name="Aim" priority="10">
        <Map from="orbit" to="missing"/>
        <Map from="orbit" to="orbit"/>
    </Context>
    <Context name="Aim" priority="5"><Map from="orbit" to="orbit"/></Context>
    <Context name="Empty" priority="1"/>
</CameraPresetCatalog>)",
                                   "test.cameras.xml" );
    const CameraContextDef* pAim = catalog.findContext( hashed_string( "Aim" ) );
    SW_REQUIRE_NOT_NULL( pAim );
    SW_EXPECT_EQUAL( 10, pAim->_priority );
    SW_EXPECT_EQUAL( size_t( 1 ), pAim->_listMap.size() );
    SW_EXPECT_NULL( catalog.findContext( hashed_string( "Empty" ) ) );
}
