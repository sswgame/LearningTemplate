// 모디파이어 뷰포트 편집의 순수 계산 — 레이와 직선 · 선분 · 점 · 평면, 끌기 평면 이동량, 점 고르기, 부호 있는 각, 한계 사각형, 방향, 2D 대칭 짝, 매개변수 분류.
#include "pch.h"

#include "Core/Math/MathUtil.h"

#include "GameFramework/PlugIn/Editor/MeshModifiers/Stack/ModifierGizmoLogic.h"

#include "TestFramework/TestFramework.h"

#include <cmath>

using namespace sw;
using namespace sw::editor;

namespace
{
    struct ModifierGizmoLogicTestInternal
    {
        static bool isNear( const float3& value, const float3& expected )
        {
            return ( value - expected ).getLength() < 1e-4f;
        }

        /** @brief 기준 축 @p axisIndex 둘레로 @p degree 만큼 오른손 방향으로 돌립니다(기대값을 따로 만드는 용도). */
        static float3 rotateAboutBaseAxis( const float3& value, uint32 axisIndex, float32 degree )
        {
            const float32 radian = MathUtil::toRadian( degree );
            const float32 c      = MathUtil::cos( radian );
            const float32 s      = MathUtil::sin( radian );
            if ( axisIndex == 0 )
                return float3{ value._x, value._y * c - value._z * s, value._y * s + value._z * c };
            if ( axisIndex == 1 )
                return float3{ value._x * c + value._z * s, value._y, -value._x * s + value._z * c };
            return float3{ value._x * c - value._y * s, value._x * s + value._y * c, value._z };
        }

        /** @brief 두 오일러 각이 같은 회전 행렬을 만드는지 세 단위 축의 기준 방향으로 견줍니다. */
        static bool isSameRotation( const float3& eulerA, const float3& eulerB, float32 tolerance )
        {
            MeshModifierGizmo gizmoA{};
            MeshModifierGizmo gizmoB{};
            gizmoA._rotationDegree  = eulerA;
            gizmoB._rotationDegree  = eulerB;
            const float3 arrUnit[3] = {
                float3{1.0f, 0.0f, 0.0f},
                float3{0.0f, 1.0f, 0.0f},
                float3{0.0f, 0.0f, 1.0f}
            };
            for ( const float3& unit : arrUnit )
            {
                if ( ( gizmoA.toBaseDirection( unit ) - gizmoB.toBaseDirection( unit ) ).getLength() > tolerance )
                    return false;
            }
            return true;
        }
    };
} // namespace

SW_TEST_CASE( ModifierGizmoLogicTest, ClosestLineParameterFindsCrossingPoint )
{
    // 레이: (2, 5, 0) 에서 -Y 로. 직선: 원점에서 X 축(길이 2 벡터) → 교차점 x = 2 는 t = 1
    const float32 parameter = ModifierGizmoLogic::computeClosestLineParameter( float3{ 2.0f, 5.0f, 0.0f }, float3{ 0.0f, -1.0f, 0.0f }, float3{ 0.0f, 0.0f, 0.0f },
                                                                               float3{ 2.0f, 0.0f, 0.0f } );
    SW_EXPECT_TRUE( MathUtil::abs( parameter - 1.0f ) < 1e-5f );
    // 평행하면 0
    SW_EXPECT_TRUE( ModifierGizmoLogic::computeClosestLineParameter( float3{ 0.0f, 1.0f, 0.0f }, float3{ 1.0f, 0.0f, 0.0f }, float3{ 0.0f, 0.0f, 0.0f },
                                                                     float3{ 1.0f, 0.0f, 0.0f } ) == 0.0f );
}

SW_TEST_CASE( ModifierGizmoLogicTest, RaySegmentDistanceClampsToSegment )
{
    const float3 origin{ 0.0f, 0.0f, -10.0f };
    const float3 direction{ 0.0f, 0.0f, 1.0f };
    // 선분이 레이를 가로지르면 0
    SW_EXPECT_TRUE( ModifierGizmoLogic::computeRaySegmentDistance( origin, direction, float3{ -1.0f, 0.0f, 0.0f }, float3{ 1.0f, 0.0f, 0.0f } ) < 1e-5f );
    // 선분이 x = 2..3 이면 가장 가까운 끝점 거리 2
    SW_EXPECT_TRUE( MathUtil::abs( ModifierGizmoLogic::computeRaySegmentDistance( origin, direction, float3{ 2.0f, 0.0f, 0.0f }, float3{ 3.0f, 0.0f, 0.0f } ) - 2.0f ) <
                    1e-5f );
}

