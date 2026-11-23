#include "pch.h"

#include "Core/Testing/TestFramework.h"

#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManager.h"
#include "Engine/Visual/Component/MeshComponent.h"

#include "GameFramework/Kits/Genre/Action/AbilityArena/Client/ArenaDeathSquashComponent.h"

using namespace sw;

/** @brief [ArenaDeathSquashTest] 쓰러진 몸은 스폰 크기에서 높이만 줄고(줄인 크기에서 다시 줄지 않는다), 풀에 돌아가면 스폰 크기다 */
SW_TEST_CASE( ArenaDeathSquashTest, SquashShrinksFromTheSpawnScaleAndResets )
{
    GameObjectManager manager;
    GameObject*       pBody = manager.createGameObject( hashed_string( "ArenaGrunt" ) );
    SW_REQUIRE_NOT_NULL( pBody );
    MeshComponent*             pMesh   = pBody->addComponent<MeshComponent>();
    ArenaDeathSquashComponent* pSquash = pBody->addComponent<ArenaDeathSquashComponent>();
    SW_REQUIRE_TRUE( pMesh != nullptr && pSquash != nullptr );
    pMesh->setLocalScale( float3{ 2.0f, 2.0f, 2.0f } );

    pSquash->squash( 0.5f );
    pSquash->squash( 0.25f );
    SW_EXPECT_EQUAL( 2.0f, pMesh->getLocalScale()._x );
    SW_EXPECT_EQUAL( 0.5f, pMesh->getLocalScale()._y );

    SW_EXPECT_TRUE( pSquash->resetToSpawnState() == SpawnStateReset::Reset );
    SW_EXPECT_EQUAL( 2.0f, pMesh->getLocalScale()._y );
}
