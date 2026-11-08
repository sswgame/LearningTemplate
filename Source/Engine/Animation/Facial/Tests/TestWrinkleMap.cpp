#include "pch.h"

#include "Core/Testing/TestFramework.h"

#include "Engine/Animation/Facial/WrinkleMap.h"

// WrinkleMapTest — 표정 가중치 → 영역 마스크 가중치: 출처 가중치 × 표정 가중치의 합, 강도, 같은 채널 합산 뒤 자르기, 모르는 표정 검사(nogpu).

namespace
{
    sw::WrinkleMap makeMap( float32 intensity )
    {
        sw::WrinkleRegionDef forehead;
        forehead._region = sw::hashed_string( "Forehead" );
        forehead._mask   = 0;
        forehead._listSource.push_back( sw::WrinkleSourceDef{ sw::hashed_string( "BrowUp" ), 1.0f } );
        sw::WrinkleRegionDef smile;
        smile._region = sw::hashed_string( "Smile" );
        smile._mask   = 5;
        smile._listSource.push_back( sw::WrinkleSourceDef{ sw::hashed_string( "Happy" ), 0.8f } );
        smile._listSource.push_back( sw::WrinkleSourceDef{ sw::hashed_string( "Squint" ), 0.5f } );
        return sw::WrinkleMap( { forehead, smile }, intensity );
    }
} // namespace

/**
 * @brief [WrinkleMapTest] 영역 가중치는 출처마다 가중치 × 표정 가중치의 합에 강도를 곱한 값이고, 마스크 B 채널(4..7)로 간다
 */
SW_TEST_CASE( WrinkleMapTest, SumsSourcesIntoMaskChannels )
{
    const sw::WrinkleMap    map         = makeMap( 1.0f );
    const sw::hashed_string arrName[]   = { sw::hashed_string( "Happy" ), sw::hashed_string( "BrowUp" ), sw::hashed_string( "Squint" ) };
    const float32           arrWeight[] = { 0.5f, 0.25f, 0.2f };
    sw::WrinkleWeights      weights;
    map.evaluate( sw::span<const sw::hashed_string>( arrName, 3 ), sw::span<const float32>( arrWeight, 3 ), weights );
    SW_EXPECT_NEAR( 0.25f, weights._maskA._x, 1e-6f );
    SW_EXPECT_NEAR( 0.5f, weights._maskB._y, 1e-6f ); // 0.8 × 0.5 + 0.5 × 0.2
    SW_EXPECT_NEAR( 0.0f, weights._maskA._y, 1e-6f );
}

/**
 * @brief [WrinkleMapTest] 강도가 곱해지고 1 에서 잘린다, 표정이 모두 0 이면 가중치도 0 이다
 */
SW_TEST_CASE( WrinkleMapTest, IntensityScalesAndSaturates )
{
    const sw::WrinkleMap    map       = makeMap( 3.0f );
    const sw::hashed_string arrName[] = { sw::hashed_string( "BrowUp" ) };
    const float32           arrFull[] = { 0.5f };
    const float32           arrNone[] = { 0.0f };
    sw::WrinkleWeights      weights;
    map.evaluate( sw::span<const sw::hashed_string>( arrName, 1 ), sw::span<const float32>( arrFull, 1 ), weights );
    SW_EXPECT_NEAR( 1.0f, weights._maskA._x, 1e-6f );
    map.evaluate( sw::span<const sw::hashed_string>( arrName, 1 ), sw::span<const float32>( arrNone, 1 ), weights );
    SW_EXPECT_TRUE( weights.isZero() );
}

/**
 * @brief [WrinkleMapTest] 리그에 없는 표정을 출처로 적으면 검사가 그 이름을 돌려준다
 */
SW_TEST_CASE( WrinkleMapTest, ReportsUnknownExpressions )
{
    const sw::WrinkleMap          map       = makeMap( 1.0f );
    const sw::hashed_string       arrName[] = { sw::hashed_string( "Happy" ), sw::hashed_string( "BrowUp" ) };
    sw::vector<sw::hashed_string> listMissing;
    SW_EXPECT_FALSE( map.checkExpressions( sw::span<const sw::hashed_string>( arrName, 2 ), &listMissing ) );
    SW_REQUIRE_EQUAL( size_t{ 1 }, listMissing.size() );
    SW_EXPECT_TRUE( listMissing[0] == sw::hashed_string( "Squint" ) );
}
