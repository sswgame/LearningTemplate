#include "pch.h"

#include "Core/Testing/TestFramework.h"

#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManager.h"
#include "Engine/Object/GameObject/Service/NetObjectTable.h"
#include "Engine/Object/GameObject/Tests/SceneIterationTestAccess.h"

using namespace sw;

// 씬 번호는 64 비트 해시에서 나와 32 비트를 넘고 생성 비트가 꺼져 있다. 대소문자는 무시한다.
SW_TEST_CASE( NetObjectTableTest, SceneIDsUseTheWideHashAndKeepTheSpawnedBitClear )
{
    GameObjectManager manager;
    GameObject*       pDoor  = manager.createGameObject( hashed_string( "Door" ) );
    GameObject*       pCrate = manager.createGameObject( hashed_string( "Crate" ) );
    SW_REQUIRE_NOT_NULL( pDoor );
    SW_REQUIRE_NOT_NULL( pCrate );
    const NetObjectID doorID  = NetObjectTable::computeSceneID( *pDoor );
    const NetObjectID crateID = NetObjectTable::computeSceneID( *pCrate );
    SW_EXPECT_EQUAL( doorID & NetObjectTable::kSpawnedBit, NetObjectID{ 0 } );
    SW_EXPECT_NOT_EQUAL( doorID, NetObjectID{ 0 } );
    SW_EXPECT_NOT_EQUAL( doorID, crateID );
    // 64 비트 해시의 위 62 비트를 쓴다 — 두 이름 모두 32 비트 안에 들 확률은 2^-60 이다.
    SW_EXPECT_TRUE( doorID > NetObjectID{ 0xFFFFFFFFull } || crateID > NetObjectID{ 0xFFFFFFFFull } );

    NetObjectTable table{ manager };
    table.captureSceneObjects( SceneIterationTestAccess::getKey() );
    SW_EXPECT_EQUAL( table.findID( *pDoor ), doorID );
    SW_EXPECT_TRUE( table.findObject( doorID ) == pDoor );
    SW_EXPECT_TRUE( table.findObject( crateID ) == pCrate );
}

// 생성 번호는 맨 아래 비트가 켜진 작은 수다 — 서버가 매긴 번호에 클라이언트가 묶고, 씬 번호나 일련번호 0 은 묶지 않는다.
SW_TEST_CASE( NetObjectTableTest, SpawnedIDsAreSmallOddNumbersAndBindOnTheClient )
{
    GameObjectManager server;
    NetObjectTable    serverTable{ server };
    GameObject*       pShot = server.createGameObject( hashed_string( "Shot" ) );
    SW_REQUIRE_NOT_NULL( pShot );
    const NetObjectID shotID = serverTable.registerSpawned( *pShot );
    SW_EXPECT_EQUAL( shotID & NetObjectTable::kSpawnedBit, NetObjectTable::kSpawnedBit );
    SW_EXPECT_TRUE( shotID < NetObjectID{ 0x80u } ); // 가변 길이 정수 1 바이트
    SW_EXPECT_EQUAL( serverTable.registerSpawned( *pShot ), shotID );
    SW_EXPECT_TRUE( serverTable.findObject( shotID ) == pShot );

    GameObjectManager client;
    NetObjectTable    clientTable{ client };
    GameObject*       pLocalShot = client.createGameObject( hashed_string( "Shot" ) );
    SW_REQUIRE_NOT_NULL( pLocalShot );
    {
        SW_EXPECT_LOG( "NetObjectTable.NetObjectCannotBound" );
        SW_EXPECT_FALSE( clientTable.bindSpawned( NetObjectTable::kSpawnedBit, *pLocalShot ) );
        SW_EXPECT_FALSE( clientTable.bindSpawned( NetObjectTable::computeSceneID( *pLocalShot ), *pLocalShot ) );
    }
    SW_EXPECT_TRUE( clientTable.bindSpawned( shotID, *pLocalShot ) );
    SW_EXPECT_EQUAL( clientTable.findID( *pLocalShot ), shotID );
}
