#include "pch.h"

#include "Core/Algorithm/VectorUtil.h"
#include "Core/Math/MathUtil.h"
#include "Core/Network/Connection/NetEndpoint.h"
#include "Core/Serialization/BitStream.h"
#include "Core/Testing/TestFramework.h"

#include "GameFramework/Kits/Feature/World/Housing/Catalog/HousingPart.h"
#include "GameFramework/Kits/Feature/World/Housing/Net/HousingReplication.h"
#include "GameFramework/Kits/Feature/World/Housing/Rule/HousingLayout.h"
#include "GameFramework/Runtime/Gameplay/Inventory/Shop.h"

// 하우징 서버 권위 배치 — 남의 부지 · 겹침 · 예산 · 거리 · 요청률 거절과 깨진 요청, 늦게 들어온 클라이언트가 같은 부지 상태를 받는지,
// 주인이 주체라 다시 붙어도 같은지, 되돌리기 · 다시 하기의 거리, 큰 부지의 조각 전송 · 델타.

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

        vector<Sent>   _listSent;
        vector<int32>  _listConnected;
        vector<uint64> _listPrincipal; ///< 연결 번호 자리의 주체(접속 인증기가 지정한 계정, 0 은 계정 없음)
        int32          _sendBudget;    ///< 남은 보내기 수. 음수면 무한이다(신뢰 창이 찬 것을 흉내)
        uint8          _bServer;

        explicit FakeHousingPeers( bool bServer )
            : _listSent{}
            , _listConnected{}
            , _listPrincipal{}
            , _sendBudget{ -1 }
            , _bServer{ static_cast<uint8>( bServer ? SW_TRUE : SW_FALSE ) }
        {
        }

        bool sendMessage( int32 connectionID, NetChannelKind channel, const uint8* pData, int32 size ) override { return trySendMessage( connectionID, channel, pData, size ); }
        bool trySendMessage( int32 connectionID, NetChannelKind channel, const uint8* pData, int32 size ) override
        {
            (void)channel;
            if ( _sendBudget == 0 )
                return false;
            if ( _sendBudget > 0 )
                --_sendBudget;
            _listSent.push_back( Sent{ vector<uint8>( pData, pData + size ), connectionID } );
            return true;
        }
        bool  sendNotifiedMessage( int32, const uint8*, int32, uint32 ) override { return false; }
        int32 broadcast( NetChannelKind channel, const uint8* pData, int32 size, int32 exceptID ) override
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
        uint64             getConnectionPrincipal( int32 connectionID ) const override
        {
            return connectionID >= 0 && static_cast<size_t>( connectionID ) < _listPrincipal.size() ? _listPrincipal[static_cast<size_t>( connectionID )] : 0;
        }
        optional<int32> getClientIndex() const override { return nullopt; }
        int32           getConnectedCount() const override { return static_cast<int32>( _listConnected.size() ); }
        void            collectConnected( vector<int32>& outListConnection ) const override { outListConnection = _listConnected; }
        bool            isConnectionSaturated( int32 ) const override { return false; }
        void            addSentKindStats( NetMessageKindStatsTable& ) const override {}
    };

    /** @brief 부지 하나(레이아웃 + 지갑)입니다. 레이아웃이 지갑 주소를 묶으므로 옮기지 않는다. */
    struct HousingNetTestPlotState
    {
        HousingLayout _layout;
        Wallet        _wallet;
        uint64        _owner;

        HousingNetTestPlotState( const HousingPlotSettings& settings, const HousingPartCatalog& catalog, int64 funds )
            : _layout{}
            , _wallet{}
            , _owner{ HousingOwnerID::kNone }
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
            plot._pOwner  = &_owner;
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

        explicit HousingNetRig( int64 serverFunds, int32 maxRequestsPerTick = 16, float32 maxReach = 0.0f, const HousingCallerLocator& locator = HousingCallerLocator{}, int32 plotSize = 6 )
            : _catalog{}
            , _settings{}
            , _serverPeers{ true }
            , _server{}
            , _pServerPlot{}
            , _listClient{}
        {
            SW_REQUIRE_TRUE( _catalog.loadFromXMLText( R"(<HousingPartCatalog currency="Gold" cellSize="1" levelHeight="3" socketSnapRadius="0.6">
                <Part id="floor" name="Floor" prefab="game/playground/prefabs/housing/floor.prefab.xml" layer="Floor" height="0.1" cost="5" refundRatio="0.5"/>
                <Part id="wall" name="Wall" prefab="game/playground/prefabs/housing/wall.prefab.xml" layer="Structure" height="2.8" cost="10" snapTo="wallEnd">
                    <Socket kind="wallEnd" position="-0.5,0,0"/><Socket kind="wallEnd" position="0.5,0,0"/>
                </Part></HousingPartCatalog>)",
                                                       "test" ) );
            _settings._plotID     = hashed_string( "home" );
            _settings._sizeX      = plotSize;
            _settings._sizeZ      = plotSize;
            _settings._levelCount = 2;
            _settings._maxReach   = maxReach;
            _pServerPlot          = make_unique<HousingNetTestPlotState>( _settings, _catalog, serverFunds );
            HousingServerConfig config;
            config._maxRequestsPerTick = maxRequestsPerTick;
            config._locateCaller       = locator;
            SW_REQUIRE_TRUE( _server.initialize( &_serverPeers, config ) );
        }

        /** @brief 클라이언트를 새 연결로 붙입니다(@p principal 은 그 연결의 계정, 0 은 계정 없음). 클라이언트 지갑은 서버와 다르게 크다 — 판정이 서버 지갑으로만 되는지 본다. */
        Client& connect( uint64 principal = 0 )
        {
            _listClient.push_back( make_unique<Client>() );
            Client&     client       = *_listClient.back();
            const int32 connectionID = static_cast<int32>( _listClient.size() ) - 1;
            _serverPeers._listPrincipal.resize( _listClient.size(), 0 );
            _serverPeers._listPrincipal.back() = principal;
            client._pPlot                      = make_unique<HousingNetTestPlotState>( _settings, _catalog, 100000 );
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
        void update()
        {
            for ( size_t index = 0; index < _listClient.size(); ++index )
            {
                vector<FakeHousingPeers::Sent> listSent = std::move( _listClient[index]->_peers._listSent );
                _listClient[index]->_peers._listSent.clear();
                for ( const FakeHousingPeers::Sent& sent : listSent )
                    (void)_server.handleMessage( static_cast<int32>( index ), sent._bytes.data(), static_cast<int32>( sent._bytes.size() ), NetChannelKind::ReliableOrdered );
            }
            _server.update( vector<HousingNetPlotRef>{ _pServerPlot->makeNetPlot() } );
            vector<FakeHousingPeers::Sent> listOut = std::move( _serverPeers._listSent );
            _serverPeers._listSent.clear();
            for ( const FakeHousingPeers::Sent& sent : listOut )
            {
                Client& client = *_listClient[static_cast<size_t>( sent._connectionID )];
                SW_EXPECT_TRUE( client._replication.handleMessage( 0, sent._bytes.data(), static_cast<int32>( sent._bytes.size() ), NetChannelKind::ReliableOrdered ) == NetHandleResult::Handled );
            }
            for ( unique_ptr<Client>& pClient : _listClient )
            {
                pClient->_listReply.clear();
                pClient->_replication.update( vector<HousingNetPlotRef>{ pClient->_pPlot->makeNetPlot() }, pClient->_listReply );
            }
        }

        /** @brief 연결 @p connectionID 를 닫습니다(서버 쪽). */
        void disconnect( int32 connectionID )
        {
            (void)VectorUtil::removeValue( _serverPeers._listConnected, connectionID );
            _server.onConnectionClosed( connectionID, NetDisconnectReason::Timeout );
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
    rig.update();
    SW_EXPECT_TRUE( rig.lastReply( 1 ) == HousingPlaceResult::NotOwner );
    rig.send( 0, HousingNetRig::makeOp( HousingEditOp::Claim ) );
    rig.send( 1, HousingNetRig::makeOp( HousingEditOp::Claim ) );
    rig.update();
    SW_EXPECT_TRUE( rig.lastReply( 0 ) == HousingPlaceResult::Ok );
    SW_EXPECT_TRUE( rig.lastReply( 1 ) == HousingPlaceResult::NotOwner );
    SW_EXPECT_TRUE( rig._pServerPlot->_owner == HousingOwnerID::makeForConnection( 0, 0 ) ); // 계정 없는 연결 — 세션 주체
    SW_EXPECT_TRUE( rig._listClient[0]->_replication.findOwner( hashed_string( "home" ) ) == HousingPlotOwner::Self );
    SW_EXPECT_TRUE( rig._listClient[1]->_replication.findOwner( hashed_string( "home" ) ) == HousingPlotOwner::Other );

    // 남의 부지: 클라이언트 1 의 놓기 · 되돌리기는 판정 전에 거절된다.
    rig.send( 1, HousingNetRig::makePlace( "floor", 0.5f, 0.5f ) );
    rig.send( 1, HousingNetRig::makeOp( HousingEditOp::Undo ) );
    rig.update();
    SW_REQUIRE_EQUAL( size_t{ 2 }, rig._listClient[1]->_listReply.size() );
    SW_EXPECT_TRUE( rig._listClient[1]->_listReply[0]._result == HousingPlaceResult::NotOwner );
    SW_EXPECT_TRUE( rig._listClient[1]->_listReply[1]._result == HousingPlaceResult::NotOwner );
    SW_EXPECT_TRUE( rig._pServerPlot->_layout.getPlacements().empty() );

    // 주인의 놓기 — 서버가 스냅하고 값을 서버 지갑에서 받는다. 같은 칸은 겹침, 둘째 바닥은 예산 초과.
    rig.send( 0, HousingNetRig::makePlace( "floor", 0.4f, 0.6f ) );
    rig.send( 0, HousingNetRig::makePlace( "floor", 0.5f, 0.5f ) );
    rig.send( 0, HousingNetRig::makePlace( "floor", 1.5f, 0.5f ) );
    rig.update();
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
    rig.update();
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
        rig.update();
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
        rig.update();
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
        rig.update();
        SW_REQUIRE_EQUAL( size_t{ 3 }, rig._listClient[0]->_listReply.size() );
        SW_EXPECT_TRUE( rig._listClient[0]->_listReply[0]._result == HousingPlaceResult::RateLimited ); // 바로 답했다 — 줄의 둘보다 먼저 온다
        SW_EXPECT_TRUE( rig._listClient[0]->_listReply[1]._result == HousingPlaceResult::Ok );
        SW_EXPECT_TRUE( rig._listClient[0]->_listReply[2]._result == HousingPlaceResult::Ok );
        SW_EXPECT_EQUAL( size_t{ 1 }, rig._pServerPlot->_layout.getPlacements().size() );
        // 다음 틱에는 다시 받는다.
        rig.send( 0, HousingNetRig::makePlace( "floor", 1.5f, 0.5f ) );
        rig.update();
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
        SW_EXPECT_TRUE( rig._server.handleMessage( 0, writer.getBytes().data(), writer.getByteCount(), NetChannelKind::ReliableOrdered ) == NetHandleResult::Malformed );
        BitWriter& badOp = writer.begin( NetHousingMessage::kEdit );
        badOp.writeVarUint( 2 );
        badOp.writeVarUint( static_cast<uint64>( hashed_string( "home" ).getHash() ) );
        badOp.writeBits( 7u, 3 );
        SW_EXPECT_TRUE( rig._server.handleMessage( 0, writer.getBytes().data(), writer.getByteCount(), NetChannelKind::ReliableOrdered ) == NetHandleResult::Malformed );
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
    rig.update();
    HousingEditRequest remove;
    remove._op          = HousingEditOp::Remove;
    remove._placementID = 4; // 마지막 바닥 — 다음 id 는 5 로 남는다
    rig.send( 0, remove );
    rig.update();
    SW_EXPECT_TRUE( rig.lastReply( 0 ) == HousingPlaceResult::Ok );
    SW_REQUIRE_EQUAL( size_t{ 3 }, rig._pServerPlot->_layout.getPlacements().size() );

    // 늦은 참가 — 부지는 이번 틱에 바뀌지 않았어도 들어온 연결은 상태를 받는다.
    HousingNetRig::Client& late = rig.connect();
    SW_EXPECT_TRUE( late._pPlot->_layout.getPlacements().empty() );
    rig.update();
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
    rig.disconnect( 0 );
    rig.update();
    SW_EXPECT_TRUE( late._replication.findOwner( hashed_string( "home" ) ) == HousingPlotOwner::None );
    rig.send( 1, HousingNetRig::makeOp( HousingEditOp::Claim ) );
    rig.send( 1, HousingNetRig::makeOp( HousingEditOp::Undo ) );
    rig.update();
    SW_REQUIRE_EQUAL( size_t{ 2 }, late._listReply.size() );
    SW_EXPECT_TRUE( late._listReply[0]._result == HousingPlaceResult::Ok );
    SW_EXPECT_TRUE( late._listReply[1]._result == HousingPlaceResult::NothingToUndo );
    SW_EXPECT_TRUE( late._replication.findOwner( hashed_string( "home" ) ) == HousingPlotOwner::Self );
}

/**
 * @brief [HousingReplicationTest] 주인은 연결이 아니라 주체다 — 계정 주인이 끊겼다 다시 붙으면(새 연결) 같은 주인이고, 다른 계정의 차지는 거절된다
 */
SW_TEST_CASE( HousingReplicationTest, OwnerIsThePrincipalAcrossReconnects )
{
    constexpr uint64 kAccount      = 77;
    constexpr uint64 kOtherAccount = 88;
    HousingNetRig    rig( 100 );
    rig.connect( kAccount );
    rig.send( 0, HousingNetRig::makeOp( HousingEditOp::Claim ) );
    rig.send( 0, HousingNetRig::makePlace( "floor", 0.5f, 0.5f ) );
    rig.update();
    SW_EXPECT_TRUE( rig.lastReply( 0 ) == HousingPlaceResult::Ok );
    SW_EXPECT_EQUAL( kAccount, rig._pServerPlot->_owner );

    // 끊겨도 계정 주인은 남는다(세이브에 실리는 값).
    rig.disconnect( 0 );
    rig.update();
    SW_EXPECT_EQUAL( kAccount, rig._pServerPlot->_owner );
    SW_EXPECT_TRUE( HousingOwnerID::isPersistent( rig._pServerPlot->_owner ) );

    // 같은 계정이 새 연결로 붙으면 내 부지로 받고 바로 짓는다.
    HousingNetRig::Client& back = rig.connect( kAccount );
    rig.update();
    SW_EXPECT_TRUE( back._replication.findOwner( hashed_string( "home" ) ) == HousingPlotOwner::Self );
    rig.send( 1, HousingNetRig::makePlace( "floor", 1.5f, 0.5f ) );
    rig.update();
    SW_EXPECT_TRUE( rig.lastReply( 1 ) == HousingPlaceResult::Ok );

    // 다른 계정은 남의 부지 — 차지도 편집도 거절된다.
    HousingNetRig::Client& other = rig.connect( kOtherAccount );
    rig.update();
    SW_EXPECT_TRUE( other._replication.findOwner( hashed_string( "home" ) ) == HousingPlotOwner::Other );
    rig.send( 2, HousingNetRig::makeOp( HousingEditOp::Claim ) );
    rig.send( 2, HousingNetRig::makeOp( HousingEditOp::Undo ) );
    rig.update();
    SW_REQUIRE_EQUAL( size_t{ 2 }, other._listReply.size() );
    SW_EXPECT_TRUE( other._listReply[0]._result == HousingPlaceResult::NotOwner );
    SW_EXPECT_TRUE( other._listReply[1]._result == HousingPlaceResult::NotOwner );
    SW_EXPECT_EQUAL( size_t{ 2 }, rig._pServerPlot->_layout.getPlacements().size() );
}

/**
 * @brief [HousingReplicationTest] 되돌리기 · 다시 하기도 거리로 판정한다 — 서버는 그 편집이 건드릴 이력 칸의 자리를 서버가 아는 요청자 자리와 견준다
 */
SW_TEST_CASE( HousingReplicationTest, UndoAndRedoAreJudgedByTheirTargetReach )
{
    float3        caller{ 0.0f, 0.0f, 0.0f };
    float3*       pCaller = &caller;
    HousingNetRig rig( 100, 16, 3.0f, HousingCallerLocator::create( [pCaller]( int32, float3& outPosition )
    {
        outPosition = *pCaller;
        return true;
    } ) );
    rig.connect();
    rig.send( 0, HousingNetRig::makeOp( HousingEditOp::Claim ) );
    rig.send( 0, HousingNetRig::makePlace( "floor", 0.5f, 0.5f ) );
    rig.send( 0, HousingNetRig::makePlace( "floor", 1.5f, 0.5f ) );
    HousingEditRequest remove;
    remove._op          = HousingEditOp::Remove;
    remove._placementID = 2;
    rig.send( 0, remove );
    rig.update();
    SW_EXPECT_TRUE( rig.lastReply( 0 ) == HousingPlaceResult::Ok );
    SW_REQUIRE_EQUAL( size_t{ 1 }, rig._pServerPlot->_layout.getPlacements().size() );

    // 요청자가 멀어지면 치운 바닥을 되돌리는 것(그 자리에 다시 놓기)도 닿지 않는다.
    caller = float3{ 100.0f, 0.0f, 100.0f };
    rig.send( 0, HousingNetRig::makeOp( HousingEditOp::Undo ) );
    rig.update();
    SW_EXPECT_TRUE( rig.lastReply( 0 ) == HousingPlaceResult::OutOfReach );
    SW_EXPECT_EQUAL( size_t{ 1 }, rig._pServerPlot->_layout.getPlacements().size() );

    // 돌아오면 되돌린다. 다시 멀어지면 다시 하기(그 자리에서 치우기)도 닿지 않는다.
    caller = float3{ 0.0f, 0.0f, 0.0f };
    rig.send( 0, HousingNetRig::makeOp( HousingEditOp::Undo ) );
    rig.update();
    SW_EXPECT_TRUE( rig.lastReply( 0 ) == HousingPlaceResult::Ok );
    SW_EXPECT_EQUAL( size_t{ 2 }, rig._pServerPlot->_layout.getPlacements().size() );
    caller = float3{ 100.0f, 0.0f, 100.0f };
    rig.send( 0, HousingNetRig::makeOp( HousingEditOp::Redo ) );
    rig.update();
    SW_EXPECT_TRUE( rig.lastReply( 0 ) == HousingPlaceResult::OutOfReach );
    SW_EXPECT_EQUAL( size_t{ 2 }, rig._pServerPlot->_layout.getPlacements().size() );
}

/**
 * @brief [HousingReplicationTest] 신뢰 메시지 하나(64 KB)를 넘는 부지(배치 4096)는 조각으로 가고 창이 차면 다음 틱에 잇는다 — 그 뒤 바뀐 배치는 델타로만 간다
 */
SW_TEST_CASE( HousingReplicationTest, LargePlotTravelsInChunksThenAsDeltas )
{
    constexpr int32 kPlotSize = 64;
    HousingNetRig   rig( 1000000, 16, 0.0f, HousingCallerLocator{}, kPlotSize );
    // 서버 월드의 편집(사람이 아닌 서버 코드)은 바로 적용된다 — 바닥 4096 개.
    for ( int32 z = 0; z < kPlotSize; ++z )
    {
        for ( int32 x = 0; x < kPlotSize; ++x )
        {
            HousingCandidate candidate;
            candidate._partID              = hashed_string( "floor" );
            candidate._transform._position = float2{ static_cast<float32>( x ) + 0.5f, static_cast<float32>( z ) + 0.5f };
            uint32 placementID             = 0;
            SW_REQUIRE_TRUE( rig._pServerPlot->_layout.place( candidate, placementID ) == HousingPlaceResult::Ok );
        }
    }
    HousingNetRig::Client& late = rig.connect();
    // 신뢰 창이 조각 둘만 받는다 — 나머지는 서버가 들고 있다가 다음 틱에 잇는다(연결을 닫지 않는다).
    rig._serverPeers._sendBudget = 2;
    rig.update();
    SW_EXPECT_EQUAL( uint64{ 2 }, rig._server.getStats()._snapshotChunkCount );
    SW_EXPECT_EQUAL( size_t{ 1 }, rig._server.getTransferCount() );
    SW_EXPECT_TRUE( late._pPlot->_layout.getPlacements().empty() );
    rig._serverPeers._sendBudget = -1;
    rig.update();
    SW_EXPECT_EQUAL( size_t{ 0 }, rig._server.getTransferCount() );
    SW_EXPECT_TRUE( rig._server.getStats()._snapshotChunkCount > 4 ); // 4096 배치는 64 KB 를 넘는다 — 조각이 여럿이다
    SW_REQUIRE_EQUAL( size_t{ 4096 }, late._pPlot->_layout.getPlacements().size() );
    SW_EXPECT_EQUAL( uint64{ 1 }, late._replication.getStats()._snapshotAppliedCount );

    // 하나를 치우고 하나를 옮기면 델타 하나만 간다(조각은 더 가지 않는다).
    const uint64 chunkCount = rig._server.getStats()._snapshotChunkCount;
    SW_REQUIRE_TRUE( rig._pServerPlot->_layout.remove( 4096 ) == HousingPlaceResult::Ok );
    SW_REQUIRE_TRUE( rig._pServerPlot->_layout.move( 1, HousingTransform{
                                                            float2{ 63.5f, 63.5f },
                                                            0, 0.0f
    },
                                                     HousingPlacementMode::Grid ) == HousingPlaceResult::Ok );
    rig.update();
    SW_EXPECT_EQUAL( chunkCount, rig._server.getStats()._snapshotChunkCount );
    SW_EXPECT_EQUAL( uint64{ 1 }, rig._server.getStats()._deltaSentCount );
    SW_EXPECT_EQUAL( uint64{ 1 }, late._replication.getStats()._deltaAppliedCount );
    const HousingLayout& serverLayout = rig._pServerPlot->_layout;
    const HousingLayout& lateLayout   = late._pPlot->_layout;
    SW_REQUIRE_EQUAL( serverLayout.getPlacements().size(), lateLayout.getPlacements().size() );
    for ( size_t index = 0; index < serverLayout.getPlacements().size(); ++index )
    {
        SW_EXPECT_EQUAL( serverLayout.getPlacements()[index]._placementID, lateLayout.getPlacements()[index]._placementID );
        SW_EXPECT_TRUE( serverLayout.getPlacements()[index]._transform == lateLayout.getPlacements()[index]._transform );
    }
    SW_EXPECT_EQUAL( rig._pServerPlot->getFunds(), late._pPlot->getFunds() );
}

/**
 * @brief [HousingReplicationTest] 고정 배치형 본문 — 판정의 등록 밖 결과 · 잘린 상태 조각 · 전체 밖 조각은 깨진 메시지이고 쌓이지 않는다
 */
SW_TEST_CASE( HousingReplicationTest, BodyMessagesRejectUnknownValuesAndTruncation )
{
    HousingReplicationClient client;
    NetMessageWriter         writer;

    // 판정 — 결과 열거자 자리에 등록 밖 값을 꾸민다(요청 번호 · 부지 해시 뒤 8 비트 0xFF).
    BitWriter& badReply = writer.begin( NetHousingMessage::kReply );
    badReply.writeVarUint( 1 );
    badReply.writeVarUint( 7 );
    badReply.writeBits( 0xFFu, 8 );
    badReply.writeVarUint( 0 );
    SW_EXPECT_TRUE( client.handleMessage( 0, writer.getBytes().data(), writer.getByteCount(), NetChannelKind::ReliableOrdered ) == NetHandleResult::Malformed );

    HousingPlotSnapshotBody chunk;
    chunk._plotHash  = 7;
    chunk._owner     = HousingPlotOwner::Self;
    chunk._totalSize = 4;
    chunk._offset    = 2;
    chunk._chunk     = vector<uint8>{ 1, 2, 3 }; // 2 + 3 > 4 — 전체 밖이다
    SW_REQUIRE_TRUE( ( writer.writeMessage<NetHousingMessage::Table, NetHousingMessage::PlotSnapshot>( chunk ) ) );
    SW_EXPECT_TRUE( client.handleMessage( 0, writer.getBytes().data(), writer.getByteCount(), NetChannelKind::ReliableOrdered ) == NetHandleResult::Malformed );

    chunk._offset = 0;
    SW_REQUIRE_TRUE( ( writer.writeMessage<NetHousingMessage::Table, NetHousingMessage::PlotSnapshot>( chunk ) ) );
    const vector<uint8> listTruncated( writer.getBytes().begin(), writer.getBytes().end() - 1 );
    SW_EXPECT_TRUE( client.handleMessage( 0, listTruncated.data(), static_cast<int32>( listTruncated.size() ), NetChannelKind::ReliableOrdered ) == NetHandleResult::Malformed );
}

/**
 * @brief [HousingReplicationTest] 손 본문 — 종류에 맞지 않는 편집 꼬리(남는 바이트) · 등록 밖 주인 · 상한 넘는 델타 개수 · 델타 뒤 남는 바이트는 깨진 메시지다
 */
SW_TEST_CASE( HousingReplicationTest, HandBodiesRejectTrailingTailsAndUnknownOwners )
{
    {
        HousingNetRig    rig( 100 );
        NetMessageWriter writer;
        BitWriter&       claim = writer.begin( NetHousingMessage::kEdit );
        claim.writeVarUint( 1 );
        claim.writeVarUint( static_cast<uint64>( hashed_string( "home" ).getHash() ) );
        claim.writeBits( static_cast<uint32>( HousingEditOp::Claim ), 3 );
        claim.writeVarUint( 99 ); // 차지에는 꼬리가 없다
        SW_EXPECT_TRUE( rig._server.handleMessage( 0, writer.getBytes().data(), writer.getByteCount(), NetChannelKind::ReliableOrdered ) == NetHandleResult::Malformed );
        SW_EXPECT_EQUAL( uint64{ 0 }, rig._server.getStats()._editCount );
    }

    HousingReplicationClient client;
    NetMessageWriter         writer;
    const auto               writeDeltaHead = [&writer]( uint32 owner, uint64 removedCount ) -> BitWriter&
    {
        BitWriter& delta = writer.begin( NetHousingMessage::kPlotDelta );
        delta.writeVarUint( 7 );     // 부지
        delta.writeBits( owner, 2 ); // 주인
        delta.writeVarInt( 0 );      // 잔액
        delta.writeVarUint( 1 );     // 다음 배치 id
        delta.writeVarUint( removedCount );
        return delta;
    };
    (void)writeDeltaHead( static_cast<uint32>( HousingPlotOwner::Count ), 0 ).writeVarUint( 0 );
    SW_EXPECT_TRUE( client.handleMessage( 0, writer.getBytes().data(), writer.getByteCount(), NetChannelKind::ReliableOrdered ) == NetHandleResult::Malformed );
    (void)writeDeltaHead( static_cast<uint32>( HousingPlotOwner::Self ), uint64{ 1 } << 30 );
    SW_EXPECT_TRUE( client.handleMessage( 0, writer.getBytes().data(), writer.getByteCount(), NetChannelKind::ReliableOrdered ) == NetHandleResult::Malformed );
    BitWriter& trailing = writeDeltaHead( static_cast<uint32>( HousingPlotOwner::Self ), 0 );
    trailing.writeVarUint( 0 );     // 새로 · 바뀐 배치 수
    trailing.writeBits( 0xFFu, 8 ); // 남는 바이트
    SW_EXPECT_TRUE( client.handleMessage( 0, writer.getBytes().data(), writer.getByteCount(), NetChannelKind::ReliableOrdered ) == NetHandleResult::Malformed );
}
