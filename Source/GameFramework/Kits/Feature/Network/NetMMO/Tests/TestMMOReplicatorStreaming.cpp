#include "pch.h"

#include "Core/Container/unordered_map.h"
#include "Core/Network/Connection/NetEndpoint.h"
#include "Core/Network/Tests/LoopbackCluster.h"
#include "Core/Serialization/BitStream.h"
#include "Core/Testing/TestFramework.h"

#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManager.h"
#include "Engine/Scene/Scene.h"
#include "Engine/Scene/SceneDocument.h"
#include "Engine/Scene/SceneStreamingSubsystem.h"
#include "Engine/Serialization/Fixed/ReplicatedLayout.h"

#include "GameFramework/Kits/Feature/Network/NetMMO/MMOReplicator.h"

#include <limits>

// 스트리밍 월드의 MMO 복제(docs/agents/Gameplay.md "스트리밍 · 네트워크") — 클라이언트 보류 큐 · 정적 엔티티 묶기 · 관찰자 소스 합집합을 손 메시지로 보고,
// 루프백 끝단 위의 서버 · 두 클라이언트(씬 셋)로 서로 다른 셀 · 경계 넘기 · 늦게 오르는 셀을 끝까지 돌린다.

using namespace sw;

namespace
{
    struct NetMMOStreamingTestInternal
    {
        static constexpr float32 kTimeoutSeconds = 5.0f;
        static constexpr float32 kTickSeconds    = 1.0f / 20.0f;

        /** @brief 셀 상태와 셀 오브젝트를 테스트가 정하는 클라이언트 월드입니다. */
        class FakeClientWorld final : public INetClientWorld
        {
        public:
            unordered_map<uint64, uint64> _mapStaticObject; ///< 파일 id → 런타임 id
            uint8                         _bStreamed : 1;   ///< 자리의 셀이 올랐다

            FakeClientWorld()
                : _mapStaticObject{}
                , _bStreamed{ SW_FALSE }
            {
            }

            bool isPositionStreamed( const float3& position ) const override
            {
                (void)position;
                return _bStreamed != SW_FALSE;
            }
            uint64 findStaticObject( uint64 staticID ) const override
            {
                const auto objectIter = _mapStaticObject.find( staticID );
                return objectIter != _mapStaticObject.end() ? objectIter->second : 0;
            }
        };

        static MMOClientViewConfig makeViewConfig()
        {
            MMOClientViewConfig settings;
            settings._cellHoldTimeoutSeconds = kTimeoutSeconds;
            return settings;
        }

        /** @brief 새로 보임 하나(엔티티 하나)를 씁니다 — 머리는 보내는 쪽과 같은 칸 선언(`MMOEntityEnterHeader`)이다. */
        static vector<uint8> writeEnter( uint32 tick, uint32 entityID, uint64 staticID, const float3& position )
        {
            BitWriter writer;
            writer.writeBits( NetMMOMessage::kEnter, 8 );
            writer.writeVarUint( tick );
            writer.writeBool( true );
            MMOEntityEnterHeader header;
            header._header._entityID = entityID;
            header._header._position = position;
            header._typeID           = 1;
            header._staticID         = staticID;
            const bool bWritten      = ReplicatedLayout::writeFull( header, writer );
            SW_EXPECT_TRUE( bWritten );
            writer.writeVarUint( 0 ); // 상태 길이
            writer.writeBool( false );
            return writer.getBytes();
        }

        /** @brief 갱신 하나(엔티티 하나)를 씁니다 — 머리는 `MMOEntityUpdateHeader` 다. */
        static vector<uint8> writeUpdate( uint32 tick, uint32 entityID, const float3& position )
        {
            BitWriter writer;
            writer.writeBits( NetMMOMessage::kUpdate, 8 );
            writer.writeVarUint( tick );
            writer.writeBool( true );
            MMOEntityUpdateHeader header;
            header._entityID    = entityID;
            header._position    = position;
            const bool bWritten = ReplicatedLayout::writeFull( header, writer );
            SW_EXPECT_TRUE( bWritten );
            writer.writeVarUint( 0 ); // 상태 길이
            writer.writeBool( false );
            return writer.getBytes();
        }

        static vector<uint8> writeLeave( uint32 entityID )
        {
            BitWriter writer;
            writer.writeBits( NetMMOMessage::kLeave, 8 );
            writer.writeVarUint( 1 );
            writer.writeVarUint( entityID );
            return writer.getBytes();
        }

        static int32 countEvents( const vector<MMOClientEvent>& listEvent, MMOClientEvent::Kind kind, uint32 entityID )
        {
            int32 count = 0;
            for ( const MMOClientEvent& event : listEvent )
            {
                count += event._kind == kind && event._entityID == entityID ? 1 : 0;
            }
            return count;
        }

