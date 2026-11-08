#include "pch.h"

#include "Core/Math/MathUtil.h"
#include "Core/Testing/TestFramework.h"

#include "Engine/Animation/Rig/RigAsset.h"
#include "Engine/Animation/Rig/RigInstance.h"
#include "Engine/Animation/Skeletal/Pose.h"
#include "Engine/Animation/Skeletal/Skeleton.h"

using namespace sw;

namespace
{
    /** @brief 시험 몸의 본 번호입니다. 목표 본(`goal` · `pole` · `anchor`)은 루트 아래에 있어 몸을 움직여도 따라가지 않습니다. */
    enum RigIKTestBone : uint32
    {
        kRoot = 0,
        kPelvis,
        kHipL,
        kKneeL,
        kFootL,
        kHipR,
        kKneeR,
        kFootR,
        kArm,
        kFore,
        kHand,
        kTip,
        kGoal,
        kPole,
        kAnchor,
    };

    /** @brief 왼쪽은 @p _leftY, 오른쪽은 @p _rightY 높이의 평지입니다. 쏜 광선을 기억합니다. */
    class FakeRigGround final : public IRigGroundQuery
    {
    public:
        float3  _lastOrigin{};
        float3  _lastDirection{};
        float3  _normal{ 0.0f, 1.0f, 0.0f };
        float32 _lastMaxDistance{ 0.0f };
        float32 _leftY{ 0.0f };
        float32 _rightY{ 0.0f };
        uint8   _bHasGround{ SW_TRUE };

        bool raycastGround( const float3& origin, const float3& direction, float32 maxDistance, RigGroundHit& outHit ) const override
        {
            FakeRigGround& self   = const_cast<FakeRigGround&>( *this ); // 시험 기록만 고친다
            self._lastOrigin      = origin;
            self._lastDirection   = direction;
            self._lastMaxDistance = maxDistance;
            if ( _bHasGround == SW_FALSE )
                return false;
            outHit._position = float3{ origin._x, origin._x < 0.0f ? _leftY : _rightY, origin._z };
            outHit._normal   = _normal;
            return true;
        }
    };

    /** @brief z = @p _wallZ 에 선 벽입니다(법선 -Z). +Z 로 쏜 광선만 닿습니다. */
    class FakeRigWallQuery final : public IRigGroundQuery
    {
    public:
        float32 _wallZ{ 0.5f };
        uint8   _bHasWall{ SW_TRUE };

        bool raycastGround( const float3& origin, const float3& direction, float32 maxDistance, RigGroundHit& outHit ) const override
        {
            if ( _bHasWall == SW_FALSE || direction._z < 1.0e-4f )
                return false;
            const float32 distance = ( _wallZ - origin._z ) / direction._z;
            if ( distance < 0.0f || distance > maxDistance )
                return false;
            outHit._position = origin + direction * distance;
            outHit._normal   = float3{ 0.0f, 0.0f, -1.0f };
            return true;
        }
    };

    /** @brief 두 다리와 네 마디 팔을 가진 몸에 리그 노드 한 줄을 묶어 돌립니다. 대상은 `Goal` · `Pole` · `Anchor` 입니다. */
    struct RigIKTest
    {
        Skeleton         _skeleton;
        RigInstance      _instance;
        Pose             _pose;
        vector<float4x4> _listModel;

        RigIKTest()
            : _skeleton{}
            , _instance{}
            , _pose{}
            , _listModel{}
        {
            const auto add = [this]( const utf8* pName, int32 parent, const float3& offset )
            {
                BoneTransform transform{};
                transform._translation = offset;
                (void)_skeleton.addBone( hashed_string( pName ), parent, transform );
            };
            add( "root", -1, float3{} );
            add( "pelvis", kRoot, float3{ 0.0f, 1.0f, 0.0f } );
            add( "hip.l", kPelvis, float3{ -0.2f, 0.0f, 0.0f } );
            add( "knee.l", kHipL, float3{ 0.0f, -0.45f, 0.02f } ); // 곧게 펴지 않아 무릎 방향이 정해진다
            add( "foot.l", kKneeL, float3{ 0.0f, -0.45f, -0.02f } );
            add( "hip.r", kPelvis, float3{ 0.2f, 0.0f, 0.0f } );
            add( "knee.r", kHipR, float3{ 0.0f, -0.45f, 0.02f } );
            add( "foot.r", kKneeR, float3{ 0.0f, -0.45f, -0.02f } );
            add( "arm", kPelvis, float3{ 0.0f, 0.5f, 0.0f } );
            add( "fore", kArm, float3{ 0.0f, 0.3f, 0.0f } );
            add( "hand", kFore, float3{ 0.0f, 0.3f, 0.0f } );
            add( "tip", kHand, float3{ 0.0f, 0.2f, 0.0f } );
            add( "goal", kRoot, float3{} );
            add( "pole", kRoot, float3{} );
            add( "anchor", kRoot, float3{} );
            _pose.setToReference( _skeleton );
            refreshModel();
        }

        /** @brief 노드 한 줄을 읽어 묶습니다. 읽기나 묶기가 실패하면 false 입니다. */
        [[nodiscard]] bool load( const string& nodeLine, const string& extraTargets = string{} )
        {
            const string xml = string( R"(<RigAsset><targets><item name="Goal" bone="goal" /><item name="Pole" bone="pole" /><item name="Anchor" bone="anchor" />)" ) + extraTargets +
                               "</targets><nodes>" + nodeLine + "</nodes></RigAsset>";
            shared_ptr<RigAsset> asset = make_shared<RigAsset>();
            if ( asset->loadFromXMLText( xml, "test.rig.xml" ) == false )
                return false;
            return _instance.initialize( asset, _skeleton, nullptr, "test" );
        }

        void setBone( uint32 bone, const float3& translation, const quaternion& rotation = quaternion::Identity )
        {
            BoneTransform transform = _pose.getBoneTransform( bone );
            transform._translation  = translation;
            transform._rotation     = rotation;
            _pose.setBoneTransform( bone, transform );
            refreshModel();
        }

