#include "pch.h"

#include "Core/Math/MathUtil.h"
#include "Core/Time/MonotonicClock.h"

#include "Engine/Animation/Rig/RigAsset.h"
#include "Engine/Animation/Rig/RigIkSolver.h"
#include "Engine/Animation/Rig/RigInstance.h"
#include "Engine/Animation/Skeletal/Pose.h"
#include "Engine/Animation/Skeletal/Skeleton.h"
#include "Engine/Resource/ResourceUtil.h"

#include "EngineTest/AnimationTestUtil.h"

#include "TestFramework/TestFramework.h"

using namespace sw;

// RigAssetTest — 리그 데이터(대상 · 순서 있는 노드)를 읽고 인스턴스로 평가한다: 엄격한 읽기, 노드 순서, 가중치(커브 · 시퀀서 칸), 제약 노드,
// 포즈 구동(RBF), 발 디딤, 평면 리그.

namespace
{
    struct TestRigAssetInternal
    {
        struct BoneSpec
        {
            const utf8* _pName;
            int32       _parent;
            float3      _offset;
        };

        static Skeleton makeSkeleton( std::initializer_list<BoneSpec> listSpec )
        {
            Skeleton skeleton;
            for ( const BoneSpec& spec : listSpec )
            {
                (void)skeleton.addBone( hashed_string( spec._pName ), spec._parent, test::makeBoneTransform( spec._offset ), float4x4::Identity );
            }
            skeleton.computeInverseBindFromReference();
            return skeleton;
        }

        /** @brief 다리 둘 — root → hips(y 1) → 허벅지(x ±0.2) → 정강이(y -0.45, 무릎 살짝 앞) → 발(y -0.45). 발은 y 0.1 에 있다. */
        static Skeleton makeLegs()
        {
            return makeSkeleton( {
                {      "root", -1,                       float3{}},
                {      "hips",  0,     float3{ 0.0f, 1.0f, 0.0f }},
                {"upperleg.l",  1,    float3{ -0.2f, 0.0f, 0.0f }},
                {"lowerleg.l",  2,  float3{ 0.0f, -0.45f, 0.02f }},
                {    "foot.l",  3, float3{ 0.0f, -0.45f, -0.02f }},
                {"upperleg.r",  1,     float3{ 0.2f, 0.0f, 0.0f }},
                {"lowerleg.r",  5,  float3{ 0.0f, -0.45f, 0.02f }},
                {    "foot.r",  6, float3{ 0.0f, -0.45f, -0.02f }},
            } );
        }

        static shared_ptr<const RigAsset> parseRig( string_view json )
        {
            shared_ptr<RigAsset> asset = make_shared<RigAsset>();
            if ( asset->parseJson( json, "test.rig.json" ) == false )
                return nullptr;
            return asset;
        }

        /** @brief 레퍼런스 포즈에서 리그를 한 번 평가하고 모델 공간을 냅니다. */
        static void evaluateReference( RigInstance& instance, const Skeleton& skeleton, Pose& outPose, vector<float4x4>& outListModel )
        {
            outPose.setToReference( skeleton );
            instance.evaluate( outPose, skeleton.getParentIndices(), float4x4::Identity );
            outPose.computeModelSpace( skeleton.getParentIndices(), outListModel );
        }

        static RigTargetValue makeTargetAt( const float3& position, const quaternion& rotation = quaternion::Identity )
        {
            RigTargetValue value{};
            value._position = position;
            value._rotation = rotation;
            value._bValid   = SW_TRUE;
            return value;
        }

        static bool isNear( const float3& expected, const float3& actual, float32 tolerance ) { return ( expected - actual ).getLength() <= tolerance; }

        /** @brief 이름 → 값 표 커브 출처입니다. */
        class TestCurveSource final : public IRigCurveSource
        {
        public:
            float32 getCurveValue( const hashed_string& curveName ) const override { return curveName == hashed_string( "Ik" ) ? _value : 0.0f; }
            float32 _value{ 0.0f };
        };

