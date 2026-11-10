// 포즈 노드 Mirror · Montage 와 스켈레톤 미러 표 — 메모리 클립 · 샘플러로 평가한다(nogpu).
#include "pch.h"

#include "Core/Math/MathUtil.h"
#include "Core/Testing/TestFramework.h"

#include "Engine/Animation/AnimationPlayer.h"
#include "Engine/Animation/Skeletal/AnimationPoseNode.h"
#include "Engine/Animation/Skeletal/Skeleton.h"
#include "Engine/Animation/Skeletal/SkeletonMirrorTable.h"
#include "Engine/Animation/Tests/AnimationTestUtil.h"
#include "Engine/Foundation/EngineServices.h"

using namespace sw;

namespace
{
    struct AnimationPoseNodeTestUtil
    {
        /** @brief 길이만 있는 입력 클립입니다. */
        class FixedPlayable final : public IAnimationPlayable
        {
        public:
            explicit FixedPlayable( float32 length )
                : _length{ length }
            {
            }
            float32 getPlayLength() const override { return _length; }
            bool    isLoopingByDefault() const override { return false; }

        private:
            float32 _length;
        };

        /** @brief 받은 시각을 적고, 정해 둔 포즈를 씁니다. */
        class RecordingSampler final : public IAnimationPoseSampler
        {
        public:
            float32 _lastTime{ -1.0f };
            Pose    _pose;

            void samplePose( const IAnimationPlayable& /*playable*/, float32 time, Pose& inoutPose ) override
            {
                _lastTime = time;
                if ( _pose.getBoneCount() == inoutPose.getBoneCount() )
                    inoutPose = _pose;
            }
        };

        /** @brief root 아래 왼팔(+X 1 m) · 오른팔(-X 1 m) · 척추(+Y 1 m)입니다. */
        static Skeleton makeArmsSkeleton()
        {
            Skeleton skeleton;
            (void)skeleton.addBone( hashed_string( "root" ), -1, test::makeBoneTransform( float3{ 0.0f, 0.0f, 0.0f } ) );
            (void)skeleton.addBone( hashed_string( "arm_l" ), 0, test::makeBoneTransform( float3{ 1.0f, 0.0f, 0.0f } ) );
            (void)skeleton.addBone( hashed_string( "arm_r" ), 0, test::makeBoneTransform( float3{ -1.0f, 0.0f, 0.0f } ) );
            (void)skeleton.addBone( hashed_string( "spine" ), 0, test::makeBoneTransform( float3{ 0.0f, 1.0f, 0.0f } ) );
            return skeleton;
        }

        static bool isSameRotation( const quaternion& expected, const quaternion& actual ) { return MathUtil::abs( expected.dot( actual ) ) > 0.9999f; }

        /** @brief 섹션 Start(0..0.5) → Loop(0.5..1, 반복, Done 이면 End) → End(1..1.5) 의 몽타주입니다. */
        static unique_ptr<IAnimationPoseNode> makeReloadMontage( const IAnimationPlayable& input )
        {
            AnimationGraphPoseNodeDef def;
            def._type = hashed_string( "Montage" );
            def._listInput.push_back( hashed_string( "Clip" ) );
            def._listSection.push_back( AnimationMontageSection{ hashed_string( "Start" ), 0.0f, hashed_string( "Loop" ), {}, {} } );
            def._listSection.push_back( AnimationMontageSection{ hashed_string( "Loop" ), 0.5f, hashed_string( "Loop" ), hashed_string( "Done" ), hashed_string( "End" ) } );
            def._listSection.push_back( AnimationMontageSection{ hashed_string( "End" ), 1.0f, {}, {}, {} } );
            const AnimationPoseNodeType* pType = engine::getSubsystem<AnimationPoseNodeRegistry>().findType( def._type );
            if ( pType == nullptr || AnimationPoseNodeRegistry::isSatisfiedBy( *pType, def, "Reload" ) == false )
                return nullptr;
            return pType->_create( vector<const IAnimationPlayable*>{ &input }, def, "Reload" );
        }
    };
    using Util = AnimationPoseNodeTestUtil;
} // namespace

/**
 * @brief [AnimationPoseNodeTest] 엔진 노드 셋(BlendPoses · Mirror · Montage)이 표에 등록돼 있다 — 그래프 XML 의 type 이름이 곧 이 이름이다
 */