SW_TEST_CASE( ModifierGizmoLogicTest, RayPointDistanceIgnoresBehind )
{
    const float3 origin{ 0.0f, 0.0f, 0.0f };
    const float3 direction{ 1.0f, 0.0f, 0.0f };
    SW_EXPECT_TRUE( MathUtil::abs( ModifierGizmoLogic::computeRayPointDistance( origin, direction, float3{ 5.0f, 3.0f, 0.0f } ) - 3.0f ) < 1e-5f );
    // 뒤에 있으면 원점까지의 거리
    SW_EXPECT_TRUE( MathUtil::abs( ModifierGizmoLogic::computeRayPointDistance( origin, direction, float3{ -3.0f, 4.0f, 0.0f } ) - 5.0f ) < 1e-5f );
}

SW_TEST_CASE( ModifierGizmoLogicTest, RayPlaneIntersectionAndParallel )
{
    float3 hit{ 0.0f, 0.0f, 0.0f };
    SW_EXPECT_TRUE( ModifierGizmoLogic::tryIntersectRayPlane( float3{ 1.0f, 5.0f, 2.0f }, float3{ 0.0f, -1.0f, 0.0f }, float3{ 0.0f, 1.0f, 0.0f },
                                                              float3{ 0.0f, 1.0f, 0.0f }, hit ) );
    SW_EXPECT_TRUE( ModifierGizmoLogicTestInternal::isNear( hit, float3{ 1.0f, 1.0f, 2.0f } ) );
    SW_EXPECT_FALSE( ModifierGizmoLogic::tryIntersectRayPlane( float3{ 0.0f, 5.0f, 0.0f }, float3{ 1.0f, 0.0f, 0.0f }, float3{ 0.0f, 0.0f, 0.0f },
                                                               float3{ 0.0f, 1.0f, 0.0f }, hit ) );
    // 평면이 레이 뒤
    SW_EXPECT_FALSE( ModifierGizmoLogic::tryIntersectRayPlane( float3{ 0.0f, 5.0f, 0.0f }, float3{ 0.0f, 1.0f, 0.0f }, float3{ 0.0f, 0.0f, 0.0f },
                                                               float3{ 0.0f, 1.0f, 0.0f }, hit ) );
}

SW_TEST_CASE( ModifierGizmoLogicTest, PlaneDragDeltaFollowsRay )
{
    // 카메라가 -Z 를 본다. 원점을 지나는 Z 평면에서 레이가 x 0 → 2 로 옮겨지면 이동량은 (2, 0, 0)
    float3 delta{ 0.0f, 0.0f, 0.0f };
    SW_EXPECT_TRUE( ModifierGizmoLogic::tryComputePlaneDragDelta( float3{ 0.0f, 0.0f, 10.0f }, float3{ 0.0f, 0.0f, -1.0f }, float3{ 2.0f, 0.0f, 10.0f },
                                                                  float3{ 0.0f, 0.0f, -1.0f }, float3{ 0.0f, 0.0f, 0.0f }, float3{ 0.0f, 0.0f, -1.0f }, delta ) );
    SW_EXPECT_TRUE( ModifierGizmoLogicTestInternal::isNear( delta, float3{ 2.0f, 0.0f, 0.0f } ) );
}

SW_TEST_CASE( ModifierGizmoLogicTest, FindPointOnRayPicksNearestAlongRay )
{
    const vector<float3> listPoint{
        float3{ 0.0f, 0.0f,  5.0f},
        float3{0.05f, 0.0f,  2.0f},
        float3{ 3.0f, 0.0f,  1.0f},
        float3{ 0.0f, 0.0f, -1.0f}
    };
    // 레이 원점 (0,0,0) +Z: 0 번(거리 0, 5 앞)과 1 번(거리 0.05, 2 앞)이 반경 안 — 더 앞인 1 번. 3 번은 뒤
    SW_EXPECT_EQUAL( 1, ModifierGizmoLogic::findPointOnRay( float3{ 0.0f, 0.0f, 0.0f }, float3{ 0.0f, 0.0f, 1.0f }, listPoint, 0.1f ) );
    SW_EXPECT_EQUAL( -1, ModifierGizmoLogic::findPointOnRay( float3{ 0.0f, 10.0f, 0.0f }, float3{ 0.0f, 0.0f, 1.0f }, listPoint, 0.1f ) );
}

