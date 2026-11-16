// 리그 포즈 구동(RBF) — weight_threshold 아래 가중치는 0 이고 그 포즈의 보정(모프 · 본)을 내지 않는다, 가까운 포즈는 그대로 낸다(nogpu).
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
    /** @brief 시험 몸의 본 번호입니다. 팔꿈치가 구동 관절이고 근육이 보정 본입니다. */
    enum RigPoseDriverTestBone : uint32
    {
        kDriverRoot = 0,
        kDriverElbow,
        kDriverMuscle,
    };

    /**
     * @brief 쉼(출력 없음) · 굽힘(모프 Bulge 1, 근육 +0.1 m) 두 포즈의 포즈 구동 하나를 묶어 돌립니다. 가우스 폭은 45 도입니다.
     * @details 굽힘은 첫 축 90 도다. 팔꿈치를 둘째 축으로 60 도 돌리면 굽힘과 약 104 도라 RBF 가중치가 약 0.0015 로 작지만 0 이 아니다(쉼은 약 0.17).
     */
    struct RigPoseDriverTest
    {
        Skeleton    _skeleton;
        RigInstance _instance;
        Pose        _pose;

        RigPoseDriverTest()
            : _skeleton{}
            , _instance{}
            , _pose{}
        {
            (void)_skeleton.addBone( hashed_string( "root" ), -1, BoneTransform{} );
            (void)_skeleton.addBone( hashed_string( "elbow" ), kDriverRoot, BoneTransform{} );
            (void)_skeleton.addBone( hashed_string( "muscle" ), kDriverElbow, BoneTransform{} );
            _pose.setToReference( _skeleton );
        }

        /** @brief @p pThreshold 를 "weight_threshold" 로 적은 포즈 구동을 읽어 묶습니다. */
        [[nodiscard]] bool load( const utf8* pThreshold )
        {
            const string xml = string( R"(<RigAsset><nodes><item type="PoseDriver" name="Elbow" driver="elbow" radius_degrees="45" weight_threshold=")" ) + pThreshold +
                               R"("><poses><item name="Rest" rotation="0,0,0" />)"
                               R"(<item name="Bent" rotation="90,0,0"><morphs><item morph="Bulge" weight="1" /></morphs><bones><item bone="muscle" translation="0,0.1,0" /></bones></item>)"
                               R"(</poses></item></nodes></RigAsset>)";
            shared_ptr<RigAsset> asset = make_shared<RigAsset>();
            if ( asset->loadFromXMLText( xml, "test.rig.xml" ) == false )
                return false;
            return _instance.initialize( asset, _skeleton, nullptr, "test" );
        }

        /** @brief 팔꿈치를 (도 단위) @p degrees 로 돌리고 근육을 레퍼런스로 되돌린 뒤 준비 · 평가를 한 번 돌립니다. */
        void run( const float3& degrees )
        {
            _pose.setToReference( _skeleton );
            BoneTransform elbow = _pose.getBoneTransform( kDriverElbow );
            elbow._rotation     = quaternion::makeFromYawPitchRoll( degrees * MathUtil::kDegreeToRadian );
            _pose.setBoneTransform( kDriverElbow, elbow );
            RigPrepareContext prepare{};
            prepare._worldFromModel = float4x4::Identity;
            prepare._deltaSeconds   = 1.0f / 60.0f;
            _instance.prepare( prepare );
            _instance.evaluate( _pose, _skeleton.getParentIndices(), float4x4::Identity );
        }

        [[nodiscard]] float32 findMorph( const utf8* pName ) const
        {
            const hashed_string name( pName );
            float32             weight = 0.0f;
            for ( const RigMorphWeight& morph : _instance.getMorphWeights() )
            {
                if ( morph._name == name )
                    weight += morph._weight;
            }
            return weight;
        }

        [[nodiscard]] float32 getMuscleOffset() const { return _pose.getBoneTransform( kDriverMuscle )._translation._y; }
    };
} // namespace

/** @brief [RigPoseDriverTest] 문턱이 없으면 먼 포즈도 작게 보정을 낸다 — 아래 문턱 시험의 대조군이다 */
SW_TEST_CASE_F( RigPoseDriverTest, FarPoseLeaksWithoutThreshold )
{
    SW_REQUIRE_TRUE( load( "0" ) );
    run( float3{ 0.0f, 60.0f, 0.0f } );
    SW_EXPECT_TRUE( findMorph( "Bulge" ) > 0.0f );
    SW_EXPECT_TRUE( getMuscleOffset() > 0.0f );
}

/** @brief [RigPoseDriverTest] 문턱 아래 가중치는 0 이라 먼 포즈의 모프 · 보정 본을 내지 않는다 */
SW_TEST_CASE_F( RigPoseDriverTest, FarPoseBelowThresholdIsSkipped )
{
    SW_REQUIRE_TRUE( load( "0.01" ) );
    run( float3{ 0.0f, 60.0f, 0.0f } );
    SW_EXPECT_NEAR( 0.0f, findMorph( "Bulge" ), 0.0f );
    SW_EXPECT_NEAR( 0.0f, getMuscleOffset(), 0.0f );
}

/** @brief [RigPoseDriverTest] 포즈 위에서는 그 포즈 가중치가 1 이라 문턱과 무관하게 보정을 다 낸다 */
SW_TEST_CASE_F( RigPoseDriverTest, NearPoseKeepsFullCorrection )
{
    SW_REQUIRE_TRUE( load( "0.01" ) );
    run( float3{ 90.0f, 0.0f, 0.0f } );
    SW_EXPECT_NEAR( 1.0f, findMorph( "Bulge" ), 1.0e-3f );
    SW_EXPECT_NEAR( 0.1f, getMuscleOffset(), 1.0e-4f );
}
