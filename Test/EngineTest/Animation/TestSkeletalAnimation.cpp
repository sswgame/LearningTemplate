#include "pch.h"

#include "Core/Container/StringUtil.h"
#include "Core/File/FileUtil.h"
#include "Core/Math/MathUtil.h"

#include "Engine/Animation/AnimationClip.h"
#include "Engine/Animation/AnimationPlayer.h"
#include "Engine/Animation/Codec/ACL/ACLAnimationCodec.h"
#include "Engine/Animation/Codec/AnimationCodec.h"
#include "Engine/Animation/Codec/Raw/RawAnimationCodec.h"
#include "Engine/Animation/Skeletal/LocomotionSpeedUtil.h"
#include "Engine/Animation/Skeletal/Pose.h"
#include "Engine/Animation/Skeletal/Skeleton.h"
#include "Engine/Object/Component/3D/SkeletalAnimatorComponent.h"
#include "Engine/Object/Component/3D/SkeletalMeshComponent.h"
#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManager.h"
#include "Engine/Object/SceneAnimation/AnimationSystem.h"
#include "Engine/Resource/ResourcePaths.h"

#include "EngineTest/AnimationTestUtil.h"

#include "TestFramework/TestFramework.h"

using namespace sw;

// SkeletalAnimationTest — 포즈 · 클립 샘플 · 코덱 · 블렌드 · 루트 모션 · 알림 · 동기 · 애니메이션 시스템(의존 순서 · LOD · 리더 포즈) · 애니메이터.

namespace
{
    struct TestSkeletalAnimationInternal
    {
        static bool isNear( const float3& expected, const float3& actual, float32 tolerance ) { return ( expected - actual ).getLength() <= tolerance; }

        /** @brief 두 회전이 같은 회전인지(부호 무관) 봅니다. */
        static bool isSameRotation( const quaternion& expected, const quaternion& actual, float32 tolerance )
        {
            return MathUtil::abs( MathUtil::abs( expected.dot( actual ) ) - 1.0f ) <= tolerance;
        }

        /**
         * @class RecordingTask
         * @brief 단계가 불린 순서를 적고, 기본 포즈 단계에서 첫 본을 정해진 값으로 돌리는 테스트용 일입니다.
         */
        class RecordingTask final : public IAnimationPhaseTask
        {
        public:
            RecordingTask( vector<const SkeletalMeshComponent*>* pOrder, float32 rootAngle )
                : _pOrder{ pOrder }
                , _rootAngle{ rootAngle }
                , _timeCallCount{ 0 }
                , _poseCallCount{ 0 }
                , _bActive{ true }
            {
            }

            bool isAnimationActive() const override { return _bActive; }
            void runAnimationPhase( AnimationPhase phase, SkeletalMeshComponent& unit, const AnimationFrameContext& context ) override
            {
                (void)context;
                if ( phase == AnimationPhase::Time )
                    ++_timeCallCount;
                if ( phase != AnimationPhase::BasePose )
                    return;
                ++_poseCallCount;
                if ( _pOrder != nullptr )
                    _pOrder->push_back( &unit );
                BoneTransform root = unit.getLocalPose().getBoneTransform( 0 );
                root._rotation     = quaternion::makeFromAxisAngle( float3{ 0.0f, 1.0f, 0.0f }, _rootAngle );
                unit.getLocalPose().setBoneTransform( 0, root );
            }

            vector<const SkeletalMeshComponent*>* _pOrder;
            float32                               _rootAngle;
            uint32                                _timeCallCount;
            uint32                                _poseCallCount;
            bool                                  _bActive;
        };

        /** @brief 사슬 스켈레톤을 쥔 유닛 하나를 만듭니다. */
        static SkeletalMeshComponent* createUnit( GameObjectManager& manager, const utf8* pName, uint32 boneCount )
        {
            GameObject* pObject = manager.createGameObject( hashed_string( pName ) );
            if ( pObject == nullptr )
                return nullptr;
            SkeletalMeshComponent* pUnit = pObject->addComponent<SkeletalMeshComponent>();
            if ( pUnit != nullptr )
                pUnit->setSkeleton( make_shared<Skeleton>( test::makeChainSkeleton( boneCount ) ) );
            return pUnit;
        }
    };
} // namespace

/**
 * @brief [SkeletalAnimationTest] 클립 샘플링이 손으로 계산한 값과 같다 — 표본 사이는 이동 선형, 회전 nlerp(짧은 쪽)
 * @details 표본율 2 Hz, 표본 셋: 루트 이동 x = 0 · 1 · 3, 자식 Y 축 회전 0° · 90° · 180°. 0.25 초는 첫 구간 가운데라 x = 0.5, 회전 45°.
 *          0.75 초는 둘째 구간 가운데라 x = 2, 회전 135°. 범위 밖 시각은 끝 표본이다.
 */