        /** @brief @p entityID 의 첫 Bound 가 묶은 셀 오브젝트입니다. 없으면 0 입니다. */
        static uint64 findBoundObject( const vector<MMOClientEvent>& listEvent, uint32 entityID )
        {
            for ( const MMOClientEvent& event : listEvent )
            {
                if ( event._kind == MMOClientEvent::Kind::Bound && event._entityID == entityID )
                    return event._staticObjectID;
            }
            return 0;
        }

        static int32 countKind( const vector<MMOClientEvent>& listEvent, MMOClientEvent::Kind kind )
        {
            int32 count = 0;
            for ( const MMOClientEvent& event : listEvent )
            {
                count += event._kind == kind ? 1 : 0;
            }
            return count;
        }
    };
    using Internal = NetMMOStreamingTestInternal;
} // namespace

/** @brief [NetMMOStreamingTest] 셀이 오르지 않은 자리의 새로 보임은 보류되고, 보류 중 갱신은 틱 규칙대로 들었다가 셀이 오르면 마지막 상태로 한 번 보인다 */
SW_TEST_CASE( NetMMOStreamingTest, ClientHoldsEnterUntilCellStreamed )
{
    Internal::FakeClientWorld world;
    MMOClientView             view;
    SW_REQUIRE_TRUE( view.setWorld( world, Internal::makeViewConfig() ) );

    SW_EXPECT_TRUE( NetHandleResult::Handled == view.handleMessage( 0, Internal::writeEnter( 7, 42, 0, float3{ 1.0f, 0.0f, 1.0f } ) ) );
    SW_EXPECT_EQUAL( 0, view.getEntityCount() );
    SW_EXPECT_EQUAL( 1, view.getHeldEntityCount() );
    // 보류 중 갱신: 새 틱은 받아 두고 오래된 틱은 버린다. 사건은 없다.
    (void)view.handleMessage( 0, Internal::writeUpdate( 9, 42, float3{ 3.0f, 0.0f, 3.0f } ) );
    (void)view.handleMessage( 0, Internal::writeUpdate( 8, 42, float3{ 2.0f, 0.0f, 2.0f } ) );
    SW_EXPECT_EQUAL( static_cast<uint64>( 1 ), view.getStaleUpdateCount() );
    view.update( Internal::kTickSeconds );
    vector<MMOClientEvent> listEvent;
    view.pollEvents( listEvent );
    SW_EXPECT_TRUE( listEvent.empty() );

    world._bStreamed = SW_TRUE;
    view.update( Internal::kTickSeconds );
    view.pollEvents( listEvent );
    SW_REQUIRE_EQUAL( static_cast<size_t>( 1 ), listEvent.size() );
    SW_EXPECT_TRUE( listEvent[0]._kind == MMOClientEvent::Kind::Entered && listEvent[0]._entityID == 42u );
    SW_EXPECT_EQUAL( 0, view.getHeldEntityCount() );
    const MMOEntity* pEntity = view.findEntity( 42 );
    SW_REQUIRE_TRUE( pEntity != nullptr );
    SW_EXPECT_EQUAL( 3.0f, pEntity->_position._x );

    // 셀이 이미 올라 있으면 보류 없이 곧바로 보인다. 보류 중 사라짐은 사건 없이 버린다.
    SW_EXPECT_TRUE( NetHandleResult::Handled == view.handleMessage( 0, Internal::writeEnter( 10, 43, 0, float3{} ) ) );
    SW_EXPECT_TRUE( view.findEntity( 43 ) != nullptr );
    world._bStreamed = SW_FALSE;
    (void)view.handleMessage( 0, Internal::writeEnter( 11, 44, 0, float3{} ) );
    (void)view.handleMessage( 0, Internal::writeLeave( 44 ) );
    listEvent.clear();
    view.pollEvents( listEvent );
    SW_EXPECT_EQUAL( 1, Internal::countEvents( listEvent, MMOClientEvent::Kind::Entered, 43 ) );
    SW_EXPECT_EQUAL( 0, Internal::countEvents( listEvent, MMOClientEvent::Kind::Entered, 44 ) );
    SW_EXPECT_EQUAL( 0, Internal::countEvents( listEvent, MMOClientEvent::Kind::Left, 44 ) );
    SW_EXPECT_EQUAL( 0, view.getHeldEntityCount() );
}

