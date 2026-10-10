#include "pch.h"

#include "Core/File/FileUtil.h"
#include "Core/Math/MathUtil.h"

#include "Engine/Animation/AnimClip.h"
#include "Engine/Animation/Codec/ACL/ACLAnimCodec.h"
#include "Engine/Animation/Codec/Raw/RawAnimCodec.h"
#include "Engine/Animation/Skeletal/Pose.h"
#include "Engine/Animation/Skeletal/Skeleton.h"
#include "Engine/Animation/Skeletal/SkeletonBoneLOD.h"
#include "Engine/Object/Animation/AnimationLOD.h"
#include "Engine/Object/Animation/AnimationSystem.h"
#include "Engine/Object/Component/3D/SkeletalAnimatorComponent.h"
#include "Engine/Object/Component/3D/SkeletalMeshComponent.h"
#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManager.h"
#include "Engine/Resource/ResourceUtil.h"

#include "EngineTest/AnimationTestUtil.h"

#include "TestFramework/TestFramework.h"

using namespace sw;

// AnimationLODTest — 화면 크기 · 표 · 예산 배분 · 본 LOD · 뷰 가시성(여러 뷰) · 갱신 주기와 보간.

namespace
{
    struct TestAnimationLODInternal
    {
        /** @brief 원점에서 +Z 를 보는 시야각 90° · 비율 1 의 뷰입니다(투영 y 배율 = 1). */
        static AnimationLODView makeForwardView( const float3& eye, const float3& target )
        {
            const float4x4 view       = float4x4::createLookAt( eye, target, float3{ 0.0f, 1.0f, 0.0f } );
            const float4x4 projection = float4x4::createPerspectiveFieldOfView( MathUtil::kPi * 0.5f, 1.0f, 0.1f, 1000.0f );
            return AnimationLODView::make( view * projection, eye );
        }

        /**
         * @class CountingTask
         * @brief 기본 포즈 단계마다 루트를 Y 축으로 `_step` 씩 더 돌리는 시험용 일입니다(평가할 때마다 포즈가 달라진다).
         */
        class CountingTask final : public IAnimationPhaseTask
        {
        public:
            explicit CountingTask( float32 step )
                : _step{ step }
                , _poseCallCount{ 0 }
            {
            }

            bool isAnimationActive() const override { return true; }
            void runAnimationPhase( AnimationPhase phase, SkeletalMeshComponent& unit, const AnimationFrameContext& context ) override
            {
                (void)context;
                if ( phase != AnimationPhase::BasePose )
                    return;
                ++_poseCallCount;
                BoneTransform root = unit.getLocalPose().getBoneTransform( 0 );
                root._rotation     = quaternion::createFromAxisAngle( float3{ 0.0f, 1.0f, 0.0f }, _step * static_cast<float32>( _poseCallCount ) );
                unit.getLocalPose().setBoneTransform( 0, root );
            }

            float32 _step;
            uint32  _poseCallCount;
        };

        /** @brief 사슬 스켈레톤 유닛을 @p position 에 둡니다. */
        static SkeletalMeshComponent* createUnit( GameObjectManager& manager, const utf8* pName, uint32 boneCount, const float3& position )
        {
            GameObject* pObject = manager.createGameObject( hashed_string( pName ) );
            if ( pObject == nullptr )
                return nullptr;
            SkeletalMeshComponent* pUnit = pObject->addComponent<SkeletalMeshComponent>();
            if ( pUnit == nullptr )
                return nullptr;
            pUnit->setSkeleton( make_shared<Skeleton>( test::makeChainSkeleton( boneCount ) ) );
            pUnit->setLocalPosition( position );
            return pUnit;
        }

        /** @brief 루트 본의 Y 축 회전각(라디안)입니다. */
        static float32 getRootYaw( const SkeletalMeshComponent& unit )
        {
            const quaternion rotation = unit.getLocalPose().getBoneTransform( 0 )._rotation;
            return 2.0f * MathUtil::atan2( rotation._y, rotation._w );
        }

