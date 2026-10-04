#include "pch.h"

#include "Core/Math/MathUtil.h"

#include "Engine/Animation/Pose.h"
#include "Engine/Animation/Rig/RigIkSolver.h"
#include "Engine/Animation/Rig/RigPoseBuffer.h"
#include "Engine/Animation/Rig/RigSpringChain.h"
#include "Engine/Animation/Skeleton.h"

#include "EngineTest/AnimationTestUtil.h"

#include "TestFramework/TestFramework.h"

using namespace sw;

// RigSolverTest — 후처리 리그의 작업 포즈 · IK 풀이(2 본 · FABRIK · CCD · 조준 · 관절 제한) · 스프링 사슬, 그리고 같은 풀이의 평면(2D) 경우.

namespace
{
    struct TestRigSolverInternal
    {
        static bool isNear( const float3& expected, const float3& actual, float32 tolerance ) { return ( expected - actual ).getLength() <= tolerance; }

        /** @brief 사슬 스켈레톤의 레퍼런스 포즈로 작업 포즈를 엽니다. */
        static void openReference( const Skeleton& skeleton, RigPoseBuffer& outBuffer )
        {
            Pose pose;
            pose.setToReference( skeleton );
            outBuffer.initialize( pose, skeleton.getParentIndices() );
        }

        /** @brief 수평(+X) 사슬입니다 — 본마다 X 로 @p segment 미터. */
        static Skeleton makeHorizontalChain( uint32 boneCount, float32 segment )
        {
            Skeleton skeleton;
            for ( uint32 boneIndex = 0; boneIndex < boneCount; ++boneIndex )
            {
                const string name = string( "bone" ) + to_string( boneIndex ).c_str();
                const float3 offset{ boneIndex == 0 ? 0.0f : segment, 0.0f, 0.0f };
                (void)skeleton.addBone( hashed_string( name ), static_cast<int32>( boneIndex ) - 1, test::makeBoneTransform( offset ), float4x4::Identity );
            }
            skeleton.computeInverseBindFromReference();
            return skeleton;
        }

        /** @brief 사슬의 본 번호 목록(0..n-1)입니다. */
        static vector<uint32> makeChainIndices( uint32 boneCount )
        {
            vector<uint32> listBone;
            for ( uint32 boneIndex = 0; boneIndex < boneCount; ++boneIndex )
                listBone.push_back( boneIndex );
            return listBone;
        }

        /** @brief 스프링 사슬을 @p seconds 동안 @p frameSeconds 프레임으로 돌리고 끝 입자의 월드 위치를 냅니다. */
        static float3 simulateHanging( float32 seconds, float32 frameSeconds, const RigSpringSettings& settings, span<const RigSpringCollider> listCollider )
        {
            const Skeleton       skeleton = makeHorizontalChain( 4, 0.5f );
            const vector<uint32> listBone = makeChainIndices( 4 );
            RigSpringChain       chain;
            const RigSolveSpace  space{};
            const uint32         frameCount = static_cast<uint32>( MathUtil::round( seconds / frameSeconds ) );
            for ( uint32 frame = 0; frame < frameCount; ++frame )
            {
                RigPoseBuffer buffer;
                openReference( skeleton, buffer ); // 애니메이션은 늘 수평 레퍼런스 — 스프링이 매 프레임 그 위에 얹는다
                chain.simulate( buffer, listBone, settings, listCollider, float4x4::Identity, frameSeconds, space );
            }
            return chain.getParticles().back();
        }
    };
} // namespace

/**
 * @brief [RigSolverTest] 작업 포즈의 모델 공간은 `Pose::computeModelSpace` 와 같고, 모델 공간에 쓰면 로컬이 바뀌고 자손이 따라온다
 */
