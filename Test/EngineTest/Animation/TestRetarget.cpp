#include "pch.h"

#include "Core/File/FileUtil.h"
#include "Core/Math/MathUtil.h"

#include "Engine/Animation/AnimClip.h"
#include "Engine/Animation/Codec/AnimCodec.h"
#include "Engine/Animation/Codec/Raw/RawAnimCodec.h"
#include "Engine/Animation/Pose.h"
#include "Engine/Animation/Retarget/PoseRetargeter.h"
#include "Engine/Animation/Retarget/RetargetProfile.h"
#include "Engine/Animation/Skeleton.h"
#include "Engine/Character/Fit/BodyShape.h"
#include "Engine/Object/Animation/AnimationSystem.h"
#include "Engine/Object/Component/3D/PoseRetargetComponent.h"
#include "Engine/Object/Component/3D/SkeletalMeshComponent.h"
#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManager.h"
#include "Engine/Resource/ResourceUtil.h"

#include "EngineTest/AnimationTestUtil.h"

#include "TestFramework/TestFramework.h"

using namespace sw;

// RetargetTest — 리타깃 프로필(데이터) · 모델 공간 회전 차이 옮기기 · 골반 높이 비 · 다리 IK 목표(발이 미끄러지지 않게) · 오프라인 굽기 · 런타임 컴포넌트,
// 그리고 KayKit 기사 클립을 본 비율을 건 해골 스켈레톤으로.

namespace
{
    struct TestRetargetInternal
    {
        static constexpr const utf8* kKnightSkeleton = "game/shooter3d/models/kaykit/knight/knight.skeleton.json";
        static constexpr const utf8* kMinionSkeleton = "game/shooter3d/models/kaykit/skeleton_minion/skeleton_minion.skeleton.json";
        static constexpr const utf8* kKnightWalk     = "game/shooter3d/models/kaykit/knight/clips/walking_a.animclip";
        static constexpr const utf8* kProfile        = "game/shooter3d/rigs/knight_to_minion.retarget.json";

        static bool isNear( const float3& expected, const float3& actual, float32 tolerance ) { return ( expected - actual ).getLength() <= tolerance; }

        /** @brief 다리 둘 — root → hips(y 1) → 허벅지(x ±0.2) → 정강이(y -0.45, 무릎 살짝 앞) → 발(y -0.45). */
        static Skeleton makeLegs()
        {
            Skeleton skeleton;
            (void)skeleton.addBone( "root", -1, test::makeBoneTransform( float3{} ), float4x4::Identity );
            (void)skeleton.addBone( "hips", 0, test::makeBoneTransform( float3{ 0.0f, 1.0f, 0.0f } ), float4x4::Identity );
            (void)skeleton.addBone( "upperleg.l", 1, test::makeBoneTransform( float3{ -0.2f, 0.0f, 0.0f } ), float4x4::Identity );
            (void)skeleton.addBone( "lowerleg.l", 2, test::makeBoneTransform( float3{ 0.0f, -0.45f, 0.03f } ), float4x4::Identity );
            (void)skeleton.addBone( "foot.l", 3, test::makeBoneTransform( float3{ 0.0f, -0.45f, -0.03f } ), float4x4::Identity );
            (void)skeleton.addBone( "upperleg.r", 1, test::makeBoneTransform( float3{ 0.2f, 0.0f, 0.0f } ), float4x4::Identity );
            (void)skeleton.addBone( "lowerleg.r", 5, test::makeBoneTransform( float3{ 0.0f, -0.45f, 0.03f } ), float4x4::Identity );
            (void)skeleton.addBone( "foot.r", 6, test::makeBoneTransform( float3{ 0.0f, -0.45f, -0.03f } ), float4x4::Identity );
            skeleton.computeInverseBindFromReference();
            return skeleton;
        }

        static hashed_string makeName( const utf8* pStem, const utf8* pSuffix )
        {
            string name = pStem;
            name += pSuffix;
            return hashed_string( name );
        }

