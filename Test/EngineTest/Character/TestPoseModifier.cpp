#include "pch.h"

#include "Core/File/FileUtil.h"
#include "Core/Math/MathUtil.h"

#include "Engine/Animation/AnimationAssetCache.h"
#include "Engine/Animation/Rig/RigAsset.h"
#include "Engine/Animation/Skeletal/Skeleton.h"
#include "Engine/Character/PoseModifier/PoseModifierComponent.h"
#include "Engine/Character/Socket/SocketSet.h"
#include "Engine/Graphics/Mesh/Mesh.h"
#include "Engine/Graphics/Mesh/MeshUtil.h"
#include "Engine/Object/Animation/AnimationSystem.h"
#include "Engine/Object/Component/3D/SkeletalMeshComponent.h"
#include "Engine/Object/Component/Physics/RigidBodyComponent.h"
#include "Engine/Object/Component/SceneComponent.h"
#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManager.h"
#include "Engine/Resource/ResourceUtil.h"

#include "EngineTest/AnimationTestUtil.h"

#include "TestFramework/TestFramework.h"

using namespace sw;

// PoseModifierTest — 후처리 리그 컴포넌트가 AnimationSystem 의 PostProcess 단계에서 돌고(기본 포즈 뒤 · 스킨 팔레트 앞), 다른 유닛 대상이 의존을 걸고
// (고리는 오류), `space` 대상이 의존 없이 본을 따르고, 소켓 대상이 풀리고, 씬 물리로 발을 디딘다.

namespace
{
    struct TestPoseModifierInternal
    {
        /** @brief 기본 포즈 단계에서 루트를 Y 축으로 돌리는 일입니다(애니메이터 대신). */
        class RootTurnTask final : public IAnimationPhaseTask
        {
        public:
            bool isAnimationActive() const override { return true; }
            void runAnimationPhase( AnimationPhase phase, SkeletalMeshComponent& unit, const AnimationFrameContext& context ) override
            {
                (void)context;
                if ( phase != AnimationPhase::BasePose )
                    return;
                BoneTransform root = unit.getLocalPose().getBoneTransform( 0 );
                root._rotation     = quaternion::createFromAxisAngle( float3::UnitY, _angle );
                unit.getLocalPose().setBoneTransform( 0, root );
            }
            float32 _angle{ 0.0f };
        };

        static SkeletalMeshComponent* createUnit( GameObjectManager& manager, const utf8* pName, const float3& position )
        {
            GameObject* pObject = manager.createGameObject( hashed_string( pName ) );
            if ( pObject == nullptr )
                return nullptr;
            SkeletalMeshComponent* pUnit = pObject->addComponent<SkeletalMeshComponent>();
            if ( pUnit == nullptr )
                return nullptr;
            pUnit->setSkeleton( make_shared<Skeleton>( test::makeChainSkeleton( 3 ) ) );
            pUnit->setLocalPosition( position );
            return pUnit;
        }

        static GameObject* createMarker( GameObjectManager& manager, const utf8* pName, const float3& position )
        {
            GameObject*     pObject = manager.createGameObject( hashed_string( pName ) );
            SceneComponent* pScene  = ( pObject != nullptr ) ? pObject->addComponent<SceneComponent>() : nullptr;
            if ( pScene != nullptr )
                pScene->setLocalPosition( position );
            return pObject;
        }

        static PoseModifierComponent* addRig( SkeletalMeshComponent& unit, string_view json )
        {
            shared_ptr<RigAsset> asset = make_shared<RigAsset>();
            if ( asset->parseJSON( json, "test.rig.json" ) == false )
                return nullptr;
            PoseModifierComponent* pModifier = unit.getOwner()->addComponent<PoseModifierComponent>();
            if ( pModifier == nullptr )
                return nullptr;
            pModifier->setRigAsset( asset );
            return pModifier;
        }

        static float3 getBoneModel( const SkeletalMeshComponent& unit, uint32 bone ) { return unit.getModelSpaceTransforms()[bone].getTranslation(); }
        static bool   isNear( const float3& expected, const float3& actual, float32 tolerance ) { return ( expected - actual ).getLength() <= tolerance; }
    };
} // namespace

