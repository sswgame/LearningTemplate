// NavTileStreamingTest — 스트리밍 씬의 내비메시 타일 스트리밍(감사 A-13): 씬 셀과 같은 원점 · 반경으로 쿠킹본 타일을 올리고 내린다(Detour `addTile` · `removeTile`).
// 타일마다 그 영역의 입력 해시를 쿠킹 때와 견주어 다른 타일만 지금 기하로 다시 굽고, 셀이 오르내리면 그 구역 타일의 입력을 다시 잰다.
// 쿠킹본은 메모리로 주고(`SceneNavigation::setCookedAsset`), 씬 기하는 기하 소스(`INavGeometrySource`)가 낸다.
// 먼 경로(`NavStreamRoute`) — 오르지 않은 타일 너머 목적지로 출발해 타일이 오를 때마다 이어 가고, 내려간 구역은 대략 그래프로 가상 이동하며, 관찰자 둘(전용 서버 구성)에서도 닿는다.
#include "pch.h"

#include "Core/Delegate/Subscription.h"
#include "Core/Memory/Memory.h"
#include "Core/Name/hashed_string.h"
#include "Core/Testing/TestFramework.h"

#include "Engine/Navigation/Component/NavMeshAgentComponent.h"
#include "Engine/Navigation/INavMesh.h"
#include "Engine/Navigation/NavMeshAsset.h"
#include "Engine/Navigation/NavMeshGeometry.h"
#include "Engine/Navigation/NavMeshSettings.h"
#include "Engine/Navigation/Scene/SceneNavigation.h"
#include "Engine/Object/Component/SceneComponent.h"
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
            pBaked->collectClusterGraph( entry._clusterGraph ); // 쿠커(`SceneNavigation::makeCookedEntry`)와 같다
            for ( const NavTileData& tile : entry._listTile )
            {
                entry._listTileInputHash.push_back( bTileHashes ? geometry.computeRegionHash( pBaked->computeTileBakeRegion( tile._tileX, tile._tileZ ) ) : uint64{ 0 } );
            }
            shared_ptr<NavMeshAsset> pAsset = make_shared<NavMeshAsset>();
            pAsset->setEntry( entry );
            return pAsset;
        }

        static void setSource( SceneStreamingSubsystem& streaming, const float3& source ) { streaming.setExternalSources( span<const float3>( &source, 1 ) ); }

        /** @brief 고정 걸음(초)입니다. */
        static constexpr float32 kDeltaTime = 1.0f / 30.0f;

        /** @brief x [-5, 95] · z [-4, 4] 의 긴 땅입니다 — 9.6 m 타일 열한 개가 한 줄로 잇는다(타일마다 클러스터 하나). */
        static vector<BoxGeometrySource::Box> makeStrip()
        {
            return {
                BoxGeometrySource::Box{ float3{ 45.0f, 0.0f, 0.0f }, float3{ 50.0f, 0.5f, 4.0f } }
            };
        }

        /** @brief 이름 붙은 오브젝트에 씬 컴포넌트 · 내비메시 에이전트를 붙입니다(자리 @p position, 최고 속도 @p maxSpeed). */
        static NavMeshAgentComponent* spawnAgent( GameObjectManager& objects, const utf8* pName, const float3& position, float32 maxSpeed )
        {
            GameObject* pObject = objects.createGameObject( hashed_string{ pName } );
            if ( pObject == nullptr )
                return nullptr;
            SceneComponent* pScene = pObject->addComponent<SceneComponent>();
            pScene->setWorldPosition( position );
            NavMeshAgentComponent* pAgent = pObject->addComponent<NavMeshAgentComponent>();
            pAgent->setMaxSpeed( maxSpeed );
            return pAgent;
        }

        /** @brief 내비게이션 갱신 한 번(갱신 → 결과 쓰기 — 매니저의 `Navigation` · `FlushPostAnimation` 단계 차례)입니다. */
        static void step( SceneNavigation& navigation )
        {
            navigation.update( kDeltaTime );
            navigation.completeFrameJob();
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

namespace
{
    /** @brief 긴 땅 씬 하나 — 쿠킹본(대략 그래프 포함) · 기하 소스 · 마련한 내비메시입니다. 케이스마다 원점과 에이전트만 다르다. */
    struct NavStripWorld
    {
        NavMeshSettings               _settings  = Util::makeSettings();
        vector<Box>                   _listStrip = Util::makeStrip();
        uint32                        _tileCount = 0;
        shared_ptr<NavMeshAsset>      _pAsset    = Util::cook( _settings, _listStrip, true, _tileCount );
        Scene                         _scene{ "NavStream" };
        unique_ptr<BoxGeometrySource> _pGroundSource;
        Subscription                  _groundSubscription;

        /** @brief 씬을 세우고 쿠킹본으로 내비메시를 마련합니다. 실패하면 false 입니다. */
        bool prepare()
        {
            if ( _pAsset == nullptr || _scene.instantiate( Util::makeStreamingDocument() ) == false )
                return false;
            GameObjectManager& objects  = *_scene.getObjectManager();
            _pGroundSource              = make_unique<BoxGeometrySource>( objects.findGameObjectByName( hashed_string{ "Ground" } ), _listStrip );
            SceneNavigation& navigation = getNavigation();
            _groundSubscription         = navigation.registerGeometrySource( _pGroundSource.get() );
            navigation.setSettings( _settings );
            navigation.setCookedAsset( _pAsset );
            return navigation.ensureNavMesh( _settings._listAgentType[0]._name ) != nullptr && navigation.isLoadedFromCooked( _settings._listAgentType[0]._name );
        }

        GameObjectManager&       getObjects() { return *_scene.getObjectManager(); }
        SceneNavigation&         getNavigation() { return getObjects().getSubsystem<SceneNavigation>(); }
        SceneStreamingSubsystem& getStreaming() { return getObjects().getSubsystem<SceneStreamingSubsystem>(); }
    };
} // namespace

/** @brief [NavTileStreamingTest] 계층 경로 질의 — 목적지 타일이 내려가 있으면 세밀 경로는 앞선 타일 가장자리까지이고 그 너머는 대략 점(마지막은 목적지)이다. 보통 경로는 실패한다 */
SW_TEST_CASE( NavTileStreamingTest, StreamingPathReachesPastStreamedOutTiles )
{
    NavStripWorld world;
    SW_REQUIRE_TRUE( world.prepare() );
    SceneNavigation&    navigation = world.getNavigation();
    const hashed_string agentName  = world._settings._listAgentType[0]._name;
    Util::setSource( world.getStreaming(), float3{ 0.0f, 0.0f, 0.0f } );
    Util::step( navigation );

    const float3 start{ 0.0f, 0.5f, 0.0f };
    const float3 end{ 90.0f, 0.5f, 0.0f };
    NavPath      direct;
    SW_EXPECT_TRUE( navigation.findPath( agentName, start, end, direct ) == NavPathStatus::Failed ); // 목적지 타일이 없다
    NavPath        path;
    vector<float3> listCoarse;
    SW_REQUIRE_TRUE( navigation.findStreamingPath( agentName, start, end, path, listCoarse ) == NavPathStatus::Partial );
    SW_REQUIRE_TRUE( path._listPoint.empty() == false );
    SW_EXPECT_TRUE( path._listPoint.back()._x < 14.2f ); // 오른 타일(0 · 1) 안에서 끝난다
    SW_REQUIRE_TRUE( listCoarse.size() >= 2u );
    SW_EXPECT_NEAR( end._x, listCoarse.back()._x, 1.0e-4f );
    for ( size_t pointIndex = 1; pointIndex < listCoarse.size(); ++pointIndex )
    {
        SW_EXPECT_TRUE( listCoarse[pointIndex]._x > listCoarse[pointIndex - 1]._x ); // 한 줄 땅이라 앞으로만 간다
    }
}

/** @brief [NavTileStreamingTest] 원점이 에이전트를 따라가면(클라이언트 카메라) 오르지 않은 먼 목적지로도 출발하고, 타일이 오를 때마다 중간 목표를 이어 도착한다 */
SW_TEST_CASE( NavTileStreamingTest, FarDestinationIsReachedAsTilesStreamIn )
{
    NavStripWorld world;
    SW_REQUIRE_TRUE( world.prepare() );
    SceneNavigation&       navigation = world.getNavigation();
    NavMeshAgentComponent* pAgent     = Util::spawnAgent( world.getObjects(), "Walker", float3{ 0.0f, 0.5f, 0.0f }, 10.0f );
    SW_REQUIRE_NOT_NULL( pAgent );
    world.getObjects().beginPlay();
    Util::setSource( world.getStreaming(), float3{ 0.0f, 0.0f, 0.0f } );
    Util::step( navigation );

    pAgent->setDestination( float3{ 90.0f, 0.5f, 0.0f } );
    Util::step( navigation );
    SW_EXPECT_TRUE( pAgent->getMoveStatus() == NavMoveStatus::Moving ); // 먼 목적지라고 실패하지 않는다
    SW_EXPECT_TRUE( pAgent->isFollowingCoarseRoute() );
    for ( uint32 frame = 0; frame < 900 && pAgent->getMoveStatus() == NavMoveStatus::Moving; ++frame )
    {
        Util::setSource( world.getStreaming(), pAgent->getAgentPosition() );
        Util::step( navigation );
    }
    SW_EXPECT_TRUE( pAgent->getMoveStatus() == NavMoveStatus::Arrived );
    SW_EXPECT_TRUE( pAgent->getAgentPosition()._x > 88.0f );
    SW_EXPECT_TRUE( pAgent->getRouteReplanCount() >= 2u ); // 타일이 오를 때마다 이어 갔다
    SW_EXPECT_FALSE( pAgent->isFollowingCoarseRoute() );
}

/**
 * @brief [NavTileStreamingTest] 전용 서버 구성 — 원점이 관찰자 둘(x 0 · 45)뿐이면 그 사이 · 너머 타일은 내려가 있다. 에이전트는 내려간 구역을 대략 그래프로 가상 이동하고,
 *        둘째 관찰자 둘레의 오른 타일에서는 군중(세밀 이동)으로 돌아오며, 관찰자 밖 목적지에도 닿는다
 */
SW_TEST_CASE( NavTileStreamingTest, ServerObserversLeaveGapsTheAgentCrossesVirtually )
{
    NavStripWorld world;
    SW_REQUIRE_TRUE( world.prepare() );
    SceneNavigation&       navigation = world.getNavigation();
    NavMeshAgentComponent* pAgent     = Util::spawnAgent( world.getObjects(), "Courier", float3{ 0.0f, 0.5f, 0.0f }, 10.0f );
    SW_REQUIRE_NOT_NULL( pAgent );
    world.getObjects().beginPlay();
    const float3 arrObserver[2] = {
        float3{ 0.0f, 0.0f, 0.0f},
        float3{45.0f, 0.0f, 0.0f}
    };
    world.getStreaming().setExternalSources( span<const float3>( arrObserver, 2 ) );
    Util::step( navigation );

    pAgent->setDestination( float3{ 90.0f, 0.5f, 0.0f } );
    bool bSawVirtualMove  = false;
    bool bReturnedToCrowd = false;
    for ( uint32 frame = 0; frame < 900 && pAgent->getMoveStatus() != NavMoveStatus::Arrived && pAgent->getMoveStatus() != NavMoveStatus::Failed; ++frame )
    {
        Util::step( navigation );
        bSawVirtualMove = bSawVirtualMove || pAgent->isInVirtualMove();
        // 가상 이동 뒤 둘째 관찰자 둘레(타일 4 · 5 — x 33.4 ~ 52.6)에서 군중으로 걷는다.
        const float32 x  = pAgent->getAgentPosition()._x;
        bReturnedToCrowd = bReturnedToCrowd || ( bSawVirtualMove && pAgent->isInVirtualMove() == false && pAgent->isOnNavMesh() && x > 33.4f && x < 52.6f );
    }
    SW_EXPECT_TRUE( bSawVirtualMove );
    SW_EXPECT_TRUE( bReturnedToCrowd );
    SW_EXPECT_TRUE( pAgent->getMoveStatus() == NavMoveStatus::Arrived );
    SW_EXPECT_NEAR( 90.0f, pAgent->getAgentPosition()._x, 1.0f );
    SW_EXPECT_FALSE( pAgent->isInVirtualMove() );
    SW_EXPECT_TRUE( pAgent->getRouteReplanCount() >= 3u ); // 중간 목표 · 가상 이동 · 되돌아옴 · 다시 가상 이동
}

/** @brief [NavTileStreamingTest] 선 타일이 내려갔다 다시 오른 뒤에도 에이전트가 군중에 다시 붙어 목적지로 간다(Detour 는 `INVALID` 에이전트를 스스로 되살리지 않는다) */
SW_TEST_CASE( NavTileStreamingTest, AgentOnReloadedTileMovesAgain )
{
    NavStripWorld world;
    SW_REQUIRE_TRUE( world.prepare() );
    SceneNavigation&       navigation = world.getNavigation();
    NavMeshAgentComponent* pAgent     = Util::spawnAgent( world.getObjects(), "Sleeper", float3{ 0.0f, 0.5f, 0.0f }, 5.0f );
    SW_REQUIRE_NOT_NULL( pAgent );
    world.getObjects().beginPlay();
    Util::setSource( world.getStreaming(), float3{ 0.0f, 0.0f, 0.0f } );
    Util::step( navigation );
    Util::step( navigation );

    Util::setSource( world.getStreaming(), float3{ -500.0f, 0.0f, -500.0f } );
    Util::step( navigation );
    Util::step( navigation );
    Util::setSource( world.getStreaming(), float3{ 0.0f, 0.0f, 0.0f } );
    Util::step( navigation );

    pAgent->setDestination( float3{ 3.0f, 0.5f, 0.0f } );
    for ( uint32 frame = 0; frame < 120 && pAgent->getMoveStatus() == NavMoveStatus::Moving; ++frame )
    {
        Util::step( navigation );
    }
    SW_EXPECT_TRUE( pAgent->getMoveStatus() == NavMoveStatus::Arrived );
    SW_EXPECT_TRUE( pAgent->isOnNavMesh() );
}