        static RetargetProfile makeLegProfile( bool bIkGoal )
        {
            RetargetProfile profile;
            profile.setRootAndPelvis( "root", "root", "hips", "hips", RetargetTranslationMode::ScaleByPelvisHeight );
            const utf8* arrSide[] = { ".l", ".r" };
            for ( const utf8* pSide : arrSide )
            {
                RetargetChain chain{};
                chain._name = makeName( "Leg", pSide );
                for ( const utf8* pBone : { "upperleg", "lowerleg", "foot" } )
                {
                    chain._listSourceBone.push_back( makeName( pBone, pSide ) );
                    chain._listTargetBone.push_back( makeName( pBone, pSide ) );
                }
                chain._bIkGoal = bIkGoal ? SW_TRUE : SW_FALSE;
                profile.addChain( chain );
            }
            return profile;
        }

        /** @brief 다리를 @p scale 배로 늘리고 골반을 그만큼 올린 레퍼런스(본 비율)입니다. */
        static Pose makeLongLegReference( const Skeleton& skeleton, float32 scale, float32 legLength )
        {
            BoneProportion proportion;
            proportion.setBone( "upperleg.l", float3{ scale }, float3{} );
            proportion.setBone( "upperleg.r", float3{ scale }, float3{} );
            proportion.setBone( "hips", float3{ 1.0f }, float3{ 0.0f, legLength * ( scale - 1.0f ), 0.0f } );
            Pose reference;
            reference.setToReference( skeleton );
            proportion.applyToPose( skeleton, reference );
            return reference;
        }

        static void computeModel( const Skeleton& skeleton, const Pose& pose, vector<float4x4>& outListModel )
        {
            pose.computeModelSpace( skeleton.getParentIndices(), outListModel );
        }

        static void rotateLocal( Pose& inoutPose, uint32 bone, const float3& axis, float32 angle )
        {
            BoneTransform local = inoutPose.getBoneTransform( bone );
            local._rotation     = ( local._rotation * quaternion::createFromAxisAngle( axis, angle ) ).normalize();
            inoutPose.setBoneTransform( bone, local );
        }

        /** @brief 기본 포즈 단계에서 본 1 을 Z 축으로 돌리는 일입니다(애니메이터 대신). */
        class BendTask final : public IAnimationPhaseTask
        {
        public:
            bool isAnimationActive() const override { return true; }
            void runAnimationPhase( AnimationPhase phase, SkeletalMeshComponent& unit, const AnimationFrameContext& context ) override
            {
                (void)context;
                if ( phase == AnimationPhase::BasePose )
                    rotateLocal( unit.getLocalPose(), 1, float3::UnitZ, 0.6f );
            }
        };
    };
} // namespace

/**
 * @brief [RetargetTest] 프로필 데이터 — 리소스의 기사 → 해골 프로필을 읽고, 모르는 키 · 빠진 골반 · 겹친 사슬 · 모르는 이동 방법은 오류, 없는 본은 묶기 오류다
 */
SW_TEST_CASE( RetargetTest, ProfileParsesAndRejectsUnknownNames )
{
    SW_ASSERT_TRUE( ResourceUtil::initialize() );
    RetargetProfile profile;
    SW_ASSERT_TRUE( profile.loadFromResource( TestRetargetInternal::kProfile ) );
    SW_EXPECT_EQUAL( 8u, static_cast<uint32>( profile.getChains().size() ) );
    SW_EXPECT_TRUE( profile.getTargetPelvis() == hashed_string( "hips" ) );
    SW_EXPECT_TRUE( profile.getChains()[4]._bIkGoal == SW_TRUE );

    test::ScopedDefensiveTestLog expected( "malformed retarget profiles are rejected" );
    const string_view            listBad[] = {
        R"({ "root": { "source": "r", "target": "r" }, "pelvis": { "source": "h", "target": "h" }, "chains": [], "colour": 1 })",
        R"({ "root": { "source": "r", "target": "r" }, "chains": [] })",
        R"({ "root": { "source": "r", "target": "r" }, "pelvis": { "source": "h", "target": "h" }, "chains": [ { "name": "A", "source": ["x"], "target": ["x"] }, { "name": "A", "source": ["y"], "target": ["y"] } ] })",
        R"({ "root": { "source": "r", "target": "r" }, "pelvis": { "source": "h", "target": "h" }, "translation": "Stretch", "chains": [] })",
    };
    for ( const string_view bad : listBad )
    {
        RetargetProfile broken;
        SW_EXPECT_FALSE( broken.parseJson( bad, "bad.retarget.json" ) );
    }
    RetargetProfile missingBone;
    SW_ASSERT_TRUE( missingBone.parseJson( R"({ "root": { "source": "bone0", "target": "bone0" }, "pelvis": { "source": "bone0", "target": "bone0" },
        "chains": [ { "name": "Arm", "source": ["bone1"], "target": ["elbow"] } ] })",
                                           "missing.retarget.json" ) );
    const Skeleton chain = test::makeChainSkeleton( 3 );
    PoseRetargeter retargeter;
    SW_EXPECT_FALSE( retargeter.initialize( missingBone, chain, chain, nullptr ) );
}

