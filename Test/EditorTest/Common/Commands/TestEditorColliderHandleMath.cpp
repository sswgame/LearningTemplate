#include "pch.h"

#include "Core/Math/MatrixMath.h"

#include "Editor/Common/Commands/EditorColliderHandleMath.h"

#include "TestFramework/TestFramework.h"

// EditorColliderHandleMath — 콜라이더 크기 손잡이의 위치와 크기 계산(ImGui 없음).

namespace
{
    struct EditorColliderHandleMathTestInternal
    {
        static const sw::editor::ColliderHandle* findHandle( const sw::vector<sw::editor::ColliderHandle>& listHandle, uint32 axis, float32 sign )
        {
            for ( const sw::editor::ColliderHandle& handle : listHandle )
            {
                if ( handle._axis == axis && handle._sign == sign )
                    return &handle;
            }
            return nullptr;
        }
    };
} // namespace

/**
 * @brief [EditorColliderHandleMathTest] 상자는 면마다 손잡이 여섯 개가 월드 배율을 먹은 가장자리에 서고, 끌면 배율 전 반 크기로 돌아간다
 */
SW_TEST_CASE( EditorColliderHandleMathTest, BoxHandlesFollowTheScaledEdges )
{
    using Internal = EditorColliderHandleMathTestInternal;
    sw::PhysicsShapeDesc3D box;
    box._type        = sw::PhysicsShapeType3D::Box;
    box._halfExtents = sw::float3{ 0.5f, 0.5f, 0.5f };
    sw::vector<sw::editor::ColliderHandle> listHandle;
    const sw::float3                       bodyScale{ 2.0f, 1.0f, 1.0f };
    sw::editor::EditorColliderHandleMath::collectHandles( box, 0, sw::float3{ 1.0f, 2.0f, 3.0f }, sw::quaternion{}, bodyScale, listHandle );
    SW_ASSERT_EQUAL( size_t( 6 ), listHandle.size() );

    const sw::editor::ColliderHandle* pPlusX = Internal::findHandle( listHandle, 0, 1.0f );
    SW_ASSERT_NOT_NULL( pPlusX );
    SW_EXPECT_NEAR_EQUAL( 2.0f, pPlusX->_position._x, 0.0001f ); // 중심 1 + 반 크기 0.5 × 배율 2
    SW_EXPECT_NEAR_EQUAL( 2.0f, pPlusX->_position._y, 0.0001f );
    SW_EXPECT_NEAR_EQUAL( 1.0f, pPlusX->_direction._x, 0.0001f );

    // 가장자리를 월드 거리 3 으로 끌면 배율 2 를 나눈 1.5 가 저장 값이고, 다른 축은 그대로다.
    const sw::PhysicsShapeDesc3D resized = sw::editor::EditorColliderHandleMath::applyDrag( box, *pPlusX, 3.0f, bodyScale );
    SW_EXPECT_NEAR_EQUAL( 1.5f, resized._halfExtents._x, 0.0001f );
    SW_EXPECT_NEAR_EQUAL( 0.5f, resized._halfExtents._y, 0.0001f );
    SW_EXPECT_NEAR_EQUAL( 0.5f, resized._halfExtents._z, 0.0001f );

    // 중심을 넘겨 끌어도 셰이프가 사라지지 않는다.
    const sw::PhysicsShapeDesc3D clamped = sw::editor::EditorColliderHandleMath::applyDrag( box, *pPlusX, -5.0f, bodyScale );
    SW_EXPECT_NEAR_EQUAL( sw::editor::EditorColliderHandleMath::kMinExtent / 2.0f, clamped._halfExtents._x, 0.0001f );
}

/**
 * @brief [EditorColliderHandleMathTest] 구는 반지름을 균일 배율(가장 큰 축)로 환산하고, 캡슐은 반 높이에서 끝 반구 반지름을 뺀다
 */
