// GameObjectEventsTest — 오브젝트 사건(`GameObjectEvents`)은 구독한 컴포넌트에만 구독 차례로 가고, 사건 안의 구독 해지는 그 자리에서 듣지 않게 하며,
// 컴포넌트가 사라지면 구독이 풀린다. 듣는 쪽이 없는 오브젝트는 묶음을 만들지 않는다(nogpu, 물리 없음).
#include "pch.h"

#include "Core/Testing/TestFramework.h"

#include "Engine/Object/Component/SceneComponent.h"
#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectEvents.h"
#include "Engine/Object/GameObject/GameObjectManager.h"
#include "Engine/Object/GameObject/Tests/GameObjectEventsTestComponents.h"

using namespace sw;

namespace
{
    void broadcastHit( const GameObject& target )
    {
        GameObjectEvents::broadcast( target, &GameObjectEvents::_hitReceived, HitInfo{} );
    }

    /** @brief @p pObject 에 기록 컴포넌트를 붙이고 번호 · 기록을 겁니다. */
    EventListenerTestComponent* addListener( GameObject* pObject, uint32 id, vector<uint32>& log )
    {
        EventListenerTestComponent* pListener = pObject->addComponent<EventListenerTestComponent>();
        if ( pListener != nullptr )
        {
            pListener->_id   = id;
            pListener->_pLog = &log;
        }
        return pListener;
    }
} // namespace

/** @brief [GameObjectEventsTest] 듣는 쪽이 없는 오브젝트는 묶음이 없고 사건은 아무것도 하지 않는다 — 구독은 첫 구독 때 묶음을 만든다 */
SW_TEST_CASE( GameObjectEventsTest, NoListenerMeansNoEventBlock )
{
    GameObjectManager manager;
    GameObject*       pObject = manager.createGameObject( hashed_string( "Plain" ) );
    SW_REQUIRE_NOT_NULL( pObject );
    (void)pObject->addComponent<SceneComponent>();
    SW_EXPECT_TRUE( pObject->findEvents() == nullptr );
    broadcastHit( *pObject ); // 묶음이 없으면 그냥 돌아온다

    vector<uint32> log;
    SW_REQUIRE_NOT_NULL( addListener( pObject, 1, log ) );
    SW_REQUIRE_NOT_NULL( pObject->findEvents() );
    broadcastHit( *pObject );
    SW_EXPECT_EQUAL( size_t{ 1 }, log.size() );
}

/** @brief [GameObjectEventsTest] 같은 오브젝트의 여러 컴포넌트가 구독하면 모두 구독 차례로 받고, 남의 오브젝트 · 다른 사건으로는 가지 않는다 */
SW_TEST_CASE( GameObjectEventsTest, SeveralComponentsReceiveInSubscriptionOrder )
{
    GameObjectManager manager;
    GameObject*       pObject = manager.createGameObject( hashed_string( "Target" ) );
    GameObject*       pOther  = manager.createGameObject( hashed_string( "Other" ) );
    SW_REQUIRE_TRUE( pObject != nullptr && pOther != nullptr );
    vector<uint32>              log;
    EventListenerTestComponent* pFirst  = addListener( pObject, 1, log );
    EventListenerTestComponent* pSecond = addListener( pObject, 2, log );
    EventListenerTestComponent* pThird  = addListener( pObject, 3, log );
    EventListenerTestComponent* pOthers = addListener( pOther, 9, log );
    SW_REQUIRE_TRUE( pFirst != nullptr && pSecond != nullptr && pThird != nullptr && pOthers != nullptr );

    broadcastHit( *pObject );
    SW_REQUIRE_EQUAL( size_t{ 3 }, log.size() );
    SW_EXPECT_EQUAL( 1u, log[0] );
    SW_EXPECT_EQUAL( 2u, log[1] );
    SW_EXPECT_EQUAL( 3u, log[2] );

    // 겹침 진입은 맞음과 다른 칸이다 — 맞음 기록은 늘지 않고 겹침 수만 는다.
    GameObjectEvents::broadcast( *pObject, &GameObjectEvents::_overlapBegin, OverlapInfo{} );
    SW_EXPECT_EQUAL( size_t{ 3 }, log.size() );
    SW_EXPECT_EQUAL( 1u, pFirst->_overlapBeginCount );
    SW_EXPECT_EQUAL( 1u, pThird->_overlapBeginCount );
    SW_EXPECT_EQUAL( 0u, pOthers->_overlapBeginCount );
}