SW_TEST_CASE( RigSolverTest, PoseBufferMatchesModelSpaceAndWritesBack )
{
    const Skeleton skeleton = test::makeChainSkeleton( 4 );
    Pose           pose;
    pose.setToReference( skeleton );
    for ( uint32 boneIndex = 0; boneIndex < 4; ++boneIndex )
    {
        BoneTransform local = pose.getBoneTransform( boneIndex );
        local._rotation     = quaternion::createFromYawPitchRoll( 0.3f * static_cast<float32>( boneIndex ), 0.2f, -0.1f * static_cast<float32>( boneIndex ) );
        local._scale        = float3{ 1.25f, 1.25f, 1.25f };
        pose.setBoneTransform( boneIndex, local );
    }
    vector<float4x4> listModel;
    pose.computeModelSpace( skeleton.getParentIndices(), listModel );

    RigPoseBuffer buffer;
    buffer.initialize( pose, skeleton.getParentIndices() );
    for ( uint32 boneIndex = 0; boneIndex < 4; ++boneIndex )
    {
        const float4x4 model = buffer.getModelMatrix( boneIndex );
        SW_EXPECT_TRUE( TestRigSolverInternal::isNear( listModel[boneIndex].getTranslation(), model.getTranslation(), 1e-4f ) );
        const float3 axis = float3::transformVector( float3::UnitX, listModel[boneIndex] ).normalize();
        SW_EXPECT_TRUE( TestRigSolverInternal::isNear( axis, float3::transformVector( float3::UnitX, model ).normalize(), 1e-4f ) );
    }

    // 모델 공간 회전 · 위치를 쓰면 그대로 읽히고, 자식의 모델 위치는 새 부모를 따른다.
    const quaternion wanted = quaternion::createFromAxisAngle( float3::UnitZ, 1.0f );
    buffer.setModelRotation( 1, wanted );
    SW_EXPECT_NEAR_EQUAL( 1.0f, MathUtil::abs( wanted.dot( buffer.getModelRotation( 1 ) ) ), 1e-5f );
    buffer.setModelPosition( 2, float3{ 3.0f, 1.0f, -2.0f } );
    SW_EXPECT_TRUE( TestRigSolverInternal::isNear( float3{ 3.0f, 1.0f, -2.0f }, buffer.getModelPosition( 2 ), 1e-4f ) );
    Pose written = pose;
    buffer.writeTo( written );
    written.computeModelSpace( skeleton.getParentIndices(), listModel );
    SW_EXPECT_TRUE( TestRigSolverInternal::isNear( float3{ 3.0f, 1.0f, -2.0f }, listModel[2].getTranslation(), 1e-4f ) );
    SW_EXPECT_TRUE( TestRigSolverInternal::isNear( buffer.getModelPosition( 3 ), listModel[3].getTranslation(), 1e-4f ) );
}

/**
 * @brief [RigSolverTest] 2 본 IK — 닿는 목표에 끝이 닿고 극점 쪽으로 굽으며 길이를 지킨다. 닿지 않는 목표는 그쪽으로 곧게 편다
 */
SW_TEST_CASE( RigSolverTest, TwoBoneReachesTargetAndBendsTowardPole )
{
    const Skeleton skeleton = test::makeChainSkeleton( 3 ); // 0 → 1 (y 1) → 2 (y 2)
    RigPoseBuffer  buffer;
    TestRigSolverInternal::openReference( skeleton, buffer );
    const RigSolveSpace space{};
    const float3        target{ 1.0f, 1.0f, 0.0f };
    const float3        pole{ 0.0f, 0.5f, 1.0f };
    SW_EXPECT_TRUE( RigIkSolver::solveTwoBone( buffer, 0, 1, 2, target, &pole, space ) );
    SW_EXPECT_TRUE( TestRigSolverInternal::isNear( target, buffer.getModelPosition( 2 ), 1e-3f ) );
    SW_EXPECT_TRUE( buffer.getModelPosition( 1 )._z > 0.1f ); // 극점(+Z) 쪽으로 굽었다
    SW_EXPECT_NEAR_EQUAL( 1.0f, ( buffer.getModelPosition( 1 ) - buffer.getModelPosition( 0 ) ).getLength(), 1e-4f );
    SW_EXPECT_NEAR_EQUAL( 1.0f, ( buffer.getModelPosition( 2 ) - buffer.getModelPosition( 1 ) ).getLength(), 1e-4f );

    // 극점을 반대로 두면 반대쪽으로 굽는다.
    const float3 poleBack{ 0.0f, 0.5f, -1.0f };
    (void)RigIkSolver::solveTwoBone( buffer, 0, 1, 2, target, &poleBack, space );
    SW_EXPECT_TRUE( buffer.getModelPosition( 1 )._z < -0.1f );

    // 닿지 않는다 — 목표 쪽으로 곧게, 끝은 뿌리에서 길이만큼.
    const float3 distant{ 5.0f, 0.0f, 0.0f };
    SW_EXPECT_FALSE( RigIkSolver::solveTwoBone( buffer, 0, 1, 2, distant, nullptr, space ) );
    SW_EXPECT_NEAR_EQUAL( 2.0f, buffer.getModelPosition( 2 )._x, 1e-2f );
    SW_EXPECT_NEAR_EQUAL( 0.0f, buffer.getModelPosition( 2 )._y, 1e-2f );
}

