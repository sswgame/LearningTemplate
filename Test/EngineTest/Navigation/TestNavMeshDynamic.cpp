// 내비메시 동적 변경 — 움직이는 장애물이 닿은 타일만 워커에서 재베이크해 경로를 바꾸고, 기하 소스(파괴)가 바뀐 자리를 다시 베이크한다.
#include "pch.h"

#include "Core/Math/MathUtil.h"

#include "Engine/Destruction/FractureComponent.h"
#include "Engine/Navigation/INavMesh.h"
#include "Engine/Navigation/NavMeshGeometry.h"
#include "Engine/Object/Component/3D/MeshComponent.h"
#include "Engine/Object/Component/Navigation/NavMeshObstacleComponent.h"
#include "Engine/Object/Component/Navigation/NavMeshSurfaceComponent.h"
#include "Engine/Object/Component/SceneComponent.h"
#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManager.h"
#include "Engine/Object/GameObject/SceneNavigation.h"
#include "Engine/Resource/ResourceUtil.h"
#include "Engine/Scene/Scene.h"
#include "Engine/Scene/SceneDocument.h"

#include "EngineTest/NavMeshTestUtil.h"

#include "TestFramework/TestFramework.h"

namespace sw
{
    /** @brief 켜고 끌 수 있는 기둥 하나를 내는 기하 소스입니다(파괴 오브젝트의 자리). */
    class MockPillarGeometrySource final : public INavGeometrySource
    {
    public:
        const GameObject* _pOwner{ nullptr };
        bool              _bPresent{ true };

        const GameObject* getNavGeometryOwner() const override { return _pOwner; }
        bool              overridesOwnerGeometry() const override { return true; }
        void              collectNavGeometry( NavMeshGeometry& outGeometry, uint8 area ) const override
        {
            if ( _bPresent )
                outGeometry.addBox( float3{ 1.0f, 1.0f, 6.0f }, float4x4::makeTrs( float3{ 0.0f, 1.0f, 0.0f }, float3{}, float3{ 1.0f, 1.0f, 1.0f } ), area );
        }
    };
} // namespace sw

namespace
{
    struct NavMeshDynamicTestInternal
    {
        static constexpr float32 kFrame = 1.0f / 60.0f;

        static void spawnFloorAndSurface( sw::GameObjectManager& manager )
        {
            sw::GameObject*    pFloor = manager.createGameObject( sw::hashed_string( "Floor" ) );
            sw::MeshComponent* pMesh  = pFloor->addComponent<sw::MeshComponent>();
            pMesh->setMeshID( "Cube" );
            pMesh->setLocalPosition( sw::float3{ 0.0f, -0.5f, 0.0f } );
            pMesh->setLocalScale( sw::float3{ 30.0f, 1.0f, 30.0f } );
            sw::GameObject* pSurfaceObject = manager.createGameObject( sw::hashed_string( "NavSurface" ) );
            pSurfaceObject->addComponent<sw::NavMeshSurfaceComponent>()->setAgentTypes( { sw::hashed_string( "TestHumanoid" ) } );
        }

        static float32 findPathLength( const sw::SceneNavigation& navigation )
        {
            sw::NavPath path;
            if ( navigation.findPath( sw::hashed_string( "TestHumanoid" ), sw::float3{ -8.0f, 0.0f, 0.0f }, sw::float3{ 8.0f, 0.0f, 0.0f }, path ) !=
                 sw::NavPathStatus::Complete )
                return -1.0f;
            return path.computeLength();
        }

        static void tickAndFlush( sw::GameObjectManager& manager )
        {
            manager.tick( kFrame );
            manager.getSceneNavigation().flushTileBakes();
            manager.tick( kFrame );
        }
    };
} // namespace

/**
 * @brief [NavMeshDynamicTest] 장애물 — 놓으면 닿은 타일만 재베이크해 경로가 돌아가고, 문턱보다 작게 움직이면 재베이크하지 않고, 비키거나 사라지면 곧은 경로로 돌아온다
 */
SW_TEST_CASE( NavMeshDynamicTest, ObstacleRebakesOnlyTheTouchedTilesAndPathsFollow )
{
    using Internal = NavMeshDynamicTestInternal;
    sw::GameObjectManager manager;
    sw::SceneNavigation&  navigation = manager.getSceneNavigation();
    navigation.setSettings( navtest::makeSettings() );
    Internal::spawnFloorAndSurface( manager );
    manager.beginPlay();
    manager.tick( Internal::kFrame );
    const sw::INavMesh* pNavMesh = navigation.findNavMesh( sw::hashed_string( "TestHumanoid" ) );
    SW_ASSERT_NOT_NULL( pNavMesh );
    const uint32 tileCount = static_cast<uint32>( pNavMesh->getTileGrid().getTileCount() );
    SW_EXPECT_NEAR_EQUAL( 16.0f, Internal::findPathLength( navigation ), 0.1f );

    sw::GameObject*               pBlock    = manager.createGameObject( sw::hashed_string( "Block" ) );
    sw::SceneComponent*           pRoot     = pBlock->addComponent<sw::SceneComponent>();
    sw::NavMeshObstacleComponent* pObstacle = pBlock->addComponent<sw::NavMeshObstacleComponent>();
    pObstacle->setHalfExtents( sw::float3{ 1.0f, 1.0f, 6.0f } );
    pObstacle->setCenter( sw::float3{ 0.0f, 1.0f, 0.0f } );
    Internal::tickAndFlush( manager );
    const uint32  rebakedAfterPlace = navigation.getRebakedTileCount();
    const float32 detour            = Internal::findPathLength( navigation );
    SW_EXPECT_TRUE( rebakedAfterPlace > 0 );
    SW_EXPECT_TRUE_MSG( rebakedAfterPlace < tileCount, ( std::to_string( rebakedAfterPlace ) + " of " + std::to_string( tileCount ) ).c_str() );
    SW_EXPECT_TRUE_MSG( detour > 18.0f, ( "detour " + std::to_string( detour ) ).c_str() );

    // 문턱(0.25 m)보다 작게 움직이면 그대로다.
    pRoot->setLocalPosition( sw::float3{ 0.1f, 0.0f, 0.0f } );
    Internal::tickAndFlush( manager );
    SW_EXPECT_EQUAL( rebakedAfterPlace, navigation.getRebakedTileCount() );

    // 길 밖으로 비키면 옛 자리 · 새 자리만 다시 베이크하고 곧은 경로가 돌아온다.
    pRoot->setLocalPosition( sw::float3{ 0.0f, 0.0f, 12.0f } );
    Internal::tickAndFlush( manager );
    SW_EXPECT_TRUE( navigation.getRebakedTileCount() > rebakedAfterPlace );
    SW_EXPECT_NEAR_EQUAL( 16.0f, Internal::findPathLength( navigation ), 0.1f );

    // 다시 길 위로 와서 사라지면 구멍도 사라진다.
    pRoot->setLocalPosition( sw::float3{ 0.0f, 0.0f, 0.0f } );
    Internal::tickAndFlush( manager );
    SW_EXPECT_TRUE( Internal::findPathLength( navigation ) > 18.0f );
    manager.destroyObject( pBlock );
    Internal::tickAndFlush( manager );
    SW_EXPECT_NEAR_EQUAL( 16.0f, Internal::findPathLength( navigation ), 0.1f );
    manager.endPlay();
}

