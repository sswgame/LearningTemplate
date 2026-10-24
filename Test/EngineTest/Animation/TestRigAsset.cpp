#include "pch.h"

#include "Core/Math/MathUtil.h"
#include "Core/Math/MatrixMath.h"
#include "Core/String/StringBuilder.h"
#include "Core/Time/MonotonicClock.h"

#include "Engine/Animation/Rig/RigAsset.h"
#include "Engine/Animation/Rig/RigIKSolver.h"
#include "Engine/Animation/Rig/RigInstance.h"
#include "Engine/Animation/Skeletal/LocomotionSpeedUtil.h"
#include "Engine/Animation/Skeletal/Pose.h"
#include "Engine/Animation/Skeletal/Skeleton.h"
#include "Engine/Resource/ResourcePaths.h"

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
            if ( asset->parseXML( json, "test.rig.xml" ) == false )
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

        /** @brief 노드를 만들지 않는 생성 함수입니다(교체 전). */
        static unique_ptr<RigNode> createNoNode() { return nullptr; }
        /** @brief 엔진의 `Aim` 노드를 만드는 생성 함수입니다(교체 후). */
        static unique_ptr<RigNode> createAimNode() { return RigNodeRegistry::getInstance().createNode( hashed_string( "Aim" ) ); }

        /** @brief 이름 → 값 표 커브 출처입니다. */
        class TestCurveSource final : public IRigCurveSource
        {
        public:
            float32 getCurveValue( const hashed_string& curveName ) const override { return curveName == hashed_string( "IK" ) ? _value : 0.0f; }
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
    const string_view          valid    = R"(<RigAsset>
	<targets>
		<item name="Goal" object="Ball" />
		<item name="Tip" bone="bone2" translation="0,0.5,0" />
	</targets>
	<nodes>
		<item type="TwoBoneIK" name="Arm" root="bone0" mid="bone1" end="bone2" target="Goal" />
		<item type="Aim" name="Look" bone="bone2" target="Goal" max_degrees="45" />
	</nodes>
</RigAsset>)";
    shared_ptr<const RigAsset> asset    = TestRigAssetInternal::parseRig( valid );
    SW_ASSERT_NOT_NULL( asset );
    SW_ASSERT_EQUAL( 2u, static_cast<uint32>( asset->getNodes().size() ) );
    SW_EXPECT_TRUE( asset->getNodes()[0]->getName() == hashed_string( "Arm" ) );
    SW_EXPECT_TRUE( asset->getNodes()[1]->getName() == hashed_string( "Look" ) );
    SW_EXPECT_TRUE( asset->getTargets()[0].isExternal() );
    SW_EXPECT_FALSE( asset->getTargets()[1].isExternal() );
    RigInstance instance;
    SW_EXPECT_TRUE( instance.initialize( asset, skeleton, nullptr, "test" ) );

    // 같은 종류 이름으로 다시 등록하면 생성 함수를 교체하고 이름은 하나로 남는다.
    RigNodeRegistry&    registry     = RigNodeRegistry::getInstance();
    const hashed_string replacedType = hashed_string( "TestReplacedRigNode" );
    registry.registerNode( replacedType, &TestRigAssetInternal::createNoNode );
    const size_t typeCount = registry.getTypeNames().size();
    SW_EXPECT_TRUE( registry.createNode( replacedType ) == nullptr );
    registry.registerNode( replacedType, &TestRigAssetInternal::createAimNode );
    SW_EXPECT_TRUE( registry.createNode( replacedType ) != nullptr );
    SW_EXPECT_EQUAL( typeCount, registry.getTypeNames().size() );

    test::ScopedDefensiveTestLog expected( "malformed rig data is rejected with an error" );
    const string_view            listBad[] = {
        R"(<RigAsset><nodes><item type="NoSuchNode" name="X" /></nodes></RigAsset>)",
        R"(<RigAsset><nodes><item type="Aim" name="X" bone="bone1" target="T" colour="1" /></nodes></RigAsset>)",
        R"(<RigAsset><targets><item name="T" bone="bone1" object="Ball" /></targets><nodes><item type="Aim" name="X" bone="bone1" target="T" /></nodes></RigAsset>)",
        R"(<RigAsset><targets><item name="T" bone="bone1" /><item name="T" bone="bone2" /></targets><nodes><item type="Aim" name="X" bone="bone1" target="T" /></nodes></RigAsset>)",
        R"(<RigAsset><nodes><item type="Aim" name="X" bone="bone1" target="T" /><item type="Aim" name="X" bone="bone1" target="T" /></nodes></RigAsset>)",
        R"(<RigAsset><nodes><item type="CcdChain" name="X" target="T"><bones><item>bone0</item><item>bone1</item></bones><limits><item bone="bone0" type="Ball" /></limits></item></nodes></RigAsset>)",
        R"(<RigAsset extra="true"><nodes /></RigAsset>)",
    };
    for ( const string_view bad : listBad )
    {
        SW_EXPECT_TRUE( TestRigAssetInternal::parseRig( bad ) == nullptr );
    }

    // 읽히지만 묶이지 않는다 — 없는 대상 · 없는 본 · 사슬이 아닌 본 순서.
    const string_view listUnbindable[] = {
        R"(<RigAsset><nodes><item type="Aim" name="X" bone="bone1" target="Missing" /></nodes></RigAsset>)",
        R"(<RigAsset><targets><item name="T" bone="nose" /></targets><nodes><item type="Aim" name="X" bone="bone1" target="T" /></nodes></RigAsset>)",
        R"(<RigAsset><targets><item name="T" bone="bone0" /></targets><nodes><item type="FabrikChain" name="X" target="T"><bones><item>bone2</item><item>bone0</item></bones></item></nodes></RigAsset>)",
        R"(<RigAsset><targets><item name="T" socket="Grip" /></targets><nodes><item type="Aim" name="X" bone="bone1" target="T" /></nodes></RigAsset>)",
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
 * @brief [RigAssetTest] 전신 IK — 손이 닿지 않으면 골반이 손 쪽으로 옮겨 가고(발이 끄는 만큼 덜), 닿는 목표에는 골반이 그대로다. 골반 이동은 상한을 넘지 않는다
 * @details 몸: hips(y 1) 아래 팔(어깨 (0.2, 1.5) · 길이 0.6)과 오른 다리(길이 0.9, 발 (0.2, 0.1)). 손 목표 (1.5, 1.5) 는 어깨에서 1.3 이라 0.7 모자란다.
 *          발 목표는 지금 발 자리라 골반이 옆으로 가면 다리가 모자라 골반을 되끈다 — 두 당김의 가중 평균에서 멈춘다.
 */
SW_TEST_CASE( RigAssetTest, FullBodyIKShiftsPelvisTowardWeightedEffectors )
{
    const Skeleton skeleton = TestRigAssetInternal::makeSkeleton( {
        {      "root", -1,                       float3{}},
        {      "hips",  0,     float3{ 0.0f, 1.0f, 0.0f }},
        {  "shoulder",  1,     float3{ 0.2f, 0.5f, 0.0f }},
        {     "elbow",  2,   float3{ 0.3f, -0.03f, 0.0f }}, // 팔꿈치를 살짝 굽혀 둔다 — 곧게 편 사슬은 FABRIK 이 굽힐 쪽을 정하지 못한다
        {      "hand",  3,    float3{ 0.3f, 0.03f, 0.0f }},
        {"upperleg.r",  1,     float3{ 0.2f, 0.0f, 0.0f }},
        {"lowerleg.r",  5,  float3{ 0.0f, -0.45f, 0.02f }},
        {    "foot.r",  6, float3{ 0.0f, -0.45f, -0.02f }},
    } );
    auto           makeRig  = []( float32 footWeight, float32 maxOffset )
    {
        StringBuilder<constant::kMaxBuffer1024> xml;
        xml.append( R"(<RigAsset><targets><item name="Hand" object="Hand" /><item name="Foot" object="Foot" /></targets><nodes>
            <item type="FullBodyIK" name="Body" pelvis="hips" pelvis_iterations="8" max_pelvis_offset=")" )
            .append( maxOffset )
            .append( R"("><chains><item target="Hand"><bones><item>shoulder</item><item>elbow</item><item>hand</item></bones></item>
            <item target="Foot" weight=")" )
            .append( footWeight )
            .append( R"("><bones><item>upperleg.r</item><item>lowerleg.r</item><item>foot.r</item></bones></item></chains></item></nodes></RigAsset>)" );
        return TestRigAssetInternal::parseRig( string_view( xml.c_str(), xml.size() ) );
    };
    // 골반 · 손의 모델 위치를 냅니다.
    auto solve = [&skeleton]( const shared_ptr<const RigAsset>& asset, const float3& hand, float3& outPelvis, float3& outHand )
    {
        RigInstance instance;
        if ( instance.initialize( asset, skeleton, nullptr, "test" ) == false )
            return false;
        instance.setExternalTarget( 0, TestRigAssetInternal::makeTargetAt( hand ) );
        instance.setExternalTarget( 1, TestRigAssetInternal::makeTargetAt( float3{ 0.2f, 0.1f, 0.0f } ) );
        Pose             pose;
        vector<float4x4> listModel;
        TestRigAssetInternal::evaluateReference( instance, skeleton, pose, listModel );
        outPelvis = listModel[1].getTranslation();
        outHand   = listModel[4].getTranslation();
        return true;
    };
    const float3 farHand{ 1.5f, 1.5f, 0.0f };
    float3       lightPelvis{};
    float3       lightHand{};
    float3       heavyPelvis{};
    float3       heavyHand{};
    SW_ASSERT_TRUE( solve( makeRig( 1.0f, 1.0f ), farHand, lightPelvis, lightHand ) );
    SW_ASSERT_TRUE( solve( makeRig( 4.0f, 1.0f ), farHand, heavyPelvis, heavyHand ) );
    SW_EXPECT_TRUE( lightPelvis._x > 0.4f );                   // 손 쪽으로 기운다
    SW_EXPECT_TRUE( heavyPelvis._x < lightPelvis._x - 0.05f ); // 발이 무거우면 덜 기운다
    SW_EXPECT_TRUE( ( farHand - lightHand ).getLength() < 0.25f );
    SW_EXPECT_TRUE( ( farHand - lightHand ).getLength() < ( farHand - heavyHand ).getLength() );

    // 상한 — 골반은 애니메이션 자리에서 0.2 m 넘게 가지 않는다.
    float3 clampedPelvis{};
    float3 clampedHand{};
    SW_ASSERT_TRUE( solve( makeRig( 1.0f, 0.2f ), farHand, clampedPelvis, clampedHand ) );
    SW_EXPECT_TRUE( ( clampedPelvis - float3{ 0.0f, 1.0f, 0.0f } ).getLength() <= 0.2f + 1e-4f );

    // 닿는 목표 — 골반은 그대로, 손은 목표에 붙는다.
    const float3 nearHand{ 0.6f, 1.5f, 0.0f };
    float3       stillPelvis{};
    float3       reachedHand{};
    SW_ASSERT_TRUE( solve( makeRig( 1.0f, 1.0f ), nearHand, stillPelvis, reachedHand ) );
    SW_EXPECT_TRUE( TestRigAssetInternal::isNear( float3{ 0.0f, 1.0f, 0.0f }, stillPelvis, 1e-4f ) );
    SW_EXPECT_TRUE( TestRigAssetInternal::isNear( nearHand, reachedHand, 2e-3f ) );

    // 골반 아래에서 시작하지 않는 사슬은 묶이지 않는다.
    test::ScopedDefensiveTestLog expected( "a FullBodyIK chain above the pelvis is rejected" );
    shared_ptr<const RigAsset>   above = TestRigAssetInternal::parseRig( R"(<RigAsset><targets><item name="Hand" object="Hand" /></targets><nodes>
        <item type="FullBodyIK" name="Body" pelvis="hips" max_pelvis_offset="1"><chains><item target="Hand"><bones><item>root</item><item>hips</item><item>shoulder</item></bones></item></chains></item>
        </nodes></RigAsset>)" );
    SW_ASSERT_NOT_NULL( above );
    RigInstance failing;
    SW_EXPECT_FALSE( failing.initialize( above, skeleton, nullptr, "test" ) );
    SW_EXPECT_TRUE( TestRigAssetInternal::parseRig( R"(<RigAsset><nodes><item type="FullBodyIK" name="Body" pelvis="hips" max_pelvis_offset="1"><chains /></item></nodes></RigAsset>)" ) ==
                    nullptr );
}

/**
 * @brief [RigAssetTest] 노드 순서가 결과를 바꾼다 — "위치 복사 → 거리 제한" 이면 본이 B 에서 0.5 m 안, "거리 제한 → 위치 복사" 면 A 에 선다
 */
SW_TEST_CASE( RigAssetTest, NodeOrderChangesResult )
{
    const Skeleton    skeleton      = test::makeChainSkeleton( 3 );
    const string_view copyThenLimit = R"(<RigAsset>
	<targets>
		<item name="A" object="A" />
		<item name="B" object="B" />
	</targets>
	<nodes>
		<item type="Position" name="Copy" bone="bone2" target="A" />
		<item type="Distance" name="Limit" bone="bone2" target="B" max="0.5" />
	</nodes>
</RigAsset>)";
    const string_view limitThenCopy = R"(<RigAsset>
	<targets>
		<item name="A" object="A" />
		<item name="B" object="B" />
	</targets>
	<nodes>
		<item type="Distance" name="Limit" bone="bone2" target="B" max="0.5" />
		<item type="Position" name="Copy" bone="bone2" target="A" />
	</nodes>
</RigAsset>)";
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
 * @brief [RigAssetTest] 노드 가중치 — 기본값 × 클립 커브 × 시퀀서 칸. 커브 0.5 면 반만, 칸 0 이면 꺼지고, 칸을 놓으면 커브로 돌아오며, 커브가 없으면 0 이다.
 *        칸 값은 읽을 수 있다(정하지 않은 · 놓은 칸은 1)
 */
