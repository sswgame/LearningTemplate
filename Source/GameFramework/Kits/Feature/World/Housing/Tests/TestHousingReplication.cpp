#include "pch.h"

#include "Core/Math/MathUtil.h"
#include "Core/Network/Connection/NetEndpoint.h"
#include "Core/Serialization/BitStream.h"
#include "Core/Testing/TestFramework.h"

#include "GameFramework/Kits/Feature/World/Housing/Catalog/HousingPart.h"
#include "GameFramework/Kits/Feature/World/Housing/Net/HousingReplication.h"
#include "GameFramework/Kits/Feature/World/Housing/Rule/HousingLayout.h"
#include "GameFramework/Runtime/Gameplay/Inventory/Shop.h"

// 하우징 서버 권위 배치 — 남의 부지 · 겹침 · 예산 · 거리 · 요청률 거절과 깨진 요청, 늦게 들어온 클라이언트가 같은 부지 상태를 받는지.

using namespace sw;

namespace
{
    /** @brief 보낸 메시지를 쌓기만 하는 상대입니다. 연결 목록은 테스트가 정합니다. */
    class FakeHousingPeers final : public INetPeers
    {
    public:
        struct Sent
        {
            vector<uint8> _bytes{};
            int32         _connectionID{ -1 };
        };

        vector<Sent>  _listSent;
        vector<int32> _listConnected;
        uint8         _bServer;

        explicit FakeHousingPeers( bool bServer )
            : _listSent{}
            , _listConnected{}
            , _bServer{ bServer ? SW_TRUE : SW_FALSE }
        {
        }

        bool sendMessage( int32 connectionID, NetChannelType channel, const uint8* pData, int32 size ) override { return trySendMessage( connectionID, channel, pData, size ); }
        bool trySendMessage( int32 connectionID, NetChannelType channel, const uint8* pData, int32 size ) override
        {
            (void)channel;
            _listSent.push_back( Sent{ vector<uint8>( pData, pData + size ), connectionID } );
            return true;
        }
        bool  sendNotifiedMessage( int32, const uint8*, int32, uint32 ) override { return false; }
        int32 broadcast( NetChannelType channel, const uint8* pData, int32 size, int32 exceptID ) override
        {
            int32 count = 0;
            for ( const int32 connectionID : _listConnected )
            {
                if ( connectionID != exceptID && trySendMessage( connectionID, channel, pData, size ) )
                    ++count;
            }
            return count;
        }
        void               disconnect( int32 ) override {}
        void               pollInbound( NetInbound& ) override {}
        bool               isServer() const override { return _bServer != SW_FALSE; }
        NetConnectionState getConnectionState( int32 ) const override { return NetConnectionState::Connected; }
        bool               getConnectionStats( int32, NetConnectionStats& ) const override { return false; }
        NetAddress         getConnectionAddress( int32 ) const override { return NetAddress{}; }
        uint64             getConnectionPrincipal( int32 ) const override { return 0; }
        optional<int32>    getClientIndex() const override { return nullopt; }
        int32              getConnectedCount() const override { return static_cast<int32>( _listConnected.size() ); }
        void               collectConnected( vector<int32>& outListConnection ) const override { outListConnection = _listConnected; }
        int32              getMaxBytesPerSecond() const override { return 0; }
        bool               isConnectionSaturated( int32 ) const override { return false; }
        void               addSentKindStats( NetMessageKindStatsTable& ) const override {}
    };

    /** @brief 부지 하나(레이아웃 + 지갑)입니다. 레이아웃이 지갑 주소를 묶으므로 옮기지 않는다. */
    struct HousingNetTestPlotState
    {
        HousingLayout _layout;
        Wallet        _wallet;

        HousingNetTestPlotState( const HousingPlotSettings& settings, const HousingPartCatalog& catalog, int64 funds )
            : _layout{}
            , _wallet{}
        {
            _layout.initialize( settings, catalog );
            _layout.bindWallet( &_wallet );
            _wallet.setBalance( hashed_string( "Gold" ), funds );
        }

        HousingNetTestPlotState( const HousingNetTestPlotState& )            = delete;
        HousingNetTestPlotState& operator=( const HousingNetTestPlotState& ) = delete;

