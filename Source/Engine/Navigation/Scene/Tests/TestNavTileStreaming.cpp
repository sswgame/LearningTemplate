// NavTileStreamingTest — 스트리밍 씬의 내비메시 타일 스트리밍(감사 A-13): 씬 셀과 같은 원점 · 반경으로 쿠킹본 타일을 올리고 내린다(Detour `addTile` · `removeTile`).
// 타일마다 그 영역의 입력 해시를 쿠킹 때와 견주어 다른 타일만 지금 기하로 다시 굽고, 셀이 오르내리면 그 구역 타일의 입력을 다시 잰다.
// 쿠킹본은 메모리로 주고(`SceneNavigation::setCookedAsset`), 씬 기하는 기하 소스(`INavGeometrySource`)가 낸다.
#include "pch.h"

#include "Core/Delegate/Subscription.h"
#include "Core/Memory/Memory.h"
#include "Core/Name/hashed_string.h"
#include "Core/Testing/TestFramework.h"

#include "Engine/Navigation/INavMesh.h"
#include "Engine/Navigation/NavMeshAsset.h"
#include "Engine/Navigation/NavMeshGeometry.h"
#include "Engine/Navigation/NavMeshSettings.h"
#include "Engine/Navigation/Scene/SceneNavigation.h"
#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManager.inl"
#include "Engine/Scene/Scene.h"
#include "Engine/Scene/SceneDocument.h"
#include "Engine/Scene/SceneStreamingSubsystem.h"

using namespace sw;

namespace
{
    /** @brief 오브젝트 하나의 기하로 축 상자들을 냅니다. */
    class BoxGeometrySource final : public INavGeometrySource
    {
    public:
        struct Box
        {
            float3 _center{};
            float3 _halfExtents{};
        };

        BoxGeometrySource( const GameObject* pOwner, vector<Box> listBox )
            : _pOwner{ pOwner }
            , _listBox{ std::move( listBox ) }
        {
        }

        const GameObject* getNavGeometryOwner() const override { return _pOwner; }
        bool              shouldOverrideOwnerGeometry() const override { return true; }
        void              collectNavGeometry( NavMeshGeometry& outGeometry, uint8 area ) const override { addBoxes( _listBox, area, outGeometry ); }

        static void addBoxes( const vector<Box>& listBox, uint8 area, NavMeshGeometry& outGeometry )
        {
            for ( const Box& box : listBox )
            {
                outGeometry.addBox( box._halfExtents, float4x4::makeTranslation( box._center ), area );
            }
        }

    private:
        const GameObject* _pOwner;
        vector<Box>       _listBox;
    };

    struct NavTileStreamingTestUtil
    {
        /** @brief 씬 컴포넌트 하나짜리 엔티티입니다. */
        static SceneDocument::SceneObjectNode makeNode( uint64 fileID, const utf8* pName, const utf8* pPosition )
        {
            SceneDocument::SceneObjectNode node{};
            node._fileID      = fileID;
            node._name        = pName;
            node._embeddedXML = string( "<GameObject _schemaVersion=\"0\" _name=\"" ) + pName + "\" _bActive=\"true\"><_listComponent><SceneComponent _localPosition=\"" + pPosition +
                                "\" /></_listComponent></GameObject>";
            return node;
        }

        /** @brief 셀 한 변 10 m · 로딩 반경 6 m 인 스트리밍 문서입니다. 늘 올라 있는 `Ground`(원점)와 셀 (3,0) 의 `FarGround`(35,0,5)가 있다. */
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
            doc._listSceneObjectNode.push_back( makeNode( 1, "Ground", "0,0,0" ) );
            SceneDocument::StreamingCell cell{};
            cell._cell = StreamingCellCoord{ 3, 0 };
            cell._listSceneObjectNode.push_back( makeNode( 2, "FarGround", "35,0,5" ) );
            doc._listStreamingCell.push_back( std::move( cell ) );
            return doc;
        }

