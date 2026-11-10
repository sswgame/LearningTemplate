// SceneSpatialGridTest — 씬 공간 색인이 움직인 항목만 버퍼마다 고쳐 넣고(증분), 일이 도는 동안 질의가 앞 버퍼를 읽으며(이중 버퍼), 뺀 항목을 바로 숨기는지 본다(NEXT 5-3 6).
#include "pch.h"

#include "Core/Testing/TestFramework.h"

#include "Engine/Object/Component/SceneComponent.h"
#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManager.h"
#include "Engine/Object/GameObject/Service/SceneSpatialGrid.h"

using namespace sw;

namespace
{
    const SpatialGridLayerDesc kTestLayer{ "SpatialGridTest", SpatialGridPlane::XZ, 1 };

    /** @brief 한 프레임의 색인 몫(넘기기 → 마치기)을 돌리고 그동안 고쳐 넣은 항목 수를 돌려줍니다. */
    uint64 runGridFrame( SceneSpatialGrid& grid )
    {
        const uint64 before = grid.getAppliedChangeCount();
        grid.update( 1.0f / 60.0f );
        grid.completeFrameJob();
        return grid.getAppliedChangeCount() - before;
    }

    SceneComponent* makePoint( GameObjectManager& manager, const char* pName, const float3& position )
    {
        GameObject*     pObject = manager.createGameObject( hashed_string( pName ) );
        SceneComponent* pScene  = pObject->addComponent<SceneComponent>();
        pScene->setLocalPosition( position );
        return pScene;
    }

    bool contains( const vector<Component*>& listKey, const Component* pKey )
    {
        for ( const Component* pFound : listKey )
        {
            if ( pFound == pKey )
                return true;
        }
        return false;
    }
} // namespace

/** @brief [SceneSpatialGridTest] 움직인 항목만 버퍼마다 한 번씩 고쳐 넣고, 아무것도 움직이지 않으면 고칠 것이 없다 */
SW_TEST_CASE( SceneSpatialGridTest, IncrementalUpdateTouchesOnlyMovedEntries )
{
    GameObjectManager manager;
    SceneSpatialGrid  grid{ manager };
    SceneComponent*   pA = makePoint( manager, "A", float3{ 0.0f, 0.0f, 0.0f } );
    SceneComponent*   pB = makePoint( manager, "B", float3{ 10.0f, 0.0f, 0.0f } );
    SceneComponent*   pC = makePoint( manager, "C", float3{ 20.0f, 0.0f, 0.0f } );
    (void)grid.add( kTestLayer, *pA, *pA, 0.5f );
    (void)grid.add( kTestLayer, *pB, *pB, 0.5f );
    (void)grid.add( kTestLayer, *pC, *pC, 0.5f );
    SW_EXPECT_FALSE( grid.isActive( kTestLayer ) ); // 사용자가 없다
    grid.addLayerReader( kTestLayer );
    grid.requireCellSize( kTestLayer, 4.0f );
    SW_REQUIRE_TRUE( grid.isActive( kTestLayer ) ); // 앞 버퍼는 바로 짓는다

    vector<Component*> listKey;
    grid.query( kTestLayer, 9.0f, -1.0f, 11.0f, 1.0f, listKey );
    SW_REQUIRE_EQUAL( static_cast<size_t>( 1 ), listKey.size() );
    SW_EXPECT_TRUE( listKey[0] == pB );

    SW_EXPECT_EQUAL( static_cast<uint64>( 3 ), runGridFrame( grid ) ); // 뒤 버퍼는 처음 짓는다
    SW_EXPECT_EQUAL( static_cast<uint64>( 0 ), runGridFrame( grid ) ); // 두 버퍼 모두 맞았다

    pA->setLocalPosition( float3{ 30.0f, 0.0f, 0.0f } );
    SW_EXPECT_EQUAL( static_cast<uint64>( 1 ), runGridFrame( grid ) ); // 뒤 버퍼(A) 몫
    SW_EXPECT_EQUAL( static_cast<uint64>( 1 ), runGridFrame( grid ) ); // 다른 버퍼(B) 몫 — 버퍼마다 자기가 본 번호로 고른다
    SW_EXPECT_EQUAL( static_cast<uint64>( 0 ), runGridFrame( grid ) );

    grid.query( kTestLayer, -100.0f, -1.0f, 100.0f, 1.0f, listKey );
    SW_REQUIRE_EQUAL( static_cast<size_t>( 3 ), listKey.size() );
    SW_EXPECT_TRUE( listKey[0] == pA && listKey[1] == pB && listKey[2] == pC ); // 올린 순서다
    grid.query( kTestLayer, 29.0f, -1.0f, 31.0f, 1.0f, listKey );
    SW_EXPECT_TRUE( listKey.size() == 1 && listKey[0] == pA );
}

