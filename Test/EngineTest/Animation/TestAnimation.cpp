#include "pch.h"

#include "Core/File/FileUtil.h"
#include "Core/Math/MathUtil.h"

#include "Engine/Animation/AnimPlayer.h"
#include "Engine/Animation/Graph/AnimGraphAsset.h"
#include "Engine/Animation/Graph/BlendSpace.h"
#include "Engine/Animation/Skeletal/DualQuaternion.h"
#include "Engine/Animation/Skeletal/Pose.h"
#include "Engine/Animation/Skeletal/Skeleton.h"
#include "Engine/Animation/Sprite/SpriteClipAsset.h"
#include "Engine/Object/Component/2D/SpriteAnimatorComponent.h"
#include "Engine/Object/Component/2D/SpriteComponent.h"
#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManager.h"
#include "Engine/Reflection/ReflectionCore.h"
#include "Engine/Resource/ResourceUtil.h"

#include "EngineTest/AnimationTestUtil.h"

#include "TestFramework/TestFramework.h"

using namespace sw;

namespace
{
    /** @brief 이름 붙은 노드만 가진 그래프를 @p path 에 씁니다(링크 없음). */
    [[nodiscard]] bool writeNamedGraph( const string& path, std::initializer_list<const utf8*> listName )
    {
        AnimGraphAsset graph;
        int32          nodeID = 1;
        for ( const utf8* pName : listName )
        {
            AnimGraphNode node{};
            node._id   = nodeID++;
            node._name = pName;
            graph._listNode.push_back( node );
        }
        return graph.saveToFile( path );
    }

    /** @brief 애니메이터의 그래프 경로 PROPERTY 를 쓰고, 인스펙터 · 에셋 핫 리로드처럼 바뀐 칸을 알립니다. */
    [[nodiscard]] bool writeGraphPath( SpriteAnimatorComponent* pAnimator, const string& path )
    {
        const PropertyInfo* pProperty = pAnimator->getTypeInfo()->findPropertyInHierarchy( hashed_string( "_animGraphPath" ) );
        if ( pProperty == nullptr )
            return false;
        pProperty->setValue<string>( pAnimator, path );
        pAnimator->onPropertyChanged( hashed_string( "_animGraphPath" ) );
        return true;
    }
} // namespace

// ------------------------------------------------------------------------------
// 1) AnimationTest — 클립 샘플링, 루프 및 크로스페이드 검증
// ------------------------------------------------------------------------------

/**
 * @brief [AnimationTest] 재생 커서가 반복이면 한 바퀴 안으로 감고, 아니면 끝에서 멈추며 끝에 닿았다고 알린다
 */
SW_TEST_CASE( AnimationTest, AnimClipCursorLoopsAndClamps )
{
    AnimClipCursor cursor;
    AnimTimeStep   step = cursor.advance( 2.0f, 4.0f, true );
    SW_EXPECT_NEAR_EQUAL( 2.0f, cursor.getTime(), 1e-4f );
    SW_EXPECT_NEAR_EQUAL( 0.5f, cursor.computeNormalizedTime( 4.0f ), 1e-4f );
    SW_EXPECT_TRUE( step._bIncludesStart == SW_TRUE ); // 첫 걸음은 시작 시각을 포함한다

    // 반복: 2 + 3 = 5 → 한 번 감아 1.
    step = cursor.advance( 3.0f, 4.0f, true );
    SW_EXPECT_NEAR_EQUAL( 1.0f, cursor.getTime(), 1e-4f );
    SW_EXPECT_EQUAL( 1u, step._wrapCount );
    SW_EXPECT_TRUE( step._bIncludesStart == SW_FALSE );

    // 반복하지 않으면 끝에서 멈추고 끝에 닿았다고 알린다.
    AnimClipCursor once;
    step = once.advance( 5.0f, 4.0f, false );
    SW_EXPECT_NEAR_EQUAL( 4.0f, once.getTime(), 1e-4f );
    SW_EXPECT_TRUE( step._bReachedEnd == SW_TRUE );

    // 음수 걸음은 0 이다.
    step = cursor.advance( -1.0f, 4.0f, true );
    SW_EXPECT_NEAR_EQUAL( 1.0f, cursor.getTime(), 1e-4f );
}