        HousingNetPlotRef makeNetPlot()
        {
            HousingNetPlotRef plot;
            plot._pLayout = &_layout;
            plot._pWallet = &_wallet;
            plot._origin  = float3{ 0.0f, 0.0f, 0.0f };
            return plot;
        }

        int64 getFunds() const { return _wallet.getBalance( hashed_string( "Gold" ) ); }
    };

    /** @brief 서버 하나(부지 "home")와 클라이언트 여럿을 가짜 상대로 잇습니다. 연결 번호는 클라이언트 순번입니다. */
    struct HousingNetRig
    {
        struct Client
        {
            FakeHousingPeers                    _peers{ false };
            HousingReplicationClient            _replication{};
            unique_ptr<HousingNetTestPlotState> _pPlot{};
            vector<HousingEditReply>            _listReply{};
        };

        HousingPartCatalog                  _catalog;
        HousingPlotSettings                 _settings;
        FakeHousingPeers                    _serverPeers;
        HousingReplicationServer            _server;
        unique_ptr<HousingNetTestPlotState> _pServerPlot;
        vector<unique_ptr<Client>>          _listClient;

        explicit HousingNetRig( int64 serverFunds, int32 maxRequestsPerTick = 16, float32 maxReach = 0.0f, const HousingCallerLocator& locator = HousingCallerLocator{} )
            : _catalog{}
            , _settings{}
            , _serverPeers{ true }
            , _server{}
            , _pServerPlot{}
            , _listClient{}
        {
            SW_REQUIRE_TRUE( _catalog.loadFromXMLText( R"(<HousingPartCatalog currency="Gold" cellSize="1" levelHeight="3" socketSnapRadius="0.6">
                <Part id="floor" name="Floor" prefab="game/empty/prefabs/housing/floor.prefab.xml" layer="Floor" height="0.1" cost="5" refundRatio="0.5"/>
                <Part id="wall" name="Wall" prefab="game/empty/prefabs/housing/wall.prefab.xml" layer="Structure" height="2.8" cost="10" snapTo="wallEnd">
                    <Socket kind="wallEnd" position="-0.5,0,0"/><Socket kind="wallEnd" position="0.5,0,0"/>
                </Part></HousingPartCatalog>)",
                                                       "test" ) );
            _settings._plotID     = hashed_string( "home" );
            _settings._sizeX      = 6;
            _settings._sizeZ      = 6;
            _settings._levelCount = 2;
            _settings._maxReach   = maxReach;
            _pServerPlot          = make_unique<HousingNetTestPlotState>( _settings, _catalog, serverFunds );
            HousingServerConfig config;
            config._maxRequestsPerTick = maxRequestsPerTick;
            config._locateCaller       = locator;
            SW_REQUIRE_TRUE( _server.initialize( &_serverPeers, config ) );
        }

        /** @brief 클라이언트를 붙입니다. 클라이언트 지갑은 서버와 다르게 크다 — 판정이 서버 지갑으로만 되는지 본다. */
        Client& connect()
        {
            _listClient.push_back( make_unique<Client>() );
            Client&     client       = *_listClient.back();
            const int32 connectionID = static_cast<int32>( _listClient.size() ) - 1;
            client._pPlot            = make_unique<HousingNetTestPlotState>( _settings, _catalog, 100000 );
            client._replication.initialize( &client._peers );
            _serverPeers._listConnected.push_back( connectionID );
            _server.onConnectionOpened( connectionID );
            return client;
        }

        static HousingEditRequest makePlace( const utf8* pPartID, float32 x, float32 z, int32 level = 0, float32 yawDegrees = 0.0f )
        {
            HousingEditRequest request;
            request._op                             = HousingEditOp::Place;
            request._candidate._partID              = hashed_string( pPartID );
            request._candidate._transform._position = float2{ x, z };
            request._candidate._transform._level    = level;
            request._candidate._transform._yaw      = yawDegrees * MathUtil::kDegreeToRadian;
            return request;
        }

        static HousingEditRequest makeOp( HousingEditOp op )
        {
            HousingEditRequest request;
            request._op = op;
            return request;
        }

        /** @brief 요청을 보냅니다. */
        void send( size_t clientIndex, const HousingEditRequest& request ) { SW_EXPECT_TRUE( _listClient[clientIndex]->_replication.sendEdit( _settings._plotID, request ) != 0 ); }