SW_TEST_CASE( SkeletalAnimationTest, ClipSamplingMatchesHandComputedValues )
{
    AnimationRawClip raw;
    raw._sampleRate           = 2.0f;
    raw._sampleCount          = 3;
    raw._listTrackName        = { hashed_string( "root" ), hashed_string( "child" ) };
    raw._listTrackParent      = { -1, 0 };
    const float32 arrX[3]     = { 0.0f, 1.0f, 3.0f };
    const float32 arrAngle[3] = { 0.0f, MathUtil::kHalfPi, MathUtil::kPi };
    for ( uint32 sampleIndex = 0; sampleIndex < 3; ++sampleIndex )
    {
        raw._listSample.push_back( test::makeBoneTransform( float3{ arrX[sampleIndex], 0.0f, 0.0f } ) );
        raw._listSample.push_back(
            test::makeBoneTransform( float3{ 0.0f, 1.0f, 0.0f }, quaternion::makeFromAxisAngle( float3{ 0.0f, 1.0f, 0.0f }, arrAngle[sampleIndex] ) ) );
    }

    AnimationClip clip;
    clip.setName( hashed_string( "Hand" ) );
    SW_ASSERT_TRUE( clip.compressFrom( raw, RawAnimationCodec::getInstance(), AnimationCodecSettings{}, nullptr ) );
    SW_EXPECT_NEAR_EQUAL( 1.0f, clip.getDuration(), 1e-6f );

    Pose pose;
    SW_ASSERT_TRUE( clip.sampleTracks( 0.25f, pose ) );
    SW_EXPECT_NEAR_EQUAL( 0.5f, pose.getBoneTransform( 0 )._translation._x, 1e-5f );
    const quaternion expected45 = quaternion::makeFromAxisAngle( float3{ 0.0f, 1.0f, 0.0f }, MathUtil::kPi * 0.25f );
    SW_EXPECT_TRUE( TestSkeletalAnimationInternal::isSameRotation( expected45, pose.getBoneTransform( 1 )._rotation, 1e-5f ) );

    SW_ASSERT_TRUE( clip.sampleTracks( 0.75f, pose ) );
    SW_EXPECT_NEAR_EQUAL( 2.0f, pose.getBoneTransform( 0 )._translation._x, 1e-5f );
    const quaternion expected135 = quaternion::makeFromAxisAngle( float3{ 0.0f, 1.0f, 0.0f }, MathUtil::kPi * 0.75f );
    SW_EXPECT_TRUE( TestSkeletalAnimationInternal::isSameRotation( expected135, pose.getBoneTransform( 1 )._rotation, 1e-5f ) );

    SW_ASSERT_TRUE( clip.sampleTracks( 5.0f, pose ) );
    SW_EXPECT_NEAR_EQUAL( 3.0f, pose.getBoneTransform( 0 )._translation._x, 1e-5f );

    // 트랙 → 본 표: 스켈레톤에 없는 트랙은 -1, 있는 본만 옮긴다.
    Skeleton skeleton;
    (void)skeleton.addBone( hashed_string( "child" ), -1, BoneTransform{}, float4x4::Identity );
    vector<int32> listTrackToBone;
    clip.makeTrackToBoneMap( skeleton, listTrackToBone );
    SW_ASSERT_EQUAL( 2u, static_cast<uint32>( listTrackToBone.size() ) );
    SW_EXPECT_EQUAL( -1, listTrackToBone[0] );
    SW_EXPECT_EQUAL( 0, listTrackToBone[1] );
}

/**
 * @brief [SkeletalAnimationTest] 코덱 — Raw 는 오차 0, ACL 은 정밀도 기준(0.1 mm) 근처에서 1 mm 아래이고 Raw 보다 작다
 * @details 같은 잣대(`AnimationCodecRegistry::measureMaxError` — 모델 공간 가상 정점)로 두 코덱을 측정한다. 이름으로 찾는 레지스트리도 본다.
 */
SW_TEST_CASE( SkeletalAnimationTest, CodecRawVsACLMaxErrorUnderThreshold )
{
    const Skeleton         skeleton = test::makeChainSkeleton( 5 );
    const AnimationRawClip raw      = test::makeChainRawClip( skeleton, 61, 30.0f, 0.8f );

    SW_EXPECT_TRUE( AnimationCodecRegistry::findCodecByName( "ACL" ) == &ACLAnimationCodec::getInstance() );
    SW_EXPECT_TRUE( AnimationCodecRegistry::findCodecByName( "raw" ) == &RawAnimationCodec::getInstance() );
    SW_EXPECT_NULL( AnimationCodecRegistry::findCodecByName( "zip" ) );

    AnimationCodecSettings settings{};
    settings._precision     = 0.0001f;
    settings._shellDistance = 0.1f;
    vector<uint8>       rawBytes;
    AnimationCodecStats rawStats{};
    SW_ASSERT_TRUE( AnimationCodecRegistry::compressAndMeasure( RawAnimationCodec::getInstance(), raw, settings, rawBytes, rawStats ) );
    SW_EXPECT_TRUE( rawStats._maxError < 1e-5f );

    vector<uint8>       aclBytes;
    AnimationCodecStats aclStats{};
    SW_ASSERT_TRUE( AnimationCodecRegistry::compressAndMeasure( ACLAnimationCodec::getInstance(), raw, settings, aclBytes, aclStats ) );
    SW_EXPECT_TRUE_MSG( aclStats._maxError < 0.001f, ( string( "ACL max error " ) + to_string( aclStats._maxError ) ).c_str() );
    SW_EXPECT_TRUE( aclStats._compressedByteCount < rawStats._compressedByteCount );
    SW_EXPECT_TRUE( aclStats.computeRatio() > 2.0f );

    // ACL 블롭을 클립으로 실어도 같은 값이 나온다(정렬된 보관 · 코덱 번호).
    AnimationClip clip;
    SW_ASSERT_TRUE( clip.compressFrom( raw, ACLAnimationCodec::getInstance(), settings, nullptr ) );
    SW_EXPECT_TRUE( clip.getCodecID() == AnimationCodecID::ACL );
    Pose rawPose;
    Pose aclPose;
    raw.sample( 1.0f, rawPose );
    SW_ASSERT_TRUE( clip.sampleTracks( 1.0f, aclPose ) );
    SW_EXPECT_TRUE( TestSkeletalAnimationInternal::isNear( rawPose.getBoneTransform( 0 )._translation, aclPose.getBoneTransform( 0 )._translation, 0.001f ) );
    SW_EXPECT_TRUE( TestSkeletalAnimationInternal::isSameRotation( rawPose.getBoneTransform( 3 )._rotation, aclPose.getBoneTransform( 3 )._rotation, 1e-4f ) );
}

/**
 * @brief [SkeletalAnimationTest] 클립 파일 왕복 — 이름 · 트랙 · 반복 · 루트 모션 트랙 · 알림 · 커브 · 샘플이 남고, 깨진 바이트는 거절한다
 */
