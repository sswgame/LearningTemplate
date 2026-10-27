#include "pch.h"

#include "Engine/Animation/Facial/FacialRig.h"
#include "Engine/Animation/Retarget/RetargetProfile.h"
#include "Engine/Animation/Rig/RigAsset.h"
#include "Engine/Animation/Skeletal/Skeleton.h"
#include "Engine/Character/PoseModifier/PoseModifierComponent.h"
#include "Engine/Object/Component/3D/FacialAnimationComponent.h"
#include "Engine/Object/Component/3D/PoseRetargetComponent.h"
#include "Engine/Object/Component/3D/SkeletalAnimatorComponent.h"
#include "Engine/Object/Component/3D/SkeletalMeshComponent.h"
#include "Engine/Object/Component/SceneComponent.h"
#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManager.h"
#include "Engine/Object/SceneAnimation/AnimationCrowd.h"
#include "Engine/Object/SceneAnimation/AnimationSystem.h"
#include "Engine/Object/SceneAnimation/LocomotionWarpingComponent.h"
#include "Engine/Resource/ResourcePaths.h"

#include "EngineTest/AnimationCrowdTestUtil.h"

#include "GameFramework/Base/Gameplay/Appearance/CharacterAppearanceComponent.h"

#include "TestFramework/TestFramework.h"

using namespace sw;

// AnimationPoseAccessTest — 포즈 접근 선언(`IAnimationPhaseTask::getPoseAccess`)이 실제 쓰기와 어긋나지 않는다.
// 군중 공유는 포즈를 쓰는 작업이 하나뿐일 때만 유닛을 묶음에 넣는다. 어떤 때만 쓰는 작업(후처리 리그 · 방향 보정 · 리타깃 · 얼굴 · 체형 비율)이 쉬는 동안에도
// `Writes` 로 선언하면 쓰지도 않는 포즈 때문에 묶음을 영영 나누지 못한다(Shooter3D 래그돌 사건). 작업마다 "쉬면 묶음에 들고, 쓰면 혼자가 되고, 다시 쉬면 돌아온다" 를 본다.
// 읽기만 한다고 선언한 작업이 포즈를 쓰면 Debug 단언(`SkeletalMeshComponent::runTasks`)이 잡는다 — 모든 케이스는 단언이 0 개인지도 확인한다.

namespace
{
    /** @brief 군중 공유 테스트의 공통 준비물입니다(스켈레톤 · 메시 · 클립 폴더). */
    struct TestAnimationPoseAccessInternal
    {
        shared_ptr<Skeleton> _skeleton{ test::makeCrowdSkeleton() };
        shared_ptr<Mesh>     _mesh{ test::makeCrowdSkinnedCube() };
        string               _folder{ test::writeCrowdWaveClip( *_skeleton ) };
    };

    /** @brief 읽기만 한다고 선언하고 후처리에서 본을 쓰는 거짓 작업입니다 — Debug 단언이 잡아야 한다. */
    class LyingReaderTask final : public IAnimationPhaseTask
    {
    public:
        bool                isAnimationActive() const override { return true; }
        AnimationPoseAccess getPoseAccess() const override { return AnimationPoseAccess::ReadsOnly; }
        void                runAnimationPhase( AnimationPhase phase, SkeletalMeshComponent& unit, const AnimationFrameContext& context ) override
        {
            (void)context;
            if ( phase != AnimationPhase::PostProcess )
                return;
            BoneTransform bone = unit.getLocalPose().getBoneTransform( 1 );
            bone._rotation     = quaternion::makeFromAxisAngle( float3::UnitZ, 0.9f );
            unit.getLocalPose().setBoneTransform( 1, bone );
        }
    };

    bool isShared( const SkeletalMeshComponent& unit )
    {
        return unit.getCrowdMode() == AnimationCrowdMode::Shared;
    }

    bool isSolo( const SkeletalMeshComponent& unit )
    {
        return unit.getCrowdMode() == AnimationCrowdMode::Solo;
    }
} // namespace