        static NavMeshSettings makeSettings()
        {
            NavAgentTypeDef agentType;
            agentType._name = hashed_string{ "Walker" };
            NavAreaDef area;
            area._name = hashed_string{ "Walkable" };
            NavMeshSettings settings;
            settings._listAgentType.push_back( agentType );
            settings._listArea.push_back( area );
            settings._defaultAgentType = agentType._name;
            return settings;
        }

        /**
         * @brief 상자들(@p listBox)을 실제 백엔드로 베이크해 쿠킹본을 만듭니다(쿠커가 하는 일). @p bTileHashes 면 타일마다 그 영역의 입력 해시를 적고, 아니면 "모름"(0)으로 둔다.
         */
        static shared_ptr<NavMeshAsset> cook( const NavMeshSettings& settings, const vector<BoxGeometrySource::Box>& listBox, bool bTileHashes, uint32& outTileCount )
        {
            const NavAgentTypeDef& agentType = settings._listAgentType[0];
            NavMeshGeometry        geometry;
            BoxGeometrySource::addBoxes( listBox, 0, geometry );
            const AABB           bounds = NavMeshAsset::computeBakeBounds( geometry );
            unique_ptr<INavMesh> pBaked = NavMeshBackend::createNavMesh();
            // 도우미는 값을 돌려주므로 SW_REQUIRE_*(케이스 본문 전용)를 쓰지 않는다 — 기대로 남기고 nullptr 로 알린다.
            SW_EXPECT_NOT_NULL( pBaked );
            if ( pBaked == nullptr )
                return nullptr;
            const bool bInitialized = pBaked->initialize( agentType, settings, bounds );
            SW_EXPECT_TRUE( bInitialized );
            const bool bBaked = bInitialized && NavMeshAsset::bakeAllTiles( *pBaked, geometry );
            SW_EXPECT_TRUE( bBaked );
            if ( bBaked == false )
                return nullptr;
            outTileCount = pBaked->getLoadedTileCount();
            NavMeshAssetEntry entry;
            entry._agentType    = agentType._name;
            entry._settingsHash = NavMeshSettings::computeAgentTypeHash( agentType );
            entry._bounds       = bounds;
            pBaked->collectTiles( entry._listTile );
            for ( const NavTileData& tile : entry._listTile )
            {
                entry._listTileInputHash.push_back( bTileHashes ? geometry.computeRegionHash( pBaked->computeTileBakeRegion( tile._tileX, tile._tileZ ) ) : uint64{ 0 } );
            }
            shared_ptr<NavMeshAsset> pAsset = make_shared<NavMeshAsset>();
            pAsset->setEntry( entry );
            return pAsset;
        }

        static void setSource( SceneStreamingSubsystem& streaming, const float3& position )
        {
            const StreamingSource source{ position, streaming.getLoadingRadius(), StreamingSourcePriority::Normal };
            SW_EXPECT_TRUE( streaming.setExternalSources( span<const StreamingSource>( &source, 1 ) ) );
        }
    };
    using Util = NavTileStreamingTestUtil;
    using Box  = BoxGeometrySource::Box;
} // namespace

