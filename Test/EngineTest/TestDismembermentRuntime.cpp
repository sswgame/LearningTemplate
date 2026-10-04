#include "pch.h"

#include "Core/File/FileUtil.h"
#include "Core/Math/MathUtil.h"

#include "Engine/Animation/Skeleton.h"
#include "Engine/Character/CharacterHit.h"
#include "Engine/Character/DismembermentComponent.h"
#include "Engine/Character/RagdollComponent.h"
#include "Engine/Graphics/Mesh/Mesh.h"
#include "Engine/Object/Component/3D/MeshComponent.h"
#include "Engine/Object/Component/3D/SkeletalMeshComponent.h"
#include "Engine/Object/Component/Physics/RigidBodyComponent.h"
#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManager.h"
#include "Engine/Object/GameObject/ScenePhysics.h"
#include "Engine/Physics/IPhysicsScene.h"
#include "Engine/Physics/PhysicsAsset.h"
#include "Engine/Resource/ResourceUtil.h"

#include "EngineTest/AnimationTestUtil.h"

#include "TestFramework/TestFramework.h"

using namespace sw;

// DismembermentRuntimeTest — KayKit 스켈레톤의 치명적 맞음 → 영역 자르기: 몸 메시에서 삼각형이 빠지고 캡이 붙고, 조각이 강체 오브젝트로 떨어지고, 그 뼈의 래그돌 바디가
// 꺼지고, 영역에 피가 오른다.

namespace
{
    struct TestDismembermentRuntimeInternal
    {
        static constexpr float32     kFrame        = 1.0f / 60.0f;
        static constexpr const utf8* kMeshPath     = "game/shooter3d/models/kaykit/skeleton_warrior.mesh";
        static constexpr const utf8* kSkeletonPath = "game/shooter3d/models/kaykit/skeleton_warrior/skeleton_warrior.skeleton.json";
        static constexpr const utf8* kPhysicsPath  = "game/shooter3d/characters/skeleton_warrior/skeleton_warrior.physics.xml";
        static constexpr const utf8* kRegionPath   = "game/shooter3d/characters/skeleton_warrior/skeleton_warrior.fit.xml";

        static void tickFor( GameObjectManager& manager, uint32 frameCount )
        {
            for ( uint32 frameIndex = 0; frameIndex < frameCount; ++frameIndex )
                manager.tick( kFrame );
        }

        /** @brief 육각 관(인덱스 없는 삼각형 목록) — 고리 y = 0 · 1 은 뼈 0, y = 2 는 뼈 1 을 따른다. 두 띠가 고리 y = 1 을 나눠 이어져 있다. */
        static shared_ptr<Mesh> makeSkinnedTube()
        {
            constexpr uint32       kSideCount = 6;
            vector<RHIVertex>      listVertex;
            vector<MeshSkinVertex> listSkin;
            auto                   addCorner = [&listVertex, &listSkin]( uint32 side, uint32 ring )
            {
                const float32 angle = MathUtil::Pi * 2.0f * static_cast<float32>( side % kSideCount ) / static_cast<float32>( kSideCount );
                RHIVertex     vertex{};
                vertex._arrPosition[0] = 0.3f * MathUtil::cos( angle );
                vertex._arrPosition[1] = static_cast<float32>( ring );
                vertex._arrPosition[2] = 0.3f * MathUtil::sin( angle );
                vertex._arrNormal[0]   = MathUtil::cos( angle );
                vertex._arrNormal[2]   = MathUtil::sin( angle );
                vertex._arrColor[0]    = 1.0f;
                vertex._arrColor[1]    = 1.0f;
                vertex._arrColor[2]    = 1.0f;
                vertex._arrColor[3]    = 1.0f;
                listVertex.push_back( vertex );
                MeshSkinVertex skin;
                skin._arrJoint[0] = ring == 2 ? 1 : 0;
                listSkin.push_back( skin );
            };
            for ( uint32 ring = 0; ring < 2; ++ring )
            {
                for ( uint32 side = 0; side < kSideCount; ++side )
                {
                    addCorner( side, ring );
                    addCorner( side, ring + 1 );
                    addCorner( side + 1, ring + 1 );
                    addCorner( side, ring );
                    addCorner( side + 1, ring + 1 );
                    addCorner( side + 1, ring );
                }
            }
            shared_ptr<Mesh> mesh = Mesh::create();
            mesh->setVertices( std::move( listVertex ) );
            mesh->setSkin( std::move( listSkin ), 2 );
            return mesh;
        }
    };
} // namespace

