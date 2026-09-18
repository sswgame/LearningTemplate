#include "pch.h"

#include "Engine/Animation/AnimClip.h"
#include "Engine/Animation/AnimPlayer.h"
#include "Engine/Animation/BlendSpace.h"
#include "Engine/Animation/DualQuaternion.h"
#include "Engine/Animation/Skeleton.h"
#include "Engine/Object/Component/2D/SpriteAnimatorComponent.h"

#include "TestFramework/TestFramework.h"

using namespace sw;

// ------------------------------------------------------------------------------
// 1) AnimationTest — 클립 샘플링, 루프 및 크로스페이드 검증
// ------------------------------------------------------------------------------

/**
 * @brief [AnimationTest] AnimClip 속성 설정 및 시간대별 가중치 샘플링 검증
 */
SW_TEST_CASE( AnimationTest, AnimClipSamplingAndLooping )
{
    AnimClip clip( "Run", 2.0f );
    SW_EXPECT_EQUAL( string( "Run" ), clip.getName() );
    SW_EXPECT_NEAR_EQUAL( 2.0f, clip.getDuration(), 1e-4f );

    clip.setName( "Walk" );
    SW_EXPECT_EQUAL( string( "Walk" ), clip.getName() );

    clip.setDuration( 4.0f );
    SW_EXPECT_NEAR_EQUAL( 4.0f, clip.getDuration(), 1e-4f );

    // 1) 시작 시점 샘플링
    AnimSample sampleStart = clip.sample( 0.0f, true );
    SW_EXPECT_NEAR_EQUAL( 0.0f, sampleStart._normalizedTime, 1e-4f );

    // 2) 중간 시점 샘플링 (t = 2.0s on 4.0s duration -> weight = 0.5)
    AnimSample sampleMid = clip.sample( 2.0f, true );
    SW_EXPECT_NEAR_EQUAL( 0.5f, sampleMid._normalizedTime, 1e-4f );

    // 3) 루핑 모드 초과 시간 래핑 (t = 5.0s on 4.0s duration -> t = 1.0s, weight = 0.25)
    AnimSample sampleLoopWrap = clip.sample( 5.0f, true );
    SW_EXPECT_NEAR_EQUAL( 0.25f, sampleLoopWrap._normalizedTime, 1e-4f );

    // 4) 비루핑 모드 클램핑 (t = 5.0s on 4.0s duration -> t = 4.0s, weight = 1.0)
    AnimSample sampleClamp = clip.sample( 5.0f, false );
    SW_EXPECT_NEAR_EQUAL( 1.0f, sampleClamp._normalizedTime, 1e-4f );

    // 5) 음수 시간 루핑 래핑 (t = -1.0s on 4.0s duration -> t = 3.0s, weight = 0.75)
    AnimSample sampleNeg = clip.sample( -1.0f, true );
    SW_EXPECT_NEAR_EQUAL( 0.75f, sampleNeg._normalizedTime, 1e-4f );
}

/**
 * @brief [AnimationTest] AnimPlayer 단일 클립 재생 및 시간 갱신 검증
 */
SW_TEST_CASE( AnimationTest, AnimPlayerPlayAndUpdate )
{
    AnimClip   idleClip( "Idle", 1.0f );
    AnimPlayer player;

    SW_EXPECT_NULL( player.getCurrentClip() );
    SW_EXPECT_NULL( player.getNextClip() );
    SW_EXPECT_FALSE( player.isCrossfading() );

    // 빈 플레이어 평가 시 항등 변환 반환 검증
    AnimSample emptySample = player.evaluate();
    SW_EXPECT_NEAR_EQUAL( 0.0f, emptySample._normalizedTime, 1e-4f );

    player.play( &idleClip, true );
    SW_EXPECT_EQUAL( &idleClip, player.getCurrentClip() );
    SW_EXPECT_NULL( player.getNextClip() );
    SW_EXPECT_FALSE( player.isCrossfading() );

    // 0.5초 경과 후 평가
    player.update( 0.5f );
    AnimSample sampleHalf = player.evaluate();
    SW_EXPECT_NEAR_EQUAL( 0.5f, sampleHalf._normalizedTime, 1e-4f );

    // 1.0초 추가 경과 후 루프 래핑 검증 (총 1.5s -> weight = 0.5)
    player.update( 1.0f );
    AnimSample sampleWrap = player.evaluate();
    SW_EXPECT_NEAR_EQUAL( 0.5f, sampleWrap._normalizedTime, 1e-4f );
}