        /** @brief 두 단계 표(화면 0.3 이상 매 프레임, 그 아래 4 프레임마다 · 보간 @p bInterpolate)를 만듭니다. */
        static AnimationLODSettings makeTwoLevelSettings( bool bInterpolate )
        {
            AnimationLODSettings settings{};
            settings._listRateLevel.push_back( AnimationLODRateLevel{ 0.3f, 1u, static_cast<uint8>( SW_FALSE ) } );
            settings._listRateLevel.push_back( AnimationLODRateLevel{ 0.0f, 4u, static_cast<uint8>( bInterpolate ? SW_TRUE : SW_FALSE ) } );
            return settings;
        }
    };
} // namespace

/**
 * @brief [AnimationLODTest] 화면 크기 = 반지름 × 투영 y 배율 / w — 시야각 90° 에서 10 m 앞 반지름 1 구는 0.1, 20 m 는 0.05, 뒤는 0(절두체 밖)
 * @details 직교 투영(높이 10)은 거리와 상관없이 반지름 × 2 / 10 이다. 눈이 구 안이면 1 이다.
 */
SW_TEST_CASE( AnimationLODTest, ScreenSizeFromViewProjection )
{
    const AnimationLODView view = TestAnimationLODInternal::makeForwardView( float3{ 0.0f, 0.0f, 0.0f }, float3{ 0.0f, 0.0f, 1.0f } );
    SW_EXPECT_NEAR_EQUAL( 0.1f, AnimationLODUtil::computeScreenSize( view, float3{ 0.0f, 0.0f, 10.0f }, 1.0f ), 1e-3f );
    SW_EXPECT_NEAR_EQUAL( 0.05f, AnimationLODUtil::computeScreenSize( view, float3{ 0.0f, 0.0f, 20.0f }, 1.0f ), 1e-3f );
    SW_EXPECT_NEAR_EQUAL( 0.0f, AnimationLODUtil::computeScreenSize( view, float3{ 0.0f, 0.0f, -10.0f }, 1.0f ), 1e-6f );
    SW_EXPECT_NEAR_EQUAL( 0.0f, AnimationLODUtil::computeScreenSize( view, float3{ 50.0f, 0.0f, 10.0f }, 1.0f ), 1e-6f );
    SW_EXPECT_NEAR_EQUAL( 1.0f, AnimationLODUtil::computeScreenSize( view, float3{ 0.0f, 0.0f, 0.5f }, 1.0f ), 1e-6f );

    const float4x4         lookAt = float4x4::createLookAt( float3{ 0.0f, 0.0f, 0.0f }, float3{ 0.0f, 0.0f, 1.0f }, float3{ 0.0f, 1.0f, 0.0f } );
    const AnimationLODView ortho  = AnimationLODView::make( lookAt * float4x4::createOrthographic( 10.0f, 10.0f, 0.1f, 1000.0f ), float3{} );
    SW_EXPECT_NEAR_EQUAL( 0.2f, AnimationLODUtil::computeScreenSize( ortho, float3{ 0.0f, 0.0f, 10.0f }, 1.0f ), 1e-3f );
    SW_EXPECT_NEAR_EQUAL( 0.2f, AnimationLODUtil::computeScreenSize( ortho, float3{ 0.0f, 0.0f, 300.0f }, 1.0f ), 1e-3f );
}

/**
 * @brief [AnimationLODTest] LOD 표 — 단계는 화면 크기가 줄어드는 순, 마지막은 0, 모르는 키는 오류. 화면 크기가 단계를 고른다
 */
