// GrabberTest — 집기 · 던지기: 트랜스폼 폴백(손을 따르고 놓은 자리에 남는다), 강체(손에서는 키네마틱, 놓으면 동적 + 속도), 게임이 건 백엔드,
// 버튼 한 번에 집기 · 놓기를 번갈아 하는 길(가장 가까운 태그 대상 · 닿는 거리 · 던지는 방향)(nogpu).
#include "pch.h"

#include "Core/Subsystem/SubsystemScope.h"
#include "Core/Testing/TestFramework.h"

#include "Engine/Object/Component/SceneComponent.h"
#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManager.h"
#include "Engine/Object/GameObject/Serialization/ObjectStateSerializer.h"
#include "Engine/Physics/Component/RigidBodyComponent.h"

#include "GameFramework/Runtime/Character/Control/Controller/RemoteControllerComponent.h"
#include "GameFramework/Runtime/Character/Control/Intent/ControlIntent.h"
#include "GameFramework/Runtime/Character/Control/Pawn/PawnComponent.h"
#include "GameFramework/Runtime/Foundation/Framework/GameService.h"
#include "GameFramework/Runtime/Gameplay/Interaction/GrabberComponent.h"

using namespace sw;

namespace
{
    /** @brief 부른 것을 적는 집기 백엔드입니다(게임이 거는 서비스 자리). */
    class RecordingGrabPhysics final : public IGrabPhysics
    {
    public:
        float3             _lastVelocity{};
        const GameObject*  _pAttached{ nullptr };
        uint32             _releaseCount{ 0 };
        bool               _bRefuses{ false }; ///< NOLINT(sw-bool-flag): 테스트 집합체다 — C++17 비트필드는 기본값을 둘 수 없다
        [[nodiscard]] bool attach( GameObject& holder, GameObject& target, const float3& localOffset ) override
        {
            (void)holder;
            (void)localOffset;
            if ( _bRefuses )
                return false;
            _pAttached = &target;
            return true;
        }
        void release( GameObject& holder, GameObject& target, const float3& velocity ) override
        {
            (void)holder;
            (void)target;
            _pAttached    = nullptr;
            _lastVelocity = velocity;
            ++_releaseCount;
        }
    };

    struct GrabberTestUtil
    {
        static constexpr float32 kFrame = 1.0f / 60.0f;

        /** @brief 씬 컴포넌트를 단 오브젝트를 @p position 에 둡니다. */
        static GameObject* spawnAt( GameObjectManager& manager, const utf8* pName, const float3& position )
        {
            GameObject* pObject = manager.createGameObject( hashed_string( pName ) );
            pObject->addComponent<SceneComponent>()->setWorldPosition( position );
            return pObject;
        }

        /** @brief 저작 값(버튼 Carry · 태그 Test.Grabbable · 닿는 거리 2 m · 던지는 속도 4)을 든 집는 쪽을 원점에 둡니다. 값은 씬 데이터와 같은 길로 읽는다. */
        static GrabberComponent* spawnButtonGrabber( GameObjectManager& manager )
        {
            (void)TagID::request( "Test.Grabbable" );
            GameObject* pHolder = manager.createGameObject( hashed_string( "Holder" ) );
            const bool  bLoaded = ObjectStateSerializer::loadFromXMLString( pHolder,
                                                                            R"(<GameObject _schemaVersion="0" _name="Holder" _bActive="true"><_listComponent><SceneComponent _localPosition="0,0,0" />)"
                                                                             R"(<PawnComponent _autoPossess="None"><_listButton><item>Carry</item></_listButton></PawnComponent>)"
                                                                             R"(<GrabberComponent _grabButton="Carry" _reach="2" _throwSpeed="4"><_grabbableTags><_listTag><item>Test.Grabbable</item></_listTag></_grabbableTags></GrabberComponent>)"
                                                                             R"(</_listComponent></GameObject>)",
                                                                            ObjectLoadContext{ SerializePurpose::Authoring } );
            return bLoaded ? pHolder->getComponent<GrabberComponent>() : nullptr;
        }

        static void expectPosition( const float3& expected, const GameObject& object )
        {
            const float3 position = object.getPrimarySceneComponent()->getWorldPosition();
            SW_EXPECT_NEAR( expected._x, position._x, 1.0e-3f );
            SW_EXPECT_NEAR( expected._y, position._y, 1.0e-3f );
            SW_EXPECT_NEAR( expected._z, position._z, 1.0e-3f );
        }
    };
    using Util = GrabberTestUtil;
} // namespace

