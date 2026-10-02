#include "pch.h"

#include "Core/Math/MathUtil.h"

#include "Engine/Object/Component/2D/BoxCollider2DComponent.h"
#include "Engine/Object/Component/SceneComponent.h"
#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManager.h"

#include "EngineTest/TestGameObjectMocks.h"

#include "TestFramework/TestFramework.h"

namespace sw
{
    /** @brief 겹침 시작 · 끝을 받은 상대 오브젝트 id 를 적는 컴포넌트입니다(상대가 이미 사라졌으면 0). */
    class MockOverlapListenerComponent : public Component
    {
    public:
        REFLECT_BODY();

        vector<uint64> _listBeginOther;
        vector<uint64> _listEndOther;

        const TypeInfo* getTypeInfo() const override { return StaticType(); }
        void            onOverlapBegin( GameObject* pOther ) override { _listBeginOther.push_back( pOther != nullptr ? pOther->getObjectId() : 0 ); }
        void            onOverlapEnd( GameObject* pOther ) override { _listEndOther.push_back( pOther != nullptr ? pOther->getObjectId() : 0 ); }
    };

    inline const TypeInfo* MockOverlapListenerComponent::StaticType()
    {
        return makeMockComponentTypeInfo( hashed_string( "MockOverlapListenerComponent" ), hashed_string( "sw::MockOverlapListenerComponent" ),
                                          sizeof( MockOverlapListenerComponent ) );
    }
} // namespace sw

// 2D 박스 콜라이더 — 물리가 보는 상자.

namespace
{
    /** @brief 크기 1 · 오프셋 (1, 0) 의 콜라이더를 단 오브젝트를 만들어 @p pParentRoot 에 붙입니다. */
    sw::BoxCollider2DComponent* spawnCrate( sw::GameObjectManager& manager, sw::GameObject* pParent )
    {
        sw::GameObject* pCrate = manager.createGameObject( sw::hashed_string( "Crate" ) );
        if ( pCrate == nullptr )
            return nullptr;
        sw::BoxCollider2DComponent* pBox = pCrate->addComponent<sw::BoxCollider2DComponent>();
        if ( pBox == nullptr )
            return nullptr;
        pBox->setOffsetScale( sw::float2{ 1.0f, 1.0f } );
        pBox->setOffsetPosition( sw::float2{ 1.0f, 0.0f } );
        if ( pParent != nullptr && pCrate->attachToParent( pParent ) == false )
            return nullptr;
        return pBox;
    }

    /** @brief 상자의 최소 · 최대가 기대와 같은지(오차 1e-4) 봅니다. */
    bool isBox( const sw::BoxCollider2DComponent* pBox, const sw::float2& expectedMin, const sw::float2& expectedMax )
    {
        sw::float2 minB{};
        sw::float2 maxB{};
        pBox->getBounds( minB, maxB );
        return sw::MathUtil::nearEqual( minB._x, expectedMin._x, 1e-4f ) && sw::MathUtil::nearEqual( minB._y, expectedMin._y, 1e-4f ) &&
               sw::MathUtil::nearEqual( maxB._x, expectedMax._x, 1e-4f ) && sw::MathUtil::nearEqual( maxB._y, expectedMax._y, 1e-4f );
    }

    /** @brief 크기 1 콜라이더와 겹침 수신 컴포넌트를 단 오브젝트를 (x, 0) 에 만듭니다. */
    struct OverlapProbe
    {
        sw::GameObject*                   _pObject{ nullptr };
        sw::BoxCollider2DComponent*       _pCollider{ nullptr };
        sw::MockOverlapListenerComponent* _pListener{ nullptr };
    };

    OverlapProbe spawnProbe( sw::GameObjectManager& manager, const utf8* pName, float32 x )
    {
        OverlapProbe probe;
        probe._pObject = manager.createGameObject( sw::hashed_string( pName ) );
        if ( probe._pObject == nullptr )
            return probe;
        probe._pCollider = probe._pObject->addComponent<sw::BoxCollider2DComponent>();
        probe._pListener = probe._pObject->addComponent<sw::MockOverlapListenerComponent>();
        if ( probe._pCollider != nullptr )
        {
            probe._pCollider->setOffsetScale( sw::float2{ 1.0f, 1.0f } );
            probe._pCollider->setLocalPosition( sw::float3{ x, 0.0f, 0.0f } );
        }
        return probe;
    }
} // namespace

/**
 * @brief [BoxCollider2DTest] 콜라이더 상자는 월드 스케일을 따르고, 오프셋도 그 스케일로 늘어난다
 * @details 예전에는 상자 크기(`_offsetScale`)와 오프셋을 월드 위치에 그대로 더해, 부모나 자기를 키운 콜라이더가 그려진 모습보다 작았다 — 큰 적이
 *          작은 상자로 맞았다. 유니티 `BoxCollider2D.size` · 언리얼 박스 범위는 트랜스폼의 스케일을 받는다.
 */