/**
 * @brief [PoseModifierTest] 리그는 기본 포즈 뒤 · 스킨 팔레트 앞에 돈다 — 본이 오브젝트 대상(월드 → 유닛 모델 공간)에 서고, 팔레트도 그 포즈다. 끄면 비용이 없다
 */
SW_TEST_CASE( PoseModifierTest, RunsInPostProcessBeforeSkinning )
{
    GameObjectManager      manager;
    SkeletalMeshComponent* pUnit = TestPoseModifierInternal::createUnit( manager, "Hero", float3{ 5.0f, 0.0f, 0.0f } );
    SW_ASSERT_NOT_NULL( pUnit );
    TestPoseModifierInternal::RootTurnTask turn;
    turn._angle = 0.5f;
    pUnit->addAnimationPhaseTask( &turn );
    GameObject* pBall = TestPoseModifierInternal::createMarker( manager, "Ball", float3{ 5.0f, 1.5f, 1.0f } );
    SW_ASSERT_NOT_NULL( pBall );
    PoseModifierComponent* pModifier = TestPoseModifierInternal::addRig(
        *pUnit, R"({ "targets": [ { "name": "Ball", "object": "Ball" } ], "nodes": [ { "type": "Position", "name": "Reach", "bone": "bone2", "target": "Ball" } ] })" );
    SW_ASSERT_NOT_NULL( pModifier );
    pModifier->dispatchBeginPlay();
    SW_EXPECT_TRUE( pModifier->isRigReady() );
    SW_EXPECT_TRUE( pModifier->isTargetBound( "Ball" ) );

    manager.getAnimationSystem().evaluate( 1.0f / 60.0f );
    SW_EXPECT_TRUE( TestPoseModifierInternal::isNear( float3{ 0.0f, 1.5f, 1.0f }, TestPoseModifierInternal::getBoneModel( *pUnit, 2 ), 1e-4f ) );
    // 팔레트 = 역 바인드 × 모델 — 리그가 옮긴 본의 팔레트 이동 칸이 레퍼런스(0, 2, 0) 에서 대상까지의 차이다.
    const float4x4& palette = pUnit->getSkinPalette()[2];
    SW_EXPECT_TRUE( TestPoseModifierInternal::isNear( float3{ 0.0f, -0.5f, 1.0f }, palette.getTranslation(), 1e-4f ) );
    // 기본 포즈(루트 회전)는 리그 앞에 있었다 — 리그가 덮지 않은 본 1 은 돌아간 자리다.
    SW_EXPECT_TRUE( MathUtil::abs( pUnit->getLocalPose().getBoneTransform( 0 )._rotation._y ) > 0.1f );

    pUnit->removeAnimationPhaseTask( &turn );
    pModifier->setRigEnabled( false );
    manager.getAnimationSystem().evaluate( 1.0f / 60.0f ); // 끈 프레임 — 리그 없는 포즈를 한 번 다시 만든다
    SW_EXPECT_TRUE( TestPoseModifierInternal::isNear( float3{ 0.0f, 2.0f, 0.0f }, TestPoseModifierInternal::getBoneModel( *pUnit, 2 ), 1e-4f ) );
    manager.getAnimationSystem().evaluate( 1.0f / 60.0f );
    SW_EXPECT_EQUAL( 0u, manager.getAnimationSystem().getActiveUnitCount() );
}

/**
 * @brief [PoseModifierTest] 다른 유닛의 본을 대상으로 하면 그 유닛이 먼저 평가되도록 의존이 걸리고 같은 프레임 값을 읽는다. 고리가 생기는 대상은 오류로 꺼진다
 */
