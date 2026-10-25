#include "pch.h"

#include "Core/File/FileUtil.h"

#include "Engine/Object/Component/SceneComponent.h"
#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManager.h"
#include "Engine/Scene/Scene.h"
#include "Engine/Scene/SceneCooker.h"
#include "Engine/Scene/SceneDocument.h"
#include "Engine/Scene/SceneManager.h"
#include "Engine/Scene/WorldStreamingSubsystem.h"

#include "TestFramework/TestFramework.h"

namespace sw
{
    namespace
    {
        struct WorldStreamingTestInternal
        {
            /** @brief 셀 10 m, 반경 5 m, 프레임당 셀 4 개를 올리는 스트리밍 설정입니다. */
            static WorldSettings makeStreamingSettings()
            {
                WorldSettings settings{};
                settings._bStreaming           = true;
                settings._cellGrid             = WorldCellGridDef{ 10.0f, 5.0f };
                settings._unloadMargin         = 2.0f;
                settings._maxCellLoadsPerFrame = 4;
                return settings;
            }

            /** @brief 씬 컴포넌트 하나를 @p position 에 둔 엔티티입니다. @p attachToFileID 가 0 이 아니면 그 엔티티에 붙습니다. */
            static SceneDocument::SceneObjectNode makeEntity( uint64 fileID, const utf8* pName, const utf8* pPosition, uint64 attachToFileID = 0,
                                                              bool bAlwaysLoaded = false )
            {
                SceneDocument::SceneObjectNode node{};
                node._fileID      = fileID;
                node._name        = pName;
                string attachText = attachToFileID != 0 ? string( " _attachOwnerID=\"" ) + sw::to_string( attachToFileID ) + "\" _attachComponent=\"SceneComponent#0\"" : string{};
                node._embeddedXML = string( "<GameObject _schemaVersion=\"0\" _name=\"" ) + pName + "\" _bActive=\"true\" _bAlwaysLoaded=\"" +
                                    ( bAlwaysLoaded ? "true" : "false" ) + "\"><_listComponent><SceneComponent _localPosition=\"" + pPosition +
                                    "\" _localRotation=\"0,0,0\" _localScale=\"1,1,1\"" + attachText + " /></_listComponent></GameObject>";
                return node;
            }
        };
    } // namespace
} // namespace sw

/**
 * @brief [WorldStreamingTest] 월드 설정은 XML · 바이너리로 왕복한다 — 바이너리는 셀까지 싣는다
 */
SW_TEST_CASE( WorldStreamingTest, WorldSettingsRoundTripThroughXMLAndBinary )
{
    using Internal = sw::WorldStreamingTestInternal;
    sw::SceneDocument doc{};
    doc._name          = "StreamTown";
    doc._worldSettings = Internal::makeStreamingSettings();
    doc._listSceneObjectNode.push_back( Internal::makeEntity( 1, "Sun", "0,0,0", 0, true ) );

    const sw::string xmlPath = test::makeTempPath( "stream_town.scene.xml" );
    SW_ASSERT_TRUE( doc.saveXML( xmlPath ) );
    sw::SceneDocument readXML{};
    SW_ASSERT_TRUE( readXML.loadXML( ::test::makeTempResourceID( xmlPath ) ) );
    SW_EXPECT_TRUE( readXML._worldSettings._bStreaming );
    SW_EXPECT_EQUAL( 10.0f, readXML._worldSettings._cellGrid._cellSize );
    SW_EXPECT_EQUAL( 5.0f, readXML._worldSettings._cellGrid._loadingRadius );
    SW_EXPECT_EQUAL( 2.0f, readXML._worldSettings._unloadMargin );
    SW_EXPECT_EQUAL( 4u, readXML._worldSettings._maxCellLoadsPerFrame );

    doc._listStreamingCell.push_back( sw::SceneDocument::StreamingCell{
        sw::WorldCellCoord{ -1, 2 },
        {}
    } );
    doc._listStreamingCell.back()._listSceneObjectNode.push_back( Internal::makeEntity( 2, "Tree", "-5,0,25" ) );
    const sw::string binPath = test::makeTempPath( "stream_town.scene.bin" );
    SW_ASSERT_TRUE( doc.saveBinary( binPath ) );
    sw::SceneDocument readBinary{};
    SW_ASSERT_TRUE( readBinary.loadBinary( ::test::makeTempResourceID( binPath ) ) );
    SW_EXPECT_TRUE( readBinary._worldSettings._bStreaming );
    SW_EXPECT_EQUAL( 4u, readBinary._worldSettings._maxCellLoadsPerFrame );
    SW_ASSERT_EQUAL( size_t( 1 ), readBinary._listStreamingCell.size() );
    SW_EXPECT_TRUE( readBinary._listStreamingCell[0]._cell == ( sw::WorldCellCoord{ -1, 2 } ) );
    SW_ASSERT_EQUAL( size_t( 1 ), readBinary._listStreamingCell[0]._listSceneObjectNode.size() );
    SW_EXPECT_EQUAL( uint64( 2 ), readBinary._listStreamingCell[0]._listSceneObjectNode[0]._fileID );
    SW_EXPECT_EQUAL( size_t( 2 ), readBinary.computeTotalEntityCount() );

    // 셀로 나눈 문서는 XML 로 쓰지 않는다 — 셀의 엔티티가 사라진다.
    SW_TEST_SUPPRESS_LOGS();
    SW_EXPECT_FALSE( readBinary.saveXML( test::makeTempPath( "stream_town_cells.scene.xml" ) ) );
}