SW_TEST_CASE( AnimationLODTest, SettingsParseAndSelectLevel )
{
    AnimationLODSettings settings;
    SW_ASSERT_TRUE( settings.parseJSON( R"({ "rate_levels": [ { "min_screen_size": 0.25, "update_rate_divisor": 1, "interpolate": false },
                                                              { "min_screen_size": 0.1, "update_rate_divisor": 2, "interpolate": true },
                                                              { "min_screen_size": 0.0, "update_rate_divisor": 8, "interpolate": false } ],
                                            "offscreen_update_rate_divisor": 0, "budget_milliseconds": 1.5, "max_update_rate_divisor": 16,
                                            "vertex_animation_screen_size": 0.02 })",
                                        "test" ) );
    SW_EXPECT_EQUAL( 3u, static_cast<uint32>( settings._listRateLevel.size() ) );
    SW_EXPECT_EQUAL( 0u, settings.selectRateLevel( 0.5f ) );
    SW_EXPECT_EQUAL( 1u, settings.selectRateLevel( 0.1f ) );
    SW_EXPECT_EQUAL( 2u, settings.selectRateLevel( 0.01f ) );
    SW_EXPECT_NEAR_EQUAL( 1.5f, settings._budgetMilliseconds, 1e-6f );

    const AnimationLODState farState = AnimationLODUtil::makeState( settings, 0.01f, true, nullptr );
    SW_EXPECT_EQUAL( 8u, farState._updateRateDivisor );
    SW_EXPECT_TRUE( farState._bVertexAnimation == SW_TRUE );
    const AnimationLODState midState = AnimationLODUtil::makeState( settings, 0.12f, true, nullptr );
    SW_EXPECT_EQUAL( 2u, midState._updateRateDivisor );
    SW_EXPECT_TRUE( midState._bInterpolate == SW_TRUE );
    SW_EXPECT_TRUE( midState._bVertexAnimation == SW_FALSE );

    test::ScopedDefensiveTestLog expected( "malformed animation LOD tables are rejected" );
    AnimationLODSettings         bad;
    SW_EXPECT_FALSE( bad.parseJSON( R"({ "rate_levels": [ { "min_screen_size": 0.0, "update_rate_divisor": 1, "interpolate": false } ], "typo": 1 })", "test" ) );
    SW_EXPECT_FALSE( bad.parseJSON( R"({ "rate_levels": [ { "min_screen_size": 0.1, "update_rate_divisor": 1, "interpolate": false },
                                                        { "min_screen_size": 0.2, "update_rate_divisor": 2, "interpolate": false } ],
                                       "offscreen_update_rate_divisor": 0, "budget_milliseconds": 0, "max_update_rate_divisor": 8,
                                       "vertex_animation_screen_size": 0 })",
                                    "test" ) );
    SW_EXPECT_TRUE( bad._listRateLevel.empty() );
}

/**
 * @brief [AnimationLODTest] 예산 배분 — 덜 중요한 것부터 주기를 두 배씩(상한까지) 늘려 예산에 맞춘다
 * @details 넷이 각각 100 us(합 400), 예산 250. 가장 덜 중요한(0.05) 것이 16 까지 가도 306 이라, 다음(0.1)이 4 까지 가서 231 이 된다.
 *          가장 중요한 둘은 그대로다. 예산 0 은 손대지 않는다.
 */
SW_TEST_CASE( AnimationLODTest, BudgetThrottlesLeastSignificantFirst )
{
    AnimationBudgetItem arrItem[4] = {
        { 0.5f, 1u},
        { 0.1f, 1u},
        { 0.3f, 1u},
        {0.05f, 1u}
    };
    const float32 expected = AnimationLODUtil::allocateBudget( arrItem, 4, 100.0f, 250.0f, 16 );
    SW_EXPECT_NEAR_EQUAL( 231.25f, expected, 1e-3f );
    SW_EXPECT_EQUAL( 1u, arrItem[0]._updateRateDivisor );
    SW_EXPECT_EQUAL( 4u, arrItem[1]._updateRateDivisor );
    SW_EXPECT_EQUAL( 1u, arrItem[2]._updateRateDivisor );
    SW_EXPECT_EQUAL( 16u, arrItem[3]._updateRateDivisor );

    AnimationBudgetItem arrUnbounded[2] = {
        {0.5f, 1u},
        {0.1f, 2u}
    };
    SW_EXPECT_NEAR_EQUAL( 150.0f, AnimationLODUtil::allocateBudget( arrUnbounded, 2, 100.0f, 0.0f, 16 ), 1e-3f );
    SW_EXPECT_EQUAL( 2u, arrUnbounded[1]._updateRateDivisor );
}