        /** @brief 준비와 평가를 한 번 돌립니다. @p lodState 는 유닛의 LOD 판정입니다(기본은 등급 0 — 모든 노드가 켜진다). */
        void run( const IRigGroundQuery* pGround = nullptr, float32 deltaSeconds = 1.0f / 60.0f, const AnimationLODState& lodState = AnimationLODState{} )
        {
            RigPrepareContext prepare{};
            prepare._worldFromModel = float4x4::Identity;
            prepare._pGroundQuery   = pGround;
            prepare._deltaSeconds   = deltaSeconds;
            prepare._lodState       = lodState;
            _instance.prepare( prepare );
            _instance.evaluate( _pose, _skeleton.getParentIndices(), float4x4::Identity );
            refreshModel();
        }

        void refreshModel() { _pose.computeModelSpace( _skeleton.getParentIndices(), _listModel ); }

        [[nodiscard]] float3     position( uint32 bone ) const { return _listModel[bone].getTranslation(); }
        [[nodiscard]] quaternion rotation( uint32 bone ) const { return _listModel[bone].getRotation(); }
        [[nodiscard]] float32    segment( uint32 from, uint32 to ) const { return ( position( to ) - position( from ) ).getLength(); }

        /** @brief 두 회전이 같으면(부호 무시) 1 에 가까운 값입니다. */
        [[nodiscard]] static float32 similarity( const quaternion& lhs, const quaternion& rhs )
        {
            return MathUtil::abs( lhs._x * rhs._x + lhs._y * rhs._y + lhs._z * rhs._z + lhs._w * rhs._w );
        }

        /** @brief @p rotation 이 돌린 @p axis 가 @p direction 과 이루는 각(도)입니다. */
        [[nodiscard]] static float32 angleToDirection( const quaternion& rotation, const float3& axis, const float3& direction )
        {
            const float3 turned = float3::transform( axis, rotation ).normalize();
            return MathUtil::acos( MathUtil::clamp( turned.dot( direction.normalize() ), -1.0f, 1.0f ) ) * MathUtil::kRadianToDegree;
        }
    };

    constexpr const utf8* kLegs     = R"(<feet><item root="hip.l" mid="knee.l" end="foot.l" /><item root="hip.r" mid="knee.r" end="foot.r" /></feet>)";
    constexpr const utf8* kArmBones = R"(<bones><item>arm</item><item>fore</item><item>hand</item><item>tip</item></bones>)";

    /** @brief 발 디딤 노드 한 줄입니다. */
    string makeFootPlacement( const utf8* pExtra )
    {
        return string( R"(<item type="FootPlacement" name="Feet" pelvis="pelvis" trace_up="0.5" trace_down="0.5" max_pelvis_drop="0.3" max_raise="0.2" )" ) + pExtra + ">" + kLegs + "</item>";
    }

    /** @brief 보폭 워핑 노드 한 줄입니다(배율 0.5 ~ 1.5, 골반 내림 0.3). */
    string makeStride()
    {
        return string( R"(<item type="StrideWarping" name="Stride" pelvis="pelvis" min_scale="0.5" max_scale="1.5" max_pelvis_drop="0.3">)" ) + kLegs + "</item>";
    }

    /** @brief 팔 사슬 하나의 전신 IK 노드 한 줄입니다(팔 뒤에 @p pExtraChain 을 잇습니다). */
    string makeFullBody( const utf8* pExtraChain = "" )
    {
        return string( R"(<item type="FullBodyIK" name="Body" pelvis="pelvis" pelvis_iterations="8" max_pelvis_offset="0.5" iterations="32" tolerance="0.0005"><chains><item target="Goal">)" ) + kArmBones + "</item>" + pExtraChain + "</chains></item>";
    }
} // namespace

// 2 본 IK: 닿는 목표에 끝이 붙고, 마디 길이가 그대로이고, 끝 회전은 애니메이션 값을 지킨다.
SW_TEST_CASE_F( RigIKTest, TwoBoneReachesAndKeepsSegmentLengths )
{
    SW_REQUIRE_TRUE( load( R"(<item type="TwoBoneIK" name="Leg" root="hip.l" mid="knee.l" end="foot.l" target="Goal" />)" ) );
    const float32 thigh = segment( kHipL, kKneeL );
    const float32 shin  = segment( kKneeL, kFootL );
    setBone( kGoal, float3{ -0.2f, 0.3f, 0.3f } );
    run();

    SW_EXPECT_NEAR( ( position( kFootL ) - position( kGoal ) ).getLength(), 0.0f, 1.0e-3f );
    SW_EXPECT_NEAR( segment( kHipL, kKneeL ), thigh, 1.0e-3f );
    SW_EXPECT_NEAR( segment( kKneeL, kFootL ), shin, 1.0e-3f );
    SW_EXPECT_NEAR( similarity( rotation( kFootL ), quaternion::Identity ), 1.0f, 1.0e-3f );
    SW_EXPECT_NEAR( position( kFootR )._y, 0.1f, 1.0e-4f ); // 다른 다리는 건드리지 않는다
}

// LOD 등급이 `max_lod` 를 넘으면 노드 가중치가 페이드로 내려가 끝에 꺼지고(평가하지 않는다), 다시 켜지면 지금 값에서 이어 오른다. 기본 등급(0)은 켠 그대로다.
SW_TEST_CASE_F( RigIKTest, NodeFadesOutAboveMaxLODAndBackIn )
{
    SW_REQUIRE_TRUE( load( R"(<item type="TwoBoneIK" name="Leg" root="hip.l" mid="knee.l" end="foot.l" target="Goal" max_lod="0" />)" ) );
    AnimationLODState nearState{};
    nearState._nodeFadeSeconds = 0.2f;
    AnimationLODState farState = nearState;
    farState._lodGrade         = 1;
    setBone( kGoal, float3{ -0.2f, 0.3f, 0.3f } );
    run( nullptr, 0.05f, nearState );
    SW_EXPECT_NEAR( _instance.getNodeLODWeight( 0 ), 1.0f, 0.0f );
    SW_EXPECT_NEAR( ( position( kFootL ) - position( kGoal ) ).getLength(), 0.0f, 1.0e-3f );

    run( nullptr, 0.05f, farState );
    run( nullptr, 0.05f, farState );
    SW_EXPECT_NEAR( _instance.getNodeLODWeight( 0 ), 0.5f, 1.0e-5f );
    SW_EXPECT_NEAR( _instance.computeNodeWeight( 0 ), 0.5f, 1.0e-5f );
    run( nullptr, 0.05f, nearState ); // 도중에 다시 켠다 — 0.5 에서 이어 오른다
    SW_EXPECT_NEAR( _instance.getNodeLODWeight( 0 ), 0.75f, 1.0e-5f );

    run( nullptr, 1.0f, farState );
    SW_EXPECT_NEAR( _instance.getNodeLODWeight( 0 ), 0.0f, 0.0f );
    SW_EXPECT_FALSE( _instance.canWritePose() );
    const uint32 evaluationCount = _instance.getEvaluationCount();
    _pose.setToReference( _skeleton );
    refreshModel();
    run( nullptr, 0.05f, farState );
    SW_EXPECT_EQUAL( evaluationCount, _instance.getEvaluationCount() ); // 꺼진 노드는 비용이 없다
    SW_EXPECT_NEAR( position( kFootL )._y, 0.1f, 1.0e-4f );             // 포즈를 건드리지 않았다

    run( nullptr, 0.05f, nearState );
    SW_EXPECT_NEAR( _instance.getNodeLODWeight( 0 ), 0.25f, 1.0e-5f );
    SW_EXPECT_TRUE( _instance.canWritePose() );
}

