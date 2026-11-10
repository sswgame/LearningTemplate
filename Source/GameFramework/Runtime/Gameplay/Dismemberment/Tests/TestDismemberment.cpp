#include "pch.h"

#include "Core/Testing/TestFramework.h"

#include "GameFramework/Runtime/Gameplay/Dismemberment/Dismemberment.h"

using namespace sw;

namespace
{
    struct DismembermentTestUtil
    {
        /** @brief 띠 하나입니다 — 정점 0 ~ 3 은 영역 0, 4 · 5 는 영역 1 입니다. 삼각형 넷 중 마지막만 영역 1 의 정점이 둘입니다. */
        static AppearanceGeometry makeStrip()
        {
            AppearanceGeometry geometry;
            for ( uint32 row = 0; row < 3; ++row )
            {
                for ( uint32 column = 0; column < 2; ++column )
                {
                    geometry._listPosition.push_back( float3{ static_cast<float32>( column ), static_cast<float32>( row ), 0.0f } );
                    geometry._listNormal.push_back( float3{ 0.0f, 0.0f, -1.0f } );
                    geometry._listUV.push_back( float2{ 0.0f, 0.0f } );
                }
            }
            geometry._listIndex = { 0, 1, 2, 1, 3, 2, 2, 3, 4, 3, 5, 4 };
            return geometry;
        }
    };
} // namespace

/** @brief [DismembermentTest] 정점 둘 이상이 잘라 낼 영역이면 그 삼각형이 떨어진 조각이고, 나머지는 남은 몸이다 */
SW_TEST_CASE( DismembermentTest, TriangleWithTwoSeveredVerticesGoesWithThePiece )
{
    const AppearanceGeometry strip         = DismembermentTestUtil::makeStrip();
    const uint16             arrRegion[6]  = { 0, 0, 0, 0, 1, 1 };
    const uint16             severedRegion = 1;
    DismembermentResult      result;
    SW_REQUIRE_TRUE( DismembermentResult::severRegions( strip, vector_reference<const uint16>{ arrRegion, 6 }, vector_reference<const uint16>{ &severedRegion, 1 }, result ) );
    SW_REQUIRE_EQUAL( size_t{ 4 }, result._listTriangleSevered.size() );
    SW_EXPECT_TRUE( result._listTriangleSevered[0] == SW_FALSE );
    SW_EXPECT_TRUE( result._listTriangleSevered[1] == SW_FALSE );
    SW_EXPECT_TRUE( result._listTriangleSevered[2] == SW_FALSE );
    SW_EXPECT_TRUE( result._listTriangleSevered[3] == SW_TRUE );
}

/** @brief [DismembermentTest] 잘라 낼 영역의 삼각형이 없으면 false 이고 남은 몸이 원본이다 */
SW_TEST_CASE( DismembermentTest, NothingToSeverReturnsFalse )
{
    const AppearanceGeometry strip         = DismembermentTestUtil::makeStrip();
    const uint16             arrRegion[6]  = { 0, 0, 0, 0, 1, 1 };
    const uint16             severedRegion = 7;
    DismembermentResult      result;
    SW_EXPECT_FALSE( DismembermentResult::severRegions( strip, vector_reference<const uint16>{ arrRegion, 6 }, vector_reference<const uint16>{ &severedRegion, 1 }, result ) );
    SW_EXPECT_EQUAL( strip._listIndex.size(), result._remaining._listIndex.size() );
}
