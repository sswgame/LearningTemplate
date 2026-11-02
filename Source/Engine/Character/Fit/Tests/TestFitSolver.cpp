#include "pch.h"

#include "Core/Testing/TestFramework.h"

#include "Engine/Character/Fit/CharacterGeometry.h"
#include "Engine/Character/Fit/FitPartDef.h"
#include "Engine/Character/Fit/FitSolver.h"
#include "Engine/Character/Fit/FitTables.h"

using namespace sw;

// 피팅 풀이(FitSolver)의 삼각형 자르기 — 바깥 겹의 숨김 영역이 안쪽 몸을 지우고, 남는 쪽과의 경계 한 겹은 남긴다. 디바이스 없음(nogpu).

namespace
{
    struct FitSolverTestInternal
    {
        /** @brief x 로 늘어선 사각형 넷(정점 열 x = 0..4, 삼각형 여덟). 열 0..2 는 칠한 그룹 "torso", 열 3..4 는 "head" 다. */
        static AppearanceGeometry makeBodyStrip()
        {
            AppearanceGeometry geometry;
            geometry._listGroupName = { hashed_string( "torso" ), hashed_string( "head" ) };
            for ( uint32 column = 0; column <= 4u; ++column )
            {
                for ( uint32 row = 0; row <= 1u; ++row )
                {
                    geometry._listPosition.push_back( float3{ static_cast<float32>( column ), static_cast<float32>( row ), 0.0f } );
                    geometry._listVertexGroup.push_back( column <= 2u ? 0u : 1u );
                }
            }
            for ( uint32 quad = 0; quad < 4u; ++quad )
            {
                const uint32 corner = quad * 2u; // (x, 0) 이 2x, (x, 1) 이 2x + 1
                geometry._listIndex.insert( geometry._listIndex.end(), { corner, corner + 2u, corner + 3u, corner, corner + 3u, corner + 1u } );
            }
            return geometry;
        }

        /** @brief 몸에서 멀리 떨어진 사각형 하나 — 덮임 거리 밖이라 덮어서 자르지 않고 숨김 영역으로만 지운다(헐렁한 옷). */
        static AppearanceGeometry makeFarShirt()
        {
            AppearanceGeometry geometry;
            geometry._listPosition = {
                float3{0.0f, 0.0f, 5.0f},
                float3{1.0f, 0.0f, 5.0f},
                float3{1.0f, 1.0f, 5.0f},
                float3{0.0f, 1.0f, 5.0f}
            };
            geometry._listIndex = { 0u, 1u, 2u, 0u, 2u, 3u };
            return geometry;
        }
    };
} // namespace

/**
 * @brief [FitSolverTest] 바깥 겹이 숨긴 영역의 몸 삼각형은 지우되, 남는 삼각형과 정점을 나누는 경계 한 겹은 남기는지 검증(옷 끝과 영역 경계 사이의 틈을 막는다)
 */
SW_TEST_CASE( FitSolverTest, HiddenRegionCutsBodyButKeepsBorderRing )
{
    using Internal = FitSolverTestInternal;
    FitSolver solver;
    FitTables tables;
    SW_REQUIRE_TRUE( tables.loadFromXMLText( R"(<FitTables>
        <Layer name="Body" order="0"/><Layer name="Shirt" order="10"/>
        <Region name="Torso" group="torso"/><Region name="Head" group="head"/>
        <Profile name="Skin" rigidity="0" coverageDistance="0.02"/>
        <Profile name="Cloth" rigidity="0" shrinkStrength="1" pushDistance="0.004" falloffWidth="0.05" coverageDistance="0.02"/>
        <Interaction inner="Skin" outer="Cloth" operator="Cut"/></FitTables>)",
                                             "test", solver.getOperatorRegistry() ) );
    FitPartDef bodyFit;
    FitPartDef shirtFit;
    SW_REQUIRE_TRUE( bodyFit.loadFromXMLText( R"(<PartFit layer="Body" profile="Skin"/>)", "body" ) );
    SW_REQUIRE_TRUE( shirtFit.loadFromXMLText( R"(<PartFit layer="Shirt" profile="Cloth"><Hide region="Torso"/></PartFit>)", "shirt" ) );

    const AppearanceGeometry body  = Internal::makeBodyStrip();
    const AppearanceGeometry shirt = Internal::makeFarShirt();
    const CharacterBoneArray bones;
    FitInput                 input;
    input._pBindBones = &bones;
    input._listPart.push_back( FitPartInput{ hashed_string( "Body" ), &body, &bodyFit } );
    input._listPart.push_back( FitPartInput{ hashed_string( "Shirt" ), &shirt, &shirtFit } );
    FitResult result;
    SW_REQUIRE_TRUE( solver.solve( tables, input, result ) );

    // 사각형 0(열 0..1)은 지운다. 사각형 1(열 1..2)도 숨김 영역이지만 남는 사각형 2 와 열 2 를 나눠 경계로 남는다. 사각형 2 · 3 은 머리가 섞여 숨기지 않는다.
    const FitPartResult& bodyResult = result._listPart[0];
    SW_EXPECT_EQUAL( 2u, bodyResult._cutTriangleCount );
    SW_EXPECT_FALSE( bodyResult.isTriangleVisible( 0 ) );
    SW_EXPECT_FALSE( bodyResult.isTriangleVisible( 1 ) );
    for ( uint32 triangle = 2; triangle < 8u; ++triangle )
    {
        SW_EXPECT_TRUE( bodyResult.isTriangleVisible( triangle ) );
    }
    SW_EXPECT_EQUAL( 0u, result._listPart[1]._cutTriangleCount );
}