/**
 * @brief [RetargetTest] 회전은 모델 공간 차이로 옮긴다 — 대상의 로컬 축 약속이 달라도(본 1 의 레퍼런스가 X 축으로 90° 돌아 있음) 같은 굽힘이 같은 자리를 낸다
 */
SW_TEST_CASE( RetargetTest, RotationTransfersAcrossLocalAxisConventions )
{
    const Skeleton   source = test::makeChainSkeleton( 3 );
    const quaternion turned = quaternion::createFromAxisAngle( float3::UnitX, MathUtil::kHalfPi );
    Skeleton         target;
    (void)target.addBone( "bone0", -1, test::makeBoneTransform( float3{} ), float4x4::Identity );
    (void)target.addBone( "bone1", 0, test::makeBoneTransform( float3{ 0.0f, 1.0f, 0.0f }, turned ), float4x4::Identity );
    (void)target.addBone( "bone2", 1, test::makeBoneTransform( float3::transform( float3{ 0.0f, 1.0f, 0.0f }, RigIkSolver::makeInverse( turned ) ) ), float4x4::Identity );
    target.computeInverseBindFromReference();

    RetargetProfile profile;
    profile.setRootAndPelvis( "bone0", "bone0", "bone0", "bone0", RetargetTranslationMode::Copy );
    RetargetChain chain{};
    chain._name           = hashed_string( "Arm" );
    chain._listSourceBone = { hashed_string( "bone1" ), hashed_string( "bone2" ) };
    chain._listTargetBone = chain._listSourceBone;
    profile.addChain( chain );
    PoseRetargeter retargeter;
    SW_ASSERT_TRUE( retargeter.initialize( profile, source, target, nullptr ) );

    Pose sourcePose;
    sourcePose.setToReference( source );
    TestRetargetInternal::rotateLocal( sourcePose, 1, float3::UnitZ, 0.7f );
    Pose targetPose;
    retargeter.retarget( sourcePose, targetPose );
    vector<float4x4> listSource;
    vector<float4x4> listTarget;
    TestRetargetInternal::computeModel( source, sourcePose, listSource );
    TestRetargetInternal::computeModel( target, targetPose, listTarget );
    SW_EXPECT_TRUE( TestRetargetInternal::isNear( listSource[2].getTranslation(), listTarget[2].getTranslation(), 1e-4f ) );
    // 로컬을 그대로 베꼈다면 대상 본 2 는 X 축 둘레로 돈 엉뚱한 자리다 — 위 단언이 그 차이를 본다.
    SW_EXPECT_TRUE( listSource[2].getTranslation()._x < -0.5f );
}

/**
 * @brief [RetargetTest] 다리가 1.5 배 긴 스켈레톤(본 비율) — 골반 · 발 움직임이 골반 높이 비만큼 커지고 IK 목표가 발을 그 자리에 정확히 둔다. IK 를 끄면 어긋난다
 */
