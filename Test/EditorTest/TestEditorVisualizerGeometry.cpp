#include "pch.h"

#include "Core/Math/MathUtil.h"

#include "Editor/Viewport/EditorVisualizerGeometry.h"

#include "Engine/Graphics/Renderer/Debug/DebugDrawQueue.h"
#include "Engine/Object/Component/2D/BoxCollider2DComponent.h"
#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManager.h"

#include "TestFramework/TestFramework.h"

using namespace sw;
using namespace sw::editor;

// EditorVisualizerGeometryTest — 뷰포트 시각화가 그리는 월드 도형(콜라이더 상자 · 디버그 선 · 구). 투영 · ImGui 그리기는 시각화 파일의 몫이다.

/**
 * @brief [EditorVisualizerGeometryTest] 콜라이더 시각화는 물리가 판정하는 상자(월드 스케일 · 회전을 받은 것)를 그린다
 * @details 시각화가 `getWorldPosition() + offset` 에 로컬 크기의 절반을 더해 그려, 키운 콜라이더가 물리 상자보다 작게 보였다(오프셋도 스케일을 받지
 *          않았다). 이제 `getWorldBox` 의 상자를 그린다. 시각화는 씬 전체가 아니라 매니저의 콜라이더 등록부를 본다.
 */
SW_TEST_CASE( EditorVisualizerGeometryTest, ColliderOutlineIsThePhysicsBox )
{
    GameObjectManager manager;
    GameObject*       pObject = manager.createGameObject( hashed_string( "Crate" ) );
    SW_ASSERT_NOT_NULL( pObject );
    BoxCollider2DComponent* pBox = pObject->addComponent<BoxCollider2DComponent>();
    SW_ASSERT_NOT_NULL( pBox );
    pBox->setLocalPosition( float3{ 1.0f, 1.0f, 4.0f } );
    pBox->setLocalScale( float3{ 2.0f, 3.0f, 1.0f } );
    pBox->setOffsetPosition( float2{ 0.5f, 0.0f } );
    pBox->setOffsetScale( float2{ 1.0f, 1.0f } );
    manager.flushSceneTransforms();

    const vector<BoxCollider2DComponent*>& listCollider = manager.getColliders();
    SW_ASSERT_EQUAL( size_t( 1 ), listCollider.size() );
    SW_EXPECT_TRUE( listCollider[0] == pBox );

    // 중심 = (1 + 0.5 * 2, 1) = (2, 1), 반 크기 = (0.5 * 2, 0.5 * 3) = (1, 1.5).
    float3 arrCorner[4];
    EditorVisualizerGeometryUtil::computeColliderCorners( *pBox, arrCorner );
    SW_EXPECT_NEAR_EQUAL( 1.0f, arrCorner[0]._x, 1e-4f );
    SW_EXPECT_NEAR_EQUAL( -0.5f, arrCorner[0]._y, 1e-4f );
    SW_EXPECT_NEAR_EQUAL( 3.0f, arrCorner[2]._x, 1e-4f );
    SW_EXPECT_NEAR_EQUAL( 2.5f, arrCorner[2]._y, 1e-4f );
    SW_EXPECT_NEAR_EQUAL( 4.0f, arrCorner[1]._z, 1e-4f );

    // 물리 상자와 같다.
    AABB box{};
    SW_ASSERT_TRUE( pBox->getWorldBox( box ) );
    SW_EXPECT_NEAR_EQUAL( box._min._x, arrCorner[0]._x, 1e-5f );
    SW_EXPECT_NEAR_EQUAL( box._min._y, arrCorner[0]._y, 1e-5f );
    SW_EXPECT_NEAR_EQUAL( box._max._x, arrCorner[2]._x, 1e-5f );
    SW_EXPECT_NEAR_EQUAL( box._max._y, arrCorner[2]._y, 1e-5f );

    // 크기 0 인 콜라이더도 보이게 최소 반 크기로 그린다.
    pBox->setOffsetScale( float2{ 0.0f, 0.0f } );
    EditorVisualizerGeometryUtil::computeColliderCorners( *pBox, arrCorner );
    SW_EXPECT_NEAR_EQUAL( 2.0f * EditorVisualizerGeometryUtil::kMinColliderHalfExtent, arrCorner[2]._x - arrCorner[0]._x, 1e-5f );
}

/**
 * @brief [EditorVisualizerGeometryTest] 디버그 큐의 선은 그대로, 구는 대원 세 개의 선분으로 나온다
 * @details `DebugDrawQueue` 는 게임 코드가 채우고 `EngineLoop` 가 비우지만 읽는 쪽이 없었다(`getLines` · `getSpheres` 호출 0). 뷰포트 시각화
 *          (`debug_draw`)가 이 선분을 투영해 그린다.
 */
SW_TEST_CASE( EditorVisualizerGeometryTest, DebugDrawQueueBecomesWorldSegments )
{
    DebugDrawQueue queue;
    const float4   lineColor{ 1.0f, 0.0f, 0.0f, 1.0f };
    const float4   sphereColor{ 0.0f, 1.0f, 0.0f, 0.5f };
    queue.drawLine( float3{ 0.0f, 0.0f, 0.0f }, float3{ 1.0f, 2.0f, 3.0f }, lineColor );
    const float3 center{ 1.0f, 0.0f, -2.0f };
    queue.drawSphere( center, 2.0f, sphereColor );

    vector<EditorWorldSegment> listSegment;
    EditorVisualizerGeometryUtil::appendDebugDrawSegments( queue, listSegment );
    SW_ASSERT_EQUAL( size_t( 1 + 3 * EditorVisualizerGeometryUtil::kSphereCircleSegmentCount ), listSegment.size() );
    SW_EXPECT_NEAR_EQUAL( 3.0f, listSegment[0]._to._z, 1e-6f );
    SW_EXPECT_NEAR_EQUAL( 1.0f, listSegment[0]._color._x, 1e-6f );

    // 구의 선분 끝점은 모두 중심에서 반지름만큼 떨어져 있고, 세 축 평면을 다 지난다.
    float32 maxAbsX{ 0.0f };
    float32 maxAbsY{ 0.0f };
    float32 maxAbsZ{ 0.0f };
    for ( size_t segmentIndex = 1; segmentIndex < listSegment.size(); ++segmentIndex )
    {
        const float3 offset = listSegment[segmentIndex]._from - center;
        SW_EXPECT_NEAR_EQUAL( 2.0f, offset.getLength(), 1e-4f );
        SW_EXPECT_NEAR_EQUAL( 0.5f, listSegment[segmentIndex]._color._w, 1e-6f );
        maxAbsX = MathUtil::max( maxAbsX, MathUtil::abs( offset._x ) );
        maxAbsY = MathUtil::max( maxAbsY, MathUtil::abs( offset._y ) );
        maxAbsZ = MathUtil::max( maxAbsZ, MathUtil::abs( offset._z ) );
    }
    SW_EXPECT_NEAR_EQUAL( 2.0f, maxAbsX, 1e-4f );
    SW_EXPECT_NEAR_EQUAL( 2.0f, maxAbsY, 1e-4f );
    SW_EXPECT_NEAR_EQUAL( 2.0f, maxAbsZ, 1e-4f );

    // 뒤에 붙인다(부르는 쪽이 비운다).
    EditorVisualizerGeometryUtil::appendDebugDrawSegments( queue, listSegment );
    SW_EXPECT_EQUAL( size_t( 2 * ( 1 + 3 * EditorVisualizerGeometryUtil::kSphereCircleSegmentCount ) ), listSegment.size() );
}
