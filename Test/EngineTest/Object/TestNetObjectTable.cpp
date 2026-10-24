#include "pch.h"

#include "Core/Container/formatString.h"
#include "Core/Container/unordered_map.h"
#include "Core/String/fixed_string.h"

#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManager.h"
#include "Engine/Object/GameObject/Service/NetObjectTable.h"

#include "TestFramework/TestFramework.h"

using namespace sw;

// NetObjectTableTest — 서버와 클라이언트가 같은 오브젝트를 같은 번호로 부르는 번호표(씬 번호 = 이름 해시, 생성 번호 = 서버 배정 + 클라이언트 묶기).

/**
 * @brief [NetObjectTableTest] 씬 번호는 이름 해시라 만든 순서 · 한쪽에만 있는 오브젝트와 상관없이 같고, 이름이 겹친 번호는 아무도 갖지 않는다
 */
SW_TEST_CASE( NetObjectTableTest, SceneIDsComeFromNamesNotOrder )
{
    GameObjectManager serverManager;
    GameObject*       pServerWall   = serverManager.createGameObject( hashed_string( "Wall" ) );
    GameObject*       pServerBarrel = serverManager.createGameObject( hashed_string( "Barrel" ) );
    SW_ASSERT_TRUE( pServerWall != nullptr && pServerBarrel != nullptr );
    NetObjectTable& serverTable = serverManager.getSubsystem<NetObjectTable>();
    serverTable.captureSceneObjects();

    // 클라이언트는 다른 순서로 만들고, 앞에 클라이언트만의 오브젝트가 있다 — 이름순 번호였다면 모두 밀렸다.
    GameObjectManager clientManager;
    SW_ASSERT_NOT_NULL( clientManager.createGameObject( hashed_string( "AAA_LocalCamera" ) ) );
    GameObject* pClientBarrel = clientManager.createGameObject( hashed_string( "Barrel" ) );
    GameObject* pClientWall   = clientManager.createGameObject( hashed_string( "Wall" ) );
    SW_ASSERT_TRUE( pClientWall != nullptr && pClientBarrel != nullptr );
    NetObjectTable& clientTable = clientManager.getSubsystem<NetObjectTable>();
    clientTable.captureSceneObjects();

    const uint32 wallID = serverTable.findID( *pServerWall );
    SW_EXPECT_TRUE( wallID != 0 && ( wallID & NetObjectTable::kSpawnedBit ) == 0 );
    SW_EXPECT_EQUAL( wallID, clientTable.findID( *pClientWall ) );
    SW_EXPECT_EQUAL( serverTable.findID( *pServerBarrel ), clientTable.findID( *pClientBarrel ) );
    SW_EXPECT_TRUE( clientTable.findObject( wallID ) == pClientWall );

    // 같은 이름으로 만들어도 매니저가 뒤의 것에 번호를 붙여 이름이 갈린다(GameObjectStore) — 씬 번호도 갈리고, 만든 순서가 같으면 양쪽이 같은 번호를 준다.
    // 같은 번호(해시 충돌)는 어느 쪽에도 주지 않는다(captureSceneObjects 의 오류 로그) — 이름으로는 만들 수 없는 경우다.
    GameObjectManager duplicateManager;
    GameObject*       pFirst  = duplicateManager.createGameObject( hashed_string( "Crate" ) );
    GameObject*       pSecond = duplicateManager.createGameObject( hashed_string( "Crate" ) );
    SW_ASSERT_TRUE( pFirst != nullptr && pSecond != nullptr );
    SW_EXPECT_TRUE( pFirst->getName() != pSecond->getName() );
    NetObjectTable& duplicateTable = duplicateManager.getSubsystem<NetObjectTable>();
    duplicateTable.captureSceneObjects();
    const uint32 firstID  = duplicateTable.findID( *pFirst );
    const uint32 secondID = duplicateTable.findID( *pSecond );
    SW_EXPECT_TRUE( firstID != 0u && secondID != 0u && firstID != secondID );
    SW_EXPECT_TRUE( duplicateTable.findObject( firstID ) == pFirst );

    // 잡은 뒤에 생긴 오브젝트는 씬 번호를 받지 않는다.
    GameObject* pLate = serverManager.createGameObject( hashed_string( "LateArrow" ) );
    SW_ASSERT_NOT_NULL( pLate );
    SW_EXPECT_EQUAL( 0u, serverTable.findID( *pLate ) );
}

