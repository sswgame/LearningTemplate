#include "pch.h"

#include "Core/TestFramework/TestFramework.h"

#include "Engine/Animation/Skeletal/LocomotionSpeedUtil.h"
#include "Engine/Animation/Skeletal/Pose.h"
#include "Engine/Animation/Skeletal/Skeleton.h"

using namespace sw;

// LocomotionSpeedUtilTest — 다리 길이 비: 클립 발 속도(레퍼런스 스켈레톤으로 잰 값)에 곱해 체형이 바꾼 보폭에 재생 속도를 맞춘다(nogpu).
namespace
{
    /** @brief 두 다리(엉덩 → 무릎 → 발, 마디 0.45 m)와 팔 하나를 가진 몸입니다. */
    struct LocomotionLegBody
    {
        enum Bone : uint32
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
            kHand,
        };

        Skeleton _skeleton;

        LocomotionLegBody()
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
            add( "knee.l", kHipL, float3{ 0.0f, -0.45f, 0.02f } );
            add( "foot.l", kKneeL, float3{ 0.0f, -0.45f, -0.02f } );
            add( "hip.r", kPelvis, float3{ 0.2f, 0.0f, 0.0f } );
            add( "knee.r", kHipR, float3{ 0.0f, -0.45f, 0.02f } );
            add( "foot.r", kKneeR, float3{ 0.0f, -0.45f, -0.02f } );
            add( "arm", kPelvis, float3{ 0.0f, 0.5f, 0.0f } );
            add( "hand", kArm, float3{ 0.0f, 0.3f, 0.0f } );
        }

        /** @brief 레퍼런스 포즈에서 @p listBone 의 로컬 이동만 @p scale 배 한 포즈의 모델 공간입니다(본 비율이 마디를 늘인 몸). */
        vector<float4x4> makeModel( const vector<uint32>& listBone, float32 scale ) const
        {
            Pose pose;
            pose.setToReference( _skeleton );
            for ( const uint32 bone : listBone )
            {
                BoneTransform transform = pose.getBoneTransform( bone );
                transform._translation  = transform._translation * scale;
                pose.setBoneTransform( bone, transform );
            }
            vector<float4x4> listModel;
            pose.computeModelSpace( _skeleton.getParentIndices(), listModel );
            return listModel;
        }

        float32 ratio( const vector<float4x4>& listModel ) const
        {
            return LocomotionSpeedUtil::computeLegLengthRatio( _skeleton, listModel, hashed_string( "foot.l" ), hashed_string( "foot.r" ) );
        }
    };
} // namespace

/** @brief [LocomotionSpeedUtilTest] 다리 마디를 1.2 배로 늘이면 다리 길이 비가 1.2 이고, 레퍼런스 발 속도 × 비 = 발 속도 1.2 배 */
SW_TEST_CASE( LocomotionSpeedUtilTest, LongerLegsScaleTheFootSpeed )
{
    const LocomotionLegBody body;
    const float32           referenceFootSpeed = 1.5f;
    const float32           legRatio           = body.ratio( body.makeModel( { LocomotionLegBody::kKneeL, LocomotionLegBody::kFootL, LocomotionLegBody::kKneeR, LocomotionLegBody::kFootR }, 1.2f ) );
    SW_EXPECT_NEAR( legRatio, 1.2f, 1.0e-4f );
    SW_EXPECT_NEAR( referenceFootSpeed * legRatio, 1.8f, 1.0e-4f );
    // 골반 자리를 옮겨도(마디 길이는 그대로) 비는 1.2 다 — 측정이 자리가 아니라 마디 길이다.
    SW_EXPECT_NEAR( body.ratio( body.makeModel( { LocomotionLegBody::kKneeL, LocomotionLegBody::kFootL, LocomotionLegBody::kKneeR, LocomotionLegBody::kFootR, LocomotionLegBody::kPelvis }, 1.2f ) ),
                    1.2f, 1.0e-4f );
}

/** @brief [LocomotionSpeedUtilTest] 음성 대조: 레퍼런스 포즈 · 팔만 늘인 몸은 비가 1 이고, 발 본이 없으면 1 이다 */
SW_TEST_CASE( LocomotionSpeedUtilTest, OtherBonesLeaveTheLegRatio )
{
    const LocomotionLegBody body;
    SW_EXPECT_NEAR( body.ratio( body.makeModel( {}, 1.0f ) ), 1.0f, 1.0e-5f );
    SW_EXPECT_NEAR( body.ratio( body.makeModel( { LocomotionLegBody::kArm, LocomotionLegBody::kHand }, 1.5f ) ), 1.0f, 1.0e-5f );
    SW_EXPECT_NEAR( LocomotionSpeedUtil::computeLegLengthRatio( body._skeleton, body.makeModel( {}, 1.0f ), hashed_string( "paw.l" ), hashed_string( "paw.r" ) ), 1.0f, 1.0e-6f );
}
