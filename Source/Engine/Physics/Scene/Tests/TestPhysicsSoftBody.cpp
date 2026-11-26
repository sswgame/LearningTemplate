// PhysicsSoftBodyTest — 3D 씬의 소프트 바디(`IPhysicsSoftBodyScene` — 천 · 헤어 카드): 서술 거절, 고정 정점은 움직이지 않고 자세를 따르고 나머지는 떨어진다, 늘어나지 않는 변,
// 바닥 충돌, 뼈 스킨 구속, 지운 핸들. 디바이스 없음(nogpu).
#include "pch.h"

#include "Core/Container/vector.h"
#include "Core/Math/MathUtil.h"
#include "Core/Memory/Memory.h"
#include "Core/Testing/TestFramework.h"

#include "Engine/Foundation/EngineSubsystems.h"
#include "Engine/Physics/IPhysicsScene.h"
#include "Engine/Physics/PhysicsSettings.h"
#include "Engine/Physics/PhysicsSystem.h"

using namespace sw;

namespace
{
    struct TestPhysicsSoftBodyInternal
    {
        static constexpr float32 kFrame       = 1.0f / 60.0f;
        static constexpr uint32  kColumnCount = 3;
        static constexpr uint32  kRowCount    = 5;
        static constexpr float32 kSpacing     = 0.25f;
        static constexpr uint32  kLastVertex  = kColumnCount * kRowCount - 1;

        static unique_ptr<IPhysicsScene3D> createScene()
        {
            const PhysicsSystem& system = engine::getSubsystem<PhysicsSystem>();
            return system.createScene3D( system.getSettings() );
        }

        /** @brief XY 평면의 격자(윗줄이 로컬 y 0, 아래로)입니다. @p bPinTop 이면 윗줄을 고정합니다. */
        static PhysicsSoftBodyDesc makeSheet( const float3& position, bool bPinTop )
        {
            PhysicsSoftBodyDesc desc;
            desc._position = position;
            for ( uint32 row = 0; row < kRowCount; ++row )
            {
                for ( uint32 column = 0; column < kColumnCount; ++column )
                {
                    desc._listVertex.push_back( float3{ static_cast<float32>( column ) * kSpacing, -static_cast<float32>( row ) * kSpacing, 0.0f } );
                }
            }
            for ( uint32 row = 0; row + 1 < kRowCount; ++row )
            {
                for ( uint32 column = 0; column + 1 < kColumnCount; ++column )
                {
                    const uint32 topLeft    = row * kColumnCount + column;
                    const uint32 bottomLeft = topLeft + kColumnCount;
                    desc._listTriangleIndex.insert( desc._listTriangleIndex.end(), { topLeft, bottomLeft, topLeft + 1, topLeft + 1, bottomLeft, bottomLeft + 1 } );
                }
            }
            if ( bPinTop )
                desc._listPinnedVertex.insert( desc._listPinnedVertex.end(), { 0u, 1u, 2u } );
            return desc;
        }

        static void step( IPhysicsScene3D& scene, uint32 stepCount )
        {
            for ( uint32 index = 0; index < stepCount; ++index )
            {
                scene.step( kFrame );
            }
        }
    };
    using Internal = TestPhysicsSoftBodyInternal;
} // namespace