SW_TEST_CASE( SkeletalAnimationTest, ClipFileRoundTripsAndRejectsMalformedBytes )
{
    const Skeleton         skeleton = test::makeChainSkeleton( 3 );
    const AnimationRawClip raw      = test::makeChainRawClip( skeleton, 31, 30.0f, 0.5f );
    AnimationClip          clip;
    clip.setName( hashed_string( "Walk" ) );
    clip.setLooping( false );
    clip.setRootMotionTrack( 0 );
    clip.addNotify( AnimationNotifyEvent{ hashed_string( "FootL" ), 0.25f, 0.0f } );
    AnimationClipCurve curve{};
    curve._name    = hashed_string( "Speed" );
    curve._listKey = {
        AnimationClipCurveKey{0.0f, 0.0f},
        AnimationClipCurveKey{1.0f, 2.0f}
    };
    clip.addCurve( curve );
    SW_ASSERT_TRUE( clip.compressFrom( raw, ACLAnimationCodec::getInstance(), AnimationCodecSettings{}, nullptr ) );

    vector<uint8> bytes;
    clip.makeBytes( bytes );
    AnimationClip loaded;
    SW_ASSERT_TRUE( loaded.readFromBytes( bytes.data(), bytes.size(), "round trip" ) );
    SW_EXPECT_EQUAL( string( "Walk" ), string( loaded.getName().c_str() ) );
    SW_EXPECT_FALSE( loaded.isLoopingByDefault() );
    SW_EXPECT_EQUAL( 3u, loaded.getTrackCount() );
    SW_EXPECT_EQUAL( 0, loaded.getRootMotionTrack() );
    SW_ASSERT_EQUAL( 1u, static_cast<uint32>( loaded.getNotifyTrack().getEvents().size() ) );
    SW_EXPECT_NEAR_EQUAL( 0.25f, loaded.getNotifyTrack().getEvents()[0]._time, 1e-6f );
    const AnimationClipCurve* pCurve = loaded.findCurve( hashed_string( "Speed" ) );
    SW_ASSERT_NOT_NULL( pCurve );
    SW_EXPECT_NEAR_EQUAL( 1.0f, pCurve->evaluate( 0.5f ), 1e-5f );
    Pose original;
    Pose reloaded;
    SW_ASSERT_TRUE( clip.sampleTracks( 0.4f, original ) );
    SW_ASSERT_TRUE( loaded.sampleTracks( 0.4f, reloaded ) );
    SW_EXPECT_TRUE( TestSkeletalAnimationInternal::isNear( original.getBoneTransform( 0 )._translation, reloaded.getBoneTransform( 0 )._translation, 1e-6f ) );

    test::ScopedDefensiveTestLog expected( "malformed clip bytes are rejected" );
    AnimationClip                broken;
    SW_EXPECT_FALSE( broken.readFromBytes( bytes.data(), bytes.size() - 3, "truncated" ) );
    vector<uint8> wrongVersion = bytes;
    wrongVersion[4]            = static_cast<uint8>( AnimationClip::kVersion + 1 );
    SW_EXPECT_FALSE( broken.readFromBytes( wrongVersion.data(), wrongVersion.size(), "version" ) );
    vector<uint8> noMagic = bytes;
    noMagic[0]            = 'X';
    SW_EXPECT_FALSE( broken.readFromBytes( noMagic.data(), noMagic.size(), "magic" ) );
}

/**
 * @brief [SkeletalAnimationTest] 스켈레톤 JSON 왕복과 검증 — 모르는 키 · 앞에 없는 부모 · 없는 본을 가리키는 부착은 로드 오류다
 */
SW_TEST_CASE( SkeletalAnimationTest, SkeletonJSONRoundTripsAndValidates )
{
    Skeleton           skeleton = test::makeChainSkeleton( 3 );
    SkeletonAttachment attachment{};
    attachment._name           = "Sword";
    attachment._meshPath       = "game/x/models/hero/parts/sword.mesh";
    attachment._parentBone     = hashed_string( "bone2" );
    attachment._localTransform = test::makeBoneTransform( float3{ 0.0f, 0.1f, 0.0f } );
    skeleton.addAttachment( attachment );

    Skeleton loaded;
    SW_ASSERT_TRUE( loaded.parseJSON( skeleton.toJSON(), "round trip" ) );
    SW_EXPECT_EQUAL( 3u, loaded.getBoneCount() );
    SW_EXPECT_EQUAL( 1, loaded.getBone( 2 )._parentIndex );
    SW_EXPECT_NEAR_EQUAL( -1.0f, loaded.getBone( 1 )._inverseBind._42, 1e-5f );
    SW_ASSERT_EQUAL( 1u, static_cast<uint32>( loaded.getAttachments().size() ) );
    SW_EXPECT_TRUE( loaded.getAttachments()[0]._parentBone == hashed_string( "bone2" ) );

    test::ScopedDefensiveTestLog expected( "malformed skeleton data is rejected" );
    string                       unknownKey = skeleton.toJSON();
    unknownKey.replace( unknownKey.find( "\"parent\"" ), 8, "\"parnt\": 0, \"parent\"" );
    Skeleton broken;
    SW_EXPECT_FALSE( broken.parseJSON( unknownKey, "unknown key" ) );
    SW_EXPECT_EQUAL( 0u, broken.getBoneCount() );
    string badBone = skeleton.toJSON();
    badBone.replace( badBone.find( "\"bone2\"", badBone.find( "\"attachments\"" ) ), 7, "\"bone9\"" );
    SW_EXPECT_FALSE( broken.parseJSON( badBone, "unknown attachment bone" ) );
}

/**
 * @brief [SkeletalAnimationTest] 블렌드 가중치 — 크로스페이드는 가중치 비례, 본 마스크는 마스크 밖을 건드리지 않고, 가산은 가중치만큼 차이를 더한다
 */