/**
 * @brief [AnimationTest] AnimPlayer 재생 속도(Playback Speed) 스케일링 검증
 */
SW_TEST_CASE( AnimationTest, AnimPlayerPlaybackSpeed )
{
    AnimClip   clip( "Walk", 2.0f );
    AnimPlayer player;
    player.play( &clip, true );

    SW_EXPECT_NEAR_EQUAL( 1.0f, player.getSpeed(), 1e-4f );

    // 2배속 재생 설정 -> 0.5초 경과 시 1.0초만큼 진행 (weight = 0.5)
    player.setSpeed( 2.0f );
    SW_EXPECT_NEAR_EQUAL( 2.0f, player.getSpeed(), 1e-4f );
    player.update( 0.5f );
    AnimSample sampleFast = player.evaluate();
    SW_EXPECT_NEAR_EQUAL( 0.5f, sampleFast._normalizedTime, 1e-4f );

    // 일시정지 (speed = 0.0f) -> 1.0초 경과해도 시간 미변경 (weight = 0.5 유지)
    player.setSpeed( 0.0f );
    player.update( 1.0f );
    AnimSample samplePaused = player.evaluate();
    SW_EXPECT_NEAR_EQUAL( 0.5f, samplePaused._normalizedTime, 1e-4f );

    // 0.5배속 재생 (speed = 0.5f) -> 1.0초 경과 시 0.5초 진행 (총 1.5초 진행 -> weight = 0.75)
    player.setSpeed( 0.5f );
    player.update( 1.0f );
    AnimSample sampleSlow = player.evaluate();
    SW_EXPECT_NEAR_EQUAL( 0.75f, sampleSlow._normalizedTime, 1e-4f );
}

/**
 * @brief [AnimationTest] AnimPlayer 두 클립 간 크로스페이드 및 자동 전환 검증
 */
SW_TEST_CASE( AnimationTest, AnimPlayerCrossfade )
{
    AnimClip clipA( "Walk", 2.0f );
    AnimClip clipB( "Run", 1.0f );

    AnimPlayer player;
    player.play( &clipA, true );

    // t = 0.5s 진행 (clipA weight = 0.25)
    player.update( 0.5f );

    // 1.0초 동안 clipB로 크로스페이드 시작
    player.crossfade( &clipB, 1.0f, true );
    SW_EXPECT_TRUE( player.isCrossfading() );
    SW_EXPECT_EQUAL( &clipA, player.getCurrentClip() );
    SW_EXPECT_EQUAL( &clipB, player.getNextClip() );

    // 페이드 중간 지점 (0.5s 갱신 -> fadeElapsed = 0.5s / fadeDuration = 1.0s, alpha = 0.5)
    // clipA time = 1.0s (weight = 0.5)
    // clipB time = 0.5s (weight = 0.5)
    // blended weight = 0.5 * 0.5 + 0.5 * 0.5 = 0.5
    player.update( 0.5f );
    SW_EXPECT_TRUE( player.isCrossfading() );
    AnimSample blendedSample = player.evaluate();
    SW_EXPECT_NEAR_EQUAL( 0.5f, blendedSample._normalizedTime, 1e-4f );

    // 페이드 완료 지점 (0.5s 추가 갱신 -> fadeElapsed = 1.0s >= fadeDuration)
    player.update( 0.5f );
    SW_EXPECT_FALSE( player.isCrossfading() );
    SW_EXPECT_EQUAL( &clipB, player.getCurrentClip() );
    SW_EXPECT_NULL( player.getNextClip() );

    // 전환 완료 후 clipB 단독 재생 상태 확인 (clipB time = 1.0s -> loop wrap to 0.0s)
    AnimSample finalSample = player.evaluate();
    SW_EXPECT_NEAR_EQUAL( 0.0f, finalSample._normalizedTime, 1e-4f );
}