/**
 * @brief [NetObjectTableTest] 생성 번호 — 서버가 매기고 클라이언트가 그 번호로 묶으면 양쪽이 같은 번호로 부르고, 판이 올라 쓰는 쪽이 새 오브젝트를 알아챈다
 */
SW_TEST_CASE( NetObjectTableTest, SpawnedIDsAreAssignedByTheServerAndBoundByClients )
{
    GameObjectManager serverManager;
    GameObjectManager clientManager;
    NetObjectTable&   serverTable = serverManager.getSubsystem<NetObjectTable>();
    NetObjectTable&   clientTable = clientManager.getSubsystem<NetObjectTable>();
    serverTable.captureSceneObjects();
    clientTable.captureSceneObjects();

    GameObject* pServerArrow = serverManager.createGameObject( hashed_string( "Arrow" ) );
    GameObject* pClientArrow = clientManager.createGameObject( hashed_string( "Arrow" ) );
    SW_ASSERT_TRUE( pServerArrow != nullptr && pClientArrow != nullptr );
    const uint32 revision = serverTable.getSpawnedRevision();
    const uint32 arrowID  = serverTable.registerSpawned( *pServerArrow );
    SW_EXPECT_TRUE( ( arrowID & NetObjectTable::kSpawnedBit ) != 0 );
    SW_EXPECT_EQUAL( arrowID, serverTable.registerSpawned( *pServerArrow ) ); // 두 번 불러도 같은 번호
    SW_EXPECT_TRUE( serverTable.getSpawnedRevision() != revision );
    SW_EXPECT_TRUE( serverTable.findObject( arrowID ) == pServerArrow );

    // 클라이언트 — 서버가 알린 번호로 묶는다.
    SW_EXPECT_TRUE( clientTable.bindSpawned( arrowID, *pClientArrow ) );
    SW_EXPECT_EQUAL( arrowID, clientTable.findID( *pClientArrow ) );
    SW_EXPECT_TRUE( clientTable.findObject( arrowID ) == pClientArrow );

    // 틀린 묶기 — 씬 번호, 이미 살아 있는 오브젝트가 묶인 번호.
    GameObject* pOther = clientManager.createGameObject( hashed_string( "OtherArrow" ) );
    SW_ASSERT_NOT_NULL( pOther );
    {
        SW_TEST_DEFENSIVE_SCOPE( "binding a scene id or a taken spawned id" );
        SW_EXPECT_FALSE( clientTable.bindSpawned( 5u, *pOther ) );
        SW_EXPECT_FALSE( clientTable.bindSpawned( arrowID, *pOther ) );
    }

    // 떼면 번호가 사라진다.
    clientTable.unregisterSpawned( *pClientArrow );
    SW_EXPECT_EQUAL( 0u, clientTable.findID( *pClientArrow ) );
    SW_EXPECT_NULL( clientTable.findObject( arrowID ) );
}

/**
 * @brief [NetObjectTableTest] 씬 오브젝트 1 만 개를 두 번에 나눠 잡아도 번호는 이름 해시이고, 번호 순서로 모이며, 이미 생성 번호가 있는 오브젝트는 씬 번호를 받지 않는다
 * @details 한 번에 모아 끼우는 구현이 하나씩 끼우던 결과와 같아야 한다: 번호가 겹친 오브젝트(31 비트 해시 충돌)는 번호가 없고, 나머지는 `computeSceneID` 가 곧 번호다.
 *          `collectObjects` 는 씬 번호 오름차순 다음에 생성 번호 오름차순이다. 두 번째 호출은 이미 잡은 오브젝트를 다시 잡지 않는다.
 */