SW_TEST_CASE( SkeletalAnimationTest, BlendWeightsMaskAndAdditive )
{
    Pose from;
    Pose to;
    from.resize( 2 );
    to.resize( 2 );
    to.setBoneTransform( 0, test::makeBoneTransform( float3{ 4.0f, 0.0f, 0.0f } ) );
    to.setBoneTransform( 1, test::makeBoneTransform( float3{ 0.0f, 8.0f, 0.0f }, quaternion::makeFromAxisAngle( float3{ 0.0f, 0.0f, 1.0f }, MathUtil::kHalfPi ) ) );

    Pose blended;
    Pose::blend( from, to, 0.25f, blended );
    SW_EXPECT_NEAR_EQUAL( 1.0f, blended.getBoneTransform( 0 )._translation._x, 1e-5f );
    SW_EXPECT_NEAR_EQUAL( 2.0f, blended.getBoneTransform( 1 )._translation._y, 1e-5f );
    // nlerp 의 각은 선형이 아니지만 0 과 90° 사이 · 45° 보다 작아야 한다.
    const float32 angle = 2.0f * MathUtil::acos( MathUtil::clamp( blended.getBoneTransform( 1 )._rotation._w, -1.0f, 1.0f ) );
    SW_EXPECT_TRUE( angle > 0.1f && angle < MathUtil::kPi * 0.25f );

    // 마스크: 본 1 만 섞는다.
    const float32 arrMask[2] = { 0.0f, 1.0f };
    Pose::blendMasked( from, to, 1.0f, arrMask, blended );
    SW_EXPECT_NEAR_EQUAL( 0.0f, blended.getBoneTransform( 0 )._translation._x, 1e-6f );
    SW_EXPECT_NEAR_EQUAL( 8.0f, blended.getBoneTransform( 1 )._translation._y, 1e-5f );

    // 가산: to - from 의 절반을 얹으면 이동은 절반, 회전은 45°.
    Pose additive;
    Pose::makeAdditive( to, from, additive );
    Pose layered = from;
    layered.applyAdditive( additive, 0.5f, nullptr );
    SW_EXPECT_NEAR_EQUAL( 2.0f, layered.getBoneTransform( 0 )._translation._x, 1e-5f );
    const quaternion expected45 = quaternion::makeFromAxisAngle( float3{ 0.0f, 0.0f, 1.0f }, MathUtil::kPi * 0.25f );
    SW_EXPECT_TRUE( TestSkeletalAnimationInternal::isSameRotation( expected45, layered.getBoneTransform( 1 )._rotation, 1e-4f ) );
    // 가중치 1 이면 정확히 to.
    Pose full = from;
    full.applyAdditive( additive, 1.0f, nullptr );
    SW_EXPECT_TRUE( TestSkeletalAnimationInternal::isSameRotation( to.getBoneTransform( 1 )._rotation, full.getBoneTransform( 1 )._rotation, 1e-5f ) );
}

/**
 * @brief [SkeletalAnimationTest] 루트 모션 — 반복 경계를 넘는 걸음도 실제로 지나간 거리(끝 - 이전 + 지금 - 시작)를 낸다
 * @details 루트가 +Z 로 1 m/s(길이 1 초). 0.75 → 1.25(감겨 0.25) 는 0.5 m 다. 감는 것을 빼먹으면 0.25 - 0.75 = -0.5 m 로 뒤로 간다.
 */
SW_TEST_CASE( SkeletalAnimationTest, RootMotionAcrossLoopBoundary )
{
    const Skeleton         skeleton = test::makeChainSkeleton( 2 );
    const AnimationRawClip raw      = test::makeChainRawClip( skeleton, 31, 30.0f, 0.0f );
    AnimationClip          clip;
    clip.setRootMotionTrack( 0 );
    SW_ASSERT_TRUE( clip.compressFrom( raw, RawAnimationCodec::getInstance(), AnimationCodecSettings{}, nullptr ) );

    AnimationClipCursor cursor;
    (void)cursor.advance( 0.75f, clip.getPlayLength(), true );
    const AnimationTimeStep step = cursor.advance( 0.5f, clip.getPlayLength(), true );
    SW_EXPECT_EQUAL( 1u, step._wrapCount );
    const BoneTransform delta = clip.computeRootMotionDelta( step );
    SW_EXPECT_NEAR_EQUAL( 0.5f, delta._translation._z, 1e-4f );

    // 한 걸음에 두 바퀴 반: 2.5 m.
    AnimationClipCursor     fast;
    const AnimationTimeStep longStep = fast.advance( 2.5f, clip.getPlayLength(), true );
    SW_EXPECT_NEAR_EQUAL( 2.5f, clip.computeRootMotionDelta( longStep )._translation._z, 1e-4f );
}

/**
 * @brief [SkeletalAnimationTest] 알림은 반복 경계를 넘어도 정확히 한 번 — 끝 근처(0.95 초)와 시작(0 초) 알림 모두
 * @details 0.9 → 1.1(감겨 0.1): 0.95 는 (이전, 끝] 에서, 0 은 [0, 지금] 에서 한 번씩. 첫 걸음은 시작 시각을 포함해 0 초 알림이 시작에서 울린다.
 */
SW_TEST_CASE( SkeletalAnimationTest, NotifyFiresExactlyOnceAcrossLoopBoundary )
{
    test::TestPlayable clip( 1.0f, true );
    clip.addNotify( "Start", 0.0f );
    clip.addNotify( "Late", 0.95f );

    AnimationPlayer player;
    player.play( &clip, true );
    vector<AnimationFiredNotify> listFired;
    player.update( 0.9f, &listFired );
    SW_ASSERT_EQUAL( 1u, static_cast<uint32>( listFired.size() ) );
    SW_EXPECT_TRUE( listFired[0]._name == hashed_string( "Start" ) );

    listFired.clear();
    player.update( 0.2f, &listFired );
    SW_ASSERT_EQUAL( 2u, static_cast<uint32>( listFired.size() ) );
    SW_EXPECT_TRUE( listFired[0]._name == hashed_string( "Late" ) );
    SW_EXPECT_TRUE( listFired[1]._name == hashed_string( "Start" ) );

    // 작은 걸음 백 번(한 바퀴 = 1 초 = 0.01 × 100)이면 각각 정확히 한 번.
    listFired.clear();
    for ( uint32 stepIndex = 0; stepIndex < 100; ++stepIndex )
    {
        player.update( 0.01f, &listFired );
    }
    uint32 lateCount  = 0;
    uint32 startCount = 0;
    for ( const AnimationFiredNotify& fired : listFired )
    {
        lateCount += fired._name == hashed_string( "Late" ) ? 1u : 0u;
        startCount += fired._name == hashed_string( "Start" ) ? 1u : 0u;
    }
    SW_EXPECT_EQUAL( 1u, lateCount );
    SW_EXPECT_EQUAL( 1u, startCount );
}