/**
 * @brief [AnimationTest] DualQuaternion 이동/회전 변환 복원 및 DLB(Dual Linear Blend) 보간 검증
 */
SW_TEST_CASE( AnimationTest, DualQuaternion_TransformAndDLB )
{
    const float3     transA{ 10.0f, 20.0f, 30.0f };
    const quaternion rotA = quaternion::createFromAxisAngle( float3{ 0.0f, 1.0f, 0.0f }, 0.0f );
    DualQuaternion   dqA  = DualQuaternion::fromTransform( transA, rotA );

    const float3 recoveredTransA = dqA.getTranslation();
    SW_EXPECT_NEAR_EQUAL( 10.0f, recoveredTransA._x, 1e-3f );
    SW_EXPECT_NEAR_EQUAL( 20.0f, recoveredTransA._y, 1e-3f );
    SW_EXPECT_NEAR_EQUAL( 30.0f, recoveredTransA._z, 1e-3f );

    const float3     transB{ 20.0f, 40.0f, 60.0f };
    const quaternion rotB = quaternion::Identity;
    DualQuaternion   dqB  = DualQuaternion::fromTransform( transB, rotB );

    // t = 0.5 에서 순수 이동 DLB 보간 검증 (중간 위치 (15, 30, 45))
    DualQuaternion blended  = DualQuaternion::dlb( dqA, dqB, 0.5f );
    const float3   midTrans = blended.getTranslation();
    SW_EXPECT_NEAR_EQUAL( 15.0f, midTrans._x, 1e-2f );
    SW_EXPECT_NEAR_EQUAL( 30.0f, midTrans._y, 1e-2f );
    SW_EXPECT_NEAR_EQUAL( 45.0f, midTrans._z, 1e-2f );

    // 회전 변환 복원 검증
    const quaternion rot90 = quaternion::createFromAxisAngle( float3{ 0.0f, 1.0f, 0.0f }, 3.14159265f * 0.5f );
    DualQuaternion   dqRot = DualQuaternion::fromTransform( float3{ 5.0f, 5.0f, 5.0f }, rot90 );
    SW_EXPECT_NEAR_EQUAL( rot90._y, dqRot.getRotation()._y, 1e-3f );
}

/**
 * @brief [AnimationTest] Skeleton 본 계층 구조 생성 및 스키닝 행렬 계산 검증
 */