/** @brief [NetMMOStreamingTest] 셀이 끝내 오르지 않으면 시한 뒤 경고하고 동적 엔티티는 보이고 정적 엔티티는 사라짐이 올 때까지 계속 보류한다(경고는 한 번) — 시한 전에는 그대로 보류한다 */
SW_TEST_CASE( NetMMOStreamingTest, ClientHoldTimesOutWithWarning )
{
    SW_EXPECT_LOG( "MMOClientView.CellHoldTimedOut" );
    SW_EXPECT_LOG( "MMOClientView.StaticEntityUnresolved" );
    Internal::FakeClientWorld world;
    MMOClientView             view;
    SW_REQUIRE_TRUE( view.setWorld( world, Internal::makeViewConfig() ) );
    (void)view.handleMessage( 0, Internal::writeEnter( 1, 50, 0, float3{} ) );
    (void)view.handleMessage( 0, Internal::writeEnter( 1, 51, 9, float3{} ) );
    view.update( Internal::kTimeoutSeconds * 0.5f );
    SW_EXPECT_EQUAL( 2, view.getHeldEntityCount() );
    view.update( Internal::kTimeoutSeconds * 0.5f );
    view.update( Internal::kTimeoutSeconds ); // 시한을 이미 넘겼다 — 경고를 되풀이하지 않는다
    SW_EXPECT_EQUAL( 1, view.getHeldEntityCount() );
    SW_EXPECT_TRUE( view.findEntity( 50 ) != nullptr );
    SW_EXPECT_TRUE( view.findEntity( 51 ) == nullptr );
    vector<MMOClientEvent> listEvent;
    view.pollEvents( listEvent );
    SW_EXPECT_EQUAL( 1, Internal::countEvents( listEvent, MMOClientEvent::Kind::Entered, 50 ) );
    SW_EXPECT_EQUAL( 1, static_cast<int32>( listEvent.size() ) ); // 정적 엔티티는 스폰하지도 묶지도 않는다
    (void)view.handleMessage( 0, Internal::writeLeave( 51 ) );
    SW_EXPECT_EQUAL( 0, view.getHeldEntityCount() );
    SW_EXPECT_EQUAL( 1, view.getEntityCount() );
}

/** @brief [NetMMOStreamingTest] 정적 엔티티는 셀 오브젝트가 드러날 때 Bound 로 묶이고 사라지면 Unbound 다 — Entered 가 없어 게임이 스폰할 길이 없다. 월드 없이 오면 오류를 남기고 보류한다 */
SW_TEST_CASE( NetMMOStreamingTest, StaticEntityBindsToCellObjectWithoutSpawn )
{
    SW_EXPECT_LOG( "MMOClientView.StaticEntityWithoutWorld" );
    constexpr uint64          kFileID   = 77;
    constexpr uint64          kObjectID = 5000;
    Internal::FakeClientWorld world;
    world._bStreamed = SW_TRUE; // 바닥은 올랐지만 셀 오브젝트는 아직 드러나지 않았다
    MMOClientView view;
    SW_REQUIRE_TRUE( view.setWorld( world, Internal::makeViewConfig() ) );
    (void)view.handleMessage( 0, Internal::writeEnter( 1, 60, kFileID, float3{} ) );
    SW_EXPECT_EQUAL( 1, view.getHeldEntityCount() );
    world._mapStaticObject[kFileID] = kObjectID;
    view.update( Internal::kTickSeconds );
    (void)view.handleMessage( 0, Internal::writeUpdate( 2, 60, float3{ 1.0f, 0.0f, 0.0f } ) );
    (void)view.handleMessage( 0, Internal::writeLeave( 60 ) );

    vector<MMOClientEvent> listEvent;
    view.pollEvents( listEvent );
    SW_REQUIRE_EQUAL( static_cast<size_t>( 3 ), listEvent.size() );
    SW_EXPECT_TRUE( listEvent[0]._kind == MMOClientEvent::Kind::Bound && listEvent[0]._staticObjectID == kObjectID );
    SW_EXPECT_TRUE( listEvent[1]._kind == MMOClientEvent::Kind::Updated && listEvent[1]._staticObjectID == kObjectID );
    SW_EXPECT_TRUE( listEvent[2]._kind == MMOClientEvent::Kind::Unbound && listEvent[2]._staticObjectID == kObjectID );
    SW_EXPECT_EQUAL( 0, Internal::countKind( listEvent, MMOClientEvent::Kind::Entered ) );

    MMOClientView noWorld;
    SW_EXPECT_TRUE( NetHandleResult::Handled == noWorld.handleMessage( 0, Internal::writeEnter( 1, 61, kFileID, float3{} ) ) );
    SW_EXPECT_EQUAL( 0, noWorld.getEntityCount() );
    SW_EXPECT_EQUAL( 1, noWorld.getHeldEntityCount() );
}

/** @brief [NetMMOStreamingTest] 보류 시한이 없는 설정(기본값 0)은 거절된다 — 기본값으로 메우지 않는다. 월드를 떼면 보류한 동적 엔티티는 보이고 정적 엔티티는 계속 보류한다 */
SW_TEST_CASE( NetMMOStreamingTest, WorldRequiresTimeoutAndDetachReleasesDynamic )
{
    SW_EXPECT_LOG( "MMOClientView.RefusedWorldSettings" );
    Internal::FakeClientWorld world;
    MMOClientView             view;
    SW_EXPECT_FALSE( view.setWorld( world, MMOClientViewConfig{} ) );
    SW_REQUIRE_TRUE( view.setWorld( world, Internal::makeViewConfig() ) );
    (void)view.handleMessage( 0, Internal::writeEnter( 1, 70, 0, float3{} ) );
    (void)view.handleMessage( 0, Internal::writeEnter( 1, 71, 9, float3{} ) );
    SW_EXPECT_EQUAL( 2, view.getHeldEntityCount() );
    view.detachWorld();
    SW_EXPECT_EQUAL( 1, view.getHeldEntityCount() );
    SW_EXPECT_TRUE( view.findEntity( 70 ) != nullptr );
    SW_EXPECT_TRUE( view.findEntity( 71 ) == nullptr );
}