SW_TEST_CASE( RigAssetTest, WeightFollowsCurveAndSequencerSlot )
{
    const Skeleton    skeleton = test::makeChainSkeleton( 3 );
    const string_view json     = R"(<RigAsset>
	<targets>
		<item name="A" object="A" />
	</targets>
	<nodes>
		<item type="Position" name="Copy" bone="bone2" target="A" weight_curve="IK" weight_slot="Shot" />
	</nodes>
</RigAsset>)";
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

    SW_EXPECT_NEAR_EQUAL( 1.0f, instance.getSlotWeight( "Shot" ), 1e-6f ); // 정하지 않은 칸은 1
    instance.setSlotWeight( "Shot", 0.0f );
    SW_EXPECT_NEAR_EQUAL( 0.0f, instance.getSlotWeight( "Shot" ), 1e-6f );
    TestRigAssetInternal::evaluateReference( instance, skeleton, pose, listModel );
    SW_EXPECT_TRUE( TestRigAssetInternal::isNear( rest, listModel[2].getTranslation(), 1e-4f ) );
    instance.setSlotWeight( "Shot", 1.0f );
    curve._value = 1.0f;
    TestRigAssetInternal::evaluateReference( instance, skeleton, pose, listModel );
    SW_EXPECT_TRUE( TestRigAssetInternal::isNear( float3{ 2.0f, 2.0f, 0.0f }, listModel[2].getTranslation(), 1e-4f ) );

    instance.clearSlotWeight( "Shot" );
    SW_EXPECT_NEAR_EQUAL( 1.0f, instance.getSlotWeight( "Shot" ), 1e-6f ); // 놓으면 다시 1
    instance.setCurveSource( nullptr );                                    // 커브 없음 = 0
    TestRigAssetInternal::evaluateReference( instance, skeleton, pose, listModel );
    SW_EXPECT_TRUE( TestRigAssetInternal::isNear( rest, listModel[2].getTranslation(), 1e-4f ) );
}

