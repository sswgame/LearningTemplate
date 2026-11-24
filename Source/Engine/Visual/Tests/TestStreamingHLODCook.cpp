// StreamingHLODCookTest — 씬 스트리밍 (라) HLOD 굽기: 셀을 나눌 때 조각마다 정적 메시를 인스턴스 메시로 모은 대리 엔티티(늘 올라 있음)를 굽고 조각에 그 파일 id 를 적는다.
// 움직이는 메시 · 숨긴 메시 · 늘 올라 있는 엔티티는 대리에 들지 않고, 인스턴스 자리는 원본의 월드 자리다.
#include "pch.h"

#include "Core/File/FileUtil.h"
#include "Core/Math/MathUtil.h"
#include "Core/Testing/TestFramework.h"

#include "Engine/Content/Tests/ScopedScratchDomain.h"
#include "Engine/Graphics/Mesh/Mesh.h"
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
    struct StreamingHLODCookTestUtil
    {
        static string makeMesh( const utf8* pID, const utf8* pName, const utf8* pMesh, const utf8* pPosition, const utf8* pScale, const utf8* pStatic, const utf8* pVisible = "true",
                                const utf8* pObjectFlags = "" )
        {
            StringBuilder<constant::buffer::kMax1024> builder;
            builder.appendFormat( "<entity id=\"%#\" name=\"%#\"><GameObject _schemaVersion=\"0\" _name=\"%#\" _bActive=\"true\"%#><_listComponent><MeshComponent _localPosition=\"%#\" _localRotation=\"0,0,0\" "
                                  "_localScale=\"%#\" _attachOwner=\"None\" _attachComponent=\"None\" _meshID=\"%#\" _bStatic=\"%#\" _bVisible=\"%#\" /></_listComponent></GameObject></entity>",
                                  pID, pName, pName, pObjectFlags, pPosition, pScale, pMesh, pStatic, pVisible );
            return string( builder.view() );
        }

        /**
         * @brief 10 m 셀 스트리밍 씬 — 셀 (0,0) 에 정적 Rock1 · Rock2(같은 메시), 셀 (1,0) 에 움직이는 Lamp 와 숨긴 정적 Ghost, 셀 (2,0) 에 스케일 2 인 정적 Barrel,
         *        늘 올라 있는 정적 Ground(원점 셀)입니다.
         */
        static void writeScene( const test::ScopedScratchDomain& domain, uint32 mergeMaxInstanceCount = 0 )
        {
            StringBuilder<constant::buffer::kMax4096> scene;
            scene.appendFormat( "<Scene formatVersion=\"3\" name=\"HLODCook\"><SceneSettings _bStreaming=\"true\" _unloadMargin=\"0\" _maxCellLoadsPerFrame=\"8\" _hlodMergeMaxInstanceCount=\"%#\" ", mergeMaxInstanceCount );
            scene.append( "_addToWorldBudgetMicroseconds=\"2000\"><_cellGrid _cellSize=\"10\" _loadingRadius=\"10\" /></SceneSettings><entities>" );
            for ( const string& entity : { makeMesh( "1", "Rock1", "Cube", "2,0,2", "1,1,1", "true" ), makeMesh( "2", "Rock2", "Cube", "5,0,5", "1,1,1", "true" ),
                                           makeMesh( "3", "Lamp", "Cube", "15,0,5", "1,1,1", "false" ), makeMesh( "4", "Ghost", "Cube", "16,0,5", "1,1,1", "true", "false" ),
                                           makeMesh( "5", "Barrel", "Sphere", "25,0,5", "2,2,2", "true" ),
                                           makeMesh( "6", "Ground", "Cube", "1,0,1", "1,1,1", "true", "true", " _bAlwaysLoaded=\"true\"" ) } )
            {
                scene.append( entity );
            }
            scene.append( "</entities></Scene>" );
            SW_EXPECT_TRUE( FileUtil::writeTextFile( domain.makeDiskPath( "hlod.scene.xml" ), scene.view() ) );
        }

        static const SceneDocument::StreamingCell* findCell( const SceneDocument& doc, int32 x )
        {
            for ( const SceneDocument::StreamingCell& cell : doc._listStreamingCell )
            {
                if ( cell._cell == StreamingCellCoord{ x, 0 } )
                    return &cell;
            }
            return nullptr;
        }
    };
    using Util = StreamingHLODCookTestUtil;
} // namespace