        /** @brief 평평한 기울기 면 y = slope × x 입니다(위에서 쏜 광선만). */
        class SlopeGround final : public IRigGroundQuery
        {
        public:
            bool raycastGround( const float3& origin, const float3& direction, float32 maxDistance, RigGroundHit& outHit ) const override
            {
                (void)direction;
                const float32 groundY = _slope * origin._x;
                if ( origin._y < groundY || origin._y - groundY > maxDistance )
                    return false;
                outHit._position = float3{ origin._x, groundY, origin._z };
                outHit._normal   = float3{ -_slope, 1.0f, 0.0f }.normalize();
                return true;
            }
            float32 _slope{ 0.3f };
        };
    };
} // namespace

/**
 * @brief [RigAssetTest] 리그 데이터 — 대상 · 노드를 순서대로 읽고, 모르는 종류 · 키 · 대상 · 본, 둘 이상의 출처, 겹친 이름은 오류다
 */
SW_TEST_CASE( RigAssetTest, ParsesInOrderAndRejectsUnknownNames )
{
    const Skeleton             skeleton = test::makeChainSkeleton( 3 );
    const string_view          valid    = R"({ "targets": [ { "name": "Goal", "object": "Ball" }, { "name": "Tip", "bone": "bone2", "translation": [0, 0.5, 0] } ],
        "nodes": [ { "type": "TwoBoneIk", "name": "Arm", "root": "bone0", "mid": "bone1", "end": "bone2", "target": "Goal" },
                   { "type": "Aim", "name": "Look", "bone": "bone2", "target": "Goal", "max_degrees": 45 } ] })";
    shared_ptr<const RigAsset> asset    = TestRigAssetInternal::parseRig( valid );
    SW_ASSERT_NOT_NULL( asset );
    SW_ASSERT_EQUAL( 2u, static_cast<uint32>( asset->getNodes().size() ) );
    SW_EXPECT_TRUE( asset->getNodes()[0]->getName() == hashed_string( "Arm" ) );
    SW_EXPECT_TRUE( asset->getNodes()[1]->getName() == hashed_string( "Look" ) );
    SW_EXPECT_TRUE( asset->getTargets()[0].isExternal() );
    SW_EXPECT_FALSE( asset->getTargets()[1].isExternal() );
    RigInstance instance;
    SW_EXPECT_TRUE( instance.initialize( asset, skeleton, nullptr, "test" ) );

    test::ScopedDefensiveTestLog expected( "malformed rig data is rejected with an error" );
    const string_view            listBad[] = {
        R"({ "nodes": [ { "type": "NoSuchNode", "name": "X" } ] })",
        R"({ "nodes": [ { "type": "Aim", "name": "X", "bone": "bone1", "target": "T", "colour": 1 } ] })",
        R"({ "targets": [ { "name": "T", "bone": "bone1", "object": "Ball" } ], "nodes": [ { "type": "Aim", "name": "X", "bone": "bone1", "target": "T" } ] })",
        R"({ "targets": [ { "name": "T", "bone": "bone1" }, { "name": "T", "bone": "bone2" } ], "nodes": [ { "type": "Aim", "name": "X", "bone": "bone1", "target": "T" } ] })",
        R"({ "nodes": [ { "type": "Aim", "name": "X", "bone": "bone1", "target": "T" }, { "type": "Aim", "name": "X", "bone": "bone1", "target": "T" } ] })",
        R"({ "nodes": [ { "type": "CcdChain", "name": "X", "bones": ["bone0", "bone1"], "target": "T", "limits": [ { "bone": "bone0", "type": "Ball" } ] } ] })",
        R"({ "nodes": [], "extra": true })",
    };
    for ( const string_view bad : listBad )
    {
        SW_EXPECT_TRUE( TestRigAssetInternal::parseRig( bad ) == nullptr );
    }

    // 읽히지만 묶이지 않는다 — 없는 대상 · 없는 본 · 사슬이 아닌 본 순서.
    const string_view listUnbindable[] = {
        R"({ "nodes": [ { "type": "Aim", "name": "X", "bone": "bone1", "target": "Missing" } ] })",
        R"({ "targets": [ { "name": "T", "bone": "nose" } ], "nodes": [ { "type": "Aim", "name": "X", "bone": "bone1", "target": "T" } ] })",
        R"({ "targets": [ { "name": "T", "bone": "bone0" } ], "nodes": [ { "type": "FabrikChain", "name": "X", "bones": ["bone2", "bone0"], "target": "T" } ] })",
        R"({ "targets": [ { "name": "T", "socket": "Grip" } ], "nodes": [ { "type": "Aim", "name": "X", "bone": "bone1", "target": "T" } ] })",
    };
    for ( const string_view unbindable : listUnbindable )
    {
        shared_ptr<const RigAsset> parsed = TestRigAssetInternal::parseRig( unbindable );
        SW_ASSERT_NOT_NULL( parsed );
        RigInstance failing;
        SW_EXPECT_FALSE( failing.initialize( parsed, skeleton, nullptr, "test" ) );
        SW_EXPECT_FALSE( failing.isInitialized() );
    }
}