/**
 * @brief [DismembermentRuntimeTest] 머리에 치명적 맞음 → 머리 삼각형이 몸 메시에서 빠지고(캡이 붙는다), 조각이 볼록 껍질 강체로 바닥에 떨어지고, 머리 바디가 꺼지고, 피가 오른다
 */
SW_TEST_CASE( DismembermentRuntimeTest, FatalHeadHitSeversAndSpawnsPhysicsPiece )
{
    using Internal = TestDismembermentRuntimeInternal;
    SW_ASSERT_TRUE( ResourceUtil::initialize() );
    GameObjectManager   manager;
    GameObject*         pFloorObject = manager.createGameObject( hashed_string( "Floor" ) );
    RigidBodyComponent* pFloor       = pFloorObject->addComponent<RigidBodyComponent>();
    PhysicsShapeDesc3D  floorBox;
    floorBox._halfExtents = float3{ 20.0f, 0.5f, 20.0f };
    pFloor->setShape( floorBox );
    pFloor->setBodyType( PhysicsBodyType::Static );
    pFloor->setLayer( hashed_string( "Static" ) );
    pFloor->setLocalPosition( float3{ 0.0f, -0.5f, 0.0f } );

    GameObject* pSkeleton = manager.createGameObject( hashed_string( "Skeleton" ) );
    pSkeleton->addComponent<SceneComponent>();
    SkeletalMeshComponent*  pUnit      = pSkeleton->addComponent<SkeletalMeshComponent>();
    RagdollComponent*       pRagdoll   = pSkeleton->addComponent<RagdollComponent>();
    DismembermentComponent* pDismember = pSkeleton->addComponent<DismembermentComponent>();
    SW_ASSERT_TRUE( pUnit != nullptr && pRagdoll != nullptr && pDismember != nullptr );
    pUnit->setMeshId( Internal::kMeshPath );
    pUnit->setSkeletonPath( Internal::kSkeletonPath );
    pRagdoll->setPhysicsAssetPath( Internal::kPhysicsPath );
    pDismember->setSeverableRegions( { hashed_string( "Head" ), hashed_string( "Arm_R" ) } );
    pDismember->setRegionTablePath( Internal::kRegionPath );

    manager.beginPlay();
    Internal::tickFor( manager, 2 );
    SW_ASSERT_NOT_NULL( pUnit->getMesh() );
    SW_ASSERT_TRUE( pUnit->getMesh()->hasSkin() );
    const uint32 vertexCountBefore = pUnit->getMesh()->getVertexCount();
    const int32  head              = pRagdoll->getPhysicsAsset()->findBodyIndex( hashed_string( "head" ) );
    SW_ASSERT_TRUE( head >= 0 );
    SW_EXPECT_TRUE( pDismember->findRegionOfBone( hashed_string( "lowerarm.r" ) ) == hashed_string( "Arm_R" ) );

    HitInfo hit;
    hit._body      = pRagdoll->getRagdoll()._listBody[static_cast<size_t>( head )];
    hit._direction = float3{ 0.0f, 0.0f, 1.0f };
    hit._impulse   = 20.0f;
    hit._bFatal    = true;
    CharacterHitUtil::deliverHit( *pSkeleton, hit );

    SW_ASSERT_EQUAL( size_t( 1 ), pDismember->getSeveredRegions().size() );
    SW_EXPECT_TRUE( pDismember->getSeveredRegions()[0] == hashed_string( "Head" ) );
    const uint32 vertexCountAfter = pUnit->getMesh()->getVertexCount();
    SW_EXPECT_TRUE( vertexCountAfter < vertexCountBefore );
    SW_EXPECT_TRUE( vertexCountAfter > vertexCountBefore / 2 ); // 머리만 빠졌다
    SW_EXPECT_TRUE( pUnit->getMesh()->hasSkin() );
    SW_EXPECT_EQUAL( 0u, vertexCountAfter % 3 );
    SW_EXPECT_NEAR_EQUAL( 1.0f, pDismember->getSurfaceState().getRegionValue( static_cast<uint32>( pDismember->findRegionIndex( hashed_string( "Head" ) ) ), hashed_string( "Blood" ) ),
                          1e-6f );
    IPhysicsScene3D* pScene = manager.getScenePhysics().findScene3D();
    SW_ASSERT_NOT_NULL( pScene );
    SW_EXPECT_FALSE( pScene->isBodyEnabled( pRagdoll->getRagdoll()._listBody[static_cast<size_t>( head )] ) );

    GameObject* pPiece = manager.resolveGameObject( pDismember->getLastPiece() );
    SW_ASSERT_NOT_NULL( pPiece );
    RigidBodyComponent* pPieceBody = pPiece->getComponent<RigidBodyComponent>();
    MeshComponent*      pPieceMesh = pPiece->getComponent<MeshComponent>();
    SW_ASSERT_TRUE( pPieceBody != nullptr && pPieceMesh != nullptr && pPieceMesh->getMesh() != nullptr );
    SW_EXPECT_TRUE( pPieceMesh->getMesh()->getVertexCount() > 0 );
    SW_EXPECT_FALSE( pPieceMesh->getMesh()->hasSkin() );                                               // 지금 포즈로 구운 정적 메시
    SW_EXPECT_TRUE( vertexCountAfter + pPieceMesh->getMesh()->getVertexCount() >= vertexCountBefore ); // 잘린 삼각형은 조각으로 갔다(+ 자른 자리가 있으면 캡)
    const float32 startY = pPieceBody->getWorldPosition()._y;
    SW_EXPECT_TRUE( startY > 1.0f ); // 머리 높이에서 시작한다
    Internal::tickFor( manager, 150 );
    SW_EXPECT_TRUE( pPieceBody->getWorldPosition()._y < startY - 0.5f ); // 떨어졌다
    SW_EXPECT_TRUE( pPieceBody->getWorldPosition()._y > 0.0f );          // 바닥 위
    SW_EXPECT_TRUE( pPieceBody->getWorldPosition()._z > 0.05f );         // 맞은 방향으로 밀렸다

    // 같은 영역은 다시 자르지 않는다. 잘라 낼 수 없는 영역(몸통)의 치명적 맞음은 아무것도 하지 않는다.
    SW_EXPECT_FALSE( pDismember->severRegion( hashed_string( "Head" ), float3{} ) );
    const int32 chest = pRagdoll->getPhysicsAsset()->findBodyIndex( hashed_string( "chest" ) );
    hit._body         = pRagdoll->getRagdoll()._listBody[static_cast<size_t>( chest )];
    hit._bodyIndex    = -1;
    CharacterHitUtil::deliverHit( *pSkeleton, hit );
    SW_EXPECT_EQUAL( size_t( 1 ), pDismember->getSeveredRegions().size() );

    // 래그돌 한가운데(몸통 — 관절이 사방으로 걸린 바디)를 잘라도 관절이 함께 지워져 물리가 계속 돈다.
    SW_ASSERT_TRUE( pDismember->severRegion( hashed_string( "Torso" ), float3{} ) );
    Internal::tickFor( manager, 30 );
    SW_EXPECT_FALSE( pScene->isBodyEnabled( pRagdoll->getRagdoll()._listBody[static_cast<size_t>( chest )] ) );
    SW_EXPECT_FALSE( pRagdoll->getRagdoll()._listJoint[static_cast<size_t>( chest )].isValid() );

    // 이어진 몸은 자른 자리에 캡이 붙는다(두 쪽의 정점 합이 원래보다 많다) — KayKit 해골 · 기사는 부위마다 떨어진 껍질이라 캡이 없으므로 이어진 관을 쓴다.
    const string regionPath = test::makeTempPath( "tube.fit.xml" );
    SW_ASSERT_TRUE( FileUtil::writeTextFile( regionPath, R"(<FitTables><Region name="Upper" bones="bone1"/><Region name="Lower" bones="bone0"/></FitTables>)" ) );
    GameObject* pTube = manager.createGameObject( hashed_string( "Tube" ) );
    pTube->addComponent<SceneComponent>()->setLocalPosition( float3{ 3.0f, 0.0f, 0.0f } );
    SkeletalMeshComponent*  pTubeUnit      = pTube->addComponent<SkeletalMeshComponent>();
    DismembermentComponent* pTubeDismember = pTube->addComponent<DismembermentComponent>();
    pTubeDismember->setSeverableRegions( { hashed_string( "Upper" ) } );
    pTubeDismember->setRegionTablePath( regionPath );
    Internal::tickFor( manager, 2 );
    pTubeUnit->setSkeleton( make_shared<Skeleton>( test::makeChainSkeleton( 2 ) ) );
    pTubeUnit->setMesh( Internal::makeSkinnedTube() );
    const uint32 beforeTube = pTubeUnit->getMesh()->getVertexCount();
    SW_ASSERT_TRUE( pTubeDismember->severRegion( hashed_string( "Upper" ), float3{} ) );
    GameObject* pUpper = manager.resolveGameObject( pTubeDismember->getLastPiece() );
    SW_ASSERT_TRUE( pUpper != nullptr && pUpper->getComponent<MeshComponent>() != nullptr );
    SW_EXPECT_TRUE( pTubeUnit->getMesh()->getVertexCount() + pUpper->getComponent<MeshComponent>()->getMesh()->getVertexCount() > beforeTube );
    manager.endPlay();
}
