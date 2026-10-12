#include "pch.h"

#include "Core/Math/MathUtil.h"

#include "Engine/Environment/Terrain/HeightfieldData.h"
#include "Engine/Environment/Terrain/TerrainColliderComponent.h"
#include "Engine/Environment/Terrain/TerrainComponent.h"
#include "Engine/Environment/Terrain/TerrainHeightfield.h"
#include "Engine/Object/Component/Physics/RigidBodyComponent.h"
#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManager.h"
#include "Engine/Physics/Collision/PhysicsShape.h"

#include "TestFramework/TestFramework.h"

// 지형 충돌 — 높이장 → 삼각형 메시 셰이프(높이 질의와 같은 삼각형 · 구멍 셀은 비운다) · 지형 위에 떨어진 상자가 멈추고 구멍으로는 빠진다.

using namespace sw;

namespace
{
    struct TestTerrainColliderInternal
    {
        static constexpr float32 kHeightMin = -10.0f;
        static constexpr float32 kHeightMax = 10.0f;
        static constexpr float32 kFrame     = 1.0f / 60.0f;

        static float32 wave( float32 x, float32 z ) { return 3.0f * MathUtil::sin( x * 0.3f ) * MathUtil::cos( z * 0.2f ); }

        /** @brief 칸 1 m · 해상도 @p resolution 의 높이장을 @p surface 로 굽습니다. */
        template <typename TSurface>
        static HeightfieldData makeData( uint32 resolution, const TSurface& surface )
        {
            HeightfieldData data;
            data._resolution = resolution;
            data._listHeight.resize( static_cast<size_t>( resolution ) * resolution );
            for ( uint32 sampleZ = 0; sampleZ < resolution; ++sampleZ )
            {
                for ( uint32 sampleX = 0; sampleX < resolution; ++sampleX )
                {
                    const float32 height  = surface( static_cast<float32>( sampleX ), static_cast<float32>( sampleZ ) );
                    const float32 encoded = ( height - kHeightMin ) / ( kHeightMax - kHeightMin ) * 65535.0f;
                    data._listHeight[static_cast<size_t>( sampleZ ) * resolution + sampleX] =
                        static_cast<uint16>( MathUtil::clamp( MathUtil::round( encoded ), 0.0f, 65535.0f ) );
                }
            }
            return data;
        }

        static float32 flat( float32 x, float32 z )
        {
            (void)x;
            (void)z;
            return 2.0f;
        }

        static RigidBodyComponent* spawnCrate( GameObjectManager& manager, const utf8* pName, const float3& position )
        {
            GameObject* pObject = manager.createGameObject( hashed_string( pName ) );
            if ( pObject == nullptr )
                return nullptr;
            RigidBodyComponent* pBody = pObject->addComponent<RigidBodyComponent>();
            if ( pBody == nullptr )
                return nullptr;
            PhysicsShapeDesc3D box;
            box._halfExtents = float3{ 0.5f, 0.5f, 0.5f };
            pBody->setShape( box );
            pBody->setLocalPosition( position );
            return pBody;
        }
    };
} // namespace

/**
 * @brief [TerrainColliderTest] 충돌 메시는 샘플마다 점 하나, 구멍이 아닌 칸마다 삼각형 둘이며, 삼각형은 높이 질의와 같은 면이고 위를 향한다
 * @details 삼각형의 무게중심 높이가 그 자리의 `findHeightAt` 과 같으면 대각선과 점 순서가 렌더 메시 · 질의와 같다는 뜻이다.
 *          구멍 칸 (5, 3) 의 무게중심은 어느 삼각형에도 들지 않는다.
 */
SW_TEST_CASE( TerrainColliderTest, CollisionMeshMatchesHeightQueriesAndSkipsHoles )
{
    using Internal       = TestTerrainColliderInternal;
    HeightfieldData data = Internal::makeData( 17, &Internal::wave );
    data._listHoleCell.assign( 16 * 16, 0u );
    data._listHoleCell[3 * 16 + 5] = 1u; // 칸 (5, 3)
    TerrainHeightfield field;
    const float3       origin{ 4.0f, 1.0f, -3.0f };
    SW_ASSERT_TRUE( field.initialize( data, origin, float2{ 16.0f, 16.0f }, Internal::kHeightMin, Internal::kHeightMax ) );

    PhysicsShapeDesc3D shape;
    SW_ASSERT_TRUE( TerrainCollisionUtil::makeCollisionMesh( field, shape ) );
    SW_EXPECT_TRUE( shape._type == PhysicsShapeType3D::TriangleMesh );
    SW_EXPECT_EQUAL( 17u * 17u, static_cast<uint32>( shape._listPoint.size() ) );
    SW_ASSERT_EQUAL( ( 16u * 16u - 1u ) * 6u, static_cast<uint32>( shape._listIndex.size() ) );

    float32 worstHeightError{ 0.0f };
    uint32  downwardCount{ 0 };
    uint32  holeHitCount{ 0 };
    for ( size_t index = 0; index + 2 < shape._listIndex.size(); index += 3 )
    {
        const float3& a        = shape._listPoint[shape._listIndex[index]];
        const float3& b        = shape._listPoint[shape._listIndex[index + 1]];
        const float3& c        = shape._listPoint[shape._listIndex[index + 2]];
        const float3  centroid = ( a + b + c ) * ( 1.0f / 3.0f ) + origin;
        float32       height{ 0.0f };
        if ( field.findHeightAt( centroid._x, centroid._z, height ) == false )
        {
            ++holeHitCount;
            continue;
        }
        worstHeightError = MathUtil::max( worstHeightError, MathUtil::abs( height - centroid._y ) );
        if ( ( b - a ).cross( c - a )._y <= 0.0f )
            ++downwardCount;
    }
    SW_EXPECT_TRUE( worstHeightError < 1.0e-4f );
    SW_EXPECT_EQUAL( 0u, downwardCount );
    SW_EXPECT_EQUAL( 0u, holeHitCount );

    TerrainHeightfield empty;
    SW_EXPECT_FALSE( TerrainCollisionUtil::makeCollisionMesh( empty, shape ) );
    SW_EXPECT_TRUE( shape._listIndex.empty() );
}