/** @brief [StreamingHLODCookTest] 정적 메시가 있는 조각만 대리를 받고, 대리는 늘 올라 있는 기본 엔티티이며 조각이 그 파일 id 를 가리킨다 */
SW_TEST_CASE( StreamingHLODCookTest, SplitBuildsProxyPerFragmentWithStaticMeshes )
{
    const test::ScopedScratchDomain domain;
    Util::writeScene( domain );
    SceneDocument doc{};
    SW_REQUIRE_TRUE( doc.loadXMLFromDiskFile( domain.makeDiskPath( "hlod.scene.xml" ) ) );
    SW_REQUIRE_TRUE( SceneCellSplitter::split( SceneEngineServices::makeFromBoundEngine(), doc, "hlod.scene.xml" ) );

    SW_REQUIRE_EQUAL( size_t{ 3 }, doc._listStreamingCell.size() );
    const SceneDocument::StreamingCell* pFirst      = Util::findCell( doc, 0 );
    const SceneDocument::StreamingCell* pMovingCell = Util::findCell( doc, 1 );
    const SceneDocument::StreamingCell* pScaled     = Util::findCell( doc, 2 );
    SW_REQUIRE_TRUE( pFirst != nullptr && pMovingCell != nullptr && pScaled != nullptr );
    SW_EXPECT_TRUE( pFirst->_hlodFileID != 0 );
    SW_EXPECT_EQUAL( uint64{ 0 }, pMovingCell->_hlodFileID ); // 움직이는 메시와 숨긴 메시뿐이다
    SW_EXPECT_TRUE( pScaled->_hlodFileID != 0 );
    // 조각의 경계는 엔티티의 월드 크기다(스케일 2 인 통은 24 ~ 26) — 움직이는 메시도 몸이라 잰다.
    SW_EXPECT_NEAR( 24.0f, pScaled->_contentBounds._minX, 0.1f );
    SW_EXPECT_NEAR( 26.0f, pScaled->_contentBounds._maxX, 0.1f );
    SW_EXPECT_TRUE( pMovingCell->_contentBounds.isValid() );
    SW_EXPECT_TRUE( pFirst->_hlodFileID != pScaled->_hlodFileID );

    // 기본 목록: Ground + 대리 둘. 대리의 파일 id 는 엔티티와 겹치지 않는다.
    uint32 proxyCount = 0;
    for ( const SceneDocument::SceneObjectNode& node : doc._listSceneObjectNode )
    {
        const bool bProxy = node._fileID == pFirst->_hlodFileID || node._fileID == pScaled->_hlodFileID;
        proxyCount += bProxy ? 1u : 0u;
        SW_EXPECT_TRUE( bProxy == false || node._fileID > 6 );
    }
    SW_EXPECT_EQUAL( 2u, proxyCount );
    SW_EXPECT_EQUAL( size_t{ 3 }, doc._listSceneObjectNode.size() );
}