SW_TEST_CASE( NetObjectTableTest, CapturingManySceneObjectsInTwoPassesKeepsNameHashIDsAndOrder )
{
    constexpr uint32 kObjectCount     = 10000;
    constexpr uint32 kFirstPassCount  = 6000;
    constexpr uint32 kSpawnedInSecond = 3;

    GameObjectManager   manager;
    NetObjectTable&     table = manager.getSubsystem<NetObjectTable>();
    vector<GameObject*> listObject;
    for ( uint32 objectIndex = 0; objectIndex < kObjectCount; ++objectIndex )
    {
        fixed_string<constant::kMaxBuffer32> nameBuf{};
        formatstring( nameBuf.data(), nameBuf.capacity(), "NetObject%#", objectIndex );
        GameObject* pObject = manager.createGameObject( hashed_string( nameBuf.c_str() ) );
        SW_ASSERT_NOT_NULL( pObject );
        listObject.push_back( pObject );
        if ( objectIndex + 1 == kFirstPassCount )
            table.captureSceneObjects();
    }
    // 둘째 호출 전에 몇 개는 생성 번호를 받는다 — 씬 번호를 받으면 안 된다.
    vector<GameObject*> listSpawned;
    for ( uint32 spawnedIndex = 0; spawnedIndex < kSpawnedInSecond; ++spawnedIndex )
    {
        GameObject* pSpawned = listObject[kFirstPassCount + spawnedIndex];
        SW_ASSERT_TRUE( ( table.registerSpawned( *pSpawned ) & NetObjectTable::kSpawnedBit ) != 0 );
        listSpawned.push_back( pSpawned );
    }
    table.captureSceneObjects();
    table.captureSceneObjects(); // 한 번 더 불러도 같다

    unordered_map<uint32, uint32> mapSceneIDCount;
    for ( uint32 objectIndex = 0; objectIndex < kObjectCount; ++objectIndex )
    {
        const bool bSpawned = kFirstPassCount <= objectIndex && objectIndex < kFirstPassCount + kSpawnedInSecond;
        if ( bSpawned == false )
            ++mapSceneIDCount[NetObjectTable::computeSceneID( *listObject[objectIndex] )];
    }
    bool   bIDsMatch     = true;
    uint32 numberedScene = 0;
    for ( uint32 objectIndex = 0; objectIndex < kObjectCount; ++objectIndex )
    {
        const bool bSpawned = kFirstPassCount <= objectIndex && objectIndex < kFirstPassCount + kSpawnedInSecond;
        if ( bSpawned )
        {
            bIDsMatch = bIDsMatch && ( table.findID( *listObject[objectIndex] ) & NetObjectTable::kSpawnedBit ) != 0;
            continue;
        }
        const uint32 sceneID  = NetObjectTable::computeSceneID( *listObject[objectIndex] );
        const bool   bUnique  = mapSceneIDCount[sceneID] == 1;
        const uint32 expected = bUnique ? sceneID : 0u;
        numberedScene += bUnique ? 1u : 0u;
        bIDsMatch = bIDsMatch && table.findID( *listObject[objectIndex] ) == expected;
        bIDsMatch = bIDsMatch && ( bUnique ? table.findObject( sceneID ) == listObject[objectIndex] : table.findObject( sceneID ) == nullptr );
    }
    SW_EXPECT_TRUE( bIDsMatch );

    // 씬 번호 오름차순, 그 다음 생성 번호.
    vector<GameObject*> listCollected;
    table.collectObjects( listCollected );
    SW_ASSERT_EQUAL( numberedScene + kSpawnedInSecond, static_cast<uint32>( listCollected.size() ) );
    bool bOrdered = true;
    for ( uint32 collectedIndex = 1; collectedIndex < static_cast<uint32>( listCollected.size() ); ++collectedIndex )
    {
        bOrdered = bOrdered && table.findID( *listCollected[collectedIndex - 1] ) < table.findID( *listCollected[collectedIndex] );
    }
    SW_EXPECT_TRUE( bOrdered );
    for ( uint32 spawnedIndex = 0; spawnedIndex < kSpawnedInSecond; ++spawnedIndex )
    {
        SW_EXPECT_TRUE( listCollected[numberedScene + spawnedIndex] == listSpawned[spawnedIndex] );
    }
}