/**
 * @brief [WorldStreamingTest] 월드 설정이 없는 씬 · 옛 판 씬 · 격자 없이 스트리밍하는 씬은 읽지 않는다 — 기본값으로 메우지 않는다
 */
SW_TEST_CASE( WorldStreamingTest, SceneWithoutUsableWorldSettingsIsRejected )
{
    SW_TEST_SUPPRESS_LOGS();
    const sw::string kEntities  = "<entities><entity id=\"1\" name=\"A\"/></entities></Scene>\n";
    const sw::string noSettings = "<Scene formatVersion=\"2\" name=\"NoSettings\">" + kEntities;
    const sw::string oldFormat  = "<Scene formatVersion=\"1\" name=\"Old\">" + kEntities;
    const sw::string noGrid     = "<Scene formatVersion=\"2\" name=\"NoGrid\"><WorldSettings _bStreaming=\"true\" _unloadMargin=\"0\" _maxCellLoadsPerFrame=\"1\">"
                                  "<_cellGrid _cellSize=\"0\" _loadingRadius=\"10\"/></WorldSettings>" +
                              kEntities;
    const sw::string noSettingsPath = test::makeTempPath( "no_settings.scene.xml" );
    const sw::string oldFormatPath  = test::makeTempPath( "old_format.scene.xml" );
    const sw::string noGridPath     = test::makeTempPath( "no_grid.scene.xml" );
    SW_ASSERT_TRUE( sw::FileUtil::writeTextFile( noSettingsPath, noSettings ) );
    SW_ASSERT_TRUE( sw::FileUtil::writeTextFile( oldFormatPath, oldFormat ) );
    SW_ASSERT_TRUE( sw::FileUtil::writeTextFile( noGridPath, noGrid ) );

    sw::SceneDocument doc{};
    SW_EXPECT_FALSE( doc.loadXML( ::test::makeTempResourceID( noSettingsPath ) ) );
    SW_EXPECT_FALSE( doc.loadXML( ::test::makeTempResourceID( oldFormatPath ) ) );
    SW_EXPECT_FALSE( doc.loadXML( ::test::makeTempResourceID( noGridPath ) ) );
}

/**
 * @brief [WorldStreamingTest] 쿠커는 엔티티를 루트의 자리로 셀에 나누고, 늘 올라 있는 엔티티와 자식은 제자리에 둔다
 */
