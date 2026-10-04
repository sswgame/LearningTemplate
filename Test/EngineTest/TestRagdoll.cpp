#include "pch.h"

#include "Core/File/FileUtil.h"
#include "Core/Math/MathUtil.h"

#include "Engine/Character/CharacterDataCache.h"
#include "Engine/Character/CharacterHit.h"
#include "Engine/Character/RagdollComponent.h"
#include "Engine/Object/Component/3D/SkeletalAnimatorComponent.h"
#include "Engine/Object/Component/3D/SkeletalMeshComponent.h"
#include "Engine/Object/Component/Physics/RigidBodyComponent.h"
#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManager.h"
#include "Engine/Object/GameObject/ScenePhysics.h"
#include "Engine/Physics/IPhysicsScene.h"
#include "Engine/Physics/PhysicsAsset.h"
#include "Engine/Resource/AssetManager.h"
#include "Engine/Resource/ResourceUtil.h"

#include "TestFramework/TestFramework.h"

using namespace sw;

// RagdollTest — KayKit 스켈레톤(41 뼈)의 물리 에셋으로 세운 히트박스 · 래그돌: 포즈 따르기, 레이캐스트 → 히트 존, 치명 → 래그돌 · 가라앉음 · 기상,
// 맞음 반응, 부분 래그돌, 물리 에셋 핫 리로드.

namespace
{
    struct TestRagdollInternal
    {
        static constexpr float32     kFrame        = 1.0f / 60.0f;
        static constexpr const utf8* kSkeletonPath = "game/shooter3d/models/kaykit/skeleton_warrior/skeleton_warrior.skeleton.json";
        static constexpr const utf8* kPhysicsPath  = "game/shooter3d/models/kaykit/skeleton_warrior/skeleton_warrior.physics.xml";
        static constexpr const utf8* kClipFolder   = "game/shooter3d/models/kaykit/skeleton_warrior/clips";

        static void tickFor( GameObjectManager& manager, uint32 frameCount )
        {
            for ( uint32 frameIndex = 0; frameIndex < frameCount; ++frameIndex )
                manager.tick( kFrame );
        }

        static void spawnFloor( GameObjectManager& manager )
        {
            GameObject*         pFloor = manager.createGameObject( hashed_string( "Floor" ) );
            RigidBodyComponent* pBody  = pFloor->addComponent<RigidBodyComponent>();
            PhysicsShapeDesc3D  box;
            box._halfExtents = float3{ 20.0f, 0.5f, 20.0f };
            pBody->setShape( box );
            pBody->setBodyType( PhysicsBodyType::Static );
            pBody->setLayer( hashed_string( "Static" ) );
            pBody->setLocalPosition( float3{ 0.0f, -0.5f, 0.0f } );
        }

        /** @brief KayKit 스켈레톤 유닛 + 래그돌(+ 선택 애니메이터) 오브젝트입니다. 루트는 씬 컴포넌트, 유닛은 그 아래입니다. */
        static RagdollComponent* spawnSkeleton( GameObjectManager& manager, const utf8* pName, const float3& position, bool bAnimator, const utf8* pPhysicsPath = kPhysicsPath )
        {
            GameObject* pObject = manager.createGameObject( hashed_string( pName ) );
            if ( pObject == nullptr )
                return nullptr;
            SceneComponent*        pRoot = pObject->addComponent<SceneComponent>();
            SkeletalMeshComponent* pUnit = pObject->addComponent<SkeletalMeshComponent>();
            if ( pRoot == nullptr || pUnit == nullptr )
                return nullptr;
            pRoot->setLocalPosition( position );
            pUnit->setSkeletonPath( kSkeletonPath );
            if ( bAnimator )
            {
                SkeletalAnimatorComponent* pAnimator = pObject->addComponent<SkeletalAnimatorComponent>();
                pAnimator->setClipFolder( kClipFolder );
                pAnimator->setInitialState( "Idle" );
            }
            RagdollComponent* pRagdoll = pObject->addComponent<RagdollComponent>();
            if ( pRagdoll != nullptr )
                pRagdoll->setPhysicsAssetPath( pPhysicsPath );
            return pRagdoll;
        }