/** @brief [PhysicsSoftBodyTest] 정점이 없거나, 삼각형이 온전하지 않거나, 없는 정점을 가리키거나, 질량 · 반복 수가 0 이면 오류와 함께 무효 핸들이다 — 반쯤 만든 바디를 남기지 않는다 */
SW_TEST_CASE( PhysicsSoftBodyTest, InvalidDescriptionsAreRefused )
{
    SW_EXPECT_LOG( "JoltPhysicsScene.SoftBodyNeedsVertices" );
    SW_EXPECT_LOG( "JoltPhysicsScene.SoftBodyNamesVertex" );
    SW_EXPECT_LOG( "JoltPhysicsScene.SoftBodyTriangleRepeats" );
    SW_EXPECT_LOG( "JoltPhysicsScene.SoftBodyNeedsIterations" );
    const unique_ptr<IPhysicsScene3D> pScene = Internal::createScene();
    SW_REQUIRE_NOT_NULL( pScene.get() );

    SW_EXPECT_FALSE( pScene->createSoftBody( PhysicsSoftBodyDesc{} ).isValid() );
    PhysicsSoftBodyDesc brokenTriangle = Internal::makeSheet( float3{}, false );
    brokenTriangle._listTriangleIndex.pop_back();
    SW_EXPECT_FALSE( pScene->createSoftBody( brokenTriangle ).isValid() );
    PhysicsSoftBodyDesc noMass = Internal::makeSheet( float3{}, false );
    noMass._vertexMass         = 0.0f;
    SW_EXPECT_FALSE( pScene->createSoftBody( noMass ).isValid() );
    PhysicsSoftBodyDesc unknownPin = Internal::makeSheet( float3{}, true );
    unknownPin._listPinnedVertex.push_back( Internal::kLastVertex + 1 );
    SW_EXPECT_FALSE( pScene->createSoftBody( unknownPin ).isValid() );
    PhysicsSoftBodyDesc repeatedVertex   = Internal::makeSheet( float3{}, false );
    repeatedVertex._listTriangleIndex[1] = repeatedVertex._listTriangleIndex[0];
    SW_EXPECT_FALSE( pScene->createSoftBody( repeatedVertex ).isValid() );
    PhysicsSoftBodyDesc noIteration = Internal::makeSheet( float3{}, false );
    noIteration._iterationCount     = 0;
    SW_EXPECT_FALSE( pScene->createSoftBody( noIteration ).isValid() );
}

/** @brief [PhysicsSoftBodyTest] 고정한 윗줄은 제자리에, 나머지는 중력에 매달린다 — 변은 늘어나지 않아 아랫줄까지의 길이가 격자 길이 그대로다 */
SW_TEST_CASE( PhysicsSoftBodyTest, PinnedRowHoldsAndEdgesDoNotStretch )
{
    const unique_ptr<IPhysicsScene3D> pScene = Internal::createScene();
    SW_REQUIRE_NOT_NULL( pScene.get() );
    PhysicsSoftBodyDesc desc                = Internal::makeSheet( float3{ 0.0f, 5.0f, 0.0f }, true );
    desc._rotation                          = quaternion::makeFromAxisAngle( float3{ 1.0f, 0.0f, 0.0f }, MathUtil::kPi * 0.25f ); // 45° 로 들어 올린 채 놓는다 — 매달리려면 흔들려 내려와야 한다
    desc._linearDamping                     = 1.0f;
    desc._iterationCount                    = 16;
    const PhysicsSoftBodyHandle softBody    = pScene->createSoftBody( desc );
    const float32               sheetLength = static_cast<float32>( Internal::kRowCount - 1 ) * Internal::kSpacing;
    SW_REQUIRE_TRUE( softBody.isValid() );
    Internal::step( *pScene, 600 );

    vector<float3> listVertex;
    SW_REQUIRE_TRUE( pScene->getSoftBodyVertices( softBody, listVertex ) );
    SW_REQUIRE_EQUAL( static_cast<size_t>( Internal::kLastVertex + 1 ), listVertex.size() );
    for ( uint32 column = 0; column < Internal::kColumnCount; ++column )
    {
        SW_EXPECT_NEAR( static_cast<float32>( column ) * Internal::kSpacing, listVertex[column]._x, 1.0e-3f );
        SW_EXPECT_NEAR( 5.0f, listVertex[column]._y, 1.0e-3f );
        SW_EXPECT_NEAR( 0.0f, listVertex[column]._z, 1.0e-3f );
    }
    SW_EXPECT_NEAR( 5.0f - sheetLength, listVertex[Internal::kLastVertex]._y, 0.05f ); // 아래로 똑바로 매달렸다
    SW_EXPECT_NEAR( 0.0f, listVertex[Internal::kLastVertex]._z, 0.1f );
}