SW_TEST_CASE( WorldStreamingTest, CookerSplitsEntitiesIntoCells )
{
    using Internal = sw::WorldStreamingTestInternal;
    sw::SceneManager manager;
    SW_ASSERT_TRUE( manager.initialize() );

    sw::SceneDocument doc{};
    doc._name          = "SplitTown";
    doc._worldSettings = Internal::makeStreamingSettings();
    doc._listSceneObjectNode.push_back( Internal::makeEntity( 1, "Sky", "500,0,500", 0, true ) );
    doc._listSceneObjectNode.push_back( Internal::makeEntity( 2, "HouseA", "1,0,1" ) );
    doc._listSceneObjectNode.push_back( Internal::makeEntity( 3, "Door", "9,0,0", 2 ) ); // 붙은 자식 — (1,0) 이 아니라 부모의 셀 (0,0)
    doc._listSceneObjectNode.push_back( Internal::makeEntity( 4, "HouseB", "25,0,-3" ) );

    SW_ASSERT_TRUE( sw::SceneCooker::splitStreamingCells( manager.getEngineServices(), doc, "SplitTown" ) );
    SW_ASSERT_EQUAL( size_t( 1 ), doc._listSceneObjectNode.size() );
    SW_EXPECT_EQUAL( uint64( 1 ), doc._listSceneObjectNode[0]._fileID );
    SW_ASSERT_EQUAL( size_t( 2 ), doc._listStreamingCell.size() );
    SW_EXPECT_TRUE( doc._listStreamingCell[0]._cell == ( sw::WorldCellCoord{ 2, -1 } ) ); // z 오름차순
    SW_EXPECT_TRUE( doc._listStreamingCell[1]._cell == ( sw::WorldCellCoord{ 0, 0 } ) );
    SW_EXPECT_EQUAL( size_t( 2 ), doc._listStreamingCell[1]._listSceneObjectNode.size() );
    manager.shutdown();
}

/**
 * @brief [WorldStreamingTest] 셀이 엇갈려 섞인 엔티티도 셀마다 한 번만 만들고, 셀은 z · x 오름차순, 셀 안은 문서 순서다
 * @details 셀 키→인덱스 맵으로 바꾼 묶기가 옛 선형 탐색과 같은 결과인지 고정한다(음수 좌표 포함).
 */
SW_TEST_CASE( WorldStreamingTest, CookerGroupsInterleavedEntitiesPerCellInDocumentOrder )
{
    using Internal = sw::WorldStreamingTestInternal;
    sw::SceneManager manager;
    SW_ASSERT_TRUE( manager.initialize() );

    sw::SceneDocument doc{};
    doc._name          = "InterleavedTown";
    doc._worldSettings = Internal::makeStreamingSettings();
    doc._listSceneObjectNode.push_back( Internal::makeEntity( 1, "A", "1,0,1" ) );   // (0,0)
    doc._listSceneObjectNode.push_back( Internal::makeEntity( 2, "B", "25,0,-3" ) ); // (2,-1)
    doc._listSceneObjectNode.push_back( Internal::makeEntity( 3, "C", "2,0,2" ) );   // (0,0)
    doc._listSceneObjectNode.push_back( Internal::makeEntity( 4, "D", "-5,0,25" ) ); // (-1,2)
    doc._listSceneObjectNode.push_back( Internal::makeEntity( 5, "E", "26,0,-4" ) ); // (2,-1)
    doc._listSceneObjectNode.push_back( Internal::makeEntity( 6, "F", "-5,0,1" ) );  // (-1,0)
    doc._listSceneObjectNode.push_back( Internal::makeEntity( 7, "G", "3,0,3" ) );   // (0,0)

    SW_ASSERT_TRUE( sw::SceneCooker::splitStreamingCells( manager.getEngineServices(), doc, "InterleavedTown" ) );
    SW_EXPECT_TRUE( doc._listSceneObjectNode.empty() );
    SW_ASSERT_EQUAL( size_t( 4 ), doc._listStreamingCell.size() );

    struct ExpectedCell
    {
        sw::WorldCellCoord _cell;
        sw::vector<uint64> _listFileID;
    };
    const ExpectedCell arrExpected[] = {
        {sw::WorldCellCoord{ 2, -1 },    { 2, 5 }},
        {sw::WorldCellCoord{ -1, 0 },       { 6 }},
        { sw::WorldCellCoord{ 0, 0 }, { 1, 3, 7 }},
        {sw::WorldCellCoord{ -1, 2 },       { 4 }},
    };
    for ( size_t cellIndex = 0; cellIndex < 4; ++cellIndex )
    {
        const sw::SceneDocument::StreamingCell& cell = doc._listStreamingCell[cellIndex];
        SW_EXPECT_TRUE( cell._cell == arrExpected[cellIndex]._cell );
        SW_ASSERT_EQUAL( arrExpected[cellIndex]._listFileID.size(), cell._listSceneObjectNode.size() );
        for ( size_t nodeIndex = 0; nodeIndex < cell._listSceneObjectNode.size(); ++nodeIndex )
        {
            SW_EXPECT_EQUAL( arrExpected[cellIndex]._listFileID[nodeIndex], cell._listSceneObjectNode[nodeIndex]._fileID );
        }
    }
    manager.shutdown();
}