/**
 * @brief [AnimationLODTest] 본 LOD 표 — 단계가 앞 단계를 이어받고 자손까지 빠진다, 화면 크기가 단계를 고른다, 없는 본 · 순서 오류는 거절
 */
SW_TEST_CASE( AnimationLODTest, BoneLODMasksInheritAndValidate )
{
    const Skeleton  skeleton = test::makeChainSkeleton( 4 ); // bone0 → bone1 → bone2 → bone3
    SkeletonBoneLOD boneLOD;
    SW_ASSERT_TRUE( boneLOD.parseJSON( R"({ "levels": [ { "max_screen_size": 0.2, "remove": [ "bone3" ] },
                                                       { "max_screen_size": 0.05, "remove": [ "bone2" ] } ] })",
                                       "test" ) );
    vector<vector<uint8>> listMask;
    SW_ASSERT_TRUE( boneLOD.buildMasks( skeleton, listMask, "test" ) );
    SW_ASSERT_EQUAL( 2u, static_cast<uint32>( listMask.size() ) );
    const uint8 arrLevel1[4] = { 1, 1, 1, 0 };
    const uint8 arrLevel2[4] = { 1, 1, 0, 0 };
    for ( uint32 boneIndex = 0; boneIndex < 4; ++boneIndex )
    {
        SW_EXPECT_EQUAL( static_cast<uint32>( arrLevel1[boneIndex] ), static_cast<uint32>( listMask[0][boneIndex] ) );
        SW_EXPECT_EQUAL( static_cast<uint32>( arrLevel2[boneIndex] ), static_cast<uint32>( listMask[1][boneIndex] ) );
    }
    SW_EXPECT_EQUAL( 0u, boneLOD.selectLevel( 0.5f ) );
    SW_EXPECT_EQUAL( 1u, boneLOD.selectLevel( 0.1f ) );
    SW_EXPECT_EQUAL( 2u, boneLOD.selectLevel( 0.01f ) );
    SW_EXPECT_TRUE( SkeletonBoneLOD::makePathForSkeleton( "a/knight.skeleton.json" ) == "a/knight.bonelod.json" );
    // 임포트 옆 폴더(이름이 같다)는 다시 임포트할 때 지워진다 — 그 밖에 둔다.
    SW_EXPECT_TRUE( SkeletonBoneLOD::makePathForSkeleton( "a/knight/knight.skeleton.json" ) == "a/knight.bonelod.json" );
    string importedPath;
    string siblingPath;
    SkeletonBoneLOD::makeSkeletonCandidatePaths( "a/knight.bonelod.json", importedPath, siblingPath );
    SW_EXPECT_TRUE( importedPath == "a/knight/knight.skeleton.json" );
    SW_EXPECT_TRUE( siblingPath == "a/knight.skeleton.json" );

    // 자손까지 — bone1 을 빼면 bone2 · bone3 도 빠진다.
    SkeletonBoneLOD parentOnly;
    SW_ASSERT_TRUE( parentOnly.parseJSON( R"({ "levels": [ { "max_screen_size": 0.1, "remove": [ "bone1" ] } ] })", "test" ) );
    SW_ASSERT_TRUE( parentOnly.buildMasks( skeleton, listMask, "test" ) );
    SW_EXPECT_EQUAL( 0u, static_cast<uint32>( listMask[0][3] ) );

    test::ScopedDefensiveTestLog expected( "bone LOD tables naming unknown bones or out of order are rejected" );
    SkeletonBoneLOD              unknownBone;
    SW_ASSERT_TRUE( unknownBone.parseJSON( R"({ "levels": [ { "max_screen_size": 0.1, "remove": [ "nope" ] } ] })", "test" ) );
    SW_EXPECT_FALSE( unknownBone.buildMasks( skeleton, listMask, "test" ) );
    SkeletonBoneLOD badOrder;
    SW_EXPECT_FALSE( badOrder.parseJSON( R"({ "levels": [ { "max_screen_size": 0.05, "remove": [ "bone3" ] },
                                                         { "max_screen_size": 0.2, "remove": [ "bone2" ] } ] })",
                                         "test" ) );
}