/**
 * @brief [AnimationPoseAccessTest] 후처리 리그는 평가할 노드가 있을 때만 포즈를 쓴다 — 가중치 슬롯이 0 이거나 리그를 끄면 군중 묶음에 들고, 슬롯이 열리면 혼자가 된다
 */
SW_TEST_CASE( AnimationPoseAccessTest, PoseModifierWritesOnlyWhileANodeCanRun )
{
    SW_ASSERT_TRUE( ResourcePaths::initialize() );
    const TestAnimationPoseAccessInternal data;
    SW_ASSERT_FALSE( data._folder.empty() );
    const test::ScopedAssertCapture assertCapture;

    GameObjectManager manager;
    AnimationSystem&  system = manager.getSubsystem<AnimationSystem>();
    system.setCrowdSettings( AnimationCrowdSettings{} );
    SkeletalMeshComponent* pUnit = test::createCrowdCharacter( manager, "Hero", data._skeleton, data._mesh, data._folder );
    SW_ASSERT_NOT_NULL( pUnit );
    GameObject* pBall = manager.createGameObject( hashed_string( "Ball" ) );
    SW_ASSERT_NOT_NULL( pBall );
    SW_ASSERT_NOT_NULL( pBall->addComponent<SceneComponent>() );

    shared_ptr<RigAsset> asset = make_shared<RigAsset>();
    SW_ASSERT_TRUE( asset->parseXML(
        R"(<RigAsset><targets><item name="Ball" object="Ball" /></targets><nodes><item type="Position" name="Reach" bone="bone1" target="Ball" weight_slot="Hit" /></nodes></RigAsset>)",
        "test.rig.xml" ) );
    PoseModifierComponent* pModifier = pUnit->getOwner()->addComponent<PoseModifierComponent>();
    SW_ASSERT_NOT_NULL( pModifier );
    pModifier->setRigAsset( asset );
    pModifier->dispatchBeginPlay();
    SW_ASSERT_TRUE( pModifier->isRigReady() );

    // 슬롯이 0 이면 어떤 노드도 돌지 않는다 — 리그는 읽기만 하는 작업이라 애니메이터가 유일한 쓰는 작업이다.
    pModifier->setSlotWeight( hashed_string( "Hit" ), 0.0f );
    system.evaluate( 1.0f / 60.0f );
    SW_EXPECT_TRUE( isShared( *pUnit ) );

    // 슬롯이 열리면 노드가 돈다 — 쓰는 작업이 둘이라 혼자 평가한다.
    pModifier->setSlotWeight( hashed_string( "Hit" ), 1.0f );
    system.evaluate( 1.0f / 60.0f );
    SW_EXPECT_TRUE( isSolo( *pUnit ) );

    // 다시 닫히면 묶음으로 돌아온다.
    pModifier->setSlotWeight( hashed_string( "Hit" ), 0.0f );
    system.evaluate( 1.0f / 60.0f );
    SW_EXPECT_TRUE( isShared( *pUnit ) );

    // 리그를 끄면 슬롯이 열려 있어도 쓰지 않는다.
    pModifier->setSlotWeight( hashed_string( "Hit" ), 1.0f );
    pModifier->setRigEnabled( false );
    system.evaluate( 1.0f / 60.0f );
    SW_EXPECT_TRUE( isShared( *pUnit ) );
    system.evaluate( 1.0f / 60.0f );
    SW_EXPECT_TRUE( isShared( *pUnit ) );
    SW_EXPECT_EQUAL( 0u, assertCapture.getCount() );
}

/**
 * @brief [AnimationPoseAccessTest] 방향 보정은 몸을 돌릴 때만 포즈를 쓴다 — 서 있는 동안은 묶음에 들고, 움직여 각이 생기면 혼자가 되며, 멈추면 돌아온다
 * @details 각은 마무리(`finishAnimationFrame`)에서 정해져 다음 프레임의 선언과 후처리가 같은 값을 읽는다. 움직인 첫 프레임에는 아직 각이 0 이라 묶음에 남는다.
 */
