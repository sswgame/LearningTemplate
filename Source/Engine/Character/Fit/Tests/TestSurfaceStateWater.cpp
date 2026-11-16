// CharacterSurfaceState::applyWater — 물은 씻김 빠르기가 있는 채널(흙 · 피)만 잠긴 비율만큼 더 내리고, 젖음 · 상처 · 찢김은 그대로 둔다(nogpu).
#include "pch.h"

#include "Core/Testing/TestFramework.h"

#include "Engine/Character/Fit/SurfaceState.h"

using namespace sw;

namespace
{
    struct SurfaceStateWaterTestUtil
    {
        /** @brief 엔진 기본 표와 같은 모양의 채널 표(젖음 · 흙 · 상처)입니다. 흙만 씻긴다. */
        static SurfaceChannelTable makeTable()
        {
            SurfaceChannelTable table;
            const bool          bRead = table.loadFromXMLText( R"(<SurfaceChannels>
                <Channel name="Wet" decayPerSecond="0"/>
                <Channel name="Dirt" decayPerSecond="0" washPerSecond="0.5" maskResolution="8"/>
                <Channel name="Wound" maskResolution="8"/>
            </SurfaceChannels>)",
                                                               "test.surfacechannels.xml" );
            SW_EXPECT_TRUE( bRead );
            return table;
        }
    };
} // namespace

/**
 * @brief [SurfaceStateWaterTest] 반쯤 잠기면 흙이 씻김 빠르기 × 시간 × 0.5 만큼 빠지고(영역 값과 마스크 모두), 젖음과 상처는 그대로다
 */
SW_TEST_CASE( SurfaceStateWaterTest, WaterWashesOnlyWashableChannels )
{
    CharacterSurfaceState state;
    state.initialize( SurfaceStateWaterTestUtil::makeTable(), 2, 1 );
    SW_REQUIRE_TRUE( state.setRegionValue( 0, "Wet", 0.4f ) );
    SW_REQUIRE_TRUE( state.setRegionValue( 1, "Dirt", 0.8f ) );
    SW_REQUIRE_TRUE( state.setRegionValue( 0, "Wound", 1.0f ) );
    SW_REQUIRE_TRUE( state.paintHit( 0, 1, "Dirt", float2{ 0.5f, 0.5f }, 0.2f, 0.8f ) );
    SW_REQUIRE_TRUE( state.paintHit( 0, 0, "Wound", float2{ 0.5f, 0.5f }, 0.2f, 1.0f ) );
    const float32 dirtBefore  = state.getRegionValue( 1, "Dirt" );
    const uint64  revisionOld = state.getRevision();

    state.applyWater( 1.0f, 0.5f );

    SW_EXPECT_NEAR( dirtBefore - 0.25f, state.getRegionValue( 1, "Dirt" ), 1.0e-5f );
    SW_EXPECT_NEAR( 0.4f, state.getRegionValue( 0, "Wet" ), 1.0e-6f );
    SW_EXPECT_NEAR( 1.0f, state.getRegionValue( 0, "Wound" ), 1.0e-6f );
    const SurfaceMask* pDirtMask  = state.findMask( 0, "Dirt" );
    const SurfaceMask* pWoundMask = state.findMask( 0, "Wound" );
    SW_REQUIRE_NOT_NULL( pDirtMask );
    SW_REQUIRE_NOT_NULL( pWoundMask );
    SW_EXPECT_NEAR( 0.55f, pDirtMask->sample( float2{ 0.5f, 0.5f } ), 1.0e-5f );
    SW_EXPECT_NEAR( 1.0f, pWoundMask->sample( float2{ 0.5f, 0.5f } ), 1.0e-6f );
    SW_EXPECT_TRUE( state.getRevision() != revisionOld );
    SW_EXPECT_NEAR( 0.4f, state.computeMaxValue( "Wet" ), 1.0e-6f );

    // 물 밖(잠긴 비율 0)은 씻지 않는다 — 번호도 오르지 않는다(머티리얼을 다시 올리지 않는다).
    const uint64 revisionDry = state.getRevision();
    state.applyWater( 1.0f, 0.0f );
    SW_EXPECT_EQUAL( revisionDry, state.getRevision() );
}