// 닿지 않는 목표로는 다리가 늘어나지 않고 그쪽으로 뻗는다.
SW_TEST_CASE_F( RigIKTest, TwoBoneDoesNotStretchToAFarGoal )
{
    SW_REQUIRE_TRUE( load( R"(<item type="TwoBoneIK" name="Leg" root="hip.l" mid="knee.l" end="foot.l" target="Goal" />)" ) );
    const float32 length = segment( kHipL, kKneeL ) + segment( kKneeL, kFootL );
    setBone( kGoal, float3{ -0.2f, -3.0f, 0.0f } );
    run();

    SW_EXPECT_TRUE( segment( kHipL, kFootL ) <= length + 1.0e-3f );
    SW_EXPECT_TRUE( segment( kHipL, kFootL ) > length * 0.98f );
    SW_EXPECT_NEAR( position( kFootL )._x, -0.2f, 1.0e-2f );
    SW_EXPECT_NEAR( position( kFootL )._z, 0.0f, 5.0e-2f );
}

// 극점 대상이 무릎이 굽는 쪽을 정한다.
SW_TEST_CASE_F( RigIKTest, PoleChoosesTheKneeSide )
{
    SW_REQUIRE_TRUE( load( R"(<item type="TwoBoneIK" name="Leg" root="hip.l" mid="knee.l" end="foot.l" target="Goal" pole="Pole" />)" ) );
    setBone( kGoal, float3{ -0.2f, 0.4f, 0.0f } );
    setBone( kPole, float3{ -0.2f, 0.7f, 2.0f } );
    run();
    SW_EXPECT_TRUE( position( kKneeL )._z > 0.05f );
    SW_EXPECT_NEAR( ( position( kFootL ) - position( kGoal ) ).getLength(), 0.0f, 1.0e-3f );

    _pose.setToReference( _skeleton );
    setBone( kGoal, float3{ -0.2f, 0.4f, 0.0f } );
    setBone( kPole, float3{ -0.2f, 0.7f, -2.0f } );
    run();
    SW_EXPECT_TRUE( position( kKneeL )._z < -0.05f );
}

// match_rotation 은 끝 회전을 대상 회전으로 맞추고, keep_end_rotation 을 끄면 IK 가 돌린 그대로 둔다.
SW_TEST_CASE_F( RigIKTest, TwoBoneEndRotationOptions )
{
    const quaternion goalRotation = quaternion::makeFromAxisAngle( float3{ 0.0f, 1.0f, 0.0f }, 1.0f );
    SW_REQUIRE_TRUE( load( R"(<item type="TwoBoneIK" name="Leg" root="hip.l" mid="knee.l" end="foot.l" target="Goal" match_rotation="true" />)" ) );
    setBone( kGoal, float3{ -0.2f, 0.3f, 0.3f }, goalRotation );
    run();
    SW_EXPECT_NEAR( similarity( rotation( kFootL ), goalRotation ), 1.0f, 1.0e-3f );

    RigIKTest free;
    SW_REQUIRE_TRUE( free.load( R"(<item type="TwoBoneIK" name="Leg" root="hip.l" mid="knee.l" end="foot.l" target="Goal" keep_end_rotation="false" />)" ) );
    free.setBone( kGoal, float3{ -0.2f, 0.3f, 0.3f } );
    free.run();
    SW_EXPECT_TRUE( similarity( free.rotation( kFootL ), quaternion::Identity ) < 0.999f );
}

// 가중치는 결과를 애니메이션 쪽으로 섞고, 0 이면 평가가 포즈를 바꿀 수 없다.
SW_TEST_CASE_F( RigIKTest, NodeWeightBlendsTheResult )
{
    SW_REQUIRE_TRUE( load( R"(<item type="TwoBoneIK" name="Leg" weight="0.5" root="hip.l" mid="knee.l" end="foot.l" target="Goal" />)" ) );
    const float3 goal{ -0.2f, 0.3f, 0.5f };
    setBone( kGoal, goal );
    const float32 before = ( position( kFootL ) - goal ).getLength();
    run();
    const float32 after = ( position( kFootL ) - goal ).getLength();
    SW_EXPECT_TRUE( after > 0.02f );
    SW_EXPECT_TRUE( after < before );

    RigIKTest off;
    SW_REQUIRE_TRUE( off.load( R"(<item type="TwoBoneIK" name="Leg" weight="0" root="hip.l" mid="knee.l" end="foot.l" target="Goal" />)" ) );
    off.setBone( kGoal, goal );
    off.run();
    SW_EXPECT_FALSE( off._instance.canWritePose() );
    SW_EXPECT_NEAR( off.position( kFootL )._y, 0.1f, 1.0e-4f );
}

