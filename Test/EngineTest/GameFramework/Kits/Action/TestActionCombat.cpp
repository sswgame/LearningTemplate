#include "pch.h"

#include "Core/Event/EventDispatcher.h"
#include "Core/File/FileUtil.h"

#include "Engine/Object/Component/2D/BoxCollider2DComponent.h"
#include "Engine/Object/Component/SceneComponent.h"
#include "Engine/Object/GameObject/GameObjectManager.h"
#include "Engine/Object/GameObject/ObjectStateSerializer.h"
#include "Engine/Object/Prefab/PrefabAsset.h"
#include "Engine/Reflection/ReflectionCore.h"
#include "Engine/Scene/Scene.h"
#include "Engine/Scene/SceneDocument.h"
#include "Engine/Scene/SceneManager.h"
#include "Engine/Serialization/Format/Archive.h"

#include "EngineTest/StateReloadTestUtil.h"
#include "EngineTest/TestGameObjectMocks.h"

#include "GameFramework/Base/Foundation/Framework/GameEvents.h"
#include "GameFramework/Base/Foundation/Framework/GameService.h"
#include "GameFramework/Base/UI/Marker/DamageNumberComponent.h"
#include "GameFramework/Base/UI/Marker/HealthBarComponent.h"
#include "GameFramework/Kits/Genre/Action/ActionCombat/ActionCombatEvents.h"
#include "GameFramework/Kits/Genre/Action/ActionCombat/Catalog/MonsterCatalog.h"
#include "GameFramework/Kits/Genre/Action/ActionCombat/Component/MeleeHitboxComponent.h"
#include "GameFramework/Kits/Genre/Action/ActionCombat/Component/ProjectileComponent.h"
#include "GameFramework/Kits/Genre/Action/ActionCombat/Component/UnitStatsComponent.h"
#include "GameFramework/Kits/Genre/Action/ActionCombat/Rule/ActionRoom.h"

#include "TestFramework/TestFramework.h"

#include <thread>

// ActionCombat 킷의 피해 — 투사체 · 공격 판정이 `UnitStatsComponent::takeDamage` 한 길로 피해를 주고 `DamageAppliedEvent` 를 내며, 액션 룸이
// 상태가 바뀔 때 룸 이벤트를 낸다. 다른 단위들이 `GameFrameworkTest` 의 끝에 시험을 더하므로, 병합이 같은 자리에서 부딪히지 않게 킷의 시험을
// 따로 둔다.

namespace sw
{
    /** @brief 틱 **안에서** 대상 유닛에 피해를 한 번 주는 컴포넌트입니다 — 틱 직후로 미뤄진 피해가 쏜 쪽(instigator)을 들고 가는지 봅니다. */
    class MockStrikeInTickComponent : public Component
    {
    public:
        REFLECT_BODY();

        UnitStatsComponent* _pTarget{ nullptr };
        GameObjectHandle    _instigator;
        int32               _damage{ 0 };
        int32               _hpSeenInTick{ -1 }; ///< 피해를 준 **직후**(아직 틱 안) 본 HP — 미뤄졌으면 그대로다

        const TypeInfo* getTypeInfo() const override { return StaticType(); }
        void            onTick( float32 deltaTime ) override
        {
            Component::onTick( deltaTime );
            if ( _pTarget == nullptr )
                return;
            _pTarget->takeDamage( _damage, _instigator );
            _hpSeenInTick = _pTarget->getHp();
            _pTarget      = nullptr;
        }
    };

    inline const TypeInfo* MockStrikeInTickComponent::StaticType()
    {
        return makeMockComponentTypeInfo( &GameObject::addComponentTo<MockStrikeInTickComponent>, hashed_string( "MockStrikeInTickComponent" ), hashed_string( "sw::MockStrikeInTickComponent" ),
                                          sizeof( MockStrikeInTickComponent ) );
    }
} // namespace sw

using namespace sw;

namespace
{
    /** @brief 이 시험 동안만 이벤트 버스를 게임 서비스로 겁니다 — 피해 · 룸 이벤트가 여기로 온다. 어서션이 빠져나가도 풀린다. */
    struct ScopedEventDispatcherService
    {
        explicit ScopedEventDispatcherService( EventDispatcher& dispatcher ) { game::bindLocalService<EventDispatcher>( &dispatcher ); }
        ~ScopedEventDispatcherService() { game::unbindLocalService<EventDispatcher>(); }

        ScopedEventDispatcherService( const ScopedEventDispatcherService& )            = delete;
        ScopedEventDispatcherService& operator=( const ScopedEventDispatcherService& ) = delete;
    };

    /** @brief 이 시험 동안만 몬스터 카탈로그를 게임 서비스로 겁니다 — 액션 룸이 여기서 적을 읽는다. 어서션이 빠져나가도 풀린다. */
    struct ScopedMonsterCatalogService
    {
        explicit ScopedMonsterCatalogService( MonsterCatalog& catalog ) { game::bindLocalService<MonsterCatalog>( &catalog ); }
        ~ScopedMonsterCatalogService() { game::unbindLocalService<MonsterCatalog>(); }

        ScopedMonsterCatalogService( const ScopedMonsterCatalogService& )            = delete;
        ScopedMonsterCatalogService& operator=( const ScopedMonsterCatalogService& ) = delete;
    };

    /** @brief @p size 크기 콜라이더를 primary 씬 컴포넌트로 단 오브젝트를 (x, y) 에 만듭니다. */
    GameObject* spawnColliderObject( GameObjectManager& manager, const utf8* pName, float32 x, float32 y, const float2& size )
    {
        GameObject* pObject = manager.createGameObject( hashed_string( pName ) );
        if ( pObject == nullptr )
            return nullptr;
        BoxCollider2DComponent* pBox = pObject->addComponent<BoxCollider2DComponent>();
        if ( pBox == nullptr )
            return nullptr;
        pBox->setOffsetScale( size );
        pBox->setLocalPosition( float3( x, y, 0.0f ) );
        return pObject;
    }

    /** @brief 크기 1 콜라이더와 스탯(HP @p hp · 방어 @p defense · 맞은 뒤 무적 @p invincibleTime 초)을 단 유닛을 (x, 0) 에 만듭니다. */
    UnitStatsComponent* spawnUnit( GameObjectManager& manager, const utf8* pName, float32 x, int32 hp, int32 defense, float32 invincibleTime )
    {
        GameObject* pObject = spawnColliderObject( manager, pName, x, 0.0f, float2( 1.0f, 1.0f ) );
        if ( pObject == nullptr )
            return nullptr;
        UnitStatsComponent* pStats = pObject->addComponent<UnitStatsComponent>();
        if ( pStats != nullptr )
            pStats->setStats( hp, hp, 0, defense, 0.0f, invincibleTime );
        return pStats;
    }

    /** @brief 크기 0.2 콜라이더와 투사체(+X 로 초당 @p speed, 피해 @p damage)를 단 총알을 (x, y) 에 만듭니다. */
    ProjectileComponent* spawnBullet( GameObjectManager& manager, float32 x, float32 y, float32 speed, int32 damage )
    {
        GameObject* pObject = spawnColliderObject( manager, "Bullet", x, y, float2( 0.2f, 0.2f ) );
        if ( pObject == nullptr )
            return nullptr;
        ProjectileComponent* pProjectile = pObject->addComponent<ProjectileComponent>();
        if ( pProjectile == nullptr )
            return nullptr;
        pProjectile->setVelocity( float2( speed, 0.0f ) );
        pProjectile->setDamage( damage );
        return pProjectile;
    }

    /** @brief 유닛의 HP 가 @p hp 에서 바뀔 때까지(최대 @p maxFrame 번) @p deltaTime 씩 틱하고, 바뀐 프레임 번호를 돌려줍니다(안 바뀌면 -1). */
    int32 tickUntilHpChanges( GameObjectManager& manager, const UnitStatsComponent& stats, int32 hp, float32 deltaTime, int32 maxFrame )
    {
        for ( int32 frameIndex = 0; frameIndex < maxFrame; ++frameIndex )
        {
            manager.tick( deltaTime );
            if ( stats.getHp() != hp )
                return frameIndex;
        }
        return -1;
    }

    /** @brief 룸이 클리어될 때까지(최대 @p maxFrame 번) 같은 입력으로 갱신합니다. 클리어했으면 true 입니다. */
    bool updateRoomUntilCleared( ActionRoom& room, const ActionRoomFrameInput& input, int32 maxFrame )
    {
        for ( int32 frameIndex = 0; frameIndex < maxFrame && room.isCleared() == false; ++frameIndex )
        {
            (void)room.update( 0.02f, input ); // 이 시험은 룸 이벤트를 본다 — 프레임 결과는 쓰지 않는다
        }
        return room.isCleared();
    }

    /** @brief HP 바의 보이기 정책 칸(`_bShowWhenHurt` · `_bHideWhenDead`)을 넣습니다 — 세터가 없는 PROPERTY 다. */
    bool setUnitBarFlag( HealthBarComponent& bar, const utf8* pName, bool bValue )
    {
        const TypeInfo*     pTypeInfo = bar.getTypeInfo();
        const PropertyInfo* pProperty = ( pTypeInfo != nullptr ) ? pTypeInfo->findPropertyInHierarchy( hashed_string( pName ) ) : nullptr;
        if ( pProperty == nullptr )
            return false;
        pProperty->setValue<bool>( &bar, bValue );
        return true;
    }

    /** @brief 룸을 0.02 초씩 갱신하다 플레이어가 처음 피해를 받은 프레임의 피해입니다(최대 @p maxFrame 번, 없으면 0). */
    int32 updateRoomUntilPlayerIsHit( ActionRoom& room, const ActionRoomFrameInput& input, int32 maxFrame )
    {
        for ( int32 frameIndex = 0; frameIndex < maxFrame; ++frameIndex )
        {
            const int32 damage = room.update( 0.02f, input )._damageToPlayer;
            if ( damage > 0 )
                return damage;
        }
        return 0;
    }

    /** @brief 룸의 상태 바이트를 꺼냅니다. */
    vector<uint8> captureRoomBytes( const ActionRoom& room )
    {
        Archive archive;
        room.writeState( archive );
        vector<uint8> bytes;
        archive.writeData( bytes );
        return bytes;
    }
} // namespace

/**
 * @brief [ActionCombatTest] 투사체가 스탯 있는 유닛에 닿으면 피해를 한 번 주고 그 틱 끝에 사라진다
 * @details 같은 오브젝트의 콜라이더 겹침으로 맞음을 알고 `takeDamage( 피해, 쏜 쪽 )` 뒤 사라진다 — 맞음 처리가 없으면 총알은 적을 지나 수명이
 *          다할 때까지 난다.
 *          피해는 방어력을 뺀 값이다(25 − 5 = 20). 사라짐은 표시만이 아니라 그 틱의 파괴 단계에서 매니저에서 빠진다.
 */
SW_TEST_CASE( ActionCombatTest, ProjectileDamagesTheUnitItHitsOnceAndIsGoneAfterThatTick )
{
    GameObjectManager    manager;
    UnitStatsComponent*  pTarget     = spawnUnit( manager, "Target", 3.0f, 100, 5, 0.0f );
    ProjectileComponent* pProjectile = spawnBullet( manager, 0.0f, 0.0f, 6.0f, 25 );
    SW_ASSERT_TRUE( pTarget != nullptr && pProjectile != nullptr );
    SW_EXPECT_EQUAL( 25, pProjectile->getDamage() );
    const uint64 bulletID = pProjectile->getOwner()->getObjectID();

    manager.beginPlay();
    SW_ASSERT_TRUE_MSG( tickUntilHpChanges( manager, *pTarget, 100, 0.1f, 20 ) >= 0, "the bullet never hit the unit" );
    SW_EXPECT_EQUAL( 80, pTarget->getHp() );
    SW_EXPECT_TRUE( manager.findGameObjectByID( bulletID ) == nullptr );
    SW_EXPECT_EQUAL( static_cast<size_t>( 1 ), manager.getAllGameObjects().size() );

    // 더 흘려도 다시 깎이지 않는다.
    for ( int32 frameIndex = 0; frameIndex < 10; ++frameIndex )
    {
        manager.tick( 0.1f );
    }
    SW_EXPECT_EQUAL( 80, pTarget->getHp() );
    manager.endPlay();
}