/** @brief [NavTileStreamingTest] 타일을 내리면 그 구역 경로 질의가 "경로 없음" 이 되고, 다시 올리면 원래 경로가 돌아온다(입력이 쿠킹 때와 같아 쿠킹본 타일을 끼운다) */
SW_TEST_CASE( NavTileStreamingTest, StreamedOutTilesFailPathsUntilStreamedIn )
{
    const NavMeshSettings settings   = Util::makeSettings();
    const hashed_string   agentName  = settings._listAgentType[0]._name;
    const vector<Box>     listGround = {
        Box{ float3{ 0.0f, 0.0f, 0.0f }, float3{ 8.0f, 0.5f, 8.0f } }
    };
    uint32                   tileCount = 0;
    shared_ptr<NavMeshAsset> pAsset    = Util::cook( settings, listGround, true, tileCount );

    Scene scene{ "NavStream" };
    SW_REQUIRE_TRUE( scene.instantiate( Util::makeStreamingDocument() ) );
    GameObjectManager&       objects    = *scene.getObjectManager();
    SceneStreamingSubsystem& streaming  = objects.getSubsystem<SceneStreamingSubsystem>();
    SceneNavigation&         navigation = objects.getSubsystem<SceneNavigation>();
    BoxGeometrySource        groundSource{ objects.findGameObjectByName( hashed_string{ "Ground" } ), listGround };
    const Subscription       groundSubscription = navigation.registerGeometrySource( &groundSource );
    navigation.setSettings( settings );
    navigation.setCookedAsset( pAsset );
    const INavMeshTileStore* pNavMesh = navigation.ensureNavMesh( agentName );
    SW_REQUIRE_NOT_NULL( pNavMesh );
    SW_EXPECT_TRUE( navigation.isLoadedFromCooked( agentName ) );
    SW_EXPECT_EQUAL( 0u, pNavMesh->getLoadedTileCount() ); // 원점이 오기 전에는 타일이 없다

    const float3 start{ -5.0f, 0.5f, -5.0f };
    const float3 end{ 5.0f, 0.5f, 5.0f };
    Util::setSource( streaming, float3{ 0.0f, 0.0f, 0.0f } );
    navigation.update( 0.0f );
    SW_EXPECT_EQUAL( tileCount, pNavMesh->getLoadedTileCount() );
    SW_EXPECT_EQUAL( 0u, navigation.getPendingTileCount() ); // 입력이 쿠킹 때와 같다 — 다시 굽지 않는다
    NavPath original;
    SW_REQUIRE_TRUE( navigation.findPath( agentName, start, end, original ) != NavPathStatus::Failed );

    Util::setSource( streaming, float3{ -500.0f, 0.0f, -500.0f } );
    navigation.update( 0.0f );
    SW_EXPECT_EQUAL( 0u, pNavMesh->getLoadedTileCount() );
    NavPath gone;
    SW_EXPECT_TRUE( navigation.findPath( agentName, start, end, gone ) == NavPathStatus::Failed ); // 내린 구역은 "경로 없음" 이다

    Util::setSource( streaming, float3{ 0.0f, 0.0f, 0.0f } );
    navigation.update( 0.0f );
    NavPath restored;
    SW_REQUIRE_TRUE( navigation.findPath( agentName, start, end, restored ) == original._status );
    SW_EXPECT_EQUAL( original._listPoint.size(), restored._listPoint.size() );
    SW_EXPECT_NEAR( original.computeLength(), restored.computeLength(), 1.0e-4f );
}

/** @brief [NavTileStreamingTest] 타일 영역의 입력이 쿠킹 때와 다르면 그 타일만 다시 굽고, 이웃 타일은 쿠킹본 그대로 끼운다 */
SW_TEST_CASE( NavTileStreamingTest, TileWithChangedInputIsRebakedAlone )
{
    const NavMeshSettings    settings  = Util::makeSettings();
    const hashed_string      agentName = settings._listAgentType[0]._name;
    uint32                   tileCount = 0;
    shared_ptr<NavMeshAsset> pAsset    = Util::cook( settings, {
                                                                Box{ float3{ 0.0f, 0.0f, 0.0f }, float3{ 8.0f, 0.5f, 8.0f } }
    },
                                                     true, tileCount );
    SW_REQUIRE_EQUAL( 4u, tileCount ); // 9.6 m 타일 2 × 2 가 16 m 상자를 덮는다

    Scene scene{ "NavStream" };
    SW_REQUIRE_TRUE( scene.instantiate( Util::makeStreamingDocument() ) );
    GameObjectManager&       objects    = *scene.getObjectManager();
    SceneStreamingSubsystem& streaming  = objects.getSubsystem<SceneStreamingSubsystem>();
    SceneNavigation&         navigation = objects.getSubsystem<SceneNavigation>();
    // 런타임 기하는 쿠킹 뒤 타일 (1,1) 자리에 상자 하나가 더 놓였다 — 다른 세 타일의 영역(테두리 포함)에는 닿지 않는다.
    BoxGeometrySource groundSource{
        objects.findGameObjectByName( hashed_string{ "Ground" }
         ),
        { Box{ float3{ 0.0f, 0.0f, 0.0f }, float3{ 8.0f, 0.5f, 8.0f } }, Box{ float3{ 6.0f, 1.0f, 6.0f }, float3{ 0.5f, 0.5f, 0.5f } } }
    };
    const Subscription groundSubscription = navigation.registerGeometrySource( &groundSource );
    navigation.setSettings( settings );
    navigation.setCookedAsset( pAsset );
    const INavMeshTileStore* pNavMesh = navigation.ensureNavMesh( agentName );
    SW_REQUIRE_NOT_NULL( pNavMesh );

    Util::setSource( streaming, float3{ 0.0f, 0.0f, 0.0f } );
    navigation.update( 0.0f );
    SW_EXPECT_EQUAL( 3u, pNavMesh->getLoadedTileCount() );   // 셋은 쿠킹본 타일이다
    SW_EXPECT_EQUAL( 1u, navigation.getPendingTileCount() ); // 하나만 다시 굽는다
    navigation.flushTileBakes();
    SW_EXPECT_EQUAL( 4u, pNavMesh->getLoadedTileCount() );
    SW_EXPECT_EQUAL( 1u, navigation.getRebakedTileCount() );
}