/**
 * @brief [RigAssetTest] 노드 순서가 결과를 바꾼다 — "위치 복사 → 거리 제한" 이면 본이 B 에서 0.5 m 안, "거리 제한 → 위치 복사" 면 A 에 선다
 */
SW_TEST_CASE( RigAssetTest, NodeOrderChangesResult )
{
    const Skeleton    skeleton      = test::makeChainSkeleton( 3 );
    const string_view copyThenLimit = R"({ "targets": [ { "name": "A", "object": "A" }, { "name": "B", "object": "B" } ], "nodes": [
        { "type": "Position", "name": "Copy", "bone": "bone2", "target": "A" },
        { "type": "Distance", "name": "Limit", "bone": "bone2", "target": "B", "max": 0.5 } ] })";
    const string_view limitThenCopy = R"({ "targets": [ { "name": "A", "object": "A" }, { "name": "B", "object": "B" } ], "nodes": [
        { "type": "Distance", "name": "Limit", "bone": "bone2", "target": "B", "max": 0.5 },
        { "type": "Position", "name": "Copy", "bone": "bone2", "target": "A" } ] })";
    const float3      pointA{ 3.0f, 0.0f, 0.0f };
    const float3      pointB{ 0.0f, 2.0f, 0.0f };
    float3            arrResult[2]{};
    for ( uint32 order = 0; order < 2; ++order )
    {
        RigInstance instance;
        SW_ASSERT_TRUE( instance.initialize( TestRigAssetInternal::parseRig( order == 0 ? copyThenLimit : limitThenCopy ), skeleton, nullptr, "test" ) );
        instance.setExternalTarget( 0, TestRigAssetInternal::makeTargetAt( pointA ) );
        instance.setExternalTarget( 1, TestRigAssetInternal::makeTargetAt( pointB ) );
        Pose             pose;
        vector<float4x4> listModel;
        TestRigAssetInternal::evaluateReference( instance, skeleton, pose, listModel );
        arrResult[order] = listModel[2].getTranslation();
    }
    SW_EXPECT_NEAR_EQUAL( 0.5f, ( arrResult[0] - pointB ).getLength(), 1e-4f );
    SW_EXPECT_TRUE( TestRigAssetInternal::isNear( pointA, arrResult[1], 1e-4f ) );
}

/**
 * @brief [RigAssetTest] 노드 가중치 — 기본값 × 클립 커브 × 시퀀서 칸. 커브 0.5 면 반만, 칸 0 이면 꺼지고, 칸을 놓으면 커브로 돌아오며, 커브가 없으면 0 이다
 */