/**
 * @brief [RigAssetTest] 부모 바꾸기 — 바꾸는 프레임에 본이 튀지 않고(지난 출력 자리 그대로), 정착 시간이 지나면 새 부모 자리에 앉는다
 */
SW_TEST_CASE( RigAssetTest, ParentSwitchKeepsOffsetThenSettles )
{
    const Skeleton    skeleton = test::makeChainSkeleton( 3 );
    const string_view json     = R"(<RigAsset>
	<targets>
		<item name="Hand" object="Hand" />
		<item name="Back" object="Back" />
	</targets>
	<nodes>
		<item type="ParentSwitch" name="Weapon" bone="bone2" settle_seconds="0.5">
			<parents>
				<item>Hand</item>
				<item>Back</item>
			</parents>
		</item>
	</nodes>
</RigAsset>)";
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
    const string_view json     = R"(<RigAsset>
	<nodes>
		<item type="TwistDistribution" name="Forearm" source="hand" axis="0,1,0">
			<bones>
				<item bone="wrist" weight="0.5" />
			</bones>
		</item>
	</nodes>
</RigAsset>)";
    RigInstance       instance;
    SW_ASSERT_TRUE( instance.initialize( TestRigAssetInternal::parseRig( json ), skeleton, nullptr, "test" ) );
    Pose pose;
    pose.setToReference( skeleton );
    BoneTransform hand = pose.getBoneTransform( 2 );
    hand._rotation     = quaternion::makeFromAxisAngle( float3::UnitY, MathUtil::kHalfPi ) * quaternion::makeFromAxisAngle( float3::UnitX, 0.3f );
    pose.setBoneTransform( 2, hand );
    vector<float4x4> listBefore;
    pose.computeModelSpace( skeleton.getParentIndices(), listBefore );

    instance.evaluate( pose, skeleton.getParentIndices(), float4x4::Identity );
    vector<float4x4> listAfter;
    pose.computeModelSpace( skeleton.getParentIndices(), listAfter );
    quaternion swing{};
    quaternion twist{};
    RigIKSolver::decomposeSwingTwist( pose.getBoneTransform( 1 )._rotation, float3::UnitY, swing, twist );
    SW_EXPECT_NEAR_EQUAL( MathUtil::kHalfPi * 0.5f, RigIKSolver::computeTwistAngle( twist, float3::UnitY ), 1e-3f );
    SW_EXPECT_NEAR_EQUAL( 1.0f, MathUtil::abs( listBefore[2].getRotation().dot( listAfter[2].getRotation() ) ), 1e-4f );
}