/** @brief [SceneSpatialGridTest] 일을 넘긴 뒤 마치기 전에는 질의가 앞 버퍼(옛 자리)를 보고, 마친 뒤에 새 자리를 본다 */
SW_TEST_CASE( SceneSpatialGridTest, QueriesReadFrontBufferUntilJobCompletes )
{
    GameObjectManager manager;
    SceneSpatialGrid  grid{ manager };
    SceneComponent*   pMover = makePoint( manager, "Mover", float3{ 0.0f, 0.0f, 0.0f } );
    (void)grid.add( kTestLayer, *pMover, *pMover, 0.0f );
    grid.addLayerReader( kTestLayer );
    grid.requireCellSize( kTestLayer, 2.0f );
    (void)runGridFrame( grid );

    pMover->setLocalPosition( float3{ 50.0f, 0.0f, 0.0f } );
    grid.update( 1.0f / 60.0f ); // 뒤 버퍼는 새 자리다(워커가 없으면 지금 맞춘다)
    vector<Component*> listKey;
    grid.query( kTestLayer, -1.0f, -1.0f, 1.0f, 1.0f, listKey );
    SW_EXPECT_EQUAL( static_cast<size_t>( 1 ), listKey.size() ); // 틱이 읽는 앞 버퍼는 그대로다
    grid.query( kTestLayer, 49.0f, -1.0f, 51.0f, 1.0f, listKey );
    SW_EXPECT_TRUE( listKey.empty() );

    grid.completeFrameJob();
    grid.query( kTestLayer, 49.0f, -1.0f, 51.0f, 1.0f, listKey );
    SW_EXPECT_EQUAL( static_cast<size_t>( 1 ), listKey.size() );
    grid.query( kTestLayer, -1.0f, -1.0f, 1.0f, 1.0f, listKey );
    SW_EXPECT_TRUE( listKey.empty() );
}

/** @brief [SceneSpatialGridTest] 더하고 뺀 항목은 앞 버퍼에서 바로 보이고 사라지며, 뺀 칸은 두 버퍼에서 빠진 뒤 새 세대로 다시 쓴다 */
SW_TEST_CASE( SceneSpatialGridTest, AddAndRemoveAreVisibleImmediately )
{
    GameObjectManager manager;
    SceneSpatialGrid  grid{ manager };
    SceneComponent*   pFirst  = makePoint( manager, "First", float3{ 0.0f, 0.0f, 0.0f } );
    SceneComponent*   pSecond = makePoint( manager, "Second", float3{ 0.0f, 0.0f, 0.0f } );
    grid.addLayerReader( kTestLayer );
    grid.requireCellSize( kTestLayer, 2.0f );
    SlotHandle         first = grid.add( kTestLayer, *pFirst, *pFirst, 0.0f );
    vector<Component*> listKey;
    grid.query( kTestLayer, -1.0f, -1.0f, 1.0f, 1.0f, listKey );
    SW_EXPECT_TRUE( contains( listKey, pFirst ) );

    const SlotHandle stale = first;
    grid.remove( first );
    SW_EXPECT_FALSE( first.isValid() );
    grid.query( kTestLayer, -1.0f, -1.0f, 1.0f, 1.0f, listKey );
    SW_EXPECT_TRUE( listKey.empty() );

    (void)runGridFrame( grid ); // 뒤 버퍼에서도 빼고 칸을 놓는다
    const SlotHandle second = grid.add( kTestLayer, *pSecond, *pSecond, 0.0f );
    SW_EXPECT_EQUAL( stale.index(), second.index() ); // 칸을 다시 쓴다
    SW_EXPECT_TRUE( stale.generation() != second.generation() );
    SlotHandle staleCopy = stale;
    grid.remove( staleCopy ); // 옛 핸들은 새 주인을 빼지 않는다
    (void)runGridFrame( grid );
    (void)runGridFrame( grid );
    grid.query( kTestLayer, -1.0f, -1.0f, 1.0f, 1.0f, listKey );
    SW_EXPECT_TRUE( listKey.size() == 1 && listKey[0] == pSecond );
}