/**
 * @brief [NetMMOStreamingTest] 한 id 는 보류 · 보임 중 하나다 — 보이는 엔티티의 새로 보임이 셀이 안 오른 자리로 다시 와도 보류로 갈라지지 않고(상태만 새로),
 *        사라짐 한 번이 그 엔티티를 지우며 Entered · Left 는 한 번씩이다
 */
SW_TEST_CASE( NetMMOStreamingTest, ReenterOfVisibleEntityNeverSplitsOrDoublesEvents )
{
    Internal::FakeClientWorld world;
    MMOClientView             view;
    SW_REQUIRE_TRUE( view.setWorld( world, Internal::makeViewConfig() ) );
    (void)view.handleMessage( 0, Internal::writeEnter( 1, 80, 0, float3{} ) );
    world._bStreamed = SW_TRUE;
    view.update( Internal::kTickSeconds );
    SW_REQUIRE_TRUE( view.findEntity( 80 ) != nullptr );

    world._bStreamed = SW_FALSE; // 다시 온 새로 보임의 자리 셀은 오르지 않았다
    (void)view.handleMessage( 0, Internal::writeEnter( 2, 80, 0, float3{ 4.0f, 0.0f, 0.0f } ) );
    SW_EXPECT_EQUAL( 0, view.getHeldEntityCount() );
    SW_EXPECT_EQUAL( 1, view.getEntityCount() );
    SW_EXPECT_EQUAL( 4.0f, view.findEntity( 80 )->_position._x );
    (void)view.handleMessage( 0, Internal::writeLeave( 80 ) );
    world._bStreamed = SW_TRUE;
    view.update( Internal::kTickSeconds ); // 남은 보류가 없어 다시 보이지 않는다
    SW_EXPECT_EQUAL( 0, view.getEntityCount() );
    SW_EXPECT_EQUAL( 0, view.getHeldEntityCount() );

    vector<MMOClientEvent> listEvent;
    view.pollEvents( listEvent );
    SW_EXPECT_EQUAL( 1, Internal::countEvents( listEvent, MMOClientEvent::Kind::Entered, 80 ) );
    SW_EXPECT_EQUAL( 1, Internal::countEvents( listEvent, MMOClientEvent::Kind::Left, 80 ) );
}

/** @brief [NetMMOStreamingTest] 두 엔티티가 한 셀 오브젝트를 가리키면 둘째 묶기는 받지 않고(시한에 오류) 앞 엔티티가 풀린 뒤에야 묶는다 */
SW_TEST_CASE( NetMMOStreamingTest, SecondBindToSameCellObjectIsRefused )
{
    SW_EXPECT_LOG( "MMOClientView.StaticObjectAlreadyBound" );
    constexpr uint64          kFileID   = 77;
    constexpr uint64          kObjectID = 5000;
    Internal::FakeClientWorld world;
    world._mapStaticObject[kFileID] = kObjectID;
    MMOClientView view;
    SW_REQUIRE_TRUE( view.setWorld( world, Internal::makeViewConfig() ) );
    (void)view.handleMessage( 0, Internal::writeEnter( 1, 90, kFileID, float3{} ) );
    (void)view.handleMessage( 0, Internal::writeEnter( 1, 91, kFileID, float3{} ) );
    SW_EXPECT_TRUE( view.findEntity( 90 ) != nullptr );
    SW_EXPECT_EQUAL( 1, view.getHeldEntityCount() );
    view.update( Internal::kTimeoutSeconds );
    SW_EXPECT_TRUE( view.findEntity( 91 ) == nullptr );

    (void)view.handleMessage( 0, Internal::writeLeave( 90 ) );
    view.update( Internal::kTickSeconds );
    SW_EXPECT_TRUE( view.findEntity( 91 ) != nullptr );
    vector<MMOClientEvent> listEvent;
    view.pollEvents( listEvent );
    SW_EXPECT_EQUAL( 1, Internal::countEvents( listEvent, MMOClientEvent::Kind::Bound, 90 ) );
    SW_EXPECT_EQUAL( 1, Internal::countEvents( listEvent, MMOClientEvent::Kind::Unbound, 90 ) );
    SW_EXPECT_EQUAL( 1, Internal::countEvents( listEvent, MMOClientEvent::Kind::Bound, 91 ) );
}