/**
 * @brief [ActionCombatTest] 투사체는 쏜 쪽과 거기 붙은 것을 지나쳐 앞의 유닛을 맞힌다
 * @details 총구에서 나온 총알은 처음에 쏜 몸과 몸에 붙은 총의 콜라이더에 겹쳐 있다. 쏜 쪽을 모르면 첫 step 에 쏜 몸을 맞히고, 몸에 붙은 총(스탯
 *          없음 — 벽처럼 막는다)에 걸려 사라진다. 쏜 쪽은 핸들로 든다(`setInstigator`) — 총알이 나는 동안 쏜 쪽이 사라져도 매달린 포인터가 없다.
 */
SW_TEST_CASE( ActionCombatTest, ProjectilePassesItsInstigatorAndWhatIsAttachedToIt )
{
    GameObjectManager   manager;
    UnitStatsComponent* pShooter = spawnUnit( manager, "Shooter", 0.0f, 100, 0, 0.0f );
    UnitStatsComponent* pTarget  = spawnUnit( manager, "Target", 4.0f, 100, 0, 0.0f );
    GameObject*         pGun     = spawnColliderObject( manager, "Gun", 0.0f, 0.0f, float2( 1.0f, 0.4f ) );
    SW_ASSERT_TRUE( pShooter != nullptr && pTarget != nullptr && pGun != nullptr );
    SW_ASSERT_TRUE( pGun->attachToParent( pShooter->getOwner() ) );
    // 첫 틱 뒤에도 몸 · 총과 겹쳐 있을 만큼 느리게 쏜다(한 틱 0.2).
    ProjectileComponent* pProjectile = spawnBullet( manager, 0.0f, 0.0f, 2.0f, 10 );
    SW_ASSERT_NOT_NULL( pProjectile );
    pProjectile->setInstigator( pShooter->getOwner()->getHandle() );
    SW_EXPECT_TRUE( pProjectile->getInstigator() == pShooter->getOwner()->getHandle() );

    manager.beginPlay();
    SW_ASSERT_TRUE_MSG( tickUntilHpChanges( manager, *pTarget, 100, 0.1f, 40 ) >= 0, "the bullet never reached the target" );
    SW_EXPECT_EQUAL( 100, pShooter->getHp() );
    SW_EXPECT_EQUAL( 90, pTarget->getHp() );
    manager.endPlay();
}

/**
 * @brief [ActionCombatTest] 투사체는 레이어 행렬이 막은 유닛을 지나친다 — 맞히지도, 막히지도 않는다
 * @details 무엇에 맞고 막힐지는 레이어 행렬(`CollisionLayers`)이 정한다. 같은 편 유닛 · 장식 콜라이더를 거르는 규칙을 투사체가 따로 들지 않는다.
 */
SW_TEST_CASE( ActionCombatTest, ProjectileIgnoresUnitsOnALayerItDoesNotCollideWith )
{
    GameObjectManager manager;
    manager.getOverlapWorld2D().getPhysicsWorld().layers().setLayerCollision( 1, 2, false );
    UnitStatsComponent*  pAlly       = spawnUnit( manager, "Ally", 3.0f, 100, 0, 0.0f );
    ProjectileComponent* pProjectile = spawnBullet( manager, 0.0f, 0.0f, 6.0f, 10 );
    SW_ASSERT_TRUE( pAlly != nullptr && pProjectile != nullptr );
    pAlly->getOwner()->getComponent<BoxCollider2DComponent>()->setColliderType( 2 );
    pProjectile->getOwner()->getComponent<BoxCollider2DComponent>()->setColliderType( 1 );
    const uint64 bulletID = pProjectile->getOwner()->getObjectID();

    manager.beginPlay();
    for ( int32 frameIndex = 0; frameIndex < 10; ++frameIndex )
    {
        manager.tick( 0.1f );
    }
    SW_EXPECT_EQUAL( 100, pAlly->getHp() );
    SW_EXPECT_TRUE( manager.findGameObjectByID( bulletID ) != nullptr );
    manager.endPlay();
}

/**
 * @brief [ActionCombatTest] 한 프레임에 얇은 유닛을 통째로 건너뛰는 빠른 투사체도 맞힌다(터널링 없음)
 * @details 겹침을 step 마다 끝 자리만 보면 초당 600 으로 나는 총알은 한 프레임(1/60 초)에 10 을 가서 두께 0.1 인 유닛과 한 번도 겹치지
 *          않는다. 투사체는 시작할 때 제 콜라이더를 연속 충돌로 켜고(`setContinuous`), 물리가 지난 자리에서 지금 자리까지 쓸어 지나간 유닛과의
 *          겹침을 낸다(`PhysicsWorld::step`).
 */
SW_TEST_CASE( ActionCombatTest, FastProjectileHitsAThinUnitItCrossesInOneFrame )
{
    GameObjectManager    manager;
    UnitStatsComponent*  pTarget     = spawnUnit( manager, "ThinTarget", 5.0f, 100, 0, 0.0f );
    ProjectileComponent* pProjectile = spawnBullet( manager, 0.0f, 0.0f, 600.0f, 30 );
    SW_ASSERT_TRUE( pTarget != nullptr && pProjectile != nullptr );
    pTarget->getOwner()->getComponent<BoxCollider2DComponent>()->setOffsetScale( float2( 0.1f, 1.0f ) );
    const BoxCollider2DComponent* pBulletBox = pProjectile->getOwner()->getComponent<BoxCollider2DComponent>();
    SW_ASSERT_NOT_NULL( pBulletBox );
    SW_EXPECT_FALSE( pBulletBox->isContinuous() );

    manager.beginPlay();
    SW_EXPECT_TRUE( pBulletBox->isContinuous() );
    manager.tick( 1.0f / 60.0f );
    SW_EXPECT_EQUAL( 70, pTarget->getHp() );
    SW_EXPECT_EQUAL( static_cast<size_t>( 1 ), manager.getAllGameObjects().size() );
    manager.endPlay();
}

/**
 * @brief [ActionCombatTest] 피해는 "game" 채널에 `DamageAppliedEvent` 를 낸다 — 쏜 쪽 · 맞은 쪽 · 들어간 피해 · 남은 HP · 죽음
 * @details HP 바 · 피해 숫자가 받는 이벤트다. 피해가 HP 에 닿는 자리(`UnitStatsComponent`) 하나에서 내므로, 틱 안의 게임 코드가
 *          부르든(틱 직후로 미뤄진다) 투사체로 맞든 같은 모양으로 온다. 미룬 피해도
 *          쏜 쪽을 들고 간다. 죽인 피해는 남았던 HP 보다 크게 실린다(넘친 피해). 버스 스레드에서 적용되므로 구독자는 그 틱 안에 받는다.
 */
SW_TEST_CASE( ActionCombatTest, DamagePublishesAnEventWithInstigatorTargetAmountAndRemainingHp )
{
    EventDispatcher                    dispatcher;
    const ScopedEventDispatcherService scopedDispatcher{ dispatcher };
    vector<DamageAppliedEvent>         listReceived;
    dispatcher.subscribe<DamageAppliedEvent>( gameEventChannel(), SW_DELEGATE_LAMBDA( Delegate<void( const DamageAppliedEvent& )>, [&listReceived]( const DamageAppliedEvent& event )
    { listReceived.push_back( event ); } ) );

    GameObjectManager   manager;
    GameObject*         pShooter = manager.createGameObject( hashed_string( "Shooter" ) );
    UnitStatsComponent* pTarget  = spawnUnit( manager, "Target", 3.0f, 30, 5, 0.0f );
    GameObject*         pStriker = manager.createGameObject( hashed_string( "Striker" ) );
    SW_ASSERT_TRUE( pShooter != nullptr && pTarget != nullptr && pStriker != nullptr );
    MockStrikeInTickComponent* pStrike = pStriker->addComponent<MockStrikeInTickComponent>();
    SW_ASSERT_NOT_NULL( pStrike );
    pStrike->_pTarget    = pTarget;
    pStrike->_instigator = pShooter->getHandle();
    pStrike->_damage     = 25;

    // 1) 틱 안의 피해 — 틱 직후로 미뤄지고, 이벤트는 그 틱이 끝나기 전에 온다.
    manager.beginPlay();
    manager.tick( 0.1f );
    SW_EXPECT_EQUAL( 30, pStrike->_hpSeenInTick );
    SW_EXPECT_EQUAL( 10, pTarget->getHp() );
    SW_ASSERT_EQUAL( static_cast<size_t>( 1 ), listReceived.size() );
    SW_EXPECT_TRUE( listReceived[0]._instigator == pShooter->getHandle() );
    SW_EXPECT_TRUE( listReceived[0]._target == pTarget->getOwner()->getHandle() );
    SW_EXPECT_EQUAL( 20, listReceived[0]._amount );
    SW_EXPECT_EQUAL( 10, listReceived[0]._remainingHp );
    SW_EXPECT_TRUE( listReceived[0]._bKilled == SW_FALSE );

    // 2) 투사체로 죽인다 — 같은 모양, 쏜 쪽은 투사체의 instigator.
    ProjectileComponent* pProjectile = spawnBullet( manager, 0.0f, 0.0f, 6.0f, 25 );
    SW_ASSERT_NOT_NULL( pProjectile );
    pProjectile->setInstigator( pShooter->getHandle() );
    SW_ASSERT_TRUE( tickUntilHpChanges( manager, *pTarget, 10, 0.1f, 20 ) >= 0 );
    SW_ASSERT_EQUAL( static_cast<size_t>( 2 ), listReceived.size() );
    SW_EXPECT_TRUE( listReceived[1]._instigator == pShooter->getHandle() );
    SW_EXPECT_TRUE( listReceived[1]._target == pTarget->getOwner()->getHandle() );
    SW_EXPECT_EQUAL( 20, listReceived[1]._amount );
    SW_EXPECT_EQUAL( 0, listReceived[1]._remainingHp );
    SW_EXPECT_TRUE( listReceived[1]._bKilled == SW_TRUE );
    manager.endPlay();
}

/**
 * @brief [ActionCombatTest] 같은 프레임에 닿은 투사체 둘은 둘 다 사라지지만 피해는 무적 시간 때문에 한 번만 깎인다
 * @details 두 총알의 맞음은 같은 물리 step 의 겹침 이벤트로 차례로 온다. 첫 피해가 건 무적(0.5 초)이 둘째를 막는다 — 투사체는 HP 를 직접
 *          깎지 않고 무적 · 방어력을 지나는 한 길(`takeDamage`)로만 간다.
 */
SW_TEST_CASE( ActionCombatTest, TwoProjectilesInOneFrameRespectTheInvincibilityWindow )
{
    GameObjectManager    manager;
    UnitStatsComponent*  pTarget = spawnUnit( manager, "Target", 3.0f, 100, 0, 0.5f );
    ProjectileComponent* pUpper  = spawnBullet( manager, 0.0f, 0.3f, 6.0f, 10 );
    ProjectileComponent* pLower  = spawnBullet( manager, 0.0f, -0.3f, 6.0f, 10 );
    SW_ASSERT_TRUE( pTarget != nullptr && pUpper != nullptr && pLower != nullptr );

    manager.beginPlay();
    SW_ASSERT_TRUE( tickUntilHpChanges( manager, *pTarget, 100, 0.1f, 20 ) >= 0 );
    SW_EXPECT_EQUAL( 90, pTarget->getHp() );
    SW_EXPECT_EQUAL( static_cast<size_t>( 1 ), manager.getAllGameObjects().size() );
    manager.endPlay();
}