/**
 * @brief [WorldStreamingTest] 다른 셀의 엔티티를 가리키면 쿠킹이 멈춘다 — 대상을 늘 올라 있게 하면 통과한다
 */
SW_TEST_CASE( WorldStreamingTest, CrossCellReferenceFailsTheCook )
{
    using Internal = sw::WorldStreamingTestInternal;
    sw::SceneManager manager;
    SW_ASSERT_TRUE( manager.initialize() );

    // Lamp(셀 (5,0) 자리)가 셀 (0,0) 의 Post 에 붙는다. 붙은 자식은 부모 셀을 따르므로 붙음 자체는 셀 안의 참조다 —
    // 위반을 만들려고 Lamp 를 늘 올라 있게 표시한다(늘 올라 있는 쪽이 셀의 오브젝트를 가리키면 셀이 내려갈 때 끊긴다).
    sw::SceneDocument doc{};
    doc._name          = "CrossTown";
    doc._worldSettings = Internal::makeStreamingSettings();
    doc._listSceneObjectNode.push_back( Internal::makeEntity( 1, "Post", "1,0,1" ) );
    doc._listSceneObjectNode.push_back( Internal::makeEntity( 2, "Lamp", "55,0,1", 1, true ) );
    {
        SW_TEST_DEFENSIVE_SCOPE( "a cross-cell reference" );
        SW_EXPECT_FALSE( sw::SceneCooker::splitStreamingCells( manager.getEngineServices(), doc, "CrossTown" ) );
    }
    SW_EXPECT_TRUE( doc._listStreamingCell.empty() ); // 실패하면 문서는 그대로다
    SW_EXPECT_EQUAL( size_t( 2 ), doc._listSceneObjectNode.size() );
    manager.shutdown();
}

/**
 * @brief [WorldStreamingTest] 열린 씬의 셀 미리보기는 쿠커와 같게 나누고 결과를 그 씬의 오브젝트 id 로 돌려준다 — 위반도 오브젝트로 짚는다
 * @details 에디터 World Cells 창과 `world.cells` 가 이 결과를 읽는다(`SceneCooker::previewStreamingCells`).
 */