/** @brief [NetMMOStreamingTest] 관찰자 소스: 기본 반경은 격자 로딩 반경, 나가는 반경보다 작거나 유한하지 않은 반경 · 없는 관찰자는 거절된다 */
SW_TEST_CASE( NetMMOStreamingTest, ObserverStreamingSourceIsValidated )
{
    SW_EXPECT_LOG( "MMOReplicator.ObserverStreamingRadius" );
    SW_EXPECT_LOG( "MMOReplicator.ObserverStreamingTooLarge" );
    SW_EXPECT_LOG( "MMOReplicator.ObserverStreamingUnknown" );
    NetEndpointConfig settings;
    settings._sendInterval   = 1.0 / 20.0;
    settings._saltSeed       = 11;
    settings._maxConnections = 4;
    test::LoopbackCluster cluster( 3u );
    SW_REQUIRE_TRUE( cluster.addEndpoint( 4200, settings ).has_value() );
    MMOReplicatorConfig mmoSettings;
    mmoSettings._enterRadius = 15.0f;
    mmoSettings._leaveRadius = 20.0f;
    // `_maxStreamingRadius` 는 아래에서 준다 — 주기 전(0)에는 어떤 반경도 받지 않는다.
    StreamingCellGridDef cellGridDef;
    cellGridDef._cellSize      = 20.0f;
    cellGridDef._loadingRadius = 25.0f;
    StreamingCellGrid cellGrid;
    SW_REQUIRE_TRUE( StreamingCellGrid::tryCreate( cellGridDef, cellGrid ) );
    MMOReplicator server;
    SW_REQUIRE_TRUE( server.initialize( &cluster.getServer(), mmoSettings, cellGrid ) );
    MMOEntity avatar;
    avatar._entityID = 1;
    avatar._position = float3{ 5.0f, 0.0f, 10.0f };
    server.setEntity( avatar );
    avatar._entityID = 2;
    avatar._position = float3{ 150.0f, 0.0f, 10.0f };
    server.setEntity( avatar );
    server.setObserver( 0, 1 );
    server.setObserver( 1, 2 );

    SW_EXPECT_FALSE( server.setObserverStreaming( 1, 200.0f, StreamingSourcePriority::High ) ); // 상한을 주지 않았다
    mmoSettings._maxStreamingRadius = 300.0f;
    SW_REQUIRE_TRUE( server.initialize( &cluster.getServer(), mmoSettings, cellGrid ) );
    server.setEntity( avatar );
    avatar._entityID = 1;
    avatar._position = float3{ 5.0f, 0.0f, 10.0f };
    server.setEntity( avatar );
    server.setObserver( 0, 1 );
    server.setObserver( 1, 2 );
    SW_EXPECT_TRUE( server.setObserverStreaming( 1, 200.0f, StreamingSourcePriority::High ) );
    SW_EXPECT_FALSE( server.setObserverStreaming( 0, 301.0f, StreamingSourcePriority::Highest ) );                                   // 상한 300 을 넘는다
    SW_EXPECT_FALSE( server.setObserverStreaming( 0, 19.0f, StreamingSourcePriority::Highest ) );                                    // 나가는 반경 20 보다 작다
    SW_EXPECT_FALSE( server.setObserverStreaming( 0, std::numeric_limits<float32>::infinity(), StreamingSourcePriority::Highest ) ); // 유한하지 않다(상한에 걸린다)
    SW_EXPECT_FALSE( server.setObserverStreaming( 5, 30.0f, StreamingSourcePriority::Highest ) );                                    // 관찰자가 없다

    vector<StreamingSource> listSource;
    server.collectStreamingSources( listSource );
    SW_REQUIRE_EQUAL( static_cast<size_t>( 2 ), listSource.size() );
    SW_EXPECT_EQUAL( 25.0f, listSource[0]._loadingRadius );
    SW_EXPECT_TRUE( listSource[0]._priority == StreamingSourcePriority::Normal );
    SW_EXPECT_EQUAL( 5.0f, listSource[0]._position._x );
    SW_EXPECT_EQUAL( 200.0f, listSource[1]._loadingRadius );
    SW_EXPECT_TRUE( listSource[1]._priority == StreamingSourcePriority::High );
}

namespace
{
    /**
     * @brief 서버 씬 하나 · 클라이언트 씬 둘 · 루프백 끝단 셋입니다. 문서는 셀 (0,0) ~ (9,0)(한 변 20 m, 로딩 반경 25 m, 여유 0)에 상자 하나씩(파일 id x+1, 셀 가운데 x*20+10, z 10).
     * @details 서버 게임 몫(`syncStaticEntities`): 서버에 드러난 상자를 정적 엔티티(id 101+x, 정적 id = 파일 id)로 올리고 내려간 것은 뺀다.
     */
    struct NetMMOStreamingRig
    {
        static constexpr int32  kCellCount   = 10;
        static constexpr int32  kClientCount = 2;
        static constexpr uint32 kAvatarA     = 1;
        static constexpr uint32 kAvatarB     = 2;
        static constexpr uint32 kStaticBase  = 100; ///< 상자의 엔티티 id = 이것 + 파일 id