        /** @brief 서버 틱 하나: 클라이언트 → 서버를 나르고, 서버가 판정 · 방송하고, 서버 → 클라이언트를 나르고, 클라이언트가 적용합니다. */
        void tick()
        {
            for ( size_t index = 0; index < _listClient.size(); ++index )
            {
                vector<FakeHousingPeers::Sent> listSent = std::move( _listClient[index]->_peers._listSent );
                _listClient[index]->_peers._listSent.clear();
                for ( const FakeHousingPeers::Sent& sent : listSent )
                    (void)_server.handleMessage( static_cast<int32>( index ), sent._bytes.data(), static_cast<int32>( sent._bytes.size() ), NetChannelType::ReliableOrdered );
            }
            _server.update( vector<HousingNetPlotRef>{ _pServerPlot->makeNetPlot() } );
            vector<FakeHousingPeers::Sent> listOut = std::move( _serverPeers._listSent );
            _serverPeers._listSent.clear();
            for ( const FakeHousingPeers::Sent& sent : listOut )
            {
                Client& client = *_listClient[static_cast<size_t>( sent._connectionID )];
                SW_EXPECT_TRUE( client._replication.handleMessage( 0, sent._bytes.data(), static_cast<int32>( sent._bytes.size() ), NetChannelType::ReliableOrdered ) == NetHandleResult::Handled );
            }
            for ( unique_ptr<Client>& pClient : _listClient )
            {
                pClient->_listReply.clear();
                pClient->_replication.update( vector<HousingNetPlotRef>{ pClient->_pPlot->makeNetPlot() }, pClient->_listReply );
            }
        }

        /** @brief 클라이언트의 마지막 판정입니다(이번 틱에 하나 이상 받았어야 한다). */
        HousingPlaceResult lastReply( size_t clientIndex ) const
        {
            const vector<HousingEditReply>& listReply = _listClient[clientIndex]->_listReply;
            SW_EXPECT_FALSE( listReply.empty() );
            return listReply.empty() ? HousingPlaceResult::Pending : listReply.back()._result;
        }
    };
} // namespace

/**
 * @brief [HousingReplicationTest] 서버는 남의 부지 · 겹침 · 예산 초과 요청을 자기 레이아웃 · 지갑으로 거절한다 — 클라이언트의 큰 지갑은 판정에 들지 않는다
 */
