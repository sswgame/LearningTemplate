#include "pch.h"

#include "Core/Math/MathUtil.h"

#include "Engine/Common/EngineServices.h"
#include "Engine/Physics/IPhysicsScene.h"
#include "Engine/Physics/PhysicsAsset.h"
#include "Engine/Physics/PhysicsRagdoll.h"
#include "Engine/Physics/PhysicsSettings.h"
#include "Engine/Physics/PhysicsSystem.h"

#include "TestFramework/TestFramework.h"

// 물리 에셋 + 평범한 뼈 배열로 래그돌을 세우고 되읽는다 — 애니메이션 타입 없이.

namespace
{
    constexpr float32 kRagdollStep = 1.0f / 60.0f;

    /** @brief 세로 사슬 pelvis(y=2) → spine(2.5) → head(3.0) → hat(3.2, 바디 없음)의 이름 · 부모 · 모델 공간 행렬입니다. */
    struct ChainSkeleton
    {
        sw::vector<sw::hashed_string> _listName{ sw::hashed_string( "pelvis" ), sw::hashed_string( "spine" ), sw::hashed_string( "head" ), sw::hashed_string( "hat" ) };
        sw::vector<int32>             _listParent{ -1, 0, 1, 2 };
        sw::vector<sw::float4x4>      _listModel{ sw::float4x4::createTranslation( 0.0f, 2.0f, 0.0f ), sw::float4x4::createTranslation( 0.0f, 2.5f, 0.0f ),
                                             sw::float4x4::createTranslation( 0.0f, 3.0f, 0.0f ), sw::float4x4::createTranslation( 0.0f, 3.2f, 0.0f ) };

        sw::PhysicsSkeletonView makeView() const
        {
            sw::PhysicsSkeletonView view;
            view._listBoneName       = sw::span<const sw::hashed_string>{ _listName.data(), _listName.size() };
            view._listParentIndex    = sw::span<const int32>{ _listParent.data(), _listParent.size() };
            view._listModelSpaceBone = sw::span<const sw::float4x4>{ _listModel.data(), _listModel.size() };
            return view;
        }
    };

    sw::PhysicsAssetBodyDef makeBody( const utf8* pBone, const utf8* pZone, float32 damageMultiplier )
    {
        sw::PhysicsAssetBodyDef body;
        body._bone = sw::hashed_string( pBone );
        sw::PhysicsShapeDesc3D capsule;
        capsule._type          = sw::PhysicsShapeType3D::Capsule;
        capsule._radius        = 0.1f;
        capsule._halfHeight    = 0.12f;
        capsule._localPosition = sw::float3{ 0.0f, 0.25f, 0.0f };
        body._listShape.push_back( capsule );
        body._mass                      = 10.0f;
        body._joint._type               = sw::PhysicsJointType::Cone;
        body._joint._swingLimitNormal   = 0.4f;
        body._joint._swingLimitPlane    = 0.4f;
        body._hitZone._name             = sw::hashed_string( pZone );
        body._hitZone._damageMultiplier = damageMultiplier;
        return body;
    }

    sw::PhysicsAsset makeAsset()
    {
        sw::PhysicsAsset asset;
        asset._defaultLayer = sw::hashed_string( "Default" );
        asset._listBody.push_back( makeBody( "pelvis", "Torso", 1.0f ) );
        asset._listBody.push_back( makeBody( "spine", "Torso", 1.0f ) );
        asset._listBody.push_back( makeBody( "head", "Head", 2.5f ) );
        return asset;
    }