/**
 * @brief [ActionCombatTest] 스탯 없는 콜라이더(벽)는 투사체를 멈추고, 요격탄이 아닌 투사체는 서로를 멈추지 않는다
 * @details 무엇에 막힐지는 레이어가 정한다 — 부딪히게 둔 벽은 피해 없이 총알을 지운다(언리얼 투사체가 막는 것에 닿으면 멈추는 것과 같다).
 *          투사체끼리는 요격탄(`setInterceptor`)이 아니면 지나친다: 한 자리에서 나란히 나는 총알(산탄)은 처음부터 서로 겹쳐 있어, 서로를 벽으로
 *          보면 쏘자마자 모두 사라진다.
 */
SW_TEST_CASE( ActionCombatTest, WallsStopProjectilesButProjectilesPassEachOther )
{
    GameObjectManager    manager;
    GameObject*          pWall   = spawnColliderObject( manager, "Wall", 3.0f, 2.0f, float2( 0.5f, 1.0f ) );
    UnitStatsComponent*  pTarget = spawnUnit( manager, "Target", 3.0f, 100, 0, 0.0f );
    ProjectileComponent* pFirst  = spawnBullet( manager, 0.0f, 0.0f, 6.0f, 10 );
    ProjectileComponent* pSecond = spawnBullet( manager, 0.0f, 0.0f, 6.0f, 10 );
    ProjectileComponent* pWalled = spawnBullet( manager, 0.0f, 2.0f, 6.0f, 10 );
    SW_ASSERT_TRUE( pWall != nullptr && pTarget != nullptr && pFirst != nullptr && pSecond != nullptr && pWalled != nullptr );

    manager.beginPlay();
    for ( int32 frameIndex = 0; frameIndex < 10; ++frameIndex )
    {
        manager.tick( 0.1f );
    }
    // 나란히 난 둘은 서로를 지나쳐 유닛에 닿았다(무적 0 이라 둘 다 깎는다). 벽 줄의 총알은 벽에서 멈췄다 — 남은 것은 벽과 유닛뿐이다.
    SW_EXPECT_EQUAL( 80, pTarget->getHp() );
    SW_EXPECT_EQUAL( static_cast<size_t>( 2 ), manager.getAllGameObjects().size() );
    manager.endPlay();
}

/**
 * @brief [ActionCombatTest] 관통 수만큼 유닛을 꿰뚫고 날다가, 다 쓰면 다음 유닛에서 사라진다
 */
SW_TEST_CASE( ActionCombatTest, PiercingProjectilePassesThroughAsManyUnitsAsItsPierceCount )
{
    GameObjectManager    manager;
    UnitStatsComponent*  pFirst      = spawnUnit( manager, "First", 2.0f, 100, 0, 0.0f );
    UnitStatsComponent*  pSecond     = spawnUnit( manager, "Second", 4.0f, 100, 0, 0.0f );
    UnitStatsComponent*  pThird      = spawnUnit( manager, "Third", 6.0f, 100, 0, 0.0f );
    ProjectileComponent* pProjectile = spawnBullet( manager, 0.0f, 0.0f, 6.0f, 10 );
    SW_ASSERT_TRUE( pFirst != nullptr && pSecond != nullptr && pThird != nullptr && pProjectile != nullptr );
    pProjectile->setPierceCount( 1 );
    SW_EXPECT_EQUAL( 1, pProjectile->getPierceCount() );

    manager.beginPlay();
    for ( int32 frameIndex = 0; frameIndex < 20; ++frameIndex )
    {
        manager.tick( 0.1f );
    }
    SW_EXPECT_EQUAL( 90, pFirst->getHp() );
    SW_EXPECT_EQUAL( 90, pSecond->getHp() );
    SW_EXPECT_EQUAL( 100, pThird->getHp() );
    SW_EXPECT_EQUAL( static_cast<size_t>( 3 ), manager.getAllGameObjects().size() );
    manager.endPlay();
}

/**
 * @brief [ActionCombatTest] 공격 판정은 휘두르는 동안 닿은 유닛마다 한 번 피해를 준다 — 이미 서 있던 유닛도, 들어온 유닛도, 공격자 자신은 빼고
 * @details 같은 오브젝트의 콜라이더가 판정이다. 휘두르기 전부터 판정 안에 있던 유닛은 시작할 때, 휘두르는 동안 들어온 유닛은 들어올 때 맞고,
 *          한 번 휘두를 때 유닛마다 한 번이다(나갔다 다시 들어와도). 공격자 계층(판정이 붙은 몸)은 맞지 않고, 피해 이벤트의 instigator 는 그 몸이다.
 *          판정이 꺼진 뒤 들어온 유닛은 맞지 않는다.
 */
SW_TEST_CASE( ActionCombatTest, AttackHitsEachUnitInItsHitboxOncePerSwing )
{
    EventDispatcher                    dispatcher;
    const ScopedEventDispatcherService scopedDispatcher{ dispatcher };
    vector<DamageAppliedEvent>         listReceived;
    dispatcher.subscribe<DamageAppliedEvent>( gameEventChannel(), SW_DELEGATE_LAMBDA( Delegate<void( const DamageAppliedEvent& )>, [&listReceived]( const DamageAppliedEvent& event )
    { listReceived.push_back( event ); } ) );

    GameObjectManager   manager;
    UnitStatsComponent* pAttacker = spawnUnit( manager, "Attacker", 0.0f, 100, 0, 0.0f );
    GameObject*         pHitbox   = spawnColliderObject( manager, "Hitbox", 1.0f, 0.0f, float2( 2.0f, 1.0f ) ); // x 0..2 — 몸(-0.5..0.5)과도 겹친다
    UnitStatsComponent* pStanding = spawnUnit( manager, "Standing", 1.5f, 100, 0, 0.0f );
    UnitStatsComponent* pWalker   = spawnUnit( manager, "Walker", 6.0f, 100, 0, 0.0f );
    SW_ASSERT_TRUE( pAttacker != nullptr && pHitbox != nullptr && pStanding != nullptr && pWalker != nullptr );
    SW_ASSERT_TRUE( pHitbox->attachToParent( pAttacker->getOwner(), AttachRule::KeepWorld ) );
    MeleeHitboxComponent* pAttack = pHitbox->addComponent<MeleeHitboxComponent>();
    SW_ASSERT_NOT_NULL( pAttack );
    SceneComponent* pWalkerBody = pWalker->getOwner()->getPrimarySceneComponent();
    SW_ASSERT_NOT_NULL( pWalkerBody );

    manager.beginPlay();
    manager.tick( 0.1f ); // 판정이 몸 · 서 있는 유닛과의 겹침을 받는다 — 아직 휘두르지 않았다
    SW_EXPECT_EQUAL( 100, pStanding->getHp() );

    // 휘두르기 전부터 서 있던 유닛은 시작할 때 맞는다. 몸은 맞지 않는다.
    pAttack->beginAttack( 10, 1.0f );
    SW_EXPECT_EQUAL( 90, pStanding->getHp() );
    SW_EXPECT_EQUAL( 100, pAttacker->getHp() );
    SW_ASSERT_EQUAL( static_cast<size_t>( 1 ), listReceived.size() );
    SW_EXPECT_TRUE( listReceived[0]._instigator == pAttacker->getOwner()->getHandle() );

    // 휘두르는 동안 들어온 유닛은 들어올 때 맞고, 나갔다 다시 들어와도 다시 맞지 않는다.
    pWalkerBody->setLocalPosition( float3( 1.2f, 0.0f, 0.0f ) );
    manager.tick( 0.1f );
    SW_EXPECT_EQUAL( 90, pWalker->getHp() );
    pWalkerBody->setLocalPosition( float3( 6.0f, 0.0f, 0.0f ) );
    manager.tick( 0.1f );
    pWalkerBody->setLocalPosition( float3( 1.2f, 0.0f, 0.0f ) );
    manager.tick( 0.1f );
    SW_EXPECT_EQUAL( 90, pWalker->getHp() );
    SW_EXPECT_EQUAL( 90, pStanding->getHp() );

    // 판정이 꺼진 뒤 들어온 유닛은 맞지 않는다.
    for ( int32 frameIndex = 0; frameIndex < 20 && pAttack->isAttackActive(); ++frameIndex )
    {
        manager.tick( 0.1f );
    }
    SW_ASSERT_FALSE( pAttack->isAttackActive() );
    pWalkerBody->setLocalPosition( float3( 6.0f, 0.0f, 0.0f ) );
    manager.tick( 0.1f );
    pWalkerBody->setLocalPosition( float3( 1.2f, 0.0f, 0.0f ) );
    manager.tick( 0.1f );
    SW_EXPECT_EQUAL( 90, pWalker->getHp() );
    SW_EXPECT_EQUAL( 100, pAttacker->getHp() );
    manager.endPlay();
}

/**
 * @brief [ActionCombatTest] 프레임 사이에 투사체 길을 가로질러 건너간 유닛도 맞는다(상대 운동)
 * @details 연속 충돌이 상대를 이번 step 의 자리에 세워 두고 재면 총알이 한 프레임에 x 로 10 을 가는 동안 y 로 길을 건너간 유닛은 끝 자리가 길
 *          밖이라 맞지 않는다. 그래서 물리가 두 바디의 이동 차이로 쓴다(Box2D 총알 TOI · 유니티 Continuous Dynamic).
 */
SW_TEST_CASE( ActionCombatTest, ProjectileHitsAUnitThatCrossesItsPathBetweenFrames )
{
    GameObjectManager    manager;
    UnitStatsComponent*  pCrosser    = spawnUnit( manager, "Crosser", 5.0f, 100, 0, 0.0f );
    ProjectileComponent* pProjectile = spawnBullet( manager, 0.0f, 0.0f, 600.0f, 10 );
    SW_ASSERT_TRUE( pCrosser != nullptr && pProjectile != nullptr );
    SceneComponent* pCrosserBody = pCrosser->getOwner()->getPrimarySceneComponent();
    SW_ASSERT_NOT_NULL( pCrosserBody );
    pCrosserBody->setLocalPosition( float3( 5.0f, -3.0f, 0.0f ) );

    manager.beginPlay();
    manager.tick( 0.0f ); // 둘 다 제자리에서 첫 step — 출발점을 잡는다
    // 한 프레임에 총알은 x 0 → 10, 유닛은 y -3 → 3 — 둘은 x 5 · y 0 에서 같은 때 만난다. 끝 자리는 서로 멀다.
    pCrosserBody->setLocalPosition( float3( 5.0f, 3.0f, 0.0f ) );
    manager.tick( 1.0f / 60.0f );
    SW_EXPECT_EQUAL( 90, pCrosser->getHp() );
    manager.endPlay();
}

/**
 * @brief [ActionCombatTest] 투사체는 트리거를 지나쳐 뒤의 몸을 맞힌다 — 트리거만 가진 유닛은 맞지 않는다
 * @details 겹침 훅이 상대 오브젝트만 넘기면 투사체가 감지 범위 · 구역 볼륨(트리거)에도 벽처럼 막힌다. `OverlapInfo::_bOtherTrigger` 로 가른다
 *          (유니티 `isTrigger` · 언리얼 Overlap 반응) — 트리거는 막지도 맞지도 않는다.
 */