SW_TEST_CASE( AnimationPoseNodeTest, EngineNodesAreRegistered )
{
    const AnimationPoseNodeRegistry& registry = engine::getSubsystem<AnimationPoseNodeRegistry>();
    for ( const utf8* pName : { "BlendPoses", "Mirror", "Montage" } )
    {
        SW_EXPECT_NOT_NULL( registry.findType( hashed_string( pName ) ) );
    }
    SW_EXPECT_EQUAL( 1u, registry.findType( hashed_string( "Mirror" ) )->_inputCount );
}

/**
 * @brief [AnimationPoseNodeTest] 몽타주는 섹션 링크를 따라 클립 시각을 옮긴다 — 반복 섹션은 돌고, 분기 파라미터가 켜지면 끝 섹션으로 가서 멈춘다
 */
SW_TEST_CASE( AnimationPoseNodeTest, MontageFollowsSectionsAndBranches )
{
    const Util::FixedPlayable      clip( 1.5f );
    unique_ptr<IAnimationPoseNode> node = Util::makeReloadMontage( clip );
    SW_REQUIRE_NOT_NULL( node.get() );
    SW_EXPECT_EQUAL( MontagePoseNode::kEndlessLength, node->getPlayLength() ); // Loop → Loop 이 돈다

    Util::RecordingSampler sampler;
    AnimationParameterSet  parameter;
    AnimationPoseParams    params;
    params._pSampler   = &sampler;
    params._pParameter = &parameter;
    Pose pose;
    pose.resize( 1u );

    SW_REQUIRE_TRUE( node->evaluatePose( params, 0.25f, pose ) );
    SW_EXPECT_NEAR( 0.25f, sampler._lastTime, 1.0e-5f ); // Start 안
    SW_REQUIRE_TRUE( node->evaluatePose( params, 1.25f, pose ) );
    SW_EXPECT_NEAR( 0.75f, sampler._lastTime, 1.0e-5f ); // Start 0.5 + Loop 한 바퀴(0.5) 뒤 0.25
    parameter.setBool( hashed_string( "Done" ), true );
    SW_REQUIRE_TRUE( node->evaluatePose( params, 1.75f, pose ) );
    SW_EXPECT_NEAR( 1.25f, sampler._lastTime, 1.0e-5f ); // Loop 끝(0.25 남음)에서 End 로 갈라져 0.25
    SW_REQUIRE_TRUE( node->evaluatePose( params, 5.0f, pose ) );
    SW_EXPECT_NEAR( 1.5f, sampler._lastTime, 1.0e-5f ); // End 다음이 없다 — 끝에서 멈춘다
    SW_REQUIRE_TRUE( node->evaluatePose( params, 0.1f, pose ) );
    SW_EXPECT_NEAR( 0.1f, sampler._lastTime, 1.0e-5f ); // 시각이 뒤로 가면 처음부터
}

/**
 * @brief [AnimationPoseNodeTest] 섹션이 입력 클립 끝을 지나 시작하면 노드를 만들지 않는다
 */
SW_TEST_CASE( AnimationPoseNodeTest, MontageSectionPastTheClipIsAnError )
{
    const Util::FixedPlayable shortClip( 0.8f );
    SW_EXPECT_LOG( "AnimationPoseNode.MontageSectionOutsideClip" );
    SW_EXPECT_NULL( Util::makeReloadMontage( shortClip ).get() );
}

/**
 * @brief [AnimationPoseNodeTest] 미러는 짝 본의 포즈를 축 면으로 비춰 옮긴다 — 왼팔의 Z 회전은 오른팔의 반대 회전이 되고, 레퍼런스 포즈는 그대로다
 */