/** @brief [NavTileStreamingTest] 셀이 오르면 그 구역 타일의 입력을 다시 재고, 다시 베이크가 셀의 기하가 든 지금 기하를 쓴다 */
SW_TEST_CASE( NavTileStreamingTest, LoadedCellRefreshesTileInputForRebake )
{
    const NavMeshSettings settings  = Util::makeSettings();
    const hashed_string   agentName = settings._listAgentType[0]._name;
    const vector<Box>     listFar   = {
        Box{ float3{ 35.0f, 0.0f, 5.0f }, float3{ 4.0f, 0.5f, 4.0f } }
    };
    uint32                   tileCount = 0;
    shared_ptr<NavMeshAsset> pAsset    = Util::cook( settings, listFar, false, tileCount ); // 타일 입력 해시를 모른다 — 늘 지금 기하로 굽는다

    Scene scene{ "NavStream" };
    SW_REQUIRE_TRUE( scene.instantiate( Util::makeStreamingDocument() ) );
    GameObjectManager&       objects    = *scene.getObjectManager();
    SceneStreamingSubsystem& streaming  = objects.getSubsystem<SceneStreamingSubsystem>();
    SceneNavigation&         navigation = objects.getSubsystem<SceneNavigation>();
    navigation.setSettings( settings );
    navigation.setCookedAsset( pAsset );
    const INavMeshTileStore* pNavMesh = navigation.ensureNavMesh( agentName );
    SW_REQUIRE_NOT_NULL( pNavMesh );

    const float3 start{ 32.0f, 0.5f, 2.0f };
    const float3 end{ 38.0f, 0.5f, 8.0f };
    Util::setSource( streaming, float3{ 35.0f, 0.0f, 5.0f } );
    navigation.update( 0.0f ); // 셀이 아직 오르지 않았다 — 지금 기하에는 먼 땅이 없다
    navigation.flushTileBakes();
    NavPath before;
    SW_EXPECT_TRUE( navigation.findPath( agentName, start, end, before ) == NavPathStatus::Failed );

    streaming.update( 0.0f ); // 셀 (3,0) 이 오른다
    GameObject* pFarGround = objects.findGameObjectByName( hashed_string{ "FarGround" } );
    SW_REQUIRE_NOT_NULL( pFarGround );
    BoxGeometrySource  farSource{ pFarGround, listFar };
    const Subscription farSubscription = navigation.registerGeometrySource( &farSource );
    navigation.update( 0.0f ); // 셀이 오른 것을 보고 기하를 다시 모아 그 구역 타일을 다시 정한다
    SW_EXPECT_TRUE( navigation.getPendingTileCount() > 0u );
    navigation.flushTileBakes();
    NavPath after;
    SW_EXPECT_TRUE( navigation.findPath( agentName, start, end, after ) != NavPathStatus::Failed );
}