SW_TEST_CASE( ActionCombatTest, ProjectilePassesTriggersAndHitsTheBodyBehind )
{
    GameObjectManager    manager;
    GameObject*          pZone       = spawnColliderObject( manager, "Zone", 2.0f, 0.0f, float2( 1.0f, 4.0f ) );
    UnitStatsComponent*  pSensorOnly = spawnUnit( manager, "SensorOnly", 3.5f, 100, 0, 0.0f );
    UnitStatsComponent*  pTarget     = spawnUnit( manager, "Target", 5.0f, 100, 0, 0.0f );
    ProjectileComponent* pProjectile = spawnBullet( manager, 0.0f, 0.0f, 6.0f, 10 );
    SW_ASSERT_TRUE( pZone != nullptr && pSensorOnly != nullptr && pTarget != nullptr && pProjectile != nullptr );
    pZone->getComponent<BoxCollider2DComponent>()->setTrigger( true );
    pSensorOnly->getOwner()->getComponent<BoxCollider2DComponent>()->setTrigger( true );

    manager.beginPlay();
    SW_ASSERT_TRUE_MSG( tickUntilHpChanges( manager, *pTarget, 100, 0.1f, 20 ) >= 0, "the bullet never reached the body behind the triggers" );
    SW_EXPECT_EQUAL( 90, pTarget->getHp() );
    SW_EXPECT_EQUAL( 100, pSensorOnly->getHp() );
    manager.endPlay();
}

/**
 * @brief [ActionCombatTest] 요격탄은 다른 쪽 투사체를 맞혀 함께 사라진다 — 같은 쪽 산탄과 요격탄이 아닌 투사체는 서로 지나친다
 * @details "투사체끼리는 지나친다" 를 고정 규칙으로 두면 요격탄을 만들 수 없다. 그래서 데이터다(`setInterceptor` — 언리얼의 채널별 충돌 반응). 같은
 *          쪽이 쏜 것끼리는 요격탄이어도 지나친다(한 자리에서 퍼지는 산탄).
 */
SW_TEST_CASE( ActionCombatTest, InterceptorDestroysAnEnemyProjectileButPelletsPassEachOther )
{
    GameObjectManager manager;
    GameObject*       pPlayer = manager.createGameObject( hashed_string( "Player" ) );
    GameObject*       pEnemy  = manager.createGameObject( hashed_string( "Enemy" ) );
    SW_ASSERT_TRUE( pPlayer != nullptr && pEnemy != nullptr );

    // 줄 0: 적 탄이 지키는 유닛 쪽으로 오고, 요격탄이 마주 나간다.
    UnitStatsComponent*  pGuard       = spawnUnit( manager, "Guard", -3.0f, 100, 0, 0.0f );
    ProjectileComponent* pEnemyShot   = spawnBullet( manager, 6.0f, 0.0f, -6.0f, 10 );
    ProjectileComponent* pInterceptor = spawnBullet( manager, 0.0f, 0.0f, 6.0f, 10 );
    // 줄 5: 같은 쪽이 한 자리에서 쏜 요격탄 둘(산탄).
    ProjectileComponent* pPelletA = spawnBullet( manager, 0.0f, 5.0f, 6.0f, 10 );
    ProjectileComponent* pPelletB = spawnBullet( manager, 0.0f, 5.0f, 6.0f, 10 );
    // 줄 10: 요격탄이 아닌 총알과 적 탄이 마주친다.
    ProjectileComponent* pPlainShot = spawnBullet( manager, 0.0f, 10.0f, 6.0f, 10 );
    ProjectileComponent* pEnemyPass = spawnBullet( manager, 6.0f, 10.0f, -6.0f, 10 );
    SW_ASSERT_TRUE( pGuard != nullptr && pEnemyShot != nullptr && pInterceptor != nullptr && pPelletA != nullptr && pPelletB != nullptr &&
                    pPlainShot != nullptr && pEnemyPass != nullptr );
    pEnemyShot->setInstigator( pEnemy->getHandle() );
    pEnemyPass->setInstigator( pEnemy->getHandle() );
    for ( ProjectileComponent* pOwn : { pInterceptor, pPelletA, pPelletB, pPlainShot } )
    {
        pOwn->setInstigator( pPlayer->getHandle() );
    }
    for ( ProjectileComponent* pOwn : { pInterceptor, pPelletA, pPelletB } )
    {
        pOwn->setInterceptor( true );
    }
    SW_EXPECT_TRUE( pInterceptor->isInterceptor() );
    SW_EXPECT_FALSE( pPlainShot->isInterceptor() );
    const uint64 enemyShotID   = pEnemyShot->getOwner()->getObjectID();
    const uint64 interceptorID = pInterceptor->getOwner()->getObjectID();
    const uint64 pelletAID     = pPelletA->getOwner()->getObjectID();
    const uint64 pelletBID     = pPelletB->getOwner()->getObjectID();
    const uint64 plainShotID   = pPlainShot->getOwner()->getObjectID();
    const uint64 enemyPassID   = pEnemyPass->getOwner()->getObjectID();

    manager.beginPlay();
    for ( int32 frameIndex = 0; frameIndex < 15; ++frameIndex )
    {
        manager.tick( 0.1f );
    }
    SW_EXPECT_TRUE( manager.findGameObjectByID( enemyShotID ) == nullptr );
    SW_EXPECT_TRUE( manager.findGameObjectByID( interceptorID ) == nullptr );
    SW_EXPECT_EQUAL( 100, pGuard->getHp() );
    SW_EXPECT_TRUE( manager.findGameObjectByID( pelletAID ) != nullptr );
    SW_EXPECT_TRUE( manager.findGameObjectByID( pelletBID ) != nullptr );
    SW_EXPECT_TRUE( manager.findGameObjectByID( plainShotID ) != nullptr );
    SW_EXPECT_TRUE( manager.findGameObjectByID( enemyPassID ) != nullptr );
    manager.endPlay();
}

/**
 * @brief [ActionCombatTest] 피해는 그 프레임에 구독자에게 닿는다 — 컴포넌트 델리게이트는 그 자리에서, 채널은 버스 스레드면 그 자리에서 · 아니면 큐로
 * @details 피해 이벤트가 늘 큐(`push`)로 실리면 HP 바 · 피해 숫자가 한 프레임 늦게 받는다. 언리얼 `OnTakeAnyDamage` 는 `ApplyDamage` 안에서 바로
 *          불린다. `UnitStatsComponent::registerDamageApplied` 가 HP 가 깎인 자리에서 불리고, "game" 채널은 버스 스레드(큐를 비우는 스레드)에서
 *          적용됐으면 바로 · 다른 스레드에서 적용됐으면 다음 `processEvents` 에 받는다(`GameEventUtil::send`). 뗀 델리게이트는 다시 불리지 않는다.
 */
SW_TEST_CASE( ActionCombatTest, DamageReachesSubscribersInTheSameFrame )
{
    EventDispatcher                    dispatcher;
    const ScopedEventDispatcherService scopedDispatcher{ dispatcher };
    dispatcher.processEvents(); // 이 스레드가 버스 스레드가 된다(엔진에서는 `EngineLoop` 의 게임 스레드)
    vector<int32> listChannelHp;
    dispatcher.subscribe<DamageAppliedEvent>( gameEventChannel(), SW_DELEGATE_LAMBDA( Delegate<void( const DamageAppliedEvent& )>, [&listChannelHp]( const DamageAppliedEvent& event )
    { listChannelHp.push_back( event._remainingHp ); } ) );

    GameObjectManager    manager;
    UnitStatsComponent*  pTarget     = spawnUnit( manager, "Target", 3.0f, 100, 0, 0.0f );
    ProjectileComponent* pProjectile = spawnBullet( manager, 0.0f, 0.0f, 6.0f, 10 );
    SW_ASSERT_TRUE( pTarget != nullptr && pProjectile != nullptr );
    vector<int32>        listComponentHp;
    const DelegateHandle subscription = pTarget->registerDamageApplied( SW_DELEGATE_LAMBDA( UnitStatsComponent::DamageAppliedDelegate, [&listComponentHp]( const DamageAppliedEvent& event )
    { listComponentHp.push_back( event._remainingHp ); } ) );

    // 겹침 전달에서 맞는다 — 그 틱이 끝나기 전에 둘 다 받았다.
    manager.beginPlay();
    SW_ASSERT_TRUE( tickUntilHpChanges( manager, *pTarget, 100, 0.1f, 20 ) >= 0 );
    SW_ASSERT_EQUAL( static_cast<size_t>( 1 ), listComponentHp.size() );
    SW_ASSERT_EQUAL( static_cast<size_t>( 1 ), listChannelHp.size() );
    SW_EXPECT_EQUAL( 90, listComponentHp[0] );
    SW_EXPECT_EQUAL( 90, listChannelHp[0] );

    // 다른 스레드에서 적용된 피해: 델리게이트는 그 자리에서, 채널은 큐에 실렸다가 다음 processEvents 에 온다.
    std::thread worker( [pTarget]()
    { pTarget->takeDamage( 5 ); } );
    worker.join();
    SW_EXPECT_EQUAL( static_cast<size_t>( 2 ), listComponentHp.size() );
    SW_EXPECT_EQUAL( static_cast<size_t>( 1 ), listChannelHp.size() );
    dispatcher.processEvents();
    SW_ASSERT_EQUAL( static_cast<size_t>( 2 ), listChannelHp.size() );
    SW_EXPECT_EQUAL( 85, listChannelHp[1] );

    // 뗀 델리게이트는 다시 불리지 않는다.
    pTarget->unregisterDamageApplied( subscription );
    pTarget->takeDamage( 5 );
    SW_EXPECT_EQUAL( static_cast<size_t>( 2 ), listComponentHp.size() );
    SW_EXPECT_EQUAL( static_cast<size_t>( 3 ), listChannelHp.size() );
    manager.endPlay();
}

/**
 * @brief [ActionCombatTest] 콜라이더 없는 투사체는 시작할 때 경고한다 — 날기만 하고 아무것도 맞힐 수 없다
 * @details 맞음은 같은 오브젝트의 콜라이더 겹침으로만 온다. 콜라이더를 빠뜨린 프리팹의 총알은 조용히 모든 것을 통과해 "총알이 안 맞는다" 가 원인
 *          없이 보인다. 콜라이더가 있으면 경고하지 않는다.
 */
SW_TEST_CASE( ActionCombatTest, ProjectileWithoutAColliderWarnsAtBeginPlay )
{
    GameObjectManager manager;
    GameObject*       pBare = manager.createGameObject( hashed_string( "BareShot" ) );
    SW_ASSERT_NOT_NULL( pBare );
    SW_ASSERT_NOT_NULL( pBare->addComponent<SceneComponent>() );
    SW_ASSERT_NOT_NULL( pBare->addComponent<ProjectileComponent>() );
    SW_ASSERT_NOT_NULL( spawnBullet( manager, 0.0f, 0.0f, 1.0f, 1 ) );

    test::ScopedLogCollector logs;
    {
        test::ScopedDefensiveTestLog expected( "a projectile without a collider" );
        manager.beginPlay();
    }
    SW_EXPECT_TRUE_MSG( logs.countContaining( "can never hit anything" ) == 1u, logs.joined().c_str() );
    SW_EXPECT_EQUAL( 1u, logs.countContaining( "BareShot" ) );
    manager.endPlay();
}

/**
 * @brief [ActionCombatTest] 날던 중에 상태를 다시 읽은 투사체는 흐른 수명을 이어 간다
 * @details 플레이 중 되돌리기 · 핫 리로드는 컴포넌트를 다시 만들고 `onBeginPlay` 를 다시 부른다. 거기서 흐른 수명을 0 으로 돌리면 되돌릴 때마다
 *          총알이 수명을 처음부터 다시 산다(이펙트 페이드 · 데미지 숫자와 같은 규칙).
 */
