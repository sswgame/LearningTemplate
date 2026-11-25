// 군중 결과 쓰기(`NavAgentCrowdSync::writeAgentResult`)의 일괄 길(부모 없는 에이전트 → `applyTransformBatch` 한 번)이 예전 세터 길(`setWorldPosition` · `setLocalRotation`)과
// 같은 트랜스폼을 낸다. 쓰는 쪽: 게임 스레드(`SceneNavigation::completeFrameJob` — 군중 일을 기다린 뒤). 읽는 쪽: 군중 상태는 워커의 군중 진행이 썼고 게임 스레드가 읽는다.
#include "pch.h"

#include "Core/Container/vector.h"
#include "Core/TestFramework/TestFramework.h"

#include "Engine/Navigation/Component/NavMeshAgentComponent.h"
#include "Engine/Navigation/INavMesh.h"
#include "Engine/Navigation/Scene/NavAgentCrowdSync.h"
#include "Engine/Navigation/Scene/NavMeshRuntime.h"
#include "Engine/Object/Component/SceneComponent.h"
#include "Engine/Object/Component/SceneTransformHierarchy.h"
#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManager.h"

using namespace sw;

namespace
{
    /** @brief 모든 에이전트에 같은 상태를 내는 군중입니다 — 쓰기 길만 본다(Detour 진행은 이 테스트의 일이 아니다). */
    class FixedStateCrowd final : public INavCrowd
    {
    public:
        float3 _receivedPosition{}; ///< 마지막으로 받은 자리
        uint32 _teleportCount{ 0 };
        uint32 _syncCount{ 0 };

        explicit FixedStateCrowd( const NavCrowdAgentState& state )
            : _state{ state }
        {
        }

        NavCrowdAgentID addAgent( const float3&, const NavCrowdAgentParams& ) override { return 0; }
        void            removeAgent( NavCrowdAgentID ) override {}
        void            updateAgentParams( NavCrowdAgentID, const NavCrowdAgentParams& ) override {}
        bool            requestMoveTarget( NavCrowdAgentID, const float3& ) override { return true; }
        void            resetMoveTarget( NavCrowdAgentID ) override {}
        void            teleportAgent( NavCrowdAgentID, const float3& position ) override
        {
            ++_teleportCount;
            _receivedPosition = position;
        }
        void syncAgentPosition( NavCrowdAgentID, const float3& position ) override
        {
            ++_syncCount;
            _receivedPosition = position;
        }
        void update( float32 ) override {}
        bool findAgentState( NavCrowdAgentID, NavCrowdAgentState& outState ) const override
        {
            outState = _state;
            return true;
        }
        void   setQueryFilter( const NavQueryFilter& ) override {}
        uint32 getActiveAgentCount() const override { return 1; }
        uint32 getMaxAgentCount() const override { return 1; }
        void   drawDebug( IPhysicsDebugRenderer&, bool, bool ) const override {}

    private:
        NavCrowdAgentState _state;
    };
} // namespace

/**
 * @brief [NavAgentCrowdSyncTest] 부모 없는 에이전트는 쓰기 목록 한 건으로, 원점 부모 아래 에이전트는 세터로 쓰는데 두 길의 자리 · 회전이 같다
 * @details 원점 · 회전 없는 부모 아래라 월드 = 로컬이므로 예전 세터 길과 같은 값이 나와야 한다. 자리는 군중의 자리, 회전은 속도 쪽으로 한 프레임만큼 돈 각이다.
 */