SW_TEST_CASE( HousingReplicationTest, ServerRefusesOthersPlotOverlapAndBudget )
{
    HousingNetRig rig( 7 ); // 바닥(5) 하나만 살 수 있다
    rig.connect();
    rig.connect();

    // 주인 없는 부지의 편집은 거절, 차지는 먼저 온 쪽이 이긴다.
    rig.send( 1, HousingNetRig::makePlace( "floor", 0.5f, 0.5f ) );
    rig.tick();
    SW_EXPECT_TRUE( rig.lastReply( 1 ) == HousingPlaceResult::NotOwner );
    rig.send( 0, HousingNetRig::makeOp( HousingEditOp::Claim ) );
    rig.send( 1, HousingNetRig::makeOp( HousingEditOp::Claim ) );
    rig.tick();
    SW_EXPECT_TRUE( rig.lastReply( 0 ) == HousingPlaceResult::Ok );
    SW_EXPECT_TRUE( rig.lastReply( 1 ) == HousingPlaceResult::NotOwner );
    SW_EXPECT_EQUAL( 0, rig._server.findOwner( hashed_string( "home" ) ) );
    SW_EXPECT_TRUE( rig._listClient[0]->_replication.findOwner( hashed_string( "home" ) ) == HousingPlotOwner::Self );
    SW_EXPECT_TRUE( rig._listClient[1]->_replication.findOwner( hashed_string( "home" ) ) == HousingPlotOwner::Other );

    // 남의 부지: 클라이언트 1 의 놓기 · 되돌리기는 판정 전에 거절된다.
    rig.send( 1, HousingNetRig::makePlace( "floor", 0.5f, 0.5f ) );
    rig.send( 1, HousingNetRig::makeOp( HousingEditOp::Undo ) );
    rig.tick();
    SW_REQUIRE_EQUAL( size_t{ 2 }, rig._listClient[1]->_listReply.size() );
    SW_EXPECT_TRUE( rig._listClient[1]->_listReply[0]._result == HousingPlaceResult::NotOwner );
    SW_EXPECT_TRUE( rig._listClient[1]->_listReply[1]._result == HousingPlaceResult::NotOwner );
    SW_EXPECT_TRUE( rig._pServerPlot->_layout.getPlacements().empty() );

    // 주인의 놓기 — 서버가 스냅하고 값을 서버 지갑에서 받는다. 같은 칸은 겹침, 둘째 바닥은 예산 초과.
    rig.send( 0, HousingNetRig::makePlace( "floor", 0.4f, 0.6f ) );
    rig.send( 0, HousingNetRig::makePlace( "floor", 0.5f, 0.5f ) );
    rig.send( 0, HousingNetRig::makePlace( "floor", 1.5f, 0.5f ) );
    rig.tick();
    SW_REQUIRE_EQUAL( size_t{ 3 }, rig._listClient[0]->_listReply.size() );
    SW_EXPECT_TRUE( rig._listClient[0]->_listReply[0]._result == HousingPlaceResult::Ok );
    SW_EXPECT_EQUAL( 1u, rig._listClient[0]->_listReply[0]._placementID );
    SW_EXPECT_TRUE( rig._listClient[0]->_listReply[1]._result == HousingPlaceResult::Overlap );
    SW_EXPECT_TRUE( rig._listClient[0]->_listReply[2]._result == HousingPlaceResult::NotEnoughFunds );
    SW_EXPECT_EQUAL( int64{ 2 }, rig._pServerPlot->getFunds() );
    // 방송된 상태 — 두 클라이언트 모두 서버의 배치 · 잔액을 받는다(클라이언트의 큰 지갑은 서버 값으로 바뀐다).
    for ( const unique_ptr<HousingNetRig::Client>& pClient : rig._listClient )
    {
        SW_REQUIRE_EQUAL( size_t{ 1 }, pClient->_pPlot->_layout.getPlacements().size() );
        const HousingPlacement& placement = pClient->_pPlot->_layout.getPlacements()[0];
        SW_EXPECT_EQUAL( 1u, placement._placementID );
        SW_EXPECT_NEAR( 0.5f, placement._transform._position._x, 1.0e-5f );
        SW_EXPECT_NEAR( 0.5f, placement._transform._position._y, 1.0e-5f );
        SW_EXPECT_EQUAL( int64{ 2 }, pClient->_pPlot->getFunds() );
    }
    // 모르는 부지 · 모르는 부품.
    SW_EXPECT_TRUE( rig._listClient[0]->_replication.sendEdit( hashed_string( "elsewhere" ), HousingNetRig::makeOp( HousingEditOp::Undo ) ) != 0 );
    rig.send( 0, HousingNetRig::makePlace( "throne", 2.5f, 2.5f ) );
    rig.tick();
    SW_REQUIRE_EQUAL( size_t{ 1 }, rig._listClient[0]->_listReply.size() ); // 모르는 부지의 답은 클라이언트가 버린다(그 부지가 없다)
    SW_EXPECT_TRUE( rig._listClient[0]->_listReply[0]._result == HousingPlaceResult::UnknownPart );
    SW_EXPECT_EQUAL( uint64{ 2 }, rig._server.getStats()._appliedCount ); // 적용은 차지 하나 + 놓기 하나
}

/**
 * @brief [HousingReplicationTest] 거리 · 요청률 · 깨진 날 값 — 거리는 서버의 위치 조회로만 보고(없으면 거절), 틱당 상한을 넘은 요청은 판정하지 않으며, 범위 밖 날 값은 깨진 메시지다
 */