// FABRIK · CCD 는 닿는 목표에 끝을 붙이고 마디 길이를 지킨다.
SW_TEST_CASE( RigIKChainTest, ChainsReachAndKeepSegmentLengths )
{
    for ( const utf8* pType : { "FABRIKChain", "CCDChain" } )
    {
        RigIKTest rig;
        SW_REQUIRE_TRUE( rig.load( string( R"(<item type=")" ) + pType + R"(" name="Arm" target="Goal" iterations="64" tolerance="0.0005">)" + kArmBones + "</item>" ) );
        const float32 foreLength = rig.segment( kFore, kHand );
        const float3  goal{ 0.3f, 1.7f, 0.2f };
        rig.setBone( kGoal, goal );
        rig.run();
        SW_EXPECT_TRUE( ( rig.position( kTip ) - goal ).getLength() < 5.0e-3f );
        SW_EXPECT_NEAR( rig.segment( kFore, kHand ), foreLength, 1.0e-3f );
        SW_EXPECT_NEAR( rig.position( kArm )._y, 1.5f, 1.0e-4f ); // 사슬 뿌리는 그 자리다
    }
}

// 닿지 않는 목표에는 사슬이 늘어나지 않고 그쪽으로 뻗는다.
SW_TEST_CASE( RigIKChainTest, ChainsDoNotStretch )
{
    for ( const utf8* pType : { "FABRIKChain", "CCDChain" } )
    {
        RigIKTest rig;
        SW_REQUIRE_TRUE( rig.load( string( R"(<item type=")" ) + pType + R"(" name="Arm" target="Goal" iterations="64">)" + kArmBones + "</item>" ) );
        rig.setBone( kGoal, float3{ 3.0f, 1.5f, 0.0f } );
        rig.run();
        SW_EXPECT_TRUE( rig.segment( kArm, kTip ) <= 0.8f + 1.0e-3f );
        SW_EXPECT_TRUE( rig.position( kTip )._x > 0.7f );
    }
}

// 관절 제한: 한 관절의 원뿔을 넘지 않고, 사슬 밖 본의 제한은 묶을 때 오류다.
SW_TEST_CASE_F( RigIKTest, ChainLimitsHoldAndForeignBoneFails )
{
    SW_REQUIRE_TRUE( load( string( R"(<item type="CCDChain" name="Arm" target="Goal" iterations="32">)" ) + kArmBones + R"(<limits><item bone="arm" type="Cone" swing_degrees="10" /></limits></item>)" ) );
    setBone( kGoal, float3{ 0.7f, 1.5f, 0.0f } );
    run();
    SW_EXPECT_TRUE( angleToDirection( rotation( kArm ), float3{ 0.0f, 1.0f, 0.0f }, float3{ 0.0f, 1.0f, 0.0f } ) <= 10.5f );

    SW_EXPECT_LOG( "RigIKNodes.RigLimitBoneNot" );
    SW_EXPECT_LOG( "RigInstance.RigNodeCouldNot" );
    RigIKTest foreign;
    SW_EXPECT_FALSE( foreign.load( string( R"(<item type="CCDChain" name="Arm" target="Goal">)" ) + kArmBones + R"(<limits><item bone="pelvis" type="Cone" swing_degrees="10" /></limits></item>)" ) );
    SW_EXPECT_FALSE( foreign._instance.isInitialized() );
}

// 조준: 축이 목표를 향하고, 상한이 있으면 애니메이션 방향에서 그 각까지만 돈다. 목표가 제자리면 그대로다.
SW_TEST_CASE_F( RigIKTest, AimPointsTheAxisWithinTheLimit )
{
    SW_REQUIRE_TRUE( load( R"(<item type="Aim" name="Aim" bone="hand" target="Goal" aim_axis="0,0,1" />)" ) );
    const float3 handPosition = position( kHand );
    setBone( kGoal, handPosition + float3{ 1.0f, 0.0f, 0.0f } );
    run();
    SW_EXPECT_TRUE( angleToDirection( rotation( kHand ), float3{ 0.0f, 0.0f, 1.0f }, float3{ 1.0f, 0.0f, 0.0f } ) < 0.5f );
    SW_EXPECT_NEAR( ( position( kHand ) - handPosition ).getLength(), 0.0f, 1.0e-4f );

    RigIKTest limited;
    SW_REQUIRE_TRUE( limited.load( R"(<item type="Aim" name="Aim" bone="hand" target="Goal" aim_axis="0,0,1" max_degrees="30" />)" ) );
    limited.setBone( kGoal, handPosition + float3{ 1.0f, 0.0f, 0.0f } );
    limited.run();
    SW_EXPECT_NEAR( angleToDirection( limited.rotation( kHand ), float3{ 0.0f, 0.0f, 1.0f }, float3{ 0.0f, 0.0f, 1.0f } ), 30.0f, 0.5f );

    RigIKTest sameSpot;
    SW_REQUIRE_TRUE( sameSpot.load( R"(<item type="Aim" name="Aim" bone="hand" target="Goal" />)" ) );
    sameSpot.setBone( kGoal, handPosition );
    sameSpot.run();
    SW_EXPECT_NEAR( similarity( sameSpot.rotation( kHand ), quaternion::Identity ), 1.0f, 1.0e-4f );
}

// 조상 사슬은 몫만큼 돌아 조준을 나눠 받고, 끝은 여전히 목표를 향한다.
SW_TEST_CASE_F( RigIKTest, AimChainSharesTheTurn )
{
    SW_REQUIRE_TRUE( load( R"(<item type="Aim" name="Aim" bone="hand" target="Goal" aim_axis="0,0,1"><chain><item bone="arm" weight="0.5" /></chain></item>)" ) );
    const float3 goal = position( kHand ) + float3{ 2.0f, 0.0f, 0.0f };
    setBone( kGoal, goal );
    run();

    SW_EXPECT_TRUE( similarity( rotation( kArm ), quaternion::Identity ) < 0.99f ); // 팔도 돌았다
    const float3 toGoal = goal - position( kHand );
    SW_EXPECT_TRUE( angleToDirection( rotation( kHand ), float3{ 0.0f, 0.0f, 1.0f }, toGoal ) < 5.0f );
}

// 사슬 본마다 상한: 팔은 자기 상한(10 도)까지만 돌고, 남은 회전은 끝 본이 받아 여전히 목표를 향한다.
SW_TEST_CASE_F( RigIKTest, AimChainBoneStopsAtItsOwnLimit )
{
    SW_REQUIRE_TRUE( load( R"(<item type="Aim" name="Aim" bone="hand" target="Goal" aim_axis="0,0,1"><chain><item bone="arm" weight="1" max_degrees="10" /></chain></item>)" ) );
    const float3 goal = position( kHand ) + float3{ 2.0f, 0.0f, 0.0f };
    setBone( kGoal, goal );
    run();

    SW_EXPECT_NEAR( angleToDirection( rotation( kArm ), float3{ 0.0f, 0.0f, 1.0f }, float3{ 0.0f, 0.0f, 1.0f } ), 10.0f, 0.5f );
    const float3 toGoal = goal - position( kHand );
    SW_EXPECT_TRUE( angleToDirection( rotation( kHand ), float3{ 0.0f, 0.0f, 1.0f }, toGoal ) < 1.0f );
}

// 모델 공간 조준 축: 레퍼런스에서 요 90 도 돈 본도 데이터의 +Z(모델 앞)를 본 로컬로 바꿔 쓴다 — 조준 뒤 그 축이 목표(+X)를 향한다.
SW_TEST_CASE_F( RigIKTest, AimAxisInModelSpaceFollowsTheReferencePose )
{
    BoneTransform turned{};
    turned._translation  = float3{ 0.0f, 1.5f, 0.0f };
    turned._rotation     = quaternion::makeFromYawPitchRoll( float3{ 0.0f, MathUtil::kPi * 0.5f, 0.0f } );
    const uint32 kTurned = static_cast<uint32>( _skeleton.getBoneCount() );
    (void)_skeleton.addBone( hashed_string( "turned" ), kRoot, turned );
    _pose.setToReference( _skeleton );
    refreshModel();
    SW_REQUIRE_TRUE( load( R"(<item type="Aim" name="Look" bone="turned" target="Goal" aim_axis="0,0,1" aim_axis_in_model="true" />)" ) );
    setBone( kGoal, position( kTurned ) + float3{ 1.0f, 0.0f, 0.0f } );
    run();

    const float3 localAxis = float3::transform( float3{ 0.0f, 0.0f, 1.0f }, RigIKSolver::makeInverse( turned._rotation ) );
    SW_EXPECT_TRUE( angleToDirection( rotation( kTurned ), localAxis, float3{ 1.0f, 0.0f, 0.0f } ) < 0.5f );
}

// 발 디딤의 출력: 준비 단계가 지난 평가의 골반 · 발 높이를 옮겨 둔다(첫 준비 전에는 0, 다음 준비부터 값). 모르는 출력 · 범위 밖 발은 false 다.
SW_TEST_CASE_F( RigIKTest, FootPlacementPublishesOffsetsAfterThePrepare )
{
    SW_REQUIRE_TRUE( load( makeFootPlacement( R"(interp_speed="0")" ) ) );
    FakeRigGround ground;
    ground._leftY  = -0.1f;
    ground._rightY = 0.05f;
    run( &ground );
    run( &ground );

    float32 value = 1.0f;
    SW_REQUIRE_TRUE( _instance.getNodeOutputValue( hashed_string( "Feet" ), hashed_string( "pelvis_offset" ), value ) );
    SW_EXPECT_NEAR( value, -0.1f, 1.0e-4f );
    SW_REQUIRE_TRUE( _instance.getNodeOutputValue( hashed_string( "Feet" ), hashed_string( "foot_offset_0" ), value ) );
    SW_EXPECT_NEAR( value, -0.1f, 1.0e-4f );
    SW_REQUIRE_TRUE( _instance.getNodeOutputValue( hashed_string( "Feet" ), hashed_string( "foot_offset_1" ), value ) );
    SW_EXPECT_NEAR( value, 0.05f, 1.0e-4f );
    SW_REQUIRE_TRUE( _instance.getNodeOutputValue( hashed_string( "Feet" ), hashed_string( "ground_hits" ), value ) );
    SW_EXPECT_NEAR( value, 2.0f, 1.0e-4f );
    SW_EXPECT_FALSE( _instance.getNodeOutputValue( hashed_string( "Feet" ), hashed_string( "foot_offset_2" ), value ) );
    SW_EXPECT_FALSE( _instance.getNodeOutputValue( hashed_string( "Feet" ), hashed_string( "knee_offset" ), value ) );
    SW_EXPECT_FALSE( _instance.getNodeOutputValue( hashed_string( "Nobody" ), hashed_string( "pelvis_offset" ), value ) );
}

// 접촉 대상 + 2 본 IK: 엉덩이에서 +Z 로 쏜 광선이 벽에 닿으면 발이 벽 앞(법선 × surface_offset)에 붙는다. 닿지 않으면 애니메이션 그대로다.
SW_TEST_CASE_F( RigIKTest, ContactTargetPutsTheEndOnTheSurface )
{
    const string wallTarget = R"(<item name="Wall" trace_from="hip.l" trace_direction="0,0,1" trace_distance="1" surface_offset="0.05" />)";
    SW_REQUIRE_TRUE( load( R"(<item type="TwoBoneIK" name="Touch" root="hip.l" mid="knee.l" end="foot.l" target="Wall" />)", wallTarget ) );
    FakeRigWallQuery wall;
    wall._wallZ = 0.5f;
    run( &wall );
    SW_EXPECT_NEAR( ( position( kFootL ) - float3{ -0.2f, 1.0f, 0.45f } ).getLength(), 0.0f, 2.0e-3f );

    RigIKTest        missing;
    FakeRigWallQuery noWall;
    noWall._bHasWall = SW_FALSE;
    SW_REQUIRE_TRUE( missing.load( R"(<item type="TwoBoneIK" name="Touch" root="hip.l" mid="knee.l" end="foot.l" target="Wall" />)", wallTarget ) );
    missing.run( &noWall );
    SW_EXPECT_NEAR( ( missing.position( kFootL ) - float3{ -0.2f, 0.1f, 0.0f } ).getLength(), 0.0f, 1.0e-4f );
}

// 접촉 가중치는 blend_speed 로 오른다: 첫 준비(0.1 초, 초당 5)에 0.5 라 발은 애니메이션 자리와 접촉점의 가운데로 간다. 놓으면 같은 속도로 내린다.
SW_TEST_CASE_F( RigIKTest, ContactTargetBlendsInAndOut )
{
    const string wallTarget = R"(<item name="Wall" trace_from="hip.l" trace_direction="0,0,1" trace_distance="1" surface_offset="0.05" blend_speed="5" />)";
    SW_REQUIRE_TRUE( load( R"(<item type="TwoBoneIK" name="Touch" root="hip.l" mid="knee.l" end="foot.l" target="Wall" />)", wallTarget ) );
    FakeRigWallQuery wall;
    run( &wall, 0.1f );
    SW_EXPECT_NEAR( _instance.getTargetWeight( 3 ), 0.5f, 1.0e-4f );
    SW_EXPECT_NEAR( ( position( kFootL ) - float3{ -0.2f, 0.55f, 0.225f } ).getLength(), 0.0f, 2.0e-3f );

    wall._bHasWall = SW_FALSE;
    _pose.setToReference( _skeleton );
    run( &wall, 0.06f );
    SW_EXPECT_NEAR( _instance.getTargetWeight( 3 ), 0.2f, 1.0e-4f ); // 마지막 접촉점으로 줄어든다
    SW_EXPECT_NEAR( _instance.getTargetWeight( 0 ), 1.0f, 1.0e-6f ); // 본 대상은 늘 1
}

// 발 디딤: 낮은 쪽만큼 골반이 내려가고 양발이 땅 높이를 따른다. 광선은 발 위에서 아래로 쏜다.
SW_TEST_CASE_F( RigIKTest, FootPlacementFollowsTheGround )
{
    SW_REQUIRE_TRUE( load( makeFootPlacement( R"(interp_speed="0")" ) ) );
    FakeRigGround ground;
    ground._leftY  = -0.1f;
    ground._rightY = 0.05f;
    run( &ground );

    SW_EXPECT_NEAR( position( kPelvis )._y, 0.9f, 1.0e-3f );
    SW_EXPECT_NEAR( position( kFootL )._y, 0.0f, 2.0e-3f ); // 기준 발 높이 0.1 + 땅 -0.1
    SW_EXPECT_NEAR( position( kFootR )._y, 0.15f, 2.0e-3f );
    SW_EXPECT_NEAR( ground._lastOrigin._y, 0.5f, 1.0e-4f ); // 바닥 높이 + trace_up
    SW_EXPECT_NEAR( ground._lastDirection._y, -1.0f, 1.0e-4f );
    SW_EXPECT_NEAR( ground._lastMaxDistance, 1.0f, 1.0e-4f );
}

// 굽 높이(몸 입력의 발 들림): 골반과 두 발이 땅 높이 보정 위에 굽만큼 더 올라간다. 땅이 없어도 굽은 걸린다.
SW_TEST_CASE_F( RigIKTest, FootPlacementRaisesByTheHeelLift )
{
    SW_REQUIRE_TRUE( load( makeFootPlacement( R"(interp_speed="0")" ) ) );
    _instance.getBodyInput().setFootLift( 0.08f );
    FakeRigGround ground;
    ground._leftY  = -0.1f;
    ground._rightY = 0.05f;
    run( &ground );
    SW_EXPECT_NEAR( position( kPelvis )._y, 0.98f, 1.0e-3f );
    SW_EXPECT_NEAR( position( kFootL )._y, 0.08f, 2.0e-3f ); // 땅(-0.1) 위 기준 발 높이 0.1 + 굽 0.08
    SW_EXPECT_NEAR( position( kFootR )._y, 0.23f, 2.0e-3f );

    _pose.setToReference( _skeleton );
    run( nullptr );
    SW_EXPECT_NEAR( position( kPelvis )._y, 1.08f, 1.0e-3f );
    SW_EXPECT_NEAR( position( kFootL )._y, 0.18f, 2.0e-3f );
}

// 땅이 없거나 쿼리가 없으면 애니메이션 그대로다.
SW_TEST_CASE_F( RigIKTest, FootPlacementWithoutGroundLeavesThePose )
{
    SW_REQUIRE_TRUE( load( makeFootPlacement( R"(interp_speed="0")" ) ) );
    FakeRigGround ground;
    ground._bHasGround = SW_FALSE;
    run( &ground );
    SW_EXPECT_NEAR( position( kPelvis )._y, 1.0f, 1.0e-4f );
    SW_EXPECT_NEAR( position( kFootL )._y, 0.1f, 1.0e-3f );

    _pose.setToReference( _skeleton );
    run( nullptr );
    SW_EXPECT_NEAR( position( kPelvis )._y, 1.0f, 1.0e-4f );
}

// 내림 · 올림은 상한에서 멈춘다.
SW_TEST_CASE_F( RigIKTest, FootPlacementClampsDropAndRaise )
{
    SW_REQUIRE_TRUE( load( makeFootPlacement( R"(interp_speed="0")" ) ) );
    FakeRigGround ground;
    ground._leftY  = -1.0f;
    ground._rightY = 1.0f;
    run( &ground );
    SW_EXPECT_NEAR( position( kPelvis )._y, 0.7f, 1.0e-3f );
    SW_EXPECT_NEAR( position( kFootL )._y, -0.2f, 2.0e-3f );
    SW_EXPECT_NEAR( position( kFootR )._y, 0.3f, 2.0e-3f );
}

// 첫 평가는 땅으로 바로 가고 그 뒤로 부드럽게 따라간다. reset 뒤에는 다시 바로 간다.
SW_TEST_CASE_F( RigIKTest, FootPlacementInterpolatesAfterTheFirstFrame )
{
    SW_REQUIRE_TRUE( load( makeFootPlacement( R"(interp_speed="5")" ) ) );
    FakeRigGround ground;
    ground._leftY  = -0.1f;
    ground._rightY = -0.1f;
    run( &ground, 0.1f );
    SW_EXPECT_NEAR( position( kPelvis )._y, 0.9f, 1.0e-3f );

    ground._leftY  = 0.0f;
    ground._rightY = 0.0f;
    _pose.setToReference( _skeleton );
    run( &ground, 0.1f ); // 비율 0.5 - 발은 -0.1 에서 -0.05 로, 골반은 그 발 높이를 다시 반만 따라 -0.075 로 간다
    SW_EXPECT_NEAR( position( kPelvis )._y, 0.925f, 1.0e-3f );

    _instance.reset();
    _pose.setToReference( _skeleton );
    run( &ground, 0.1f );
    SW_EXPECT_NEAR( position( kPelvis )._y, 1.0f, 1.0e-3f );
}

// 법선 맞추기는 발을 기울이되 상한까지만이다.
SW_TEST_CASE_F( RigIKTest, FootPlacementAlignsToTheNormalWithinTheLimit )
{
    SW_REQUIRE_TRUE( load( makeFootPlacement( R"(interp_speed="0" align_to_normal="true" max_align_degrees="10")" ) ) );
    FakeRigGround ground;
    ground._normal = float3{ 0.5f, 0.8660254f, 0.0f }; // 30 도
    run( &ground );
    SW_EXPECT_NEAR( angleToDirection( rotation( kFootL ), float3{ 0.0f, 1.0f, 0.0f }, float3{ 0.0f, 1.0f, 0.0f } ), 10.0f, 0.5f );
    SW_EXPECT_TRUE( float3::transform( float3{ 0.0f, 1.0f, 0.0f }, rotation( kFootL ) )._x > 0.0f );
}

// 보폭 워핑: 배율이 1 이거나 방향이 없으면 그대로이고, 이동 방향 성분만 늘린다. 발 회전은 애니메이션 값이다. 배율은 범위로 잘린다.
SW_TEST_CASE_F( RigIKTest, StrideScalesAlongTheDirectionOnly )
{
    SW_REQUIRE_TRUE( load( makeStride() ) );
    const quaternion swing        = quaternion::makeFromAxisAngle( float3{ 1.0f, 0.0f, 0.0f }, -0.3f );
    const auto       prepareSwing = [&]()
    {
        _pose.setToReference( _skeleton );
        setBone( kHipL, _pose.getBoneTransform( kHipL )._translation, swing );
    };
    prepareSwing();
    const float3     before         = position( kFootL );
    const quaternion beforeRotation = rotation( kFootL );
    SW_REQUIRE_TRUE( MathUtil::abs( before._z ) > 0.1f );

    run();
    SW_EXPECT_NEAR( ( position( kFootL ) - before ).getLength(), 0.0f, 1.0e-4f ); // 넣기 전 배율은 1

    SW_EXPECT_TRUE( _instance.setNodeControl( hashed_string( "Stride" ), hashed_string( "stride_scale" ), 1.25f ) );
    prepareSwing();
    run();
    SW_EXPECT_NEAR( position( kFootL )._z, before._z * 1.25f, 2.0e-3f );
    SW_EXPECT_NEAR( position( kFootL )._x, before._x, 2.0e-3f );
    SW_EXPECT_NEAR( similarity( rotation( kFootL ), beforeRotation ), 1.0f, 1.0e-3f );

    SW_EXPECT_TRUE( _instance.setNodeControl( hashed_string( "Stride" ), hashed_string( "stride_scale" ), 9.0f ) );
    prepareSwing();
    run();
    SW_EXPECT_NEAR( position( kFootL )._z, before._z * 1.5f, 5.0e-2f ); // 최대 1.5 에서 멈춘다(몸이 닿는 만큼)

    SW_EXPECT_TRUE( _instance.setNodeControl( hashed_string( "Stride" ), hashed_string( "direction_z" ), 0.0f ) );
    prepareSwing();
    run();
    SW_EXPECT_NEAR( ( position( kFootL ) - before ).getLength(), 0.0f, 1.0e-4f ); // 방향이 없다

    SW_EXPECT_TRUE( _instance.setNodeControl( hashed_string( "Stride" ), hashed_string( "direction_x" ), 1.0f ) );
    prepareSwing();
    run();
    SW_EXPECT_NEAR( position( kFootL )._z, before._z, 2.0e-3f ); // 옆 방향 워핑은 앞 성분을 바꾸지 않는다

    SW_EXPECT_FALSE( _instance.setNodeControl( hashed_string( "Stride" ), hashed_string( "unknown" ), 1.0f ) );
    SW_EXPECT_FALSE( _instance.setNodeControl( hashed_string( "NoNode" ), hashed_string( "stride_scale" ), 1.0f ) );
}

// 보폭을 늘려 다리가 닿지 않으면 골반을 내려 닿게 한다. 발 높이는 땅 그대로다.
SW_TEST_CASE_F( RigIKTest, StrideDropsThePelvisToReach )
{
    SW_REQUIRE_TRUE( load( makeStride() ) );
    SW_EXPECT_TRUE( _instance.setNodeControl( hashed_string( "Stride" ), hashed_string( "stride_scale" ), 1.25f ) );
    const quaternion swing = quaternion::makeFromAxisAngle( float3{ 1.0f, 0.0f, 0.0f }, -0.5f );
    setBone( kHipL, _pose.getBoneTransform( kHipL )._translation, swing );
    const float3 before = position( kFootL );
    run();

    SW_EXPECT_TRUE( position( kPelvis )._y < 1.0f );
    SW_EXPECT_TRUE( position( kPelvis )._y >= 0.7f - 1.0e-4f );
    SW_EXPECT_NEAR( position( kFootL )._y, before._y, 2.0e-3f );
    SW_EXPECT_NEAR( position( kFootL )._z, before._z * 1.25f, 2.0e-3f );
}

// 전신 IK: 닿는 목표면 골반은 그대로이고, 닿지 않으면 골반이 모자란 만큼 끌려가 끝이 닿는다.
SW_TEST_CASE_F( RigIKTest, FullBodyPelvisMovesOnlyWhenTheChainFallsShort )
{
    SW_REQUIRE_TRUE( load( makeFullBody() ) );
    setBone( kGoal, float3{ 0.3f, 1.7f, 0.2f } );
    run();
    SW_EXPECT_NEAR( ( position( kPelvis ) - float3{ 0.0f, 1.0f, 0.0f } ).getLength(), 0.0f, 1.0e-4f );
    SW_EXPECT_TRUE( ( position( kTip ) - position( kGoal ) ).getLength() < 5.0e-3f );

    _pose.setToReference( _skeleton );
    const float3 far{ 0.0f, 1.5f, 1.0f }; // 팔 길이 0.8 - 0.2 모자란다
    setBone( kGoal, far );
    run();
    SW_EXPECT_NEAR( position( kPelvis )._z, 0.2f, 1.0e-2f );
    SW_EXPECT_TRUE( ( position( kTip ) - far ).getLength() < 5.0e-2f );
}

// 골반은 애니메이션 위치에서 상한까지만 움직인다.
SW_TEST_CASE_F( RigIKTest, FullBodyPelvisStopsAtTheLimit )
{
    SW_REQUIRE_TRUE( load( makeFullBody() ) );
    setBone( kGoal, float3{ 0.0f, 1.5f, 5.0f } );
    run();
    SW_EXPECT_NEAR( ( position( kPelvis ) - float3{ 0.0f, 1.0f, 0.0f } ).getLength(), 0.5f, 1.0e-3f );
}

// 다른 사슬의 몫이 크면 골반이 덜 끌려간다(발이 몸을 붙잡는다).
SW_TEST_CASE( RigIKChainTest, FullBodyHeavyChainHoldsThePelvis )
{
    const float3 far{ 0.0f, 1.5f, 2.0f };
    RigIKTest    alone;
    SW_REQUIRE_TRUE( alone.load( makeFullBody() ) );
    alone.setBone( kGoal, far );
    alone.run();

    RigIKTest held;
    SW_REQUIRE_TRUE( held.load( makeFullBody( R"(<item target="Anchor" weight="20"><bones><item>hip.l</item><item>knee.l</item><item>foot.l</item></bones></item>)" ) ) );
    held.setBone( kGoal, far );
    held.setBone( kAnchor, held.position( kFootL ) );
    held.run();

    SW_EXPECT_TRUE( held.position( kPelvis )._z > 0.0f );
    SW_EXPECT_TRUE( held.position( kPelvis )._z < alone.position( kPelvis )._z );
}

// 골반 위에서 시작하는 사슬은 골반 풀이가 닿지 않아 묶을 때 오류다.
SW_TEST_CASE_F( RigIKTest, FullBodyChainMustStartBelowThePelvis )
{
    SW_EXPECT_LOG( "RigIKNodes.RigFullBodyIKChainMust" );
    SW_EXPECT_LOG( "RigInstance.RigNodeCouldNot" );
    SW_EXPECT_FALSE( load( R"(<item type="FullBodyIK" name="Body" pelvis="pelvis" max_pelvis_offset="0.5"><chains><item target="Goal"><bones><item>pelvis</item><item>arm</item></bones></item></chains></item>)" ) );

    RigIKTest other;
    SW_EXPECT_FALSE( other.load( R"(<item type="FullBodyIK" name="Body" pelvis="hip.l" max_pelvis_offset="0.5"><chains><item target="Goal"><bones><item>arm</item><item>fore</item></bones></item></chains></item>)" ) );
}

// 노드 복제는 인스턴스마다 상태를 따로 가진다 - 한 에셋을 두 유닛이 쓴다.
SW_TEST_CASE( RigIKChainTest, InstancesSharingAnAssetKeepTheirOwnState )
{
    const string         xml   = string( R"(<RigAsset><nodes>)" ) + makeFootPlacement( R"(interp_speed="5")" ) + "</nodes></RigAsset>";
    shared_ptr<RigAsset> asset = make_shared<RigAsset>();
    SW_REQUIRE_TRUE( asset->loadFromXMLText( xml, "shared.rig.xml" ) );

    RigIKTest first;
    RigIKTest second;
    SW_REQUIRE_TRUE( first._instance.initialize( asset, first._skeleton, nullptr, "first" ) );
    SW_REQUIRE_TRUE( second._instance.initialize( asset, second._skeleton, nullptr, "second" ) );

    FakeRigGround low;
    low._leftY  = -0.1f;
    low._rightY = -0.1f;
    first.run( &low, 0.1f );
    FakeRigGround flat;
    second.run( &flat, 0.1f ); // 첫 평가라 바로 간다 - first 의 상태를 물려받지 않는다
    SW_EXPECT_NEAR( first.position( kPelvis )._y, 0.9f, 1.0e-3f );
    SW_EXPECT_NEAR( second.position( kPelvis )._y, 1.0f, 1.0e-3f );
}

// 읽기 오류: 사슬은 두 본 이상, 다리 · 사슬 목록은 비지 않게, 배율 범위, 조준 축.
SW_TEST_CASE( RigIKChainTest, ParseRefusesBadShapes )
{
    SW_EXPECT_LOG( "Rig.Rig" );
    const auto parses = []( const string& line )
    {
        RigIKTest rig;
        return rig.load( line );
    };
    SW_EXPECT_FALSE( parses( R"(<item type="FABRIKChain" name="A" target="Goal"><bones><item>arm</item></bones></item>)" ) );
    SW_EXPECT_FALSE( parses( R"(<item type="FootPlacement" name="A" pelvis="pelvis" trace_up="1" trace_down="1" max_pelvis_drop="1" max_raise="1"><feet /></item>)" ) );
    SW_EXPECT_FALSE( parses( string( R"(<item type="StrideWarping" name="A" pelvis="pelvis" min_scale="2" max_scale="1" max_pelvis_drop="0">)" ) + kLegs + "</item>" ) );
    SW_EXPECT_FALSE( parses( string( R"(<item type="StrideWarping" name="A" pelvis="pelvis" min_scale="0" max_scale="1" max_pelvis_drop="0">)" ) + kLegs + "</item>" ) );
    SW_EXPECT_FALSE( parses( R"(<item type="Aim" name="A" bone="hand" target="Goal" aim_axis="0,0,0" />)" ) );
    SW_EXPECT_FALSE( parses( R"(<item type="Aim" name="A" bone="hand" target="Goal"><chain><item bone="arm" weight="2" /></chain></item>)" ) );
    SW_EXPECT_FALSE( parses( R"(<item type="FullBodyIK" name="A" pelvis="pelvis" max_pelvis_offset="1"><chains /></item>)" ) );
}

// 접촉 대상 읽기 오류: 광선 길이 · 방향은 필수이고, 출처는 하나이며, 광선 칸만 적은 대상은 오류다.
SW_TEST_CASE( RigIKChainTest, ContactTargetParseRefusesBadShapes )
{
    SW_EXPECT_LOG( "RigAsset.RigTraceTargetInvalid" );
    SW_EXPECT_LOG( "RigAsset.RigTargetMustName" );
    const auto loadTarget = []( const string& target )
    {
        RigIKTest rig;
        return rig.load( R"(<item type="TwoBoneIK" name="T" root="hip.l" mid="knee.l" end="foot.l" target="Wall" />)", target );
    };
    SW_EXPECT_FALSE( loadTarget( R"(<item name="Wall" trace_from="hip.l" trace_direction="0,0,1" />)" ) );                                 // 길이 없음
    SW_EXPECT_FALSE( loadTarget( R"(<item name="Wall" trace_from="hip.l" trace_distance="1" />)" ) );                                      // 방향 없음
    SW_EXPECT_FALSE( loadTarget( R"(<item name="Wall" trace_from="hip.l" bone="hip.l" trace_direction="0,0,1" trace_distance="1" />)" ) ); // 출처 둘
    SW_EXPECT_FALSE( loadTarget( R"(<item name="Wall" bone="hip.l" trace_distance="1" />)" ) );                                            // 광선 칸만
    SW_EXPECT_TRUE( loadTarget( R"(<item name="Wall" trace_from="hip.l" trace_direction="0,0,1" trace_distance="1" />)" ) );
}