SW_TEST_CASE( PoseModifierTest, CrossUnitTargetOrdersUnitsAndRejectsCycle )
{
    GameObjectManager      manager;
    SkeletalMeshComponent* pProp = TestPoseModifierInternal::createUnit( manager, "Prop", float3{ 0.0f, 0.0f, 3.0f } );
    SkeletalMeshComponent* pBody = TestPoseModifierInternal::createUnit( manager, "Body", float3{ 0.0f, 0.0f, 0.0f } );
    SW_ASSERT_TRUE( pProp != nullptr && pBody != nullptr );
    TestPoseModifierInternal::RootTurnTask turn;
    pBody->addAnimationPhaseTask( &turn );

    PoseModifierComponent* pFollow = TestPoseModifierInternal::addRig( *pProp, R"({ "targets": [ { "name": "Hand", "bone": "bone2", "unit": "Body" } ],
        "nodes": [ { "type": "CopyTransform", "name": "Follow", "bone": "bone0", "target": "Hand" } ] })" );
    SW_ASSERT_NOT_NULL( pFollow );
    pFollow->dispatchBeginPlay();
    SW_ASSERT_EQUAL( 1u, static_cast<uint32>( pProp->getAnimationDependencies().size() ) );

    AnimationSystem& system = manager.getAnimationSystem();
    for ( uint32 frame = 0; frame < 3; ++frame )
    {
        turn._angle = 0.4f * static_cast<float32>( frame + 1 ); // 몸이 프레임마다 다르게 돈다 — 한 프레임 늦으면 어긋난다
        system.evaluate( 1.0f / 60.0f );
        const float3 handWorld = float3::transform( TestPoseModifierInternal::getBoneModel( *pBody, 2 ), pBody->getWorldMatrix() );
        const float3 propRoot  = float3::transform( TestPoseModifierInternal::getBoneModel( *pProp, 0 ), pProp->getWorldMatrix() );
        SW_EXPECT_TRUE( TestPoseModifierInternal::isNear( handWorld, propRoot, 1e-4f ) );
    }
    SW_EXPECT_FALSE( system.hasDependencyCycle() );
    SW_ASSERT_EQUAL( 2u, static_cast<uint32>( system.getLevels().size() ) );
    SW_EXPECT_TRUE( system.getLevels()[0][0] == pBody );

    // 몸이 소품을 대상으로 하면 고리다 — 대상은 꺼지고, 시스템은 고리 없이 돈다.
    {
        test::ScopedDefensiveTestLog expected( "a rig target that would create a dependency cycle is rejected" );
        PoseModifierComponent*       pBack = TestPoseModifierInternal::addRig( *pBody, R"({ "targets": [ { "name": "Prop", "bone": "bone0", "unit": "Prop" } ],
            "nodes": [ { "type": "Position", "name": "Back", "bone": "bone1", "target": "Prop" } ] })" );
        SW_ASSERT_NOT_NULL( pBack );
        pBack->dispatchBeginPlay();
        SW_EXPECT_FALSE( pBack->isTargetBound( "Prop" ) );
        SW_EXPECT_TRUE( pBody->getAnimationDependencies().empty() );
    }
    system.evaluate( 1.0f / 60.0f );
    SW_EXPECT_FALSE( system.hasDependencyCycle() );
    pBody->removeAnimationPhaseTask( &turn );
}

/**
 * @brief [PoseModifierTest] `space` 대상 — 손에 붙은 오브젝트(무기 손잡이)를 의존 없이 따르고, 손이 이번 프레임에 움직여도 늦지 않는다
 */
