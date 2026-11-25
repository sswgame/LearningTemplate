// ShooterEnemyPoolResetTest — 슈터 적을 풀(`PrefabPool`)에 돌려주면 쓰러진 적(체력 · 단계 · 래그돌 · 애니메이터 쓰러짐)이 갓 스폰한 적과 같아져 다시 `launch` 할 수 있다.
// 복제 모습이 끈 폰의 자동 빙의도 되돌린다. 인스턴스는 프리팹 대신 같은 컴포넌트를 코드로 단 오브젝트다(nogpu, 에셋 없음).
#include "pch.h"

#include "Core/Delegate/Subscription.h"
#include "Core/TestFramework/TestFramework.h"

#include "Engine/Animation/Component/SkeletalAnimatorComponent.h"
#include "Engine/Character/Hit/CharacterHit.h"
#include "Engine/Character/Hit/RagdollComponent.h"
#include "Engine/Object/Component/SceneComponent.h"
#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManager.h"

#include "GameFramework/Runtime/Character/Control/Pawn/PawnComponent.h"
#include "GameFramework/Runtime/Foundation/GameMode/PrefabPool.h"
#include "GameFramework/Runtime/Foundation/GameMode/Tests/PrefabPoolComponentSpawner.h"

#include "Games/Playground/Shooter3D/ShooterBodyMovementComponent.h"
#include "Games/Playground/Shooter3D/ShooterEnemyComponent.h"

using namespace sw;

namespace
{
    struct ShooterEnemyPoolResetTestUtil
    {
        /** @brief 적 프리팹의 규칙 · 몸 컴포넌트를 단 오브젝트를 만드는 풀입니다(그리기 · 내비 · 외형은 에셋이 있어야 해서 뺀다). 폰은 프리팹처럼 AI 가 자동 빙의한다. */
        static PrefabPool makePool()
        {
            return PrefabPoolComponentSpawner<SkeletalAnimatorComponent, RagdollComponent, ShooterEnemyComponent, PawnComponent, ShooterBodyMovementComponent>::makePool(
                4, []( GameObject& object )
            { object.getComponent<PawnComponent>()->setAutoPossess( PawnAutoPossess::AI ); } );
        }

        /** @brief 쓰러뜨리는 맞음입니다. */
        static HitInfo makeFatalDamage()
        {
            HitInfo hit;
            hit._damage    = 1000.0f;
            hit._direction = float3{ 0.0f, 0.0f, 1.0f };
            hit._impulse   = 5.0f;
            return hit;
        }
    };
    using Util = ShooterEnemyPoolResetTestUtil;
} // namespace

/** @brief [ShooterEnemyPoolResetTest] 쓰러진 적을 돌려주고 다시 꺼내면 같은 오브젝트가 갓 스폰한 적(launch 전, 래그돌 Animated, Dead 파라미터 없음)이고 다시 launch 된다 */
SW_TEST_CASE( ShooterEnemyPoolResetTest, DeadEnemyComesBackAsFreshSpawn )
{
    GameObjectManager manager;
    PrefabPool        pool    = Util::makePool();
    GameObject*       pObject = pool.acquire( manager, "QuadShell" );
    SW_REQUIRE_NOT_NULL( pObject ); // 처음 만든 인스턴스로 풀 계약을 확인했다 — 모든 컴포넌트가 되돌리기를 구현한다
    ShooterEnemyComponent*     pEnemy    = pObject->getComponent<ShooterEnemyComponent>();
    RagdollComponent*          pRagdoll  = pObject->getComponent<RagdollComponent>();
    SkeletalAnimatorComponent* pAnimator = pObject->getComponent<SkeletalAnimatorComponent>();
    SW_REQUIRE_TRUE( pEnemy != nullptr && pRagdoll != nullptr && pAnimator != nullptr );

    SW_REQUIRE_TRUE( pEnemy->launch( GameObjectHandle{
    },
                                     AbsolutePosition{ float3{ 1.0f, 0.0f, 2.0f } }, 0.5f, 30.0f, 3.0f ) );
    pEnemy->setNetEntityID( 7 );
    CharacterRayHit::deliverHit( *pObject, Util::makeFatalDamage() );    // 무기 판정이 보내는 자리(틱 밖)
    pAnimator->getParameters().setBool( hashed_string( "Dead" ), true ); // 적의 틱이 쓰는 값이다
    SW_REQUIRE_TRUE( pEnemy->isDead() );
    SW_REQUIRE_TRUE( pRagdoll->getState() == RagdollState::Ragdoll );

    pool.release( manager, pObject->getHandle() );
    SW_REQUIRE_TRUE( pool.acquire( manager, "QuadShell" ) == pObject );
    SW_EXPECT_EQUAL( uint64{ 1 }, pool.getCreatedCount().get() );
    SW_EXPECT_FALSE( pEnemy->isLaunched() );
    SW_EXPECT_FALSE( pEnemy->isDead() );
    SW_EXPECT_TRUE( pEnemy->getPhase() == ShooterEnemyPhase::Rising );
    SW_EXPECT_EQUAL( 0u, pEnemy->getNetEntityID() );
    SW_EXPECT_TRUE( pRagdoll->getState() == RagdollState::Animated );
    SW_EXPECT_FALSE( pAnimator->getParameters().isTrue( hashed_string( "Dead" ) ) );

    SW_EXPECT_TRUE( pEnemy->launch( GameObjectHandle{
    },
                                    AbsolutePosition{ float3{ 0.0f, 0.0f, 0.0f } }, 0.0f, 30.0f, 3.0f ) );
    SW_EXPECT_TRUE( pEnemy->isAlive() );
}

