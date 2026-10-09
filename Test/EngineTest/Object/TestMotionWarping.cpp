#include "pch.h"

#include "Core/File/FileUtil.h"
#include "Core/Math/MathUtil.h"

#include "Engine/Animation/AnimClip.h"
#include "Engine/Animation/Codec/Raw/RawAnimCodec.h"
#include "Engine/Animation/Skeleton.h"
#include "Engine/Character/AnimNotify/AnimNotifyComponent.h"
#include "Engine/Character/AnimNotify/AnimNotifyTable.h"
#include "Engine/Character/Socket/SocketSetComponent.h"
#include "Engine/Object/Animation/LocomotionWarpingComponent.h"
#include "Engine/Object/Animation/MotionWarpingComponent.h"
#include "Engine/Object/Component/3D/SkeletalAnimatorComponent.h"
#include "Engine/Object/Component/3D/SkeletalMeshComponent.h"
#include "Engine/Object/Component/Physics/CharacterControllerComponent.h"
#include "Engine/Object/Component/Physics/RigidBodyComponent.h"
#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManager.h"
#include "Engine/Resource/ResourceUtil.h"

#include "EngineTest/AnimationTestUtil.h"

#include "GameFramework/Base/Gameplay/Interaction/InteractableComponent.h"

#include "TestFramework/TestFramework.h"

using namespace sw;

// MotionWarpingTest — 클립 알림 창의 모션 워핑(자리 · 요), 루트 모션 → 캐릭터 컨트롤러, 상호작용 맞춤 마커(소켓 표), 보폭 · 방향 보정.

namespace
{
    struct TestMotionWarpingInternal
    {
        static constexpr float32 kFrame = 1.0f / 60.0f;

        /** @brief 루트가 +Z 로 1 m/s 인 1 초 클립(사슬 3 뼈)을 폴더에 쓰고 그 폴더를 돌려줍니다. @p warpWindowSeconds 가 0 보다 크면 0 초부터 그 길이의 `Warp` 구간 알림을 답니다. */
        static string writeWalkClip( const utf8* pFolderName, float32 warpWindowSeconds, float32 authoredSpeed )
        {
            const Skeleton    skeleton = test::makeChainSkeleton( 3 );
            const AnimRawClip raw      = test::makeChainRawClip( skeleton, 31, 30.0f, 0.0f );
            AnimClip          clip;
            clip.setName( hashed_string( "Walk" ) );
            clip.setLooping( true );
            clip.setRootMotionTrack( 0 );
            if ( warpWindowSeconds > 0.0f )
                clip.addNotify( AnimNotifyEvent{ hashed_string( "Warp" ), 0.0f, warpWindowSeconds } );
            if ( authoredSpeed > 0.0f )
            {
                AnimCurve curve{};
                curve._name    = hashed_string( "Speed" );
                curve._listKey = {
                    AnimCurveKey{0.0f, authoredSpeed},
                    AnimCurveKey{1.0f, authoredSpeed}
                };
                clip.addCurve( curve );
            }
            if ( clip.compressFrom( raw, RawAnimCodec::getInstance(), AnimCodecSettings{}, nullptr ) == false )
                return string{};
            const string folder = test::makeTempPath( pFolderName );
            if ( clip.saveToFile( FileUtil::joinPath( folder, "walk.animclip" ) ) == false )
                return string{};
            return folder;
        }

        /** @brief 오브젝트에 유닛 · 애니메이터를 붙여 `Walk` 를 재생할 준비를 합니다(루트 모션 뽑기). */
        static SkeletalAnimatorComponent* addAnimator( GameObject& object, const string& folder )
        {
            SkeletalMeshComponent*     pUnit     = object.addComponent<SkeletalMeshComponent>();
            SkeletalAnimatorComponent* pAnimator = object.addComponent<SkeletalAnimatorComponent>();
            if ( pUnit == nullptr || pAnimator == nullptr )
                return nullptr;
            pUnit->setSkeleton( make_shared<Skeleton>( test::makeChainSkeleton( 3 ) ) );
            pAnimator->setClipFolder( folder );
            pAnimator->setInitialState( "Walk" );
            pAnimator->setExtractRootMotion( true );
            return pAnimator;
        }

        static float32 computeYaw( const SceneComponent& scene )
        {
            const float3 forward = float3::transformVector( float3{ 0.0f, 0.0f, 1.0f }, scene.getWorldMatrix() );
            return MathUtil::atan2( forward._x, forward._z );
        }