SW_TEST_CASE( PoseModifierTest, SpaceTargetRidesOwnBoneWithoutDependency )
{
    GameObjectManager      manager;
    SkeletalMeshComponent* pBody = TestPoseModifierInternal::createUnit( manager, "Body", float3{} );
    SW_ASSERT_NOT_NULL( pBody );
    TestPoseModifierInternal::RootTurnTask turn;
    pBody->addAnimationPhaseTask( &turn );
    GameObject* pGrip = TestPoseModifierInternal::createMarker( manager, "Grip", float3{} );
    SW_ASSERT_NOT_NULL( pGrip );
    PoseModifierComponent* pModifier = TestPoseModifierInternal::addRig( *pBody, R"({ "targets": [ { "name": "Grip", "object": "Grip", "space": "bone1" } ],
        "nodes": [ { "type": "Position", "name": "Hold", "bone": "bone2", "target": "Grip" } ] })" );
    SW_ASSERT_NOT_NULL( pModifier );
    pModifier->dispatchBeginPlay();
    SW_EXPECT_TRUE( pBody->getAnimationDependencies().empty() );

    // 게임 코드가 손잡이를 "지난 프레임 본 1 기준 (0.3, 0.5, 0)" 자리에 둔다 — 붙어 다니는 무기처럼.
    AnimationSystem& system = manager.getAnimationSystem();
    const float3     gripInBone{ 0.3f, 0.5f, 0.0f };
    for ( uint32 frame = 0; frame < 4; ++frame )
    {
        const float4x4 lastBone = pBody->getModelSpaceTransforms()[1] * pBody->getWorldMatrix();
        pGrip->getPrimarySceneComponent()->setLocalPosition( float3::transform( gripInBone, lastBone ) );
        turn._angle = 0.6f * static_cast<float32>( frame );
        system.evaluate( 1.0f / 60.0f );
        // 이번 프레임의 본 1 기준으로도 같은 자리 — 한 프레임 늦은 손 자리가 아니다.
        const float3 expected = float3::transform( gripInBone, pBody->getModelSpaceTransforms()[1] );
        if ( frame > 0 )
            SW_EXPECT_TRUE( TestPoseModifierInternal::isNear( expected, TestPoseModifierInternal::getBoneModel( *pBody, 2 ), 1e-4f ) );
    }
    pBody->removeAnimationPhaseTask( &turn );
}

/**
 * @brief [PoseModifierTest] 소켓 대상 — 자기 유닛의 소켓 에셋과, bindUnit 으로 건 다른 유닛의 소켓이 (부모 본 + 소켓 로컬)로 풀린다
 */
SW_TEST_CASE( PoseModifierTest, SocketTargetsResolveOnOwnAndOtherUnits )
{
    GameObjectManager      manager;
    SkeletalMeshComponent* pBody   = TestPoseModifierInternal::createUnit( manager, "Body", float3{} );
    SkeletalMeshComponent* pWeapon = TestPoseModifierInternal::createUnit( manager, "Weapon", float3{ 2.0f, 0.0f, 0.0f } );
    SW_ASSERT_TRUE( pBody != nullptr && pWeapon != nullptr );
    SocketKindTable kinds;
    kinds.addKind( "Attach" );
    shared_ptr<SocketSet> bodySockets   = make_shared<SocketSet>();
    shared_ptr<SocketSet> weaponSockets = make_shared<SocketSet>();
    SW_ASSERT_TRUE( bodySockets->loadFromXMLText( R"(<SocketSet><Socket name="Chest" parent="bone1" kind="Attach" translation="0 0 0.2"/></SocketSet>)", "body", kinds ) );
    SW_ASSERT_TRUE( weaponSockets->loadFromXMLText( R"(<SocketSet><Socket name="Grip" parent="bone2" kind="Attach" translation="0.1 0 0"/></SocketSet>)", "weapon", kinds ) );

    PoseModifierComponent* pModifier = TestPoseModifierInternal::addRig( *pBody, R"({ "targets": [ { "name": "Chest", "socket": "Chest" },
        { "name": "Grip", "socket": "Grip", "unit": "Weapon" } ], "nodes": [
        { "type": "Position", "name": "ToChest", "bone": "bone0", "target": "Chest" },
        { "type": "Position", "name": "ToGrip", "bone": "bone2", "target": "Grip" } ] })" );
    SW_ASSERT_NOT_NULL( pModifier );
    pModifier->setOwnSockets( bodySockets );
    pModifier->bindUnit( "Weapon", pWeapon, weaponSockets );
    pModifier->dispatchBeginPlay();
    SW_ASSERT_TRUE( pModifier->isRigReady() );
    SW_EXPECT_TRUE( pModifier->isTargetBound( "Grip" ) );

    manager.getAnimationSystem().evaluate( 1.0f / 60.0f );
    // 뿌리가 가슴 소켓(노드가 돌 때의 본 1 (0, 1, 0) + 소켓 로컬 (0, 0, 0.2))으로 갔다.
    SW_EXPECT_TRUE( TestPoseModifierInternal::isNear( float3{ 0.0f, 1.0f, 0.2f }, TestPoseModifierInternal::getBoneModel( *pBody, 0 ), 1e-4f ) );
    // 무기 손잡이 = 무기 유닛 본 2 (월드 2, 2, 0) + (0.1, 0, 0) — 몸 유닛 모델 공간 그대로(몸은 원점).
    SW_EXPECT_TRUE( TestPoseModifierInternal::isNear( float3{ 2.1f, 2.0f, 0.0f }, TestPoseModifierInternal::getBoneModel( *pBody, 2 ), 1e-4f ) );
}

