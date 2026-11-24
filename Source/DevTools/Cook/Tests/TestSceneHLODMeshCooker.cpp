// SceneHLODMeshCookerTest — 합친 HLOD 대리를 쿠킹 때 씬마다 메시 하나(대리 컴포넌트마다 구간 하나)로 합치고, 쿠킹본의 대리는 그 구간을 그리는 인스턴스 메시가 된다.
#include "pch.h"

#include "Core/File/FileUtil.h"
#include "Core/String/StringBuilder.h"
#include "Core/Testing/TestFramework.h"

#include "DevTools/Cook/SceneHLODMeshCooker.h"

#include "Engine/Content/Tests/ScopedScratchDomain.h"
#include "Engine/Graphics/Mesh/Mesh.h"
#include "Engine/Graphics/Mesh/MeshAssetFormat.h"
#include "Engine/Graphics/Mesh/MeshUtil.h"
#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManager.inl"
#include "Engine/Object/GameObject/Service/SceneEngineServices.h"
#include "Engine/Scene/Scene.h"
#include "Engine/Scene/SceneCellSplitter.h"
#include "Engine/Scene/SceneDocument.h"
#include "Engine/Visual/Component/HLODMeshComponent.h"
#include "Engine/Visual/Component/InstancedMeshComponent.h"

using namespace sw;

namespace
{
    struct SceneHLODMeshCookerTestUtil
    {
        static string makeMesh( const utf8* pID, const utf8* pMesh, const utf8* pPosition )
        {
            StringBuilder<constant::buffer::kMax1024> builder;
            builder.appendFormat( "<entity id=\"%#\" name=\"M%#\"><GameObject _schemaVersion=\"0\" _name=\"M%#\" _bActive=\"true\"><_listComponent><MeshComponent _localPosition=\"%#\" _localRotation=\"0,0,0\" "
                                  "_localScale=\"1,1,1\" _attachOwner=\"None\" _attachComponent=\"None\" _meshID=\"%#\" _bStatic=\"true\" _bVisible=\"true\" /></_listComponent></GameObject></entity>",
                                  pID, pID, pID, pPosition, pMesh );
            return string( builder.view() );
        }

        /** @brief 도형 @p pMesh 하나가 합친 메시에 더하는 인덱스 수입니다(가장 낮은 LOD). */
        static uint32 getMergedIndexCount( const utf8* pMesh )
        {
            const shared_ptr<Mesh> mesh = MeshUtil::acquirePrimitive( pMesh );
            if ( mesh == nullptr )
                return 0;
            return mesh->getLODs().empty() ? mesh->getIndexCount() : mesh->getLODs().back()._indexCount;
        }

        /** @brief 10 m 셀 스트리밍 씬 — 셀 (0,0) 에 상자 · 공, 셀 (2,0) 에 상자 둘을 놓고 합치는 상한이 2 다. 나눈 문서를 돌려줍니다. */
        static bool loadSplitScene( const test::ScopedScratchDomain& domain, SceneDocument& outDoc )
        {
            StringBuilder<constant::buffer::kMax4096> scene;
            scene.append( "<Scene formatVersion=\"3\" name=\"HLODMesh\"><SceneSettings _bStreaming=\"true\" _unloadMargin=\"0\" _maxCellLoadsPerFrame=\"8\" _hlodMergeMaxInstanceCount=\"2\" "
                          "_addToWorldBudgetMicroseconds=\"2000\"><_cellGrid _cellSize=\"10\" _loadingRadius=\"10\" /></SceneSettings><entities>" );
            for ( const string& entity : { makeMesh( "1", "Cube", "2,0,2" ), makeMesh( "2", "Sphere", "5,0,5" ), makeMesh( "3", "Cube", "22,0,2" ), makeMesh( "4", "Cube", "26,0,6" ) } )
            {
                scene.append( entity );
            }
            scene.append( "</entities></Scene>" );
            return FileUtil::writeTextFile( domain.makeDiskPath( "hlodmesh.scene.xml" ), scene.view() ) && outDoc.loadXMLFromDiskFile( domain.makeDiskPath( "hlodmesh.scene.xml" ) ) &&
                   SceneCellSplitter::split( SceneEngineServices::makeFromBoundEngine(), outDoc, "hlodmesh.scene.xml" );
        }
    };
    using Util = SceneHLODMeshCookerTestUtil;
} // namespace

