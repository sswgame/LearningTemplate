// PoseRetargeterTest — 본 수가 다른 사슬의 짝짓기: 대상 본은 사슬을 따라 잰 길이 비율이 가장 가까운 원본 본을 받는다(본 순번 비율이 아니다).
#include "pch.h"

#include "Core/Math/MathUtil.h"
#include "Core/Testing/TestFramework.h"

#include "Engine/Animation/Retarget/PoseRetargeter.h"
#include "Engine/Animation/Retarget/RetargetProfile.h"
#include "Engine/Animation/Skeletal/Pose.h"
#include "Engine/Animation/Skeletal/Skeleton.h"
#include "Engine/Animation/Tests/AnimationTestUtil.h"

using namespace sw;

namespace
{
    struct PoseRetargeterTestUtil
    {
        /** @brief 루트 · 골반 아래 +X 로 뻗은 팔입니다. @p listLength 는 팔 본마다 부모에서의 거리(첫 본은 골반 자리)입니다. */
        static Skeleton makeArm( const vector<const utf8*>& listName, const vector<float32>& listLength )
        {
            Skeleton skeleton;
            (void)skeleton.addBone( hashed_string( "root" ), -1, test::makeBoneTransform( float3{ 0.0f, 0.0f, 0.0f } ) );
            (void)skeleton.addBone( hashed_string( "pelvis" ), 0, test::makeBoneTransform( float3{ 0.0f, 1.0f, 0.0f } ) );
            for ( size_t index = 0; index < listName.size(); ++index )
            {
                (void)skeleton.addBone( hashed_string( listName[index] ), static_cast<int32>( index + 1 ), test::makeBoneTransform( float3{ listLength[index], 0.0f, 0.0f } ) );
            }
            return skeleton;
        }
    };
} // namespace

/**
 * @brief [PoseRetargeterTest] 위팔 · 아래팔 · 손목 · 손(4) → 위팔 · 아래팔 · 손(3): 대상 아래팔은 원본 아래팔을, 손은 손을 받는다 — 손목만 꺾어도 대상 아래팔은 돌지 않는다
 * @details 순번 비율이면 대상 아래팔(1/2)이 원본 2 번(손목)을 받아 손목 꺾임이 팔뚝으로 갔다(KayKit 팔 → UBC 팔이 이 모양이다).
 */
SW_TEST_CASE( PoseRetargeterTest, ChainOfOtherBoneCountPairsByChainLength )
{
    using Util             = PoseRetargeterTestUtil;
    const Skeleton  source = Util::makeArm( { "upper", "lower", "wrist", "hand" }, { 0.0f, 0.24f, 0.26f, 0.08f } ); // 길이 비율 0 · 0.41 · 0.86 · 1
    const Skeleton  target = Util::makeArm( { "t_upper", "t_lower", "t_hand" }, { 0.0f, 0.28f, 0.27f } );           // 길이 비율 0 · 0.51 · 1
    RetargetProfile profile;
    SW_REQUIRE_TRUE( profile.loadFromXMLText( R"(<RetargetProfile translation="None">
        <root source="root" target="root" />
        <pelvis source="pelvis" target="pelvis" />
        <chains>
            <item name="Arm">
                <source><item>upper</item><item>lower</item><item>wrist</item><item>hand</item></source>
                <target><item>t_upper</item><item>t_lower</item><item>t_hand</item></target>
            </item>
        </chains>
    </RetargetProfile>)",
                                              "arm.retarget.xml" ) );
    PoseRetargeter retargeter;
    SW_REQUIRE_TRUE( retargeter.initialize( profile, source, target, nullptr ) );

    // 손목만 Y 로 60 도 꺾는다(아래팔 · 손은 레퍼런스 그대로).
    Pose sourcePose;
    sourcePose.setToReference( source );
    const uint32  wrist = *source.findBoneIndex( hashed_string( "wrist" ) );
    BoneTransform bent  = sourcePose.getBoneTransform( wrist );
    bent._rotation      = quaternion::makeFromYawPitchRoll( 60.0f * MathUtil::kDegreeToRadian, 0.0f, 0.0f );
    sourcePose.setBoneTransform( wrist, bent );

    Pose targetPose;
    retargeter.retarget( sourcePose, targetPose );
    const uint32 lower = *target.findBoneIndex( hashed_string( "t_lower" ) );
    const uint32 hand  = *target.findBoneIndex( hashed_string( "t_hand" ) );
    SW_EXPECT_NEAR( 0.0f, quaternion::getAngleBetween( quaternion::Identity, targetPose.getBoneTransform( lower )._rotation ), 1.0e-3f );                             // 아래팔은 돌지 않는다
    SW_EXPECT_NEAR( 60.0f * MathUtil::kDegreeToRadian, quaternion::getAngleBetween( quaternion::Identity, targetPose.getBoneTransform( hand )._rotation ), 1.0e-3f ); // 꺾임은 손이 받는다
}