/**
 * @brief [RigSolverTest] FABRIK · CCD — 사슬 끝이 목표에 닿는다. 경첩 제한이 있으면 관절 각이 범위 밖으로 나가지 않는다
 */
SW_TEST_CASE( RigSolverTest, ChainSolversReachTargetAndRespectLimits )
{
    const Skeleton       skeleton = test::makeChainSkeleton( 5 );
    const vector<uint32> listBone = TestRigSolverInternal::makeChainIndices( 5 );
    const RigSolveSpace  space{};
    const float3         target{ 2.0f, 2.0f, 0.5f };
    RigChainSettings     settings{};
    settings._iterationCount = 32;

    RigPoseBuffer fabrik;
    TestRigSolverInternal::openReference( skeleton, fabrik );
    SW_EXPECT_TRUE( RigIkSolver::solveFabrik( fabrik, listBone, target, {}, settings, space ) );
    SW_EXPECT_TRUE( TestRigSolverInternal::isNear( target, fabrik.getModelPosition( 4 ), 2e-3f ) );
    for ( uint32 boneIndex = 1; boneIndex < 5; ++boneIndex )
        SW_EXPECT_NEAR_EQUAL( 1.0f, ( fabrik.getModelPosition( boneIndex ) - fabrik.getModelPosition( boneIndex - 1 ) ).getLength(), 1e-3f );

    // CCD 는 곧게 편 사슬에서 선형으로 수렴한다 — 반복을 더 주고 허용 오차를 2 mm 로 둔다.
    RigChainSettings ccdSettings = settings;
    ccdSettings._iterationCount  = 64;
    ccdSettings._tolerance       = 2e-3f;
    RigPoseBuffer ccd;
    TestRigSolverInternal::openReference( skeleton, ccd );
    SW_EXPECT_TRUE( RigIkSolver::solveCcd( ccd, listBone, target, {}, ccdSettings, space ) );
    SW_EXPECT_TRUE( TestRigSolverInternal::isNear( target, ccd.getModelPosition( 4 ), 2e-3f ) );

    // 경첩: Z 축 둘레 [-30°, 0°] 만 — 관절마다 그 범위 안에 있어야 한다.
    vector<RigJointLimit> listLimit( 5 );
    for ( RigJointLimit& limit : listLimit )
    {
        limit._type      = RigJointLimitType::Hinge;
        limit._hingeAxis = float3::UnitZ;
        limit._minAngle  = -30.0f * MathUtil::DegreeToRadian;
        limit._maxAngle  = 0.0f;
    }
    for ( uint32 solver = 0; solver < 2; ++solver )
    {
        RigPoseBuffer limited;
        TestRigSolverInternal::openReference( skeleton, limited );
        const float3 sideTarget{ 2.5f, 1.5f, 0.0f }; // +X 쪽 — Z 축 음의 회전으로 닿는다
        if ( solver == 0 )
            (void)RigIkSolver::solveFabrik( limited, listBone, sideTarget, listLimit, settings, space );
        else
            (void)RigIkSolver::solveCcd( limited, listBone, sideTarget, listLimit, settings, space );
        for ( uint32 boneIndex = 0; boneIndex < 5; ++boneIndex )
        {
            quaternion swing{};
            quaternion twist{};
            RigIkSolver::decomposeSwingTwist( limited.getLocalRotation( boneIndex ), float3::UnitZ, swing, twist );
            const float32 angle = RigIkSolver::computeTwistAngle( twist, float3::UnitZ );
            SW_EXPECT_TRUE( -30.5f * MathUtil::DegreeToRadian <= angle && angle <= 0.5f * MathUtil::DegreeToRadian );
            SW_EXPECT_TRUE( 2.0f * MathUtil::acos( MathUtil::min( 1.0f, MathUtil::abs( swing._w ) ) ) < 1e-3f ); // 경첩 밖 흔들림 없음
        }
        // 제한 안에서 최대한 가까이 갔다 — 굽힘 쪽(+X)으로 기울었다.
        SW_EXPECT_TRUE( limited.getModelPosition( 4 )._x > 1.0f );
    }
}

/**
 * @brief [RigSolverTest] 조준 — 상한(30°)이 있으면 애니메이션 방향에서 그 각까지만 돌고, 상한이 넉넉하면 목표를 정확히 본다. 원뿔 제한은 흔들림 각을 자른다
 */