SW_TEST_CASE( ActionCombatTest, ProjectileKeepsItsLifeAfterTheStateIsReadAgain )
{
    GameObjectManager    manager;
    ProjectileComponent* pProjectile = spawnBullet( manager, 0.0f, 0.0f, 1.0f, 5 );
    SW_ASSERT_NOT_NULL( pProjectile );
    pProjectile->setLifeTime( 1.0f );
    GameObject* pBullet = pProjectile->getOwner();
    manager.beginPlay();
    pProjectile->onTick( 0.6f );
    SW_EXPECT_FALSE( pBullet->isPendingDestroy() );

    SW_ASSERT_TRUE( StateReloadTestUtil::reloadInPlace( pBullet ) );
    pProjectile = pBullet->getComponent<ProjectileComponent>();
    SW_ASSERT_NOT_NULL( pProjectile );
    SW_ASSERT_TRUE( pProjectile->hasBegunPlay() );

    // 남은 0.4 초가 지나면 지운다 — 수명을 0 에서 다시 셌다면 아직 남아 있다.
    pProjectile->onTick( 0.5f );
    SW_EXPECT_TRUE( pBullet->isPendingDestroy() );
    manager.endPlay();
}

/**
 * @brief [ActionCombatTest] 씬을 플레이 중에 저장했다 다시 열어도 핸들 PROPERTY 는 같은 오브젝트를 가리킨다 — 파일에 없는 것은 없음이 된다
 * @details 핸들 PROPERTY(`ProjectileComponent::_instigator` · `MeleeHitboxComponent::_listHitTarget`)에 런타임 id 를 그대로 파일에 넣으면, 다시 연
 *          씬의 오브젝트는 새 id 를 받으므로 그 값은 아무것도 아니거나 우연히 같은 값을 받은 **다른 오브젝트**다. 그래서 부모 부착과 같은 규칙이다 —
 *          쓸 때 그 오브젝트의 파일 id 로(파일에 없으면 0), 읽을 때 묶음이 이 실행의 오브젝트로 옮긴다(`ObjectStateBatch`). 언리얼의 Instigator 처럼
 *          파일 밖을 가리키는 런타임 참조는 남지 않는다.
 */
SW_TEST_CASE( ActionCombatTest, ObjectReferencesSurviveASceneRoundTripAsTheSameObjects )
{
    SceneManager sceneManager;
    Scene*       pScene = sceneManager.createScene( "ReferenceWorld" );
    SW_ASSERT_NOT_NULL( pScene );
    GameObjectManager* pObjects = pScene->getObjectManager();
    SW_ASSERT_NOT_NULL( pObjects );

    UnitStatsComponent*  pShooter = spawnUnit( *pObjects, "Shooter", -20.0f, 100, 0, 0.0f );
    UnitStatsComponent*  pVictim  = spawnUnit( *pObjects, "Victim", 1.5f, 100, 0, 0.0f );
    GameObject*          pHitbox  = spawnColliderObject( *pObjects, "Hitbox", 1.0f, 0.0f, float2( 2.0f, 1.0f ) );
    ProjectileComponent* pLive    = spawnBullet( *pObjects, 10.0f, 10.0f, 0.0f, 10 );
    ProjectileComponent* pOrphan  = spawnBullet( *pObjects, 20.0f, 20.0f, 0.0f, 10 );
    GameObject*          pGone    = pObjects->createGameObject( hashed_string( "Gone" ) );
    SW_ASSERT_TRUE( pShooter != nullptr && pVictim != nullptr && pHitbox != nullptr && pLive != nullptr && pOrphan != nullptr && pGone != nullptr );
    MeleeHitboxComponent* pAttack = pHitbox->addComponent<MeleeHitboxComponent>();
    SW_ASSERT_NOT_NULL( pAttack );
    pLive->getOwner()->setName( hashed_string( "LiveShot" ) );
    pOrphan->getOwner()->setName( hashed_string( "OrphanShot" ) );
    pLive->setInstigator( pShooter->getOwner()->getHandle() );
    pOrphan->setInstigator( pGone->getHandle() );
    pObjects->destroyObject( pGone ); // 쏜 쪽이 사라졌다 — 파일에 없다

    // 플레이 중 한 번 휘둘러 판정 안의 유닛을 맞힌다 — 맞은 목록(`_listHitTarget`)에 그 유닛이 든 채로 저장한다.
    pObjects->beginPlay();
    pObjects->tick( 0.1f );
    pAttack->beginAttack( 10, 5.0f );
    SW_ASSERT_EQUAL( 90, pVictim->getHp() );
    const GameObjectHandle oldShooter = pShooter->getOwner()->getHandle();

    SceneDocument doc;
    SW_ASSERT_TRUE( pScene->serializeToDocument( doc ) );
    Scene* pReloaded = sceneManager.createScene( "ReferenceWorldReloaded" );
    SW_ASSERT_NOT_NULL( pReloaded );
    SW_ASSERT_TRUE( pReloaded->instantiate( doc ) );
    GameObjectManager* pReloadedObjects = pReloaded->getObjectManager();

    GameObject* pNewShooter = pReloadedObjects->findGameObjectByName( hashed_string( "Shooter" ) );
    GameObject* pNewLive    = pReloadedObjects->findGameObjectByName( hashed_string( "LiveShot" ) );
    GameObject* pNewOrphan  = pReloadedObjects->findGameObjectByName( hashed_string( "OrphanShot" ) );
    GameObject* pNewVictim  = pReloadedObjects->findGameObjectByName( hashed_string( "Victim" ) );
    GameObject* pNewHitbox  = pReloadedObjects->findGameObjectByName( hashed_string( "Hitbox" ) );
    SW_ASSERT_TRUE( pNewShooter != nullptr && pNewLive != nullptr && pNewOrphan != nullptr && pNewVictim != nullptr && pNewHitbox != nullptr );
    SW_EXPECT_TRUE( pNewShooter->getHandle() != oldShooter );
    SW_EXPECT_TRUE( pNewLive->getComponent<ProjectileComponent>()->getInstigator() == pNewShooter->getHandle() );
    SW_EXPECT_FALSE( pNewOrphan->getComponent<ProjectileComponent>()->getInstigator().isValid() );

    // 휘두르던 중에 저장했다 — 다시 연 판정은 이미 맞힌 유닛을 같은 휘두르기에 다시 맞히지 않는다(맞은 목록이 새 유닛을 가리킨다).
    UnitStatsComponent* pNewVictimStats = pNewVictim->getComponent<UnitStatsComponent>();
    SW_ASSERT_NOT_NULL( pNewVictimStats );
    SW_ASSERT_EQUAL( 90, pNewVictimStats->getHp() );
    SW_ASSERT_TRUE( pNewHitbox->getComponent<MeleeHitboxComponent>()->isAttackActive() );
    pReloadedObjects->beginPlay();
    pReloadedObjects->tick( 0.1f );
    SW_EXPECT_EQUAL( 90, pNewVictimStats->getHp() );
    sceneManager.shutdown();
}

/**
 * @brief [ActionCombatTest] 같은 실행에서 상태를 제자리에 다시 읽어도(되돌리기 · 핫 리로드) 핸들은 묶음 밖의 같은 오브젝트를 가리킨다
 * @details 같은 실행의 상태는 런타임 id 로 적는다 — 쏜 쪽이 그 묶음에 없어도(총알 하나만 다시 읽었다) 런타임 id 는 다시 쓰이지 않으므로 그대로 둔다.
 *          파일 상태(`ObjectIDSpace::Saved`)만 묶음에 없는 것을 없음으로 만든다.
 */
SW_TEST_CASE( ActionCombatTest, ObjectReferencesKeepTheirTargetWhenReloadedInPlace )
{
    GameObjectManager    manager;
    UnitStatsComponent*  pShooter = spawnUnit( manager, "Shooter", -20.0f, 100, 0, 0.0f );
    ProjectileComponent* pShot    = spawnBullet( manager, 0.0f, 0.0f, 0.0f, 10 );
    SW_ASSERT_TRUE( pShooter != nullptr && pShot != nullptr );
    pShot->setInstigator( pShooter->getOwner()->getHandle() );
    GameObject* pShotObject = pShot->getOwner();

    vector<uint8> bytes;
    SW_ASSERT_TRUE( ObjectStateSerializer::saveToBinaryBuffer( pShotObject, bytes ) );
    const ObjectIdentity identity = ObjectStateSerializer::captureIdentity( pShotObject );
    ObjectLoadContext    context{};
    context._pIdentity = &identity;
    SW_ASSERT_TRUE( ObjectStateSerializer::loadFromBinaryBuffer( pShotObject, bytes.data(), bytes.size(), context ) > 0 );

    const ProjectileComponent* pReloaded = pShotObject->getComponent<ProjectileComponent>();
    SW_ASSERT_NOT_NULL( pReloaded );
    SW_EXPECT_TRUE( pReloaded->getInstigator() == pShooter->getOwner()->getHandle() );
}

/**
 * @brief [ActionCombatTest] 바이너리 파일 상태(쿠킹한 씬)도 핸들을 파일 id 로 싣고, 읽는 묶음이 그 id 의 새 오브젝트로 잇는다
 * @details 핸들은 내장 타입이라 기본 문맥에 바이트 처리기가 있다. 글 처리기만 파일 id 로 바꾸면 XML 은 맞고 바이너리는 런타임 id 를 싣는다 —
 *          쿠킹한 씬에서 핸들 PROPERTY 가 비고, 쿠커는 왕복 검증에서 그 엔티티를 XML 로 남긴다.
 */
SW_TEST_CASE( ActionCombatTest, ObjectReferencesSurviveBinaryFileState )
{
    GameObjectManager    manager;
    UnitStatsComponent*  pShooter = spawnUnit( manager, "Shooter", -20.0f, 100, 0, 0.0f );
    ProjectileComponent* pShot    = spawnBullet( manager, 0.0f, 0.0f, 0.0f, 10 );
    SW_ASSERT_TRUE( pShooter != nullptr && pShot != nullptr );
    pShot->setInstigator( pShooter->getOwner()->getHandle() );

    // 파일 id 로 적는다 — 쏜 쪽 7, 총알 8.
    ObjectSavedIDMap mapSavedID;
    mapSavedID.emplace( pShooter->getOwner()->getHandle().objectID(), 7u );
    mapSavedID.emplace( pShot->getOwner()->getHandle().objectID(), 8u );
    ObjectSaveOptions options{};
    options._pSavedIDMap = &mapSavedID;
    vector<uint8> bytes;
    SW_ASSERT_TRUE( ObjectStateSerializer::saveToBinaryBuffer( pShot->getOwner(), bytes, options ) );

    GameObject* pNewShooter = manager.createGameObject( hashed_string( "NewShooter" ) );
    GameObject* pNewShot    = manager.createGameObject( hashed_string( "NewShot" ) );
    SW_ASSERT_TRUE( pNewShooter != nullptr && pNewShot != nullptr );
    {
        ObjectStateBatch batch( ObjectIDSpace::Saved );
        batch.add( pNewShooter, 7u, hashed_string( "Shooter" ), false );
        ObjectLoadContext context{};
        context._pBatch  = &batch;
        context._savedID = 8u;
        SW_ASSERT_TRUE( ObjectStateSerializer::loadFromBinaryBuffer( pNewShot, bytes.data(), bytes.size(), context ) > 0 );
        batch.finish();
    }
    const ProjectileComponent* pFromFile = pNewShot->getComponent<ProjectileComponent>();
    SW_ASSERT_NOT_NULL( pFromFile );
    SW_EXPECT_TRUE( pFromFile->getInstigator() == pNewShooter->getHandle() );
}