/**
 * @brief [SkeletalAnimationTest] 동기 그룹 — 가중치가 가장 큰 리더의 정규화 위치로 팔로워가 맞춰진다(길이가 달라도 같은 위상)
 */
SW_TEST_CASE( SkeletalAnimationTest, SyncGroupFollowsLeaderPhase )
{
    test::TestPlayable walk( 1.0f, true );
    test::TestPlayable run( 0.5f, true );
    AnimationPlayer    walkPlayer;
    AnimationPlayer    runPlayer;
    walkPlayer.play( &walk, true );
    runPlayer.play( &run, true );
    walkPlayer.update( 0.3f, nullptr );
    runPlayer.update( 0.05f, nullptr );

    AnimationPlayer* const arrPlayer[2] = { &runPlayer, &walkPlayer };
    const float32          arrWeight[2] = { 0.25f, 0.75f };
    SW_EXPECT_EQUAL( 1, AnimationSyncGroup::synchronize( arrPlayer, arrWeight, 2 ) );
    SW_EXPECT_NEAR_EQUAL( 0.3f, runPlayer.getCurrentNormalizedTime(), 1e-5f );
    SW_EXPECT_NEAR_EQUAL( 0.15f, runPlayer.getCurrentTime(), 1e-5f );
    SW_EXPECT_NEAR_EQUAL( 0.3f, walkPlayer.getCurrentTime(), 1e-5f ); // 리더는 그대로
}

/**
 * @brief [SkeletalAnimationTest] 애니메이션 시스템은 의존 순서(위 유닛 → 아래 유닛)로 평가하고, 고리는 오류로 알린다
 * @details C → B → A 의존을 등록 순서와 반대로 걸어도 A · B · C 순서로 기본 포즈 단계가 불린다(레벨 0 · 1 · 2). 고리를 만들면 hasDependencyCycle 이다.
 */
SW_TEST_CASE( SkeletalAnimationTest, SystemEvaluatesDependenciesInOrder )
{
    GameObjectManager      manager;
    SkeletalMeshComponent* pUnitC = TestSkeletalAnimationInternal::createUnit( manager, "C", 2 );
    SkeletalMeshComponent* pUnitB = TestSkeletalAnimationInternal::createUnit( manager, "B", 2 );
    SkeletalMeshComponent* pUnitA = TestSkeletalAnimationInternal::createUnit( manager, "A", 2 );
    SW_ASSERT_TRUE( pUnitA != nullptr && pUnitB != nullptr && pUnitC != nullptr );
    pUnitC->addAnimationDependency( pUnitB );
    pUnitB->addAnimationDependency( pUnitA );

    vector<const SkeletalMeshComponent*>         listOrder;
    TestSkeletalAnimationInternal::RecordingTask taskA( &listOrder, 0.1f );
    TestSkeletalAnimationInternal::RecordingTask taskB( &listOrder, 0.2f );
    TestSkeletalAnimationInternal::RecordingTask taskC( &listOrder, 0.3f );
    pUnitA->addAnimationPhaseTask( &taskA );
    pUnitB->addAnimationPhaseTask( &taskB );
    pUnitC->addAnimationPhaseTask( &taskC );

    AnimationSystem& system = manager.getAnimationSystem();
    system.evaluate( 0.016f );
    SW_EXPECT_FALSE( system.hasDependencyCycle() );
    SW_ASSERT_EQUAL( 3u, static_cast<uint32>( system.getLevels().size() ) );
    SW_ASSERT_EQUAL( 3u, static_cast<uint32>( listOrder.size() ) );
    SW_EXPECT_TRUE( listOrder[0] == pUnitA );
    SW_EXPECT_TRUE( listOrder[1] == pUnitB );
    SW_EXPECT_TRUE( listOrder[2] == pUnitC );
    // 팔레트는 포즈를 따른다 — 루트를 돌린 유닛의 팔레트는 단위가 아니다.
    SW_EXPECT_TRUE( MathUtil::abs( pUnitA->getSkinPalette()[0]._11 - 1.0f ) > 1e-4f );

    {
        test::ScopedDefensiveTestLog expected( "an animation dependency cycle is reported" );
        pUnitA->addAnimationDependency( pUnitC );
        system.evaluate( 0.016f );
    }
    SW_EXPECT_TRUE( system.hasDependencyCycle() );
    pUnitA->removeAnimationDependency( pUnitC );
    system.evaluate( 0.016f );
    SW_EXPECT_FALSE( system.hasDependencyCycle() );

    pUnitA->removeAnimationPhaseTask( &taskA );
    pUnitB->removeAnimationPhaseTask( &taskB );
    pUnitC->removeAnimationPhaseTask( &taskC );
}

/**
 * @brief [SkeletalAnimationTest] 애니메이션 LOD — 갱신 주기 2 면 포즈는 두 프레임에 한 번(시간 단계는 매 프레임), 화면 밖이면 포즈를 만들지 않고, 쉬는 유닛은 돌지 않는다
 */