/** @brief [StreamingHLODCookTest] 대리를 지으면 (메시, 머티리얼, 구간)마다 인스턴스 메시 하나이고 인스턴스 자리 · 스케일은 원본의 월드 값이며, 대리는 늘 올라 있다 */
SW_TEST_CASE( StreamingHLODCookTest, ProxyInstancesUseWorldPlacement )
{
    const test::ScopedScratchDomain domain;
    Util::writeScene( domain );
    SceneDocument doc{};
    SW_REQUIRE_TRUE( doc.loadXMLFromDiskFile( domain.makeDiskPath( "hlod.scene.xml" ) ) );
    SW_REQUIRE_TRUE( SceneCellSplitter::split( SceneEngineServices::makeFromBoundEngine(), doc, "hlod.scene.xml" ) );

    Scene scene{ "HLODCook" };
    SW_REQUIRE_TRUE( scene.instantiate( doc ) );
    GameObjectManager& objects = *scene.getObjectManager();
    const GameObject*  pFirst  = objects.findGameObjectByName( hashed_string( "HLOD_0_0" ) );
    const GameObject*  pScaled = objects.findGameObjectByName( hashed_string( "HLOD_2_0" ) );
    SW_REQUIRE_TRUE( pFirst != nullptr && pScaled != nullptr );
    SW_EXPECT_TRUE( pFirst->isAlwaysLoaded() );
    SW_EXPECT_TRUE( objects.findGameObjectByName( hashed_string( "HLOD_1_0" ) ) == nullptr );

    const InstancedMeshComponent* pFirstInstances = pFirst->getComponent<InstancedMeshComponent>();
    SW_REQUIRE_NOT_NULL( pFirstInstances );
    SW_EXPECT_EQUAL( string( "Cube" ), pFirstInstances->getMeshID() );
    SW_REQUIRE_EQUAL( size_t{ 2 }, pFirstInstances->getInstances().size() );
    SW_EXPECT_NEAR( 2.0f, pFirstInstances->getInstances()[0]._position._x, 1e-4f );
    SW_EXPECT_NEAR( 5.0f, pFirstInstances->getInstances()[1]._position._z, 1e-4f );

    const InstancedMeshComponent* pScaledInstances = pScaled->getComponent<InstancedMeshComponent>();
    SW_REQUIRE_NOT_NULL( pScaledInstances );
    SW_EXPECT_EQUAL( string( "Sphere" ), pScaledInstances->getMeshID() );
    SW_REQUIRE_EQUAL( size_t{ 1 }, pScaledInstances->getInstances().size() );
    SW_EXPECT_NEAR( 25.0f, pScaledInstances->getInstances()[0]._position._x, 1e-4f );
    SW_EXPECT_NEAR( 2.0f, pScaledInstances->getInstances()[0]._scale._y, 1e-4f );
}

/**
 * @brief [StreamingHLODCookTest] 합치는 상한 이하로 놓인 메시는 머티리얼마다 합친 메시 하나(`HLODMeshComponent`)로, 하나뿐인 머티리얼은 인스턴스로 둔다
 * @details 셀 (0,0) 의 바위 둘(같은 메시, 상한 2)은 부품 둘인 합친 메시가 되고 중심에 놓인다. 셀 (2,0) 은 통 하나라 합칠 것이 없다.
 */
SW_TEST_CASE( StreamingHLODCookTest, RareMeshesMergeByMaterial )
{
    const test::ScopedScratchDomain domain;
    Util::writeScene( domain, 2 );
    SceneDocument doc{};
    SW_REQUIRE_TRUE( doc.loadXMLFromDiskFile( domain.makeDiskPath( "hlod.scene.xml" ) ) );
    SW_REQUIRE_TRUE( SceneCellSplitter::split( SceneEngineServices::makeFromBoundEngine(), doc, "hlod.scene.xml" ) );

    Scene scene{ "HLODCook" };
    SW_REQUIRE_TRUE( scene.instantiate( doc ) );
    GameObjectManager& objects = *scene.getObjectManager();
    const GameObject*  pFirst  = objects.findGameObjectByName( hashed_string( "HLOD_0_0" ) );
    const GameObject*  pScaled = objects.findGameObjectByName( hashed_string( "HLOD_2_0" ) );
    SW_REQUIRE_TRUE( pFirst != nullptr && pScaled != nullptr );

    const HLODMeshComponent* pMerged = pFirst->getComponent<HLODMeshComponent>();
    SW_REQUIRE_NOT_NULL( pMerged );
    SW_REQUIRE_EQUAL( size_t{ 2 }, pMerged->getParts().size() );
    SW_REQUIRE_EQUAL( size_t{ 1 }, pMerged->getInstances().size() );
    SW_EXPECT_NEAR( 3.5f, pMerged->getInstances()[0]._position._x, 1e-4f ); // (2 + 5) / 2
    SW_EXPECT_NEAR( -1.5f, pMerged->getParts()[0]._position._x, 1e-4f );
    SW_EXPECT_NEAR( 1.5f, pMerged->getParts()[1]._position._z, 1e-4f );
    SW_EXPECT_TRUE( pScaled->getComponent<HLODMeshComponent>() == nullptr );
    SW_EXPECT_NOT_NULL( pScaled->getComponent<InstancedMeshComponent>() );
}