SW_TEST_CASE( HousingReplicationTest, ServerChecksReachRateAndRawValues )
{
    {
        // 요청자는 부지에서 멀다(100, 100) — 거리 3 m 를 넘는다.
        HousingNetRig rig( 100, 16, 3.0f, HousingCallerLocator::create( []( int32, float3& outPosition )
        {
            outPosition = float3{ 100.0f, 0.0f, 100.0f };
            return true;
        } ) );
        rig.connect();
        rig.send( 0, HousingNetRig::makeOp( HousingEditOp::Claim ) );
        rig.send( 0, HousingNetRig::makePlace( "floor", 0.5f, 0.5f ) );
        rig.tick();
        SW_REQUIRE_EQUAL( size_t{ 2 }, rig._listClient[0]->_listReply.size() );
        SW_EXPECT_TRUE( rig._listClient[0]->_listReply[1]._result == HousingPlaceResult::OutOfReach );
        SW_EXPECT_TRUE( rig._pServerPlot->_layout.getPlacements().empty() );
    }
    {
        // 거리 제한이 있는데 위치 조회가 없으면 거절하고 오류를 한 번 남긴다(클라이언트 자리로 대신하지 않는다).
        HousingNetRig rig( 100, 16, 3.0f );
        rig.connect();
        SW_EXPECT_LOG( "HousingReplication.NoCallerLocator" );
        rig.send( 0, HousingNetRig::makeOp( HousingEditOp::Claim ) );
        rig.send( 0, HousingNetRig::makePlace( "floor", 0.5f, 0.5f ) );
        rig.send( 0, HousingNetRig::makePlace( "floor", 1.5f, 0.5f ) );
        rig.tick();
        SW_REQUIRE_EQUAL( size_t{ 3 }, rig._listClient[0]->_listReply.size() );
        SW_EXPECT_TRUE( rig._listClient[0]->_listReply[1]._result == HousingPlaceResult::OutOfReach );
        SW_EXPECT_TRUE( rig._listClient[0]->_listReply[2]._result == HousingPlaceResult::OutOfReach );
    }
    {
        // 틱당 두 요청 — 셋째는 판정하지 않고 RateLimited.
        HousingNetRig rig( 100, 2 );
        rig.connect();
        rig.send( 0, HousingNetRig::makeOp( HousingEditOp::Claim ) );
        rig.send( 0, HousingNetRig::makePlace( "floor", 0.5f, 0.5f ) );
        rig.send( 0, HousingNetRig::makePlace( "floor", 1.5f, 0.5f ) );
        rig.tick();
        SW_REQUIRE_EQUAL( size_t{ 3 }, rig._listClient[0]->_listReply.size() );
        SW_EXPECT_TRUE( rig._listClient[0]->_listReply[0]._result == HousingPlaceResult::RateLimited ); // 바로 답했다 — 줄의 둘보다 먼저 온다
        SW_EXPECT_TRUE( rig._listClient[0]->_listReply[1]._result == HousingPlaceResult::Ok );
        SW_EXPECT_TRUE( rig._listClient[0]->_listReply[2]._result == HousingPlaceResult::Ok );
        SW_EXPECT_EQUAL( size_t{ 1 }, rig._pServerPlot->_layout.getPlacements().size() );
        // 다음 틱에는 다시 받는다.
        rig.send( 0, HousingNetRig::makePlace( "floor", 1.5f, 0.5f ) );
        rig.tick();
        SW_EXPECT_TRUE( rig.lastReply( 0 ) == HousingPlaceResult::Ok );
    }
    {
        // 범위 밖 자리 · 모르는 종류는 깨진 메시지 — 줄에 들지 않는다.
        HousingNetRig    rig( 100 );
        NetMessageWriter writer;
        BitWriter&       body = writer.begin( NetHousingMessage::kEdit );
        body.writeVarUint( 1 );
        body.writeVarUint( static_cast<uint64>( hashed_string( "home" ).getHash() ) );
        body.writeBits( static_cast<uint32>( HousingEditOp::Place ), 3 );
        body.writeVarUint( static_cast<uint64>( hashed_string( "floor" ).getHash() ) );
        body.writeFloat( 1.0e9f );
        body.writeFloat( 0.5f );
        body.writeVarInt( 0 );
        body.writeFloat( 0.0f );
        body.writeBool( false );
        SW_EXPECT_TRUE( rig._server.handleMessage( 0, writer.getBytes().data(), writer.getByteCount(), NetChannelType::ReliableOrdered ) == NetHandleResult::Malformed );
        BitWriter& badOp = writer.begin( NetHousingMessage::kEdit );
        badOp.writeVarUint( 2 );
        badOp.writeVarUint( static_cast<uint64>( hashed_string( "home" ).getHash() ) );
        badOp.writeBits( 7u, 3 );
        SW_EXPECT_TRUE( rig._server.handleMessage( 0, writer.getBytes().data(), writer.getByteCount(), NetChannelType::ReliableOrdered ) == NetHandleResult::Malformed );
        SW_EXPECT_EQUAL( uint64{ 0 }, rig._server.getStats()._editCount );
    }
}