SW_TEST_CASE( WorldStreamingTest, CellPreviewPointsAtSceneObjects )
{
    using Internal = sw::WorldStreamingTestInternal;
    sw::SceneManager manager;
    SW_ASSERT_TRUE( manager.initialize() );

    sw::SceneDocument splitDoc{};
    splitDoc._name          = "PreviewTown";
    splitDoc._worldSettings = Internal::makeStreamingSettings();
    splitDoc._listSceneObjectNode.push_back( Internal::makeEntity( 1, "Sky", "500,0,500", 0, true ) );
    splitDoc._listSceneObjectNode.push_back( Internal::makeEntity( 2, "HouseA", "1,0,1" ) );
    splitDoc._listSceneObjectNode.push_back( Internal::makeEntity( 4, "HouseB", "25,0,-3" ) );
    sw::Scene* pSplitScene = manager.createScene( "PreviewTown" );
    SW_ASSERT_TRUE( pSplitScene->instantiate( splitDoc ) );
    const sw::GameObject* pSky    = pSplitScene->getObjectManager()->findGameObjectByName( sw::hashed_string( "Sky" ) );
    const sw::GameObject* pHouseA = pSplitScene->getObjectManager()->findGameObjectByName( sw::hashed_string( "HouseA" ) );
    SW_ASSERT_NOT_NULL( pSky );
    SW_ASSERT_NOT_NULL( pHouseA );

    sw::StreamingCellPreview preview{};
    SW_ASSERT_TRUE( sw::SceneCooker::previewStreamingCells( *pSplitScene, preview ) );
    SW_EXPECT_TRUE( preview._listViolation.empty() );
    SW_ASSERT_EQUAL( size_t( 1 ), preview._listAlwaysLoadedID.size() );
    SW_EXPECT_EQUAL( pSky->getObjectID(), preview._listAlwaysLoadedID[0] );
    SW_ASSERT_EQUAL( size_t( 2 ), preview._listCell.size() );
    SW_EXPECT_TRUE( preview._listCell[1]._cell == ( sw::WorldCellCoord{ 0, 0 } ) );
    SW_ASSERT_EQUAL( size_t( 1 ), preview._listCell[1]._listObjectID.size() );
    SW_EXPECT_EQUAL( pHouseA->getObjectID(), preview._listCell[1]._listObjectID[0] );

    // 늘 올라 있는 Lamp 가 셀 (0,0) 의 Post 에 붙는다 — 쿠킹이 실패할 참조를 두 오브젝트로 짚는다.
    sw::SceneDocument crossDoc{};
    crossDoc._name          = "PreviewCross";
    crossDoc._worldSettings = Internal::makeStreamingSettings();
    crossDoc._listSceneObjectNode.push_back( Internal::makeEntity( 1, "Post", "1,0,1" ) );
    crossDoc._listSceneObjectNode.push_back( Internal::makeEntity( 2, "Lamp", "55,0,1", 1, true ) );
    sw::Scene* pCrossScene = manager.createScene( "PreviewCross" );
    SW_ASSERT_TRUE( pCrossScene->instantiate( crossDoc ) );
    const sw::GameObject* pPost = pCrossScene->getObjectManager()->findGameObjectByName( sw::hashed_string( "Post" ) );
    const sw::GameObject* pLamp = pCrossScene->getObjectManager()->findGameObjectByName( sw::hashed_string( "Lamp" ) );
    SW_ASSERT_NOT_NULL( pPost );
    SW_ASSERT_NOT_NULL( pLamp );
    {
        SW_TEST_DEFENSIVE_SCOPE( "a cross-cell reference" );
        SW_ASSERT_TRUE( sw::SceneCooker::previewStreamingCells( *pCrossScene, preview ) );
    }
    SW_EXPECT_TRUE( preview._listCell.empty() );
    SW_ASSERT_EQUAL( size_t( 1 ), preview._listViolation.size() );
    SW_EXPECT_EQUAL( pLamp->getObjectID(), preview._listViolation[0]._fromObjectID );
    SW_EXPECT_EQUAL( pPost->getObjectID(), preview._listViolation[0]._toObjectID );
    manager.shutdown();
}

/**
 * @brief [WorldStreamingTest] 원점 둘레의 셀만 올리고, 원점이 멀어지면 여유 반경을 넘은 셀을 내린다 — 바깥 원점 여럿은 합집합이다
 */