SW_TEST_CASE( RetargetTest, LongerLegsKeepFeetOnScaledStride )
{
    const Skeleton legs      = TestRetargetInternal::makeLegs();
    const Pose     reference = TestRetargetInternal::makeLongLegReference( legs, 1.5f, 0.9f );
    Pose           sourcePose;
    sourcePose.setToReference( legs );
    BoneTransform hips = sourcePose.getBoneTransform( 1 );
    hips._translation  = hips._translation + float3{ 0.0f, -0.05f, 0.3f };
    sourcePose.setBoneTransform( 1, hips );
    TestRetargetInternal::rotateLocal( sourcePose, 2, float3::UnitX, -0.5f );
    TestRetargetInternal::rotateLocal( sourcePose, 3, float3::UnitX, 0.6f );
    TestRetargetInternal::rotateLocal( sourcePose, 5, float3::UnitX, 0.3f );
    vector<float4x4> listSourceReference;
    vector<float4x4> listSource;
    Pose             sourceReference;
    sourceReference.setToReference( legs );
    TestRetargetInternal::computeModel( legs, sourceReference, listSourceReference );
    TestRetargetInternal::computeModel( legs, sourcePose, listSource );
    vector<float4x4> listTargetReference;
    TestRetargetInternal::computeModel( legs, reference, listTargetReference );

    float32 arrError[2]{};
    for ( uint32 ik = 0; ik < 2; ++ik )
    {
        PoseRetargeter retargeter;
        SW_ASSERT_TRUE( retargeter.initialize( TestRetargetInternal::makeLegProfile( ik == 1 ), legs, legs, &reference ) );
        SW_EXPECT_NEAR_EQUAL( 1.45f, retargeter.getHeightRatio(), 1e-4f );
        Pose targetPose;
        retargeter.retarget( sourcePose, targetPose );
        vector<float4x4> listTarget;
        TestRetargetInternal::computeModel( legs, targetPose, listTarget );
        const float32 ratio = retargeter.getHeightRatio();
        // 골반: 레퍼런스에서 움직인 만큼 × 비.
        const float3 hipsGoal = listTargetReference[1].getTranslation() + ( listSource[1].getTranslation() - listSourceReference[1].getTranslation() ) * ratio;
        SW_EXPECT_TRUE( TestRetargetInternal::isNear( hipsGoal, listTarget[1].getTranslation(), 1e-4f ) );
        for ( const uint32 foot : { 4u, 7u } )
        {
            const float3 footGoal = listTargetReference[foot].getTranslation() + ( listSource[foot].getTranslation() - listSourceReference[foot].getTranslation() ) * ratio;
            arrError[ik]          = MathUtil::max( arrError[ik], ( footGoal - listTarget[foot].getTranslation() ).getLength() );
        }
    }
    SW_EXPECT_TRUE( arrError[1] < 2e-3f ); // IK 목표 — 발이 보폭 비에 맞게 선다
    SW_EXPECT_TRUE( arrError[0] > 1e-2f ); // 회전만 옮기면 긴 다리의 발은 다른 자리에 간다(미끄러짐)
}

/**
 * @brief [RetargetTest] 오프라인 굽기 — 구운 클립을 샘플한 포즈가 런타임 리타깃과 같고, 알림 · 커브 · 반복 · 루트 모션 트랙이 대상으로 옮겨진다
 */
