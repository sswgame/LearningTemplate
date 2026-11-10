#include "pch.h"

#include "Core/Container/unordered_set.h"
#include "Core/Math/MathUtil.h"
#include "Core/Math/MatrixMath.h"
#include "Core/Memory/Memory.h"
#include "Core/Testing/TestFramework.h"

#include "Engine/Graphics/LOD/LODView.h"
#include "Engine/Graphics/Mesh/Mesh.h"
#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManager.inl"
#include "Engine/Object/GameObject/Registry/PrimitiveRegistry.h"
#include "Engine/Visual/Component/MeshComponent.h"
#include "Engine/Visual/MeshLODSystem.h"

using namespace sw;

// MeshLODSystemTest — 메시 LOD 고르기: 워커로 나눠 고르고 게임 스레드가 차례로 넣은 결과가 컴포넌트 하나씩 직렬로 고른 결과와 같다(nogpu).

namespace
{
    struct TestMeshLODSystemInternal
    {
        /** @brief 정점 넷의 사각형(반지름 약 0.7)입니다. */
        static vector<RHIVertex> makeQuadVertices()
        {
            vector<RHIVertex> listVertex( 4 );
            const float32     arrPosition[4][2] = {
                {0.0f, 0.0f},
                {1.0f, 0.0f},
                {1.0f, 1.0f},
                {0.0f, 1.0f}
            };
            for ( size_t index = 0; index < listVertex.size(); ++index )
            {
                listVertex[index]._arrPosition[0] = arrPosition[index][0];
                listVertex[index]._arrPosition[1] = arrPosition[index][1];
                listVertex[index]._arrNormal[2]   = 1.0f;
                listVertex[index]._arrUV[0]       = arrPosition[index][0];
                listVertex[index]._arrUV[1]       = 1.0f - arrPosition[index][1];
                listVertex[index]._arrColor[3]    = 1.0f;
            }
            return listVertex;
        }

        /** @brief 단계 셋(원본 + 화면 크기 0.2 · 0.02 의 거친 단계 둘)을 가진 메시입니다. */
        static shared_ptr<Mesh> makeLODMesh()
        {
            shared_ptr<Mesh> mesh = Mesh::create();
            if ( mesh->setGeometry( makeQuadVertices(), vector<uint32>{ 0, 1, 2, 0, 2, 3 } ) == false )
                return nullptr;
            vector<MeshLODLevel> listLevel;
            for ( const float32 screenSize : { 0.2f, 0.02f } )
            {
                MeshLODLevel level{};
                level._listVertex = makeQuadVertices();
                level._listVertex.resize( 3 );
                Mesh::computeTangents( level._listVertex );
                level._listIndex  = vector<uint32>{ 0, 1, 2 };
                level._screenSize = screenSize;
                listLevel.push_back( std::move( level ) );
            }
            return mesh->setLODs( std::move( listLevel ) ) ? mesh : nullptr;
        }

        /** @brief (0, 0, -10) 에서 +Z 를 보는 시야각 90° · 정사각 화면의 뷰입니다 — 화면 크기 = 반지름 / 거리. */
        static LODView makeView()
        {
            const float3   eye  = float3{ 0.0f, 0.0f, -10.0f };
            const float4x4 view = float4x4::makeLookAt( eye, float3{ 0.0f, 0.0f, 0.0f }, float3{ 0.0f, 1.0f, 0.0f } );
            const float4x4 proj = float4x4::makePerspectiveFieldOfView( MathUtil::kPi * 0.5f, 1.0f, 0.1f, 1000.0f );
            return LODView::make( view * proj, eye );
        }
    };
} // namespace

/**
 * @brief [MeshLODSystemTest] 거리가 다른 메시 수백 개를 한 번에 고른 단계가 컴포넌트마다 `selectNextLOD`( 화면 크기, 지금 단계 ) 로 직렬로 고른 단계와 같다
 * @details 병렬 본문은 고르기만 하고 적용(`setLODIndex`)은 게임 스레드가 목록 순서로 하므로, 나누는 방법과 상관없이 결과가 같아야 한다. 단계가 둘 이상 나와야 판정이 뜻을 가진다.
 */
SW_TEST_CASE( MeshLODSystemTest, ParallelPickMatchesSerialPickPerComponent )
{
    constexpr uint32       kMeshCount = 512;
    const shared_ptr<Mesh> mesh       = TestMeshLODSystemInternal::makeLODMesh();
    SW_REQUIRE_NOT_NULL( mesh.get() );
    SW_REQUIRE_TRUE( mesh->getLODCount() > 1 );

    GameObjectManager      manager;
    vector<MeshComponent*> listComponent;
    for ( uint32 meshIndex = 0; meshIndex < kMeshCount; ++meshIndex )
    {
        GameObject* pObject = manager.createGameObject( hashed_string( "LODMesh" ) );
        SW_REQUIRE_NOT_NULL( pObject );
        MeshComponent* pMesh = pObject->addComponent<MeshComponent>();
        SW_REQUIRE_NOT_NULL( pMesh );
        pMesh->setWorldPosition( float3{ 0.0f, 0.0f, 0.5f * static_cast<float32>( meshIndex ) } ); // 눈에서 10 ~ 265 m
        pMesh->setMesh( mesh );
        listComponent.push_back( pMesh );
    }
    manager.mergePendingAdds();
    manager.flushSceneTransforms();
    SW_REQUIRE_EQUAL( kMeshCount, static_cast<uint32>( manager.getPrimitiveRegistry().getLODPrimitives().size() ) );

    MeshLODSystem& lodSystem = manager.getSubsystem<MeshLODSystem>();
    lodSystem.setViews( vector<LODView>{ TestMeshLODSystemInternal::makeView() } );
    // 기대값은 고르기 전에 같은 입력(화면 크기 · 지금 단계)으로 하나씩 구한다.
    vector<uint32> listExpected;
    for ( const MeshComponent* pMesh : listComponent )
        listExpected.push_back( MeshLODSystem::selectNextLOD( *mesh, lodSystem.computeScreenSize( *pMesh ), pMesh->getLODIndex() ) );

    lodSystem.update( 0.0f );

    unordered_set<uint32> uniqueSetLOD;
    for ( uint32 meshIndex = 0; meshIndex < kMeshCount; ++meshIndex )
    {
        SW_EXPECT_EQUAL( listExpected[meshIndex], listComponent[meshIndex]->getLODIndex() );
        uniqueSetLOD.insert( listComponent[meshIndex]->getLODIndex() );
    }
    SW_EXPECT_TRUE( uniqueSetLOD.size() >= 2u );
}