        static RigidBodyComponent* spawnStaticBox( GameObjectManager& manager, const utf8* pName, const float3& position, const float3& halfExtents )
        {
            GameObject*         pObject = manager.createGameObject( hashed_string( pName ) );
            RigidBodyComponent* pBody   = pObject != nullptr ? pObject->addComponent<RigidBodyComponent>() : nullptr;
            if ( pBody == nullptr )
                return nullptr;
            PhysicsShapeDesc3D box;
            box._halfExtents = halfExtents;
            pBody->setShape( box );
            pBody->setBodyType( PhysicsBodyType::Static );
            pBody->setLayer( hashed_string( "Static" ) );
            pBody->setLocalPosition( position );
            return pBody;
        }
    };
} // namespace

/**
 * @brief [MotionWarpingTest] 클립의 `MotionWarp` 창(0 ~ 0.5 초)이 루트 모션을 휘어 창 끝 프레임에 목표 자리 · 요에 정확히 닿는다
 * @details 프레임 0.125 초(이진 정확) — 넷째 프레임이 창 끝(0.5)이고 그 프레임에 구간 끝도 울린다. 끝 알림이 루트 모션보다 먼저 처리되지만 창은 그 프레임까지 휜다.
 *          클립만이면 0.5 초에 (0, 0, 0.5). 목표 (0.5, 0, 2) · 요 90° 와 1 mm 안. 창이 닫힌 뒤에는 클립 그대로 간다.
 */
SW_TEST_CASE( MotionWarpingTest, WarpWindowReachesTargetAtWindowEnd )
{
    SW_ASSERT_TRUE( ResourceUtil::initialize() );
    const string folder = TestMotionWarpingInternal::writeWalkClip( "warpclips", 0.5f, 0.0f );
    SW_ASSERT_FALSE( folder.empty() );

    GameObjectManager manager;
    GameObject*       pObject = manager.createGameObject( hashed_string( "Warper" ) );
    SW_ASSERT_NOT_NULL( pObject );
    SkeletalAnimatorComponent* pAnimator = TestMotionWarpingInternal::addAnimator( *pObject, folder );
    MotionWarpingComponent*    pWarping  = pObject->addComponent<MotionWarpingComponent>();
    AnimNotifyComponent*       pNotify   = pObject->addComponent<AnimNotifyComponent>();
    SW_ASSERT_TRUE( pAnimator != nullptr && pWarping != nullptr && pNotify != nullptr );
    shared_ptr<AnimNotifyTable> table = make_shared<AnimNotifyTable>();
    SW_ASSERT_TRUE( table->loadFromXmlText( R"(<AnimNotifies><Notify name="Warp" handler="MotionWarp" target="Seat"/></AnimNotifies>)", "warp",
                                            AnimNotifyHandlerRegistry::getDefault() ) );
    pNotify->setNotifyTable( table );
    pAnimator->dispatchBeginPlay();
    pWarping->dispatchBeginPlay();
    pNotify->dispatchBeginPlay();
    SW_ASSERT_TRUE( pAnimator->play( hashed_string( "Walk" ), false, 0.0f ) );
    pWarping->setWarpTarget( hashed_string( "Seat" ), float3{ 0.5f, 0.0f, 2.0f }, MathUtil::kHalfPi );

    SceneComponent* pRoot = pObject->getPrimarySceneComponent();
    SW_ASSERT_NOT_NULL( pRoot );
    constexpr float32 kStep = 0.125f;
    for ( uint32 frameIndex = 0; frameIndex < 3; ++frameIndex )
    {
        manager.getAnimationSystem().evaluate( kStep );
    }
    SW_EXPECT_EQUAL( 1u, pWarping->getOpenWindowCount() );
    manager.getAnimationSystem().evaluate( kStep );
    const float3 position = pRoot->getWorldPosition();
    SW_EXPECT_NEAR_EQUAL( 0.5f, position._x, 1e-3f );
    SW_EXPECT_NEAR_EQUAL( 0.0f, position._y, 1e-3f );
    SW_EXPECT_NEAR_EQUAL( 2.0f, position._z, 1e-3f );
    SW_EXPECT_NEAR_EQUAL( MathUtil::kHalfPi, TestMotionWarpingInternal::computeYaw( *pRoot ), 1e-3f );
    SW_EXPECT_EQUAL( 0u, pWarping->getOpenWindowCount() );

    // 창 밖 — 클립 그대로(요 90° 라 +Z 루트 모션이 월드 +X 로 0.125 m).
    manager.getAnimationSystem().evaluate( kStep );
    SW_EXPECT_NEAR_EQUAL( 0.625f, pRoot->getWorldPosition()._x, 1e-3f );
    SW_EXPECT_NEAR_EQUAL( 2.0f, pRoot->getWorldPosition()._z, 1e-3f );
}