/**
 * @brief [AnimationLODTest] 본 LOD 가 코덱 트랙을 건너뛴다 — 빠진 본은 레퍼런스 포즈로 남고 나머지는 클립을 받는다(Raw · ACL 같은 답)
 * @details 사슬 4 본 클립(본 1.. 사인 회전)을 0.3 초에 샘플한다. 단계 1(bone3 뺌)이면 bone3 은 레퍼런스(단위 회전), bone2 는 클립 값이다.
 */
SW_TEST_CASE( AnimationLODTest, AnimatorSkipsRemovedBoneTracks )
{
    SW_ASSERT_TRUE( ResourceUtil::initialize() );
    // bone3 의 레퍼런스는 X 축 0.3 라디안이다 — 빠진 본이 "단위" 가 아니라 "레퍼런스" 로 남는지 가른다.
    Skeleton         skeleton;
    const quaternion referenceTilt = quaternion::createFromAxisAngle( float3{ 1.0f, 0.0f, 0.0f }, 0.3f );
    for ( uint32 boneIndex = 0; boneIndex < 4; ++boneIndex )
    {
        const string name = string( "bone" ) + to_string( boneIndex ).c_str();
        (void)skeleton.addBone( hashed_string( name ), static_cast<int32>( boneIndex ) - 1,
                                test::makeBoneTransform( float3{ 0.0f, boneIndex == 0 ? 0.0f : 1.0f, 0.0f }, boneIndex == 3 ? referenceTilt : quaternion::Identity ),
                                float4x4::Identity );
    }
    skeleton.computeInverseBindFromReference();
    const AnimRawClip raw = test::makeChainRawClip( skeleton, 31, 30.0f, 0.6f );

    // 코덱 단독 — 마스크 0 인 트랙은 풀지 않는다(단위 변환).
    const IAnimCodec* arrCodec[2] = { &RawAnimCodec::getInstance(), &ACLAnimCodec::getInstance() };
    for ( const IAnimCodec* pCodec : arrCodec )
    {
        AnimClip codecClip;
        SW_ASSERT_TRUE( codecClip.compressFrom( raw, *pCodec, AnimCodecSettings{}, nullptr ) );
        const uint8 arrTrackMask[4] = { 1, 1, 1, 0 };
        Pose        pose;
        SW_ASSERT_TRUE( codecClip.sampleTracks( 0.3f, pose, arrTrackMask ) );
        SW_EXPECT_NEAR_EQUAL( 1.0f, MathUtil::abs( pose.getBoneTransform( 3 )._rotation._w ), 1e-6f );
        SW_EXPECT_TRUE( MathUtil::abs( pose.getBoneTransform( 2 )._rotation._w ) < 0.9999f );
    }

    AnimClip clip;
    clip.setName( hashed_string( "Wave" ) );
    SW_ASSERT_TRUE( clip.compressFrom( raw, ACLAnimCodec::getInstance(), AnimCodecSettings{}, nullptr ) );
    const string folder = test::makeTempPath( "lodclips" );
    SW_ASSERT_TRUE( clip.saveToFile( FileUtil::joinPath( folder, "wave.animclip" ) ) );

    GameObjectManager manager;
    GameObject*       pObject = manager.createGameObject( hashed_string( "LodHero" ) );
    SW_ASSERT_NOT_NULL( pObject );
    SkeletalMeshComponent*     pUnit     = pObject->addComponent<SkeletalMeshComponent>();
    SkeletalAnimatorComponent* pAnimator = pObject->addComponent<SkeletalAnimatorComponent>();
    SW_ASSERT_TRUE( pUnit != nullptr && pAnimator != nullptr );
    pUnit->setSkeleton( make_shared<Skeleton>( skeleton ) );
    shared_ptr<SkeletonBoneLOD> boneLOD = make_shared<SkeletonBoneLOD>();
    SW_ASSERT_TRUE( boneLOD->parseJSON( R"({ "levels": [ { "max_screen_size": 0.2, "remove": [ "bone3" ] } ] })", "test" ) );
    pUnit->setBoneLOD( boneLOD );
    pAnimator->setClipFolder( folder );
    pAnimator->setInitialState( "Wave" );
    pAnimator->dispatchBeginPlay();

    AnimationLODState state{};
    state._boneLODLevel = 1;
    pUnit->applyAnimationLOD( state );
    SW_ASSERT_NOT_NULL( pUnit->findBoneLODMask() );
    manager.getAnimationSystem().evaluate( 0.3f );
    SW_EXPECT_NEAR_EQUAL( 1.0f, MathUtil::abs( pUnit->getLocalPose().getBoneTransform( 3 )._rotation.dot( referenceTilt ) ), 1e-6f );
    SW_EXPECT_TRUE( MathUtil::abs( pUnit->getLocalPose().getBoneTransform( 2 )._rotation._w ) < 0.9999f );

    // 단계 0 이면 bone3 도 클립을 받는다.
    pUnit->applyAnimationLOD( AnimationLODState{} );
    SW_EXPECT_NULL( pUnit->findBoneLODMask() );
    manager.getAnimationSystem().evaluate( 0.05f );
    SW_EXPECT_TRUE( MathUtil::abs( pUnit->getLocalPose().getBoneTransform( 3 )._rotation.dot( referenceTilt ) ) < 0.9999f );
}