SW_TEST_CASE( RetargetTest, BakedClipMatchesRuntimeRetarget )
{
    const Skeleton    source = test::makeChainSkeleton( 3 );
    const AnimRawClip raw    = test::makeChainRawClip( source, 31, 30.0f, 0.4f );
    AnimClip          clip;
    clip.setName( "Wave" );
    clip.setLooping( true );
    clip.setRootMotionTrack( 0 );
    clip.addNotify( AnimNotifyEvent{ hashed_string( "Step" ), 0.5f, 0.0f } );
    AnimCurve curve{};
    curve._name    = hashed_string( "Ik" );
    curve._listKey = {
        AnimCurveKey{0.0f, 0.0f},
        AnimCurveKey{1.0f, 1.0f}
    };
    clip.addCurve( curve );
    SW_ASSERT_TRUE( clip.compressFrom( raw, RawAnimCodec::getInstance(), AnimCodecSettings{}, nullptr ) );

    // 대상: 같은 이름, 본 1 이 두 배 긴 레퍼런스(본 비율 덮어쓰기).
    Pose reference;
    reference.setToReference( source );
    BoneTransform longer = reference.getBoneTransform( 2 );
    longer._translation  = longer._translation * 2.0f;
    reference.setBoneTransform( 2, longer );
    RetargetProfile profile;
    profile.setRootAndPelvis( "bone0", "bone0", "bone1", "bone1", RetargetTranslationMode::ScaleByPelvisHeight );
    RetargetChain chain{};
    chain._name           = hashed_string( "Tail" );
    chain._listSourceBone = { hashed_string( "bone1" ), hashed_string( "bone2" ) };
    chain._listTargetBone = chain._listSourceBone;
    profile.addChain( chain );
    PoseRetargeter retargeter;
    SW_ASSERT_TRUE( retargeter.initialize( profile, source, source, &reference ) );

    AnimClip baked;
    SW_ASSERT_TRUE( RetargetBakeUtil::bakeClip( retargeter, clip, source, source, 30.0f, RawAnimCodec::getInstance(), AnimCodecSettings{}, baked ) );
    SW_EXPECT_NEAR_EQUAL( clip.getDuration(), baked.getDuration(), 1e-4f );
    SW_EXPECT_TRUE( baked.isLoopingByDefault() );
    SW_EXPECT_EQUAL( 0, baked.getRootMotionTrack() );
    SW_EXPECT_EQUAL( 1u, static_cast<uint32>( baked.getNotifyTrack().getEvents().size() ) );
    SW_ASSERT_NOT_NULL( baked.findCurve( "Ik" ) );

    vector<int32> listTrackToBone;
    clip.makeTrackToBoneMap( source, listTrackToBone );
    for ( const float32 time : { 0.0f, 1.0f / 3.0f, 0.5f, 1.0f } )
    {
        Pose trackPose;
        Pose sourcePose;
        sourcePose.setToReference( source );
        SW_ASSERT_TRUE( clip.sampleTracks( time, trackPose ) );
        AnimClip::copyTracksToPose( trackPose, listTrackToBone, sourcePose );
        Pose runtime;
        retargeter.retarget( sourcePose, runtime );
        Pose bakedPose;
        SW_ASSERT_TRUE( baked.sampleTracks( time, bakedPose ) );
        for ( uint32 bone = 0; bone < 3; ++bone )
        {
            SW_EXPECT_TRUE( TestRetargetInternal::isNear( runtime.getBoneTransform( bone )._translation, bakedPose.getBoneTransform( bone )._translation, 1e-4f ) );
            SW_EXPECT_NEAR_EQUAL( 1.0f, MathUtil::abs( runtime.getBoneTransform( bone )._rotation.dot( bakedPose.getBoneTransform( bone )._rotation ) ), 1e-4f );
        }
    }
}

/**
 * @brief [RetargetTest] KayKit — 기사 걷기 클립을 다리를 1.25 배 늘린(본 비율) 해골 하수인에게. 골반 높이 비가 1 보다 크고, 프레임마다 발이 보폭 비의 자리에 선다
 */
