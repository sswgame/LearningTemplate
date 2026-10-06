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
    /** @brief 겹침 시작 · 끝을 받은 상대 오브젝트 id 와 상대가 트리거였는지를 적는 컴포넌트입니다(상대가 이미 사라졌으면 0). */
    class MockOverlapListenerComponent : public Component
    {
    public:
        REFLECT_BODY();

        vector<uint64> _listBeginOther;
        vector<uint64> _listEndOther;
        vector<bool>   _listBeginOtherTrigger;

        const TypeInfo* getTypeInfo() const override { return StaticType(); }
        void            onOverlapBegin( const OverlapInfo& overlap ) override
        {
            _listBeginOther.push_back( overlap._pOther != nullptr ? overlap._pOther->getObjectId() : 0 );
            _listBeginOtherTrigger.push_back( overlap._bOtherTrigger == SW_TRUE );
        }
        void onOverlapEnd( const OverlapInfo& overlap ) override { _listEndOther.push_back( overlap._pOther != nullptr ? overlap._pOther->getObjectId() : 0 ); }
    };

    inline const TypeInfo* MockOverlapListenerComponent::StaticType()
    {
        return makeMockComponentTypeInfo( &GameObject::addComponentTo<MockOverlapListenerComponent>, hashed_string( "MockOverlapListenerComponent" ), hashed_string( "sw::MockOverlapListenerComponent" ),
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
 * @details 상자 크기(`_offsetScale`)와 오프셋을 월드 위치에 그대로 더하면 부모나 자기를 키운 콜라이더가 그려진 모습보다 작아 큰 적이
 *          작은 상자로 맞는다. 유니티 `BoxCollider2D.size` · 언리얼 박스 범위는 트랜스폼의 스케일을 받는다.
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
    pRoot->setLocalRotation( sw::float3{ 0.0f, 0.0f, sw::MathUtil::kHalfPi } );
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
 * @details 매니저가 틱 · 트랜스폼 적용 뒤에 콜라이더 바디를 한 번에 맞추고 step 해 이벤트를 게임 스레드에서 나눠 준다(유니티 OnTriggerEnter2D/Exit2D
 *          — 그 오브젝트의 모든 컴포넌트에). 콜라이더가 병렬 틱에서 제 바디를 맞추면 같은 그룹에서 겹침을 묻는 쪽이 스케줄에 따라 옛 · 새 자리를 본다.
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
 * @details step 직전 동기화가 상자 · 레이어 · 연속 여부를 함께 맞춘다. 레이어를 더할 때 한 번만 적으면 플레이 중 `setColliderType` 으로 레이어를
 *          바꿔도(튕겨 낸 총알이 편을 바꾸는 식) 겹침이 옛 레이어로 걸러진다.
 */
SW_TEST_CASE( BoxCollider2DTest, ColliderTypeChangedDuringPlayFiltersTheNextStep )
{
    sw::GameObjectManager manager;
    manager.getOverlapWorld2D().getPhysicsWorld().layers().setLayerCollision( 0, 1, false );
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

/**
 * @brief [BoxCollider2DTest] 연속 콜라이더를 `teleportTo` 로 옮기면 그 사이를 쓸지 않는다 — `setWorldPosition` 으로 옮기면 쓴다, 붙은 자식도 같다
 * @details 연속 콜라이더는 지난 step 의 자리부터 쓸리므로 그대로 두면 리스폰 · 문 통과 같은 순간이동도 그 길의 벽과 겹친다(언리얼은 `TeleportPhysics`, 유니티는
 *          `Rigidbody.position` 대입으로 가른다). `SceneComponent::teleportTo` 는 그 컴포넌트와 그 아래에 붙은 모두를 순간이동으로 표시하고, 콜라이더는
 *          step 직전 바디를 맞출 때 새 자리를 다음 쓸림의 출발점으로 둔다.
 */
SW_TEST_CASE( BoxCollider2DTest, TeleportToDoesNotSweepTheGapButSetWorldPositionDoes )
{
    sw::GameObjectManager manager;
    const OverlapProbe    wall   = spawnProbe( manager, "Wall", 5.0f );
    const OverlapProbe    runner = spawnProbe( manager, "Runner", 0.0f );
    const OverlapProbe    rider  = spawnProbe( manager, "Rider", 0.0f );
    SW_ASSERT_NOT_NULL( wall._pListener );
    SW_ASSERT_NOT_NULL( runner._pListener );
    SW_ASSERT_NOT_NULL( rider._pListener );
    wall._pCollider->setOffsetScale( sw::float2{ 0.1f, 20.0f } );
    runner._pCollider->setContinuous( true );
    rider._pCollider->setContinuous( true );
    rider._pCollider->setLocalPosition( sw::float3{ 0.0f, 3.0f, 0.0f } );
    SW_ASSERT_TRUE( rider._pObject->attachToParent( runner._pObject, sw::AttachRule::KeepWorld ) );

    manager.beginPlay();
    manager.tick( 0.016f );
    SW_ASSERT_TRUE( wall._pListener->_listBeginOther.empty() );

    // 순간이동 — 달리는 쪽도, 거기 탄 자식도 벽을 지나가지 않는다.
    runner._pCollider->teleportTo( sw::float3{ 10.0f, 0.0f, 0.0f } );
    manager.tick( 0.016f );
    SW_EXPECT_TRUE( wall._pListener->_listBeginOther.empty() );

    // 같은 거리를 그냥 옮기면 둘 다 벽을 쓸고 지나간다.
    runner._pCollider->setWorldPosition( sw::float3{ 0.0f, 0.0f, 0.0f } );
    manager.tick( 0.016f );
    SW_EXPECT_EQUAL( static_cast<size_t>( 2 ), wall._pListener->_listBeginOther.size() );
    manager.endPlay();
}

/**
 * @brief [BoxCollider2DTest] 트리거 콜라이더도 겹침을 내고, 받는 쪽은 상대가 트리거였는지 안다
 * @details 겹침 훅이 상대 오브젝트 하나만 받으면 감지 범위(트리거)와 몸(막는 콜라이더)을 가를 수 없다. 그래서 `OverlapInfo` 가 양쪽 콜라이더의
 *          트리거 여부를 싣는다(유니티 `isTrigger` · 언리얼 Overlap 반응).
 */
SW_TEST_CASE( BoxCollider2DTest, TriggerCollidersReportOverlapsAndSaySo )
{
    sw::GameObjectManager manager;
    const OverlapProbe    sensor = spawnProbe( manager, "Sensor", 0.0f );
    const OverlapProbe    body   = spawnProbe( manager, "Body", 0.5f );
    SW_ASSERT_NOT_NULL( sensor._pListener );
    SW_ASSERT_NOT_NULL( body._pListener );
    sensor._pCollider->setTrigger( true );
    SW_EXPECT_TRUE( sensor._pCollider->isTrigger() );

    manager.beginPlay();
    manager.tick( 0.016f );
    SW_ASSERT_EQUAL( static_cast<size_t>( 1 ), body._pListener->_listBeginOtherTrigger.size() );
    SW_EXPECT_TRUE( body._pListener->_listBeginOtherTrigger[0] );
    SW_ASSERT_EQUAL( static_cast<size_t>( 1 ), sensor._pListener->_listBeginOtherTrigger.size() );
    SW_EXPECT_FALSE( sensor._pListener->_listBeginOtherTrigger[0] );
    manager.endPlay();
}

/**
 * @brief [BoxCollider2DTest] 순수 기하(`overlapsBounds`)와 레이어 반영 겹침(`isTouching`)은 각자 바디 등록 전후로 같은 답이고, 레이어가 막는 쌍에서 서로 다르다
 * @details 판정 하나가 바디 등록 여부로 갈리면 시작 전(에디터 · 시험)은 상자만, 시작 뒤(플레이)는 물리의 레이어 행렬까지 봐서 같은 쌍을 물어도 Play 를
 *          누르면 답이 바뀐다. 유니티처럼 `Bounds.Intersects`(기하) 와 `Collider2D.IsTouching`(물리 규칙) 을 이름이 다른 두 함수로 둔다.
 */
SW_TEST_CASE( BoxCollider2DTest, GeometricAndLayerOverlapAnswerTheSameBeforeAndAfterBodiesExist )
{
    sw::GameObjectManager manager;
    manager.getOverlapWorld2D().getPhysicsWorld().layers().setLayerCollision( 1, 2, false );
    const OverlapProbe blocked = spawnProbe( manager, "Blocked", 0.0f );
    const OverlapProbe blocker = spawnProbe( manager, "Blocker", 0.5f );
    const OverlapProbe allowed = spawnProbe( manager, "Allowed", -0.5f );
    const OverlapProbe faraway = spawnProbe( manager, "Faraway", 10.0f );
    SW_ASSERT_NOT_NULL( blocked._pCollider );
    SW_ASSERT_NOT_NULL( blocker._pCollider );
    SW_ASSERT_NOT_NULL( allowed._pCollider );
    SW_ASSERT_NOT_NULL( faraway._pCollider );
    blocked._pCollider->setColliderType( 1 );
    blocker._pCollider->setColliderType( 2 );
    allowed._pCollider->setColliderType( 1 );

    for ( int32 phaseIndex = 0; phaseIndex < 2; ++phaseIndex )
    {
        // 0: 시작 전(바디 없음) · 1: 시작해 한 번 step 한 뒤(바디 있음)
        SW_EXPECT_TRUE( blocked._pCollider->overlapsBounds( blocker._pCollider ) );
        SW_EXPECT_FALSE( blocked._pCollider->isTouching( blocker._pCollider ) );
        SW_EXPECT_FALSE( blocker._pCollider->isTouching( blocked._pCollider ) );
        SW_EXPECT_TRUE( blocked._pCollider->overlapsBounds( allowed._pCollider ) );
        SW_EXPECT_TRUE( blocked._pCollider->isTouching( allowed._pCollider ) );
        SW_EXPECT_FALSE( blocked._pCollider->overlapsBounds( faraway._pCollider ) );
        SW_EXPECT_FALSE( blocked._pCollider->isTouching( faraway._pCollider ) );
        SW_EXPECT_FALSE( blocked._pCollider->isTouching( nullptr ) );
        SW_EXPECT_FALSE( blocked._pCollider->overlapsBounds( nullptr ) );

        if ( phaseIndex == 0 )
        {
            manager.beginPlay();
            manager.tick( 0.016f );
            // 바디가 생겼다는 증거: 허용된 쌍은 겹침 이벤트를 받았고, 막힌 쌍은 받지 않았다.
            SW_EXPECT_EQUAL( static_cast<size_t>( 1 ), allowed._pListener->_listBeginOther.size() );
            SW_EXPECT_EQUAL( static_cast<size_t>( 0 ), blocker._pListener->_listBeginOther.size() );
        }
    }
    manager.endPlay();

    // 매니저에 등록되지 않은 콜라이더(물리 월드 없음)는 기본 행렬(모든 쌍 허용)로 잰다.
    sw::BoxCollider2DComponent looseA;
    sw::BoxCollider2DComponent looseB;
    looseA.setOffsetScale( sw::float2{ 1.0f, 1.0f } );
    looseB.setOffsetScale( sw::float2{ 1.0f, 1.0f } );
    looseA.setColliderType( 1 );
    looseB.setColliderType( 2 );
    SW_EXPECT_TRUE( looseA.isTouching( &looseB ) );

    // 점 · 사각형 판정은 경계를 포함한다.
    SW_EXPECT_TRUE( blocked._pCollider->containsPoint( sw::float2{ 0.5f, 0.0f } ) );
    SW_EXPECT_FALSE( blocked._pCollider->containsPoint( sw::float2{ 0.6f, 0.0f } ) );
    SW_EXPECT_TRUE( blocked._pCollider->overlapsBounds( sw::float2{ 0.5f, 0.5f }, sw::float2{ 2.0f, 2.0f } ) );
    SW_EXPECT_FALSE( blocked._pCollider->overlapsBounds( sw::float2{ 0.6f, 0.0f }, sw::float2{ 2.0f, 2.0f } ) );
}