SW_TEST_CASE( RigAssetTest, WeightFollowsCurveAndSequencerSlot )
{
    const Skeleton    skeleton = test::makeChainSkeleton( 3 );
    const string_view json     = R"({ "targets": [ { "name": "A", "object": "A" } ], "nodes": [
        { "type": "Position", "name": "Copy", "bone": "bone2", "target": "A", "weight_curve": "Ik", "weight_slot": "Shot" } ] })";
    RigInstance       instance;
    SW_ASSERT_TRUE( instance.initialize( TestRigAssetInternal::parseRig( json ), skeleton, nullptr, "test" ) );
    TestRigAssetInternal::TestCurveSource curve;
    instance.setCurveSource( &curve );
    instance.setExternalTarget( 0, TestRigAssetInternal::makeTargetAt( float3{ 2.0f, 2.0f, 0.0f } ) );
    const float3 rest{ 0.0f, 2.0f, 0.0f };

    Pose             pose;
    vector<float4x4> listModel;
    curve._value = 0.5f;
    TestRigAssetInternal::evaluateReference( instance, skeleton, pose, listModel );
    SW_EXPECT_TRUE( TestRigAssetInternal::isNear( float3{ 1.0f, 2.0f, 0.0f }, listModel[2].getTranslation(), 1e-4f ) );

    instance.setSlotWeight( "Shot", 0.0f );
    TestRigAssetInternal::evaluateReference( instance, skeleton, pose, listModel );
    SW_EXPECT_TRUE( TestRigAssetInternal::isNear( rest, listModel[2].getTranslation(), 1e-4f ) );
    instance.setSlotWeight( "Shot", 1.0f );
    curve._value = 1.0f;
    TestRigAssetInternal::evaluateReference( instance, skeleton, pose, listModel );
    SW_EXPECT_TRUE( TestRigAssetInternal::isNear( float3{ 2.0f, 2.0f, 0.0f }, listModel[2].getTranslation(), 1e-4f ) );

    instance.clearSlotWeight( "Shot" );
    instance.setCurveSource( nullptr ); // 커브 없음 = 0
    TestRigAssetInternal::evaluateReference( instance, skeleton, pose, listModel );
    SW_EXPECT_TRUE( TestRigAssetInternal::isNear( rest, listModel[2].getTranslation(), 1e-4f ) );
}

/**
 * @brief [RigAssetTest] 부모 바꾸기 — 바꾸는 프레임에 본이 튀지 않고(지난 출력 자리 그대로), 정착 시간이 지나면 새 부모 자리에 앉는다
 */
SW_TEST_CASE( RigAssetTest, ParentSwitchKeepsOffsetThenSettles )
{
    const Skeleton    skeleton = test::makeChainSkeleton( 3 );
    const string_view json     = R"({ "targets": [ { "name": "Hand", "object": "Hand" }, { "name": "Back", "object": "Back" } ], "nodes": [
        { "type": "ParentSwitch", "name": "Weapon", "bone": "bone2", "parents": ["Hand", "Back"], "settle_seconds": 0.5 } ] })";
    RigInstance       instance;
    SW_ASSERT_TRUE( instance.initialize( TestRigAssetInternal::parseRig( json ), skeleton, nullptr, "test" ) );
    const float3 hand{ 1.0f, 1.0f, 0.0f };
    const float3 back{ -1.0f, 3.0f, 0.0f };
    instance.setExternalTarget( 0, TestRigAssetInternal::makeTargetAt( hand ) );
    instance.setExternalTarget( 1, TestRigAssetInternal::makeTargetAt( back ) );
    RigPrepareContext prepare{};
    prepare._deltaSeconds = 0.6f;
    Pose             pose;
    vector<float4x4> listModel;
    for ( uint32 frame = 0; frame < 2; ++frame )
    {
        instance.prepare( prepare );
        TestRigAssetInternal::evaluateReference( instance, skeleton, pose, listModel );
    }
    SW_EXPECT_TRUE( TestRigAssetInternal::isNear( hand, listModel[2].getTranslation(), 1e-4f ) ); // 처음 부모(손)에 정착했다

    SW_EXPECT_TRUE( instance.setNodeControl( "Weapon", "parent", 1.0f ) );
    SW_EXPECT_FALSE( instance.setNodeControl( "Weapon", "parent", 5.0f ) );
    prepare._deltaSeconds = 0.0f;
    instance.prepare( prepare );
    TestRigAssetInternal::evaluateReference( instance, skeleton, pose, listModel );
    SW_EXPECT_TRUE( TestRigAssetInternal::isNear( hand, listModel[2].getTranslation(), 1e-4f ) ); // 바꾼 프레임 — 튀지 않음

    prepare._deltaSeconds = 0.25f;
    instance.prepare( prepare );
    TestRigAssetInternal::evaluateReference( instance, skeleton, pose, listModel );
    const float3 halfway = listModel[2].getTranslation();
    SW_EXPECT_FALSE( TestRigAssetInternal::isNear( hand, halfway, 0.1f ) );
    SW_EXPECT_FALSE( TestRigAssetInternal::isNear( back, halfway, 0.1f ) );
    instance.prepare( prepare );
    TestRigAssetInternal::evaluateReference( instance, skeleton, pose, listModel );
    SW_EXPECT_TRUE( TestRigAssetInternal::isNear( back, listModel[2].getTranslation(), 1e-4f ) );
}

