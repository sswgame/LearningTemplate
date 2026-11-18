// CharacterSurfaceState::clear — 풀 반환(외형 되돌리기)이 피 · 흙의 영역 값과 마스크를 모두 0 으로 돌리고, 바뀌었으면 번호를 올린다(nogpu).
#include "pch.h"

#include "Core/Testing/TestFramework.h"

#include "Engine/Character/Fit/SurfaceState.h"

using namespace sw;

namespace
{
    struct SurfaceStateClearTestUtil
    {
        /** @brief 피(마스크 있음)와 젖음 채널 표입니다. */
        static SurfaceChannelTable makeTable()
        {
            SurfaceChannelTable table;
            const bool          bRead = table.loadFromXMLText( R"(<SurfaceChannels>
                <Channel name="Wet" decayPerSecond="0"/>
                <Channel name="Blood" decayPerSecond="0" maskResolution="8"/>
            </SurfaceChannels>)",
                                                               "test.surfacechannels.xml" );
            SW_EXPECT_TRUE( bRead );
            return table;
        }
    };
} // namespace

/** @brief [SurfaceStateClearTest] 피를 올린 상태를 비우면 갓 만든 상태와 같은 값(영역 · 마스크 0)이고 번호가 오른다. 빈 상태를 비우면 번호가 그대로다 */
SW_TEST_CASE( SurfaceStateClearTest, ClearReturnsValuesAndMasksToZero )
{
    CharacterSurfaceState state;
    state.initialize( SurfaceStateClearTestUtil::makeTable(), 2, 1 );
    const uint64 revisionFresh = state.getRevision();
    state.clear();
    SW_EXPECT_EQUAL( revisionFresh, state.getRevision() );

    SW_REQUIRE_TRUE( state.setRegionValue( 1, "Blood", 1.0f ) );
    SW_REQUIRE_TRUE( state.setRegionValue( 0, "Wet", 0.5f ) );
    SW_REQUIRE_TRUE( state.paintHit( 0, 1, "Blood", float2{ 0.5f, 0.5f }, 0.2f, 1.0f ) );
    const uint64 revisionAfterHit = state.getRevision();

    state.clear();
    SW_EXPECT_NEAR( 0.0f, state.getRegionValue( 1, "Blood" ), 1.0e-6f );
    SW_EXPECT_NEAR( 0.0f, state.getRegionValue( 0, "Wet" ), 1.0e-6f );
    const SurfaceMask* pBloodMask = state.findMask( 0, "Blood" );
    SW_REQUIRE_NOT_NULL( pBloodMask );
    SW_EXPECT_NEAR( 0.0f, pBloodMask->sample( float2{ 0.5f, 0.5f } ), 1.0e-6f );
    SW_EXPECT_TRUE( state.getRevision() != revisionAfterHit );
    SW_EXPECT_EQUAL( 2u, state.getRegionCount() );
}