        static int32 findBody( const RagdollComponent& ragdoll, const utf8* pBone )
        {
            return ragdoll.getPhysicsAsset() != nullptr ? ragdoll.getPhysicsAsset()->findBodyIndex( hashed_string( pBone ) ) : -1;
        }

        static float3 getBodyPosition( GameObjectManager& manager, const RagdollComponent& ragdoll, int32 bodyIndex )
        {
            float3           position{};
            quaternion       rotation{};
            IPhysicsScene3D* pScene = manager.getScenePhysics().findScene3D();
            if ( pScene != nullptr && bodyIndex >= 0 )
                (void)pScene->getBodyTransform( ragdoll.getRagdoll()._listBody[static_cast<size_t>( bodyIndex )], position, rotation );
            return position;
        }

        static float3 getBoneWorldPosition( const RagdollComponent& ragdoll, const utf8* pBone )
        {
            const SkeletalMeshComponent* pUnit = ragdoll.getOwner()->getComponent<SkeletalMeshComponent>();
            float4x4                     model;
            if ( pUnit == nullptr || pUnit->findBoneModelTransform( hashed_string( pBone ), model ) == false )
                return float3{};
            return ( model * pUnit->getWorldMatrix() ).getTranslation();
        }
    };
} // namespace

/**
 * @brief [RagdollTest] KayKit 물리 에셋(16 바디)이 41 뼈 스켈레톤에 서고, 키네마틱 히트박스가 애니메이션 포즈를 따른다(오브젝트를 옮기면 따라간다)
 */
SW_TEST_CASE( RagdollTest, KayKitHitboxesFollowThePose )
{
    SW_ASSERT_TRUE( ResourceUtil::initialize() );
    GameObjectManager manager;
    TestRagdollInternal::spawnFloor( manager );
    RagdollComponent* pRagdoll = TestRagdollInternal::spawnSkeleton( manager, "Skeleton", float3{ 0.0f, 0.0f, 0.0f }, false );
    SW_ASSERT_NOT_NULL( pRagdoll );
    manager.beginPlay();
    TestRagdollInternal::tickFor( manager, 2 );
    SW_ASSERT_EQUAL( size_t( 16 ), pRagdoll->getRagdoll()._listBody.size() );
    SW_EXPECT_TRUE( pRagdoll->getState() == RagdollState::Animated );
    const int32 head = TestRagdollInternal::findBody( *pRagdoll, "head" );
    SW_ASSERT_TRUE( head >= 0 );
    SW_EXPECT_FALSE( pRagdoll->isBodyDynamic( static_cast<uint32>( head ) ) );

    pRagdoll->getOwner()->getPrimarySceneComponent()->setWorldPosition( float3{ 1.0f, 0.0f, 0.5f } );
    TestRagdollInternal::tickFor( manager, 2 );
    const float3 expected = TestRagdollInternal::getBoneWorldPosition( *pRagdoll, "head" );
    const float3 actual   = TestRagdollInternal::getBodyPosition( manager, *pRagdoll, head );
    SW_EXPECT_NEAR_EQUAL( 1.0f, expected._x, 1e-3f );
    SW_EXPECT_TRUE( ( expected - actual ).getLength() < 1e-3f );
    manager.endPlay();
}

/**
 * @brief [RagdollTest] 무기 레이캐스트 → 맞은 히트박스 바디 → 히트 존(머리 ×2 · 몸통 ×1)이 맞음에 실리고, 쏜 쪽 자신의 바디는 건너뛴다
 */