/**
 * @brief [AnimationLODTest] 가시성 — 어느 뷰의 절두체에도 없으면 포즈를 만들지 않고, 둘째 뷰(추가 뷰 · CCTV)에 들면 다시 만든다
 */
SW_TEST_CASE( AnimationLODTest, FrustumVisibilityAcrossViews )
{
    GameObjectManager      manager;
    SkeletalMeshComponent* pUnit = TestAnimationLODInternal::createUnit( manager, "Behind", 3, float3{ 0.0f, 0.0f, -10.0f } );
    SW_ASSERT_NOT_NULL( pUnit );
    manager.flushSceneTransforms();
    TestAnimationLODInternal::CountingTask task( 0.1f );
    pUnit->addAnimationPhaseTask( &task );
    AnimationSystem& system = manager.getAnimationSystem();
    system.setLODSettings( AnimationLODSettings::makeDefault() );
    system.evaluate( 0.016f ); // 처음은 포즈가 더러워 늘 만든다(뷰도 아직 없다)

    vector<AnimationLODView> listView{ TestAnimationLODInternal::makeForwardView( float3{}, float3{ 0.0f, 0.0f, 1.0f } ) };
    system.setLODViews( listView );
    const uint32 before = pUnit->getPoseEvaluationCount();
    for ( uint32 frame = 0; frame < 3; ++frame )
    {
        system.evaluate( 0.016f );
    }
    SW_EXPECT_FALSE( pUnit->isVisibleHint() );
    SW_EXPECT_EQUAL( before, pUnit->getPoseEvaluationCount() );

    // 뒤를 보는 둘째 뷰 — 이제 보인다.
    listView.push_back( TestAnimationLODInternal::makeForwardView( float3{}, float3{ 0.0f, 0.0f, -1.0f } ) );
    system.setLODViews( listView );
    system.evaluate( 0.016f );
    SW_EXPECT_TRUE( pUnit->isVisibleHint() );
    SW_EXPECT_EQUAL( before + 1u, pUnit->getPoseEvaluationCount() );
    SW_EXPECT_NEAR_EQUAL( 0.0866f, pUnit->getAnimationLODState()._screenSize, 2e-3f ); // 반지름 0.866(기본 단위 상자) / 10 m

    // 뷰를 지우면 LOD 가 꺼지고 판정을 되돌린다(보임 · 매 프레임).
    system.clearLODViews();
    system.evaluate( 0.016f );
    SW_EXPECT_TRUE( pUnit->isVisibleHint() );
    SW_EXPECT_EQUAL( 1u, pUnit->getAnimationLODState()._updateRateDivisor );
    pUnit->removeAnimationPhaseTask( &task );
}

