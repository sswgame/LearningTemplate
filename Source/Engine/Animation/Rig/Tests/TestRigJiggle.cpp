#include "pch.h"

#include "Core/Math/MathUtil.h"
#include "Core/Testing/TestFramework.h"

#include "Engine/Animation/Rig/RigBodyInput.h"
#include "Engine/Animation/Rig/RigIKSolver.h"
#include "Engine/Animation/Rig/RigJiggleNode.h"
#include "Engine/Animation/Rig/RigPoseBuffer.h"
#include "Engine/Animation/Rig/RigSpringChain.h"
#include "Engine/Animation/Skeletal/Pose.h"

// RigJiggleTest — 지글: 동작 배율이 느슨함에 곱해진다, 충격이 끝 입자를 밀었다 돌아온다, 최대 벌어짐 각이 흔들림을 자른다, 충격은 준비 단계에서 프레임 값이 된다(nogpu).

namespace
{
    /** @brief 원점에서 앞(+z)으로 0.1 m 뻗은 두 본(가슴 뿌리 → 끝)의 포즈입니다. */
    void makeBustPose( sw::RigPoseBuffer& outBuffer )
    {
        sw::Pose pose;
        pose.resize( 2 );
        sw::BoneTransform tip{};
        tip._translation = sw::float3{ 0.0f, 0.0f, 0.1f };
        pose.setBoneTransform( 0, sw::BoneTransform{} );
        pose.setBoneTransform( 1, tip );
        outBuffer.initialize( pose, { -1, 0 } );
    }

    /** @brief 끝 입자가 애니메이션 방향(+z)에서 벌어진 각(도)입니다. */
    float32 measureSwingDegrees( const sw::RigSpringChain& chain )
    {
        const sw::float3 direction = ( chain.getParticles()[1] - chain.getParticles()[0] ).normalize();
        return sw::MathUtil::acos( sw::MathUtil::clamp( direction._z, -1.0f, 1.0f ) ) * sw::MathUtil::kRadianToDegree;
    }
} // namespace

/**
 * @brief [RigJiggleTest] 배율 0 은 강성 1(애니메이션 그대로), 1 은 프리셋 그대로, 2 는 느슨함이 두 배다
 */
SW_TEST_CASE( RigJiggleTest, MotionScaleMultipliesLooseness )
{
    sw::RigJiggleGroupParams group;
    group._stiffness = 0.6f;
    SW_EXPECT_NEAR( 1.0f, sw::RigJiggleNode::makeSettings( group, 0.0f )._stiffness, 1e-6f );
    SW_EXPECT_NEAR( 0.6f, sw::RigJiggleNode::makeSettings( group, 1.0f )._stiffness, 1e-6f );
    SW_EXPECT_NEAR( 0.2f, sw::RigJiggleNode::makeSettings( group, 2.0f )._stiffness, 1e-6f );
    SW_EXPECT_NEAR( 0.0f, sw::RigJiggleNode::makeSettings( group, 0.0f )._gravityScale, 1e-6f );
}

/**
 * @brief [RigJiggleTest] 위로 친 충격이 끝을 들어 올리고, 감쇠 · 강성으로 다시 애니메이션 자세 근처로 돌아온다
 */
SW_TEST_CASE( RigJiggleTest, ImpulseDisplacesThenSettles )
{
    sw::RigPoseBuffer buffer;
    makeBustPose( buffer );
    sw::RigJiggleGroupParams group;
    group._gravityScale                   = 0.0f;
    group._maxSwingDegrees                = 0.0f;
    const sw::RigSpringSettings settings  = sw::RigJiggleNode::makeSettings( group, 1.0f );
    const uint32                arrBone[] = { 0, 1 };
    sw::RigSpringChain          chain;
    sw::RigSolveSpace           space;
    const float32               delta = 1.0f / 60.0f;
    chain.simulate( buffer, arrBone, settings, {}, sw::float4x4::Identity, sw::float3{}, delta, space );
    chain.addVelocity( sw::float3{ 0.0f, 3.0f, 0.0f }, settings._fixedStep );
    float32 peak = 0.0f;
    // 리그 평가는 프레임마다 애니메이션 자세에서 다시 시작한다 — 사슬이 돌린 본을 다음 프레임의 목표로 쓰지 않게 자세를 다시 세운다.
    for ( int32 frame = 0; frame < 10; ++frame )
    {
        makeBustPose( buffer );
        chain.simulate( buffer, arrBone, settings, {}, sw::float4x4::Identity, sw::float3{}, delta, space );
        peak = sw::MathUtil::max( peak, measureSwingDegrees( chain ) );
    }
    SW_EXPECT_TRUE( peak > 5.0f );
    for ( int32 frame = 0; frame < 240; ++frame )
    {
        makeBustPose( buffer );
        chain.simulate( buffer, arrBone, settings, {}, sw::float4x4::Identity, sw::float3{}, delta, space );
    }
    SW_EXPECT_TRUE( measureSwingDegrees( chain ) < 1.0f );
}

/**
 * @brief [RigJiggleTest] 최대 벌어짐 각이 10 도면 강한 충격에도 10 도를 넘지 않는다
 */
SW_TEST_CASE( RigJiggleTest, MaxSwingClampsDisplacement )
{
    sw::RigPoseBuffer buffer;
    makeBustPose( buffer );
    sw::RigJiggleGroupParams group;
    group._gravityScale                   = 0.0f;
    group._maxSwingDegrees                = 10.0f;
    const sw::RigSpringSettings settings  = sw::RigJiggleNode::makeSettings( group, 2.0f );
    const uint32                arrBone[] = { 0, 1 };
    sw::RigSpringChain          chain;
    sw::RigSolveSpace           space;
    chain.simulate( buffer, arrBone, settings, {}, sw::float4x4::Identity, sw::float3{}, 1.0f / 60.0f, space );
    chain.addVelocity( sw::float3{ 0.0f, 50.0f, 0.0f }, settings._fixedStep );
    for ( int32 frame = 0; frame < 30; ++frame )
    {
        makeBustPose( buffer ); // 목표는 프레임마다 애니메이션 자세다
        chain.simulate( buffer, arrBone, settings, {}, sw::float4x4::Identity, sw::float3{}, 1.0f / 60.0f, space );
        SW_EXPECT_TRUE( measureSwingDegrees( chain ) <= 10.0f + 0.05f );
    }
}

/**
 * @brief [RigJiggleTest] 쌓은 충격은 준비 단계(`beginFrame`)에서 그 프레임 값이 되고 다음 준비에서 비워진다
 */
SW_TEST_CASE( RigJiggleTest, ImpulseLatchesOncePerFrame )
{
    sw::RigBodyInput input;
    input.addImpulse( sw::float3{ 1.0f, 0.0f, 0.0f } );
    input.addImpulse( sw::float3{ 0.0f, 2.0f, 0.0f } );
    SW_EXPECT_NEAR( 0.0f, input.getFrameImpulse()._x, 1e-6f );
    input.beginFrame();
    SW_EXPECT_NEAR( 1.0f, input.getFrameImpulse()._x, 1e-6f );
    SW_EXPECT_NEAR( 2.0f, input.getFrameImpulse()._y, 1e-6f );
    input.beginFrame();
    SW_EXPECT_NEAR( 0.0f, input.getFrameImpulse()._y, 1e-6f );
    input.setMotionScale( 5.0f );
    SW_EXPECT_NEAR( sw::RigBodyInput::kMaxMotionScale, input.getMotionScale(), 1e-6f );
}