/**
 * @brief [ActionCombatTest] 파일 상태의 핸들이 묶음에 없는 id 를 가리키면 없음이 되고, 프리팹은 핸들을 싣지 않는다
 * @details 파일 id 는 그 파일(묶음) 안에서만 뜻이 있다 — 묶음에 없는 값을 그대로 두면 이 실행에서 우연히 같은 값을 받은 오브젝트를 가리킨다(부모
 *          부착과 같은 함정). 프리팹은 여러 번 스폰되므로 다른 오브젝트를 가리키는 것을 싣지 않는다 — 실으면 스폰한 인스턴스가
 *          모두 원본을 만든 실행의 오브젝트를 가리킨다.
 */
SW_TEST_CASE( ActionCombatTest, ObjectReferencesOutsideTheirFileBecomeNone )
{
    GameObjectManager    manager;
    UnitStatsComponent*  pShooter = spawnUnit( manager, "Shooter", -20.0f, 100, 0, 0.0f );
    ProjectileComponent* pShot    = spawnBullet( manager, 0.0f, 0.0f, 0.0f, 10 );
    SW_ASSERT_TRUE( pShooter != nullptr && pShot != nullptr );

    // 1) 파일 상태(Saved)로 읽는데 그 값의 오브젝트가 묶음에 없다 — 없음.
    pShot->setInstigator( GameObjectHandle::make( 987654321 ) );
    const string state  = ObjectStateSerializer::saveToXmlString( pShot->getOwner() );
    GameObject*  pFresh = manager.createGameObject( hashed_string( "FromFile" ) );
    SW_ASSERT_TRUE( state.empty() == false && pFresh != nullptr );
    {
        ObjectStateBatch  batch( ObjectIDSpace::Saved );
        ObjectLoadContext context{};
        context._pBatch  = &batch;
        context._savedID = 1;
        SW_ASSERT_TRUE( ObjectStateSerializer::loadFromXmlString( pFresh, state, context ) );
        batch.finish();
    }
    const ProjectileComponent* pFromFile = pFresh->getComponent<ProjectileComponent>();
    SW_ASSERT_NOT_NULL( pFromFile );
    SW_EXPECT_FALSE( pFromFile->getInstigator().isValid() );

    // 2) 프리팹은 쏜 쪽을 싣지 않는다 — 스폰한 것이 원본의 쏜 쪽을 가리키지 않는다.
    pShot->setInstigator( pShooter->getOwner()->getHandle() );
    PrefabAsset asset;
    asset.setFromGameObject( pShot->getOwner() );
    GameObject* pSpawned = manager.createGameObject( hashed_string( "FromPrefab" ) );
    SW_ASSERT_NOT_NULL( pSpawned );
    SW_ASSERT_TRUE( asset.applyStateTo( pSpawned ) );
    const ProjectileComponent* pFromPrefab = pSpawned->getComponent<ProjectileComponent>();
    SW_ASSERT_NOT_NULL( pFromPrefab );
    SW_EXPECT_FALSE( pFromPrefab->getInstigator().isValid() );
}

/**
 * @brief [ActionCombatTest] 공격 판정은 접촉마다 기억하고, 판정 안에서 사라진 유닛은 잊는다 — 남은 접촉 · 새로 들어온 유닛은 제대로 맞는다
 * @details 판정은 겹친 상대를 핸들로 기억해 휘두를 때 맞힌다. 상대가 판정 안에서 사라지면 끝 이벤트가 상대 없이 오므로 풀리지 않는 핸들을 덜어
 *          낸다(아니면 핸들이 남고 목록이 줄지 않는다). 또 상대 하나가 콜라이더 둘로 겹치면 접촉 둘로 기억한다 — 하나만 떨어져도 남은 접촉으로 맞는다
 *          (오브젝트당 하나로 기억하면 한 콜라이더가 떨어질 때 다른 콜라이더가 겹쳐 있어도 잊는다). 상대의 트리거(감지 범위)는 접촉이 아니다.
 */
SW_TEST_CASE( ActionCombatTest, AttackTracksEachContactAndForgetsUnitsThatDieInside )
{
    GameObjectManager   manager;
    GameObject*         pHitbox = spawnColliderObject( manager, "Hitbox", 0.0f, 0.0f, float2( 2.0f, 2.0f ) );
    UnitStatsComponent* pDoomed = spawnUnit( manager, "Doomed", 0.5f, 100, 0, 0.0f );
    UnitStatsComponent* pTwin   = spawnUnit( manager, "Twin", -0.5f, 100, 0, 0.0f );
    UnitStatsComponent* pSensed = spawnUnit( manager, "Sensed", 0.0f, 100, 0, 0.0f );
    UnitStatsComponent* pLate   = spawnUnit( manager, "Late", 8.0f, 100, 0, 0.0f );
    SW_ASSERT_TRUE( pHitbox != nullptr && pDoomed != nullptr && pTwin != nullptr && pSensed != nullptr && pLate != nullptr );
    MeleeHitboxComponent* pAttack = pHitbox->addComponent<MeleeHitboxComponent>();
    SW_ASSERT_NOT_NULL( pAttack );
    // 쌍둥이는 몸 콜라이더가 둘이다 — 둘 다 판정 안에 있다.
    BoxCollider2DComponent* pTwinSecond = pTwin->getOwner()->addComponent<BoxCollider2DComponent>();
    SW_ASSERT_NOT_NULL( pTwinSecond );
    pTwinSecond->setOffsetScale( float2( 0.5f, 0.5f ) );
    // 감지 범위만 판정에 닿은 유닛 — 몸은 판정 밖에 있다.
    pSensed->getOwner()->getComponent<BoxCollider2DComponent>()->setTrigger( true );

    manager.beginPlay();
    manager.tick( 0.1f );
    SW_EXPECT_EQUAL( 3u, pAttack->getOverlapCount() ); // 운 나쁜 유닛 하나 · 쌍둥이 둘(감지 범위는 아니다)

    // 판정 안에서 사라지면 잊는다.
    manager.destroyObject( pDoomed->getOwner() );
    manager.tick( 0.1f );
    SW_EXPECT_EQUAL( 2u, pAttack->getOverlapCount() );

    // 쌍둥이의 콜라이더 하나가 떨어져도 남은 하나로 맞는다.
    pTwinSecond->setOffsetPosition( float2( 0.0f, 20.0f ) );
    manager.tick( 0.1f );
    SW_EXPECT_EQUAL( 1u, pAttack->getOverlapCount() );
    pAttack->beginAttack( 10, 1.0f );
    SW_EXPECT_EQUAL( 90, pTwin->getHp() );
    SW_EXPECT_EQUAL( 100, pSensed->getHp() );

    // 휘두르는 동안 새로 들어온 유닛도 한 번 맞는다.
    pLate->getOwner()->getPrimarySceneComponent()->setLocalPosition( float3( 0.5f, 0.0f, 0.0f ) );
    manager.tick( 0.1f );
    SW_EXPECT_EQUAL( 90, pLate->getHp() );
    SW_EXPECT_EQUAL( 2u, pAttack->getOverlapCount() );
    manager.endPlay();
}

/**
 * @brief [ActionCombatTest] 액션 룸은 상태가 바뀔 때 룸 이벤트를 낸다 — 시작에 게이트 닫힘, 클리어에 결과와 게이트 열림, 패배에 복귀와 게이트 열림
 * @details 룸 이벤트 셋(`RoomClearedEvent` · `PlayerDefeatedInRoomEvent` · `ClearGateStateChangedEvent`)은 룸이 상태가 바뀌는 자리에서 낸다
 *          (언리얼 GameMode 의 브로드캐스트). 어느 맵 · 존의 룸인지는 룸을 연 게임이 준다(`ActionRoomSite`). 클리어는 한
 *          번만 알린다. 패배는 HP 를 가진 게임이 알린다(`onPlayerDefeated`) — 활성이 아닌 룸은 아무것도 내지 않는다.
 */
SW_TEST_CASE( ActionCombatTest, ActionRoomAnnouncesGateClearAndDefeat )
{
    EventDispatcher                    dispatcher;
    const ScopedEventDispatcherService scopedDispatcher{ dispatcher };
    vector<RoomClearedEvent>           listCleared;
    vector<PlayerDefeatedInRoomEvent>  listDefeated;
    vector<ClearGateStateChangedEvent> listGate;
    dispatcher.subscribe<RoomClearedEvent>( gameEventChannel(), SW_DELEGATE_LAMBDA( Delegate<void( const RoomClearedEvent& )>, [&listCleared]( const RoomClearedEvent& event )
    { listCleared.push_back( event ); } ) );
    dispatcher.subscribe<PlayerDefeatedInRoomEvent>( gameEventChannel(), SW_DELEGATE_LAMBDA( Delegate<void( const PlayerDefeatedInRoomEvent& )>, [&listDefeated]( const PlayerDefeatedInRoomEvent& event )
    { listDefeated.push_back( event ); } ) );
    dispatcher.subscribe<ClearGateStateChangedEvent>( gameEventChannel(), SW_DELEGATE_LAMBDA( Delegate<void( const ClearGateStateChangedEvent& )>, [&listGate]( const ClearGateStateChangedEvent& event )
    { listGate.push_back( event ); } ) );

    ActionRoom     room;
    ActionRoomSite site;
    site._mapPath       = "game/test/maps/dungeon.scene.xml";
    site._zoneID        = "zone_dungeon";
    site._returnMapPath = "game/test/maps/town.scene.xml";
    room.setSite( site );

    // 1) 홀 — 들어오면 닫히고, 마지막 적이 쓰러진 프레임에 클리어와 열림. 적이 오른쪽에서 다가오는 자리에 서서 공격한다.
    room.beginEntrance();
    SW_ASSERT_EQUAL( static_cast<size_t>( 1 ), listGate.size() );
    SW_EXPECT_TRUE( listGate[0]._zoneID == site._zoneID );
    SW_EXPECT_TRUE( listGate[0]._bLocked == SW_TRUE );
    SW_EXPECT_TRUE( listGate[0]._bTriggered == SW_TRUE );
    ActionRoomFrameInput input;
    input._playerPos      = float2{ 0.0f, 4.0f };
    input._facing         = FacingDir::Right;
    input._bAttackPressed = SW_TRUE;
    SW_ASSERT_TRUE( updateRoomUntilCleared( room, input, 1000 ) );
    SW_ASSERT_EQUAL( static_cast<size_t>( 1 ), listCleared.size() );
    SW_EXPECT_TRUE( listCleared[0]._mapPath == site._mapPath );
    SW_EXPECT_TRUE( listCleared[0]._bBossDefeated == SW_FALSE );
    SW_ASSERT_EQUAL( static_cast<size_t>( 2 ), listGate.size() );
    SW_EXPECT_TRUE( listGate[1]._bLocked == SW_FALSE );
    // 클리어는 한 번만 알린다.
    for ( int32 frameIndex = 0; frameIndex < 10; ++frameIndex )
    {
        (void)room.update( 0.02f, input );
    }
    SW_EXPECT_EQUAL( static_cast<size_t>( 1 ), listCleared.size() );
    SW_EXPECT_EQUAL( static_cast<size_t>( 2 ), listGate.size() );

    // 2) 보스 — 클리어가 보스 처치를 싣는다. 보스 바로 앞에 서서 친다.
    room.beginBoss();
    SW_ASSERT_EQUAL( static_cast<size_t>( 3 ), listGate.size() );
    input._playerPos = float2{ 5.0f, 4.0f };
    SW_ASSERT_TRUE( updateRoomUntilCleared( room, input, 1000 ) );
    SW_ASSERT_EQUAL( static_cast<size_t>( 2 ), listCleared.size() );
    SW_EXPECT_TRUE( listCleared[1]._bBossDefeated == SW_TRUE );
    SW_EXPECT_EQUAL( static_cast<size_t>( 4 ), listGate.size() );

    // 3) 패배 — 복귀할 맵을 싣고, 문이 열리고, 룸이 비워진다. 비워진 룸은 다시 알리지 않는다.
    room.beginHall();
    SW_ASSERT_EQUAL( static_cast<size_t>( 5 ), listGate.size() );
    room.onPlayerDefeated();
    SW_ASSERT_EQUAL( static_cast<size_t>( 1 ), listDefeated.size() );
    SW_EXPECT_TRUE( listDefeated[0]._returnMapPath == site._returnMapPath );
    SW_ASSERT_EQUAL( static_cast<size_t>( 6 ), listGate.size() );
    SW_EXPECT_TRUE( listGate[5]._bLocked == SW_FALSE );
    SW_EXPECT_FALSE( room.isActive() );
    room.onPlayerDefeated();
    SW_EXPECT_EQUAL( static_cast<size_t>( 1 ), listDefeated.size() );
}