SW_TEST_CASE( RigSolverTest, AimClampsToMaxAngleAndConeLimitsSwing )
{
    const Skeleton skeleton = test::makeChainSkeleton( 2 );
    RigPoseBuffer  buffer;
    TestRigSolverInternal::openReference( skeleton, buffer );
    const RigSolveSpace space{};
    const float32       turned = RigIkSolver::aimBone( buffer, 0, float3::UnitZ, float3{ 5.0f, 0.0f, 0.0f }, 30.0f * MathUtil::DegreeToRadian, 1.0f, space );
    SW_EXPECT_NEAR_EQUAL( 30.0f * MathUtil::DegreeToRadian, turned, 1e-3f );
    const float3 facing = float3::transform( float3::UnitZ, buffer.getModelRotation( 0 ) );
    SW_EXPECT_NEAR_EQUAL( MathUtil::cos( 30.0f * MathUtil::DegreeToRadian ), facing._z, 1e-3f );

    TestRigSolverInternal::openReference( skeleton, buffer );
    (void)RigIkSolver::aimBone( buffer, 0, float3::UnitZ, float3{ 3.0f, 4.0f, 0.0f }, MathUtil::Pi, 1.0f, space );
    SW_EXPECT_TRUE( TestRigSolverInternal::isNear( float3{ 0.6f, 0.8f, 0.0f }, float3::transform( float3::UnitZ, buffer.getModelRotation( 0 ) ), 1e-3f ) );

    // 원뿔 20°: 본 축(+Y)을 X 축으로 60° 눕힌 로컬 회전은 20° 로 잘린다.
    RigJointLimit cone{};
    cone._type       = RigJointLimitType::Cone;
    cone._boneAxis   = float3::UnitY;
    cone._swingLimit = 20.0f * MathUtil::DegreeToRadian;
    cone._twistLimit = 5.0f * MathUtil::DegreeToRadian;
    TestRigSolverInternal::openReference( skeleton, buffer );
    buffer.setLocalRotation( 1, quaternion::createFromAxisAngle( float3::UnitX, 60.0f * MathUtil::DegreeToRadian ) * quaternion::createFromAxisAngle( float3::UnitY, 0.5f ) );
    RigIkSolver::applyJointLimit( buffer, 1, cone );
    const float3 boneAxis = float3::transform( float3::UnitY, buffer.getLocalRotation( 1 ) );
    SW_EXPECT_NEAR_EQUAL( MathUtil::cos( 20.0f * MathUtil::DegreeToRadian ), boneAxis._y, 1e-3f );
    quaternion swing{};
    quaternion twist{};
    RigIkSolver::decomposeSwingTwist( buffer.getLocalRotation( 1 ), float3::UnitY, swing, twist );
    SW_EXPECT_NEAR_EQUAL( 5.0f * MathUtil::DegreeToRadian, RigIkSolver::computeTwistAngle( twist, float3::UnitY ), 1e-3f );
}

/**
 * @brief [RigSolverTest] 평면(2D) — 평면 밖 목표를 줘도 2 본 · FABRIK · CCD · 스프링이 모든 관절을 XY 평면에 두고, 끝은 투영한 목표에 닿는다
 */