/** @brief [GrabberTest] 강체 없는 대상은 손(오프셋)을 따라다니고 놓으면 그 자리에 남는다. 자기 자신 · 이미 든 채로는 집지 못하고, 빈손으로는 놓지 못한다 */
SW_TEST_CASE( GrabberTest, PlainObjectFollowsTheHandAndStaysWhereDropped )
{
    GameObjectManager manager;
    GameObject*       pHolder  = Util::spawnAt( manager, "Holder", float3{ 10.0f, 0.0f, 0.0f } );
    GrabberComponent* pGrabber = pHolder->addComponent<GrabberComponent>();
    GameObject*       pCrate   = Util::spawnAt( manager, "Crate", float3{ 12.0f, 0.0f, 0.0f } );
    GameObject*       pOther   = Util::spawnAt( manager, "Other", float3{ 11.0f, 0.0f, 0.0f } );
    pGrabber->setHoldOffset( float3{ 0.0f, 1.0f, 0.5f } );

    SW_EXPECT_FALSE( pGrabber->dropHeld() );
    SW_EXPECT_FALSE( pGrabber->grab( *pHolder ) );
    SW_REQUIRE_TRUE( pGrabber->grab( *pCrate ) );
    SW_EXPECT_TRUE( pGrabber->isHolding() );
    SW_EXPECT_TRUE( pGrabber->getHeld() == pCrate->getHandle() );
    SW_EXPECT_FALSE( pGrabber->grab( *pOther ) ); // 하나만 든다
    Util::expectPosition( float3{ 10.0f, 1.0f, 0.5f }, *pCrate );

    pHolder->getPrimarySceneComponent()->setWorldPosition( float3{ 20.0f, 2.0f, 0.0f } );
    Util::expectPosition( float3{ 20.0f, 3.0f, 0.5f }, *pCrate );

    SW_REQUIRE_TRUE( pGrabber->dropHeld() );
    SW_EXPECT_FALSE( pGrabber->isHolding() );
    pHolder->getPrimarySceneComponent()->setWorldPosition( float3{} );
    Util::expectPosition( float3{ 20.0f, 3.0f, 0.5f }, *pCrate );
    SW_EXPECT_FALSE( pGrabber->dropHeld() );
}

/** @brief [GrabberTest] 남이 쥔 것은 집지 못하고(빼앗으면 앞 손이 놓을 때 남의 손에서 떨어진다), 쥔 쪽이 사라지면 든 것은 함께 지워지지 않고 그 자리에 남는다 */
SW_TEST_CASE( GrabberTest, HeldObjectHasOneHolderAndOutlivesIt )
{
    GameObjectManager manager;
    GameObject*       pHolder  = Util::spawnAt( manager, "Holder", float3{ 0.0f, 0.0f, 0.0f } );
    GrabberComponent* pGrabber = pHolder->addComponent<GrabberComponent>();
    GrabberComponent* pSecond  = Util::spawnAt( manager, "Second", float3{ 1.0f, 0.0f, 0.0f } )->addComponent<GrabberComponent>();
    GameObject*       pCrate   = manager.createGameObject( hashed_string( "Crate" ) );
    auto*             pBody    = pCrate->addComponent<RigidBodyComponent>();
    pBody->setWorldPosition( float3{ 0.5f, 0.0f, 0.0f } );
    manager.beginPlay();
    manager.update( Util::kFrame );
    SW_REQUIRE_TRUE( pGrabber->grab( *pCrate ) );
    SW_EXPECT_TRUE( GrabberComponent::findHolderOf( *pCrate ) == pGrabber );
    SW_EXPECT_FALSE( pSecond->grab( *pCrate ) );
    SW_EXPECT_FALSE( pSecond->isHolding() );

    const GameObjectHandle crate = pCrate->getHandle();
    pHolder->getPrimarySceneComponent()->setWorldPosition( float3{ 5.0f, 0.0f, 0.0f } );
    manager.destroyGameObject( pHolder );
    manager.update( Util::kFrame );
    manager.update( Util::kFrame );
    SW_REQUIRE_TRUE( manager.resolveGameObject( crate ) == pCrate );
    SW_EXPECT_TRUE( pCrate->getPrimarySceneComponent()->getParent() == nullptr );
    SW_EXPECT_FALSE( pCrate->isKeptWhenParentDestroyed() );
    SW_EXPECT_TRUE( pBody->getBodyType() == PhysicsBodyType::Dynamic ); // 물리로 돌아갔다
    SW_EXPECT_TRUE( pSecond->grab( *pCrate ) );
}