/**
 * @brief [RigAssetTest] 트위스트 분배 — 손의 비틀림 90° 중 절반을 손목(손의 조상)이 받고 손은 그만큼 덜 비튼다. 손의 모델 방향은 그대로다
 */
SW_TEST_CASE( RigAssetTest, TwistDistributionSpreadsForearmTwist )
{
    const Skeleton    skeleton = TestRigAssetInternal::makeSkeleton( {
        {"lowerarm", -1,                    float3{}},
        {   "wrist",  0, float3{ 0.0f, 0.25f, 0.0f }},
        {    "hand",  1, float3{ 0.0f, 0.08f, 0.0f }},
    } );
    const string_view json     = R"({ "nodes": [ { "type": "TwistDistribution", "name": "Forearm", "source": "hand", "axis": [0, 1, 0],
        "bones": [ { "bone": "wrist", "weight": 0.5 } ] } ] })";
    RigInstance       instance;
    SW_ASSERT_TRUE( instance.initialize( TestRigAssetInternal::parseRig( json ), skeleton, nullptr, "test" ) );
    Pose pose;
    pose.setToReference( skeleton );
    BoneTransform hand = pose.getBoneTransform( 2 );
    hand._rotation     = quaternion::createFromAxisAngle( float3::UnitY, MathUtil::kHalfPi ) * quaternion::createFromAxisAngle( float3::UnitX, 0.3f );
    pose.setBoneTransform( 2, hand );
    vector<float4x4> listBefore;
    pose.computeModelSpace( skeleton.getParentIndices(), listBefore );

    instance.evaluate( pose, skeleton.getParentIndices(), float4x4::Identity );
    vector<float4x4> listAfter;
    pose.computeModelSpace( skeleton.getParentIndices(), listAfter );
    quaternion swing{};
    quaternion twist{};
    RigIkSolver::decomposeSwingTwist( pose.getBoneTransform( 1 )._rotation, float3::UnitY, swing, twist );
    SW_EXPECT_NEAR_EQUAL( MathUtil::kHalfPi * 0.5f, RigIkSolver::computeTwistAngle( twist, float3::UnitY ), 1e-3f );
    SW_EXPECT_NEAR_EQUAL( 1.0f, MathUtil::abs( listBefore[2].getRotation().dot( listAfter[2].getRotation() ) ), 1e-4f );
}

/**
 * @brief [RigAssetTest] 포즈 구동(RBF) — 관절이 굽힘 포즈에 있으면 보정 모프 1 · 보정 본이 그 포즈 값, 쉬는 포즈면 0, 그 사이면 사이 값이다
 */