SW_TEST_CASE( AnimationTest, Skeleton_BoneHierarchyAndSkinningMatrices )
{
    Skeleton skel;
    SW_EXPECT_EQUAL( 0u, static_cast<uint32>( skel.getBoneCount() ) );

    // Root Bone (Hips)
    float4x4 rootBoneSpace        = float4x4::createTranslation( float3{ 0.0f, 10.0f, 0.0f } );
    float4x4 rootInvReferencePose = float4x4::createTranslation( float3{ 0.0f, -10.0f, 0.0f } );
    int32    rootIdx              = skel.addBone( "Hips", -1, rootInvReferencePose, rootBoneSpace );
    SW_EXPECT_EQUAL( 0, rootIdx );

    // Child Bone (Spine)
    float4x4 spineBoneSpace        = float4x4::createTranslation( float3{ 0.0f, 5.0f, 0.0f } );
    float4x4 spineInvReferencePose = float4x4::createTranslation( float3{ 0.0f, -15.0f, 0.0f } );
    int32    spineIdx              = skel.addBone( "Spine", rootIdx, spineInvReferencePose, spineBoneSpace );
    SW_EXPECT_EQUAL( 1, spineIdx );

    skel.updateCharacterSpaceTransforms();

    const auto& skinningMats = skel.getSkinningMatrices();
    SW_EXPECT_EQUAL( 2u, static_cast<uint32>( skinningMats.size() ) );

    // 레퍼런스 포즈와 동일할 때 SkinningMatrix = CharacterSpace * InvReferencePose = Identity
    SW_EXPECT_NEAR_EQUAL( 0.0f, skinningMats[0]._41, 1e-3f );
    SW_EXPECT_NEAR_EQUAL( 0.0f, skinningMats[0]._42, 1e-3f );
    SW_EXPECT_NEAR_EQUAL( 0.0f, skinningMats[0]._43, 1e-3f );

    SW_EXPECT_NEAR_EQUAL( 0.0f, skinningMats[1]._41, 1e-3f );
    SW_EXPECT_NEAR_EQUAL( 0.0f, skinningMats[1]._42, 1e-3f );
    SW_EXPECT_NEAR_EQUAL( 0.0f, skinningMats[1]._43, 1e-3f );
}

/**
 * @brief [AnimationTest] BlendSpace1D 및 2D 파라메트릭 모션 블렌딩 검증
 */
SW_TEST_CASE( AnimationTest, BlendSpace_ParametricMotionInterpolation )
{
    BlendSpace1D bs1D;
    bs1D.addSample( 0.0f, "Idle", float4x4::createTranslation( float3{ 0.0f, 0.0f, 0.0f } ) );
    bs1D.addSample( 5.0f, "Walk", float4x4::createTranslation( float3{ 0.0f, 0.0f, 5.0f } ) );
    bs1D.addSample( 10.0f, "Run", float4x4::createTranslation( float3{ 0.0f, 0.0f, 15.0f } ) );

    // 1) Idle 경계
    float4x4 pose0 = bs1D.evaluate( 0.0f );
    SW_EXPECT_NEAR_EQUAL( 0.0f, pose0._43, 1e-3f );

    // 2) Walk-Run 중간 (speed = 7.5 -> t = 0.5 between 5 and 15 -> 10.0f)
    float4x4 poseMid = bs1D.evaluate( 7.5f );
    SW_EXPECT_NEAR_EQUAL( 10.0f, poseMid._43, 1e-2f );

    // 3) 2D Blend Space (IDW)
    BlendSpace2D bs2D;
    bs2D.addSample( 0.0f, 0.0f, "Idle", float4x4::createTranslation( float3{ 0.0f, 0.0f, 0.0f } ) );
    bs2D.addSample( 1.0f, 0.0f, "Right", float4x4::createTranslation( float3{ 10.0f, 0.0f, 0.0f } ) );
    bs2D.addSample( -1.0f, 0.0f, "Left", float4x4::createTranslation( float3{ -10.0f, 0.0f, 0.0f } ) );

    float4x4 pose2D = bs2D.evaluate( 1.0f, 0.0f );
    SW_EXPECT_NEAR_EQUAL( 10.0f, pose2D._41, 1e-2f );
}

/**
 * @brief [AnimationTest] 스케일이 섞인 행렬에서도 회전을 제대로 뽑는지 검증
 * @details 축 길이로 나누지 않고 `createFromRotationMatrix` 를 바로 걸면 스케일이 회전에 새어 든다.
 *          스케일 (2,1,1) + Z 90도는 그렇게 읽으면 112.6도가 된다.
 */