/**
 * @brief [ActionCombatTest] 유닛은 같은 오브젝트의 HP 바를 몰고 간다 — 시작 · 피해 · 회복 · 스탯 재설정이 비율을 맞춘다
 * @details HP 가 바뀌는 자리(`UnitStatsComponent`)가 HP 바의 `setTargetRatio` 를 맞춘다 — 부르는 곳이 없으면 HP 바는 늘 가득 차 있다.
 */
SW_TEST_CASE( ActionCombatTest, UnitDrivesItsHealthBar )
{
    GameObjectManager   manager;
    UnitStatsComponent* pUnit = spawnUnit( manager, "Hero", 0.0f, 100, 0, 0.0f );
    SW_ASSERT_NOT_NULL( pUnit );
    pUnit->setStats( 50, 100, 0, 0, 0.0f, 0.0f );
    // 바는 스탯 뒤에 단다 — 씬에서 읽은 유닛처럼 시작할 때만 맞출 수 있다.
    HealthBarComponent* pBar = pUnit->getOwner()->addComponent<HealthBarComponent>();
    SW_ASSERT_NOT_NULL( pBar );
    manager.beginPlay();
    SW_EXPECT_NEAR_EQUAL( 0.5f, pBar->getTargetRatio(), 1e-4f );
    SW_EXPECT_NEAR_EQUAL( 0.5f, pBar->getRemainRatio(), 1e-4f ); // 시작은 흔적 없이

    pUnit->takeDamage( 20 );
    SW_EXPECT_NEAR_EQUAL( 0.3f, pBar->getTargetRatio(), 1e-4f );
    pUnit->heal( 10 );
    SW_EXPECT_NEAR_EQUAL( 0.4f, pBar->getTargetRatio(), 1e-4f );
    pUnit->setStats( 80, 100, 0, 0, 0.0f, 0.0f ); // 스탯 재설정(부활)은 흔적 없이
    SW_EXPECT_NEAR_EQUAL( 0.8f, pBar->getRemainRatio(), 1e-4f );
    manager.endPlay();
}

/**
 * @brief [ActionCombatTest] 유닛이 쓰러지면 HP 바는 쓰러짐을 받는다 — `_bHideWhenDead` 인 바는 숨는다
 * @details 유닛은 체력 원천(`HealthSourceComponent`)이고 알림 종류를 읽기(`_bIsDead`)에서 정한다. 바뀜으로만 알리면 쓰러져도 바가 남는다.
 */
SW_TEST_CASE( ActionCombatTest, UnitDeathHidesAHealthBarThatHidesOnDeath )
{
    GameObjectManager   manager;
    UnitStatsComponent* pUnit = spawnUnit( manager, "Hero", 0.0f, 30, 0, 0.0f );
    SW_ASSERT_NOT_NULL( pUnit );
    HealthBarComponent* pBar = pUnit->getOwner()->addComponent<HealthBarComponent>();
    SW_ASSERT_NOT_NULL( pBar );
    SW_ASSERT_TRUE( setUnitBarFlag( *pBar, "_bHideWhenDead", true ) );
    pBar->setVisible( true );
    manager.beginPlay();

    pUnit->takeDamage( 10 );
    SW_EXPECT_TRUE( pBar->isVisible() ); // 살아 있다
    pUnit->takeDamage( 100 );
    SW_EXPECT_TRUE( pUnit->isDead() );
    SW_EXPECT_NEAR_EQUAL( 0.0f, pBar->getTargetRatio(), 1e-4f );
    SW_EXPECT_FALSE( pBar->isVisible() );
    manager.endPlay();
}

/**
 * @brief [ActionCombatTest] 맞은 뒤에 붙인 HP 바는 유닛의 지금 HP 에서 시작한다
 * @details 바는 시작할 때 같은 오브젝트의 체력 원천을 읽는다 — 알림만 기다리면 다음 피해 전까지 저장된 칸(`_hpRatio`, 기본 0)을 그린다.
 */
SW_TEST_CASE( ActionCombatTest, HealthBarAddedAfterAHitStartsAtTheUnitsHealth )
{
    GameObjectManager   manager;
    UnitStatsComponent* pUnit = spawnUnit( manager, "Hero", 0.0f, 100, 0, 0.0f );
    SW_ASSERT_NOT_NULL( pUnit );
    manager.beginPlay();
    pUnit->takeDamage( 60 );
    SW_ASSERT_EQUAL( 40, pUnit->getHp() );

    HealthBarComponent* pBar = pUnit->getOwner()->addComponent<HealthBarComponent>();
    SW_ASSERT_NOT_NULL( pBar );
    manager.tick( 0.016f ); // 플레이 중에 붙인 컴포넌트는 다음 틱의 시작 단계에서 시작한다
    SW_EXPECT_NEAR_EQUAL( 0.4f, pBar->getTargetRatio(), 1e-4f );
    SW_EXPECT_NEAR_EQUAL( 0.4f, pBar->getRemainRatio(), 1e-4f ); // 흔적 없이
    manager.endPlay();
}

/**
 * @brief [ActionCombatTest] 데미지 숫자를 켠 유닛은 깎인 피해만큼의 숫자를 머리 위에 띄운다 — 끈 유닛은 띄우지 않는다
 * @details 피해가 들어간 자리에서 데미지 숫자 컴포넌트에 값을 넣고 띄운다. 숫자는 요청한 피해가 아니라 방어력을 뺀 실제 피해다.
 */
SW_TEST_CASE( ActionCombatTest, UnitSpawnsDamageNumbersWhenAsked )
{
    GameObjectManager   manager;
    UnitStatsComponent* pShown  = spawnUnit( manager, "Shown", 2.0f, 100, 5, 0.0f );
    UnitStatsComponent* pSilent = spawnUnit( manager, "Silent", -2.0f, 100, 5, 0.0f );
    SW_ASSERT_TRUE( pShown != nullptr && pSilent != nullptr );
    pShown->setShowDamageNumbers( true );
    manager.beginPlay();

    pSilent->takeDamage( 25 );
    pShown->takeDamage( 25 );

    sw::vector<DamageNumberComponent*> listNumber;
    manager.forEachGameObject( [&listNumber]( GameObject* pObject )
    {
        if ( DamageNumberComponent* pNumber = pObject->getComponent<DamageNumberComponent>() )
            listNumber.push_back( pNumber );
    } );
    SW_ASSERT_EQUAL( static_cast<size_t>( 1 ), listNumber.size() );
    SW_EXPECT_EQUAL( 20, listNumber[0]->getDamageValue() );
    SW_EXPECT_TRUE( listNumber[0]->getLifeTime() > 0.0f ); // 떠오르다 사라진다 — 수명 0 이면 영영 남는다
    const SceneComponent* pNumberRoot = listNumber[0]->getOwner()->getPrimarySceneComponent();
    SW_ASSERT_NOT_NULL( pNumberRoot );
    const float3 expected = float3( 2.0f, 0.0f, 0.0f ) + pShown->getDamageNumberOffset();
    SW_EXPECT_NEAR_EQUAL( expected._x, pNumberRoot->getWorldPosition()._x, 1e-4f );
    SW_EXPECT_NEAR_EQUAL( expected._y, pNumberRoot->getWorldPosition()._y, 1e-4f );
    manager.endPlay();
}

/**
 * @brief [ActionCombatTest] 피해 0 은 맞지 않은 것이고, 방어가 피해보다 커도 맞으면 1 은 깎인다
 * @details 방어 식은 `DamageMath::applyArmor` 하나다(액션 룸의 적도 같은 식). 0 이하 피해는 HP · 무적 · 이벤트를 건드리지 않는다 — 언리얼
 *          `UGameplayStatics::ApplyDamage` 가 0 을 버리는 것과 같다. 설정하지 않은 투사체(피해 0)가 맞을 때마다 1 씩 깎으면 안 된다.
 */
SW_TEST_CASE( ActionCombatTest, ZeroDamageIsNoHitAndArmorLeavesAtLeastOne )
{
    GameObjectManager   manager;
    UnitStatsComponent* pUnit = spawnUnit( manager, "Tank", 0.0f, 100, 10, 0.5f );
    SW_ASSERT_NOT_NULL( pUnit );
    int32                hitCount     = 0;
    const DelegateHandle subscription = pUnit->registerDamageApplied( SW_DELEGATE_LAMBDA( UnitStatsComponent::DamageAppliedDelegate, [&hitCount]( const DamageAppliedEvent& event )
    {
        (void)event;
        ++hitCount;
    } ) );
    manager.beginPlay();

    pUnit->takeDamage( 0 );
    SW_EXPECT_EQUAL( 100, pUnit->getHp() );
    SW_EXPECT_EQUAL( 0, hitCount );
    pUnit->takeDamage( 3 ); // 0 은 무적을 걸지 않았다 — 방어 10 이 3 보다 커도 1
    SW_EXPECT_EQUAL( 99, pUnit->getHp() );
    SW_EXPECT_EQUAL( 1, hitCount );
    pUnit->unregisterDamageApplied( subscription );
    manager.endPlay();
}

/**
 * @brief [ActionCombatTest] 보스 발사 빈도는 프레임률과 상관없다 — 20 · 30 · 60 fps 로 120 초면 1 + (120 − 1.2) / 1.6 = 75.25 발(±1)
 * @details 끝난 프레임에 간격으로 덮으면 지나친 몫을 버린다 — float 로 dt 를 빼다 0 에 조금 못 미치는 프레임이 생겨 20 fps 72 발, 30 fps 73 발이 된다.
 *          쏜 횟수는 프레임 결과(`ActionRoomFrameResult::_enemyVolleyCount`)로 센다.
 */
SW_TEST_CASE( ActionCombatTest, BossFireRateDoesNotDependOnFrameRate )
{
    EventDispatcher                    dispatcher;
    const ScopedEventDispatcherService scopedDispatcher{ dispatcher };
    const float32                      design = 1.0f + ( 120.0f - 1.2f ) / 1.6f;
    for ( const float32 framesPerSecond : { 20.0f, 30.0f, 60.0f } )
    {
        ActionRoom room;
        room.beginBoss();
        ActionRoomFrameInput input;
        input._playerPos       = float2{ 7.0f, 60.0f }; // 멀리 서서 치지 않는다
        const int32 frameCount = static_cast<int32>( 120.0f * framesPerSecond + 0.5f );
        int32       shotCount  = 0;
        for ( int32 frameIndex = 0; frameIndex < frameCount; ++frameIndex )
        {
            shotCount += room.update( 1.0f / framesPerSecond, input )._enemyVolleyCount;
        }
        SW_EXPECT_NEAR_EQUAL( design, static_cast<float32>( shotCount ), 1.0f );
    }
}