SW_TEST_CASE( RigAssetTest, PoseDriverInterpolatesCorrectives )
{
    const Skeleton    skeleton = test::makeChainSkeleton( 3 );
    const string_view json     = R"({ "nodes": [ { "type": "PoseDriver", "name": "Elbow", "driver": "bone1", "radius_degrees": 60, "poses": [
        { "name": "Rest", "rotation": [0, 0, 0] },
        { "name": "Bent", "rotation": [90, 0, 0], "morphs": [ { "morph": "ElbowBend", "weight": 1 } ],
          "bones": [ { "bone": "bone2", "translation": [0, 0, 0.1] } ] } ] } ] })";
    RigInstance       instance;
    SW_ASSERT_TRUE( instance.initialize( TestRigAssetInternal::parseRig( json ), skeleton, nullptr, "test" ) );

    const float32 arrDegree[] = { 0.0f, 45.0f, 90.0f };
    float32       arrMorph[3]{};
    float32       arrShift[3]{};
    for ( uint32 index = 0; index < 3; ++index )
    {
        Pose pose;
        pose.setToReference( skeleton );
        BoneTransform elbow = pose.getBoneTransform( 1 );
        elbow._rotation     = quaternion::createFromYawPitchRoll( 0.0f, arrDegree[index] * MathUtil::kDegreeToRadian, 0.0f );
        pose.setBoneTransform( 1, elbow );
        instance.evaluate( pose, skeleton.getParentIndices(), float4x4::Identity );
        SW_ASSERT_EQUAL( 1u, static_cast<uint32>( instance.getMorphWeights().size() ) );
        arrMorph[index] = instance.getMorphWeights()[0]._weight;
        arrShift[index] = pose.getBoneTransform( 2 )._translation._z;
    }
    SW_EXPECT_NEAR_EQUAL( 0.0f, arrMorph[0], 1e-4f );
    SW_EXPECT_NEAR_EQUAL( 1.0f, arrMorph[2], 1e-4f );
    SW_EXPECT_TRUE( 0.1f < arrMorph[1] && arrMorph[1] < 0.9f );
    SW_EXPECT_NEAR_EQUAL( 0.0f, arrShift[0], 1e-5f );
    SW_EXPECT_NEAR_EQUAL( 0.1f, arrShift[2], 1e-5f );
    SW_EXPECT_NEAR_EQUAL( 0.1f * arrMorph[1], arrShift[1], 1e-5f );
}

/**
 * @brief [RigAssetTest] 발 디딤 — 기울기 면(y = 0.3x) 위에서 낮은 쪽 발만큼 골반을 내리고, 두 발이 각자 땅 높이에 서고, 발이 면의 법선 쪽으로 기운다
 */
SW_TEST_CASE( RigAssetTest, FootPlacementPlantsFeetOnSlope )
{
    const Skeleton    skeleton = TestRigAssetInternal::makeLegs();
    const string_view json     = R"({ "nodes": [ { "type": "FootPlacement", "name": "Feet", "pelvis": "hips", "interp_speed": 0, "max_align_degrees": 45,
        "feet": [ { "root": "upperleg.l", "mid": "lowerleg.l", "end": "foot.l" }, { "root": "upperleg.r", "mid": "lowerleg.r", "end": "foot.r" } ] } ] })";
    RigInstance       instance;
    SW_ASSERT_TRUE( instance.initialize( TestRigAssetInternal::parseRig( json ), skeleton, nullptr, "test" ) );
    TestRigAssetInternal::SlopeGround ground;
    RigPrepareContext                 prepare{};
    prepare._worldFromModel = float4x4::Identity;
    prepare._pGroundQuery   = &ground;
    prepare._deltaSeconds   = 1.0f / 60.0f;

    Pose             pose;
    vector<float4x4> listReference;
    pose.setToReference( skeleton );
    pose.computeModelSpace( skeleton.getParentIndices(), listReference );
    vector<float4x4> listModel;
    for ( uint32 frame = 0; frame < 2; ++frame ) // 첫 평가가 애니메이션 발 자리를 적고, 다음 준비가 그 자리로 광선을 쏜다
    {
        instance.prepare( prepare );
        TestRigAssetInternal::evaluateReference( instance, skeleton, pose, listModel );
    }
    const float32 leftGround  = 0.3f * listReference[4].getTranslation()._x;
    const float32 rightGround = 0.3f * listReference[7].getTranslation()._x;
    SW_EXPECT_NEAR_EQUAL( listReference[4].getTranslation()._y + leftGround, listModel[4].getTranslation()._y, 2e-3f );
    SW_EXPECT_NEAR_EQUAL( listReference[7].getTranslation()._y + rightGround, listModel[7].getTranslation()._y, 2e-3f );
    SW_EXPECT_NEAR_EQUAL( listReference[1].getTranslation()._y + leftGround, listModel[1].getTranslation()._y, 1e-4f ); // 골반이 낮은 쪽만큼 내려갔다
    // 발의 위(+Y)가 면 법선(-0.3, 1) 쪽으로 기울었다.
    const float3 footUp = float3::transformVector( float3::UnitY, listModel[7] ).normalize();
    SW_EXPECT_TRUE( footUp._x < -0.2f );

    // 땅이 없으면 발은 애니메이션 자리다.
    prepare._pGroundQuery = nullptr;
    instance.prepare( prepare );
    TestRigAssetInternal::evaluateReference( instance, skeleton, pose, listModel );
    SW_EXPECT_TRUE( TestRigAssetInternal::isNear( listReference[7].getTranslation(), listModel[7].getTranslation(), 2e-3f ) );
}