/** @brief [GameObjectEventsTest] 꺼진 컴포넌트는 구독이 있어도 받지 않는다(가상 디스패치 때의 거름) — 다시 켜면 받는다 */
SW_TEST_CASE( GameObjectEventsTest, InactiveComponentIsSkipped )
{
    GameObjectManager manager;
    GameObject*       pObject = manager.createGameObject( hashed_string( "Target" ) );
    SW_REQUIRE_NOT_NULL( pObject );
    vector<uint32>              log;
    EventListenerTestComponent* pFirst  = addListener( pObject, 1, log );
    EventListenerTestComponent* pSecond = addListener( pObject, 2, log );
    SW_REQUIRE_TRUE( pFirst != nullptr && pSecond != nullptr );

    pFirst->setActive( false );
    broadcastHit( *pObject );
    SW_REQUIRE_EQUAL( size_t{ 1 }, log.size() );
    SW_EXPECT_EQUAL( 2u, log[0] );

    pFirst->setActive( true );
    broadcastHit( *pObject );
    SW_EXPECT_EQUAL( size_t{ 3 }, log.size() );
}

/** @brief [GameObjectEventsTest] 사건 안에서 뒤 구독을 풀면 그 사건부터 받지 않는다 — 자기 구독을 풀어도 남은 차례는 이어진다 */
SW_TEST_CASE( GameObjectEventsTest, ReleaseDuringDispatch )
{
    GameObjectManager manager;
    GameObject*       pObject = manager.createGameObject( hashed_string( "Target" ) );
    SW_REQUIRE_NOT_NULL( pObject );
    vector<uint32>              log;
    EventListenerTestComponent* pFirst  = addListener( pObject, 1, log );
    EventListenerTestComponent* pSecond = addListener( pObject, 2, log );
    EventListenerTestComponent* pThird  = addListener( pObject, 3, log );
    SW_REQUIRE_TRUE( pFirst != nullptr && pSecond != nullptr && pThird != nullptr );

    pFirst->_pReleaseOnHit = &pSecond->_hitSubscription; // 첫째가 둘째의 구독을 푼다
    broadcastHit( *pObject );
    SW_REQUIRE_EQUAL( size_t{ 2 }, log.size() );
    SW_EXPECT_EQUAL( 1u, log[0] );
    SW_EXPECT_EQUAL( 3u, log[1] );
    SW_EXPECT_FALSE( pSecond->_hitSubscription.isActive() );

    log.clear();
    pFirst->_pReleaseOnHit = &pFirst->_hitSubscription; // 자기 구독을 푼다 — 셋째는 이번에도 받는다
    broadcastHit( *pObject );
    SW_REQUIRE_EQUAL( size_t{ 2 }, log.size() );
    SW_EXPECT_EQUAL( 1u, log[0] );
    SW_EXPECT_EQUAL( 3u, log[1] );

    log.clear();
    pFirst->_pReleaseOnHit = nullptr;
    broadcastHit( *pObject );
    SW_REQUIRE_EQUAL( size_t{ 1 }, log.size() );
    SW_EXPECT_EQUAL( 3u, log[0] );
}

/** @brief [GameObjectEventsTest] 컴포넌트를 떼면 구독이 함께 풀린다 — 남은 쪽만 받고, 마지막 구독이 풀리면 칸이 빈다 */
SW_TEST_CASE( GameObjectEventsTest, RemovedComponentReleasesItsSubscription )
{
    GameObjectManager manager;
    GameObject*       pObject = manager.createGameObject( hashed_string( "Target" ) );
    SW_REQUIRE_NOT_NULL( pObject );
    vector<uint32>              log;
    EventListenerTestComponent* pFirst  = addListener( pObject, 1, log );
    EventListenerTestComponent* pSecond = addListener( pObject, 2, log );
    SW_REQUIRE_TRUE( pFirst != nullptr && pSecond != nullptr );

    SW_REQUIRE_TRUE( pObject->removeComponent( pFirst ) );
    broadcastHit( *pObject );
    SW_REQUIRE_EQUAL( size_t{ 1 }, log.size() );
    SW_EXPECT_EQUAL( 2u, log[0] );

    SW_REQUIRE_TRUE( pObject->removeComponent( pSecond ) );
    SW_REQUIRE_NOT_NULL( pObject->findEvents() );
    SW_EXPECT_FALSE( pObject->findEvents()->_hitReceived.isBound() );
    SW_EXPECT_FALSE( pObject->findEvents()->_overlapBegin.isBound() );
}