SW_TEST_CASE( AnimationTest, DualQuaternionKeepsRotationOfScaledMatrix )
{
    const float4x4 rotationOnly = float4x4::createRotationZ( MathUtil::HalfPi );
    const float4x4 scaledPose   = float4x4::createScale( float3{ 2.0f, 1.0f, 1.0f } ) * rotationOnly;

    const quaternion expected = DualQuaternion::fromMatrix( rotationOnly ).getRotation();
    const quaternion actual   = DualQuaternion::fromMatrix( scaledPose ).getRotation();

    SW_EXPECT_NEAR_EQUAL( expected._x, actual._x, 1e-3f );
    SW_EXPECT_NEAR_EQUAL( expected._y, actual._y, 1e-3f );
    SW_EXPECT_NEAR_EQUAL( expected._z, actual._z, 1e-3f );
    SW_EXPECT_NEAR_EQUAL( expected._w, actual._w, 1e-3f );
}

/**
 * @brief [AnimationTest] BlendSpace1D 가 표본 사이에서도 스케일을 유지하는지 검증
 * @details 예전에는 표본 지점에서만 원본 포즈를 돌려주고 그 사이에서는 스케일이 1 로 주저앉아,
 *          파라미터를 조금 옮기는 것만으로 포즈가 튀었다.
 */
SW_TEST_CASE( AnimationTest, BlendSpace1DKeepsScaleBetweenSamples )
{
    const float4x4 poseA = float4x4::createScale( 2.0f ) * float4x4::createTranslation( float3{ 0.0f, 0.0f, 0.0f } );
    const float4x4 poseB = float4x4::createScale( 2.0f ) * float4x4::createTranslation( float3{ 0.0f, 0.0f, 10.0f } );

    BlendSpace1D blendSpace;
    blendSpace.addSample( 0.0f, "A", poseA );
    blendSpace.addSample( 1.0f, "B", poseB );

    const float4x4 midPose = blendSpace.evaluate( 0.5f );
    SW_EXPECT_NEAR_EQUAL( 2.0f, midPose.getScale()._x, 1e-3f );
    SW_EXPECT_NEAR_EQUAL( 2.0f, midPose.getScale()._z, 1e-3f );
    SW_EXPECT_NEAR_EQUAL( 5.0f, midPose._43, 1e-2f );

    // 표본 바로 옆이 표본과 이어져야 한다.
    const float4x4 nearStartPose = blendSpace.evaluate( 0.001f );
    SW_EXPECT_NEAR_EQUAL( 2.0f, nearStartPose.getScale()._x, 1e-3f );
}

/**
 * @brief [AnimationTest] BlendSpace2D 가 33번째 이후 표본도 쓰는지 검증
 * @details 예전에는 가중치를 `float[32]` 에 담고 표본 수를 그 길이로 min 해서, 33번째부터
 *          아무 말 없이 버렸다. 목표 바로 옆에 둔 표본이 33번째면 결과가 통째로 달라진다.
 */
SW_TEST_CASE( AnimationTest, BlendSpace2DUsesSamplesBeyondThirtyTwo )
{
    BlendSpace2D blendSpace;
    for ( int32 index = 0; index < 32; ++index )
        blendSpace.addSample( 100.0f + static_cast<float32>( index ), 100.0f, "Far", float4x4::createTranslation( float3{ 0.0f, 0.0f, 0.0f } ) );

    blendSpace.addSample( 0.1f, 0.0f, "Near", float4x4::createTranslation( float3{ 0.0f, 0.0f, 100.0f } ) );
    SW_EXPECT_EQUAL( 33u, static_cast<uint32>( blendSpace.getSampleCount() ) );

    // 목표(0,0)에서 가장 가까운 표본이 결과를 지배해야 한다.
    const float4x4 pose = blendSpace.evaluate( 0.0f, 0.0f );
    SW_EXPECT_TRUE( pose._43 > 90.0f );
}