/** @brief [PhysicsSoftBodyTest] 고정 정점은 `setSoftBodyPinnedTransform` 이 준 자세로 옮겨지고, 매달린 나머지가 따라온다(자던 바디도 깨운다) */
SW_TEST_CASE( PhysicsSoftBodyTest, PinnedVerticesFollowTheTransform )
{
    const unique_ptr<IPhysicsScene3D> pScene = Internal::createScene();
    SW_REQUIRE_NOT_NULL( pScene.get() );
    PhysicsSoftBodyDesc desc             = Internal::makeSheet( float3{ 0.0f, 5.0f, 0.0f }, true );
    desc._linearDamping                  = 1.0f;
    const PhysicsSoftBodyHandle softBody = pScene->createSoftBody( desc );
    SW_REQUIRE_TRUE( softBody.isValid() );
    Internal::step( *pScene, 300 ); // 매달려 쉰다(잠들 수 있다)

    // 걸음마다 조금씩 옮긴다(`ClothComponent` 와 같다) — 한 걸음에 몇 미터를 순간 이동시키면 천이 그 속도로 튕겨 올라 거꾸로 접힌다.
    constexpr uint32 kMoveStepCount = 120;
    for ( uint32 index = 1; index <= kMoveStepCount; ++index )
    {
        const float32 alpha = static_cast<float32>( index ) / static_cast<float32>( kMoveStepCount );
        pScene->setSoftBodyPinnedTransform( softBody, float3{ 2.0f * alpha, 5.0f + 3.0f * alpha, 0.0f }, quaternion{} );
        Internal::step( *pScene, 1 );
    }
    Internal::step( *pScene, 300 );
    vector<float3> listVertex;
    SW_REQUIRE_TRUE( pScene->getSoftBodyVertices( softBody, listVertex ) );
    SW_EXPECT_NEAR( 2.0f, listVertex[0]._x, 1.0e-3f );
    SW_EXPECT_NEAR( 8.0f, listVertex[0]._y, 1.0e-3f );
    SW_EXPECT_NEAR( 7.0f, listVertex[Internal::kLastVertex]._y, 0.2f );
    SW_EXPECT_NEAR( 2.5f, listVertex[Internal::kLastVertex]._x, 0.2f );
}

/** @brief [PhysicsSoftBodyTest] 고정하지 않은 천은 떨어져 바닥 위에서 멈춘다(정점 반지름만큼 떨어져 뚫지 않는다) */
SW_TEST_CASE( PhysicsSoftBodyTest, FreeSheetRestsOnTheFloor )
{
    const unique_ptr<IPhysicsScene3D> pScene = Internal::createScene();
    SW_REQUIRE_NOT_NULL( pScene.get() );
    PhysicsBodyDesc3D floor;
    floor._listShape.push_back( PhysicsShapeDesc3D{} );
    floor._listShape[0]._type        = PhysicsShapeType3D::Box;
    floor._listShape[0]._halfExtents = float3{ 20.0f, 0.5f, 20.0f };
    floor._position                  = float3{ 0.0f, -0.5f, 0.0f };
    floor._type                      = PhysicsBodyType::Static;
    SW_REQUIRE_TRUE( pScene->createBody( floor ).isValid() );
    const PhysicsSoftBodyHandle softBody = pScene->createSoftBody( Internal::makeSheet( float3{ 0.0f, 2.0f, 0.0f }, false ) );
    SW_REQUIRE_TRUE( softBody.isValid() );
    Internal::step( *pScene, 300 );

    vector<float3> listVertex;
    SW_REQUIRE_TRUE( pScene->getSoftBodyVertices( softBody, listVertex ) );
    for ( const float3& vertex : listVertex )
    {
        SW_EXPECT_TRUE( vertex._y > -0.02f );
        SW_EXPECT_TRUE( vertex._y < 1.5f ); // 처음(1 ~ 2 m)에서 내려와 쓰러졌다
    }
}