SW_TEST_CASE( RagdollTest, WeaponTraceResolvesHitZone )
{
    SW_ASSERT_TRUE( ResourceUtil::initialize() );
    GameObjectManager manager;
    RagdollComponent* pTarget  = TestRagdollInternal::spawnSkeleton( manager, "Target", float3{ 0.0f, 0.0f, 0.0f }, false );
    RagdollComponent* pShooter = TestRagdollInternal::spawnSkeleton( manager, "Shooter", float3{ 0.0f, 0.0f, -3.0f }, false );
    SW_ASSERT_TRUE( pTarget != nullptr && pShooter != nullptr );
    manager.beginPlay();
    TestRagdollInternal::tickFor( manager, 2 );

    const float3 headPosition = TestRagdollInternal::getBoneWorldPosition( *pTarget, "head" ) + float3{ 0.0f, 0.43f, 0.0f };
    HitInfo      hit;
    // 쏘는 쪽의 머리 안에서 쏘아도 자기 바디는 건너뛴다.
    const float3 muzzle = float3{ headPosition._x, headPosition._y, -3.0f };
    SW_ASSERT_TRUE( CharacterHitUtil::traceWeaponHit( manager, muzzle, float3{ 0.0f, 0.0f, 1.0f }, 10.0f, 0xFFFFFFFFu, pShooter->getOwner(), 10.0f, 0.0f, false, hit ) );
    SW_EXPECT_TRUE( hit._zone == hashed_string( "Head" ) );
    SW_EXPECT_NEAR_EQUAL( 2.0f, hit._damageMultiplier, 1e-6f );
    SW_EXPECT_EQUAL( TestRagdollInternal::findBody( *pTarget, "head" ), hit._bodyIndex );

    const float3 chest = TestRagdollInternal::getBoneWorldPosition( *pTarget, "chest" ) + float3{ 0.0f, 0.1f, 0.0f };
    SW_ASSERT_TRUE( CharacterHitUtil::traceWeaponHit( manager, float3{ chest._x, chest._y, -3.0f }, float3{ 0.0f, 0.0f, 1.0f }, 10.0f, 0xFFFFFFFFu, pShooter->getOwner(),
                                                      10.0f, 0.0f, false, hit ) );
    SW_EXPECT_TRUE( hit._zone == hashed_string( "Torso" ) );
    SW_EXPECT_NEAR_EQUAL( 1.0f, hit._damageMultiplier, 1e-6f );
    manager.endPlay();
}

/**
 * @brief [RagdollTest] 치명적 맞음 → 모든 바디가 동적으로 넘어져 바닥에 눕고 가라앉는다 → 기상: 오브젝트가 골반 아래로 옮겨지고 래그돌 자세에서 클립으로 섞여 돌아온다
 */
