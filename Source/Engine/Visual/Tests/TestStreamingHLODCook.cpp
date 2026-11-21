// StreamingHLODCookTest — 씬 스트리밍 (라) HLOD 굽기: 셀을 나눌 때 조각마다 정적 메시를 인스턴스 메시로 모은 대리 엔티티(늘 올라 있음)를 굽고 조각에 그 파일 id 를 적는다.
// 움직이는 메시 · 숨긴 메시 · 늘 올라 있는 엔티티는 대리에 들지 않고, 인스턴스 자리는 원본의 월드 자리다.
#include "pch.h"

#include "Core/File/FileUtil.h"
#include "Core/Testing/TestFramework.h"

#include "Engine/Content/Tests/ScopedScratchDomain.h"
#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManager.inl"
#include "Engine/Object/GameObject/Service/SceneEngineServices.h"
#include "Engine/Scene/Scene.h"
#include "Engine/Scene/SceneCooker.h"
#include "Engine/Scene/SceneDocument.h"
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
        static void writeScene( const test::ScopedScratchDomain& domain )
        {
            StringBuilder<constant::buffer::kMax4096> scene;
            scene.append( "<Scene formatVersion=\"3\" name=\"HLODCook\"><SceneSettings _bStreaming=\"true\" _unloadMargin=\"0\" _maxCellLoadsPerFrame=\"8\" "
                          "_addToWorldBudgetMicroseconds=\"2000\"><_cellGrid _cellSize=\"10\" _loadingRadius=\"10\" /></SceneSettings><entities>" );
            for ( const string& entity : { makeMesh( "1", "Rock1", "Cube", "2,0,2", "1,1,1", "true" ), makeMesh( "2", "Rock2", "Cube", "5,0,5", "1,1,1", "true" ),
                                           makeMesh( "3", "Lamp", "Cube", "15,0,5", "1,1,1", "false" ), makeMesh( "4", "Ghost", "Cube", "16,0,5", "1,1,1", "true", "false" ),
                                           makeMesh( "5", "Barrel", "Sphere", "25,0,5", "2,2,2", "true" ),
                                           makeMesh( "6", "Ground", "Cube", "1,0,1", "1,1,1", "true", "true", " _bAlwaysLoaded=\"true\"" ) } )
            {
                scene.append( entity );
            }
            scene.append( "</entities></Scene>" );
            SW_EXPECT_TRUE( FileUtil::writeTextFile( FileUtil::toDiskPath( domain.makeDiskPath( "hlod.scene.xml" ) ), scene.view() ) );
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
    SW_REQUIRE_TRUE( SceneCooker::splitStreamingCells( SceneEngineServices::makeFromBoundEngine(), doc, "hlod.scene.xml" ) );

    SW_REQUIRE_EQUAL( size_t{ 3 }, doc._listStreamingCell.size() );
    const SceneDocument::StreamingCell* pFirst      = Util::findCell( doc, 0 );
    const SceneDocument::StreamingCell* pMovingCell = Util::findCell( doc, 1 );
    const SceneDocument::StreamingCell* pScaled     = Util::findCell( doc, 2 );
    SW_REQUIRE_TRUE( pFirst != nullptr && pMovingCell != nullptr && pScaled != nullptr );
    SW_EXPECT_TRUE( pFirst->_hlodFileID != 0 );
    SW_EXPECT_EQUAL( uint64{ 0 }, pMovingCell->_hlodFileID ); // 움직이는 메시와 숨긴 메시뿐이다
    SW_EXPECT_TRUE( pScaled->_hlodFileID != 0 );
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
    SW_REQUIRE_TRUE( SceneCooker::splitStreamingCells( SceneEngineServices::makeFromBoundEngine(), doc, "hlod.scene.xml" ) );

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