SW_TEST_CASE( AnimationPoseNodeTest, MirrorSwapsPairedBonesAcrossTheAxis )
{
    const Skeleton      skeleton = Util::makeArmsSkeleton();
    SkeletonMirrorTable table;
    SW_REQUIRE_TRUE( table.loadFromXMLText( R"(<SkeletonMirrorTable><_listPair><SkeletonMirrorPair _bone="arm_l" _mirroredBone="arm_r" /></_listPair></SkeletonMirrorTable>)", "arms" ) );
    vector<uint32> listMirrored;
    SW_REQUIRE_TRUE( table.buildBoneMap( skeleton, listMirrored, "arms" ) );
    SW_REQUIRE_EQUAL( size_t{ 4 }, listMirrored.size() );
    SW_EXPECT_EQUAL( 2u, listMirrored[1] );
    SW_EXPECT_EQUAL( 3u, listMirrored[3] ); // 척추는 자기 자신

    const Util::FixedPlayable clip( 1.0f );
    const MirrorPoseNode      node( clip, AnimationMirrorAxis::X );
    Util::RecordingSampler    sampler;
    AnimationParameterSet     parameter;
    AnimationPoseParams       params;
    params._pSampler      = &sampler;
    params._pParameter    = &parameter;
    params._pSkeleton     = &skeleton;
    params._pMirroredBone = &listMirrored;

    const quaternion raise = quaternion::makeFromAxisAngle( float3{ 0.0f, 0.0f, 1.0f }, 0.5f );
    sampler._pose.setToReference( skeleton );
    BoneTransform raisedArm = sampler._pose.getBoneTransform( 1u );
    raisedArm._rotation     = raise;
    sampler._pose.setBoneTransform( 1u, raisedArm );

    Pose pose;
    pose.setToReference( skeleton );
    SW_REQUIRE_TRUE( node.evaluatePose( params, 0.0f, pose ) );
    const BoneTransform left  = pose.getBoneTransform( 1u );
    const BoneTransform right = pose.getBoneTransform( 2u );
    SW_EXPECT_TRUE( Util::isSameRotation( quaternion::Identity, left._rotation ) );
    SW_EXPECT_TRUE( Util::isSameRotation( quaternion::makeFromAxisAngle( float3{ 0.0f, 0.0f, 1.0f }, -0.5f ), right._rotation ) );
    SW_EXPECT_NEAR( -1.0f, right._translation._x, 1.0e-4f );
    SW_EXPECT_NEAR( 1.0f, left._translation._x, 1.0e-4f );
    SW_EXPECT_NEAR( 1.0f, pose.getBoneTransform( 3u )._translation._y, 1.0e-4f );

    sampler._pose.setToReference( skeleton );
    SW_REQUIRE_TRUE( node.evaluatePose( params, 0.0f, pose ) );
    SW_EXPECT_TRUE( Util::isSameRotation( quaternion::Identity, pose.getBoneTransform( 2u )._rotation ) );
    SW_EXPECT_NEAR( -1.0f, pose.getBoneTransform( 2u )._translation._x, 1.0e-4f );

    params._pMirroredBone = nullptr;
    SW_EXPECT_LOG( "AnimationPoseNode.MirrorNeedsSkeleton" );
    SW_EXPECT_FALSE( node.evaluatePose( params, 0.0f, pose ) );
}

/**
 * @brief [AnimationPoseNodeTest] 미러 표의 없는 본 · 자기 짝 · 두 번 쓴 본은 오류다
 */
SW_TEST_CASE( AnimationPoseNodeTest, MirrorTableRejectsBadPairs )
{
    const Skeleton skeleton = Util::makeArmsSkeleton();
    vector<uint32> listMirrored;
    const auto     build = [&]( const utf8* pPairs )
    {
        SkeletonMirrorTable table;
        const string        text = string( "<SkeletonMirrorTable><_listPair>" ) + pPairs + "</_listPair></SkeletonMirrorTable>";
        return table.loadFromXMLText( text, "bad" ) && table.buildBoneMap( skeleton, listMirrored, "bad" );
    };
    SW_EXPECT_LOG( "SkeletonMirrorTable.UnknownBone" );
    SW_EXPECT_FALSE( build( R"(<SkeletonMirrorPair _bone="arm_l" _mirroredBone="leg_r" />)" ) );
    SW_EXPECT_LOG( "SkeletonMirrorTable.SelfPair" );
    SW_EXPECT_FALSE( build( R"(<SkeletonMirrorPair _bone="spine" _mirroredBone="spine" />)" ) );
    SW_EXPECT_LOG( "SkeletonMirrorTable.BoneInTwoPairs" );
    SW_EXPECT_FALSE( build( R"(<SkeletonMirrorPair _bone="arm_l" _mirroredBone="arm_r" /><SkeletonMirrorPair _bone="arm_l" _mirroredBone="spine" />)" ) );
    SW_EXPECT_TRUE( listMirrored.empty() );
    SW_EXPECT_EQUAL( string( "a/knight.mirrortable.xml" ), SkeletonMirrorTable::makePathForSkeleton( "a/knight/knight.skeleton.xml" ) );
}