/**
 * @brief [RigAssetTest] 평면 리그(`"planar": true`) — 2D 스켈레톤의 CCD 사슬 · 스프링이 평면 밖 목표 · 중력에도 XY 평면에 남는다
 */
SW_TEST_CASE( RigAssetTest, PlanarRigStaysInPlane )
{
    const Skeleton    skeleton = test::makeChainSkeleton( 4 );
    const string_view json     = R"({ "planar": true, "targets": [ { "name": "Goal", "object": "Goal" } ], "nodes": [
        { "type": "CcdChain", "name": "Arm", "bones": ["bone0", "bone1", "bone2"], "target": "Goal", "iterations": 32 },
        { "type": "SpringChain", "name": "Tail", "bones": ["bone2", "bone3"], "gravity": [0, -9.8, 4], "stiffness": 0 } ] })";
    RigInstance       instance;
    SW_ASSERT_TRUE( instance.initialize( TestRigAssetInternal::parseRig( json ), skeleton, nullptr, "test" ) );
    SW_EXPECT_TRUE( instance.getSolveSpace()._bPlanar == SW_TRUE );
    instance.setExternalTarget( 0, TestRigAssetInternal::makeTargetAt( float3{ 1.2f, 1.0f, 0.8f } ) );
    RigPrepareContext prepare{};
    prepare._deltaSeconds = 1.0f / 30.0f;
    Pose             pose;
    vector<float4x4> listModel;
    for ( uint32 frame = 0; frame < 30; ++frame )
    {
        instance.prepare( prepare );
        TestRigAssetInternal::evaluateReference( instance, skeleton, pose, listModel );
    }
    for ( const float4x4& model : listModel )
    {
        SW_EXPECT_NEAR_EQUAL( 0.0f, model.getTranslation()._z, 1e-3f );
    }
    SW_EXPECT_TRUE( TestRigAssetInternal::isNear( float3{ 1.2f, 1.0f, 0.0f }, listModel[2].getTranslation(), 5e-3f ) );
}

/**
 * @brief [RigAssetTest] 데모 리그 비용 — 기사(`knight.rig.json`, 41 본: 발 디딤 · 시선 · 왼손 IK · 팔뚝 비틀림)와 망토(스프링 사슬)를 한 번 평가하는 시간을 잰다
 * @details 숫자를 남기는 시험이다(Release 로 돌려 읽는다). 단언은 넉넉한 상한 하나 — 회귀가 몇 배로 커졌을 때만 걸린다.
 */