/** @brief [PhysicsSoftBodyTest] 뼈 스킨 구속 정점(최대 거리 0)은 준 뼈 자세를 따르고, 뼈 수가 다른 자세는 오류와 함께 무시한다 */
SW_TEST_CASE( PhysicsSoftBodyTest, SkinnedVerticesFollowTheJoint )
{
    SW_EXPECT_LOG( "JoltPhysicsScene.SoftBodySkinningPose" );
    const unique_ptr<IPhysicsScene3D> pScene = Internal::createScene();
    SW_REQUIRE_NOT_NULL( pScene.get() );
    PhysicsSoftBodyDesc desc = Internal::makeSheet( float3{ 0.0f, 5.0f, 0.0f }, false );
    desc._listInverseBind.push_back( float4x4::makeScale( 1.0f ) );
    for ( uint32 column = 0; column < Internal::kColumnCount; ++column )
    {
        PhysicsSoftBodySkinnedVertex skinned;
        skinned._vertex       = column;
        skinned._arrWeight[0] = PhysicsSoftBodySkinWeight{ 0, 1.0f };
        skinned._weightCount  = 1;
        skinned._maxDistance  = 0.0f;
        desc._listSkinnedVertex.push_back( skinned );
    }
    const PhysicsSoftBodyHandle softBody = pScene->createSoftBody( desc );
    SW_REQUIRE_TRUE( softBody.isValid() );
    const vector<float4x4> listJoint{ float4x4::makeTrs( float3{ 1.0f, 6.0f, 0.0f }, quaternion{}, float3{ 1.0f, 1.0f, 1.0f } ) };
    pScene->setSoftBodySkinningPose( softBody, listJoint, true );
    for ( uint32 frame = 0; frame < 120; ++frame )
    {
        pScene->setSoftBodySkinningPose( softBody, listJoint, false );
        pScene->step( Internal::kFrame );
    }
    vector<float3> listVertex;
    SW_REQUIRE_TRUE( pScene->getSoftBodyVertices( softBody, listVertex ) );
    SW_EXPECT_NEAR( 1.0f, listVertex[0]._x, 0.02f );
    SW_EXPECT_NEAR( 6.0f, listVertex[0]._y, 0.02f );
    SW_EXPECT_TRUE( listVertex[Internal::kLastVertex]._y < 5.8f ); // 나머지는 매달린다

    pScene->setSoftBodySkinningPose( softBody, vector<float4x4>{}, false ); // 뼈 수가 다르다
}

/** @brief [PhysicsSoftBodyTest] 지운 핸들은 무효이고 정점을 주지 않는다. 다시 만든 바디가 같은 칸을 써도 옛 핸들은 살아나지 않는다. 반복 수는 1 이상이다 */
SW_TEST_CASE( PhysicsSoftBodyTest, DestroyedHandleStaysDead )
{
    SW_EXPECT_LOG( "JoltPhysicsScene.SoftBodyIterationCount" );
    const unique_ptr<IPhysicsScene3D> pScene = Internal::createScene();
    SW_REQUIRE_NOT_NULL( pScene.get() );
    const PhysicsSoftBodyHandle first = pScene->createSoftBody( Internal::makeSheet( float3{ 0.0f, 5.0f, 0.0f }, true ) );
    SW_REQUIRE_TRUE( pScene->isSoftBodyValid( first ) );
    pScene->setSoftBodyIterationCount( first, 0 ); // 거절한다
    pScene->setSoftBodyIterationCount( first, 2 );
    pScene->destroySoftBody( first );
    SW_EXPECT_FALSE( pScene->isSoftBodyValid( first ) );
    const PhysicsSoftBodyHandle second = pScene->createSoftBody( Internal::makeSheet( float3{ 0.0f, 5.0f, 0.0f }, true ) );
    SW_EXPECT_TRUE( pScene->isSoftBodyValid( second ) );
    SW_EXPECT_FALSE( pScene->isSoftBodyValid( first ) );
    vector<float3> listVertex{ float3{} };
    SW_EXPECT_FALSE( pScene->getSoftBodyVertices( first, listVertex ) );
    SW_EXPECT_TRUE( listVertex.empty() );
    pScene->destroySoftBody( first ); // 다시 지워도 새 바디는 그대로다
    SW_EXPECT_TRUE( pScene->isSoftBodyValid( second ) );
}