/** @brief [ShooterEnemyPoolResetTest] 몸 멈춤은 건 쪽마다 따로다 — 다른 쪽이 멈춘 몸은 적을 돌려줘도 멈춰 있고(제 몫만 푼다), 그쪽이 놓아야 움직인다 */
SW_TEST_CASE( ShooterEnemyPoolResetTest, BodySuspensionsAreOwnedSeparately )
{
    GameObjectManager manager;
    PrefabPool        pool    = Util::makePool();
    GameObject*       pObject = pool.acquire( manager, "QuadShell" );
    SW_REQUIRE_NOT_NULL( pObject );
    ShooterEnemyComponent*        pEnemy    = pObject->getComponent<ShooterEnemyComponent>();
    ShooterBodyMovementComponent* pMovement = pObject->getComponent<ShooterBodyMovementComponent>();
    SW_REQUIRE_TRUE( pEnemy != nullptr && pMovement != nullptr );
    SW_EXPECT_FALSE( pMovement->isSuspended() );

    SW_REQUIRE_TRUE( pEnemy->launch( GameObjectHandle{
    },
                                     AbsolutePosition{ float3{ 0.0f, 0.0f, 0.0f } }, 0.0f, 30.0f, 3.0f ) );
    SW_EXPECT_TRUE( pMovement->isSuspended() ); // 일어나는 중
    Subscription focus = pMovement->suspend();
    pool.release( manager, pObject->getHandle() );
    SW_EXPECT_TRUE( pMovement->isSuspended() );
    focus.reset();
    SW_EXPECT_FALSE( pMovement->isSuspended() );
}

/** @brief [ShooterEnemyPoolResetTest] 복제 모습이 끈 폰의 자동 빙의(AI)는 돌려줄 때 프리팹 값으로 돌아온다 */
SW_TEST_CASE( ShooterEnemyPoolResetTest, ReplicaRestoresThePawnAutoPossess )
{
    GameObjectManager manager;
    PrefabPool        pool    = Util::makePool();
    GameObject*       pObject = pool.acquire( manager, "QuadShell" );
    SW_REQUIRE_NOT_NULL( pObject );
    ShooterEnemyComponent* pEnemy = pObject->getComponent<ShooterEnemyComponent>();
    PawnComponent*         pPawn  = pObject->getComponent<PawnComponent>();
    SW_REQUIRE_TRUE( pEnemy != nullptr && pPawn != nullptr );

    pEnemy->launchReplica( GameObjectHandle{
    },
                           AbsolutePosition{ float3{ 0.0f, 0.0f, 0.0f } }, 0.0f );
    SW_REQUIRE_TRUE( pPawn->getAutoPossess() == PawnAutoPossess::None );
    SW_EXPECT_TRUE( pObject->isTransient() ); // 서버의 적을 비춘 모습은 세이브에 실리지 않는다

    pool.release( manager, pObject->getHandle() );
    SW_EXPECT_TRUE( pPawn->getAutoPossess() == PawnAutoPossess::AI );
    SW_EXPECT_FALSE( pEnemy->isReplica() );
    SW_EXPECT_FALSE( pObject->isTransient() );
}

/** @brief [ShooterEnemyPoolResetTest] 월드 원점을 옮긴 씬에서 선 적의 시뮬레이션 자리는 절대 그대로이고, 모습만 원점만큼 옮긴 씬 자리에 선다 */
SW_TEST_CASE( ShooterEnemyPoolResetTest, ShiftedOriginMovesOnlyTheView )
{
    GameObjectManager manager;
    manager.setWorldOrigin( double3{ 100.0, 0.0, -50.0 } );
    GameObject* const      pObject = manager.createGameObject( hashed_string( "Enemy" ) );
    SceneComponent* const  pScene  = pObject->addComponent<SceneComponent>();
    ShooterEnemyComponent* pEnemy  = pObject->addComponent<ShooterEnemyComponent>();
    SW_REQUIRE_TRUE( pEnemy != nullptr && pScene != nullptr );

    SW_REQUIRE_TRUE( pEnemy->launch( GameObjectHandle{
    },
                                     AbsolutePosition{ float3{ 101.0f, 0.0f, -48.0f } }, 0.5f, 30.0f, 3.0f ) );
    SW_EXPECT_TRUE( pEnemy->getPosition()._value == float3( 101.0f, 0.0f, -48.0f ) );
    SW_EXPECT_TRUE( pScene->getWorldPosition() == float3( 1.0f, 0.0f, 2.0f ) );
}
