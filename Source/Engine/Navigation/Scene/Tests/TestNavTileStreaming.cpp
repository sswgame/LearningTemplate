// NavTileStreamingTest — 스트리밍 씬의 내비메시 타일 스트리밍(감사 A-13): 씬 셀과 같은 원점 · 반경으로 쿠킹본 타일을 올리고 내린다(Detour `addTile` · `removeTile`).
// 내린 구역의 경로 질의는 기존 "경로 없음" 으로 끝나고, 다시 올리면 원래 경로가 돌아온다. 쿠킹본은 메모리로 준다(`SceneNavigation::setCookedAsset`).
#include "pch.h"

#include "Core/Memory/Memory.h"
#include "Core/Name/hashed_string.h"
#include "Core/Testing/TestFramework.h"

#include "Engine/Navigation/INavMesh.h"
#include "Engine/Navigation/NavMeshAsset.h"
#include "Engine/Navigation/NavMeshGeometry.h"
#include "Engine/Navigation/NavMeshSettings.h"
#include "Engine/Navigation/Scene/SceneNavigation.h"
#include "Engine/Object/GameObject/GameObjectManager.inl"
#include "Engine/Scene/Scene.h"
#include "Engine/Scene/SceneDocument.h"
#include "Engine/Scene/SceneStreamingSubsystem.h"

using namespace sw;

namespace
{
    struct NavTileStreamingTestUtil
    {
        /** @brief 셀 한 변 10 m · 로딩 반경 6 m 인 스트리밍 문서입니다. 셀은 멀리 하나(엔티티 하나) — 내비메시 타일만 본다. */
        static SceneDocument makeStreamingDocument()
        {
            SceneDocument doc{};
            doc._name                                        = "NavStream";
            doc._sceneSettings._bStreaming                   = true;
            doc._sceneSettings._cellGrid._cellSize           = 10.0f;
            doc._sceneSettings._cellGrid._loadingRadius      = 6.0f;
            doc._sceneSettings._unloadMargin                 = 0.0f;
            doc._sceneSettings._maxCellLoadsPerFrame         = 1;
            doc._sceneSettings._addToWorldBudgetMicroseconds = 1000;
            doc._bValid                                      = true;
            SceneDocument::StreamingCell cell{};
            cell._cell = StreamingCellCoord{ 100, 100 };
            SceneDocument::SceneObjectNode node{};
            node._fileID      = 1;
            node._name        = "FarMarker";
            node._embeddedXML = "<GameObject _schemaVersion=\"0\" _name=\"FarMarker\" _bActive=\"true\"><_listComponent><SceneComponent _localPosition=\"1005,0,1005\" /></_listComponent></GameObject>";
            cell._listSceneObjectNode.push_back( node );
            doc._listStreamingCell.push_back( std::move( cell ) );
            return doc;
        }

        static void setSource( SceneStreamingSubsystem& streaming, const float3& source ) { streaming.setExternalSources( span<const float3>( &source, 1 ) ); }
    };
    using Util = NavTileStreamingTestUtil;
} // namespace

/** @brief [NavTileStreamingTest] 타일을 내리면 그 구역 경로 질의가 "경로 없음" 이 되고, 다시 올리면 원래 경로가 돌아온다 */
SW_TEST_CASE( NavTileStreamingTest, StreamedOutTilesFailPathsUntilStreamedIn )
{
    NavAgentTypeDef agentType;
    agentType._name = hashed_string{ "Walker" };
    NavAreaDef area;
    area._name = hashed_string{ "Walkable" };
    NavMeshSettings settings;
    settings._listAgentType.push_back( agentType );
    settings._listArea.push_back( area );
    settings._defaultAgentType = agentType._name;

    // 실제 백엔드로 상자 하나를 베이크해 쿠킹본 항목을 만든다(쿠커가 하는 일).
    NavMeshGeometry geometry;
    geometry.addBox( float3{ 8.0f, 0.5f, 8.0f }, float4x4{}, 0 );
    const AABB           bounds = NavMeshAsset::computeBakeBounds( geometry );
    unique_ptr<INavMesh> pBaked = NavMeshBackend::createNavMesh();
    SW_REQUIRE_NOT_NULL( pBaked );
    SW_REQUIRE_TRUE( pBaked->initialize( agentType, settings, bounds ) );
    SW_REQUIRE_TRUE( NavMeshAsset::bakeAllTiles( *pBaked, geometry ) );
    SW_REQUIRE_TRUE( pBaked->getLoadedTileCount() > 0u );
    NavMeshAssetEntry entry;
    entry._agentType    = agentType._name;
    entry._settingsHash = NavMeshSettings::computeAgentTypeHash( agentType );
    entry._bounds       = bounds;
    pBaked->collectTiles( entry._listTile );
    shared_ptr<NavMeshAsset> pAsset = make_shared<NavMeshAsset>();
    pAsset->setEntry( entry );

    Scene scene{ "NavStream" };
    SW_REQUIRE_TRUE( scene.instantiate( Util::makeStreamingDocument() ) );
    GameObjectManager&       objects    = *scene.getObjectManager();
    SceneStreamingSubsystem& streaming  = objects.getSubsystem<SceneStreamingSubsystem>();
    SceneNavigation&         navigation = objects.getSubsystem<SceneNavigation>();
    navigation.setSettings( settings );
    navigation.setCookedAsset( pAsset );
    const INavMeshTileStore* pNavMesh = navigation.ensureNavMesh( agentType._name );
    SW_REQUIRE_NOT_NULL( pNavMesh );
    SW_EXPECT_TRUE( navigation.isLoadedFromCooked( agentType._name ) );
    SW_EXPECT_EQUAL( 0u, pNavMesh->getLoadedTileCount() ); // 원점이 오기 전에는 타일이 없다

    const float3 start{ -5.0f, 0.5f, -5.0f };
    const float3 end{ 5.0f, 0.5f, 5.0f };
    Util::setSource( streaming, float3{ 0.0f, 0.0f, 0.0f } );
    navigation.update( 0.0f );
    SW_EXPECT_EQUAL( pBaked->getLoadedTileCount(), pNavMesh->getLoadedTileCount() );
    NavPath original;
    SW_REQUIRE_TRUE( navigation.findPath( agentType._name, start, end, original ) != NavPathStatus::Failed );

    Util::setSource( streaming, float3{ 500.0f, 0.0f, 500.0f } );
    navigation.update( 0.0f );
    SW_EXPECT_EQUAL( 0u, pNavMesh->getLoadedTileCount() );
    NavPath gone;
    SW_EXPECT_TRUE( navigation.findPath( agentType._name, start, end, gone ) == NavPathStatus::Failed ); // 내린 구역은 "경로 없음" 이다

    Util::setSource( streaming, float3{ 0.0f, 0.0f, 0.0f } );
    navigation.update( 0.0f );
    NavPath restored;
    SW_REQUIRE_TRUE( navigation.findPath( agentType._name, start, end, restored ) == original._status );
    SW_EXPECT_EQUAL( original._listPoint.size(), restored._listPoint.size() );
    SW_EXPECT_NEAR( original.computeLength(), restored.computeLength(), 1.0e-4f );
}