/**
 * @brief [RigAssetTest] 포즈 구동(RBF) — 관절이 굽힘 포즈에 있으면 보정 모프 1 · 보정 본이 그 포즈 값, 쉬는 포즈면 0, 그 사이면 사이 값이다
 */
SW_TEST_CASE( RigAssetTest, PoseDriverInterpolatesCorrectives )
{
    const Skeleton    skeleton = test::makeChainSkeleton( 3 );
    const string_view json     = R"(<RigAsset>
	<nodes>
		<item type="PoseDriver" name="Elbow" driver="bone1" radius_degrees="60">
			<poses>
				<item name="Rest" rotation="0,0,0" />
				<item name="Bent" rotation="90,0,0">
					<morphs>
						<item morph="ElbowBend" weight="1" />
					</morphs>
					<bones>
						<item bone="bone2" translation="0,0,0.1" />
					</bones>
				</item>
			</poses>
		</item>
	</nodes>
</RigAsset>)";
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
        elbow._rotation     = quaternion::makeFromYawPitchRoll( 0.0f, arrDegree[index] * MathUtil::kDegreeToRadian, 0.0f );
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
    const string_view json     = R"(<RigAsset>
	<nodes>
		<item type="FootPlacement" name="Feet" pelvis="hips" interp_speed="0" max_align_degrees="45">
			<feet>
				<item root="upperleg.l" mid="lowerleg.l" end="foot.l" />
				<item root="upperleg.r" mid="lowerleg.r" end="foot.r" />
			</feet>
		</item>
	</nodes>