/** @brief [StreamingHLODCookTest] 부품 합치기 — 정점은 부품 자리로 옮고 노멀은 단위 길이로 남으며, 거울 스케일은 감김을 뒤집는다. 없는 구간은 더하지 않고 알린다 */
SW_TEST_CASE( StreamingHLODCookTest, PartsAppendIntoOneMesh )
{
    const shared_ptr<Mesh> cube = MeshUtil::acquirePrimitive( "Cube" );
    SW_REQUIRE_NOT_NULL( cube );
    HLODMeshPart part;
    part._position = float3{ 10.0f, 0.0f, 0.0f };
    part._scale    = float3{ 2.0f, 1.0f, 1.0f };
    vector<RHIVertex> listVertex;
    vector<uint32>    listIndex;
    SW_REQUIRE_TRUE( HLODMeshComponent::appendPart( part, *cube, listVertex, listIndex ) );
    SW_REQUIRE_EQUAL( static_cast<size_t>( cube->getIndexCount() ), listIndex.size() );
    const size_t firstVertexCount = listVertex.size();
    float32      minX             = MathUtil::kMaxFloat;
    float32      maxX             = -MathUtil::kMaxFloat;
    for ( const RHIVertex& vertex : listVertex )
    {
        minX = MathUtil::min( minX, vertex._arrPosition[0] );
        maxX = MathUtil::max( maxX, vertex._arrPosition[0] );
        SW_EXPECT_NEAR( 1.0f, ( float3{ vertex._arrNormal[0], vertex._arrNormal[1], vertex._arrNormal[2] } ).getLength(), 1e-4f );
    }
    const float32 cubeWidth = cube->getLocalBoundsMax()._x - cube->getLocalBoundsMin()._x;
    SW_EXPECT_NEAR( 10.0f - cubeWidth, minX, 1e-4f );
    SW_EXPECT_NEAR( 10.0f + cubeWidth, maxX, 1e-4f );

    HLODMeshPart mirrored;
    mirrored._scale = float3{ -1.0f, 1.0f, 1.0f };
    SW_REQUIRE_TRUE( HLODMeshComponent::appendPart( mirrored, *cube, listVertex, listIndex ) );
    SW_EXPECT_EQUAL( firstVertexCount * 2, listVertex.size() );
    // 거울상도 바깥을 본다 — 첫 삼각형의 기하 노멀이 정점 노멀과 같은 쪽이다.
    const size_t firstMirrored = static_cast<size_t>( cube->getIndexCount() );
    const auto   position      = [&listVertex, &listIndex]( size_t index )
    {
        const RHIVertex& vertex = listVertex[listIndex[index]];
        return float3{ vertex._arrPosition[0], vertex._arrPosition[1], vertex._arrPosition[2] };
    };
    const RHIVertex& corner = listVertex[listIndex[firstMirrored]];
    const float3     originalNormal{ listVertex[listIndex[0]]._arrNormal[0], listVertex[listIndex[0]]._arrNormal[1], listVertex[listIndex[0]]._arrNormal[2] };
    const float3     originalFace = ( position( 1 ) - position( 0 ) ).cross( position( 2 ) - position( 0 ) );
    const float3     mirroredNormal{ corner._arrNormal[0], corner._arrNormal[1], corner._arrNormal[2] };
    const float3     mirroredFace = ( position( firstMirrored + 1 ) - position( firstMirrored ) ).cross( position( firstMirrored + 2 ) - position( firstMirrored ) );
    SW_EXPECT_EQUAL( originalFace.dot( originalNormal ) > 0.0f, mirroredFace.dot( mirroredNormal ) > 0.0f );

    HLODMeshPart missing;
    missing._sectionName = hashed_string( "NoSuchSection" );
    SW_EXPECT_FALSE( HLODMeshComponent::appendPart( missing, *cube, listVertex, listIndex ) );
    SW_EXPECT_EQUAL( firstVertexCount * 2, listVertex.size() );
}