SW_TEST_CASE( SkeletalAnimationTest, LODSkipsPoseEvaluation )
{
    GameObjectManager      manager;
    SkeletalMeshComponent* pUnit = TestSkeletalAnimationInternal::createUnit( manager, "Lod", 3 );
    SW_ASSERT_NOT_NULL( pUnit );
    TestSkeletalAnimationInternal::RecordingTask task( nullptr, 0.5f );
    pUnit->addAnimationPhaseTask( &task );
    AnimationSystem& system = manager.getAnimationSystem();
    system.evaluate( 0.016f ); // 처음은 포즈가 더러워 늘 만든다
    const uint32 baseCount = pUnit->getPoseEvaluationCount();

    pUnit->setUpdateRateDivisor( 2 );
    for ( uint32 frame = 0; frame < 4; ++frame )
    {
        system.evaluate( 0.016f );
    }
    SW_EXPECT_EQUAL( baseCount + 2u, pUnit->getPoseEvaluationCount() );
    SW_EXPECT_EQUAL( 5u, task._timeCallCount );

    pUnit->setUpdateRateDivisor( 1 );
    pUnit->setVisibleHint( false );
    for ( uint32 frame = 0; frame < 3; ++frame )
    {
        system.evaluate( 0.016f );
    }
    SW_EXPECT_EQUAL( baseCount + 2u, pUnit->getPoseEvaluationCount() );
    pUnit->setAnimateWhenOffscreen( true );
    system.evaluate( 0.016f );
    SW_EXPECT_EQUAL( baseCount + 3u, pUnit->getPoseEvaluationCount() );

    // 할 일이 없으면 쉰다 — 단계도 돌지 않는다.
    task._bActive          = false;
    const uint32 timeCalls = task._timeCallCount;
    system.evaluate( 0.016f );
    SW_EXPECT_EQUAL( 0u, system.getActiveUnitCount() );
    SW_EXPECT_EQUAL( timeCalls, task._timeCallCount );
    pUnit->removeAnimationPhaseTask( &task );
}

/**
 * @brief [SkeletalAnimationTest] 리더 포즈 — 팔로워는 리더의 로컬 포즈를 본 이름으로 옮겨 받고, 리더는 의존이 되어 먼저 평가된다
 */
SW_TEST_CASE( SkeletalAnimationTest, FollowerReadsLeaderPose )
{
    GameObjectManager      manager;
    SkeletalMeshComponent* pFollower = TestSkeletalAnimationInternal::createUnit( manager, "Follower", 2 );
    SkeletalMeshComponent* pLeader   = TestSkeletalAnimationInternal::createUnit( manager, "Leader", 3 );
    SW_ASSERT_TRUE( pFollower != nullptr && pLeader != nullptr );
    TestSkeletalAnimationInternal::RecordingTask task( nullptr, 0.7f );
    pLeader->addAnimationPhaseTask( &task );
    pFollower->setLeaderPose( pLeader );
    SW_EXPECT_TRUE( pFollower->findLeaderPose() == pLeader );

    manager.getAnimationSystem().evaluate( 0.016f );
    const quaternion expected = quaternion::makeFromAxisAngle( float3{ 0.0f, 1.0f, 0.0f }, 0.7f );
    SW_EXPECT_TRUE( TestSkeletalAnimationInternal::isSameRotation( expected, pFollower->getLocalPose().getBoneTransform( 0 )._rotation, 1e-5f ) );
    float4x4 leaderBone{};
    float4x4 followerBone{};
    SW_ASSERT_TRUE( pLeader->findBoneModelTransform( hashed_string( "bone1" ), leaderBone ) );
    SW_ASSERT_TRUE( pFollower->findBoneModelTransform( hashed_string( "bone1" ), followerBone ) );
    SW_EXPECT_NEAR_EQUAL( leaderBone._41, followerBone._41, 1e-5f );
    SW_EXPECT_NEAR_EQUAL( leaderBone._43, followerBone._43, 1e-5f );
    pLeader->removeAnimationPhaseTask( &task );
}

/**
 * @brief [SkeletalAnimationTest] 스켈레탈 애니메이터 — 클립 폴더의 클립을 상태로 재생하고, 시스템 평가가 유닛 포즈 · 커브 · 알림 · 루트 모션을 낸다
 * @details 클립 파일(`<폴더>/walk.animclip`)은 루트가 +Z 로 1 m/s · 알림 0.1 초 · 커브 Speed 0→2. 0.25 초 뒤 오브젝트가 0.25 m 움직이고 루트 본은
 *          시작 자리에 묶이며(루트 모션 뽑기), 알림이 한 번, 커브는 0.5 다.
 */