</RigAsset>)";
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
 * @brief [RigAssetTest] 보폭 워핑 — 발이 골반에서 이동 방향으로 떨어진 거리만 배율만큼 늘고, 닿지 않는 다리는 골반을 내려 닿게 한다
 * @details 허벅지를 앞뒤로 0.3 라디안 벌린 자세. 배율 1.3(+Z 방향)이면 발의 z 거리가 1.3 배, x 와 높이는 그대로이고 골반은 내려간다.
 *          배율은 `[min_scale, max_scale]` 로 잘리고, 1 이면 포즈를 바꾸지 않는다. 남은 몫은 `LocomotionSpeedUtil::computeStrideScale` 이 낸다.
 */
SW_TEST_CASE( RigAssetTest, StrideWarpingStretchesStrideAlongMoveDirection )
{
    SW_EXPECT_NEAR_EQUAL( 1.5f, LocomotionSpeedUtil::computeStrideScale( 3.0f, 1.0f, 2.0f ), 1e-6f );
    SW_EXPECT_NEAR_EQUAL( 1.0f, LocomotionSpeedUtil::computeStrideScale( 3.0f, 0.0f, 2.0f ), 1e-6f );

    const Skeleton    skeleton = TestRigAssetInternal::makeLegs();
    const string_view json     = R"(<RigAsset>
	<nodes>
		<item type="StrideWarping" name="Stride" pelvis="hips" min_scale="0.5" max_scale="1.5" max_pelvis_drop="0.3">
			<feet>
				<item root="upperleg.l" mid="lowerleg.l" end="foot.l" />
				<item root="upperleg.r" mid="lowerleg.r" end="foot.r" />
			</feet>
		</item>
	</nodes>
</RigAsset>)";
    RigInstance       instance;
    SW_ASSERT_TRUE( instance.initialize( TestRigAssetInternal::parseRig( json ), skeleton, nullptr, "test" ) );
    SW_EXPECT_FALSE( instance.setNodeControl( hashed_string( "Stride" ), hashed_string( "unknown" ), 1.0f ) );

    // 벌린 자세(왼다리 앞, 오른다리 뒤)의 모델 공간 — 리그 없이.
    Pose stance;
    stance.setToReference( skeleton );
    BoneTransform leftThigh  = stance.getBoneTransform( 2 );
    BoneTransform rightThigh = stance.getBoneTransform( 5 );
    leftThigh._rotation      = quaternion::makeFromAxisAngle( float3{ 1.0f, 0.0f, 0.0f }, 0.3f );
    rightThigh._rotation     = quaternion::makeFromAxisAngle( float3{ 1.0f, 0.0f, 0.0f }, -0.3f );
    stance.setBoneTransform( 2, leftThigh );
    stance.setBoneTransform( 5, rightThigh );
    vector<float4x4> listStance;
    stance.computeModelSpace( skeleton.getParentIndices(), listStance );
    const float3 pelvis = listStance[1].getTranslation();

    RigPrepareContext prepare{};
    prepare._worldFromModel = float4x4::Identity;
    prepare._deltaSeconds   = 1.0f / 60.0f;
    vector<float4x4> listModel;
    const float32    arrControl[3]  = { 1.3f, 5.0f, 1.0f }; // 늘림, 상한으로 잘림, 그대로
    const float32    arrExpected[3] = { 1.3f, 1.5f, 1.0f };
    for ( uint32 caseIndex = 0; caseIndex < 3; ++caseIndex )
    {
        SW_ASSERT_TRUE( instance.setNodeControl( hashed_string( "Stride" ), hashed_string( "stride_scale" ), arrControl[caseIndex] ) );
        SW_ASSERT_TRUE( instance.setNodeControl( hashed_string( "Stride" ), hashed_string( "direction_x" ), 0.0f ) );
        SW_ASSERT_TRUE( instance.setNodeControl( hashed_string( "Stride" ), hashed_string( "direction_z" ), 1.0f ) );
        Pose pose = stance;
        instance.prepare( prepare );
        instance.evaluate( pose, skeleton.getParentIndices(), float4x4::Identity );
        pose.computeModelSpace( skeleton.getParentIndices(), listModel );
        for ( const uint32 footBone : { 4u, 7u } )
        {
            const float3 before = listStance[footBone].getTranslation();
            const float3 after  = listModel[footBone].getTranslation();
            SW_EXPECT_NEAR_EQUAL( pelvis._z + ( before._z - pelvis._z ) * arrExpected[caseIndex], after._z, 2e-3f );
            SW_EXPECT_NEAR_EQUAL( before._x, after._x, 2e-3f );
            SW_EXPECT_NEAR_EQUAL( before._y, after._y, 2e-3f );
        }
        if ( caseIndex == 2 )
            SW_EXPECT_NEAR_EQUAL( pelvis._y, listModel[1].getTranslation()._y, 1e-5f ); // 배율 1 이면 그대로
        else
            SW_EXPECT_TRUE( listModel[1].getTranslation()._y < pelvis._y - 1e-3f ); // 뻗은 다리가 닿게 골반이 내려갔다
    }
}