SW_TEST_CASE( RigAssetTest, KnightRigEvaluationCost )
{
    SW_ASSERT_TRUE( ResourceUtil::initialize() );
    Skeleton knight;
    SW_ASSERT_TRUE( knight.loadFromResource( "game/shooter3d/models/kaykit/knight/knight.skeleton.json" ) );
    shared_ptr<RigAsset> asset = make_shared<RigAsset>();
    SW_ASSERT_TRUE( asset->loadFromResource( "game/shooter3d/rigs/knight.rig.json" ) );
    RigInstance instance;
    SW_ASSERT_TRUE( instance.initialize( asset, knight, nullptr, "knight" ) );
    instance.setExternalTarget( static_cast<uint32>( instance.findTargetIndex( "LookTarget" ) ), TestRigAssetInternal::makeTargetAt( float3{ 0.5f, 1.4f, 1.2f } ) );
    RigTargetValue grip = TestRigAssetInternal::makeTargetAt( float3{ 0.0f, -0.04f, 0.5f } );
    grip._relativeBone  = knight.findBoneIndex( "handslot.r" );
    instance.setExternalTarget( static_cast<uint32>( instance.findTargetIndex( "Grip" ) ), grip );
    TestRigAssetInternal::SlopeGround ground;
    RigPrepareContext                 prepare{};
    prepare._worldFromModel = float4x4::Identity;
    prepare._pGroundQuery   = &ground;
    prepare._deltaSeconds   = 1.0f / 60.0f;

    Pose reference;
    reference.setToReference( knight );
    Pose pose;
#if defined( SW_DEBUG )
    // Debug(· ASAN)는 수십 배 느리다 — 숫자는 Release 에서 읽고, 여기서는 돌기만 하는지 본다.
    const uint32  kIteration = 200;
    const float64 kLimit     = 20000.0;
#else
    const uint32  kIteration = 2000;
    const float64 kLimit     = 500.0;
#endif
    const Stopwatch stopwatch;
    for ( uint32 iteration = 0; iteration < kIteration; ++iteration )
    {
        instance.prepare( prepare );
        pose = reference;
        instance.evaluate( pose, knight.getParentIndices(), float4x4::Identity );
    }
    const float64 microseconds = static_cast<float64>( stopwatch.getElapsedNanoseconds() ) / 1000.0 / static_cast<float64>( kIteration );
    SW_LOG_INFO( "Knight rig (4 nodes, 41 bones): %# us per prepare + evaluate", microseconds );
    SW_EXPECT_TRUE( microseconds < kLimit );

    // 망토 리그(뿌리 고정 + 스프링 사슬 4 본 · 캡슐 하나) — 프레임 시간 1/60 이라 스텝 하나씩.
    Skeleton cape;
    SW_ASSERT_TRUE( cape.loadFromResource( "game/shooter3d/rigs/knight_cape.skeleton.json" ) );
    shared_ptr<RigAsset> capeAsset = make_shared<RigAsset>();
    SW_ASSERT_TRUE( capeAsset->loadFromResource( "game/shooter3d/rigs/knight_cape.rig.json" ) );
    RigInstance capeInstance;
    SW_ASSERT_TRUE( capeInstance.initialize( capeAsset, cape, nullptr, "cape" ) );
    Pose capeReference;
    capeReference.setToReference( cape );
    Pose            capePose;
    const Stopwatch capeStopwatch;
    for ( uint32 iteration = 0; iteration < kIteration; ++iteration )
    {
        // 어깨가 원을 그리며 움직인다 — 스프링이 매 스텝 일한다.
        const float32 angle = static_cast<float32>( iteration ) * 0.05f;
        capeInstance.setExternalTarget( 0, TestRigAssetInternal::makeTargetAt( float3{ 0.3f * MathUtil::sin( angle ), 1.2f, 0.3f * MathUtil::cos( angle ) } ) );
        capeInstance.prepare( prepare );
        capePose = capeReference;
        capeInstance.evaluate( capePose, cape.getParentIndices(), float4x4::Identity );
    }
    const float64 capeMicroseconds = static_cast<float64>( capeStopwatch.getElapsedNanoseconds() ) / 1000.0 / static_cast<float64>( kIteration );
    SW_LOG_INFO( "Cape rig (spring chain 4 bones, 1 capsule): %# us per prepare + evaluate", capeMicroseconds );
    SW_EXPECT_TRUE( capeMicroseconds < kLimit );
}