    sw::unique_ptr<sw::IPhysicsScene3D> makeSceneWithFloor()
    {
        // 엔진 설정 표(재질 Flesh 등)로 만든다 — 바디는 레이어 0(Default)에 선다.
        sw::unique_ptr<sw::IPhysicsScene3D> pScene = sw::engine::getPhysicsSystem().createScene3D();
        if ( pScene == nullptr )
            return pScene;
        sw::PhysicsBodyDesc3D  floor;
        sw::PhysicsShapeDesc3D box;
        box._halfExtents = sw::float3{ 20.0f, 0.5f, 20.0f };
        floor._listShape.push_back( box );
        floor._position = sw::float3{ 0.0f, -0.5f, 0.0f };
        floor._type     = sw::PhysicsBodyType::Static;
        (void)pScene->createBody( floor );
        return pScene;
    }

    bool isNear( const sw::float4x4& lhs, const sw::float4x4& rhs, float32 tolerance )
    {
        for ( uint32 index = 0; index < 16; ++index )
        {
            if ( sw::MathUtil::abs( lhs.data()[index] - rhs.data()[index] ) > tolerance )
                return false;
        }
        return true;
    }
} // namespace

/**
 * @brief [PhysicsRagdollTest] 3 뼈 사슬에 바디 셋 · 관절 둘(루트 없음)이 서고, 곧바로 되읽으면 입력 포즈 그대로다. 맞은 바디로 히트 존을 고른다
 */
SW_TEST_CASE( PhysicsRagdollTest, BuildsBodiesAndJointsFromChain )
{
    sw::unique_ptr<sw::IPhysicsScene3D> pScene = makeSceneWithFloor();
    SW_ASSERT_NOT_NULL( pScene.get() );
    const ChainSkeleton       skeleton;
    const sw::PhysicsAsset    asset = makeAsset();
    sw::PhysicsRagdollOptions options;
    options._userData = 42;
    sw::PhysicsRagdoll ragdoll;
    SW_ASSERT_TRUE( sw::PhysicsRagdollBuilder::create( *pScene, asset, skeleton.makeView(), sw::float4x4{}, options, ragdoll ) );
    SW_ASSERT_EQUAL( static_cast<size_t>( 3 ), ragdoll._listBody.size() );
    SW_EXPECT_FALSE( ragdoll._listJoint[0].isValid() );
    SW_EXPECT_TRUE( pScene->isJointValid( ragdoll._listJoint[1] ) );
    SW_EXPECT_TRUE( pScene->isJointValid( ragdoll._listJoint[2] ) );
    SW_EXPECT_EQUAL( 1, ragdoll._listParentBody[2] );
    SW_EXPECT_EQUAL( -1, ragdoll._listBodyOfBone[3] );
    SW_EXPECT_EQUAL( static_cast<uint64>( 42 ), pScene->getBodyUserData( ragdoll._listBody[2] ) );
    SW_EXPECT_EQUAL( 4u, pScene->getBodyCount() );

    sw::vector<sw::float4x4> listReadBack( skeleton._listModel.size() );
    sw::PhysicsRagdollBuilder::readBoneTransforms( *pScene, ragdoll, skeleton.makeView(), sw::float4x4{}, sw::span<sw::float4x4>{ listReadBack.data(), listReadBack.size() } );
    for ( size_t boneIndex = 0; boneIndex < listReadBack.size(); ++boneIndex )
        SW_EXPECT_TRUE( isNear( skeleton._listModel[boneIndex], listReadBack[boneIndex], 1e-4f ) );

    // 머리를 맞힌 레이 → 히트 존 Head, 배율 2.5.
    sw::PhysicsCastHit3D hit;
    SW_ASSERT_TRUE( pScene->raycast( sw::float3{ -5.0f, 3.25f, 0.0f }, sw::float3{ 1.0f, 0.0f, 0.0f }, 10.0f, sw::PhysicsQueryFilter{}, hit ) );
    const int32 bodyIndex = ragdoll.findBodyIndex( hit._body );
    SW_ASSERT_EQUAL( 2, bodyIndex );
    SW_EXPECT_TRUE( asset._listBody[static_cast<size_t>( bodyIndex )]._hitZone._name == sw::hashed_string( "Head" ) );
    SW_EXPECT_NEAR_EQUAL( 2.5f, asset._listBody[static_cast<size_t>( bodyIndex )]._hitZone._damageMultiplier, 1e-6f );

    sw::PhysicsRagdollBuilder::destroy( *pScene, ragdoll );
    SW_EXPECT_EQUAL( 1u, pScene->getBodyCount() );
    SW_EXPECT_TRUE( ragdoll.isEmpty() );
}