/**
 * @brief [RigAssetTest] 평면 리그(`"planar": true`) — 2D 스켈레톤의 CCD 사슬 · 스프링이 평면 밖 목표 · 중력에도 XY 평면에 남는다
 */
SW_TEST_CASE( RigAssetTest, PlanarRigStaysInPlane )
{
    const Skeleton    skeleton = test::makeChainSkeleton( 4 );
    const string_view json     = R"(<RigAsset planar="true">
	<targets>
		<item name="Goal" object="Goal" />
	</targets>
	<nodes>
		<item type="CcdChain" name="Arm" target="Goal" iterations="32">
			<bones>
				<item>bone0</item>
				<item>bone1</item>
				<item>bone2</item>
			</bones>
		</item>
		<item type="SpringChain" name="Tail" gravity="0,-9.8,4" stiffness="0">
			<bones>
				<item>bone2</item>
				<item>bone3</item>
			</bones>
		</item>
	</nodes>
</RigAsset>)";
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
 * @brief [RigAssetTest] 공통 키 `min_screen_size` — 화면 크기가 그보다 작으면 노드를 끄고(포즈는 애니메이션 그대로), 다시 크면 켠다. 음수는 로드 오류다
 */
SW_TEST_CASE( RigAssetTest, MinScreenSizeTurnsNodeOffWhenSmallOnScreen )
{
    const Skeleton    skeleton = test::makeChainSkeleton( 3 );
    const string_view json     = R"(<RigAsset>
	<nodes>
		<item type="SpringChain" name="Tail" gravity="4,0,0" stiffness="0" min_screen_size="0.2">
			<bones>
				<item>bone1</item>
				<item>bone2</item>
			</bones>
		</item>
	</nodes>
</RigAsset>)";
    RigInstance       instance;
    SW_ASSERT_TRUE( instance.initialize( TestRigAssetInternal::parseRig( json ), skeleton, nullptr, "test" ) );

    Pose reference;
    reference.setToReference( skeleton );
    vector<float4x4> listReference;
    reference.computeModelSpace( skeleton.getParentIndices(), listReference );

    RigPrepareContext prepare{};
    prepare._deltaSeconds = 1.0f / 30.0f;
    prepare._screenSize   = 0.1f;
    Pose             pose;
    vector<float4x4> listModel;
    for ( uint32 frame = 0; frame < 10; ++frame )
    {
        instance.prepare( prepare );
        TestRigAssetInternal::evaluateReference( instance, skeleton, pose, listModel );
    }
    SW_EXPECT_TRUE( instance.isNodeLODOff( 0 ) );
    SW_EXPECT_TRUE( TestRigAssetInternal::isNear( listReference[2].getTranslation(), listModel[2].getTranslation(), 1e-5f ) );

    prepare._screenSize = 0.5f;
    for ( uint32 frame = 0; frame < 10; ++frame )
    {
        instance.prepare( prepare );
        TestRigAssetInternal::evaluateReference( instance, skeleton, pose, listModel );
    }
    SW_EXPECT_FALSE( instance.isNodeLODOff( 0 ) );
    SW_EXPECT_TRUE( listModel[2].getTranslation()._x > listReference[2].getTranslation()._x + 1e-3f ); // 중력 쪽으로 처졌다

    test::ScopedDefensiveTestLog expected( "negative min_screen_size is rejected" );
    RigAsset                     negative;
    SW_EXPECT_FALSE( negative.parseXML( R"(<RigAsset><nodes><item type="SpringChain" name="Tail" min_screen_size="-1"><bones><item>bone1</item><item>bone2</item></bones></item></nodes></RigAsset>)", "test" ) );
}