SW_TEST_CASE( AnimationPoseAccessTest, LocomotionWarpingWritesOnlyWhileTurning )
{
    SW_ASSERT_TRUE( ResourcePaths::initialize() );
    const TestAnimationPoseAccessInternal data;
    SW_ASSERT_FALSE( data._folder.empty() );
    const test::ScopedAssertCapture assertCapture;

    GameObjectManager manager;
    AnimationSystem&  system = manager.getSubsystem<AnimationSystem>();
    system.setCrowdSettings( AnimationCrowdSettings{} );
    SkeletalMeshComponent* pUnit = test::createCrowdCharacter( manager, "Runner", data._skeleton, data._mesh, data._folder );
    SW_ASSERT_NOT_NULL( pUnit );
    LocomotionWarpingComponent* pLocomotion = pUnit->getOwner()->addComponent<LocomotionWarpingComponent>();
    SW_ASSERT_NOT_NULL( pLocomotion );
    pLocomotion->setOrientationBones( {
        LocomotionOrientationBone{ hashed_string( "bone1" ), 1.0f }
    } );
    pLocomotion->dispatchBeginPlay();

    constexpr float32 kFrame = 1.0f / 60.0f;
    SceneComponent*   pRoot  = pUnit->getOwner()->getPrimarySceneComponent();
    SW_ASSERT_NOT_NULL( pRoot );
    system.evaluate( kFrame ); // 서 있다 — 각 0
    SW_EXPECT_TRUE( isShared( *pUnit ) );
    SW_EXPECT_NEAR_EQUAL( 0.0f, pLocomotion->getOrientationAngle(), 1e-6f );

    const float3 step{ 3.0f * kFrame, 0.0f, 0.0f }; // 몸 앞(+Z)과 직각으로 간다
    for ( uint32 frameIndex = 0; frameIndex < 2; ++frameIndex )
    {
        pRoot->setWorldPosition( pRoot->getWorldPosition() + step );
        system.evaluate( kFrame );
    }
    SW_EXPECT_TRUE( MathUtil::abs( pLocomotion->getOrientationAngle() ) > 0.1f );
    SW_EXPECT_TRUE( isSolo( *pUnit ) ); // 각이 생긴 다음 프레임부터 뼈를 돌린다

    // 멈추면 한 프레임 뒤 각이 0 이 되고, 그다음 프레임부터 다시 묶음이다.
    system.evaluate( kFrame );
    system.evaluate( kFrame );
    SW_EXPECT_NEAR_EQUAL( 0.0f, pLocomotion->getOrientationAngle(), 1e-6f );
    SW_EXPECT_TRUE( isShared( *pUnit ) );
    SW_EXPECT_EQUAL( 0u, assertCapture.getCount() );
}

/**
 * @brief [AnimationPoseAccessTest] 리타깃은 원본을 옮겨 쓸 때만 포즈를 쓴다 — 원본이 없으면 묶음에 들고, 원본을 걸면 혼자가 되며, 떼면 돌아온다
 */
SW_TEST_CASE( AnimationPoseAccessTest, PoseRetargetWritesOnlyWithASource )
{
    SW_ASSERT_TRUE( ResourcePaths::initialize() );
    const TestAnimationPoseAccessInternal data;
    SW_ASSERT_FALSE( data._folder.empty() );
    const test::ScopedAssertCapture assertCapture;

    GameObjectManager manager;
    AnimationSystem&  system = manager.getSubsystem<AnimationSystem>();
    system.setCrowdSettings( AnimationCrowdSettings{} );
    SkeletalMeshComponent* pUnit = test::createCrowdCharacter( manager, "Copy", data._skeleton, data._mesh, data._folder );
    SW_ASSERT_NOT_NULL( pUnit );
    GameObject* pSourceObject = manager.createGameObject( hashed_string( "Source" ) );
    SW_ASSERT_NOT_NULL( pSourceObject );
    SkeletalMeshComponent* pSource = pSourceObject->addComponent<SkeletalMeshComponent>();
    SW_ASSERT_NOT_NULL( pSource );
    pSource->setSkeleton( data._skeleton );

    RetargetProfile profile;
    profile.setRootAndPelvis( "bone0", "bone0", "bone0", "bone0", RetargetTranslationMode::Copy );
    RetargetChain arm{};
    arm._name           = hashed_string( "Arm" );
    arm._listSourceBone = { hashed_string( "bone1" ) };
    arm._listTargetBone = arm._listSourceBone;
    profile.addChain( arm );
    PoseRetargetComponent* pRetarget = pUnit->getOwner()->addComponent<PoseRetargetComponent>();
    SW_ASSERT_NOT_NULL( pRetarget );
    pRetarget->setProfile( profile );
    pRetarget->dispatchBeginPlay();

    system.evaluate( 1.0f / 60.0f ); // 원본이 없다 — 옮길 것이 없다
    SW_EXPECT_TRUE( isShared( *pUnit ) );

    pRetarget->setSource( pSource );
    system.evaluate( 1.0f / 60.0f );
    SW_EXPECT_TRUE( isSolo( *pUnit ) );

    pRetarget->setSource( nullptr );
    system.evaluate( 1.0f / 60.0f );
    SW_EXPECT_TRUE( isShared( *pUnit ) );
    SW_EXPECT_EQUAL( 0u, assertCapture.getCount() );
}