/**
 * @brief [AnimationLODTest] 화면 크기 → 갱신 주기(URO) — 작은 유닛은 4 프레임에 한 번 평가하고, 보간이 켜지면 건너뛴 프레임도 포즈가 움직인다(마지막엔 목표에 닿는다)
 * @details 보간이 꺼지면 건너뛴 프레임의 포즈는 그대로다. 보간은 "평가 때 보이던 포즈 → 새 포즈" 를 1/4 씩 가므로 평가 직전 프레임에 목표와 같다.
 */
SW_TEST_CASE( AnimationLODTest, UpdateRateFromScreenSizeInterpolates )
{
    for ( const bool bInterpolate : { false, true } )
    {
        GameObjectManager      manager;
        SkeletalMeshComponent* pUnit = TestAnimationLODInternal::createUnit( manager, "Far", 2, float3{ 0.0f, 0.0f, 20.0f } );
        SW_ASSERT_NOT_NULL( pUnit );
        manager.flushSceneTransforms();
        TestAnimationLODInternal::CountingTask task( 0.2f );
        pUnit->addAnimationPhaseTask( &task );
        AnimationSystem& system = manager.getAnimationSystem();
        system.setLODSettings( TestAnimationLODInternal::makeTwoLevelSettings( bInterpolate ) );
        system.setLODViews( vector<AnimationLODView>{ TestAnimationLODInternal::makeForwardView( float3{}, float3{ 0.0f, 0.0f, 1.0f } ) } );

        // 첫 평가(더러운 포즈) 뒤로 16 프레임 — 주기 4 라 평가는 정확히 4 번이다.
        system.evaluate( 0.016f );
        SW_EXPECT_EQUAL( 4u, pUnit->getAnimationLODState()._updateRateDivisor );
        const uint32 evaluatedBefore        = pUnit->getPoseEvaluationCount();
        uint32       movedSkippedFrameCount = 0;
        uint32       skippedFrameCount      = 0;
        float32      previousYaw            = TestAnimationLODInternal::getRootYaw( *pUnit );
        for ( uint32 frame = 0; frame < 16; ++frame )
        {
            const uint32 countBefore = pUnit->getPoseEvaluationCount();
            system.evaluate( 0.016f );
            const float32 yaw = TestAnimationLODInternal::getRootYaw( *pUnit );
            if ( pUnit->getPoseEvaluationCount() == countBefore )
            {
                ++skippedFrameCount;
                if ( MathUtil::abs( yaw - previousYaw ) > 1e-5f )
                    ++movedSkippedFrameCount;
            }
            previousYaw = yaw;
        }
        SW_EXPECT_EQUAL( evaluatedBefore + 4u, pUnit->getPoseEvaluationCount() );
        SW_EXPECT_EQUAL( 12u, skippedFrameCount );
        if ( bInterpolate )
        {
            SW_EXPECT_TRUE( movedSkippedFrameCount >= 8u ); // 보간이 준비된(평가 두 번 뒤) 건너뛴 프레임은 모두 움직인다
            // 평가 직전 프레임은 목표(마지막 평가 포즈)다 — 0.2 라디안 × 평가 횟수.
            SW_EXPECT_NEAR_EQUAL( 0.2f * static_cast<float32>( task._poseCallCount ), previousYaw, 0.2f );
        }
        else
        {
            SW_EXPECT_EQUAL( 0u, movedSkippedFrameCount );
        }
        pUnit->removeAnimationPhaseTask( &task );
    }
}

/**
 * @brief [AnimationLODTest] 예산 — 평균 비용 × 유닛 수가 예산을 넘으면 화면이 작은(덜 중요한) 유닛부터 주기가 늘고, 큰 유닛은 매 프레임이다
 */