SW_TEST_CASE( RagdollTest, FatalHitRagdollsSettlesAndGetsUp )
{
    SW_ASSERT_TRUE( ResourceUtil::initialize() );
    GameObjectManager manager;
    TestRagdollInternal::spawnFloor( manager );
    RagdollComponent* pRagdoll = TestRagdollInternal::spawnSkeleton( manager, "Victim", float3{ 0.0f, 0.0f, 0.0f }, true );
    SW_ASSERT_NOT_NULL( pRagdoll );
    pRagdoll->setGetUpClips( hashed_string( "Lie_StandUp" ), hashed_string{}, hashed_string( "Idle" ) );
    manager.beginPlay();
    TestRagdollInternal::tickFor( manager, 3 );
    const int32 chest = TestRagdollInternal::findBody( *pRagdoll, "chest" );
    SW_ASSERT_TRUE( chest >= 0 );
    const float32 standingChestY = TestRagdollInternal::getBodyPosition( manager, *pRagdoll, chest )._y;

    HitInfo hit;
    hit._body      = pRagdoll->getRagdoll()._listBody[static_cast<size_t>( chest )];
    hit._direction = float3{ 0.0f, 0.0f, 1.0f };
    hit._impulse   = 300.0f;
    hit._bFatal    = true;
    CharacterHitUtil::deliverHit( *pRagdoll->getOwner(), hit );
    SW_EXPECT_TRUE( pRagdoll->getState() == RagdollState::Ragdoll );
    TestRagdollInternal::tickFor( manager, 240 );
    SW_EXPECT_TRUE( pRagdoll->isBodyDynamic( static_cast<uint32>( chest ) ) );
    const float3 lyingChest = TestRagdollInternal::getBodyPosition( manager, *pRagdoll, chest );
    SW_EXPECT_TRUE( lyingChest._y < standingChestY * 0.6f ); // 쓰러졌다
    SW_EXPECT_TRUE( lyingChest._y > 0.0f );                  // 바닥 위에 있다
    SW_EXPECT_TRUE( lyingChest._z > 0.1f );                  // 맞은 방향(+Z)으로 쓰러졌다
    SW_EXPECT_TRUE( pRagdoll->isSettled() );
    // 포즈가 물리를 따른다 — 가슴 뼈가 바디 자리에 있다.
    SW_EXPECT_TRUE( ( TestRagdollInternal::getBoneWorldPosition( *pRagdoll, "chest" ) - lyingChest ).getLength() < 0.35f );

    const int32  hips       = TestRagdollInternal::findBody( *pRagdoll, "hips" );
    const float3 lyingHips  = TestRagdollInternal::getBodyPosition( manager, *pRagdoll, hips );
    const float3 lyingPlane = float3{ lyingHips._x, 0.0f, lyingHips._z };
    SW_EXPECT_TRUE( pRagdoll->getUp() );
    SW_EXPECT_TRUE( pRagdoll->getState() == RagdollState::BlendingBack );
    // 오브젝트는 쓰러진 골반 아래로 옮겨진다(시작 자리 원점보다 골반에 훨씬 가깝다).
    const float3 root = pRagdoll->getOwner()->getPrimarySceneComponent()->getWorldPosition();
    SW_EXPECT_TRUE( lyingPlane.getLength() > 0.3f );
    SW_EXPECT_TRUE( ( float3{ root._x, 0.0f, root._z } - lyingPlane ).getLength() < lyingPlane.getLength() * 0.5f );
    TestRagdollInternal::tickFor( manager, 60 );
    SW_EXPECT_TRUE( pRagdoll->getState() == RagdollState::Animated );
    SW_EXPECT_FALSE( pRagdoll->isBodyDynamic( static_cast<uint32>( chest ) ) );
    manager.endPlay();
}

/**
 * @brief [RagdollTest] 맞음 반응 — 맞은 바디 아래만 잠깐 동적(가중치가 줄어들다 키네마틱으로 돌아온다), 부분 래그돌은 위 몸만 늘어진다
 */
SW_TEST_CASE( RagdollTest, HitReactionAndPartialRagdollAffectOnlyTheSubtree )
{
    SW_ASSERT_TRUE( ResourceUtil::initialize() );
    GameObjectManager manager;
    TestRagdollInternal::spawnFloor( manager );
    RagdollComponent* pRagdoll = TestRagdollInternal::spawnSkeleton( manager, "Dummy", float3{ 0.0f, 0.0f, 0.0f }, true );
    SW_ASSERT_NOT_NULL( pRagdoll );
    pRagdoll->setFlinchClip( hashed_string( "Hit_A" ) );
    manager.beginPlay();
    TestRagdollInternal::tickFor( manager, 3 );
    const int32 head     = TestRagdollInternal::findBody( *pRagdoll, "head" );
    const int32 chest    = TestRagdollInternal::findBody( *pRagdoll, "chest" );
    const int32 upperLeg = TestRagdollInternal::findBody( *pRagdoll, "upperleg.l" );
    SW_ASSERT_TRUE( head >= 0 && chest >= 0 && upperLeg >= 0 );

    HitInfo hit;
    hit._body      = pRagdoll->getRagdoll()._listBody[static_cast<size_t>( head )];
    hit._direction = float3{ 0.0f, 0.0f, 1.0f };
    hit._impulse   = 10.0f;
    hit._point     = TestRagdollInternal::getBodyPosition( manager, *pRagdoll, head );
    CharacterHitUtil::deliverHit( *pRagdoll->getOwner(), hit );
    TestRagdollInternal::tickFor( manager, 2 );
    SW_EXPECT_TRUE( pRagdoll->isBodyDynamic( static_cast<uint32>( head ) ) );
    SW_EXPECT_FALSE( pRagdoll->isBodyDynamic( static_cast<uint32>( chest ) ) );
    SW_EXPECT_TRUE( pRagdoll->getBodyWeight( static_cast<uint32>( head ) ) > 0.0f );
    TestRagdollInternal::tickFor( manager, 40 );
    SW_EXPECT_FALSE( pRagdoll->isBodyDynamic( static_cast<uint32>( head ) ) );
    SW_EXPECT_NEAR_EQUAL( 0.0f, pRagdoll->getBodyWeight( static_cast<uint32>( head ) ), 1e-6f );

    pRagdoll->startPartialRagdoll( 1.0f );
    TestRagdollInternal::tickFor( manager, 60 );
    SW_EXPECT_TRUE( pRagdoll->getState() == RagdollState::Partial );
    SW_EXPECT_TRUE( pRagdoll->isBodyDynamic( static_cast<uint32>( chest ) ) );
    SW_EXPECT_FALSE( pRagdoll->isBodyDynamic( static_cast<uint32>( upperLeg ) ) );
    pRagdoll->stopPartialRagdoll();
    TestRagdollInternal::tickFor( manager, 60 );
    SW_EXPECT_TRUE( pRagdoll->getState() == RagdollState::Animated );
    SW_EXPECT_FALSE( pRagdoll->isBodyDynamic( static_cast<uint32>( chest ) ) );
    manager.endPlay();
}