/**
 * @brief [PoseModifierTest] 발 디딤은 씬 물리(정적 기울기 상자)로 땅을 찾는다 — 낮은 쪽으로 골반이 내려가고 두 발이 서로 다른 높이에 선다
 */
SW_TEST_CASE( PoseModifierTest, FootPlacementUsesScenePhysics )
{
    GameObjectManager manager;
    GameObject*       pGround = manager.createGameObject( hashed_string( "Slope" ) );
    SW_ASSERT_NOT_NULL( pGround );
    RigidBodyComponent* pBody = pGround->addComponent<RigidBodyComponent>();
    SW_ASSERT_NOT_NULL( pBody );
    PhysicsShapeDesc3D box;
    box._halfExtents = float3{ 4.0f, 0.5f, 4.0f };
    pBody->setShape( box );
    pBody->setBodyType( PhysicsBodyType::Static );
    pBody->setLocalPosition( float3{ 0.0f, -0.5f, 0.0f } );
    pBody->setLocalRotation( float3{ 0.0f, 0.0f, 0.25f } ); // Z 축 둘레로 기운 면
    pBody->dispatchBeginPlay();

    GameObject* pObject = manager.createGameObject( hashed_string( "Walker" ) );
    SW_ASSERT_NOT_NULL( pObject );
    SkeletalMeshComponent* pUnit    = pObject->addComponent<SkeletalMeshComponent>();
    Skeleton               skeleton = Skeleton{};
    (void)skeleton.addBone( "root", -1, test::makeBoneTransform( float3{} ), float4x4::Identity );
    (void)skeleton.addBone( "hips", 0, test::makeBoneTransform( float3{ 0.0f, 1.0f, 0.0f } ), float4x4::Identity );
    (void)skeleton.addBone( "thigh.l", 1, test::makeBoneTransform( float3{ -0.2f, 0.0f, 0.0f } ), float4x4::Identity );
    (void)skeleton.addBone( "shin.l", 2, test::makeBoneTransform( float3{ 0.0f, -0.45f, 0.03f } ), float4x4::Identity );
    (void)skeleton.addBone( "foot.l", 3, test::makeBoneTransform( float3{ 0.0f, -0.45f, -0.03f } ), float4x4::Identity );
    (void)skeleton.addBone( "thigh.r", 1, test::makeBoneTransform( float3{ 0.2f, 0.0f, 0.0f } ), float4x4::Identity );
    (void)skeleton.addBone( "shin.r", 5, test::makeBoneTransform( float3{ 0.0f, -0.45f, 0.03f } ), float4x4::Identity );
    (void)skeleton.addBone( "foot.r", 6, test::makeBoneTransform( float3{ 0.0f, -0.45f, -0.03f } ), float4x4::Identity );
    skeleton.computeInverseBindFromReference();
    pUnit->setSkeleton( make_shared<Skeleton>( skeleton ) );
    PoseModifierComponent* pModifier = TestPoseModifierInternal::addRig( *pUnit, R"({ "nodes": [ { "type": "FootPlacement", "name": "Feet", "pelvis": "hips",
        "interp_speed": 0, "feet": [ { "root": "thigh.l", "mid": "shin.l", "end": "foot.l" }, { "root": "thigh.r", "mid": "shin.r", "end": "foot.r" } ] } ] })" );
    SW_ASSERT_NOT_NULL( pModifier );
    pModifier->dispatchBeginPlay();

    for ( uint32 frame = 0; frame < 4; ++frame )
    {
        manager.tick( 1.0f / 60.0f );
    }
    // 면: 상자 윗면 가운데와 법선을 같은 회전(오일러 → 쿼터니언)으로 구해 발 X · Z 의 높이를 잰다.
    const quaternion tilt        = quaternion::createFromYawPitchRoll( float3{ 0.0f, 0.0f, 0.25f } );
    const float3     normal      = float3::transform( float3::UnitY, tilt );
    const float3     top         = float3{ 0.0f, -0.5f, 0.0f } + float3::transform( float3{ 0.0f, 0.5f, 0.0f }, tilt );
    const float3     leftFoot    = TestPoseModifierInternal::getBoneModel( *pUnit, 4 );
    const float3     rightFoot   = TestPoseModifierInternal::getBoneModel( *pUnit, 7 );
    const float32    leftGround  = top._y - ( normal._x * ( leftFoot._x - top._x ) + normal._z * ( leftFoot._z - top._z ) ) / normal._y;
    const float32    rightGround = top._y - ( normal._x * ( rightFoot._x - top._x ) + normal._z * ( rightFoot._z - top._z ) ) / normal._y;
    // 발 높이 = 레퍼런스 발 높이(0.1) + 그 자리의 땅 높이.
    SW_EXPECT_NEAR_EQUAL( 0.1f + leftGround, leftFoot._y, 0.01f );
    SW_EXPECT_NEAR_EQUAL( 0.1f + rightGround, rightFoot._y, 0.01f );
    SW_EXPECT_TRUE( MathUtil::abs( rightFoot._y - leftFoot._y ) > 0.05f );
    SW_EXPECT_TRUE( TestPoseModifierInternal::getBoneModel( *pUnit, 1 )._y < 0.99f ); // 골반이 내려갔다
}