/**
 * @brief [RigAssetTest] `ScaleBone` — 스케일 0 이면 자손이 그 본 원점으로 모이고(1인칭 머리 숨기기), 시퀀서 슬롯 0 이면 그대로다. 음수 스케일은 로드 오류다
 */
SW_TEST_CASE( RigAssetTest, ScaleBoneCollapsesDescendantsBySlot )
{
    const Skeleton    skeleton = test::makeChainSkeleton( 3 );
    const string_view json     = R"(<RigAsset><nodes><item type="ScaleBone" name="HideHead" bone="bone1" weight_slot="FirstPerson" /></nodes></RigAsset>)";
    RigInstance       instance;
    SW_ASSERT_TRUE( instance.initialize( TestRigAssetInternal::parseRig( json ), skeleton, nullptr, "test" ) );

    Pose             pose;
    vector<float4x4> listModel;
    instance.setSlotWeight( "FirstPerson", 0.0f );
    instance.prepare( RigPrepareContext{} );
    TestRigAssetInternal::evaluateReference( instance, skeleton, pose, listModel );
    SW_EXPECT_TRUE( TestRigAssetInternal::isNear( float3{ 0.0f, 2.0f, 0.0f }, listModel[2].getTranslation(), 1e-5f ) );

    instance.setSlotWeight( "FirstPerson", 1.0f );
    instance.prepare( RigPrepareContext{} );
    TestRigAssetInternal::evaluateReference( instance, skeleton, pose, listModel );
    SW_EXPECT_TRUE( TestRigAssetInternal::isNear( float3{ 0.0f, 1.0f, 0.0f }, listModel[2].getTranslation(), 1e-5f ) );

    test::ScopedDefensiveTestLog expected( "negative bone scale is rejected" );
    RigAsset                     negative;
    SW_EXPECT_FALSE( negative.parseXML( R"(<RigAsset><nodes><item type="ScaleBone" name="A" bone="bone1" scale="1,-1,1" /></nodes></RigAsset>)", "test" ) );
}

/**
 * @brief [RigAssetTest] 데모 리그 비용 — 기사(`knight.rig.xml`, 41 본: 발 디딤 · 시선 · 왼손 IK · 팔뚝 비틀림)와 망토(스프링 사슬)를 한 번 평가하는 시간을 측정한다
 * @details 숫자를 남기는 테스트다(Release 로 돌려 읽는다). 단언은 넉넉한 상한 하나 — 회귀가 몇 배로 커졌을 때만 걸린다.
 */
SW_TEST_CASE( RigAssetTest, KnightRigEvaluationCost )
{
    SW_TEST_REQUIRES_FEATURE( SourceResources );
    SW_ASSERT_TRUE( ResourcePaths::initialize() );
    Skeleton knight;
    SW_ASSERT_TRUE( knight.loadFromResource( "game/shooter3d/models/kaykit/knight/knight.skeleton.xml" ) );
    shared_ptr<RigAsset> asset = make_shared<RigAsset>();
    SW_ASSERT_TRUE( asset->loadFromResource( "game/shooter3d/rigs/knight.rig.xml" ) );
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

    // 망토 리그(루트 고정 + 스프링 사슬 4 본 · 캡슐 하나) — 프레임 시간 1/60 이라 스텝 하나씩.
    Skeleton cape;
    SW_ASSERT_TRUE( cape.loadFromResource( "game/shooter3d/rigs/knight_cape.skeleton.xml" ) );
    shared_ptr<RigAsset> capeAsset = make_shared<RigAsset>();
    SW_ASSERT_TRUE( capeAsset->loadFromResource( "game/shooter3d/rigs/knight_cape.rig.xml" ) );
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

/**
 * @brief [RigAssetTest] 노드 설정 되쓰기 — 이름의 노드 `weight` · `enabled` 만 바뀌고(다른 키 · 노드는 그대로), 다시 읽으면 그 값이며, 끈 노드만 `enabled` 를 쓴다.
 *        없는 노드 이름 · 범위 밖 가중치는 쓰지 않는다
 */
SW_TEST_CASE( RigAssetTest, RewriteNodeSettingsKeepsOtherKeys )
{
    const string_view source = R"(<RigAsset>
	<targets>
		<item name="Goal" object="Ball" />
	</targets>
	<nodes>
		<item type="Aim" name="Look" bone="bone2" target="Goal" max_degrees="45" />
		<item type="Aim" name="Head" bone="bone1" target="Goal" weight="0.5" />
	</nodes>
</RigAsset>)";
    string            rewritten;
    RigSaveParams     nodeSave;
    nodeSave._listNode.push_back( RigNodeSaveParams{ hashed_string( "Look" ), 0.25f, false } );
    nodeSave._listNode.push_back( RigNodeSaveParams{ hashed_string( "Head" ), 0.5f, true } );
    SW_ASSERT_TRUE( RigAsset::rewriteSettings( source, "test.rig.xml", nodeSave, rewritten ) );
    shared_ptr<const RigAsset> asset = TestRigAssetInternal::parseRig( rewritten );
    SW_ASSERT_NOT_NULL( asset );
    SW_ASSERT_EQUAL( 2u, static_cast<uint32>( asset->getNodes().size() ) );
    SW_EXPECT_NEAR_EQUAL( 0.25f, asset->getNodes()[0]->getWeight(), 1e-6f );
    SW_EXPECT_FALSE( asset->getNodes()[0]->isEnabled() );
    SW_EXPECT_TRUE( asset->getNodes()[1]->isEnabled() );
    SW_EXPECT_TRUE( rewritten.find( "max_degrees" ) != string::npos );
    SW_EXPECT_TRUE( rewritten.find( " enabled=\"" ) == rewritten.rfind( " enabled=\"" ) ); // 끈 노드 하나에만

    test::ScopedDefensiveTestLog expected( "rewriting a rig refuses an unknown node and an out-of-range weight" );
    string                       unchanged = "keep";
    RigSaveParams                unknownNode;
    unknownNode._listNode.push_back( RigNodeSaveParams{ hashed_string( "Nobody" ), 1.0f, true } );
    SW_EXPECT_FALSE( RigAsset::rewriteSettings( source, "test.rig.xml", unknownNode, unchanged ) );
    RigSaveParams outOfRange;
    outOfRange._listNode.push_back( RigNodeSaveParams{ hashed_string( "Look" ), 2.0f, true } );
    SW_EXPECT_FALSE( RigAsset::rewriteSettings( source, "test.rig.xml", outOfRange, unchanged ) );
    SW_EXPECT_TRUE( unchanged == "keep" );
}