/**
 * @brief [NavMeshDynamicTest] 기하 소스 — 소스가 낸 기둥은 경로를 돌게 하고, 기둥을 치우고 그 자리를 알리면(`invalidateArea`) 그 타일을 다시 모아 베이크해 곧은 경로가 된다
 */
SW_TEST_CASE( NavMeshDynamicTest, GeometrySourceChangeRebakesThatArea )
{
    using Internal = NavMeshDynamicTestInternal;
    sw::GameObjectManager manager;
    sw::SceneNavigation&  navigation = manager.getSceneNavigation();
    navigation.setSettings( navtest::makeSettings() );
    Internal::spawnFloorAndSurface( manager );
    sw::GameObject*              pPillar = manager.createGameObject( sw::hashed_string( "Pillar" ) );
    sw::MockPillarGeometrySource source;
    source._pOwner = pPillar;
    navigation.registerGeometrySource( &source );
    SW_ASSERT_NOT_NULL( navigation.ensureNavMesh( sw::hashed_string( "TestHumanoid" ) ) );
    SW_EXPECT_TRUE( Internal::findPathLength( navigation ) > 18.0f );

    source._bPresent = false;
    navigation.invalidateArea( sw::AABB{
                                   sw::float3{-1.0f, 0.0f, -6.0f},
                                   sw::float3{ 1.0f, 2.0f,  6.0f}
    },
                               true );
    navigation.flushTileBakes();
    SW_EXPECT_TRUE( navigation.getRebakedTileCount() > 0 );
    SW_EXPECT_NEAR_EQUAL( 16.0f, Internal::findPathLength( navigation ), 0.1f );
    navigation.unregisterGeometrySource( &source );
}

/**
 * @brief [NavMeshDynamicTest] 파괴 쇼케이스 — 드럼통이 벽을 부수면 벽(붙어 있는 조각만) 자리의 타일이 다시 베이크되고, 떨어진 덩어리 · 파편은 내비메시를 뚫지 않아 벽 앞뒤가 여전히 이어진다
 */
SW_TEST_CASE( NavMeshDynamicTest, DestructionRebakesTheBrokenWallTiles )
{
    using Internal = NavMeshDynamicTestInternal;
    SW_ASSERT_TRUE( sw::ResourceUtil::initialize() );
    sw::SceneDocument doc;
    SW_ASSERT_TRUE( doc.loadXML( "game/empty/maps/destructionshowcase.scene.xml" ) );
    sw::Scene scene{ "NavDestruction" };
    SW_ASSERT_TRUE( scene.instantiate( doc ) );
    sw::GameObjectManager& manager    = *scene.getObjectManager();
    sw::SceneNavigation&   navigation = manager.getSceneNavigation();
    manager.beginPlay();
    for ( uint32 frame = 0; frame < 60; ++frame )
    {
        manager.tick( Internal::kFrame );
    }
    const sw::INavMesh* pNavMesh = navigation.findNavMesh( sw::hashed_string{} );
    SW_ASSERT_NOT_NULL( pNavMesh );
    SW_EXPECT_EQUAL( 0u, navigation.getRebakedTileCount() );

    for ( uint32 frame = 0; frame < 240; ++frame )
    {
        manager.tick( Internal::kFrame );
    }
    navigation.flushTileBakes();
    const sw::GameObject* pWall = manager.findGameObjectByName( sw::hashed_string( "BrickWall" ) );
    SW_ASSERT_NOT_NULL( pWall );
    SW_ASSERT_TRUE( pWall->getComponent<sw::FractureComponent>()->isFractured() );
    SW_EXPECT_TRUE( navigation.getRebakedTileCount() > 0 );
    SW_EXPECT_TRUE( navigation.getRebakedTileCount() < static_cast<uint32>( pNavMesh->getTileGrid().getTileCount() ) * 4 );
    sw::NavPath path;
    SW_EXPECT_TRUE( navigation.findPath( sw::hashed_string{}, sw::float3{ 0.0f, 0.0f, -4.0f }, sw::float3{ 0.0f, 0.0f, 4.0f }, path ) ==
                    sw::NavPathStatus::Complete );
    manager.endPlay();
}