/**
 * @brief [AnimationTest] AnimPlayer 단일 재생 및 시간 갱신 검증
 */
SW_TEST_CASE( AnimationTest, AnimPlayerPlayAndUpdate )
{
    test::TestPlayable idle( 1.0f, true );
    AnimPlayer         player;

    SW_EXPECT_NULL( player.getCurrentPlayable() );
    SW_EXPECT_NULL( player.getNextPlayable() );
    SW_EXPECT_FALSE( player.isCrossfading() );
    SW_EXPECT_TRUE( player.hasFinished() ); // 재생할 것이 없으면 끝난 것이다

    player.play( &idle, true );
    SW_EXPECT_TRUE( player.getCurrentPlayable() == &idle );
    SW_EXPECT_FALSE( player.isCrossfading() );

    player.update( 0.5f, nullptr );
    SW_EXPECT_NEAR_EQUAL( 0.5f, player.getCurrentNormalizedTime(), 1e-4f );

    // 1.0초 더 → 1.5 → 감겨 0.5
    player.update( 1.0f, nullptr );
    SW_EXPECT_NEAR_EQUAL( 0.5f, player.getCurrentNormalizedTime(), 1e-4f );
}

/**
 * @brief [AnimationTest] AnimPlayer 재생 속도(Playback Speed) 스케일링 검증
 */
SW_TEST_CASE( AnimationTest, AnimPlayerPlaybackSpeed )
{
    test::TestPlayable walk( 2.0f, true );
    AnimPlayer         player;
    player.play( &walk, true );
    SW_EXPECT_NEAR_EQUAL( 1.0f, player.getSpeed(), 1e-4f );

    // 2배속 → 0.5초 경과 시 1.0초 진행
    player.setSpeed( 2.0f );
    player.update( 0.5f, nullptr );
    SW_EXPECT_NEAR_EQUAL( 0.5f, player.getCurrentNormalizedTime(), 1e-4f );

    // 일시정지
    player.setSpeed( 0.0f );
    player.update( 1.0f, nullptr );
    SW_EXPECT_NEAR_EQUAL( 0.5f, player.getCurrentNormalizedTime(), 1e-4f );

    // 0.5배속 → 1.0초 경과 시 0.5초 진행(총 1.5초)
    player.setSpeed( 0.5f );
    player.update( 1.0f, nullptr );
    SW_EXPECT_NEAR_EQUAL( 0.75f, player.getCurrentNormalizedTime(), 1e-4f );
}

/**
 * @brief [AnimationTest] AnimPlayer 크로스페이드 — 두 칸이 함께 흐르고, 가중치가 시간에 비례하며, 끝나면 다음 칸이 지금 칸이 된다
 */