/**
 * @brief [RigAssetTest] 대상 오프셋 되쓰기 — 이름의 대상 `translation` · `rotation`(도, 읽기와 같은 배치)만 바뀌고, 다시 읽으면 같은 오프셋이다. 없는 대상은 쓰지 않는다
 * @details 리그 디버그 창의 대상 기즈모가 끌기를 놓을 때 이 길로 저장한다(RigEditing R2).
 */
SW_TEST_CASE( RigAssetTest, RewriteTargetOffsetRoundTrips )
{
    const string_view source = R"(<RigAsset>
	<targets>
		<item name="Goal" bone="bone1" />
		<item name="Other" object="Ball" translation="1,2,3" />
	</targets>
	<nodes>
		<item type="Aim" name="Look" bone="bone2" target="Goal" />
	</nodes>
</RigAsset>)";
    RigSaveParams     save;
    const quaternion  rotation = quaternion::makeFromYawPitchRoll( float3{ 0.1f, 0.4f, -0.2f } );
    save._listTarget.push_back( RigTargetSaveParams{
        hashed_string( "Goal" ), float3{ 0.0f, 0.25f, -0.5f },
        rotation
    } );
    string rewritten;
    SW_ASSERT_TRUE( RigAsset::rewriteSettings( source, "test.rig.xml", save, rewritten ) );
    shared_ptr<const RigAsset> asset = TestRigAssetInternal::parseRig( rewritten );
    SW_ASSERT_NOT_NULL( asset );
    const int32 goal = asset->findTargetIndex( hashed_string( "Goal" ) );
    SW_ASSERT_TRUE( goal >= 0 );
    const RigTargetDef& def = asset->getTargets()[static_cast<size_t>( goal )];
    SW_EXPECT_NEAR_EQUAL( 0.25f, def._offset._translation._y, 1e-5f );
    SW_EXPECT_NEAR_EQUAL( -0.5f, def._offset._translation._z, 1e-5f );
    SW_EXPECT_NEAR_EQUAL( 1.0f, MathUtil::abs( def._offset._rotation.dot( rotation ) ), 1e-4f );
    // 다른 대상의 키는 그대로다.
    const int32 other = asset->findTargetIndex( hashed_string( "Other" ) );
    SW_ASSERT_TRUE( other >= 0 );
    SW_EXPECT_NEAR_EQUAL( 3.0f, asset->getTargets()[static_cast<size_t>( other )]._offset._translation._z, 1e-5f );

    test::ScopedDefensiveTestLog expected( "rewriting a rig refuses an unknown target" );
    string                       unchanged = "keep";
    RigSaveParams                unknownTarget;
    unknownTarget._listTarget.push_back( RigTargetSaveParams{ hashed_string( "Nobody" ), float3{}, quaternion{} } );
    SW_EXPECT_FALSE( RigAsset::rewriteSettings( source, "test.rig.xml", unknownTarget, unchanged ) );
    SW_EXPECT_TRUE( unchanged == "keep" );
}
