// FractureLeafShapeTableTest — 파괴 잎 셰이프를 조건이 같은 파괴물끼리 나눠 쓴다. 디바이스 없음(nogpu).
#include "pch.h"

#include "Core/Container/vector.h"
#include "Core/Testing/TestFramework.h"

#include "Engine/Destruction/Fracture/FractureAsset.h"
#include "Engine/Destruction/Fracture/FractureLeafShapeTable.h"
#include "Engine/Destruction/Fracture/FractureSettings.h"
#include "Engine/Destruction/Fracture/PolygonFracture.h"
#include "Engine/Foundation/EngineSubsystems.h"
#include "Engine/Object/GameObject/GameObjectManager.h"
#include "Engine/Physics/IPhysicsScene.h"
#include "Engine/Physics/PhysicsSystem.h"

using namespace sw;

namespace
{
    struct TestFractureLeafShapeTableInternal
    {
        /** @brief 네모 하나를 여섯 조각으로 쪼갠 에셋입니다. 쪼개지 못하면 nullptr 입니다. */
        static shared_ptr<const FractureAsset> makeAsset()
        {
            const vector<float2> listBorder{
                float2{-1.0f, 0.0f},
                float2{ 1.0f, 0.0f},
                float2{ 1.0f, 2.0f},
                float2{-1.0f, 2.0f}
            };
            FractureSettings settings;
            settings._pieceCount            = 6;
            settings._seed                  = 3;
            shared_ptr<FractureAsset> asset = sw::make_shared<FractureAsset>();
            return PolygonFractureUtil::fracture( listBorder, settings, *asset ).has_value() ? asset : nullptr;
        }
    };
} // namespace

/**
 * @brief [FractureLeafShapeTableTest] 에셋 · 배율 · 축소 · 재질이 같으면 묶음 하나를 나눠 쓰고, 하나라도 다르면 다른 묶음이며, 마지막으로 놓으면 표에서 사라진다
 * @details 예전에는 파괴물마다 잎 셰이프를 따로 지어 같은 벽 수백 장이 같은 껍질을 수백 벌 만들었다.
 */
SW_TEST_CASE( FractureLeafShapeTableTest, SameConditionsShareOneSet )
{
    const shared_ptr<const FractureAsset> asset = TestFractureLeafShapeTableInternal::makeAsset();
    SW_REQUIRE_TRUE( asset != nullptr );
    const PhysicsSystem&              system   = engine::getSubsystem<PhysicsSystem>();
    const unique_ptr<IPhysicsScene2D> pScene2D = system.createScene2D();
    const unique_ptr<IPhysicsScene2D> pOther2D = system.createScene2D();
    SW_REQUIRE_NOT_NULL( pScene2D.get() );
    SW_REQUIRE_NOT_NULL( pOther2D.get() );
    GameObjectManager       manager;
    FractureLeafShapeTable& table = manager.getSubsystem<FractureLeafShapeTable>();
    const hashed_string     stone{ "Stone" };

    shared_ptr<FractureLeafShapeSet> first  = table.acquire( *pScene2D, asset, 1.0f, 0.0f, stone );
    shared_ptr<FractureLeafShapeSet> second = table.acquire( *pScene2D, asset, 1.0f, 0.0f, stone );
    SW_REQUIRE_NOT_NULL( first.get() );
    SW_EXPECT_TRUE( first == second );
    SW_EXPECT_FALSE( first == table.acquire( *pScene2D, asset, 2.0f, 0.0f, stone ) );
    SW_EXPECT_FALSE( first == table.acquire( *pScene2D, asset, 1.0f, 0.1f, stone ) );
    SW_EXPECT_FALSE( first == table.acquire( *pScene2D, asset, 1.0f, 0.0f, hashed_string( "Wood" ) ) );
    SW_EXPECT_FALSE( first == table.acquire( *pScene2D, TestFractureLeafShapeTableInternal::makeAsset(), 1.0f, 0.0f, stone ) );
    SW_EXPECT_FALSE( first == table.acquire( *pOther2D, asset, 1.0f, 0.0f, stone ) );
    SW_EXPECT_EQUAL( table.countLive(), size_t( 1 ) );

    first.reset();
    SW_EXPECT_EQUAL( table.countLive(), size_t( 1 ) );
    second.reset();
    SW_EXPECT_EQUAL( table.countLive(), size_t( 0 ) );
}

/**
 * @brief [FractureLeafShapeTableTest] 한 파괴물이 지은 잎 셰이프를 다른 파괴물이 그대로 받는다 — 미리 짓기도 묶음에 한 번이다
 */
SW_TEST_CASE( FractureLeafShapeTableTest, ShapesAreBuiltOncePerSet )
{
    const shared_ptr<const FractureAsset> asset = TestFractureLeafShapeTableInternal::makeAsset();
    SW_REQUIRE_TRUE( asset != nullptr );
    const unique_ptr<IPhysicsScene2D> pScene2D = engine::getSubsystem<PhysicsSystem>().createScene2D();
    SW_REQUIRE_NOT_NULL( pScene2D.get() );
    GameObjectManager                      manager;
    FractureLeafShapeTable&                table     = manager.getSubsystem<FractureLeafShapeTable>();
    const uint32                           leafCount = asset->getPieceCount();
    const shared_ptr<FractureLeafShapeSet> first     = table.acquire( *pScene2D, asset, 1.0f, 0.0f, hashed_string( "Stone" ) );
    const shared_ptr<FractureLeafShapeSet> second    = table.acquire( *pScene2D, asset, 1.0f, 0.0f, hashed_string( "Stone" ) );
    SW_REQUIRE_NOT_NULL( first.get() );
    SW_REQUIRE_NOT_NULL( second.get() );
    SW_EXPECT_EQUAL( first->getPendingCount(), leafCount );

    const PhysicsShapeHandle shape = first->getOrCreateShape( 0 );
    SW_EXPECT_TRUE( shape.isValid() );
    SW_EXPECT_TRUE( second->getOrCreateShape( 0 ) == shape );
    SW_EXPECT_EQUAL( second->getPendingCount(), leafCount - 1 );
    SW_EXPECT_EQUAL( first->warm( 0 ), leafCount - 1 );
    SW_EXPECT_EQUAL( second->getPendingCount(), 0u );
    SW_EXPECT_EQUAL( second->warm( 0 ), 0u );
}
