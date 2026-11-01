#include "pch.h"

#include "Core/Testing/TestFramework.h"

#include "Engine/Animation/Rig/RigIKSolver.h"
#include "Engine/Animation/Rig/RigPoseBuffer.h"
#include "Engine/Animation/Rig/RigSpringChain.h"
#include "Engine/Animation/Skeletal/Pose.h"

// RigSpringColliderTest — 스프링 사슬의 충돌체: 모델 공간 충돌체(리그 목표 · 다른 유닛 본에 붙은 것)도 본 충돌체처럼 입자를 밀어낸다(nogpu).

namespace
{
    /** @brief 원점에서 아래로 매달린 본 셋(0.5 m 간격)의 사슬을 @p collider 와 함께 2 초 돌리고 끝 입자의 모델 위치를 돌려줍니다. */
    sw::float3 simulateHangingChain( const sw::vector<sw::RigSpringCollider>& listCollider )
    {
        sw::Pose pose;
        pose.resize( 3 );
        for ( uint32 boneIndex = 0; boneIndex < 3; ++boneIndex )
        {
            sw::BoneTransform transform{};
            transform._translation = boneIndex == 0 ? sw::float3{ 0.0f, 0.0f, 0.0f } : sw::float3{ 0.0f, -0.5f, 0.0f };
            pose.setBoneTransform( boneIndex, transform );
        }
        sw::RigPoseBuffer buffer;
        buffer.initialize( pose, { -1, 0, 1 } );
        sw::RigSpringSettings settings;
        settings._stiffness          = 0.0f; // 중력만 — 애니메이션 자세로 당기지 않는다
        const uint32       arrBone[] = { 0, 1, 2 };
        sw::RigSpringChain chain;
        sw::RigSolveSpace  space;
        for ( int32 frame = 0; frame < 120; ++frame )
            chain.simulate( buffer, arrBone, settings, listCollider, sw::float4x4::Identity, sw::float3{ 0.0f, -9.8f, 0.0f }, 1.0f / 60.0f, space );
        return buffer.getModelPosition( 2 );
    }
} // namespace

/**
 * @brief [RigSpringColliderTest] 사슬이 매달린 길 위에 둔 모델 공간 구(`kModelSpaceBone`)가 끝 입자를 반지름 밖으로 밀어낸다 — 망토가 몸 유닛의 본에 붙은 충돌체를 뚫지 않는 바탕
 */
SW_TEST_CASE( RigSpringColliderTest, ModelSpaceColliderPushesParticlesOut )
{
    const sw::float3 free = simulateHangingChain( {} );
    SW_EXPECT_NEAR( -1.0f, free._y, 0.05f );

    sw::RigSpringCollider sphere{};
    sphere._bone             = sw::RigSpringCollider::kModelSpaceBone;
    sphere._shape            = sw::RigSpringColliderShape::Sphere;
    sphere._pointA           = sw::float3{ 0.08f, -1.0f, 0.0f }; // 매달린 끝에서 조금 비켜 둔다(정확히 겹치면 밀 방향이 없다)
    sphere._radius           = 0.2f;
    const sw::float3 blocked = simulateHangingChain( { sphere } );
    // 밀어낸 뒤 길이를 다시 맞추므로 반지름에 조금 못 미칠 수 있다 — 뚫고 들어가 있던 자리(중심에서 0.08)보다 확실히 밖이면 된다.
    SW_EXPECT_TRUE( sw::float3::getDistance( blocked, sphere._pointA ) >= sphere._radius * 0.9f );
    SW_EXPECT_TRUE( blocked._x < -0.05f );
}