SW_TEST_CASE( AnimationTest, AnimPlayerCrossfade )
{
    test::TestPlayable walk( 2.0f, true );
    test::TestPlayable run( 1.0f, true );

    AnimPlayer player;
    player.play( &walk, true );
    player.update( 0.5f, nullptr );

    player.crossfade( &run, 1.0f, true );
    SW_EXPECT_TRUE( player.isCrossfading() );
    SW_EXPECT_TRUE( player.getCurrentPlayable() == &walk );
    SW_EXPECT_TRUE( player.getNextPlayable() == &run );

    // 페이드 중간: 가중치 0.5, walk 1.0 초, run 0.5 초
    player.update( 0.5f, nullptr );
    SW_EXPECT_TRUE( player.isCrossfading() );
    SW_EXPECT_NEAR_EQUAL( 0.5f, player.getBlendAlpha(), 1e-4f );
    SW_EXPECT_NEAR_EQUAL( 1.0f, player.getCurrentTime(), 1e-4f );
    SW_EXPECT_NEAR_EQUAL( 0.5f, player.getNextTime(), 1e-4f );

    // 페이드 완료: run 이 지금 칸, 시각 1.0 → 감겨 0
    player.update( 0.5f, nullptr );
    SW_EXPECT_FALSE( player.isCrossfading() );
    SW_EXPECT_TRUE( player.getCurrentPlayable() == &run );
    SW_EXPECT_NULL( player.getNextPlayable() );
    SW_EXPECT_NEAR_EQUAL( 0.0f, player.getCurrentNormalizedTime(), 1e-4f );
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
 * @brief [AnimationTest] 스켈레톤 레퍼런스 포즈 — 모델 공간은 부모 다음 자식(행벡터 규약), 레퍼런스 포즈의 스킨 팔레트는 단위 행렬
 */
SW_TEST_CASE( AnimationTest, Skeleton_BoneHierarchyAndSkinningMatrices )
{
    Skeleton skeleton;
    SW_EXPECT_EQUAL( 0u, skeleton.getBoneCount() );
    SW_EXPECT_EQUAL( 0, skeleton.addBone( hashed_string( "Hips" ), -1, test::makeBoneTransform( float3{ 0.0f, 10.0f, 0.0f } ), float4x4::Identity ) );
    // 자식은 부모 기준 Z 축 90 도 회전 + Y 5 — 모델 공간 위치는 (0, 15, 0), 손자 X 1 은 회전을 받아 (0, 16, 0) 이다.
    const quaternion turn = quaternion::createFromAxisAngle( float3{ 0.0f, 0.0f, 1.0f }, MathUtil::kHalfPi );
    SW_EXPECT_EQUAL( 1, skeleton.addBone( hashed_string( "Spine" ), 0, test::makeBoneTransform( float3{ 0.0f, 5.0f, 0.0f }, turn ), float4x4::Identity ) );
    SW_EXPECT_EQUAL( 2, skeleton.addBone( hashed_string( "Chest" ), 1, test::makeBoneTransform( float3{ 1.0f, 0.0f, 0.0f } ), float4x4::Identity ) );
    skeleton.computeInverseBindFromReference();

    Pose pose;
    pose.setToReference( skeleton );
    vector<float4x4> listModel;
    pose.computeModelSpace( skeleton.getParentIndices(), listModel );
    SW_ASSERT_EQUAL( 3u, static_cast<uint32>( listModel.size() ) );
    SW_EXPECT_NEAR_EQUAL( 15.0f, listModel[1]._42, 1e-4f );
    SW_EXPECT_NEAR_EQUAL( 0.0f, listModel[2]._41, 1e-4f );
    SW_EXPECT_NEAR_EQUAL( 16.0f, listModel[2]._42, 1e-4f );

    vector<float4x4> listPalette;
    Pose::computeSkinPalette( skeleton, listModel, listPalette );
    for ( const float4x4& palette : listPalette )
    {
        SW_EXPECT_NEAR_EQUAL( 1.0f, palette._11, 1e-4f );
        SW_EXPECT_NEAR_EQUAL( 0.0f, palette._41, 1e-4f );
        SW_EXPECT_NEAR_EQUAL( 0.0f, palette._42, 1e-4f );
    }
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
    const float4x4 rotationOnly = float4x4::createRotationZ( MathUtil::kHalfPi );
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
 * @details 표본 사이의 블렌드가 스케일을 섞지 않으면 그 사이에서 스케일이 1 로 주저앉아,
 *          파라미터를 조금 옮기는 것만으로 포즈가 튄다.
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
 * @details 가중치를 고정 길이 배열(`float[32]`)에 담고 표본 수를 그 길이로 min 하면 33번째부터
 *          아무 말 없이 버려진다. 목표 바로 옆에 둔 표본이 33번째면 결과가 통째로 달라진다.
 */
SW_TEST_CASE( AnimationTest, BlendSpace2DUsesSamplesBeyondThirtyTwo )
{
    BlendSpace2D blendSpace;
    for ( int32 index = 0; index < 32; ++index )
    {
        blendSpace.addSample( 100.0f + static_cast<float32>( index ), 100.0f, "Far", float4x4::createTranslation( float3{ 0.0f, 0.0f, 0.0f } ) );
    }

    blendSpace.addSample( 0.1f, 0.0f, "Near", float4x4::createTranslation( float3{ 0.0f, 0.0f, 100.0f } ) );
    SW_EXPECT_EQUAL( 33u, static_cast<uint32>( blendSpace.getSampleCount() ) );

    // 목표(0,0)에서 가장 가까운 표본이 결과를 지배해야 한다.
    const float4x4 pose = blendSpace.evaluate( 0.0f, 0.0f );
    SW_EXPECT_TRUE( pose._43 > 90.0f );
}

/**
 * @brief [AnimationTest] Skeleton 이 아직 없는 본을 부모로 받지 않는지 검증
 * @details 모델 공간은 배열을 앞에서 뒤로 한 번만 훑는다. 부모가 뒤에 있으면 그 본을 루트로 취급해 계층이 통째로 사라진다 — 그래서 받지 않고 알린다.
 */
SW_TEST_CASE( AnimationTest, SkeletonRejectsParentThatDoesNotExistYet )
{
    test::ScopedDefensiveTestLog expected( "bones whose parent is not added yet are rejected" );
    Skeleton                     skeleton;

    // 본이 하나도 없는데 부모 0 은 자기 자신을 가리키는 셈이다.
    SW_EXPECT_EQUAL( -1, skeleton.addBone( hashed_string( "Hips" ), 0, BoneTransform{}, float4x4::Identity ) );
    SW_EXPECT_EQUAL( 0u, skeleton.getBoneCount() );
    SW_EXPECT_EQUAL( 0, skeleton.addBone( hashed_string( "Hips" ), -1, BoneTransform{}, float4x4::Identity ) );
    // 범위를 벗어난 부모도 같다.
    SW_EXPECT_EQUAL( -1, skeleton.addBone( hashed_string( "Spine" ), 7, BoneTransform{}, float4x4::Identity ) );
    SW_EXPECT_EQUAL( 1u, skeleton.getBoneCount() );
}

/**
 * @brief [AnimationTest] 음수 재생 속도가 크로스페이드를 영원히 멈추지 않는지 검증
 * @details 음수를 그대로 흘리면 페이드 경과가 뒤로 흘러 페이드가 끝나지 않는다. 속도 0 은 일시정지다.
 */
SW_TEST_CASE( AnimationTest, AnimPlayerClampsNegativeSpeed )
{
    test::TestPlayable walk( 2.0f, true );
    test::TestPlayable run( 2.0f, true );

    AnimPlayer player;
    player.play( &walk, true );
    player.crossfade( &run, 1.0f, true );

    player.setSpeed( -1.0f );
    SW_EXPECT_NEAR_EQUAL( 0.0f, player.getSpeed(), 1e-4f );
    for ( int32 step = 0; step < 4; ++step )
    {
        player.update( 0.5f, nullptr );
    }
    SW_EXPECT_TRUE( player.isCrossfading() );

    player.setSpeed( 1.0f );
    for ( int32 step = 0; step < 3; ++step )
    {
        player.update( 0.5f, nullptr );
    }
    SW_EXPECT_FALSE( player.isCrossfading() );
    SW_EXPECT_TRUE( player.getCurrentPlayable() == &run );
}

/**
 * @brief [AnimationTest] 클립이 없는 애니메이터는 프레임 하나에 머문다 — 재생 · 틱 · 프레임 지정이 범위를 벗어나지 않는다
 * @details 프레임 수는 손으로 넣지 않고 스프라이트의 클립에서 온다. 붙은 스프라이트 · 클립이 없으면 구간은 프레임 하나다.
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

    // 12fps 로 한참 돌려도 하나뿐인 프레임을 넘지 않는다.
    animator.onTick( 0.1f );
    animator.onTick( 0.4f );
    SW_EXPECT_EQUAL( 0, animator.getCurrentFrame() );
    animator.setFrame( 7 );
    SW_EXPECT_EQUAL( 0, animator.getCurrentFrame() );
    animator.setFrame( -3 );
    SW_EXPECT_EQUAL( 0, animator.getCurrentFrame() );
}

/**
 * @brief [AnimationTest] 재생 중에 그래프 경로를 바꾸면 애니메이터가 새 그래프를 읽는다 — 지금 애니메이션이 새 그래프에 있으면 잇고, 없으면 새 첫 애니메이션으로
 * @details 그래프를 `onBeginPlay` 에서만 열면 인스펙터로 경로를 고치거나 그래프 파일을 고쳐 에셋 핫 리로드가 그 칸을 알려도(`onPropertyChanged`, 값이
 *          같아도) 옛 그래프 · 옛 애니메이션 목록이 남아, "끝나면 다음" 이 옛 그래프를 따라간다.
 */
SW_TEST_CASE( AnimationTest, SpriteAnimatorReopensItsGraphWhenThePathChanges )
{
    const string graphA = test::makeTempPath( "animator_a.animgraph.json" );
    const string graphB = test::makeTempPath( "animator_b.animgraph.json" );
    SW_ASSERT_TRUE( writeNamedGraph( graphA, { "Idle", "Run" } ) );
    SW_ASSERT_TRUE( writeNamedGraph( graphB, { "Jump", "Fall" } ) );

    GameObjectManager manager;
    GameObject*       pObject = manager.createGameObject( hashed_string( "Hero" ) );
    SW_ASSERT_NOT_NULL( pObject );
    SpriteAnimatorComponent* pAnimator = pObject->addComponent<SpriteAnimatorComponent>();
    SW_ASSERT_NOT_NULL( pAnimator );
    SW_ASSERT_TRUE( writeGraphPath( pAnimator, graphA ) );
    pAnimator->dispatchBeginPlay();
    SW_EXPECT_EQUAL( string( "Idle" ), pAnimator->getCurrentAnimation() );

    // 다른 그래프 — 지금 애니메이션(Idle)이 없으니 새 그래프의 첫 애니메이션부터.
    SW_ASSERT_TRUE( writeGraphPath( pAnimator, graphB ) );
    SW_EXPECT_EQUAL( string( "Jump" ), pAnimator->getCurrentAnimation() );
    SW_EXPECT_TRUE( pAnimator->isPlaying() );

    // 같은 경로의 파일이 바뀐다(핫 리로드 알림은 값이 같다) — 지금 애니메이션(Jump)이 남아 있으면 그대로 잇는다.
    SW_ASSERT_TRUE( writeNamedGraph( graphB, { "Land", "Jump" } ) );
    pAnimator->setFrame( 0 );
    SW_ASSERT_TRUE( writeGraphPath( pAnimator, graphB ) );
    SW_EXPECT_EQUAL( string( "Jump" ), pAnimator->getCurrentAnimation() );
    SW_EXPECT_TRUE( pAnimator->isPlaying() );
}

/**
 * @brief [AnimationTest] 애니메이터의 프레임 수 · 프레임마다의 시간 · 반복은 스프라이트 클립에서 오고, 넘긴 프레임은 스프라이트의 UV 사각형이 된다
 * @details 이름이 클립의 구간을 고르고(`run` = 프레임 2..4), 프레임마다 그 프레임의 시간(ms)만큼 머물며,
 *          스프라이트가 그 프레임의 UV 사각형을 GPU 인스턴스 칸에 싣는다. 구간이 반복이 아니면 마지막 프레임에서 멈춘다.
 */
SW_TEST_CASE( AnimationTest, SpriteAnimatorTakesFrameCountAndTimingFromTheClip )
{
    SW_ASSERT_TRUE( ResourceUtil::initialize() );
    const string clipPath = test::makeTempPath( "hero.sprite.json" );
    SW_ASSERT_TRUE( FileUtil::writeTextFile( clipPath, R"({
  "atlas": "engine/textures/test/quadrants.dds",
  "frames": [
    { "u": 0.0, "v": 0.0, "w": 0.5, "h": 0.5, "durationMs": 100 },
    { "u": 0.5, "v": 0.0, "w": 0.5, "h": 0.5, "durationMs": 100 },
    { "u": 0.0, "v": 0.5, "w": 0.5, "h": 0.5, "durationMs": 50 },
    { "u": 0.5, "v": 0.5, "w": 0.5, "h": 0.5, "durationMs": 300 },
    { "u": 0.25, "v": 0.25, "w": 0.5, "h": 0.5, "durationMs": 0 }
  ],
  "transformKeys": [],
  "animations": [
    { "name": "idle", "start": 0, "count": 2, "loop": true },
    { "name": "run", "start": 2, "count": 3, "loop": false }
  ]
})" ) );

    GameObjectManager manager;
    GameObject*       pObject = manager.createGameObject( hashed_string( "Hero" ) );
    SW_ASSERT_NOT_NULL( pObject );
    SpriteComponent* pSprite = pObject->addComponent<SpriteComponent>();
    SW_ASSERT_NOT_NULL( pSprite );
    pSprite->setClipPath( clipPath );
    SW_ASSERT_NOT_NULL( pSprite->getClip() );
    SpriteAnimatorComponent* pAnimator = pObject->addComponent<SpriteAnimatorComponent>();
    SW_ASSERT_NOT_NULL( pAnimator );
    pAnimator->setFrameRate( 10.0f ); // 시간이 0 인 프레임(4)은 0.1 초

    pAnimator->play( "run" );
    SW_EXPECT_EQUAL( 3, pAnimator->getTotalFrames() );
    SW_EXPECT_EQUAL( 2, pAnimator->getFirstClipFrame() );
    SW_EXPECT_FALSE( pAnimator->isRepeating() ); // 클립의 구간이 반복이 아니다
    SW_EXPECT_EQUAL( 2, pSprite->getClipFrame() );
    // 스프라이트가 그 프레임의 UV 사각형을 인스턴스 칸에 싣는다(unorm16 양자화).
    const float4 shownRect = pSprite->getSpriteInstanceData().getUvRect();
    SW_EXPECT_NEAR_EQUAL( 0.0f, shownRect._x, 1e-4f );
    SW_EXPECT_NEAR_EQUAL( 0.5f, shownRect._y, 1e-4f );
    SW_EXPECT_NEAR_EQUAL( 0.5f, shownRect._z, 1e-4f );

    // 프레임 2 는 50 ms 다 — 40 ms 로는 그대로, 20 ms 더 가면 프레임 3.
    pAnimator->onTick( 0.04f );
    SW_EXPECT_EQUAL( 0, pAnimator->getCurrentFrame() );
    pAnimator->onTick( 0.02f );
    SW_EXPECT_EQUAL( 1, pAnimator->getCurrentFrame() );
    SW_EXPECT_EQUAL( 3, pSprite->getClipFrame() );
    SW_EXPECT_NEAR_EQUAL( 0.5f, pSprite->getSpriteInstanceData().getUvRect()._x, 1e-4f );
    // 프레임 3 은 300 ms — 200 ms 로는 그대로다(고정 프레임 속도 10fps 로 넘기면 넘어간다).
    pAnimator->onTick( 0.2f );
    SW_EXPECT_EQUAL( 3, pSprite->getClipFrame() );
    // 끝까지 가면 반복하지 않고 마지막 프레임(4)에서 멈춘다.
    pAnimator->onTick( 1.0f );
    SW_EXPECT_EQUAL( 4, pSprite->getClipFrame() );
    SW_EXPECT_FALSE( pAnimator->isPlaying() );

    // 반복 구간은 감긴다: idle(0..1) 을 100 ms 프레임 셋 만큼 → 0, 1, 0.
    pAnimator->play( "idle" );
    SW_EXPECT_TRUE( pAnimator->isRepeating() );
    SW_EXPECT_EQUAL( 2, pAnimator->getTotalFrames() );
    pAnimator->onTick( 0.25f );
    SW_EXPECT_EQUAL( 0, pSprite->getClipFrame() );
    SW_EXPECT_TRUE( pAnimator->isPlaying() );

    // 클립에 없는 이름은 알리고 프레임 0 하나로 둔다.
    {
        test::ScopedDefensiveTestLog expected( "an animation name the clip does not have" );
        pAnimator->play( "fly" );
    }
    SW_EXPECT_EQUAL( 1, pAnimator->getTotalFrames() );
    SW_EXPECT_EQUAL( 0, pSprite->getClipFrame() );
}

/**
 * @brief [AnimationTest] 클립의 트랜스폼 키가 재생 중 스프라이트의 로컬 위치 x · y · Z 회전이 된다 — 클립 타임라인 시각으로 보간하고, 루트는 움직이지 않는다
 * @details 키 시각은 클립 타임라인의 초다: 프레임 넷이 100 ms 씩이면 구간 "b"(프레임 2..3)는 0.2 초에서 시작한다. 두 키 사이는 선형 보간이고,
 *          z 위치 · 루트 컴포넌트는 그대로다. 스프라이트가 오브젝트의 루트면 키를 적용하지 않고 한 번 알린다.
 */
SW_TEST_CASE( AnimationTest, SpriteAnimatorAppliesClipTransformKeys )
{
    SW_ASSERT_TRUE( ResourceUtil::initialize() );
    const string clipPath = test::makeTempPath( "keyed.sprite.json" );
    SW_ASSERT_TRUE( FileUtil::writeTextFile( clipPath, R"({
  "atlas": "engine/textures/test/quadrants.dds",
  "frames": [
    { "u": 0.0, "v": 0.0, "w": 0.5, "h": 0.5, "durationMs": 100 },
    { "u": 0.5, "v": 0.0, "w": 0.5, "h": 0.5, "durationMs": 100 },
    { "u": 0.0, "v": 0.5, "w": 0.5, "h": 0.5, "durationMs": 100 },
    { "u": 0.5, "v": 0.5, "w": 0.5, "h": 0.5, "durationMs": 100 }
  ],
  "transformKeys": [
    { "time": 0.4, "x": 4.0, "y": 2.0, "angleDeg": 0.0 },
    { "time": 0.0, "x": 0.0, "y": 0.0, "angleDeg": 0.0 },
    { "time": 0.2, "x": 2.0, "y": 0.0, "angleDeg": 90.0 }
  ],
  "animations": [
    { "name": "a", "start": 0, "count": 2, "loop": true },
    { "name": "b", "start": 2, "count": 2, "loop": false }
  ]
})" ) );

    GameObjectManager manager;
    GameObject*       pObject = manager.createGameObject( hashed_string( "Keyed" ) );
    SW_ASSERT_NOT_NULL( pObject );
    SceneComponent* pRoot = pObject->addComponent<SceneComponent>();
    SW_ASSERT_NOT_NULL( pRoot );
    pRoot->setLocalPosition( float3{ 5.0f, 5.0f, 0.0f } );
    SpriteComponent* pSprite = pObject->addComponent<SpriteComponent>();
    SW_ASSERT_NOT_NULL( pSprite );
    SW_ASSERT_TRUE( pObject->getPrimarySceneComponent() == pRoot );
    pSprite->setLocalPosition( float3{ 0.0f, 0.0f, 0.25f } );
    pSprite->setClipPath( clipPath );
    SW_ASSERT_NOT_NULL( pSprite->getClip() );
    SpriteAnimatorComponent* pAnimator = pObject->addComponent<SpriteAnimatorComponent>();
    SW_ASSERT_NOT_NULL( pAnimator );

    const auto expectSpritePose = [pSprite]( float32 x, float32 y, float32 angleDeg )
    {
        const float3 position = pSprite->getLocalPosition();
        SW_EXPECT_NEAR_EQUAL( x, position._x, 1e-4f );
        SW_EXPECT_NEAR_EQUAL( y, position._y, 1e-4f );
        SW_EXPECT_NEAR_EQUAL( 0.25f, position._z, 1e-6f ); // 키에 없는 z 는 그대로
        SW_EXPECT_NEAR_EQUAL( MathUtil::toRadian( angleDeg ), pSprite->getLocalRotation()._z, 1e-4f );
    };

    // "a" 는 0 초에서 시작한다. 50 ms 뒤는 0 · 0.2 키의 사분의 일, 150 ms 뒤(프레임 1 안)는 사분의 삼.
    pAnimator->play( "a" );
    expectSpritePose( 0.0f, 0.0f, 0.0f );
    pAnimator->onTick( 0.05f );
    expectSpritePose( 0.5f, 0.0f, 22.5f );
    pAnimator->onTick( 0.1f );
    SW_EXPECT_EQUAL( 1, pSprite->getClipFrame() );
    expectSpritePose( 1.5f, 0.0f, 67.5f );

    // "b" 는 클립 프레임 2 = 0.2 초에서 시작한다. 100 ms 뒤는 0.2 · 0.4 키의 가운데.
    pAnimator->play( "b" );
    expectSpritePose( 2.0f, 0.0f, 90.0f );
    pAnimator->onTick( 0.1f );
    expectSpritePose( 3.0f, 1.0f, 45.0f );
    // 반복하지 않는 구간이 끝나면 구간 끝 시각(0.4 초)에 멈춘다 — 남은 타이머로 더 가지 않는다.
    pAnimator->onTick( 0.5f );
    SW_EXPECT_FALSE( pAnimator->isPlaying() );
    expectSpritePose( 4.0f, 2.0f, 0.0f );

    // 루트는 키가 움직이지 않는다.
    const float3 rootPosition = pRoot->getLocalPosition();
    SW_EXPECT_NEAR_EQUAL( 5.0f, rootPosition._x, 1e-6f );
    SW_EXPECT_NEAR_EQUAL( 5.0f, rootPosition._y, 1e-6f );

    // 스프라이트가 루트인 오브젝트는 키를 적용하지 않고 재생마다 한 번 알린다.
    GameObject* pRootSprite = manager.createGameObject( hashed_string( "RootSprite" ) );
    SW_ASSERT_NOT_NULL( pRootSprite );
    SpriteComponent* pPrimarySprite = pRootSprite->addComponent<SpriteComponent>();
    SW_ASSERT_NOT_NULL( pPrimarySprite );
    pPrimarySprite->setLocalPosition( float3{ 7.0f, 8.0f, 0.0f } );
    pPrimarySprite->setClipPath( clipPath );
    SpriteAnimatorComponent* pRootAnimator = pRootSprite->addComponent<SpriteAnimatorComponent>();
    SW_ASSERT_NOT_NULL( pRootAnimator );
    {
        test::ScopedLogCollector     collector;
        test::ScopedDefensiveTestLog expected( "transform keys on a root sprite are not applied" );
        pRootAnimator->play( "b" );
        pRootAnimator->onTick( 0.1f );
        SW_EXPECT_EQUAL( 1u, collector.countContaining( "the sprite is the object's root" ) );
    }
    SW_EXPECT_NEAR_EQUAL( 7.0f, pPrimarySprite->getLocalPosition()._x, 1e-6f );
    SW_EXPECT_NEAR_EQUAL( 8.0f, pPrimarySprite->getLocalPosition()._y, 1e-6f );
}

/**
 * @brief [AnimationTest] BlendSpace2D 는 표본 범위 밖 파라미터를 범위로 가둬, 첫 표본이 아니라 가장 가까운 모서리를 낸다
 * @details 1/d² 를 그대로 더해 절대값 1e-6 과 견주면 cm/s 단위(표본 0~600)에서 대시 2500 일 때 합이 그보다 작아 **처음 넣은 표본**(Idle)으로
 *          튄다. 언리얼 블렌드 스페이스처럼 파라미터를 범위로 가둔다.
 */
SW_TEST_CASE( AnimationTest, BlendSpace2DClampsFarParametersToTheSampleRange )
{
    BlendSpace2D blendSpace;
    blendSpace.addSample( 0.0f, 0.0f, "Idle", float4x4::createTranslation( float3{ 0.0f, 0.0f, 0.0f } ) );
    blendSpace.addSample( 600.0f, 0.0f, "Run", float4x4::createTranslation( float3{ 600.0f, 0.0f, 0.0f } ) );
    blendSpace.addSample( 0.0f, 600.0f, "Strafe", float4x4::createTranslation( float3{ 0.0f, 600.0f, 0.0f } ) );
    blendSpace.addSample( 600.0f, 600.0f, "RunStrafe", float4x4::createTranslation( float3{ 600.0f, 600.0f, 0.0f } ) );

    const float4x4 dashPose = blendSpace.evaluate( 2500.0f, 0.0f );
    SW_EXPECT_NEAR_EQUAL( 600.0f, dashPose._41, 1e-2f );
    SW_EXPECT_NEAR_EQUAL( 0.0f, dashPose._42, 1e-2f );
}

/**
 * @brief [AnimationTest] 오래 돈 반복 재생도 시간이 한 바퀴 안에 있어 프레임마다 앞으로 간다
 * @details 시간이 끝없이 커지면 10^6 초 근처에서는 0.01 초를 더해도 float32 값이 움직이지 않는다(애니메이션이 멈춘다).
 */
SW_TEST_CASE( AnimationTest, AnimPlayerLongLoopKeepsAdvancing )
{
    test::TestPlayable idle( 1.0f, true );
    AnimPlayer         player;
    player.play( &idle, true );

    player.update( 1000000.25f, nullptr ); // 열하루 남짓 켜 둔 셈
    SW_EXPECT_TRUE( player.getCurrentTime() >= 0.0f && player.getCurrentTime() < idle.getPlayLength() );
    for ( int32 frame = 0; frame < 50; ++frame )
    {
        player.update( 0.01f, nullptr );
    }
    SW_EXPECT_NEAR_EQUAL( 0.75f, player.getCurrentNormalizedTime(), 1e-3f );
}