SW_TEST_CASE( ModifierGizmoLogicTest, SignedAngleIsRightHanded )
{
    const float3 axisZ{ 0.0f, 0.0f, 1.0f };
    SW_EXPECT_TRUE( MathUtil::abs( ModifierGizmoLogic::computeSignedAngleDegree( float3{ 1.0f, 0.0f, 0.0f }, float3{ 0.0f, 1.0f, 0.0f }, axisZ ) - 90.0f ) < 1e-3f );
    SW_EXPECT_TRUE( MathUtil::abs( ModifierGizmoLogic::computeSignedAngleDegree( float3{ 1.0f, 0.0f, 0.0f }, float3{ 0.0f, -1.0f, 3.0f }, axisZ ) + 90.0f ) < 1e-3f );
    SW_EXPECT_TRUE( ModifierGizmoLogic::computeSignedAngleDegree( float3{ 0.0f, 0.0f, 1.0f }, float3{ 1.0f, 0.0f, 0.0f }, axisZ ) == 0.0f );
}

SW_TEST_CASE( ModifierGizmoLogicTest, LimitQuadSitsOnAxisFraction )
{
    MeshModifierBounds bounds{};
    bounds._min = float3{ -1.0f, 0.0f, -2.0f };
    bounds._max = float3{ 1.0f, 4.0f, 2.0f };
    vector<float3> listCorner;
    SW_EXPECT_TRUE( ModifierGizmoLogic::tryMakeLimitQuad( bounds, 1, 0.25f, listCorner ) );
    SW_EXPECT_EQUAL( static_cast<size_t>( 4 ), listCorner.size() );
    for ( const float3& corner : listCorner )
    {
        SW_EXPECT_TRUE( MathUtil::abs( corner._y - 1.0f ) < 1e-5f );
        SW_EXPECT_TRUE( MathUtil::abs( MathUtil::abs( corner._z ) - 2.0f ) < 1e-5f );
        SW_EXPECT_TRUE( MathUtil::abs( MathUtil::abs( corner._x ) - 1.0f ) < 1e-5f );
    }
    // 이웃 모서리는 한 성분만 다르다(둘레 순서)
    SW_EXPECT_TRUE( ( listCorner[0] - listCorner[1] ).getLength() > 0.0f );
    SW_EXPECT_TRUE( ( listCorner[0] - listCorner[2] ).getLength() > ( listCorner[0] - listCorner[1] ).getLength() );
    SW_EXPECT_FALSE( ModifierGizmoLogic::tryMakeLimitQuad( MeshModifierBounds{}, 1, 0.5f, listCorner ) );
    SW_EXPECT_TRUE( listCorner.empty() );
}

SW_TEST_CASE( ModifierGizmoLogicTest, DirectionVectorIsPerpendicularToAxis )
{
    SW_EXPECT_TRUE( ModifierGizmoLogicTestInternal::isNear( ModifierGizmoLogic::computeDirectionVector( 1, 0.0f ), float3{ 0.0f, 0.0f, 1.0f } ) );
    SW_EXPECT_TRUE( ModifierGizmoLogicTestInternal::isNear( ModifierGizmoLogic::computeDirectionVector( 1, 90.0f ), float3{ 1.0f, 0.0f, 0.0f } ) );
    SW_EXPECT_TRUE( ModifierGizmoLogicTestInternal::isNear( ModifierGizmoLogic::computeDirectionVector( 2, 0.0f ), float3{ 1.0f, 0.0f, 0.0f } ) );
}

SW_TEST_CASE( ModifierGizmoLogicTest, MirrorIndex2DFlipsOneAxis )
{
    // 3 x 2 격자: 점 1 = (1, 0)
    SW_EXPECT_EQUAL( 2, ModifierGizmoLogic::findMirrorIndex2D( 0, 3, 2, 0 ) );
    SW_EXPECT_EQUAL( 1, ModifierGizmoLogic::findMirrorIndex2D( 1, 3, 2, 0 ) ); // 가운데 줄은 자기 자신
    SW_EXPECT_EQUAL( 4, ModifierGizmoLogic::findMirrorIndex2D( 1, 3, 2, 1 ) );
    SW_EXPECT_EQUAL( -1, ModifierGizmoLogic::findMirrorIndex2D( 1, 3, 2, 2 ) );
    SW_EXPECT_EQUAL( -1, ModifierGizmoLogic::findMirrorIndex2D( 6, 3, 2, 0 ) );
}