SW_TEST_CASE( AnimationLODTest, BudgetAllocatorThrottlesSmallUnits )
{
    GameObjectManager      manager;
    SkeletalMeshComponent* pNear = TestAnimationLODInternal::createUnit( manager, "Near", 2, float3{ 0.0f, 0.0f, 2.0f } );
    SkeletalMeshComponent* pFar  = TestAnimationLODInternal::createUnit( manager, "Far", 2, float3{ 0.0f, 0.0f, 3.0f } );
    SW_ASSERT_TRUE( pNear != nullptr && pFar != nullptr );
    manager.flushSceneTransforms();
    AnimationLODSettings settings{};
    settings._listRateLevel.push_back( AnimationLODRateLevel{ 0.0f, 1u, static_cast<uint8>( SW_FALSE ) } );
    settings._budgetMilliseconds   = 0.15f; // 150 us
    settings._maxUpdateRateDivisor = 8;
    AnimationSystem& system        = manager.getAnimationSystem();
    system.setLODSettings( settings );
    system.setLODViews( vector<AnimationLODView>{ TestAnimationLODInternal::makeForwardView( float3{}, float3{ 0.0f, 0.0f, 1.0f } ) } );
    system.setAverageEvaluationMicroseconds( 100.0f ); // 둘이면 200 us — 예산을 넘는다
    system.evaluate( 0.016f );
    SW_EXPECT_EQUAL( 1u, pNear->getAnimationLODState()._updateRateDivisor );
    SW_EXPECT_EQUAL( 2u, pFar->getAnimationLODState()._updateRateDivisor );
    SW_EXPECT_TRUE( pFar->getAnimationLODState()._bInterpolate == SW_TRUE );
    SW_EXPECT_NEAR_EQUAL( 150.0f, system.getExpectedEvaluationMicroseconds(), 1e-3f );
}

/**
 * @brief [AnimationLODTest] 스켈레톤 경로를 정하면 곁의 본 LOD 파일(`knight.bonelod.json`)을 그 스켈레톤으로 풀어 마스크를 짓는다
 * @details 스켈레톤을 정하기 전에 마스크를 지으면 암묵 스켈레톤(본 하나)에서 이름을 찾아 "없는 본" 오류가 난다 — 순서를 지킨다.
 */
SW_TEST_CASE( AnimationLODTest, BoneLODLoadsBesideSkeletonPath )
{
    SW_ASSERT_TRUE( ResourceUtil::initialize() );
    GameObjectManager      manager;
    GameObject*            pObject = manager.createGameObject( hashed_string( "Knight" ) );
    SkeletalMeshComponent* pUnit   = ( pObject != nullptr ) ? pObject->addComponent<SkeletalMeshComponent>() : nullptr;
    SW_ASSERT_NOT_NULL( pUnit );
    {
        const test::ScopedLogCollector log;
        pUnit->setSkeletonPath( "game/shooter3d/models/kaykit/knight/knight.skeleton.json" );
        SW_EXPECT_EQUAL( 0u, log.countContaining( "Bone LOD" ) );
    }
    SW_ASSERT_NOT_NULL( pUnit->findBoneLOD() );
    AnimationLODState state{};
    state._boneLODLevel = 1;
    pUnit->applyAnimationLOD( state );
    const uint8* pMask = pUnit->findBoneLODMask();
    SW_ASSERT_NOT_NULL( pMask );
    const int32 kneeBone = pUnit->getSkeleton().findBoneIndex( hashed_string( "kneeIK.l" ) );
    const int32 hipsBone = pUnit->getSkeleton().findBoneIndex( hashed_string( "hips" ) );
    SW_ASSERT_TRUE( kneeBone >= 0 && hipsBone >= 0 );
    SW_EXPECT_EQUAL( 0u, static_cast<uint32>( pMask[kneeBone] ) );
    SW_EXPECT_EQUAL( 1u, static_cast<uint32>( pMask[hipsBone] ) );
}