/** @brief [GrabberTest] 강체는 손에 든 동안 키네마틱이고, 던지면 동적으로 돌아와 그 속도로 난다 */
SW_TEST_CASE( GrabberTest, RigidBodyIsKinematicInTheHandAndThrown )
{
    GameObjectManager manager;
    GameObject*       pHolder  = Util::spawnAt( manager, "Holder", float3{ 0.0f, 10.0f, 0.0f } );
    GrabberComponent* pGrabber = pHolder->addComponent<GrabberComponent>();
    GameObject*       pCrate   = manager.createGameObject( hashed_string( "Crate" ) );
    auto*             pBody    = pCrate->addComponent<RigidBodyComponent>();
    pBody->setWorldPosition( float3{ 1.0f, 10.0f, 0.0f } );
    manager.beginPlay();
    manager.update( Util::kFrame );

    SW_REQUIRE_TRUE( pGrabber->grab( *pCrate ) );
    SW_EXPECT_TRUE( pBody->getBodyType() == PhysicsBodyType::Kinematic );
    for ( uint32 frame = 0; frame < 3; ++frame )
    {
        manager.update( Util::kFrame );
    }
    Util::expectPosition( float3{ 0.0f, 11.0f, 0.6f }, *pCrate ); // 기본 손 자리 — 중력에 떨어지지 않는다

    SW_REQUIRE_TRUE( pGrabber->throwHeld( float3{ 6.0f, 0.0f, 0.0f } ) );
    SW_EXPECT_TRUE( pBody->getBodyType() == PhysicsBodyType::Dynamic );
    const float32 thrownFrom = pBody->getWorldPosition()._x;
    for ( uint32 frame = 0; frame < 30; ++frame )
    {
        manager.update( Util::kFrame );
    }
    SW_EXPECT_TRUE( pBody->getLinearVelocity()._x > 4.0f );
    SW_EXPECT_TRUE( pBody->getWorldPosition()._x > thrownFrom + 1.5f ); // 6 m/s 로 반 초
    SW_EXPECT_TRUE( pBody->getWorldPosition()._y < 11.0f );             // 손을 떠나 중력을 받는다
}

/** @brief [GrabberTest] 게임이 건 백엔드가 집기 · 놓기를 받고, 백엔드가 거절하면 든 것이 없다. 든 것이 사라지면 놓기는 부르지 않고 빈손이 된다 */
SW_TEST_CASE( GrabberTest, BoundBackendReceivesTheCalls )
{
    RecordingGrabPhysics backend;
    SubsystemScope       scope;
    SW_REQUIRE_TRUE( scope.bind<IGrabPhysics>( &backend ) );
    game::ScopedGameScope scopedScope( scope );
    GameObjectManager     manager;
    GrabberComponent*     pGrabber = Util::spawnAt( manager, "Holder", float3{} )->addComponent<GrabberComponent>();
    GameObject*           pCrate   = Util::spawnAt( manager, "Crate", float3{ 1.0f, 0.0f, 0.0f } );

    backend._bRefuses = true;
    SW_EXPECT_FALSE( pGrabber->grab( *pCrate ) );
    SW_EXPECT_FALSE( pGrabber->isHolding() );

    backend._bRefuses = false;
    SW_REQUIRE_TRUE( pGrabber->grab( *pCrate ) );
    SW_EXPECT_TRUE( backend._pAttached == pCrate );
    SW_EXPECT_TRUE( pGrabber->throwHeld( float3{ 1.0f, 2.0f, 3.0f } ) );
    SW_EXPECT_EQUAL( 1u, backend._releaseCount );
    SW_EXPECT_NEAR( 2.0f, backend._lastVelocity._y, 1.0e-5f );

    SW_REQUIRE_TRUE( pGrabber->grab( *pCrate ) );
    manager.destroyGameObject( pCrate );
    manager.update( Util::kFrame );
    SW_EXPECT_FALSE( pGrabber->dropHeld() );
    SW_EXPECT_FALSE( pGrabber->isHolding() );
    SW_EXPECT_EQUAL( 1u, backend._releaseCount );
}