SW_TEST_CASE( RetargetTest, KnightWalkOnProportionedMinion )
{
    SW_ASSERT_TRUE( ResourceUtil::initialize() );
    Skeleton knight;
    Skeleton minion;
    SW_ASSERT_TRUE( knight.loadFromResource( TestRetargetInternal::kKnightSkeleton ) );
    SW_ASSERT_TRUE( minion.loadFromResource( TestRetargetInternal::kMinionSkeleton ) );
    RetargetProfile profile;
    SW_ASSERT_TRUE( profile.loadFromResource( TestRetargetInternal::kProfile ) );
    AnimClip walk;
    SW_ASSERT_TRUE( walk.loadFromResource( TestRetargetInternal::kKnightWalk ) );

    // 하수인의 다리를 1.25 배 — 허벅지 스케일, 골반은 다리 길이의 0.25 만큼 올린다(발이 바닥에 남게).
    Pose minionReference;
    minionReference.setToReference( minion );
    vector<float4x4> listMinionRest;
    minionReference.computeModelSpace( minion.getParentIndices(), listMinionRest );
    const int32    hipBone   = minion.findBoneIndex( "upperleg.l" );
    const int32    footBone  = minion.findBoneIndex( "foot.l" );
    const float32  legLength = listMinionRest[static_cast<size_t>( hipBone )].getTranslation()._y - listMinionRest[static_cast<size_t>( footBone )].getTranslation()._y;
    BoneProportion proportion;
    proportion.setBone( "upperleg.l", float3{ 1.25f }, float3{} );
    proportion.setBone( "upperleg.r", float3{ 1.25f }, float3{} );
    proportion.setBone( "hips", float3{ 1.0f }, float3{ 0.0f, legLength * 0.25f, 0.0f } );
    proportion.applyToPose( minion, minionReference );

    PoseRetargeter retargeter;
    SW_ASSERT_TRUE( retargeter.initialize( profile, knight, minion, &minionReference ) );
    SW_EXPECT_TRUE( retargeter.getHeightRatio() > 1.05f );

    vector<float4x4> listKnightRest;
    vector<float4x4> listMinionScaledRest;
    Pose             knightReference;
    knightReference.setToReference( knight );
    knightReference.computeModelSpace( knight.getParentIndices(), listKnightRest );
    minionReference.computeModelSpace( minion.getParentIndices(), listMinionScaledRest );
    vector<int32> listTrackToBone;
    walk.makeTrackToBoneMap( knight, listTrackToBone );
    const int32 arrFoot[2] = { knight.findBoneIndex( "foot.l" ), knight.findBoneIndex( "foot.r" ) };
    float32     maxError   = 0.0f;
    for ( uint32 sample = 0; sample < 12; ++sample )
    {
        const float32 time = walk.getDuration() * static_cast<float32>( sample ) / 12.0f;
        Pose          trackPose;
        Pose          knightPose = knightReference;
        SW_ASSERT_TRUE( walk.sampleTracks( time, trackPose ) );
        AnimClip::copyTracksToPose( trackPose, listTrackToBone, knightPose );
        Pose minionPose;
        retargeter.retarget( knightPose, minionPose );
        vector<float4x4> listKnight;
        vector<float4x4> listMinion;
        knightPose.computeModelSpace( knight.getParentIndices(), listKnight );
        minionPose.computeModelSpace( minion.getParentIndices(), listMinion );
        for ( const int32 foot : arrFoot )
        {
            const size_t bone = static_cast<size_t>( foot ); // 두 스켈레톤의 본 순서가 같다(KayKit 같은 리그)
            const float3 goal = listMinionScaledRest[bone].getTranslation() +
                                ( listKnight[bone].getTranslation() - listKnightRest[bone].getTranslation() ) * retargeter.getHeightRatio();
            maxError = MathUtil::max( maxError, ( goal - listMinion[bone].getTranslation() ).getLength() );
        }
    }
    SW_EXPECT_TRUE( maxError < 3e-3f );
}

/**
 * @brief [RetargetTest] 런타임 컴포넌트 — 원본 유닛이 의존이 되어 먼저 평가되고, 이 유닛의 기본 포즈가 리타기터 출력과 같다
 */