/**
 * @brief [AnimationPoseAccessTest] 얼굴은 모프 가중치나 눈 본을 쓸 때만 포즈를 쓴다 — 표정 가중치가 0 이고 시선이 없으면 묶음에 든다
 * @details 표정 가중치, 시선 목표가 각각 얼굴을 깨우고 놓으면 돌아온다. 선언은 시간 단계 뒤의 값(깜빡임 정도 · 애니메이터 커브)까지 보므로 후처리가 쓸 때와 같다.
 */
SW_TEST_CASE( AnimationPoseAccessTest, FacialAnimationWritesOnlyWhileDrivingTheFace )
{
    SW_ASSERT_TRUE( ResourcePaths::initialize() );
    const TestAnimationPoseAccessInternal data;
    SW_ASSERT_FALSE( data._folder.empty() );
    const test::ScopedAssertCapture assertCapture;

    GameObjectManager manager;
    AnimationSystem&  system = manager.getSubsystem<AnimationSystem>();
    system.setCrowdSettings( AnimationCrowdSettings{} );
    SkeletalMeshComponent* pUnit = test::createCrowdCharacter( manager, "Face", data._skeleton, data._mesh, data._folder );
    SW_ASSERT_NOT_NULL( pUnit );

    FacialRig rig;
    SW_ASSERT_TRUE( rig.parseXML( R"(<FacialRig>
	<_listExpression><FacialPose _name="Happy" /></_listExpression>
	<_blink _minInterval="2.0" _maxInterval="5.0" _duration="0.15" />
	<_gaze _forwardAxis="0,0,1" _maxAngle="0.5235988" _saccadeMinInterval="0.4" _saccadeMaxInterval="1.6" _saccadeAmplitude="0.0349066">
		<_listEyeBone><item>bone1</item></_listEyeBone>
	</_gaze>
</FacialRig>)",
                                  "test.facial.xml" ) );
    FacialAnimationComponent* pFace = pUnit->getOwner()->addComponent<FacialAnimationComponent>();
    SW_ASSERT_NOT_NULL( pFace );
    pFace->setFacialRig( rig );
    pFace->setAutoBlink( false );
    pFace->setSaccades( false );
    pFace->dispatchBeginPlay();

    system.evaluate( 1.0f / 60.0f ); // 표정 0 · 시선 없음 — 쓸 것이 없다
    SW_EXPECT_TRUE( isShared( *pUnit ) );

    SW_EXPECT_TRUE( pFace->setExpressionWeight( hashed_string( "Happy" ), 1.0f ) );
    system.evaluate( 1.0f / 60.0f );
    SW_EXPECT_TRUE( isSolo( *pUnit ) );
    SW_EXPECT_TRUE( pFace->setExpressionWeight( hashed_string( "Happy" ), 0.0f ) );
    system.evaluate( 1.0f / 60.0f );
    SW_EXPECT_TRUE( isShared( *pUnit ) );

    pFace->setLookAtTarget( float3{ 0.0f, 1.0f, 5.0f } );
    system.evaluate( 1.0f / 60.0f );
    SW_EXPECT_TRUE( isSolo( *pUnit ) );
    pFace->clearLookAtTarget();
    system.evaluate( 1.0f / 60.0f );
    SW_EXPECT_TRUE( isShared( *pUnit ) );
    SW_EXPECT_EQUAL( 0u, assertCapture.getCount() );
}