/** @brief [GrabberTest] 번갈아 하기: 닿는 거리 안에서 태그를 가진 가장 가까운 것을 집고(꺼진 것 · 태그 없는 것 · 먼 것은 거른다), 다시 하면 앞(+Z)으로 던진다 */
SW_TEST_CASE( GrabberTest, ToggleTakesTheNearestTaggedObjectInReach )
{
    RecordingGrabPhysics backend;
    SubsystemScope       scope;
    SW_REQUIRE_TRUE( scope.bind<IGrabPhysics>( &backend ) );
    game::ScopedGameScope scopedScope( scope );
    GameObjectManager     manager;
    GrabberComponent*     pGrabber = Util::spawnButtonGrabber( manager );
    SW_REQUIRE_NOT_NULL( pGrabber );
    const TagID grabbable = TagID::request( "Test.Grabbable" );
    (void)Util::spawnAt( manager, "Untagged", float3{ 0.2f, 0.0f, 0.0f } );
    GameObject* pFar = Util::spawnAt( manager, "Far", float3{ 5.0f, 0.0f, 0.0f } );
    pFar->addTag( grabbable );
    SW_EXPECT_FALSE( pGrabber->toggleGrab() ); // 닿는 것이 없다

    GameObject* pNear = Util::spawnAt( manager, "Near", float3{ 1.0f, 0.0f, 0.0f } );
    pNear->addTag( grabbable );
    GameObject* pNearest = Util::spawnAt( manager, "Nearest", float3{ 0.5f, 0.0f, 0.0f } );
    pNearest->addTag( grabbable );
    SW_EXPECT_TRUE( pGrabber->findNearestGrabbable() == pNearest );
    pNearest->setActive( false );
    SW_EXPECT_TRUE( pGrabber->findNearestGrabbable() == pNear );

    SW_REQUIRE_TRUE( pGrabber->toggleGrab() );
    SW_EXPECT_TRUE( backend._pAttached == pNear );
    SW_REQUIRE_TRUE( pGrabber->toggleGrab() );
    SW_EXPECT_FALSE( pGrabber->isHolding() );
    SW_EXPECT_NEAR( 4.0f, backend._lastVelocity._z, 1.0e-3f );
    SW_EXPECT_NEAR( 0.0f, backend._lastVelocity._x, 1.0e-3f );
}

/** @brief [GrabberTest] 폰의 버튼은 발동한 한 번만 듣는다 — 누르고 있어도 다시 하지 않고, 다시 누르면 놓는다 */
SW_TEST_CASE( GrabberTest, PawnButtonPicksUpAndPutsDown )
{
    RecordingGrabPhysics backend;
    SubsystemScope       scope;
    SW_REQUIRE_TRUE( scope.bind<IGrabPhysics>( &backend ) );
    game::ScopedGameScope scopedScope( scope );
    GameObjectManager     manager;
    GrabberComponent*     pGrabber = Util::spawnButtonGrabber( manager );
    SW_REQUIRE_NOT_NULL( pGrabber );
    Util::spawnAt( manager, "Crate", float3{ 1.0f, 0.0f, 0.0f } )->addTag( TagID::request( "Test.Grabbable" ) );
    PawnComponent* pPawn = pGrabber->getOwner()->getComponent<PawnComponent>();
    SW_REQUIRE_NOT_NULL( pPawn );
    RemoteControllerComponent* pRemote = RemoteControllerComponent::spawnForConnection( manager, hashed_string( "Remote" ), 0, *pPawn );
    SW_REQUIRE_NOT_NULL( pRemote );
    manager.beginPlay();

    ControlIntent press;
    press.setButton( 0, true );
    press.addTrigger( 0, 0.0f, 0.0f, ControlViewTick::makePresent() );
    pRemote->receiveIntent( 0, press );
    manager.update( Util::kFrame );
    SW_EXPECT_TRUE( pGrabber->isHolding() );
    manager.update( Util::kFrame ); // 누르고 있다 — 발동은 지났다
    SW_EXPECT_TRUE( pGrabber->isHolding() );

    pRemote->receiveIntent( 1, ControlIntent{} );
    manager.update( Util::kFrame );
    SW_EXPECT_TRUE( pGrabber->isHolding() );
    pRemote->receiveIntent( 2, press );
    manager.update( Util::kFrame );
    SW_EXPECT_FALSE( pGrabber->isHolding() );
    SW_EXPECT_EQUAL( 1u, backend._releaseCount );
}