/**
 * @brief [PoseModifierTest] 리그 핫 리로드 — 캐시가 파일을 제자리로 다시 읽으면 내용 번호가 바뀌고, 컴포넌트가 다음 프레임에 다시 묶어 새 노드로 돈다
 */
SW_TEST_CASE( PoseModifierTest, HotReloadRebindsRig )
{
    SW_ASSERT_TRUE( ResourceUtil::initialize() );
    const string      path = FileUtil::joinPath( test::makeTempPath( "rigreload" ), "reach.rig.json" );
    const string_view firstJSON =
        R"({ "targets": [ { "name": "A", "object": "A" }, { "name": "B", "object": "B" } ], "nodes": [ { "type": "Position", "name": "Reach", "bone": "bone2", "target": "A" } ] })";
    const string_view secondJSON =
        R"({ "targets": [ { "name": "A", "object": "A" }, { "name": "B", "object": "B" } ], "nodes": [ { "type": "Position", "name": "Reach", "bone": "bone2", "target": "B" } ] })";
    FileUtil::ensureParentDirectoryExists( path );
    SW_ASSERT_TRUE( FileUtil::writeTextFile( path, firstJSON ) );

    GameObjectManager      manager;
    SkeletalMeshComponent* pUnit = TestPoseModifierInternal::createUnit( manager, "Hero", float3{} );
    SW_ASSERT_NOT_NULL( pUnit );
    SW_ASSERT_NOT_NULL( TestPoseModifierInternal::createMarker( manager, "A", float3{ 1.0f, 0.0f, 0.0f } ) );
    SW_ASSERT_NOT_NULL( TestPoseModifierInternal::createMarker( manager, "B", float3{ -1.0f, 0.0f, 0.0f } ) );
    PoseModifierComponent* pModifier = pUnit->getOwner()->addComponent<PoseModifierComponent>();
    SW_ASSERT_NOT_NULL( pModifier );
    pModifier->setRigPath( path );
    pModifier->dispatchBeginPlay();
    manager.getAnimationSystem().evaluate( 1.0f / 60.0f );
    SW_EXPECT_TRUE( TestPoseModifierInternal::isNear( float3{ 1.0f, 0.0f, 0.0f }, TestPoseModifierInternal::getBoneModel( *pUnit, 2 ), 1e-4f ) );

    SW_ASSERT_TRUE( FileUtil::writeTextFile( path, secondJSON ) );
    SW_ASSERT_TRUE( RigAssetCache::reloadShared( path ) );
    manager.getAnimationSystem().evaluate( 1.0f / 60.0f );
    SW_EXPECT_TRUE( TestPoseModifierInternal::isNear( float3{ -1.0f, 0.0f, 0.0f }, TestPoseModifierInternal::getBoneModel( *pUnit, 2 ), 1e-4f ) );
}

