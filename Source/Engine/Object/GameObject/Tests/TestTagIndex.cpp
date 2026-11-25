#include "pch.h"

#include "Core/Algorithm/VectorUtil.h"
#include "Core/TestFramework/TestFramework.h"

#include "Engine/Object/Component/TagComponent.h"
#include "Engine/Object/Component/TagSystem.h"
#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManager.h"
#include "Engine/Object/GameObject/Registry/TagIndex.h"

using namespace sw;

namespace
{
    /** @brief @p tag 하나를 가진 오브젝트를 정렬해 돌려줍니다(색인은 순서를 약속하지 않는다). */
    vector<GameObject*> findSorted( const GameObjectManager& manager, const utf8* pTag )
    {
        vector<GameObject*> listObject;
        manager.getTagIndex().findObjectsWithTags( TagContainer{ TagID::request( pTag ) }, listObject );
        VectorUtil::sort( listObject );
        return listObject;
    }

    vector<GameObject*> sorted( vector<GameObject*> listObject )
    {
        VectorUtil::sort( listObject );
        return listObject;
    }
} // namespace

// 스냅샷은 바뀐 칸만 새로 복사하고 나머지는 지난 스냅샷의 칸을 나눠 받는다 — 바꾼 칸과 바꾸지 않은 칸이 모두 지금 상태와 같아야 한다.
SW_TEST_CASE( TagIndexTest, SnapshotSharesUntouchedBucketsAndRefreshesChangedOnes )
{
    GameObjectManager manager;
    GameObject*       pFirst  = manager.createGameObject( hashed_string( "First" ) );
    GameObject*       pSecond = manager.createGameObject( hashed_string( "Second" ) );
    SW_REQUIRE_NOT_NULL( pFirst );
    SW_REQUIRE_NOT_NULL( pSecond );
    pFirst->addTag( TagID::request( "Enemy" ) );
    pSecond->addTag( TagID::request( "Ally" ) );

    SW_EXPECT_TRUE( findSorted( manager, "Enemy" ) == vector<GameObject*>{ pFirst } );
    SW_EXPECT_TRUE( findSorted( manager, "Ally" ) == vector<GameObject*>{ pSecond } );

    // Enemy 칸만 바꾼다 — Ally 칸은 지난 스냅샷의 것을 그대로 받는다.
    pSecond->addTag( TagID::request( "Enemy" ) );
    SW_EXPECT_TRUE( findSorted( manager, "Enemy" ) == sorted( { pFirst, pSecond } ) );
    SW_EXPECT_TRUE( findSorted( manager, "Ally" ) == vector<GameObject*>{ pSecond } );

    // 칸을 비우면 스냅샷에서도 빠진다.
    pFirst->removeTag( TagID::request( "Enemy" ) );
    pSecond->removeTag( TagID::request( "Enemy" ) );
    SW_EXPECT_TRUE( findSorted( manager, "Enemy" ).empty() );
    SW_EXPECT_TRUE( findSorted( manager, "Ally" ) == vector<GameObject*>{ pSecond } );

    // 옛 스냅샷을 놓은 뒤에도 나눠 가진 칸은 지금 스냅샷이 쥐고 있다.
    manager.getTagIndex().releaseRetiredSnapshots();
    SW_EXPECT_TRUE( findSorted( manager, "Ally" ) == vector<GameObject*>{ pSecond } );
    pFirst->addTag( TagID::request( "Enemy" ) );
    SW_EXPECT_TRUE( findSorted( manager, "Enemy" ) == vector<GameObject*>{ pFirst } );
}

// 계층 태그 — 조상 칸도 바뀐 칸으로 센다.
SW_TEST_CASE( TagIndexTest, AncestorBucketsRefreshWithTheChild )
{
    GameObjectManager manager;
    GameObject*       pObject = manager.createGameObject( hashed_string( "Unit" ) );
    SW_REQUIRE_NOT_NULL( pObject );
    pObject->addTag( TagID::request( "Faction.Player" ) );
    SW_EXPECT_TRUE( findSorted( manager, "Faction" ) == vector<GameObject*>{ pObject } );
    pObject->removeTag( TagID::request( "Faction.Player" ) );
    SW_EXPECT_TRUE( findSorted( manager, "Faction" ).empty() );
}

// 풀 반환은 실행 중에 쓴 태그(쓰러짐 · 숨음)를 걷고 저작한 태그는 남긴다 — 실행 중에 붙은 태그 컴포넌트가 풀을 거절하게 하지 않는다.
SW_TEST_CASE( TagIndexTest, SpawnResetDropsRuntimeTags )
{
    GameObjectManager manager;
    GameObject*       pEnemy = manager.createGameObject( hashed_string( "Enemy" ) );
    pEnemy->addTag( TagID::request( "State.Test" ) );
    TagComponent* pTags = pEnemy->getComponent<TagComponent>();
    SW_REQUIRE_NOT_NULL( pTags );
    SW_EXPECT_TRUE( pTags->resetToSpawnState() == SpawnStateReset::Reset );
    SW_EXPECT_FALSE( pEnemy->hasTag( TagID::request( "State.Test" ) ) );
    SW_EXPECT_TRUE( findSorted( manager, "State.Test" ).empty() ); // 색인도 맞췄다
    pEnemy->addTag( TagID::request( "State.Test" ) );
    SW_EXPECT_TRUE( pEnemy->hasTag( TagID::request( "State.Test" ) ) );
}