/**
 * @brief [MotionWarpingTest] 루트 모션은 같은 오브젝트의 캐릭터 컨트롤러로 움직인다 — 벽에 막힌다(트랜스폼에 바로 쓰면 벽을 지나간다)
 * @details 바닥 위의 컨트롤러가 +Z 1 m/s 루트 모션 클립을 2 초 돈다. 벽 앞면은 z = 1.1 — 캡슐 반지름 0.3 이면 z ≈ 0.8 에서 선다.
 */
SW_TEST_CASE( MotionWarpingTest, RootMotionMovesThroughCharacterController )
{
    SW_ASSERT_TRUE( ResourceUtil::initialize() );
    const string folder = TestMotionWarpingInternal::writeWalkClip( "controllerclips", 0.0f, 0.0f );
    SW_ASSERT_FALSE( folder.empty() );

    GameObjectManager manager;
    SW_ASSERT_NOT_NULL( TestMotionWarpingInternal::spawnStaticBox( manager, "Floor", float3{ 0.0f, -0.5f, 0.0f }, float3{ 10.0f, 0.5f, 10.0f } ) );
    SW_ASSERT_NOT_NULL( TestMotionWarpingInternal::spawnStaticBox( manager, "Wall", float3{ 0.0f, 1.0f, 1.3f }, float3{ 3.0f, 1.0f, 0.2f } ) );
    GameObject* pWalker = manager.createGameObject( hashed_string( "Walker" ) );
    SW_ASSERT_NOT_NULL( pWalker );
    CharacterControllerComponent* pController = pWalker->addComponent<CharacterControllerComponent>();
    SW_ASSERT_NOT_NULL( pController );
    pController->setLocalPosition( float3{ 0.0f, 0.02f, 0.0f } );
    SW_ASSERT_NOT_NULL( TestMotionWarpingInternal::addAnimator( *pWalker, folder ) );

    manager.beginPlay();
    for ( uint32 frameIndex = 0; frameIndex < 120; ++frameIndex )
    {
        manager.tick( TestMotionWarpingInternal::kFrame );
    }
    const float32 z = pController->getWorldPosition()._z;
    SW_EXPECT_TRUE( z > 0.5f );
    SW_EXPECT_TRUE( z < 0.95f );
    SW_EXPECT_TRUE( pController->isGrounded() );
    manager.endPlay();
}

/**
 * @brief [MotionWarpingTest] 상호작용 맞춤 지점은 정의의 마커를 오브젝트의 소켓 표에서 찾는다(마커의 +Z 가 볼 방향) — 없으면 오브젝트 원점 · 앞
 */
SW_TEST_CASE( MotionWarpingTest, AlignmentPointResolvesSocketMarker )
{
    SW_ASSERT_TRUE( ResourceUtil::initialize() );
    const string socketPath = test::makeTempPath( "door.sockets.xml" );
    SW_ASSERT_TRUE( FileUtil::writeTextFile( socketPath, R"(<SocketSet><Socket name="Front" translation="0 0 1.5" rotation="0 180 0"/></SocketSet>)" ) );

    GameObjectManager manager;
    GameObject*       pDoor = manager.createGameObject( hashed_string( "Door" ) );
    SW_ASSERT_NOT_NULL( pDoor );
    SW_ASSERT_NOT_NULL( pDoor->addComponent<SceneComponent>() );
    InteractableComponent* pInteractable = pDoor->addComponent<InteractableComponent>();
    SocketSetComponent*    pSockets      = pDoor->addComponent<SocketSetComponent>();
    SW_ASSERT_TRUE( pInteractable != nullptr && pSockets != nullptr );
    pDoor->getPrimarySceneComponent()->setWorldPosition( float3{ 2.0f, 0.0f, 3.0f } );
    InteractionDef def;
    def._id              = hashed_string( "Open" );
    def._alignmentMarker = hashed_string( "Front" );
    pInteractable->setDefinition( def );

    float3  position{};
    float32 yaw = 0.0f;
    SW_EXPECT_FALSE( pInteractable->computeAlignmentPoint( position, yaw ) ); // 표가 아직 없다 — 원점 · 앞
    SW_EXPECT_NEAR_EQUAL( 3.0f, position._z, 1e-4f );
    SW_EXPECT_NEAR_EQUAL( 0.0f, yaw, 1e-4f );

    pSockets->setSocketSetPath( socketPath );
    SW_EXPECT_TRUE( pInteractable->computeAlignmentPoint( position, yaw ) );
    SW_EXPECT_NEAR_EQUAL( 2.0f, position._x, 1e-4f );
    SW_EXPECT_NEAR_EQUAL( 4.5f, position._z, 1e-4f );
    SW_EXPECT_NEAR_EQUAL( MathUtil::kPi, MathUtil::abs( yaw ), 1e-3f );
}