/**
 * @brief [PoseModifierTest] 물리 에셋의 구 · 캡슐이 스프링 공유 충돌체가 된다 — 머리 구(견본 `chain.physics.xml`)가 매달린 꼬리를 밀어낸다(없으면 꼬리가 머리를 지난다)
 */
SW_TEST_CASE( PoseModifierTest, PhysicsAssetShapesBecomeSpringColliders )
{
    SW_ASSERT_TRUE( ResourceUtil::initialize() );
    Skeleton skeleton;
    (void)skeleton.addBone( "pelvis", -1, test::makeBoneTransform( float3{} ), float4x4::Identity );
    (void)skeleton.addBone( "spine", 0, test::makeBoneTransform( float3{ 0.0f, 0.5f, 0.0f } ), float4x4::Identity );
    (void)skeleton.addBone( "head", 1, test::makeBoneTransform( float3{ 0.0f, 0.5f, 0.0f } ), float4x4::Identity );
    (void)skeleton.addBone( "tail0", 2, test::makeBoneTransform( float3{ 0.0f, 0.5f, 0.0f } ), float4x4::Identity );
    (void)skeleton.addBone( "tail1", 3, test::makeBoneTransform( float3{ 0.4f, 0.0f, 0.0f } ), float4x4::Identity );
    skeleton.computeInverseBindFromReference();
    const string_view json = R"({ "nodes": [ { "type": "SpringChain", "name": "Tail", "bones": ["tail0", "tail1"], "stiffness": 0, "damping": 0.2,
        "use_shared_colliders": true } ] })";

    float32 arrClearance[2]{};
    for ( uint32 bShared = 0; bShared < 2; ++bShared )
    {
        GameObjectManager manager;
        GameObject*       pObject = manager.createGameObject( "Creature" );
        SW_ASSERT_NOT_NULL( pObject );
        SkeletalMeshComponent* pUnit = pObject->addComponent<SkeletalMeshComponent>();
        SW_ASSERT_NOT_NULL( pUnit );
        pUnit->setSkeleton( make_shared<Skeleton>( skeleton ) );
        PoseModifierComponent* pModifier = TestPoseModifierInternal::addRig( *pUnit, json );
        SW_ASSERT_NOT_NULL( pModifier );
        if ( bShared == 1 )
            pModifier->setPhysicsAssetPath( "engine/physics/samples/chain.physics.xml" );
        pModifier->dispatchBeginPlay();
        SW_EXPECT_EQUAL( bShared == 1 ? 3u : 0u, static_cast<uint32>( pModifier->getRigInstance().getSharedColliders().size() ) );
        for ( uint32 frame = 0; frame < 120; ++frame )
        {
            manager.getAnimationSystem().evaluate( 1.0f / 60.0f );
        }
        // 머리 구: 머리 본(y 1.0) + (0, 0.15, 0), 반지름 0.15. 꼬리 끝(본 4)은 꼬리 뿌리(y 1.5)에서 0.4 아래로 매달려 그 안을 지난다.
        arrClearance[bShared] = ( TestPoseModifierInternal::getBoneModel( *pUnit, 4 ) - float3{ 0.0f, 1.15f, 0.0f } ).getLength();
    }
    SW_EXPECT_TRUE( arrClearance[0] < 0.15f );
    SW_EXPECT_TRUE( arrClearance[1] >= 0.15f + 0.02f - 1e-3f );
}