/** @brief [StreamingHLODCookTest] 셀에 든 인스턴스 메시(씬 인스턴스를 묶은 것)도 인스턴스마다 대리에 들고, 같은 메시의 정적 메시와 한 묶음이다 */
SW_TEST_CASE( StreamingHLODCookTest, InstancedMeshesJoinTheProxy )
{
    const test::ScopedScratchDomain           domain;
    StringBuilder<constant::buffer::kMax4096> text;
    text.append( "<Scene formatVersion=\"3\" name=\"HLODInstanced\"><SceneSettings _bStreaming=\"true\" _unloadMargin=\"0\" _maxCellLoadsPerFrame=\"8\" _addToWorldBudgetMicroseconds=\"2000\">"
                 "<_cellGrid _cellSize=\"10\" _loadingRadius=\"10\" /></SceneSettings><entities>" );
    text.append( Util::makeMesh( "1", "Rock", "Cube", "2,0,2", "1,1,1", "true" ) );
    text.append( "<entity id=\"2\" name=\"Props\"><GameObject _schemaVersion=\"0\" _name=\"Props\" _bActive=\"true\"><_listComponent><InstancedMeshComponent _localPosition=\"4,0,4\" "
                 "_attachOwner=\"None\" _attachComponent=\"None\" _meshID=\"Cube\"><_listInstance><InstancedMeshTransform _position=\"0,0,0\" /><InstancedMeshTransform _position=\"1,0,0\" _scale=\"2,2,2\" />"
                 "</_listInstance></InstancedMeshComponent></_listComponent></GameObject></entity></entities></Scene>" );
    SW_REQUIRE_TRUE( FileUtil::writeTextFile( domain.makeDiskPath( "hlodinstanced.scene.xml" ), text.view() ) );
    SceneDocument doc{};
    SW_REQUIRE_TRUE( doc.loadXMLFromDiskFile( domain.makeDiskPath( "hlodinstanced.scene.xml" ) ) );
    SW_REQUIRE_TRUE( SceneCellSplitter::split( SceneEngineServices::makeFromBoundEngine(), doc, "hlodinstanced.scene.xml" ) );

    Scene scene{ "HLODInstanced" };
    SW_REQUIRE_TRUE( scene.instantiate( doc ) );
    const GameObject* pProxy = scene.getObjectManager()->findGameObjectByName( hashed_string( "HLOD_0_0" ) );
    SW_REQUIRE_NOT_NULL( pProxy );
    const InstancedMeshComponent* pInstances = pProxy->getComponent<InstancedMeshComponent>();
    SW_REQUIRE_NOT_NULL( pInstances );
    SW_REQUIRE_EQUAL( size_t{ 3 }, pInstances->getInstances().size() );
    SW_EXPECT_NEAR( 4.0f, pInstances->getInstances()[1]._position._x, 1e-4f );
    SW_EXPECT_NEAR( 5.0f, pInstances->getInstances()[2]._position._x, 1e-4f );
    SW_EXPECT_NEAR( 2.0f, pInstances->getInstances()[2]._scale._x, 1e-4f );
}