/**
 * @brief [MotionWarpingTest] 보폭 · 방향 보정 — 클립 속도 2 m/s · 실제 3 m/s 면 재생 배율 1.5, 몸 앞(+Z)과 45° 로 가면 골반이 위 축으로 45° 돈다
 */
SW_TEST_CASE( MotionWarpingTest, LocomotionWarpingScalesRateAndTurnsLowerBody )
{
    SW_ASSERT_TRUE( ResourceUtil::initialize() );
    const string folder = TestMotionWarpingInternal::writeWalkClip( "strideclips", 0.0f, 2.0f );
    SW_ASSERT_FALSE( folder.empty() );

    GameObjectManager manager;
    GameObject*       pObject = manager.createGameObject( hashed_string( "Runner" ) );
    SW_ASSERT_NOT_NULL( pObject );
    SkeletalAnimatorComponent* pAnimator = TestMotionWarpingInternal::addAnimator( *pObject, folder );
    SW_ASSERT_NOT_NULL( pAnimator );
    pAnimator->setExtractRootMotion( false ); // 움직임은 시험이 직접 준다
    LocomotionWarpingComponent* pLocomotion = pObject->addComponent<LocomotionWarpingComponent>();
    SW_ASSERT_NOT_NULL( pLocomotion );
    pLocomotion->setOrientationBones( {
        LocomotionOrientationBone{ hashed_string( "bone1" ), 1.0f }
    } );
    pAnimator->dispatchBeginPlay();
    pLocomotion->dispatchBeginPlay();

    SceneComponent* pRoot = pObject->getPrimarySceneComponent();
    const float32   speed = 3.0f;
    const float3    step  = float3{ 1.0f, 0.0f, 1.0f }.normalize() * ( speed * TestMotionWarpingInternal::kFrame );
    for ( uint32 frameIndex = 0; frameIndex < 10; ++frameIndex )
    {
        pRoot->setWorldPosition( pRoot->getWorldPosition() + step );
        manager.getAnimationSystem().evaluate( TestMotionWarpingInternal::kFrame );
    }
    SW_EXPECT_NEAR_EQUAL( 1.5f, pAnimator->getPlayRate(), 1e-3f );
    SW_EXPECT_NEAR_EQUAL( 1.0f, pLocomotion->getStrideScale(), 1e-3f );
    SW_EXPECT_NEAR_EQUAL( MathUtil::kPi * 0.25f, pLocomotion->getOrientationAngle(), 1e-3f );

    // 한 프레임 더 — 후처리가 지난 프레임의 각으로 뼈 1 을 돌렸다(모델 공간의 앞이 45°).
    pRoot->setWorldPosition( pRoot->getWorldPosition() + step );
    manager.getAnimationSystem().evaluate( TestMotionWarpingInternal::kFrame );
    float4x4 boneModel;
    SW_ASSERT_TRUE( pObject->getComponent<SkeletalMeshComponent>()->findBoneModelTransform( hashed_string( "bone1" ), boneModel ) );
    const float3 boneForward = float3::transformVector( float3{ 0.0f, 0.0f, 1.0f }, boneModel ).normalize();
    SW_EXPECT_NEAR_EQUAL( MathUtil::kPi * 0.25f, MathUtil::atan2( boneForward._x, boneForward._z ), 1e-3f );
}