/**
 * @brief [AnimationPoseAccessTest] 본 비율이 없는 체형 비율 작업은 포즈를 쓰지 않는다 — 등록돼 있어도 군중 묶음을 막지 않는다
 * @details 본 비율이 있을 때 쓰는 쪽은 외형 조립(`applyBodyShapes`)이 등록하므로 이 테스트의 범위 밖이다.
 */
SW_TEST_CASE( AnimationPoseAccessTest, ProportionTaskWithoutProportionIsReadOnly )
{
    SW_ASSERT_TRUE( ResourcePaths::initialize() );
    const TestAnimationPoseAccessInternal data;
    SW_ASSERT_FALSE( data._folder.empty() );
    const test::ScopedAssertCapture assertCapture;

    GameObjectManager manager;
    AnimationSystem&  system = manager.getSubsystem<AnimationSystem>();
    system.setCrowdSettings( AnimationCrowdSettings{} );
    SkeletalMeshComponent* pUnit = test::createCrowdCharacter( manager, "Body", data._skeleton, data._mesh, data._folder );
    SW_ASSERT_NOT_NULL( pUnit );
    CharacterAppearanceComponent* pAppearance = pUnit->getOwner()->addComponent<CharacterAppearanceComponent>();
    SW_ASSERT_NOT_NULL( pAppearance );
    CharacterAppearanceProportionTask task( *pAppearance );
    SW_EXPECT_TRUE( task.getPoseAccess() == AnimationPoseAccess::ReadsOnly );

    // 작업은 매니저보다 먼저 지워지므로 끝에서 직접 뗀다.
    sw::Subscription taskRegistration = pUnit->registerAnimationPhaseTask( &task );
    system.evaluate( 1.0f / 60.0f );
    SW_EXPECT_TRUE( isShared( *pUnit ) );
    taskRegistration.reset();
    SW_EXPECT_EQUAL( 0u, assertCapture.getCount() );
}

/**
 * @brief [AnimationPoseAccessTest] 읽기만 한다고 선언한 작업이 포즈를 쓰면 Debug 단언이 잡는다
 * @details 군중 공유가 믿는 계약이다 — 읽기 작업은 묶음의 포즈를 읽고, 공유 유닛은 포즈 단계를 돌지 않으므로 그런 쓰기는 조용히 사라지거나 묶음의 포즈를 덮는다.
 *          유닛이 공유하지 않아야 작업이 돈다.
 */
SW_TEST_CASE( AnimationPoseAccessTest, ReadsOnlyTaskThatWritesTripsTheAssert )
{
    SW_ASSERT_TRUE( ResourcePaths::initialize() );
    const TestAnimationPoseAccessInternal data;
    SW_ASSERT_FALSE( data._folder.empty() );

    LyingReaderTask   liar;
    GameObjectManager manager;
    AnimationSystem&  system = manager.getSubsystem<AnimationSystem>();
    system.setCrowdSettings( AnimationCrowdSettings{} );
    SkeletalMeshComponent* pUnit = test::createCrowdCharacter( manager, "Liar", data._skeleton, data._mesh, data._folder, false );
    SW_ASSERT_NOT_NULL( pUnit );
    sw::Subscription liarRegistration = pUnit->registerAnimationPhaseTask( &liar );

    SW_TEST_DEFENSIVE_SCOPE( "a ReadsOnly task that changes the pose trips the assert" );
    const test::ScopedAssertCapture assertCapture;
    system.evaluate( 1.0f / 60.0f );
    if ( test::ScopedAssertCapture::kAssertsAreActive )
        SW_EXPECT_EQUAL( 1u, assertCapture.getCount() );
    liarRegistration.reset();
}