/**
 * @brief [PoseModifierTest] 포즈 구동(RBF)의 보정 모프가 그리는 메시의 같은 이름 모프 타깃 가중치가 된다 — 굽힘 포즈면 1, 쉬는 포즈면 0
 * @details 유닛의 메시는 모프 타깃 `ElbowBend` 를 가진 스킨드 큐브다. 가중치는 기본 포즈 단계가 비우고 후처리(리그)가 더하므로, 굽힘을 풀면 다음 프레임에 0 이다.
 */
SW_TEST_CASE( PoseModifierTest, PoseDriverCorrectiveMorphDrivesMeshMorphWeight )
{
    GameObjectManager      manager;
    SkeletalMeshComponent* pUnit = TestPoseModifierInternal::createUnit( manager, "Arm", float3{} );
    SW_ASSERT_NOT_NULL( pUnit );
    shared_ptr<Mesh>       cube = MeshUtil::createUnitCube();
    vector<MeshSkinVertex> listSkin( cube->getVertices().size(), MeshSkinVertex{} );
    MeshMorphTarget        bend{};
    bend._name = hashed_string( "ElbowBend" );
    MeshMorphDelta delta{};
    delta._vertexIndex = 0;
    delta._position    = float3{ 0.0f, 0.1f, 0.0f };
    bend._listDelta.push_back( delta );
    shared_ptr<Mesh> mesh = Mesh::create();
    mesh->setVertices( cube->getVertices() );
    mesh->setSkin( listSkin, 3 );
    mesh->setMorphTargets( { bend } );
    pUnit->setMesh( mesh );
    pUnit->resolveRenderAssets();
    SW_ASSERT_EQUAL( 0, pUnit->findMorphTargetIndex( hashed_string( "ElbowBend" ) ) );

    /** @brief 기본 포즈 단계에서 bone1 을 피치로 굽힌다. */
    class ElbowTask final : public IAnimationPhaseTask
    {
    public:
        bool isAnimationActive() const override { return true; }
        void runAnimationPhase( AnimationPhase phase, SkeletalMeshComponent& unit, const AnimationFrameContext& context ) override
        {
            (void)context;
            if ( phase != AnimationPhase::BasePose )
                return;
            BoneTransform elbow = unit.getLocalPose().getBoneTransform( 1 );
            elbow._rotation     = quaternion::createFromYawPitchRoll( 0.0f, _degrees * MathUtil::kDegreeToRadian, 0.0f );
            unit.getLocalPose().setBoneTransform( 1, elbow );
        }
        float32 _degrees{ 90.0f };
    };
    ElbowTask elbow;
    pUnit->addAnimationPhaseTask( &elbow );
    PoseModifierComponent* pModifier = TestPoseModifierInternal::addRig( *pUnit, R"({ "nodes": [ { "type": "PoseDriver", "name": "Elbow", "driver": "bone1",
        "radius_degrees": 60, "poses": [ { "name": "Rest", "rotation": [0, 0, 0] },
        { "name": "Bent", "rotation": [90, 0, 0], "morphs": [ { "morph": "ElbowBend", "weight": 1 } ] } ] } ] })" );
    SW_ASSERT_NOT_NULL( pModifier );
    pModifier->dispatchBeginPlay();

    manager.getAnimationSystem().evaluate( 1.0f / 60.0f );
    SW_ASSERT_EQUAL( size_t( 1 ), pUnit->getMorphWeights().size() );
    SW_EXPECT_NEAR_EQUAL( 1.0f, pUnit->getMorphWeights()[0], 1e-4f );

    elbow._degrees = 0.0f;
    pUnit->markPoseDirty();
    manager.getAnimationSystem().evaluate( 1.0f / 60.0f );
    SW_EXPECT_NEAR_EQUAL( 0.0f, pUnit->getMorphWeights()[0], 1e-4f );
    pUnit->removeAnimationPhaseTask( &elbow );
}