SW_TEST_CASE( BoxCollider2DTest, BoxFollowsWorldScale )
{
    sw::GameObjectManager manager;
    sw::GameObject*       pGiant = manager.createGameObject( sw::hashed_string( "Giant" ) );
    sw::SceneComponent*   pRoot  = pGiant->addComponent<sw::SceneComponent>();
    SW_ASSERT_NOT_NULL( pRoot );
    pRoot->setLocalPosition( sw::float3{ 10.0f, 0.0f, 0.0f } );
    pRoot->setLocalScale( sw::float3{ 2.0f, 3.0f, 1.0f } );
    sw::BoxCollider2DComponent* pBox = spawnCrate( manager, pGiant );
    SW_ASSERT_NOT_NULL( pBox );
    manager.flushSceneTransforms();

    // 중심 = 부모 (10, 0) + 오프셋 (1, 0) × 스케일 (2, 3) = (12, 0). 크기 1 × (2, 3) = (2, 3).
    SW_EXPECT_TRUE( isBox( pBox, sw::float2{ 11.0f, -1.5f }, sw::float2{ 13.0f, 1.5f } ) );
}

/**
 * @brief [BoxCollider2DTest] 돈 콜라이더는 돈 상자를 덮는 축 정렬 상자다(물리는 축 정렬 상자로 판정한다)
 * @details Z 로 90 도 돌면 (2, 3) 크기 상자가 (3, 2) 를 덮는다. 오프셋도 돈다 — (1, 0) × 스케일이 (0, 2) 가 된다. 언리얼 `FBox::TransformBy` 와 같은
 *          계산이다(축마다 회전 · 스케일 성분의 절댓값으로 반 크기를 모은다).
 */
SW_TEST_CASE( BoxCollider2DTest, RotatedBoxIsCoveredByItsAxisAlignedBounds )
{
    sw::GameObjectManager manager;
    sw::GameObject*       pTurned = manager.createGameObject( sw::hashed_string( "Turned" ) );
    sw::SceneComponent*   pRoot   = pTurned->addComponent<sw::SceneComponent>();
    SW_ASSERT_NOT_NULL( pRoot );
    pRoot->setLocalScale( sw::float3{ 2.0f, 3.0f, 1.0f } );
    pRoot->setLocalRotation( sw::float3{ 0.0f, 0.0f, sw::MathUtil::HalfPi } );
    sw::BoxCollider2DComponent* pBox = spawnCrate( manager, pTurned );
    SW_ASSERT_NOT_NULL( pBox );
    manager.flushSceneTransforms();

    sw::float2 minB{};
    sw::float2 maxB{};
    pBox->getBounds( minB, maxB );
    SW_EXPECT_NEAR_EQUAL( 3.0f, maxB._x - minB._x, 1e-4f );
    SW_EXPECT_NEAR_EQUAL( 2.0f, maxB._y - minB._y, 1e-4f );
    // 오프셋 (1, 0) 은 스케일 (2, ·) 로 2 가 되고, 90 도 돌아 Y 축 위 2 에 놓인다(부호는 회전 방향을 따른다).
    SW_EXPECT_NEAR_EQUAL( 0.0f, ( minB._x + maxB._x ) * 0.5f, 1e-4f );
    SW_EXPECT_NEAR_EQUAL( 2.0f, sw::MathUtil::abs( ( minB._y + maxB._y ) * 0.5f ), 1e-4f );
}

/**
 * @brief [BoxCollider2DTest] 겹침 시작 · 끝이 틱 뒤에 두 오브젝트의 컴포넌트 모두에 온다 — 꺼지거나 사라진 콜라이더는 겹침을 끝낸다
 * @details 예전에는 물리 step 이 빈 함수였고 부르는 곳도 없어, 콜라이더가 서로 겹쳐도 아무도 알 수 없었다(매 틱 직접 물어야 했다). 콜라이더는
 *          병렬 틱에서 제 바디를 맞춰, 같은 그룹에서 겹침을 묻는 쪽이 스케줄에 따라 옛 · 새 자리를 봤다. 이제 매니저가 틱 · 트랜스폼 적용 뒤에
 *          콜라이더 바디를 한 번에 맞추고 step 해 이벤트를 게임 스레드에서 나눠 준다(유니티 OnTriggerEnter2D/Exit2D — 그 오브젝트의 모든 컴포넌트에).
 */