SW_TEST_CASE( SkeletalAnimationTest, AnimatorPlaysClipFromFolder )
{
    SW_ASSERT_TRUE( ResourcePaths::initialize() );
    const Skeleton         skeleton = test::makeChainSkeleton( 3 );
    const AnimationRawClip raw      = test::makeChainRawClip( skeleton, 31, 30.0f, 0.4f );
    AnimationClip          clip;
    clip.setName( hashed_string( "Walk" ) );
    clip.setRootMotionTrack( 0 );
    clip.addNotify( AnimationNotifyEvent{ hashed_string( "Step" ), 0.1f, 0.0f } );
    AnimationClipCurve curve{};
    curve._name    = hashed_string( "Speed" );
    curve._listKey = {
        AnimationClipCurveKey{0.0f, 0.0f},
        AnimationClipCurveKey{1.0f, 2.0f}
    };
    clip.addCurve( curve );
    SW_ASSERT_TRUE( clip.compressFrom( raw, ACLAnimationCodec::getInstance(), AnimationCodecSettings{}, nullptr ) );
    const string folder = test::makeTempPath( "animclips" );
    SW_ASSERT_TRUE( clip.saveToFile( FileUtil::joinPath( folder, "walk.animclip" ) ) );

    GameObjectManager manager;
    GameObject*       pObject = manager.createGameObject( hashed_string( "Hero" ) );
    SW_ASSERT_NOT_NULL( pObject );
    SkeletalMeshComponent*     pUnit     = pObject->addComponent<SkeletalMeshComponent>();
    SkeletalAnimatorComponent* pAnimator = pObject->addComponent<SkeletalAnimatorComponent>();
    SW_ASSERT_TRUE( pUnit != nullptr && pAnimator != nullptr );
    pUnit->setSkeleton( make_shared<Skeleton>( skeleton ) );
    pAnimator->setClipFolder( folder );
    pAnimator->setInitialState( "Walk" );
    pAnimator->setExtractRootMotion( true );
    pAnimator->dispatchBeginPlay();
    SW_EXPECT_TRUE( pAnimator->getCurrentStateName() == hashed_string( "Walk" ) );

    manager.getAnimationSystem().evaluate( 0.25f );
    manager.flushSceneTransforms();
    SW_EXPECT_NEAR_EQUAL( 0.25f, pUnit->getLocalPosition()._z, 1e-3f );
    SW_EXPECT_NEAR_EQUAL( 0.0f, pUnit->getLocalPose().getBoneTransform( 0 )._translation._z, 1e-4f ); // 루트 본은 시작 자리에 묶였다
    SW_ASSERT_EQUAL( 1u, static_cast<uint32>( pAnimator->getFiredNotifies().size() ) );
    SW_EXPECT_TRUE( pAnimator->getFiredNotifies()[0]._name == hashed_string( "Step" ) );
    SW_EXPECT_NEAR_EQUAL( 0.5f, pAnimator->getCurveValue( hashed_string( "Speed" ) ), 1e-4f );
    // 본 1 은 클립의 사인 회전을 받았다(레퍼런스와 다르다).
    Pose expected;
    raw.sample( 0.25f, expected );
    SW_EXPECT_TRUE( TestSkeletalAnimationInternal::isSameRotation( expected.getBoneTransform( 1 )._rotation, pUnit->getLocalPose().getBoneTransform( 1 )._rotation, 1e-3f ) );
    SW_EXPECT_FALSE( TestSkeletalAnimationInternal::isSameRotation( quaternion::Identity, pUnit->getLocalPose().getBoneTransform( 1 )._rotation, 1e-3f ) );
}

/**
 * @brief [SkeletalAnimationTest] 런타임에 정한 스켈레톤(`setSkeleton`)은 렌더 에셋을 다시 풀어도(`resolveRenderAssets`) 암묵 스켈레톤으로 덮이지 않는다 — 경로를 정하면 경로가 이긴다
 */
SW_TEST_CASE( SkeletalAnimationTest, RuntimeSkeletonSurvivesRenderAssetResolve )
{
    GameObjectManager      manager;
    SkeletalMeshComponent* pUnit = TestSkeletalAnimationInternal::createUnit( manager, "Runtime", 3 );
    SW_ASSERT_NOT_NULL( pUnit );
    SW_EXPECT_EQUAL( 3u, pUnit->getSkeleton().getBoneCount() );
    pUnit->resolveRenderAssets();
    SW_EXPECT_EQUAL( 3u, pUnit->getSkeleton().getBoneCount() );
    pUnit->setSkeletonPath( "" ); // 경로를 (빈 값으로) 정하면 런타임 스켈레톤을 놓는다
    SW_EXPECT_EQUAL( 1u, pUnit->getSkeleton().getBoneCount() );
}

/**
 * @brief [SkeletalAnimationTest] 페이드가 다른 페이드로 끊겨도 포즈가 튀지 않는다 — 끊긴 순간의 포즈에서 이어 섞인다
 * @details 루트 이동이 X 0 · 1 · 2 로 고정된 세 클립. A → B 페이드(0.2 초)의 반쯤(X ≈ 0.5)에서 C 로 넘어가면, 플레이어는 섞이던 한 칸을 버린다 —
 *          그대로 그리면 한 프레임에 X 가 0.5 에서 1 로 뛴다(애니메이션이 튄다). 끊긴 순간의 포즈를 새 페이드 동안 섞어 사라지게 하면 X 는 이어진다.
 */
SW_TEST_CASE( SkeletalAnimationTest, InterruptedCrossfadeContinuesFromThePreviousPose )
{
    SW_ASSERT_TRUE( ResourcePaths::initialize() );
    const Skeleton skeleton = test::makeChainSkeleton( 2 );
    const string   folder   = test::makeTempPath( "animclips_interrupt" );
    const utf8*    arrName[3]{ "A", "B", "C" };
    for ( uint32 clipIndex = 0; clipIndex < 3; ++clipIndex )
    {
        AnimationRawClip raw;
        raw._sampleRate  = 30.0f;
        raw._sampleCount = 31;
        for ( uint32 boneIndex = 0; boneIndex < skeleton.getBoneCount(); ++boneIndex )
        {
            raw._listTrackName.push_back( skeleton.getBone( boneIndex )._name );
            raw._listTrackParent.push_back( skeleton.getBone( boneIndex )._parentIndex );
        }
        for ( uint32 sampleIndex = 0; sampleIndex < raw._sampleCount; ++sampleIndex )
        {
            for ( uint32 boneIndex = 0; boneIndex < skeleton.getBoneCount(); ++boneIndex )
            {
                BoneTransform transform = skeleton.getBone( boneIndex )._referencePose;
                if ( boneIndex == 0 )
                    transform._translation = float3{ static_cast<float32>( clipIndex ), 0.0f, 0.0f };
                raw._listSample.push_back( transform );
            }
        }
        AnimationClip clip;
        clip.setName( hashed_string( arrName[clipIndex] ) );
        SW_ASSERT_TRUE( clip.compressFrom( raw, RawAnimationCodec::getInstance(), AnimationCodecSettings{}, nullptr ) );
        SW_ASSERT_TRUE( clip.saveToFile( FileUtil::joinPath( folder, StringUtil::toLower( arrName[clipIndex] ) + ".animclip" ) ) );
    }

    GameObjectManager manager;
    GameObject*       pObject = manager.createGameObject( hashed_string( "Dancer" ) );
    SW_ASSERT_NOT_NULL( pObject );
    SkeletalMeshComponent*     pUnit     = pObject->addComponent<SkeletalMeshComponent>();
    SkeletalAnimatorComponent* pAnimator = pObject->addComponent<SkeletalAnimatorComponent>();
    SW_ASSERT_TRUE( pUnit != nullptr && pAnimator != nullptr );
    pUnit->setSkeleton( make_shared<Skeleton>( skeleton ) );
    pAnimator->setClipFolder( folder );
    pAnimator->setInitialState( "A" );
    pAnimator->dispatchBeginPlay();
    manager.getAnimationSystem().evaluate( 0.05f );
    SW_EXPECT_NEAR_EQUAL( 0.0f, pUnit->getLocalPose().getBoneTransform( 0 )._translation._x, 1e-4f );

    // A → B 페이드의 가운데.
    SW_ASSERT_TRUE( pAnimator->play( hashed_string( "B" ), true, 0.2f ) );
    manager.getAnimationSystem().evaluate( 0.1f );
    const float32 beforeInterrupt = pUnit->getLocalPose().getBoneTransform( 0 )._translation._x;
    SW_EXPECT_NEAR_EQUAL( 0.5f, beforeInterrupt, 0.05f );

    // 끊고 C 로 — 다음 프레임(아주 짧게)의 포즈는 끊긴 순간과 거의 같아야 한다.
    SW_ASSERT_TRUE( pAnimator->play( hashed_string( "C" ), true, 0.2f ) );
    manager.getAnimationSystem().evaluate( 0.001f );
    const float32 afterInterrupt = pUnit->getLocalPose().getBoneTransform( 0 )._translation._x;
    SW_EXPECT_NEAR_EQUAL( beforeInterrupt, afterInterrupt, 0.05f );

    // 새 페이드가 끝나면 C 그대로다.
    for ( uint32 frameIndex = 0; frameIndex < 10; ++frameIndex )
    {
        manager.getAnimationSystem().evaluate( 0.05f );
    }
    SW_EXPECT_NEAR_EQUAL( 2.0f, pUnit->getLocalPose().getBoneTransform( 0 )._translation._x, 1e-3f );
}