/**
 * @brief [TerrainColliderTest] 지형 충돌 컴포넌트 — 플레이하면 정적 바디가 생기고, 떨어진 상자는 땅 위에 멈추며 구멍 위의 상자는 빠진다 · 지형을 다시 읽으면 바디를 다시 만든다
 */
SW_TEST_CASE( TerrainColliderTest, CratesRestOnTheTerrainAndFallThroughHoles )
{
    using Internal       = TestTerrainColliderInternal;
    const string    path = test::makeTempPath( "collider.heightfield" );
    HeightfieldData data = Internal::makeData( 17, &Internal::flat );
    data._listHoleCell.assign( 16 * 16, 0u );
    for ( uint32 cellZ = 10; cellZ < 14; ++cellZ )
    {
        for ( uint32 cellX = 10; cellX < 14; ++cellX )
        {
            data._listHoleCell[cellZ * 16 + cellX] = 1u; // 4 m × 4 m 구멍, 가운데 (12, 12)
        }
    }
    SW_ASSERT_TRUE( data.saveToFile( path ) );

    GameObjectManager manager;
    GameObject*       pGround = manager.createGameObject( hashed_string( "Ground" ) );
    SW_ASSERT_NOT_NULL( pGround );
    TerrainComponent* pTerrain = pGround->addComponent<TerrainComponent>();
    SW_ASSERT_NOT_NULL( pTerrain );
    pTerrain->setHeightfieldPath( path );
    pTerrain->setSize( float2{ 16.0f, 16.0f } );
    pTerrain->setHeightRange( Internal::kHeightMin, Internal::kHeightMax );
    pTerrain->setChunkCells( 16 );
    pTerrain->setMaterialPath( "" ); // 그리지 않는 테스트 — 머티리얼을 잡지 않는다
    SW_ASSERT_TRUE( pTerrain->reloadTerrain() );
    TerrainColliderComponent* pCollider = pGround->addComponent<TerrainColliderComponent>();
    SW_ASSERT_NOT_NULL( pCollider );

    RigidBodyComponent* pSolid = Internal::spawnCrate( manager, "OnGround", float3{ 4.0f, 5.0f, 4.0f } );
    RigidBodyComponent* pHole  = Internal::spawnCrate( manager, "OverHole", float3{ 12.0f, 5.0f, 12.0f } );
    SW_ASSERT_NOT_NULL( pSolid );
    SW_ASSERT_NOT_NULL( pHole );

    manager.tick( Internal::kFrame );
    SW_EXPECT_FALSE( pCollider->getBodyHandle().isValid() ); // 시작 전에는 바디가 없다

    manager.beginPlay();
    for ( uint32 frame = 0; frame < 180; ++frame )
    {
        manager.tick( Internal::kFrame );
    }
    SW_EXPECT_TRUE( pCollider->getBodyHandle().isValid() );
    SW_EXPECT_EQUAL( ( 16u * 16u - 16u ) * 2u, pCollider->getCollisionTriangleCount() );
    SW_EXPECT_NEAR_EQUAL( 2.5f, pSolid->getWorldPosition()._y, 0.05f );
    SW_EXPECT_TRUE( pHole->getWorldPosition()._y < 0.0f );

    // 다시 읽으면(세대가 오르면) 다음 물리 프레임에 바디를 새 높이장으로 다시 만든다 — 높이 범위를 올리면 상자가 함께 올라간다.
    const uint32 generation = pTerrain->getHeightfieldGeneration();
    pTerrain->setHeightRange( Internal::kHeightMin + 1.0f, Internal::kHeightMax + 1.0f );
    SW_ASSERT_TRUE( pTerrain->reloadTerrain() );
    SW_EXPECT_TRUE( pTerrain->getHeightfieldGeneration() != generation );
    pSolid->setWorldPosition( float3{ 4.0f, 5.0f, 4.0f } );
    for ( uint32 frame = 0; frame < 180; ++frame )
    {
        manager.tick( Internal::kFrame );
    }
    SW_EXPECT_TRUE( pCollider->getBodyHandle().isValid() );
    SW_EXPECT_NEAR_EQUAL( 3.5f, pSolid->getWorldPosition()._y, 0.05f );
}