SW_TEST_CASE( RigSolverTest, PlanarSolversStayInPlane )
{
    const Skeleton       skeleton = test::makeChainSkeleton( 4 );
    const vector<uint32> listBone = TestRigSolverInternal::makeChainIndices( 4 );
    RigSolveSpace        planar{};
    planar._bPlanar = SW_TRUE;
    const float3     target{ 1.5f, 1.2f, 0.7f };
    const float3     projected{ 1.5f, 1.2f, 0.0f };
    RigChainSettings settings{};
    settings._iterationCount = 32;

    for ( uint32 solver = 0; solver < 3; ++solver )
    {
        RigPoseBuffer buffer;
        TestRigSolverInternal::openReference( skeleton, buffer );
        if ( solver == 0 )
            (void)RigIkSolver::solveTwoBone( buffer, 0, 1, 2, float3{ 1.0f, 1.0f, 0.7f }, nullptr, planar );
        else if ( solver == 1 )
            (void)RigIkSolver::solveFabrik( buffer, listBone, target, {}, settings, planar );
        else
            (void)RigIkSolver::solveCcd( buffer, listBone, target, {}, settings, planar );
        for ( uint32 boneIndex = 0; boneIndex < 4; ++boneIndex )
            SW_EXPECT_NEAR_EQUAL( 0.0f, buffer.getModelPosition( boneIndex )._z, 1e-4f );
        if ( solver == 0 )
            SW_EXPECT_TRUE( TestRigSolverInternal::isNear( float3{ 1.0f, 1.0f, 0.0f }, buffer.getModelPosition( 2 ), 1e-3f ) );
        else
            SW_EXPECT_TRUE( TestRigSolverInternal::isNear( projected, buffer.getModelPosition( 3 ), 3e-3f ) );
        // 2D 본은 Z 축으로만 돈다 — 로컬 회전의 X · Y 성분이 없다.
        for ( uint32 boneIndex = 0; boneIndex < 4; ++boneIndex )
        {
            SW_EXPECT_NEAR_EQUAL( 0.0f, buffer.getLocalRotation( boneIndex )._x, 1e-4f );
            SW_EXPECT_NEAR_EQUAL( 0.0f, buffer.getLocalRotation( boneIndex )._y, 1e-4f );
        }
    }

    // 스프링: 평면 밖 중력(+Z 성분)을 줘도 입자는 평면에 남는다.
    const Skeleton    horizontal = TestRigSolverInternal::makeHorizontalChain( 3, 0.5f );
    RigSpringSettings springSettings{};
    springSettings._gravity   = float3{ 0.0f, -9.8f, 5.0f };
    springSettings._stiffness = 0.0f;
    RigSpringChain chain;
    for ( uint32 frame = 0; frame < 60; ++frame )
    {
        RigPoseBuffer buffer;
        TestRigSolverInternal::openReference( horizontal, buffer );
        chain.simulate( buffer, TestRigSolverInternal::makeChainIndices( 3 ), springSettings, {}, float4x4::Identity, 1.0f / 60.0f, planar );
        if ( frame == 59 )
            SW_EXPECT_NEAR_EQUAL( 0.0f, buffer.getModelPosition( 2 )._z, 1e-4f );
    }
    for ( const float3& particle : chain.getParticles() )
        SW_EXPECT_NEAR_EQUAL( 0.0f, particle._z, 1e-4f );
}

/**
 * @brief [RigSolverTest] 스프링 사슬 — 중력에 늘어지고 길이를 지키며, 프레임 길이를 바꿔도(30 · 60 · 120 Hz) 고정 스텝이라 같은 자리에 온다. 구 충돌체를 뚫지 않는다
 */
SW_TEST_CASE( RigSolverTest, SpringChainHangsDeterministicallyAndCollides )
{
    RigSpringSettings settings{};
    settings._stiffness = 0.0f;
    settings._damping   = 0.05f;
    const float3 at30   = TestRigSolverInternal::simulateHanging( 2.0f, 1.0f / 30.0f, settings, {} );
    const float3 at60   = TestRigSolverInternal::simulateHanging( 2.0f, 1.0f / 60.0f, settings, {} );
    const float3 at120  = TestRigSolverInternal::simulateHanging( 2.0f, 1.0f / 120.0f, settings, {} );
    SW_EXPECT_TRUE( at60._y < -0.8f );                  // 수평 1.5 m 사슬이 아래로 늘어졌다
    SW_EXPECT_TRUE( at60.getLength() <= 1.5f + 1e-3f ); // 뿌리(원점)에서 사슬 길이를 넘지 않는다
    SW_EXPECT_TRUE( TestRigSolverInternal::isNear( at60, at30, 1e-4f ) );
    SW_EXPECT_TRUE( TestRigSolverInternal::isNear( at60, at120, 1e-4f ) );

    // 구 충돌체(원점 아래 0.6 m, 반지름 0.4)가 매달린 끝을 밀어낸다.
    RigSpringCollider sphere{};
    sphere._bone         = 0;
    sphere._pointA       = float3{ 0.3f, -0.9f, 0.0f };
    sphere._radius       = 0.4f;
    const float3 blocked = TestRigSolverInternal::simulateHanging( 2.0f, 1.0f / 60.0f, settings, span<const RigSpringCollider>( &sphere, 1 ) );
    SW_EXPECT_TRUE( ( blocked - sphere._pointA ).getLength() > sphere._radius + settings._particleRadius - 1e-3f );
    SW_EXPECT_FALSE( TestRigSolverInternal::isNear( blocked, at60, 0.05f ) );
}