SW_TEST_CASE( NavAgentCrowdSyncTest, BatchedWriteMatchesSetterWrite )
{
    NavCrowdAgentState state;
    state._position   = float3{ 3.0f, 0.5f, -2.0f };
    state._velocity   = float3{ 2.0f, 0.0f, 0.0f };
    state._bOnNavMesh = true;
    NavMeshRuntime runtime;
    runtime._pCrowd = make_unique<FixedStateCrowd>( state );

    GameObjectManager manager;
    GameObject*       pLoose = manager.createGameObject( hashed_string( "Loose" ) );
    SW_REQUIRE_NOT_NULL( pLoose );
    SceneComponent* pLooseScene = pLoose->addComponent<SceneComponent>();
    SW_REQUIRE_NOT_NULL( pLooseScene );
    NavMeshAgentComponent* pLooseAgent = pLoose->addComponent<NavMeshAgentComponent>();
    SW_REQUIRE_NOT_NULL( pLooseAgent );

    GameObject* pParent = manager.createGameObject( hashed_string( "Parent" ) );
    GameObject* pChild  = manager.createGameObject( hashed_string( "Child" ) );
    SW_REQUIRE_NOT_NULL( pParent );
    SW_REQUIRE_NOT_NULL( pChild );
    (void)pParent->addComponent<SceneComponent>();
    SceneComponent* pChildScene = pChild->addComponent<SceneComponent>();
    SW_REQUIRE_NOT_NULL( pChildScene );
    SW_REQUIRE_TRUE( pChild->attachToParent( pParent ) );
    NavMeshAgentComponent* pChildAgent = pChild->addComponent<NavMeshAgentComponent>();
    SW_REQUIRE_NOT_NULL( pChildAgent );

    // `SceneNavigation::completeFrameJob` 과 같은 차례: 쓰기 → 일괄 적용 → 펼치기 → 쓴 번호 적기.
    constexpr float32           kDeltaTime = 0.1f;
    vector<SceneTransformWrite> listWrite;
    SW_REQUIRE_TRUE( NavAgentCrowdSync::writeAgentResult( *pLooseAgent, runtime, kDeltaTime, listWrite ) );
    SW_REQUIRE_TRUE( NavAgentCrowdSync::writeAgentResult( *pChildAgent, runtime, kDeltaTime, listWrite ) );
    SW_REQUIRE_EQUAL( size_t{ 1 }, listWrite.size() ); // 부모 있는 쪽은 세터로 이미 썼다
    (void)manager.applyTransformBatch( listWrite.data(), static_cast<uint32>( listWrite.size() ) );
    if ( manager.hasDirtySceneTransforms() )
        manager.flushSceneTransforms();
    NavAgentCrowdSync::recordWrittenTransform( *pLooseAgent );
    NavAgentCrowdSync::recordWrittenTransform( *pChildAgent );

    const float3 loosePosition = pLooseScene->getWorldPosition();
    const float3 childPosition = pChildScene->getWorldPosition();
    SW_EXPECT_EQUAL( state._position._x, loosePosition._x );
    SW_EXPECT_EQUAL( state._position._y, loosePosition._y );
    SW_EXPECT_EQUAL( state._position._z, loosePosition._z );
    SW_EXPECT_EQUAL( childPosition._x, loosePosition._x );
    SW_EXPECT_EQUAL( childPosition._y, loosePosition._y );
    SW_EXPECT_EQUAL( childPosition._z, loosePosition._z );

    const float3 looseRotation = pLooseScene->getLocalRotation();
    const float3 childRotation = pChildScene->getLocalRotation();
    SW_EXPECT_TRUE( looseRotation._y != 0.0f ); // 속도 쪽으로 돌았다
    SW_EXPECT_EQUAL( childRotation._x, looseRotation._x );
    SW_EXPECT_EQUAL( childRotation._y, looseRotation._y );
    SW_EXPECT_EQUAL( childRotation._z, looseRotation._z );

    SW_EXPECT_EQUAL( state._position._x, pLooseAgent->getAgentPosition()._x );
    SW_EXPECT_EQUAL( state._velocity._x, pChildAgent->getVelocity()._x );
}

/**
 * @brief [NavAgentCrowdSyncTest] 결함: `Transform` 에이전트를 바깥이 0.5 m 보다 작게 밀면(넉백 · 컨베이어) 군중이 모른 채 제자리로 되돌렸고, 그보다 짧은 순간이동은 길을 다시 찾지 않았다
 *        — 밀림은 군중이 받아들이고, 순간이동이라고 알리면 거리와 무관하게 그 자리에서 다시 찾는다
 */
SW_TEST_CASE( NavAgentCrowdSyncTest, OutsidePushAndTeleportReachTheCrowd )
{
    NavCrowdAgentState state;
    state._position   = float3{ 3.0f, 0.0f, -2.0f };
    state._bOnNavMesh = true;
    vector<unique_ptr<NavMeshRuntime>> listRuntime;
    listRuntime.push_back( make_unique<NavMeshRuntime>() );
    NavMeshRuntime& runtime                = *listRuntime.back();
    runtime._pNavMesh                      = NavMeshBackend::createNavMesh();
    unique_ptr<FixedStateCrowd> ownedCrowd = make_unique<FixedStateCrowd>( state );
    FixedStateCrowd* const      pCrowd     = ownedCrowd.get();
    runtime._pCrowd                        = std::move( ownedCrowd );

    GameObjectManager      manager;
    GameObject*            pObject = manager.createGameObject( hashed_string( "Walker" ) );
    SceneComponent*        pScene  = pObject->addComponent<SceneComponent>();
    NavMeshAgentComponent* pAgent  = pObject->addComponent<NavMeshAgentComponent>();
    const auto             step    = [&]()
    {
        NavAgentCrowdSync::syncAgentIntoCrowd( *pAgent, runtime, listRuntime );
        vector<SceneTransformWrite> listWrite;
        SW_EXPECT_TRUE( NavAgentCrowdSync::writeAgentResult( *pAgent, runtime, 0.1f, listWrite ) );
        (void)manager.applyTransformBatch( listWrite.data(), static_cast<uint32>( listWrite.size() ) );
        if ( manager.hasDirtySceneTransforms() )
            manager.flushSceneTransforms();
        NavAgentCrowdSync::recordWrittenTransform( *pAgent );
    };
    step(); // 군중에 들었다
    step(); // 스스로 쓴 자리는 바깥의 이동이 아니다
    SW_EXPECT_EQUAL( 0u, pCrowd->_syncCount );
    SW_EXPECT_EQUAL( 0u, pCrowd->_teleportCount );

    pScene->setWorldPosition( state._position + float3{ 0.2f, 0.0f, 0.0f } );
    step();
    SW_EXPECT_EQUAL( 1u, pCrowd->_syncCount );
    SW_EXPECT_NEAR( 3.2f, pCrowd->_receivedPosition._x, 1.0e-4f );
    SW_EXPECT_EQUAL( 0u, pCrowd->_teleportCount );

    pScene->teleportTo( state._position + float3{ 0.3f, 0.0f, 0.0f } );
    step();
    SW_EXPECT_EQUAL( 1u, pCrowd->_teleportCount );
    SW_EXPECT_NEAR( 3.3f, pCrowd->_receivedPosition._x, 1.0e-4f );
    step();
    SW_EXPECT_EQUAL( 1u, pCrowd->_teleportCount );
    SW_EXPECT_EQUAL( 1u, pCrowd->_syncCount );
}
