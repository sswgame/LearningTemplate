#include "pch.h"

#include "Core/Testing/TestFramework.h"

#include "Engine/Animation/Component/SkeletalMeshComponent.h"
#include "Engine/Animation/Scene/AnimationLOD.h"
#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManager.h"
#include "Engine/Physics/Cloth/ClothComponent.h"

using namespace sw;

// 천 LOD — 애니메이션 LOD 등급(그래프 · 리그 노드의 max_lod 와 같은 축)으로 반복 수를 낮추고 _maxLOD 를 넘으면 내린다. 내린 동안은 쉬는 격자다.

namespace
{
    struct ClothLODTestUtil
    {
        static constexpr float32 kTolerance = 1.0e-4f;

        static bool isNear( const float3& expected, const float3& actual ) { return float3::getDistance( expected, actual ) <= kTolerance; }
    };
} // namespace

/**
 * @brief [ClothLODTest] LOD 를 적지 않은 천은 모든 등급에서 돌고 반복 수는 Iteration Count 그대로다
 */
SW_TEST_CASE( ClothLODTest, NoLODSimulatesEveryGrade )
{
    ClothComponent cloth;
    for ( uint8 grade = 0; grade < 8; ++grade )
    {
        SW_EXPECT_TRUE( cloth.isSimulatedAtLOD( grade ) );
        SW_EXPECT_EQUAL( 5u, cloth.computeLODIterationCount( grade ) ); // 기본 Iteration Count
    }
}

/**
 * @brief [ClothLODTest] 등급 g 의 반복 수는 목록의 g - 1 번째(끝을 넘으면 마지막)이고, _maxLOD 를 넘는 등급에서는 내린다. 범위 밖 반복 수는 1..32 로 자른다
 */
SW_TEST_CASE( ClothLODTest, GradeLowersIterationsThenSuspends )
{
    ClothComponent cloth;
    cloth.setLOD( 2, { 3u, 1u } );
    SW_EXPECT_EQUAL( 5u, cloth.computeLODIterationCount( 0 ) );
    SW_EXPECT_EQUAL( 3u, cloth.computeLODIterationCount( 1 ) );
    SW_EXPECT_EQUAL( 1u, cloth.computeLODIterationCount( 2 ) );
    SW_EXPECT_EQUAL( 1u, cloth.computeLODIterationCount( 7 ) );
    SW_EXPECT_TRUE( cloth.isSimulatedAtLOD( 2 ) );
    SW_EXPECT_FALSE( cloth.isSimulatedAtLOD( 3 ) );

    cloth.setLOD( -1, { 0u, 99u } );
    SW_EXPECT_EQUAL( 1u, cloth.computeLODIterationCount( 1 ) );
    SW_EXPECT_EQUAL( 32u, cloth.computeLODIterationCount( 2 ) );
    SW_EXPECT_TRUE( cloth.isSimulatedAtLOD( 200 ) );
}

/**
 * @brief [ClothLODTest] 내린 동안의 쉬는 격자는 고정 자세에 건 격자다(윗변 가운데가 자세 자리, 아래로 −Y, 행 우선)
 */
SW_TEST_CASE( ClothLODTest, RestGridHangsFromThePinPose )
{
    using Util = ClothLODTestUtil;
    ClothComponent cloth; // 폭 1 · 높이 1 · 11 × 11
    vector<float3> listVertex;
    cloth.makeRestVertices( float3{ 1.0f, 2.0f, 3.0f }, quaternion{}, listVertex );
    SW_REQUIRE_EQUAL( size_t( 121 ), listVertex.size() );
    SW_EXPECT_TRUE( Util::isNear( float3{ 0.5f, 2.0f, 3.0f }, listVertex[0] ) );   // 윗줄 왼쪽
    SW_EXPECT_TRUE( Util::isNear( float3{ 1.5f, 2.0f, 3.0f }, listVertex[10] ) );  // 윗줄 오른쪽
    SW_EXPECT_TRUE( Util::isNear( float3{ 1.5f, 1.0f, 3.0f }, listVertex[120] ) ); // 아랫줄 오른쪽
}

/**
 * @brief [ClothLODTest] 등급은 이 오브젝트부터 조상으로 처음 만난 스켈레탈 메시의 LOD 판정이다. 스켈레탈 메시가 없으면 0 이다
 */
SW_TEST_CASE( ClothLODTest, GradeComesFromTheSkeletalMesh )
{
    GameObjectManager manager;
    GameObject*       pLoose     = manager.createGameObject( hashed_string( "Curtain" ) );
    ClothComponent*   pCurtain   = pLoose->addComponent<ClothComponent>();
    GameObject*       pCharacter = manager.createGameObject( hashed_string( "Knight" ) );
    auto*             pSkeletal  = pCharacter->addComponent<SkeletalMeshComponent>();
    ClothComponent*   pCape      = pCharacter->addComponent<ClothComponent>();
    SW_REQUIRE_NOT_NULL( pCurtain );
    SW_REQUIRE_NOT_NULL( pSkeletal );
    SW_REQUIRE_NOT_NULL( pCape );
    SW_EXPECT_EQUAL( uint8( 0 ), pCurtain->readLODGrade() );
    SW_EXPECT_EQUAL( uint8( 0 ), pCape->readLODGrade() );

    AnimationLODState state;
    state._lodGrade = 3;
    pSkeletal->applyAnimationLOD( state );
    pCape->setLOD( 2, {} );
    SW_EXPECT_EQUAL( uint8( 3 ), pCape->readLODGrade() );
    SW_EXPECT_FALSE( pCape->isSimulatedAtLOD( pCape->readLODGrade() ) );
    SW_EXPECT_EQUAL( uint8( 0 ), pCurtain->readLODGrade() ); // 다른 오브젝트의 등급은 읽지 않는다
}