/** @brief [SceneHLODMeshCookerTest] 메시 경로는 쿠킹한 씬 옆이고, 쿠킹한 씬 경로가 아니면 비어 있다 */
SW_TEST_CASE( SceneHLODMeshCookerTest, MeshPathSitsNextToTheCookedScene )
{
    SW_EXPECT_EQUAL( string( "game/a/maps/town.hlod.mesh" ), SceneHLODMeshCooker::makeMeshPath( "game/a/maps/town.scene.bin" ) );
    SW_EXPECT_TRUE( SceneHLODMeshCooker::makeMeshPath( "game/a/maps/town.prefab.bin" ).empty() );
}

/**
 * @brief [SceneHLODMeshCookerTest] 합친 대리 컴포넌트마다 구간 하나이고, 바꾼 문서를 지으면 합친 메시 컴포넌트 없이 그 구간을 그리는 인스턴스 메시가 같은 자리에 선다
 * @details 구간 이름은 두 작업(메시 · 씬)이 따로 만든다 — 어긋나면 쿠킹본의 대리가 그려지지 않는다. 변이: 어느 한쪽의 이름 규칙을 바꾸면 구간 찾기 단언이 진다.
 */
SW_TEST_CASE( SceneHLODMeshCookerTest, ProxiesDrawSectionsOfOneCookedMesh )
{
    const test::ScopedScratchDomain domain;
    SceneDocument                   doc{};
    SW_REQUIRE_TRUE( Util::loadSplitScene( domain, doc ) );

    MeshAssetData data{};
    SW_REQUIRE_TRUE( SceneHLODMeshCooker::buildMesh( SceneEngineServices::makeFromBoundEngine(), doc, "hlodmesh.scene.xml", data ) );
    SW_REQUIRE_EQUAL( size_t{ 2 }, data._listSection.size() ); // 셀 둘, 머티리얼 하나씩
    SW_EXPECT_EQUAL( static_cast<size_t>( Util::getMergedIndexCount( "Cube" ) * 3 + Util::getMergedIndexCount( "Sphere" ) ), data._listIndex.size() );
    vector<uint8> bytes;
    MeshAssetFormat::makeBytes( data, bytes );
    MeshAssetData readBack{};
    SW_REQUIRE_TRUE( MeshAssetFormat::readFromBytes( bytes.data(), bytes.size(), readBack ) );
    SW_REQUIRE_EQUAL( size_t{ 2 }, readBack._listSection.size() );

    const string           meshID   = "test/hlodmesh.hlod.mesh";
    const optional<uint32> replaced = SceneHLODMeshCooker::replaceMergedProxies( doc, meshID );
    SW_REQUIRE_TRUE( replaced.has_value() );
    SW_EXPECT_EQUAL( 2u, *replaced );
    const optional<uint32> again = SceneHLODMeshCooker::replaceMergedProxies( doc, meshID );
    SW_EXPECT_TRUE( again.has_value() && *again == 0u ); // 바꿀 것이 남지 않았다

    Scene scene{ "HLODMesh" };
    SW_REQUIRE_TRUE( scene.instantiate( doc ) );
    GameObjectManager& objects = *scene.getObjectManager();
    for ( const utf8* pName : { "HLOD_0_0", "HLOD_2_0" } )
    {
        const GameObject* pProxy = objects.findGameObjectByName( hashed_string( pName ) );
        SW_REQUIRE_NOT_NULL( pProxy );
        SW_EXPECT_TRUE( pProxy->getComponent<HLODMeshComponent>() == nullptr );
        const InstancedMeshComponent* pInstanced = pProxy->getComponent<InstancedMeshComponent>();
        SW_REQUIRE_NOT_NULL( pInstanced );
        SW_EXPECT_EQUAL( meshID, pInstanced->getMeshID() );
        SW_REQUIRE_EQUAL( size_t{ 1 }, pInstanced->getInstances().size() );
        bool bSectionFound = false;
        for ( const MeshSection& section : data._listSection )
        {
            bSectionFound = bSectionFound || section._name == pInstanced->getSectionName();
        }
        SW_EXPECT_TRUE( bSectionFound );
    }
    const InstancedMeshComponent* pSecond = objects.findGameObjectByName( hashed_string( "HLOD_2_0" ) )->getComponent<InstancedMeshComponent>();
    SW_EXPECT_NEAR( 24.0f, pSecond->getInstances()[0]._position._x, 1e-4f ); // (22 + 26) / 2
}
