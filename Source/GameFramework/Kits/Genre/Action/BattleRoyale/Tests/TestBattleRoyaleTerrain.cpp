// BattleRoyaleTerrainTest — 판이 씬의 정적 바디에 묻는 것: 걷기는 벽에서 멈추고 따라 미끄러지며, 시야는 벽 뒤를 보지 못하고 낮은 엄폐 너머는 보고, 건물 안은 설 자리 · 원 중심이 아니다(nogpu, 에셋 없음).
#include "pch.h"

#include "Core/Testing/TestFramework.h"

#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManager.h"
#include "Engine/Physics/Collision/PhysicsShape.h"
#include "Engine/Physics/Component/RigidBodyComponent.h"

#include "GameFramework/Kits/Genre/Action/BattleRoyale/BattleRoyaleSceneTerrain.h"

using namespace sw;

namespace
{
    constexpr float32 kBattleRoyaleTerrainMapSize = 100.0f;
    constexpr float32 kBattleRoyaleTerrainFrame   = 1.0f / 60.0f;

    /** @brief 땅(윗면 0)과 건물 하나(x 20..24, z 20..40)가 있는 판입니다. 건물의 높이는 시험이 정한다. */
    struct BattleRoyaleTerrainTest
    {
        GameObjectManager        _manager{};
        BattleRoyaleSceneTerrain _terrain{};

        explicit BattleRoyaleTerrainTest( float32 buildingHeight )
        {
            addBox( "Ground", float3{ 50.0f, -0.5f, 50.0f }, float3{ 70.0f, 0.5f, 70.0f } );
            addBox( "Building", float3{ 22.0f, buildingHeight * 0.5f, 30.0f }, float3{ 2.0f, buildingHeight * 0.5f, 10.0f } );
            _terrain.initialize( &_manager, BattleRoyaleBodySettings{} );
            _manager.beginPlay();
            _manager.update( kBattleRoyaleTerrainFrame ); // 첫 물리 프레임이 바디를 짓는다
        }

        ~BattleRoyaleTerrainTest() { _manager.endPlay(); }

        void addBox( const utf8* pName, const float3& center, const float3& halfExtents )
        {
            RigidBodyComponent* pBody = _manager.createGameObject( hashed_string( pName ) )->addComponent<RigidBodyComponent>();
            PhysicsShapeDesc3D  box;
            box._type        = PhysicsShapeType3D::Box;
            box._halfExtents = halfExtents;
            pBody->setShape( box );
            pBody->setBodyType( PhysicsBodyType::Static );
            pBody->setLocalPosition( center );
        }
    };
} // namespace

/** @brief [BattleRoyaleTerrainTest] 건물로 곧장 걸으면 몸 반지름 앞에서 멈추고, 비스듬히 걸으면 벽을 따라 미끄러지며, 멀어지는 걸음은 막히지 않는다 */
SW_TEST_CASE( BattleRoyaleTerrainTest, WalkStopsAtTheBuildingAndSlides )
{
    const BattleRoyaleTerrainTest test( 3.0f );
    const float32                 wall    = 20.0f - BattleRoyaleBodySettings{}._radius;
    const float2                  stopped = test._terrain.walk( float2{ 18.0f, 30.0f }, float2{ 10.0f, 0.0f } );
    SW_EXPECT_NEAR( wall, stopped._x, 0.05f );
    SW_EXPECT_NEAR( 30.0f, stopped._y, 0.05f );
    const float2 sliding = test._terrain.walk( float2{ 18.0f, 30.0f }, float2{ 4.0f, 4.0f } );
    SW_EXPECT_NEAR( wall, sliding._x, 0.05f );
    SW_EXPECT_TRUE( sliding._y > 33.0f );
    const float2 away = test._terrain.walk( stopped, float2{ -3.0f, 0.0f } );
    SW_EXPECT_NEAR( stopped._x - 3.0f, away._x, 0.05f );
    const float2 open = test._terrain.walk( float2{ 60.0f, 60.0f }, float2{ 5.0f, -2.0f } );
    SW_EXPECT_NEAR( 65.0f, open._x, 0.001f );
    SW_EXPECT_NEAR( 58.0f, open._y, 0.001f );
}

/** @brief [BattleRoyaleTerrainTest] 높은 건물은 시야를 막고, 눈높이보다 낮은 엄폐 너머는 보인다 */
SW_TEST_CASE( BattleRoyaleTerrainTest, TallBuildingBlocksSightAndLowCoverDoesNot )
{
    const float2 left{ 15.0f, 30.0f };
    const float2 right{ 30.0f, 30.0f };
    {
        const BattleRoyaleTerrainTest tall( 3.0f );
        SW_EXPECT_FALSE( tall._terrain.hasLineOfSight( left, right ) );
        SW_EXPECT_TRUE( tall._terrain.hasLineOfSight( left, float2{ 15.0f, 60.0f } ) );
    }
    const BattleRoyaleTerrainTest low( BattleRoyaleBodySettings{}._eyeHeight * 0.5f );
    SW_EXPECT_TRUE( low._terrain.hasLineOfSight( left, right ) );
    SW_EXPECT_FALSE( low._terrain.isClear( float2{ 22.0f, 30.0f } ) );
}

/** @brief [BattleRoyaleTerrainTest] 건물 안은 원 중심 · 보급 자리가 아니고, 그리로 내린 사람은 가장 가까운 바깥에 선다 */
SW_TEST_CASE( BattleRoyaleTerrainTest, InsideTheBuildingIsNotAStandingSpot )
{
    const BattleRoyaleTerrainTest test( 3.0f );
    const float2                  inside{ 21.0f, 30.0f };
    const float2                  outside{ 50.0f, 50.0f };
    SW_EXPECT_FALSE( test._terrain.isZoneCenterAllowed( inside ) );
    SW_EXPECT_TRUE( test._terrain.isZoneCenterAllowed( outside ) );
    const float2 spot = test._terrain.findStandingSpot( inside, kBattleRoyaleTerrainMapSize );
    SW_EXPECT_TRUE( test._terrain.isClear( spot ) );
    SW_EXPECT_TRUE( float2::getDistance( inside, spot ) < 2.5f );
    const float2 kept = test._terrain.findStandingSpot( outside, kBattleRoyaleTerrainMapSize );
    SW_EXPECT_EQUAL( outside._x, kept._x );
    SW_EXPECT_EQUAL( outside._y, kept._y );
}

/** @brief [BattleRoyaleTerrainTest] 바디 · 내비메시가 없는 씬은 막지 않고, 길은 목적지 하나다 */
SW_TEST_CASE( BattleRoyaleTerrainTest, EmptySceneBlocksNothing )
{
    GameObjectManager        manager;
    BattleRoyaleSceneTerrain terrain;
    terrain.initialize( &manager, BattleRoyaleBodySettings{} );
    SW_EXPECT_TRUE( terrain.isZoneCenterAllowed( float2{ 1.0f, 1.0f } ) );
    SW_EXPECT_TRUE( terrain.hasLineOfSight( float2{ 0.0f, 0.0f }, float2{ 10.0f, 0.0f } ) );
    const float2 walked = terrain.walk( float2{ 1.0f, 1.0f }, float2{ 2.0f, 0.0f } );
    SW_EXPECT_EQUAL( 3.0f, walked._x );
    vector<float2> listPoint;
    terrain.findRoute( float2{ 1.0f, 1.0f }, float2{ 9.0f, 4.0f }, listPoint );
    SW_REQUIRE_EQUAL( 1u, static_cast<uint32>( listPoint.size() ) );
    SW_EXPECT_EQUAL( 9.0f, listPoint[0]._x );
}