/**
 * @brief [RagdollTest] 물리 에셋 파일을 고쳐 다시 읽으면(에셋 캐시 reload) 래그돌이 새 바디로 다시 선다
 */
SW_TEST_CASE( RagdollTest, PhysicsAssetHotReloadRebuildsBodies )
{
    SW_ASSERT_TRUE( ResourceUtil::initialize() );
    const string path = test::makeTempPath( "dummy.physics.xml" );
    const utf8*  pTwoBodies =
        R"(<PhysicsAsset><_listBody>
            <PhysicsAssetBodyDef _bone="hips"><_listShape><PhysicsShapeDesc3D _type="Sphere" _radius="0.2" /></_listShape></PhysicsAssetBodyDef>
            <PhysicsAssetBodyDef _bone="head"><_listShape><PhysicsShapeDesc3D _type="Sphere" _radius="0.3" /></_listShape></PhysicsAssetBodyDef>
        </_listBody></PhysicsAsset>)";
    const utf8* pThreeBodies =
        R"(<PhysicsAsset><_listBody>
            <PhysicsAssetBodyDef _bone="hips"><_listShape><PhysicsShapeDesc3D _type="Sphere" _radius="0.2" /></_listShape></PhysicsAssetBodyDef>
            <PhysicsAssetBodyDef _bone="chest"><_listShape><PhysicsShapeDesc3D _type="Sphere" _radius="0.25" /></_listShape></PhysicsAssetBodyDef>
            <PhysicsAssetBodyDef _bone="head"><_listShape><PhysicsShapeDesc3D _type="Sphere" _radius="0.3" /></_listShape></PhysicsAssetBodyDef>
        </_listBody></PhysicsAsset>)";
    SW_ASSERT_TRUE( FileUtil::writeTextFile( path, pTwoBodies ) );

    GameObjectManager manager;
    RagdollComponent* pRagdoll = TestRagdollInternal::spawnSkeleton( manager, "Reloaded", float3{}, false, path.c_str() );
    SW_ASSERT_NOT_NULL( pRagdoll );
    manager.beginPlay();
    TestRagdollInternal::tickFor( manager, 2 );
    SW_EXPECT_EQUAL( size_t( 2 ), pRagdoll->getRagdoll()._listBody.size() );

    SW_ASSERT_TRUE( FileUtil::writeTextFile( path, pThreeBodies ) );
    PhysicsAssetCache cache;
    SW_ASSERT_TRUE( cache.isCached( path ) );
    cache.reload( path, nullptr );
    TestRagdollInternal::tickFor( manager, 2 );
    SW_EXPECT_EQUAL( size_t( 3 ), pRagdoll->getRagdoll()._listBody.size() );
    manager.endPlay();
}