/**
 * @brief [AnimationTest] Skeleton 이 아직 없는 본을 부모로 받지 않는지 검증
 * @details `updateCharacterSpaceTransforms` 는 배열을 앞에서 뒤로 한 번만 훑는다. 부모가 뒤에
 *          있으면 그 본을 루트로 취급해 계층이 통째로 사라지는데, 예전에는 로그조차 없었다.
 */
SW_TEST_CASE( AnimationTest, SkeletonRejectsParentThatDoesNotExistYet )
{
    Skeleton skeleton;

    // 본이 하나도 없는데 부모 0 은 자기 자신을 가리키는 셈이다.
    SW_EXPECT_EQUAL( -1, skeleton.addBone( "Hips", 0, float4x4::Identity, float4x4::Identity ) );
    SW_EXPECT_EQUAL( 0u, static_cast<uint32>( skeleton.getBoneCount() ) );

    const int32 rootIndex = skeleton.addBone( "Hips", -1, float4x4::Identity, float4x4::Identity );
    SW_EXPECT_EQUAL( 0, rootIndex );

    // 범위를 벗어난 부모도 같다.
    SW_EXPECT_EQUAL( -1, skeleton.addBone( "Spine", 7, float4x4::Identity, float4x4::Identity ) );
    SW_EXPECT_EQUAL( 1u, static_cast<uint32>( skeleton.getBoneCount() ) );
}

/**
 * @brief [AnimationTest] 음수 재생 속도가 크로스페이드를 영원히 멈추지 않는지 검증
 * @details 음수를 그대로 흘리면 `_fadeElapsed` 가 뒤로 흘러 페이드가 끝나지 않는다.
 *          `update` 가 음수 델타를 0 으로 막는 것과 같은 자리를 setSpeed 도 막는다.
 */
SW_TEST_CASE( AnimationTest, AnimPlayerClampsNegativeSpeed )
{
    AnimClip clipA( "Walk", 2.0f );
    AnimClip clipB( "Run", 2.0f );

    AnimPlayer player;
    player.play( &clipA, true );
    player.crossfade( &clipB, 1.0f, true );

    player.setSpeed( -1.0f );
    SW_EXPECT_NEAR_EQUAL( 0.0f, player.getSpeed(), 1e-4f );

    // 속도 0 은 일시정지다 — 페이드도 멈춘다(뒤로 흐르지는 않는다).
    for ( int32 step = 0; step < 4; ++step )
        player.update( 0.5f );
    SW_EXPECT_TRUE( player.isCrossfading() );

    // 다시 정방향으로 돌리면 페이드가 정상적으로 끝난다.
    player.setSpeed( 1.0f );
    for ( int32 step = 0; step < 3; ++step )
        player.update( 0.5f );
    SW_EXPECT_FALSE( player.isCrossfading() );
    SW_EXPECT_EQUAL( &clipB, player.getCurrentClip() );
}

/**
 * @brief [AnimationTest] SpriteAnimatorComponent 재생, 프레임 안전성 및 틱 검증
 */
SW_TEST_CASE( AnimationTest, SpriteAnimatorComponent_PlaybackAndFrameSafety )
{
    SpriteAnimatorComponent animator;
    SW_EXPECT_EQUAL( 1, animator.getTotalFrames() );
    SW_EXPECT_FALSE( animator.isPlaying() );

    animator.setFrameRate( 12.0f );
    animator.play( "Idle", true );
    SW_EXPECT_TRUE( animator.isPlaying() );
    SW_EXPECT_EQUAL( 1, animator.getTotalFrames() );

    animator.setTotalFrames( 4 );
    SW_EXPECT_EQUAL( 4, animator.getTotalFrames() );

    // 12fps -> 1 frame per 0.0833s. Advance by 0.1s
    animator.onTick( 0.1f );
    SW_EXPECT_EQUAL( 1, animator.getCurrentFrame() );

    // Advance past all 4 frames with looping
    animator.onTick( 0.4f );
    SW_EXPECT_TRUE( animator.getCurrentFrame() < 4 );
}
