// TerrainColliderTest — 높이를 고친 청크는 바디를 다시 만들지 않고 셰이프만 갈아, 그 위에 놓인 바디의 접촉에 끝 → 시작이 나지 않는다(감사 S-4). 디바이스 없음(nogpu).
// 언리얼 Landscape 의 콜리전 갱신(같은 컴포넌트의 높이장 교체) · Godot HeightMapShape3D 의 map_data 교체와 같은 계약이다.
#include "pch.h"

#include "Core/File/FileUtil.h"
#include "Core/Testing/TestFramework.h"

#include "Engine/Content/Tests/ScopedScratchDomain.h"
#include "Engine/Environment/Terrain/HeightfieldAsset.h"
#include "Engine/Environment/Terrain/TerrainColliderComponent.h"
#include "Engine/Environment/Terrain/TerrainComponent.h"
#include "Engine/Environment/Terrain/TerrainHeightfield.h"
#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManager.inl"
#include "Engine/Physics/Collision/PhysicsContact.h"
#include "Engine/Physics/Collision/PhysicsShape.h"
#include "Engine/Physics/Component/RigidBodyComponent.h"
#include "Engine/Physics/Scene/ScenePhysics.h"

using namespace sw;

namespace
{
    struct TestTerrainColliderInternal
    {
        static constexpr float32 kFrame = 1.0f / 60.0f;
        /** @brief 해상도 — 청크 칸 기본값(32)의 청크 하나다. 크기 32 m 라 칸 하나가 1 m 다. */
        static constexpr uint32 kResolution = 33;

        /** @brief 평평한 높이장(구멍 없음)을 케이스 도메인에 쓰고 그 리소스 id 를 돌려줍니다. */
        static string writeFlatHeightfield( const test::ScopedScratchDomain& domain )
        {
            HeightfieldAsset asset;
            asset._resolution = kResolution;
            asset._listHeight.assign( static_cast<size_t>( kResolution ) * kResolution, uint16{ 0 } );
            vector<uint8> bytes;
            asset.saveToMemory( bytes );
            SW_EXPECT_TRUE( FileUtil::writeFile( domain.makeDiskPath( "flat.heightfield" ), bytes.data(), static_cast<uint64>( bytes.size() ) ) );
            return test::ScopedScratchDomain::makeID( "flat.heightfield" ).str();
        }

        /** @brief 이번 프레임 3D 이벤트 가운데 바디 @p body 가 낀 시작 · 끝 수입니다. */
        static uint32 countEdges( const ScenePhysics& physics, PhysicsBodyHandle body )
        {
            uint32 count = 0;
            for ( const PhysicsContactEvent3D& event : physics.getFrameEvents3D() )
            {
                const bool bInvolved = event._bodyA == body || event._bodyB == body;
                count += ( bInvolved && event._phase != PhysicsContactPhase::Stay ) ? 1u : 0u;
            }
            return count;
        }
    };
} // namespace

/**
 * @brief [TerrainColliderTest] 상자가 놓인 청크의 먼 샘플 높이를 고치면 청크 셰이프는 새로 지어지되(빌드 수 +1) 바디 수는 그대로이고 상자의 접촉은 끝 → 시작을 내지 않는다
 * @details 평평한 32 m 지형(청크 하나) 위 샘플 (8, 8) 에 상자를 얹어 닿은 뒤, 같은 청크의 샘플 (30, 30) 을 3 m 올린다 — 상자 밑 삼각형은 그대로다.
 */
SW_TEST_CASE( TerrainColliderTest, HeightEditKeepsContact )
{
    using Internal = TestTerrainColliderInternal;
    const test::ScopedScratchDomain domain;
    const string                    heightfieldID = Internal::writeFlatHeightfield( domain );
    GameObjectManager               manager;
    GameObject*                     pGround = manager.createGameObject( hashed_string( "Ground" ) );
    SW_REQUIRE_NOT_NULL( pGround );
    TerrainComponent* pTerrain = pGround->addComponent<TerrainComponent>();
    SW_REQUIRE_NOT_NULL( pTerrain );
    pTerrain->setSize( float2{ 32.0f, 32.0f } );
    pTerrain->setHeightfieldPath( heightfieldID );
    SW_REQUIRE_TRUE( pTerrain->reloadTerrain() );
    TerrainColliderComponent* pCollider = pGround->addComponent<TerrainColliderComponent>();
    SW_REQUIRE_NOT_NULL( pCollider );

    const float3 origin = pTerrain->getHeightfield().getOrigin();
    float32      groundHeight{ 0.0f };
    SW_REQUIRE_TRUE( pTerrain->findHeightAt( origin._x + 8.0f, origin._z + 8.0f, groundHeight ) );
    GameObject* pCrateObject = manager.createGameObject( hashed_string( "Crate" ) );
    SW_REQUIRE_NOT_NULL( pCrateObject );
    RigidBodyComponent* pCrate = pCrateObject->addComponent<RigidBodyComponent>();
    SW_REQUIRE_NOT_NULL( pCrate );
    PhysicsShapeDesc3D box;
    box._type        = PhysicsShapeType3D::Box;
    box._halfExtents = float3{ 0.4f, 0.4f, 0.4f };
    pCrate->setShape( box );
    pCrate->setBodyType( PhysicsBodyType::Dynamic );
    pCrate->setLocalPosition( float3{ origin._x + 8.0f, groundHeight + 0.45f, origin._z + 8.0f } );

    const ScenePhysics& physics = manager.getSubsystem<ScenePhysics>();
    manager.beginPlay();
    uint32 edges = 0;
    for ( uint32 frame = 0; frame < 60; ++frame )
    {
        manager.update( Internal::kFrame );
        edges += Internal::countEdges( physics, pCrate->getBodyHandle() );
    }
    SW_REQUIRE_EQUAL( 1u, edges ); // 지형에 닿았다(시작 하나)
    SW_REQUIRE_EQUAL( 1u, pCollider->getBodyCount() );
    const uint32 buildCount = pCollider->getChunkBuildCount();

    constexpr uint32 kSampleX    = 30;
    constexpr uint32 kSampleZ    = 30;
    vector<float32>  listHeight  = pTerrain->getHeightfield().getHeightSamples();
    const size_t     sampleIndex = static_cast<size_t>( kSampleZ ) * Internal::kResolution + kSampleX;
    listHeight[sampleIndex] += 3.0f;
    TerrainSampleRect editedRect{};
    editedRect.include( kSampleX, kSampleZ );
    SW_REQUIRE_TRUE( pTerrain->writeHeights( listHeight, editedRect ) );
    uint32 restart = 0;
    for ( uint32 frame = 0; frame < 5; ++frame )
    {
        manager.update( Internal::kFrame );
        restart += Internal::countEdges( physics, pCrate->getBodyHandle() );
    }
    SW_EXPECT_TRUE( pCollider->getChunkBuildCount() > buildCount ); // 청크 셰이프를 새 높이로 지었다
    SW_EXPECT_EQUAL( 1u, pCollider->getBodyCount() );
    SW_EXPECT_EQUAL( 0u, restart );
}