SW_TEST_CASE( WorldStreamingTest, CellsFollowStreamingSources )
{
    using Internal = sw::WorldStreamingTestInternal;
    sw::SceneManager manager;
    SW_ASSERT_TRUE( manager.initialize() );

    sw::SceneDocument doc{};
    doc._name          = "LiveTown";
    doc._worldSettings = Internal::makeStreamingSettings();
    doc._listSceneObjectNode.push_back( Internal::makeEntity( 1, "Sky", "0,0,0", 0, true ) );
    doc._listStreamingCell.push_back( sw::SceneDocument::StreamingCell{
        sw::WorldCellCoord{ 0, 0 },
        {}
    } );
    doc._listStreamingCell.back()._listSceneObjectNode.push_back( Internal::makeEntity( 2, "Near", "5,0,5" ) );
    doc._listStreamingCell.push_back( sw::SceneDocument::StreamingCell{
        sw::WorldCellCoord{ 10, 0 },
        {}
    } );
    doc._listStreamingCell.back()._listSceneObjectNode.push_back( Internal::makeEntity( 3, "Far", "105,0,5" ) );

    sw::Scene* pScene = manager.createScene( "LiveTown" );
    SW_ASSERT_TRUE( pScene->instantiate( doc ) );
    sw::GameObjectManager&       objects   = *pScene->getObjectManager();
    sw::WorldStreamingSubsystem& streaming = objects.getSubsystem<sw::WorldStreamingSubsystem>();
    SW_ASSERT_TRUE( streaming.isStreaming() );
    SW_EXPECT_EQUAL( 2u, streaming.getCellCount() );
    SW_EXPECT_EQUAL( 0u, streaming.getLoadedCellCount() );
    SW_EXPECT_NOT_NULL( objects.findGameObjectByName( sw::hashed_string( "Sky" ) ) );

    const sw::float3 nearSource( 5.0f, 0.0f, 5.0f );
    streaming.setExternalSources( sw::span<const sw::float3>( &nearSource, 1 ) );
    objects.update( 0.0f );
    SW_EXPECT_TRUE( streaming.isCellLoaded( sw::WorldCellCoord{ 0, 0 } ) );
    SW_EXPECT_FALSE( streaming.isCellLoaded( sw::WorldCellCoord{ 10, 0 } ) );
    SW_EXPECT_NOT_NULL( objects.findGameObjectByName( sw::hashed_string( "Near" ) ) );

    // 두 원점(접속자 둘)이면 두 셀 모두 올라 있다.
    const sw::float3 listTwoSource[] = { sw::float3( 5.0f, 0.0f, 5.0f ), sw::float3( 105.0f, 0.0f, 5.0f ) };
    streaming.setExternalSources( sw::span<const sw::float3>( listTwoSource, 2 ) );
    objects.update( 0.0f );
    SW_EXPECT_EQUAL( 2u, streaming.getLoadedCellCount() );

    // 반경(5) + 여유(2) 안이면 내리지 않고, 그 밖이면 내린다.
    const sw::float3 slightlyAway( 16.0f, 0.0f, 5.0f ); // 셀 (0,0) 까지 6 m
    streaming.setExternalSources( sw::span<const sw::float3>( &slightlyAway, 1 ) );
    objects.update( 0.0f );
    SW_EXPECT_TRUE( streaming.isCellLoaded( sw::WorldCellCoord{ 0, 0 } ) );
    SW_EXPECT_FALSE( streaming.isCellLoaded( sw::WorldCellCoord{ 10, 0 } ) );
    const sw::float3 farAway( 60.0f, 0.0f, 5.0f );
    streaming.setExternalSources( sw::span<const sw::float3>( &farAway, 1 ) );
    objects.update( 0.0f );
    objects.update( 0.0f ); // 지운 오브젝트는 다음 프레임에 놓인다
    SW_EXPECT_EQUAL( 0u, streaming.getLoadedCellCount() );
    SW_EXPECT_NULL( objects.findGameObjectByName( sw::hashed_string( "Near" ) ) );
    SW_EXPECT_NOT_NULL( objects.findGameObjectByName( sw::hashed_string( "Sky" ) ) );

    // 쿠킹본에서 지은 씬은 문서로 쓰지 않는다 — 내려간 셀의 엔티티가 빠진다.
    sw::SceneDocument saved{};
    {
        SW_TEST_DEFENSIVE_SCOPE( "saving a streaming scene" );
        SW_EXPECT_FALSE( pScene->serializeToDocument( saved ) );
    }
    manager.shutdown();
}