/**
 * @brief [HousingReplicationTest] 늦게 들어온 클라이언트는 다음 틱에 같은 부지 상태(배치 id · 변환 · 다음 id · 잔액)를 받고, 주인이 떠나면 부지는 주인 없음으로 돌아간다
 */
SW_TEST_CASE( HousingReplicationTest, LateJoinerReceivesTheSamePlotState )
{
    HousingNetRig rig( 100 );
    rig.connect();
    rig.send( 0, HousingNetRig::makeOp( HousingEditOp::Claim ) );
    rig.send( 0, HousingNetRig::makePlace( "floor", 0.5f, 0.5f ) );
    rig.send( 0, HousingNetRig::makePlace( "floor", 1.5f, 0.5f ) );
    rig.send( 0, HousingNetRig::makePlace( "wall", 0.5f, 0.5f ) );
    rig.send( 0, HousingNetRig::makePlace( "floor", 2.5f, 0.5f ) );
    rig.tick();
    HousingEditRequest remove;
    remove._op          = HousingEditOp::Remove;
    remove._placementID = 4; // 마지막 바닥 — 다음 id 는 5 로 남는다
    rig.send( 0, remove );
    rig.tick();
    SW_EXPECT_TRUE( rig.lastReply( 0 ) == HousingPlaceResult::Ok );
    SW_REQUIRE_EQUAL( size_t{ 3 }, rig._pServerPlot->_layout.getPlacements().size() );

    // 늦은 참가 — 부지는 이번 틱에 바뀌지 않았어도 들어온 연결은 상태를 받는다.
    HousingNetRig::Client& late = rig.connect();
    SW_EXPECT_TRUE( late._pPlot->_layout.getPlacements().empty() );
    rig.tick();
    const HousingLayout& serverLayout = rig._pServerPlot->_layout;
    const HousingLayout& lateLayout   = late._pPlot->_layout;
    SW_REQUIRE_EQUAL( serverLayout.getPlacements().size(), lateLayout.getPlacements().size() );
    for ( size_t index = 0; index < serverLayout.getPlacements().size(); ++index )
    {
        const HousingPlacement& expected = serverLayout.getPlacements()[index];
        const HousingPlacement& actual   = lateLayout.getPlacements()[index];
        SW_EXPECT_EQUAL( expected._placementID, actual._placementID );
        SW_EXPECT_TRUE( expected._partID == actual._partID );
        SW_EXPECT_TRUE( expected._transform == actual._transform );
    }
    SW_EXPECT_EQUAL( serverLayout.getNextPlacementID(), lateLayout.getNextPlacementID() );
    SW_EXPECT_EQUAL( rig._pServerPlot->getFunds(), late._pPlot->getFunds() );
    SW_EXPECT_TRUE( late._replication.findOwner( hashed_string( "home" ) ) == HousingPlotOwner::Other );
    SW_EXPECT_TRUE( rig._listClient[0]->_listReply.empty() ); // 첫 클라이언트는 이번 틱에 새 답이 없다

    // 주인이 떠나면 주인 없음 — 늦게 들어온 쪽이 차지하고, 앞 주인의 편집은 되돌리지 못한다(이력을 비웠다).
    rig._serverPeers._listConnected = { 1 };
    rig._server.onConnectionClosed( 0, NetDisconnectReason::Timeout );
    rig.tick();
    SW_EXPECT_TRUE( late._replication.findOwner( hashed_string( "home" ) ) == HousingPlotOwner::None );
    rig.send( 1, HousingNetRig::makeOp( HousingEditOp::Claim ) );
    rig.send( 1, HousingNetRig::makeOp( HousingEditOp::Undo ) );
    rig.tick();
    SW_REQUIRE_EQUAL( size_t{ 2 }, late._listReply.size() );
    SW_EXPECT_TRUE( late._listReply[0]._result == HousingPlaceResult::Ok );
    SW_EXPECT_TRUE( late._listReply[1]._result == HousingPlaceResult::NothingToUndo );
    SW_EXPECT_TRUE( late._replication.findOwner( hashed_string( "home" ) ) == HousingPlotOwner::Self );
}