        static SceneDocument makeDocument()
        {
            SceneDocument doc{};
            doc._name                                        = "NetStreamed";
            doc._sceneSettings._bStreaming                   = true;
            doc._sceneSettings._cellGrid._cellSize           = 20.0f;
            doc._sceneSettings._cellGrid._loadingRadius      = 25.0f;
            doc._sceneSettings._unloadMargin                 = 0.0f;
            doc._sceneSettings._maxCellLoadsPerFrame         = 16;
            doc._sceneSettings._addToWorldBudgetMicroseconds = 1'000'000'000u;
            doc._bValid                                      = true;
            for ( int32 x = 0; x < kCellCount; ++x )
            {
                SceneDocument::StreamingCell cell{};
                cell._cell = StreamingCellCoord{ x, 0 };
                SceneDocument::SceneObjectNode node{};
                node._fileID = static_cast<uint64>( x + 1 );
                sw::StringBuilder<sw::constant::buffer::kMax32> name;
                name.appendFormat( "Crate%#", x );
                node._name = sw::string( name.view() );
                sw::StringBuilder<sw::constant::buffer::kMax256> xml;
                xml.appendFormat( "<GameObject _schemaVersion=\"0\" _name=\"%#\" _bActive=\"true\"><_listComponent><SceneComponent _localPosition=\"%#,0,10\" /></_listComponent></GameObject>",
                                  node._name, x * 20 + 10 );
                node._embeddedXML = sw::string( xml.view() );
                cell._listSceneObjectNode.push_back( std::move( node ) );
                doc._listStreamingCell.push_back( std::move( cell ) );
            }
            return doc;
        }

        static float3 getCratePosition( int32 x ) { return float3{ static_cast<float32>( x * 20 + 10 ), 0.0f, 10.0f }; }

        Scene                  _serverScene{ "NetServer" };
        Scene                  _clientSceneA{ "NetClientA" };
        Scene                  _clientSceneB{ "NetClientB" };
        Scene*                 _arrClientScene[kClientCount]{ &_clientSceneA, &_clientSceneB };
        test::LoopbackCluster  _cluster{ 13u };
        MMOReplicator          _server;
        MMOClientView          _arrView[kClientCount];
        vector<MMOClientEvent> _arrListEvent[kClientCount];
        float3                 _arrAvatar[kClientCount]{
            float3{  5.0f, 0.0f, 10.0f},
            float3{150.0f, 0.0f, 10.0f}
        };
        bool _arrClientStreams[kClientCount]{ true, true };

        [[nodiscard]] bool initialize()
        {
            NetEndpointConfig settings;
            settings._sendInterval   = 1.0 / 20.0;
            settings._saltSeed       = 17;
            settings._maxConnections = kClientCount + 2;
            if ( _cluster.addEndpoint( 4300, settings ).has_value() == false )
                return false;
            for ( int32 index = 0; index < kClientCount; ++index )
            {
                if ( _cluster.addEndpoint( static_cast<uint16>( 5300 + index ), settings ).has_value() == false )
                    return false;
            }
            if ( _cluster.connectClients( 30 ) == false || _serverScene.instantiate( makeDocument() ) == false )
                return false;
            for ( Scene* pScene : _arrClientScene )
            {
                if ( pScene->instantiate( makeDocument() ) == false )
                    return false;
            }
            MMOReplicatorConfig mmoSettings;
            mmoSettings._enterRadius = 15.0f;
            mmoSettings._leaveRadius = 20.0f;
            if ( _server.initializeWithStreaming( &_cluster.getServer(), mmoSettings, getStreaming( _serverScene ) ) == false )
                return false;
            _server.setObserver( 0, kAvatarA );
            _server.setObserver( 1, kAvatarB );
            return true;
        }

        static SceneStreamingSubsystem& getStreaming( Scene& scene ) { return scene.getObjectManager()->getSubsystem<SceneStreamingSubsystem>(); }

        /** @brief 셀 x 가 올라 있는지 비트로 모읍니다(비트 x). */
        static uint32 collectLoadedMask( Scene& scene )
        {
            uint32 mask = 0;
            for ( int32 x = 0; x < kCellCount; ++x )
            {
                mask |= getStreaming( scene ).isCellLoaded( StreamingCellCoord{ x, 0 } ) ? ( 1u << x ) : 0u;
            }
            return mask;
        }

        /** @brief 서버 게임 몫 — 서버에 드러난 상자는 정적 엔티티로, 내려간 상자는 뺀다. */
        void syncStaticEntities()
        {
            const MMOSceneClientWorld serverWorld( _serverScene ); // 파일 id → 오브젝트를 찾는 같은 규칙
            for ( int32 x = 0; x < kCellCount; ++x )
            {
                const uint64 fileID   = static_cast<uint64>( x + 1 );
                const uint32 entityID = kStaticBase + static_cast<uint32>( fileID );
                if ( serverWorld.findStaticObject( fileID ) == 0 )
                {
                    _server.removeEntity( entityID );
                    continue;
                }
                MMOEntity crate;
                crate._entityID = entityID;
                crate._typeID   = 3;
                crate._staticID = fileID;
                crate._position = getCratePosition( x );
                _server.setEntity( crate );
            }
        }