/**
 * @brief [ActionCombatTest] 액션 룸 적의 수치 — 그런트는 닿으면 8 · 한 번에 쓰러지고, 보스는 닿으면 12 · 탄 10 · 한 번에 18 씩 최대 220 이라 13 번째에 쓰러진다
 * @details 적의 수치를 코드 상수에서 몬스터 정의로 옮겨도 동작이 같은지 본다(내장 정의가 옛 상수와 같아야 한다). 피해는 프레임 결과로, HP 는 보스 게이지
 *          (`getBossHpFill`)와 살아 있는 적 수로 읽는다.
 */
SW_TEST_CASE( ActionCombatTest, ActionRoomEnemyNumbersStayTheSame )
{
    EventDispatcher                    dispatcher;
    const ScopedEventDispatcherService scopedDispatcher{ dispatcher };
    ActionRoom                         room;
    ActionRoomFrameInput               input;

    // 1) 그런트 — 첫 그런트(5, 2.5) 위에 서면 닿은 피해 8.
    room.beginHall();
    input._playerPos = float2{ 5.0f, 2.5f };
    SW_EXPECT_EQUAL( 8, room.update( 0.016f, input )._damageToPlayer );

    // 2) 그런트 — 한 번 치면 쓰러진다(34 ≥ HP 30). 왼쪽에서 오른쪽을 보고 친다(닿지는 않는 자리).
    room.beginHall();
    input._playerPos      = float2{ 4.0f, 2.5f };
    input._facing         = FacingDir::Right;
    input._bAttackPressed = SW_TRUE;
    (void)room.update( 0.016f, input ); // 살아 있는 적 수를 본다
    SW_EXPECT_EQUAL( 2, room.getAliveEnemyCount() );

    // 3) 보스 — 보스(7, 4) 위에 서면 닿은 피해 12.
    room.beginBoss();
    input._playerPos      = float2{ 7.0f, 4.0f };
    input._bAttackPressed = SW_FALSE;
    SW_EXPECT_EQUAL( 12, room.update( 0.016f, input )._damageToPlayer );

    // 4) 보스 탄 — 8 m 떨어져 서면 처음 맞는 것은 겨냥한 탄이고 피해 10(옆으로 나간 탄은 비켜 간다).
    room.beginBoss();
    input._playerPos = float2{ 7.0f, 12.0f };
    SW_EXPECT_EQUAL( 10, updateRoomUntilPlayerIsHit( room, input, 300 ) );

    // 5) 보스 HP — 한 번에 18, 최대 220 → 13 번째에 쓰러진다.
    room.beginBoss();
    input._playerPos      = float2{ 5.6f, 4.0f };
    input._bAttackPressed = SW_TRUE;
    (void)room.update( 0.016f, input ); // 게이지를 본다
    SW_EXPECT_NEAR_EQUAL( 202.0f / 220.0f, room.getBossHpFill(), 1e-5f );
    int32   hitCount = 1;
    float32 lastFill = room.getBossHpFill();
    for ( int32 frameIndex = 0; frameIndex < 2000 && room.isCleared() == false; ++frameIndex )
    {
        (void)room.update( 0.02f, input ); // 게이지를 본다
        const float32 fill = room.getBossHpFill();
        if ( fill < lastFill )
            ++hitCount;
        lastFill = fill;
    }
    SW_EXPECT_TRUE( room.isCleared() );
    SW_EXPECT_EQUAL( 13, hitCount );
}

/**
 * @brief [ActionCombatTest] 액션 룸은 게임이 건 몬스터 카탈로그에서 적을 읽는다 — HP · 방어 · 닿은 피해 · 첫 사격 · 탄 피해
 * @details 카탈로그(`MonsterCatalog`)를 게임 서비스로 걸면 룸의 종 id(`grunt` · `boss`)를 거기서 찾는다. 방어 식은 유닛 스탯과 같다(34 − 방어, 최소 1).
 */
SW_TEST_CASE( ActionCombatTest, ActionRoomReadsItsMonstersFromTheCatalogService )
{
    EventDispatcher                    dispatcher;
    const ScopedEventDispatcherService scopedDispatcher{ dispatcher };
    const string                       path = test::makeTempPath( "room_monsters.xml" );
    SW_ASSERT_TRUE( FileUtil::writeTextFile( path, "<MonsterCatalog>\n"
                                                   "  <Monster id=\"grunt\"><Stats hp=\"60\" maxHp=\"60\" atk=\"5\" def=\"4\" speed=\"1.8\" radius=\"0.32\"/></Monster>\n"
                                                   "  <Monster id=\"boss\"><Stats hp=\"100\" maxHp=\"100\" atk=\"12\" def=\"0\" speed=\"0.9\" radius=\"0.7\"/>\n"
                                                   "    <AI coolTime=\"1.6\" firstDelay=\"0.5\"/>\n"
                                                   "    <Shot angle=\"0\" speed=\"4.5\" life=\"2.5\" radius=\"0.22\" damage=\"7\"/>\n"
                                                   "  </Monster>\n"
                                                   "</MonsterCatalog>\n" ) );
    MonsterCatalog catalog;
    SW_ASSERT_TRUE( catalog.loadFromResource( path ) );
    const ScopedMonsterCatalogService scopedCatalog{ catalog };
    ActionRoom                        room;
    ActionRoomFrameInput              input;

    // 그런트 — 닿은 피해 5.
    room.beginHall();
    input._playerPos = float2{ 5.0f, 2.5f };
    SW_EXPECT_EQUAL( 5, room.update( 0.016f, input )._damageToPlayer );

    // 그런트 — 한 번에 30(34 − 4)이라 HP 60 은 첫 타에 서 있고 둘째 타에 쓰러진다.
    room.beginHall();
    input._playerPos      = float2{ 4.0f, 2.5f };
    input._facing         = FacingDir::Right;
    input._bAttackPressed = SW_TRUE;
    (void)room.update( 0.016f, input ); // 살아 있는 적 수를 본다
    SW_EXPECT_EQUAL( 3, room.getAliveEnemyCount() );
    for ( int32 frameIndex = 0; frameIndex < 50 && room.getAliveEnemyCount() == 3; ++frameIndex )
    {
        (void)room.update( 0.02f, input ); // 살아 있는 적 수를 본다
    }
    SW_EXPECT_EQUAL( 2, room.getAliveEnemyCount() );

    // 보스 — 방어 0 이라 한 번에 34(66 / 100).
    room.beginBoss();
    input._playerPos = float2{ 5.6f, 4.0f };
    (void)room.update( 0.016f, input ); // 게이지를 본다
    SW_EXPECT_NEAR_EQUAL( 0.66f, room.getBossHpFill(), 1e-5f );

    // 보스 — 첫 사격은 0.5 초 뒤(0.02 초 걸음으로 25 번째 안팎), 탄 피해 7.
    room.beginBoss();
    input._playerPos       = float2{ 7.0f, 12.0f };
    input._bAttackPressed  = SW_FALSE;
    int32 firstVolleyFrame = -1;
    for ( int32 frameIndex = 0; frameIndex < 100 && firstVolleyFrame < 0; ++frameIndex )
    {
        if ( room.update( 0.02f, input )._enemyVolleyCount > 0 )
            firstVolleyFrame = frameIndex;
    }
    SW_EXPECT_TRUE( 23 <= firstVolleyFrame && firstVolleyFrame <= 25 );
    SW_EXPECT_EQUAL( 7, updateRoomUntilPlayerIsHit( room, input, 300 ) );
}

/**
 * @brief [ActionCombatTest] 걸린 카탈로그에 룸의 종이 없으면 알리고 내장 정의로 세운다 — 한 싸움에 한 번 알린다
 * @details 데이터 오타가 조용히 내장 수치로 바뀌면 "카탈로그를 고쳤는데 그대로다" 를 찾을 길이 없다. 같은 종의 둘째 · 셋째는 그 싸움의 정의 목록에서 찾는다.
 */
SW_TEST_CASE( ActionCombatTest, ActionRoomWarnsWhenTheCatalogLacksItsMonster )
{
    EventDispatcher                    dispatcher;
    const ScopedEventDispatcherService scopedDispatcher{ dispatcher };
    const string                       path = test::makeTempPath( "bat_monsters.xml" );
    SW_ASSERT_TRUE( FileUtil::writeTextFile( path, "<MonsterCatalog>\n  <Monster id=\"bat\"/>\n</MonsterCatalog>\n" ) );
    MonsterCatalog catalog;
    SW_ASSERT_TRUE( catalog.loadFromResource( path ) );
    const ScopedMonsterCatalogService scopedCatalog{ catalog };

    SW_TEST_DEFENSIVE_SCOPE( "a monster missing from the catalog is reported" );
    test::ScopedLogCollector logCollector;
    ActionRoom               room;
    room.beginHall();
    SW_EXPECT_EQUAL( 3, room.getAliveEnemyCount() );
    SW_EXPECT_TRUE_MSG( logCollector.countContaining( "Monster 'grunt' is not in the monster catalog" ) == 1u, logCollector.joined().c_str() );
}

/**
 * @brief [ActionCombatTest] 룸 상태 바이트 — 한 번 맞은 보스 · 날아가는 탄 · 쿨다운이 그대로 오고(보스 게이지는 다시 찾은 정의의 최대 체력으로),
 *        같은 프레임을 더 돌려도 바이트가 같다. 잘린 바이트는 거절하고 그대로 둔다
 */
SW_TEST_CASE( ActionCombatTest, StateRoundTripContinuesTheSameRoom )
{
    EventDispatcher                    dispatcher;
    const ScopedEventDispatcherService scopedDispatcher{ dispatcher };
    ActionRoom                         room;
    room.beginBoss();
    ActionRoomFrameInput strike;
    strike._playerPos      = float2{ 5.6f, 4.0f };
    strike._facing         = FacingDir::Right;
    strike._bAttackPressed = SW_TRUE;
    (void)room.update( 0.02f, strike ); // 보스를 한 번 친다 — 202 / 220
    ActionRoomFrameInput keepAway;
    keepAway._playerPos = float2{ 7.0f, 12.0f }; // 멀리 서서 탄을 받는다
    for ( int32 frameIndex = 0; frameIndex < 80; ++frameIndex )
    {
        (void)room.update( 0.02f, keepAway ); // 첫 사격(1.2 초)을 지나 탄이 날고 있다
    }

    const vector<uint8> bytes = captureRoomBytes( room );
    ActionRoom          restored;
    Archive             reader( bytes.data(), bytes.size() );
    SW_ASSERT_TRUE( restored.readState( reader ) );
    SW_EXPECT_EQUAL( uint64{ 0 }, reader.getRemainingBytes() );
    SW_EXPECT_TRUE( restored.getKind() == ActionRoomKind::Boss );
    SW_EXPECT_NEAR_EQUAL( 202.0f / 220.0f, restored.getBossHpFill(), 1e-5f );
    SW_EXPECT_NEAR_EQUAL( room.getDashFill(), restored.getDashFill(), 1e-6f );
    SW_EXPECT_TRUE( bytes == captureRoomBytes( restored ) );

    int32 volleyCount         = 0;
    int32 restoredVolleyCount = 0;
    for ( int32 frameIndex = 0; frameIndex < 120; ++frameIndex )
    {
        volleyCount += room.update( 0.02f, keepAway )._enemyVolleyCount;
        restoredVolleyCount += restored.update( 0.02f, keepAway )._enemyVolleyCount;
    }
    SW_EXPECT_TRUE( 0 < volleyCount );
    SW_EXPECT_EQUAL( volleyCount, restoredVolleyCount ); // 사격 시간이 이어진다
    SW_EXPECT_TRUE( captureRoomBytes( room ) == captureRoomBytes( restored ) );

    ActionRoom truncated;
    Archive    cut( bytes.data(), bytes.size() - 1 );
    SW_EXPECT_FALSE( truncated.readState( cut ) );
    SW_EXPECT_FALSE( truncated.isActive() );
}