/**
 * @brief [SkeletalAnimationTest] 제자리 걷기 클립의 발 속도 — 디딘 발(더 낮은 발)이 뒤로 1 m/s 로 미끄러지는 합성 클립에서 1 m/s 를 잰다
 * @details 앞 반 초는 왼발, 뒤 반 초는 오른발이 땅(y 0)에 있고 z 를 0.25 에서 -0.25 로 옮긴다. 들린 발(y 0.1)은 앞으로 간다.
 *          재생 속도는 이동 속도 / 발 속도를 범위로 자르고, 발 속도가 0 이면 1 이다.
 */
SW_TEST_CASE( SkeletalAnimationTest, LocomotionFootSpeedMatchesPlantedFoot )
{
    Skeleton skeleton;
    (void)skeleton.addBone( "root", -1, test::makeBoneTransform( float3{} ), float4x4::Identity );
    (void)skeleton.addBone( "foot.l", 0, test::makeBoneTransform( float3{ -0.1f, 0.0f, 0.0f } ), float4x4::Identity );
    (void)skeleton.addBone( "foot.r", 0, test::makeBoneTransform( float3{ 0.1f, 0.0f, 0.0f } ), float4x4::Identity );
    skeleton.computeInverseBindFromReference();

    AnimationRawClip raw;
    raw._sampleRate      = 30.0f;
    raw._sampleCount     = 31;
    raw._listTrackName   = { hashed_string( "root" ), hashed_string( "foot.l" ), hashed_string( "foot.r" ) };
    raw._listTrackParent = { -1, 0, 0 };
    for ( uint32 sampleIndex = 0; sampleIndex < raw._sampleCount; ++sampleIndex )
    {
        const float32 time        = static_cast<float32>( sampleIndex ) / raw._sampleRate;
        const bool    bLeftDown   = time < 0.5f;
        const float32 halfElapsed = bLeftDown ? time : time - 0.5f;
        const float32 plantedZ    = 0.25f - halfElapsed;
        const float32 swingZ      = -0.25f + halfElapsed;
        raw._listSample.push_back( test::makeBoneTransform( float3{} ) );
        raw._listSample.push_back( test::makeBoneTransform( float3{ -0.1f, bLeftDown ? 0.0f : 0.1f, bLeftDown ? plantedZ : swingZ } ) );
        raw._listSample.push_back( test::makeBoneTransform( float3{ 0.1f, bLeftDown ? 0.1f : 0.0f, bLeftDown ? swingZ : plantedZ } ) );
    }
    AnimationClip clip;
    clip.setName( hashed_string( "Walk" ) );
    SW_ASSERT_TRUE( clip.compressFrom( raw, RawAnimationCodec::getInstance(), AnimationCodecSettings{}, nullptr ) );

    const float32 footSpeed = LocomotionSpeedUtil::computeFootSpeed( clip, skeleton, "foot.l", "foot.r", LocomotionSpeedUtil::kDefaultSampleCount );
    SW_EXPECT_NEAR_EQUAL( 1.0f, footSpeed, 1e-3f );
    SW_EXPECT_NEAR_EQUAL( 0.0f, LocomotionSpeedUtil::computeFootSpeed( clip, skeleton, "foot.l", "toe.r", LocomotionSpeedUtil::kDefaultSampleCount ), 1e-6f );

    SW_EXPECT_NEAR_EQUAL( 1.5f, LocomotionSpeedUtil::computePlayRate( 1.5f, footSpeed, 0.5f, 2.0f ), 1e-3f );
    SW_EXPECT_NEAR_EQUAL( 2.0f, LocomotionSpeedUtil::computePlayRate( 5.0f, footSpeed, 0.5f, 2.0f ), 1e-3f );
    SW_EXPECT_NEAR_EQUAL( 1.0f, LocomotionSpeedUtil::computePlayRate( 5.0f, 0.0f, 0.5f, 2.0f ), 1e-6f );
}