        /** @brief 서버 틱 → 망 → 클라이언트(받기 · 제 셀 스트리밍 · 보류 정리)를 @p tickCount 번 돕니다. */
        void runTicks( int32 tickCount )
        {
            vector<uint8>  buffer;
            int32          connectionID = -1;
            NetChannelKind channel      = NetChannelKind::Unreliable;
            for ( int32 tick = 0; tick < tickCount; ++tick )
            {
                for ( int32 index = 0; index < kClientCount; ++index )
                {
                    MMOEntity avatar;
                    avatar._entityID = index == 0 ? kAvatarA : kAvatarB;
                    avatar._typeID   = 1;
                    avatar._position = _arrAvatar[index];
                    _server.setEntity( avatar );
                }
                syncStaticEntities();
                _server.updateAndFeedStreaming( NetMMOStreamingTestInternal::kTickSeconds, getStreaming( _serverScene ) );
                getStreaming( _serverScene ).update( 0.0f );
                _cluster.step( NetMMOStreamingTestInternal::kTickSeconds );
                for ( int32 index = 0; index < kClientCount; ++index )
                {
                    while ( _cluster.getClient( index ).receiveMessage( connectionID, channel, buffer ) )
                    {
                        SW_EXPECT_TRUE( NetHandleResult::Malformed != _arrView[index].handleMessage( connectionID, buffer.data(), static_cast<int32>( buffer.size() ), channel ) );
                    }
                    if ( _arrClientStreams[index] )
                    {
                        // 클라이언트 원점 = 자기 아바타(카메라 자리, 격자 로딩 반경)
                        SceneStreamingSubsystem& clientStreaming = getStreaming( *_arrClientScene[index] );
                        const StreamingSource    clientSource{ _arrAvatar[index], clientStreaming.getLoadingRadius(), StreamingSourcePriority::Normal };
                        SW_EXPECT_TRUE( clientStreaming.setExternalSources( span<const StreamingSource>( &clientSource, 1 ) ) );
                        clientStreaming.update( 0.0f );
                    }
                    _arrView[index].update( NetMMOStreamingTestInternal::kTickSeconds );
                    _arrView[index].pollEvents( _arrListEvent[index] );
                }
            }
        }

        /** @brief 클라이언트 @p index 의 이벤트 가운데 Entered(게임이 스폰하는 것)가 동적 아바타뿐이고 아바타마다 한 번씩인지 봅니다. */
        int32 countStaticSpawns( int32 index ) const
        {
            int32 count = 0;
            for ( const MMOClientEvent& event : _arrListEvent[index] )
            {
                count += event._kind == MMOClientEvent::Kind::Entered && event._entityID > kStaticBase ? 1 : 0;
            }
            return count;
        }
    };
} // namespace

/**
 * @brief [NetMMOStreamingTest] 루프백 서버 · 두 클라이언트: 서버의 오른 셀 = 관찰자 반경 합집합, 다른 셀의 두 클라이언트는 서로 보이지 않고, 늦게 오르는 클라이언트 셀은 보류 뒤 보이며,
 *        경계를 넘어 다가오면 서로 보이고 셀 상자는 제 셀 오브젝트에 묶일 뿐 스폰되지 않는다
 */
