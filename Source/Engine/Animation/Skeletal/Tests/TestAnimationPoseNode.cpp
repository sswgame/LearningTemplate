// 포즈 노드 Mirror · Montage 와 스켈레톤 미러 표 — 메모리 클립 · 샘플러로 평가한다(nogpu). 그래프 글 → 노드 셋 → 평가는 애니메이터가 잇는 길과 같다.
#include "pch.h"

#include "Core/Math/MathUtil.h"
#include "Core/Testing/TestFramework.h"

#include "Engine/Animation/AnimationPlayer.h"
#include "Engine/Animation/Component/SkeletalMeshComponent.h"
#include "Engine/Animation/Skeletal/AnimationPoseNode.h"
#include "Engine/Animation/Skeletal/Skeleton.h"
#include "Engine/Animation/Skeletal/SkeletonMirrorTable.h"
#include "Engine/Animation/Tests/AnimationTestUtil.h"
#include "Engine/Foundation/EngineServices.h"
#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManager.h"

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
        static Skeleton makeArmSkeleton()
        {
            Skeleton skeleton;
            (void)skeleton.addBone( hashed_string( "root" ), -1, test::makeBoneTransform( float3{ 0.0f, 0.0f, 0.0f } ) );
            (void)skeleton.addBone( hashed_string( "arm_l" ), 0, test::makeBoneTransform( float3{ 1.0f, 0.0f, 0.0f } ) );
            (void)skeleton.addBone( hashed_string( "arm_r" ), 0, test::makeBoneTransform( float3{ -1.0f, 0.0f, 0.0f } ) );
            (void)skeleton.addBone( hashed_string( "spine" ), 0, test::makeBoneTransform( float3{ 0.0f, 1.0f, 0.0f } ) );
            return skeleton;
        }

        /** @brief 이름 하나에 길이만 있는 입력 클립 하나를 내는 클립 풀이입니다(애니메이터의 `SkeletalAnimatorClipSource` 자리). */
        class SingleClipSource final : public IAnimationPlayableSource
        {
        public:
            SingleClipSource( const hashed_string& name, float32 length )
                : _name{ name }
                , _clip{ length }
            {
            }
            const IAnimationPlayable* findPlayable( const hashed_string& name ) const override { return name == _name ? &_clip : nullptr; }

        private:
            hashed_string _name;
            FixedPlayable _clip;
        };

        static bool isSameRotation( const quaternion& expected, const quaternion& actual ) { return MathUtil::abs( expected.dot( actual ) ) > 0.9999f; }

        /** @brief 입력마다 정한 속도(m/s)로 X 로 가는 루트 모션을 냅니다 — 루트 모션은 걸음 구간 길이 × 속도입니다. */
        class LinearRootMotionSource final : public IAnimationRootMotionSource
        {
        public:
            const IAnimationPlayable* _pFast{ nullptr };
            float32                   _fastSpeed{ 10.0f };
            mutable uint32            _callCount{ 0 };

            BoneTransform computeRootMotion( const IAnimationPlayable& playable, const AnimationTimeStep& step ) const override
            {
                ++_callCount;
                BoneTransform delta;
                delta._translation = float3{ ( step._currentTime - step._previousTime ) * ( &playable == _pFast ? _fastSpeed : 1.0f ), 0.0f, 0.0f };
                return delta;
            }
        };

        /** @brief 받은 입력 · 시각 · 가중치를 적습니다. */
        class RecordingCurveSink final : public IAnimationCurveSink
        {
        public:
            struct Entry
            {
                const IAnimationPlayable* _pPlayable{ nullptr };
                float32                   _time{ 0.0f };
                float32                   _weight{ 0.0f };
            };
            vector<Entry> _listEntry;

            void accumulateCurves( const IAnimationPlayable& playable, float32 time, float32 weight ) override { _listEntry.push_back( Entry{ &playable, time, weight } ); }
        };

        static constexpr const utf8* kArmMirrorTable = R"(<SkeletonMirrorTable><_listPair><SkeletonMirrorPair _bone="arm_l" _mirroredBone="arm_r" /></_listPair></SkeletonMirrorTable>)";

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
    const Skeleton      skeleton = Util::makeArmSkeleton();
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
    const Skeleton skeleton = Util::makeArmSkeleton();
    vector<uint32> listMirrored;
    const auto     build = [&]( const utf8* pPairs )
    {
        SkeletonMirrorTable                      table;
        StringBuilder<constant::buffer::kMax512> text;
        text.appendFormat( "<SkeletonMirrorTable><_listPair>%#</_listPair></SkeletonMirrorTable>", pPairs );
        return table.loadFromXMLText( text.view(), "bad" ) && table.buildBoneMap( skeleton, listMirrored, "bad" );
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

/**
 * @brief [AnimationPoseNodeTest] 그래프의 Mirror 상태는 노드 셋을 거쳐 평가된다 — 레퍼런스 포즈는 그대로 나오고, 왼팔만 든 포즈는 오른팔이 반대로 든 포즈가 된다
 * @details 애니메이터가 잇는 길(그래프 글 → `AnimationPoseNodeSet::rebuild` → 상태 이름의 재생할 것 → `findPoseNode` → `evaluatePose`)과 짝 번호 표의 주인(`SkeletonMirrorBoneMap`)을 함께 본다.
 */
SW_TEST_CASE( AnimationPoseNodeTest, GraphMirrorStateEvaluatesThroughTheNodeSet )
{
    AnimationGraphAsset graph;
    SW_REQUIRE_TRUE( graph.loadFromXMLText( "<AnimationGraphAsset><nodes><node id=\"1\" name=\"WaveMirrored\"><pose_node type=\"Mirror\" mirror_axis=\"X\"><inputs><item>Wave</item></inputs></pose_node></node></nodes></AnimationGraphAsset>" ) );
    const Util::SingleClipSource clipSource( hashed_string( "Wave" ), 1.0f );
    AnimationPoseNodeSet         poseNodes;
    SW_REQUIRE_TRUE( poseNodes.rebuild( graph, clipSource ) );
    const IAnimationPlayable* pPlayable = poseNodes.findPlayable( hashed_string( "WaveMirrored" ) );
    SW_REQUIRE_NOT_NULL( pPlayable );
    const IAnimationPoseNode* pNode = pPlayable->findPoseNode();
    SW_REQUIRE_NOT_NULL( pNode );
    SW_EXPECT_NULL( poseNodes.findNode( hashed_string( "Wave" ) ) ); // 입력 클립은 노드가 아니다

    const Skeleton                  skeleton = Util::makeArmSkeleton();
    shared_ptr<SkeletonMirrorTable> table    = make_shared<SkeletonMirrorTable>();
    SW_REQUIRE_TRUE( table->loadFromXMLText( Util::kArmMirrorTable, "arms" ) );
    SkeletonMirrorBoneMap boneMap;
    boneMap.setTable( table );
    const vector<uint32>* pMirrored = boneMap.find( skeleton, "arms" );
    SW_REQUIRE_NOT_NULL( pMirrored );

    Util::RecordingSampler sampler;
    AnimationParameterSet  parameter;
    AnimationPoseParams    params;
    params._pSampler      = &sampler;
    params._pParameter    = &parameter;
    params._pSkeleton     = &skeleton;
    params._pMirroredBone = pMirrored;

    // 레퍼런스 포즈는 거울상이라 그대로다.
    sampler._pose.setToReference( skeleton );
    Pose reference;
    reference.setToReference( skeleton );
    Pose pose = reference;
    SW_REQUIRE_TRUE( pNode->evaluatePose( params, 0.0f, pose ) );
    for ( uint32 boneIndex = 0; boneIndex < skeleton.getBoneCount(); ++boneIndex )
    {
        const BoneTransform expected = reference.getBoneTransform( boneIndex );
        SW_EXPECT_TRUE( Util::isSameRotation( expected._rotation, pose.getBoneTransform( boneIndex )._rotation ) );
        SW_EXPECT_NEAR( expected._translation._x, pose.getBoneTransform( boneIndex )._translation._x, 1.0e-4f );
    }

    // 왼팔만 Z 로 든 비대칭 포즈 → 왼팔은 내려가고(오른팔의 레퍼런스가 비친 것) 오른팔이 반대 방향으로 든다.
    BoneTransform raisedArm = sampler._pose.getBoneTransform( 1u );
    raisedArm._rotation     = quaternion::makeFromAxisAngle( float3{ 0.0f, 0.0f, 1.0f }, 0.5f );
    sampler._pose.setBoneTransform( 1u, raisedArm );
    pose.setToReference( skeleton );
    SW_REQUIRE_TRUE( pNode->evaluatePose( params, 0.0f, pose ) );
    SW_EXPECT_TRUE( Util::isSameRotation( quaternion::Identity, pose.getBoneTransform( 1u )._rotation ) );
    SW_EXPECT_TRUE( Util::isSameRotation( quaternion::makeFromAxisAngle( float3{ 0.0f, 0.0f, 1.0f }, -0.5f ), pose.getBoneTransform( 2u )._rotation ) );

    // 표를 다시 읽으면(내용 토큰이 바뀐다) 짝 번호 표를 다시 만든다 — 짝이 없어진 표는 모든 본이 자기 자신이다.
    SW_REQUIRE_TRUE( table->loadFromXMLText( "<SkeletonMirrorTable><_listPair></_listPair></SkeletonMirrorTable>", "arms" ) );
    pMirrored = boneMap.find( skeleton, "arms" );
    SW_REQUIRE_NOT_NULL( pMirrored );
    SW_EXPECT_EQUAL( 1u, ( *pMirrored )[1] );
}

/**
 * @brief [AnimationPoseNodeTest] 그래프의 Montage 상태는 노드 셋의 인스턴스가 섹션 위치를 들고 플레이어 시각을 따라 섹션을 나아간다
 */
SW_TEST_CASE( AnimationPoseNodeTest, GraphMontageStateAdvancesSections )
{
    AnimationGraphAsset graph;
    SW_REQUIRE_TRUE( graph.loadFromXMLText( "<AnimationGraphAsset><nodes><node id=\"1\" name=\"Reload\"><pose_node type=\"Montage\"><inputs><item>ReloadClip</item></inputs><sections>"
                                            "<section name=\"Start\" start=\"0\" next=\"Loop\"/>"
                                            "<section name=\"Loop\" start=\"0.5\" next=\"Loop\" branch_param=\"Done\" branch=\"End\"/>"
                                            "<section name=\"End\" start=\"1\"/>"
                                            "</sections></pose_node></node></nodes></AnimationGraphAsset>" ) );
    const Util::SingleClipSource clipSource( hashed_string( "ReloadClip" ), 1.5f );
    AnimationPoseNodeSet         poseNodes;
    SW_REQUIRE_TRUE( poseNodes.rebuild( graph, clipSource ) );
    const IAnimationPoseNode* pNode = poseNodes.findNode( hashed_string( "Reload" ) );
    SW_REQUIRE_NOT_NULL( pNode );
    const MontagePoseNode& montage = static_cast<const MontagePoseNode&>( *pNode ); // 종류 이름 "Montage" 가 만든 노드다

    Util::RecordingSampler sampler;
    AnimationParameterSet  parameter;
    AnimationPoseParams    params;
    params._pSampler   = &sampler;
    params._pParameter = &parameter;
    Pose pose;
    pose.resize( 1u );

    // 플레이어가 프레임마다 시각을 늘려 준다 — 섹션 위치는 노드 인스턴스가 든다.
    for ( float32 time = 0.1f; time < 1.3f; time += 0.1f )
    {
        SW_REQUIRE_TRUE( pNode->evaluatePose( params, time, pose ) );
    }
    SW_EXPECT_EQUAL( 1u, montage.getCurrentSection() ); // Loop 을 돈다
    parameter.setBool( hashed_string( "Done" ), true );
    SW_REQUIRE_TRUE( pNode->evaluatePose( params, 2.0f, pose ) );
    SW_EXPECT_EQUAL( 2u, montage.getCurrentSection() );     // 분기로 End
    SW_EXPECT_NEAR( 1.5f, montage.getClipTime(), 1.0e-4f ); // 끝에서 멈춘다

    // 다시 지으면 새 인스턴스 — 섹션 위치가 처음으로 돌아간다.
    SW_REQUIRE_TRUE( poseNodes.rebuild( graph, clipSource ) );
    const MontagePoseNode& rebuilt = static_cast<const MontagePoseNode&>( *poseNodes.findNode( hashed_string( "Reload" ) ) );
    SW_EXPECT_EQUAL( 0u, rebuilt.getCurrentSection() );
    poseNodes.clear();
    SW_EXPECT_NULL( poseNodes.findNode( hashed_string( "Reload" ) ) );
}

/**
 * @brief [AnimationPoseNodeTest] 유닛은 미러 표로 스켈레톤의 짝 번호 표를 내고(애니메이터가 평가에 넘긴다), 표가 없거나 맞지 않으면 없음이다
 */
SW_TEST_CASE( AnimationPoseNodeTest, SkeletalMeshGivesTheMirrorBoneMap )
{
    GameObjectManager manager;
    GameObject*       pActor = manager.createGameObject( hashed_string( "Actor" ) );
    SW_REQUIRE_NOT_NULL( pActor );
    SkeletalMeshComponent* pMesh = pActor->addComponent<SkeletalMeshComponent>();
    SW_REQUIRE_NOT_NULL( pMesh );
    pMesh->setSkeleton( make_shared<Skeleton>( Util::makeArmSkeleton() ) );
    SW_EXPECT_NULL( pMesh->findMirroredBones() ); // 표 없음

    shared_ptr<SkeletonMirrorTable> table = make_shared<SkeletonMirrorTable>();
    SW_REQUIRE_TRUE( table->loadFromXMLText( Util::kArmMirrorTable, "arms" ) );
    pMesh->setMirrorTable( table );
    const vector<uint32>* pMirrored = pMesh->findMirroredBones();
    SW_REQUIRE_NOT_NULL( pMirrored );
    SW_EXPECT_EQUAL( 2u, ( *pMirrored )[1] );
    SW_EXPECT_EQUAL( 1u, ( *pMirrored )[2] );

    // 다른 스켈레톤(짝 본이 없다)으로 바꾸면 다시 만들다 오류 — 한 번만 알리고 없음이다.
    pMesh->setSkeleton( make_shared<Skeleton>( test::makeChainSkeleton( 2u ) ) );
    SW_EXPECT_LOG( "SkeletonMirrorTable.UnknownBone" );
    SW_EXPECT_NULL( pMesh->findMirroredBones() );
    SW_EXPECT_NULL( pMesh->findMirroredBones() );
}

/**
 * @brief [AnimationPoseNodeTest] 등록된 종류마다 새 노드 정의(에디터의 "노드 더하기")는 그 종류의 계약을 지키고 같은 글로 다시 읽힌다 — 저장 · 되돌리기 글이 읽히지 않는 노드를 만들지 않는다
 */
SW_TEST_CASE( AnimationPoseNodeTest, DefaultDefsSatisfyTheirContract )
{
    const AnimationPoseNodeRegistry& registry = engine::getSubsystem<AnimationPoseNodeRegistry>();
    for ( const hashed_string& typeName : registry.getTypeNames() )
    {
        AnimationGraphPoseNodeDef def;
        SW_REQUIRE_TRUE( registry.makeDefaultDef( typeName, def ) );
        SW_EXPECT_TRUE( def._type == typeName );
        SW_EXPECT_TRUE( AnimationPoseNodeRegistry::isSatisfiedBy( *registry.findType( typeName ), def, typeName.c_str() ) );

        AnimationGraphAsset graph;
        AnimationGraphNode& node = graph._listNode.emplace_back();
        node._name               = typeName.c_str();
        node._id                 = 1;
        node._poseNode           = def;
        AnimationGraphAsset reread;
        SW_EXPECT_TRUE( reread.loadFromXMLText( graph.toXML() ) );
    }
    AnimationGraphPoseNodeDef unknown;
    SW_EXPECT_LOG( "AnimationPoseNode.PoseNodeTypeUnknown" );
    SW_EXPECT_FALSE( registry.makeDefaultDef( hashed_string( "Mystery" ), unknown ) );
}

/**
 * @brief [AnimationPoseNodeTest] 포즈 노드도 루트 모션 · 커브를 낸다 — 몽타주는 섹션 링크로 끊긴 클립 구간을 잇고, 섞기는 포즈와 같은 가중치 · 위상, 비추기는 축 성분을 뒤집는다
 * @details 예전에는 포즈 노드가 루트 모션 · 커브를 내지 않아 몽타주로 도는 공격의 전진 · 발 커브가 사라졌다. 변이: 몽타주가 걸음 구간을 섹션으로 끊지 않고 플레이어 시각 그대로 물으면 첫 단언이 진다.
 */
SW_TEST_CASE( AnimationPoseNodeTest, PoseNodesExtractRootMotionAndCurves )
{
    const Util::FixedPlayable      clip( 1.5f );
    unique_ptr<IAnimationPoseNode> montage = Util::makeReloadMontage( clip );
    SW_REQUIRE_NOT_NULL( montage.get() );
    AnimationParameterSet        parameter;
    Util::LinearRootMotionSource source;

    // 0.25 → 1.25: Start 0.25..0.5, Loop 0.5..1.0, Loop 0.5..0.75 — 클립 시각으로 1 초를 지났고 조각은 셋이다.
    AnimationTimeStep step;
    step._previousTime        = 0.25f;
    step._currentTime         = 1.25f;
    const BoneTransform delta = montage->computeRootMotion( parameter, step, source );
    SW_EXPECT_NEAR( 1.0f, delta._translation._x, 1.0e-5f );
    SW_EXPECT_EQUAL( 3u, source._callCount );
    Util::RecordingCurveSink montageSink;
    montage->accumulateCurves( parameter, 1.25f, 0.5f, montageSink );
    SW_REQUIRE_EQUAL( size_t( 1 ), montageSink._listEntry.size() );
    SW_EXPECT_NEAR( 0.75f, montageSink._listEntry[0]._time, 1.0e-5f ); // 커브도 섹션을 따른 클립 시각이다

    // 섞기: 둘째 입력(길이 2)은 같은 위상이라 구간이 두 배다 — 0.5 × 1 과 1.0 × 10 을 0.25 로 섞는다.
    const Util::FixedPlayable slow( 1.0f );
    const Util::FixedPlayable run( 2.0f );
    const BlendPosesNode      blend( slow, run, hashed_string( "Speed" ) );
    parameter.setFloat( hashed_string( "Speed" ), 0.25f );
    source._pFast             = &run;
    step._previousTime        = 0.0f;
    step._currentTime         = 0.5f;
    const BoneTransform moved = blend.computeRootMotion( parameter, step, source );
    SW_EXPECT_NEAR( 0.5f * 0.75f + 10.0f * 0.25f, moved._translation._x, 1.0e-4f );
    Util::RecordingCurveSink blendSink;
    blend.accumulateCurves( parameter, 0.5f, 1.0f, blendSink );
    SW_REQUIRE_EQUAL( size_t( 2 ), blendSink._listEntry.size() );
    SW_EXPECT_NEAR( 0.75f, blendSink._listEntry[0]._weight, 1.0e-5f );
    SW_EXPECT_NEAR( 1.0f, blendSink._listEntry[1]._time, 1.0e-5f );
    SW_EXPECT_NEAR( 0.25f, blendSink._listEntry[1]._weight, 1.0e-5f );

    // 비추기(X): 이동의 X 가 뒤집힌다.
    const MirrorPoseNode mirror( slow, AnimationMirrorAxis::X );
    SW_EXPECT_NEAR( -0.5f, mirror.computeRootMotion( parameter, step, source )._translation._x, 1.0e-5f );
}