SW_TEST_CASE( BoxCollider2DTest, OverlapEventsReachBothObjectsAfterTheTick )
{
    sw::GameObjectManager manager;
    const OverlapProbe    a = spawnProbe( manager, "A", 0.0f );
    const OverlapProbe    b = spawnProbe( manager, "B", 0.5f );
    const OverlapProbe    c = spawnProbe( manager, "C", 10.0f );
    SW_ASSERT_NOT_NULL( a._pListener );
    SW_ASSERT_NOT_NULL( b._pListener );
    SW_ASSERT_NOT_NULL( c._pListener );
    const uint64 idA = a._pObject->getObjectId();
    const uint64 idB = b._pObject->getObjectId();

    manager.beginPlay();
    manager.tick( 0.016f );
    SW_ASSERT_EQUAL( static_cast<size_t>( 1 ), a._pListener->_listBeginOther.size() );
    SW_ASSERT_EQUAL( static_cast<size_t>( 1 ), b._pListener->_listBeginOther.size() );
    SW_EXPECT_EQUAL( idB, a._pListener->_listBeginOther[0] );
    SW_EXPECT_EQUAL( idA, b._pListener->_listBeginOther[0] );
    SW_EXPECT_TRUE( c._pListener->_listBeginOther.empty() );

    // 계속 겹치면 다시 오지 않는다.
    manager.tick( 0.016f );
    SW_EXPECT_EQUAL( static_cast<size_t>( 1 ), a._pListener->_listBeginOther.size() );

    // 떨어지면 끝.
    b._pCollider->setLocalPosition( sw::float3{ 20.0f, 0.0f, 0.0f } );
    manager.tick( 0.016f );
    SW_ASSERT_EQUAL( static_cast<size_t>( 1 ), a._pListener->_listEndOther.size() );
    SW_EXPECT_EQUAL( idB, a._pListener->_listEndOther[0] );
    SW_EXPECT_EQUAL( static_cast<size_t>( 1 ), b._pListener->_listEndOther.size() );

    // 다시 겹쳤다가 B 를 끄면 끝 — 꺼진 콜라이더는 겹침에 들지 않는다.
    b._pCollider->setLocalPosition( sw::float3{ 0.5f, 0.0f, 0.0f } );
    manager.tick( 0.016f );
    SW_EXPECT_EQUAL( static_cast<size_t>( 2 ), a._pListener->_listBeginOther.size() );
    b._pObject->setActive( false );
    manager.tick( 0.016f );
    SW_EXPECT_EQUAL( static_cast<size_t>( 2 ), a._pListener->_listEndOther.size() );

    // 다시 켜면 시작, A 를 지우면 B 가 끝을 받는다(A 는 사라져 상대가 없다).
    b._pObject->setActive( true );
    manager.tick( 0.016f );
    SW_EXPECT_EQUAL( static_cast<size_t>( 3 ), a._pListener->_listBeginOther.size() );
    const size_t endBefore = b._pListener->_listEndOther.size();
    manager.destroyObject( a._pObject );
    manager.tick( 0.016f );
    SW_ASSERT_EQUAL( endBefore + 1, b._pListener->_listEndOther.size() );
    SW_EXPECT_EQUAL( 0ull, b._pListener->_listEndOther.back() );
    manager.endPlay();
}

/**
 * @brief [BoxCollider2DTest] 시작한 뒤 바꾼 콜라이더 종류(레이어)도 겹침에 닿는다
 * @details 바디의 레이어는 더할 때 한 번 적혔고, 그 뒤로 매 step 맞추는 것은 상자뿐이었다 — 플레이 중 `setColliderType` 으로 레이어를 바꿔도
 *          (튕겨 낸 총알이 편을 바꾸는 식) 겹침은 옛 레이어로 걸러졌다. 이제 step 직전 동기화가 상자 · 레이어 · 연속 여부를 함께 맞춘다.
 */
SW_TEST_CASE( BoxCollider2DTest, ColliderTypeChangedDuringPlayFiltersTheNextStep )
{
    sw::GameObjectManager manager;
    manager.getPhysicsWorld().layers().setLayerCollision( 0, 1, false );
    const OverlapProbe a = spawnProbe( manager, "A", 0.0f );
    const OverlapProbe b = spawnProbe( manager, "B", 0.5f );
    SW_ASSERT_NOT_NULL( a._pListener );
    SW_ASSERT_NOT_NULL( b._pListener );

    manager.beginPlay();
    manager.tick( 0.016f );
    SW_ASSERT_EQUAL( static_cast<size_t>( 1 ), a._pListener->_listBeginOther.size() );

    // B 를 A 와 부딪히지 않는 레이어로 옮기면 다음 step 에 겹침이 끝난다.
    b._pCollider->setColliderType( 1 );
    manager.tick( 0.016f );
    SW_EXPECT_EQUAL( static_cast<size_t>( 1 ), a._pListener->_listEndOther.size() );
    manager.endPlay();
}