/**
 * @brief [PhysicsRagdollTest] 동적 래그돌은 쓰러지되 관절이 뼈 사이 거리를 지키고, 바디 없는 뼈(hat)는 부모를 따라간다
 */
SW_TEST_CASE( PhysicsRagdollTest, DynamicRagdollFallsAndKeepsBoneSpacing )
{
    sw::unique_ptr<sw::IPhysicsScene3D> pScene = makeSceneWithFloor();
    SW_ASSERT_NOT_NULL( pScene.get() );
    const ChainSkeleton    skeleton;
    const sw::PhysicsAsset asset = makeAsset();
    sw::PhysicsRagdoll     ragdoll;
    SW_ASSERT_TRUE( sw::PhysicsRagdollBuilder::create( *pScene, asset, skeleton.makeView(), sw::float4x4{}, sw::PhysicsRagdollOptions{}, ragdoll ) );
    // 옆으로 밀어 쓰러뜨린다.
    pScene->addImpulse( ragdoll._listBody[2], sw::float3{ 20.0f, 0.0f, 0.0f } );
    for ( uint32 stepIndex = 0; stepIndex < 180; ++stepIndex )
        pScene->step( kRagdollStep );

    sw::vector<sw::float4x4> listReadBack( skeleton._listModel.size() );
    sw::PhysicsRagdollBuilder::readBoneTransforms( *pScene, ragdoll, skeleton.makeView(), sw::float4x4{}, sw::span<sw::float4x4>{ listReadBack.data(), listReadBack.size() } );
    const sw::float3 head = listReadBack[2].getTranslation();
    SW_EXPECT_TRUE_MSG( head._y < 2.0f, "the chain fell over" );
    SW_EXPECT_NEAR_EQUAL( 0.5f, sw::float3::getDistance( listReadBack[0].getTranslation(), listReadBack[1].getTranslation() ), 0.03f );
    SW_EXPECT_NEAR_EQUAL( 0.5f, sw::float3::getDistance( listReadBack[1].getTranslation(), listReadBack[2].getTranslation() ), 0.03f );
    SW_EXPECT_NEAR_EQUAL( 0.2f, sw::float3::getDistance( listReadBack[2].getTranslation(), listReadBack[3].getTranslation() ), 1e-3f );
}

/**
 * @brief [PhysicsRagdollTest] 키네마틱 히트박스는 포즈를 따라가고(driveToPose), 동적으로 넘기면 떨어진다
 */