SW_TEST_CASE( ModifierGizmoLogicTest, RangeKindPrefersHintThenName )
{
    MeshModifierParamDef def{};
    def._pName = "Radius";
    def._kind  = MeshModifierParamKind::Float;
    SW_EXPECT_TRUE( ModifierGizmoLogic::computeRangeKind( def ) == ModifierRangeKind::Radius );
    def._pName = "Lower Limit";
    SW_EXPECT_TRUE( ModifierGizmoLogic::computeRangeKind( def ) == ModifierRangeKind::LowerLimit );
    def._pName = "upper limit";
    SW_EXPECT_TRUE( ModifierGizmoLogic::computeRangeKind( def ) == ModifierRangeKind::UpperLimit );
    // 이름이 비슷해도 같지 않으면 아니다
    def._pName = "Curl Radius";
    SW_EXPECT_TRUE( ModifierGizmoLogic::computeRangeKind( def ) == ModifierRangeKind::None );
    // 힌트가 이름보다 먼저다
    def._pHint = "falloff radius around the center";
    SW_EXPECT_TRUE( ModifierGizmoLogic::computeRangeKind( def ) == ModifierRangeKind::Radius );
    // 종류가 맞지 않으면 그리지 않는다
    def._pName = "Center";
    def._pHint = nullptr;
    SW_EXPECT_TRUE( ModifierGizmoLogic::computeRangeKind( def ) == ModifierRangeKind::None );
    def._kind = MeshModifierParamKind::Vector3;
    SW_EXPECT_TRUE( ModifierGizmoLogic::computeRangeKind( def ) == ModifierRangeKind::Center );
    def._pName = "Direction";
    SW_EXPECT_TRUE( ModifierGizmoLogic::computeRangeKind( def ) == ModifierRangeKind::Direction );
}

SW_TEST_CASE( ModifierGizmoLogicTest, RotationDeltaMatchesBaseAxisRotation )
{
    using Internal = ModifierGizmoLogicTestInternal;
    // 모든 성분이 0 이 아닐 때도 결과 회전 = 기준 축 회전 · 시작 회전 이어야 한다(옛 방식은 성분 하나만 더해 틀렸다)
    const float3 start{ 30.0f, -20.0f, 45.0f };
    for ( uint32 axis = 0; axis < 3; ++axis )
    {
        const float3      result = ModifierGizmoLogic::applyRotationDelta( start, axis, 25.0f );
        MeshModifierGizmo startGizmo{};
        MeshModifierGizmo resultGizmo{};
        startGizmo._rotationDegree  = start;
        resultGizmo._rotationDegree = result;
        const float3 arrUnit[3]     = {
            float3{1.0f, 0.0f, 0.0f},
            float3{0.0f, 1.0f, 0.0f},
            float3{0.0f, 0.0f, 1.0f}
        };
        for ( const float3& unit : arrUnit )
        {
            const float3 expected = Internal::rotateAboutBaseAxis( startGizmo.toBaseDirection( unit ), axis, 25.0f );
            SW_EXPECT_TRUE( ( resultGizmo.toBaseDirection( unit ) - expected ).getLength() < 1e-4f );
        }
    }
}

SW_TEST_CASE( ModifierGizmoLogicTest, RotationDeltaRoundTripRestoresRotation )
{
    const float3 start{ -50.0f, 35.0f, 120.0f };
    for ( uint32 axis = 0; axis < 3; ++axis )
    {
        const float3 forward = ModifierGizmoLogic::applyRotationDelta( start, axis, 40.0f );
        const float3 back    = ModifierGizmoLogic::applyRotationDelta( forward, axis, -40.0f );
        SW_EXPECT_TRUE( ModifierGizmoLogicTestInternal::isSameRotation( back, start, 1e-4f ) );
    }
}

SW_TEST_CASE( ModifierGizmoLogicTest, RotationDeltaStaysFiniteAtGimbal )
{
    // Y 를 정확히 90 도로 보내는 증분과 그 근처에서도 값이 유한하고 회전이 맞아야 한다
    const float3 arrStart[] = {
        float3{20.0f,   60.0f, 10.0f},
        float3{20.0f, 89.999f, 10.0f},
        float3{ 0.0f,  -90.0f, 30.0f}
    };
    for ( const float3& start : arrStart )
    {
        for ( uint32 axis = 0; axis < 3; ++axis )
        {
            const float3 result = ModifierGizmoLogic::applyRotationDelta( start, axis, 30.0f );
            SW_EXPECT_TRUE( std::isfinite( result._x ) && std::isfinite( result._y ) && std::isfinite( result._z ) );
            const float3 back = ModifierGizmoLogic::applyRotationDelta( result, axis, -30.0f );
            SW_EXPECT_TRUE( ModifierGizmoLogicTestInternal::isSameRotation( back, start, 1e-3f ) );
        }
    }
}