SW_TEST_CASE( EditorColliderHandleMathTest, SphereAndCapsuleUseTheScaleRulesOfTheBody )
{
    using Internal = EditorColliderHandleMathTestInternal;
    sw::PhysicsShapeDesc3D sphere;
    sphere._type   = sw::PhysicsShapeType3D::Sphere;
    sphere._radius = 0.5f;
    const sw::float3                       bodyScale{ 2.0f, 1.0f, 1.0f };
    sw::vector<sw::editor::ColliderHandle> listHandle;
    sw::editor::EditorColliderHandleMath::collectHandles( sphere, 0, sw::float3{}, sw::quaternion{}, bodyScale, listHandle );
    SW_ASSERT_EQUAL( size_t( 6 ), listHandle.size() );
    const sw::editor::ColliderHandle* pPlusY = Internal::findHandle( listHandle, 1, 1.0f );
    SW_ASSERT_NOT_NULL( pPlusY );
    SW_EXPECT_NEAR_EQUAL( 1.0f, pPlusY->_position._y, 0.0001f ); // 반지름 0.5 × 균일 배율 2
    SW_EXPECT_NEAR_EQUAL( 2.0f, sw::editor::EditorColliderHandleMath::applyDrag( sphere, *pPlusY, 4.0f, bodyScale )._radius, 0.0001f );

    sw::PhysicsShapeDesc3D capsule;
    capsule._type       = sw::PhysicsShapeType3D::Capsule;
    capsule._radius     = 0.5f;
    capsule._halfHeight = 1.0f;
    listHandle.clear();
    sw::editor::EditorColliderHandleMath::collectHandles( capsule, 2, sw::float3{}, sw::quaternion{}, sw::float3{ 1.0f, 1.0f, 1.0f }, listHandle );
    SW_ASSERT_EQUAL( size_t( 6 ), listHandle.size() );
    const sw::editor::ColliderHandle* pTop = Internal::findHandle( listHandle, 1, 1.0f );
    SW_ASSERT_NOT_NULL( pTop );
    SW_EXPECT_TRUE( pTop->_kind == sw::editor::ColliderHandleKind::CapsuleHalfHeight );
    SW_EXPECT_EQUAL( 2u, pTop->_shapeIndex );
    SW_EXPECT_NEAR_EQUAL( 1.5f, pTop->_position._y, 0.0001f ); // 반 높이 1 + 반지름 0.5
    const sw::float3 unitScale{ 1.0f, 1.0f, 1.0f };
    SW_EXPECT_NEAR_EQUAL( 2.5f, sw::editor::EditorColliderHandleMath::applyDrag( capsule, *pTop, 3.0f, unitScale )._halfHeight, 0.0001f );
    SW_EXPECT_NEAR_EQUAL( 0.0f, sw::editor::EditorColliderHandleMath::applyDrag( capsule, *pTop, 0.2f, unitScale )._halfHeight, 0.0001f ); // 반지름보다 안쪽이면 반 높이 0
    const sw::editor::ColliderHandle* pSide = Internal::findHandle( listHandle, 0, -1.0f );
    SW_ASSERT_NOT_NULL( pSide );
    SW_EXPECT_TRUE( pSide->_kind == sw::editor::ColliderHandleKind::CapsuleRadius );
    SW_EXPECT_NEAR_EQUAL( 0.8f, sw::editor::EditorColliderHandleMath::applyDrag( capsule, *pSide, 0.8f, unitScale )._radius, 0.0001f );
}

/**
 * @brief [EditorColliderHandleMathTest] 볼록 껍질 · 삼각형 메시는 손잡이가 없고, 회전한 바디의 손잡이는 중심에서 단위 방향으로 가장자리만큼 떨어진다
 */
SW_TEST_CASE( EditorColliderHandleMathTest, MeshShapesHaveNoHandlesAndRotationTurnsTheDirections )
{
    sw::PhysicsShapeDesc3D hull;
    hull._type = sw::PhysicsShapeType3D::ConvexHull;
    sw::vector<sw::editor::ColliderHandle> listHandle;
    sw::editor::EditorColliderHandleMath::collectHandles( hull, 0, sw::float3{}, sw::quaternion{}, sw::float3{ 1.0f, 1.0f, 1.0f }, listHandle );
    SW_EXPECT_TRUE( listHandle.empty() );

    sw::PhysicsShapeDesc3D box;
    box._halfExtents            = sw::float3{ 1.0f, 2.0f, 3.0f };
    const sw::quaternion turned = sw::quaternion::makeFromYawPitchRoll( sw::float3{ 0.0f, 1.0f, 0.0f } );
    sw::editor::EditorColliderHandleMath::collectHandles( box, 0, sw::float3{ 5.0f, 0.0f, 0.0f }, turned, sw::float3{ 1.0f, 1.0f, 1.0f }, listHandle );
    SW_ASSERT_EQUAL( size_t( 6 ), listHandle.size() );
    const float32 arrExtent[3] = { 1.0f, 2.0f, 3.0f };
    for ( const sw::editor::ColliderHandle& handle : listHandle )
    {
        SW_EXPECT_NEAR_EQUAL( 1.0f, handle._direction.getLength(), 0.0001f );
        const sw::float3 expected = handle._center + handle._direction * arrExtent[handle._axis];
        SW_EXPECT_NEAR_EQUAL( 0.0f, ( expected - handle._position ).getLength(), 0.0001f );
    }
}

/**
 * @brief [EditorColliderHandleMathTest] 레이와 가장 가까운 축 위의 점을 구한다 — 나란한 레이는 0
 */
SW_TEST_CASE( EditorColliderHandleMathTest, ClosestAxisParameterFollowsTheRay )
{
    using Math = sw::editor::EditorColliderHandleMath;
    const sw::float3 axisOrigin{ 0.0f, 0.0f, 0.0f };
    const sw::float3 axisX{ 1.0f, 0.0f, 0.0f };
    SW_EXPECT_NEAR_EQUAL( 3.0f, Math::computeClosestAxisParameter( sw::float3{ 3.0f, 5.0f, 10.0f }, sw::float3{ 0.0f, 0.0f, -1.0f }, axisOrigin, axisX ), 0.0001f );
    SW_EXPECT_NEAR_EQUAL( -2.0f, Math::computeClosestAxisParameter( sw::float3{ -2.0f, 0.0f, 4.0f }, sw::float3{ 0.0f, 0.0f, -1.0f }, axisOrigin, axisX ), 0.0001f );
    SW_EXPECT_NEAR_EQUAL( 0.0f, Math::computeClosestAxisParameter( sw::float3{ 0.0f, 1.0f, 0.0f }, axisX, axisOrigin, axisX ), 0.0001f );
}