SW_TEST_CASE( PhysicsRagdollTest, KinematicHitboxesFollowPoseThenRagdoll )
{
    sw::unique_ptr<sw::IPhysicsScene3D> pScene = makeSceneWithFloor();
    SW_ASSERT_NOT_NULL( pScene.get() );
    ChainSkeleton             skeleton;
    const sw::PhysicsAsset    asset = makeAsset();
    sw::PhysicsRagdollOptions options;
    options._bodyType = sw::PhysicsBodyType::Kinematic;
    sw::PhysicsRagdoll ragdoll;
    SW_ASSERT_TRUE( sw::PhysicsRagdollBuilder::create( *pScene, asset, skeleton.makeView(), sw::float4x4{}, options, ragdoll ) );

    // 캐릭터가 x 로 1 m 움직였다 — 월드 행렬만 바꿔 같은 포즈를 보낸다.
    const sw::float4x4 moved = sw::float4x4::createTranslation( 1.0f, 0.0f, 0.0f );
    sw::PhysicsRagdollBuilder::driveToPose( *pScene, ragdoll, skeleton.makeView(), moved, kRagdollStep );
    pScene->step( kRagdollStep );
    sw::float3     position{};
    sw::quaternion rotation{};
    SW_ASSERT_TRUE( pScene->getBodyTransform( ragdoll._listBody[0], position, rotation ) );
    SW_EXPECT_NEAR_EQUAL( 1.0f, position._x, 1e-3f );
    SW_EXPECT_NEAR_EQUAL( 2.0f, position._y, 1e-3f );
    // 키네마틱은 중력을 받지 않는다.
    for ( uint32 stepIndex = 0; stepIndex < 30; ++stepIndex )
        pScene->step( kRagdollStep );
    SW_ASSERT_TRUE( pScene->getBodyTransform( ragdoll._listBody[0], position, rotation ) );
    SW_EXPECT_NEAR_EQUAL( 2.0f, position._y, 1e-3f );

    sw::PhysicsRagdollBuilder::setBodyType( *pScene, ragdoll, sw::PhysicsBodyType::Dynamic );
    for ( uint32 stepIndex = 0; stepIndex < 120; ++stepIndex )
        pScene->step( kRagdollStep );
    SW_ASSERT_TRUE( pScene->getBodyTransform( ragdoll._listBody[0], position, rotation ) );
    SW_EXPECT_TRUE_MSG( position._y < 1.0f, "switched to Dynamic - the pelvis drops to the floor" );
}

/**
 * @brief [PhysicsRagdollTest] 스켈레톤에 없는 뼈를 적은 에셋은 오류이고 바디를 하나도 남기지 않는다
 */
SW_TEST_CASE( PhysicsRagdollTest, UnknownBoneIsAnError )
{
    sw::unique_ptr<sw::IPhysicsScene3D> pScene = makeSceneWithFloor();
    SW_ASSERT_NOT_NULL( pScene.get() );
    const ChainSkeleton skeleton;
    sw::PhysicsAsset    asset = makeAsset();
    asset._listBody.push_back( makeBody( "tail", "Tail", 0.5f ) );
    sw::PhysicsRagdoll ragdoll;
    SW_TEST_DEFENSIVE_SCOPE( "an asset bone missing from the skeleton is rejected" );
    SW_EXPECT_FALSE( sw::PhysicsRagdollBuilder::create( *pScene, asset, skeleton.makeView(), sw::float4x4{}, sw::PhysicsRagdollOptions{}, ragdoll ) );
    SW_EXPECT_EQUAL( 1u, pScene->getBodyCount() );
    SW_EXPECT_TRUE( ragdoll.isEmpty() );
}

/**
 * @brief [PhysicsRagdollTest] 견본 에셋(`engine/physics/samples/chain.physics.xml`)이 읽히고 같은 사슬에 선다
 */
SW_TEST_CASE( PhysicsRagdollTest, SampleAssetLoadsAndBuilds )
{
    sw::PhysicsAsset asset;
    SW_ASSERT_TRUE( asset.loadFromResource( "engine/physics/samples/chain.physics.xml" ) );
    SW_ASSERT_EQUAL( static_cast<size_t>( 3 ), asset._listBody.size() );
    SW_EXPECT_TRUE( asset._listBody[2]._hitZone._name == sw::hashed_string( "Head" ) );
    SW_EXPECT_EQUAL( 0, asset.findBodyIndex( sw::hashed_string( "pelvis" ) ) );

    sw::unique_ptr<sw::IPhysicsScene3D> pScene = makeSceneWithFloor();
    SW_ASSERT_NOT_NULL( pScene.get() );
    const ChainSkeleton skeleton;
    sw::PhysicsRagdoll  ragdoll;
    SW_EXPECT_TRUE( sw::PhysicsRagdollBuilder::create( *pScene, asset, skeleton.makeView(), sw::float4x4{}, sw::PhysicsRagdollOptions{}, ragdoll ) );
    SW_EXPECT_EQUAL( static_cast<size_t>( 3 ), ragdoll._listBody.size() );
}