SW_TEST_CASE( RetargetTest, ComponentFollowsSourceUnit )
{
    GameObjectManager manager;
    GameObject*       pSourceObject = manager.createGameObject( "Source" );
    GameObject*       pTargetObject = manager.createGameObject( "Target" );
    SW_ASSERT_TRUE( pSourceObject != nullptr && pTargetObject != nullptr );
    SkeletalMeshComponent* pSource = pSourceObject->addComponent<SkeletalMeshComponent>();
    SkeletalMeshComponent* pTarget = pTargetObject->addComponent<SkeletalMeshComponent>();
    SW_ASSERT_TRUE( pSource != nullptr && pTarget != nullptr );
    const Skeleton chain = test::makeChainSkeleton( 3 );
    pSource->setSkeleton( make_shared<Skeleton>( chain ) );
    pTarget->setSkeleton( make_shared<Skeleton>( chain ) );
    TestRetargetInternal::BendTask bend;
    pSource->addAnimationPhaseTask( &bend );

    RetargetProfile profile;
    profile.setRootAndPelvis( "bone0", "bone0", "bone0", "bone0", RetargetTranslationMode::Copy );
    RetargetChain arm{};
    arm._name           = hashed_string( "Arm" );
    arm._listSourceBone = { hashed_string( "bone1" ), hashed_string( "bone2" ) };
    arm._listTargetBone = arm._listSourceBone;
    profile.addChain( arm );
    PoseRetargetComponent* pRetarget = pTargetObject->addComponent<PoseRetargetComponent>();
    SW_ASSERT_NOT_NULL( pRetarget );
    pRetarget->setProfile( profile );
    pRetarget->setSource( pSource );
    pRetarget->dispatchBeginPlay();

    AnimationSystem& system = manager.getAnimationSystem();
    system.evaluate( 1.0f / 60.0f );
    SW_ASSERT_EQUAL( 2u, static_cast<uint32>( system.getLevels().size() ) );
    SW_EXPECT_TRUE( system.getLevels()[0][0] == pSource );
    SW_EXPECT_NEAR_EQUAL( 1.0f, MathUtil::abs( pSource->getLocalPose().getBoneTransform( 1 )._rotation.dot( pTarget->getLocalPose().getBoneTransform( 1 )._rotation ) ), 1e-5f );
    float4x4 sourceTip{};
    float4x4 targetTip{};
    SW_ASSERT_TRUE( pSource->findBoneModelTransform( "bone2", sourceTip ) );
    SW_ASSERT_TRUE( pTarget->findBoneModelTransform( "bone2", targetTip ) );
    SW_EXPECT_TRUE( TestRetargetInternal::isNear( sourceTip.getTranslation(), targetTip.getTranslation(), 1e-4f ) );
    SW_EXPECT_TRUE( sourceTip.getTranslation()._x < -0.3f ); // 굽힘이 실제로 옮겨졌다
    pSource->removeAnimationPhaseTask( &bend );
}

/**
 * @brief [RetargetTest] 파일 굽기(`App --bake-retarget` 의 몸) — 기사 걷기를 프로필의 해골 하수인 스켈레톤 트랙으로 구워 쓰고, 다시 읽은 클립이 같은 길이 · 하수인 본 이름 트랙이다
 */
SW_TEST_CASE( RetargetTest, BakeClipFileWritesTargetClip )
{
    SW_ASSERT_TRUE( ResourceUtil::initialize() );
    const string outPath = FileUtil::joinPath( test::makeTempPath( "retargetbake" ), "walking_a_minion.animclip" );
    SW_ASSERT_TRUE( RetargetBakeUtil::bakeClipFile( TestRetargetInternal::kProfile, TestRetargetInternal::kKnightWalk, outPath ) );
    AnimClip baked;
    AnimClip source;
    Skeleton minion;
    SW_ASSERT_TRUE( baked.loadFromResource( outPath ) );
    SW_ASSERT_TRUE( source.loadFromResource( TestRetargetInternal::kKnightWalk ) );
    SW_ASSERT_TRUE( minion.loadFromResource( TestRetargetInternal::kMinionSkeleton ) );
    SW_EXPECT_NEAR_EQUAL( source.getDuration(), baked.getDuration(), 1e-3f );
    SW_ASSERT_EQUAL( minion.getBoneCount(), baked.getTrackCount() );
    SW_EXPECT_TRUE( baked.getTrackNames()[1] == minion.getBone( 1 )._name );
    SW_EXPECT_TRUE( baked.getCodecId() == source.getCodecId() );
}