SW_TEST_CASE( NetMMOStreamingTest, TwoClientsAcrossCellsReplicateOnlyRelevantWithoutDuplicateSpawn )
{
    using Rig = NetMMOStreamingRig;
    Rig rig;
    SW_REQUIRE_TRUE( rig.initialize() );
    const MMOSceneClientWorld worldA( rig._clientSceneA );
    const MMOSceneClientWorld worldB( rig._clientSceneB );
    SW_REQUIRE_TRUE( rig._arrView[0].setWorld( worldA, NetMMOStreamingTestInternal::makeViewConfig() ) );
    SW_REQUIRE_TRUE( rig._arrView[1].setWorld( worldB, NetMMOStreamingTestInternal::makeViewConfig() ) );

    // 1) A 는 셀 0(x 5), B 는 셀 7(x 150). B 의 클라이언트 씬은 아직 스트리밍하지 않는다(셀이 늦게 오른다).
    rig._arrClientStreams[1] = false;
    rig.runTicks( 10 );
    // 서버: A 둘레 25 m = 셀 0 · 1, B 둘레 = 셀 6 · 7 · 8 — 합집합만 올랐다.
    SW_EXPECT_EQUAL( ( 1u << 0 ) | ( 1u << 1 ) | ( 1u << 6 ) | ( 1u << 7 ) | ( 1u << 8 ), Rig::collectLoadedMask( rig._serverScene ) );
    SW_EXPECT_EQUAL( 2, rig._server.getVisibleCount( 0 ) ); // 자기 + 셀 0 상자
    SW_EXPECT_EQUAL( 2, rig._server.getVisibleCount( 1 ) ); // 자기 + 셀 7 상자
    SW_EXPECT_TRUE( rig._arrView[0].findEntity( Rig::kAvatarA ) != nullptr );
    SW_EXPECT_TRUE( rig._arrView[0].findEntity( Rig::kAvatarB ) == nullptr ); // 다른 셀 — 보이지 않는다
    const MMOEntity* pCrate0 = rig._arrView[0].findEntity( Rig::kStaticBase + 1 );
    SW_REQUIRE_TRUE( pCrate0 != nullptr );
    SW_EXPECT_EQUAL( 1, NetMMOStreamingTestInternal::countEvents( rig._arrListEvent[0], MMOClientEvent::Kind::Bound, Rig::kStaticBase + 1 ) );
    // 묶인 것은 클라이언트 A 씬이 셀 0 에서 지은 그 상자다(새 오브젝트가 아니다).
    SW_EXPECT_TRUE( worldA.findStaticObject( 1 ) != 0 );
    SW_EXPECT_EQUAL( worldA.findStaticObject( 1 ), NetMMOStreamingTestInternal::findBoundObject( rig._arrListEvent[0], Rig::kStaticBase + 1 ) );
    // B 는 셀 7 이 오르지 않아 자기 아바타 · 셀 7 상자를 보류 중이다.
    SW_EXPECT_EQUAL( 0, rig._arrView[1].getEntityCount() );
    SW_EXPECT_EQUAL( 2, rig._arrView[1].getHeldEntityCount() );

    // 2) B 의 셀이 오르면 보류가 풀려 보인다.
    rig._arrClientStreams[1] = true;
    rig.runTicks( 3 );
    SW_EXPECT_EQUAL( 0, rig._arrView[1].getHeldEntityCount() );
    SW_EXPECT_TRUE( rig._arrView[1].findEntity( Rig::kAvatarB ) != nullptr );
    SW_EXPECT_TRUE( rig._arrView[1].findEntity( Rig::kAvatarA ) == nullptr );
    SW_EXPECT_EQUAL( 1, NetMMOStreamingTestInternal::countEvents( rig._arrListEvent[1], MMOClientEvent::Kind::Entered, Rig::kAvatarB ) );
    SW_EXPECT_EQUAL( 1, NetMMOStreamingTestInternal::countEvents( rig._arrListEvent[1], MMOClientEvent::Kind::Bound, Rig::kStaticBase + 8 ) );

    // 3) B 가 경계를 넘어 A 곁(x 14, 셀 0)으로 온다. 서버 셀은 A · B 둘레 합집합(셀 0 · 1)로 줄고, 셀 7 상자는 풀린다.
    rig._arrAvatar[1] = float3{ 14.0f, 0.0f, 10.0f };
    rig.runTicks( 10 );
    SW_EXPECT_EQUAL( ( 1u << 0 ) | ( 1u << 1 ), Rig::collectLoadedMask( rig._serverScene ) );
    SW_EXPECT_TRUE( rig._arrView[0].findEntity( Rig::kAvatarB ) != nullptr );
    SW_EXPECT_TRUE( rig._arrView[1].findEntity( Rig::kAvatarA ) != nullptr );
    SW_EXPECT_TRUE( rig._arrView[1].findEntity( Rig::kStaticBase + 8 ) == nullptr );
    SW_EXPECT_EQUAL( 1, NetMMOStreamingTestInternal::countEvents( rig._arrListEvent[1], MMOClientEvent::Kind::Unbound, Rig::kStaticBase + 8 ) );
    SW_EXPECT_EQUAL( 1, NetMMOStreamingTestInternal::countEvents( rig._arrListEvent[1], MMOClientEvent::Kind::Bound, Rig::kStaticBase + 1 ) );
    SW_EXPECT_EQUAL( worldB.findStaticObject( 1 ), NetMMOStreamingTestInternal::findBoundObject( rig._arrListEvent[1], Rig::kStaticBase + 1 ) );
    // 중복 스폰 0: 상자는 어느 클라이언트에서도 Entered 가 아니고, A 의 셀 0 상자는 B 가 와도 다시 묶이지 않으며, 아바타는 한 번씩만 들어왔다.
    for ( int32 index = 0; index < Rig::kClientCount; ++index )
    {
        SW_EXPECT_EQUAL( 0, rig.countStaticSpawns( index ) );
        SW_EXPECT_EQUAL( 1, NetMMOStreamingTestInternal::countEvents( rig._arrListEvent[index], MMOClientEvent::Kind::Entered, Rig::kAvatarA ) );
        SW_EXPECT_EQUAL( 1, NetMMOStreamingTestInternal::countEvents( rig._arrListEvent[index], MMOClientEvent::Kind::Entered, Rig::kAvatarB ) );
    }
    SW_EXPECT_EQUAL( 1, NetMMOStreamingTestInternal::countEvents( rig._arrListEvent[0], MMOClientEvent::Kind::Bound, Rig::kStaticBase + 1 ) );
}
