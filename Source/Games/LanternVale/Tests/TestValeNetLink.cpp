#include "pch.h"

#include "Core/Container/unordered_map.h"
#include "Core/Network/Connection/NetEndpoint.h"
#include "Core/Network/Tests/LoopbackCluster.h"
#include "Core/Testing/TestFramework.h"

#include "GameFramework/Runtime/Foundation/Framework/Save/Local/MemoryLocalStore.h"

#include "Games/LanternVale/ValeNetLink.h"

// 골짜기 서버 · 클라이언트를 루프백으로 잇는다 — 계정으로 들이기 → 반사 함수 호출로 전투 · 파티 → 서버가 알린 진행 → 검증 실패 끊기 → 저장 후 재접속 · 서버 재시작.

using namespace sw;

namespace
{
    constexpr uint32 kNearCreatureID = ValeWorld::kPlacementIDBase + 0;
    constexpr uint32 kSideCreatureID = ValeWorld::kPlacementIDBase + 1;

    /**
     * @brief 서버 엔드포인트를 그대로 넘기되 연결의 주체(계정)만 테스트가 정합니다. 루프백 연결은 티켓이 없어 주체가 0 이다 — 게임 접속 티켓 서버의 계정을 흉내 낸다.
     */
    class ValeAccountPeers final : public INetPeers
    {
    public:
        explicit ValeAccountPeers( NetEndpoint& inner )
            : _inner{ inner }
            , _mapAccount{}
        {
        }

        void setAccount( int32 connectionID, uint64 accountID ) { _mapAccount[connectionID] = accountID; }

        bool               sendMessage( int32 connectionID, NetChannelType channel, const uint8* pData, int32 size ) override { return _inner.sendMessage( connectionID, channel, pData, size ); }
        bool               trySendMessage( int32 connectionID, NetChannelType channel, const uint8* pData, int32 size ) override { return _inner.trySendMessage( connectionID, channel, pData, size ); }
        bool               sendNotifiedMessage( int32 connectionID, const uint8* pData, int32 size, uint32 tag ) override { return _inner.sendNotifiedMessage( connectionID, pData, size, tag ); }
        int32              broadcast( NetChannelType channel, const uint8* pData, int32 size, int32 exceptID ) override { return _inner.broadcast( channel, pData, size, exceptID ); }
        void               disconnect( int32 connectionID ) override { _inner.disconnect( connectionID ); }
        void               pollInbound( NetInbound& outInbound ) override { _inner.pollInbound( outInbound ); }
        bool               isServer() const override { return _inner.isServer(); }
        NetConnectionState getConnectionState( int32 connectionID ) const override { return _inner.getConnectionState( connectionID ); }
        bool               getConnectionStats( int32 connectionID, NetConnectionStats& outStats ) const override { return _inner.getConnectionStats( connectionID, outStats ); }
        NetAddress         getConnectionAddress( int32 connectionID ) const override { return _inner.getConnectionAddress( connectionID ); }
        uint64             getConnectionPrincipal( int32 connectionID ) const override
        {
            const auto iter = _mapAccount.find( connectionID );
            return iter != _mapAccount.end() ? iter->second : 0;
        }
        int32 getClientIndex() const override { return _inner.getClientIndex(); }
        int32 getConnectedCount() const override { return _inner.getConnectedCount(); }
        void  collectConnected( vector<int32>& outListConnection ) const override { _inner.collectConnected( outListConnection ); }
        int32 getMaxBytesPerSecond() const override { return _inner.getMaxBytesPerSecond(); }
        bool  isConnectionSaturated( int32 connectionID ) const override { return _inner.isConnectionSaturated( connectionID ); }
        void  addSentKindStats( NetMessageKindStatsTable& outTable ) const override { _inner.addSentKindStats( outTable ); }

    private:
        NetEndpoint&                 _inner;
        unordered_map<int32, uint64> _mapAccount;
    };

    ValeRules makeValeLinkTestRules()
    {
        ValeRules rules;
        rules._listLevelExperience = { 100, 250, 450, 700 };
        rules._moveSpeed           = 6.0f;
        ValePlacement spawn;
        spawn._kind     = ValePlacementKind::Creature;
        spawn._position = float3{ 2.0f, 0.0f, 0.0f };
        rules._listPlacement.push_back( spawn );
        spawn._position = float3{ -2.0f, 0.0f, 1.0f };
        rules._listPlacement.push_back( spawn );
        spawn._kind     = ValePlacementKind::Herb;
        spawn._position = float3{ 0.0f, 0.0f, 1.5f };
        rules._listPlacement.push_back( spawn );
        return rules;
    }

    StreamingCellGridDef makeValeLinkTestGrid()
    {
        StreamingCellGridDef grid;
        grid._cellSize      = 32.0f;
        grid._loadingRadius = 96.0f;
        return grid;
    }

    /** @brief 서버 하나와 클라이언트 @p clientCount 개 — 클라이언트 i 의 계정은 1000 + i 입니다. 진행 저장소는 메모리이고 서버를 다시 세워도 데이터는 남습니다. */
    struct ValeLinkTestRig
    {
        test::LoopbackCluster        _cluster{ 23u };
        MemoryLocalDatabase          _database;
        ValeAccountPeers*            _pServerPeers{ nullptr };
        unique_ptr<ValeAccountPeers> _pServerPeersOwner;
        NetMessageRouter             _serverRouter;
        NetMessageRouter             _arrClientRouter[2];
        ValeServerLink               _server;
        ValeClientLink               _arrClient[2];
        int32                        _clientCount{ 0 };

        explicit ValeLinkTestRig( int32 clientCount )
            : _clientCount{ clientCount }
        {
            const NetEndpointConfig settings;
            (void)_cluster.addEndpoint( 4500, settings );
            for ( int32 index = 0; index < clientCount; ++index )
            {
                (void)_cluster.addEndpoint( static_cast<uint16>( 4600 + index ), settings );
            }
            SW_EXPECT_TRUE( _cluster.connectClients( 60 ) );
            _pServerPeersOwner = make_unique<ValeAccountPeers>( _cluster.getServer() );
            _pServerPeers      = _pServerPeersOwner.get();
            for ( int32 index = 0; index < clientCount; ++index )
            {
                _pServerPeers->setAccount( _cluster.getClient( index ).getClientIndex(), 1000 + static_cast<uint64>( index ) );
                SW_EXPECT_TRUE( _arrClient[index].initialize( _cluster.getClient( index ), _arrClientRouter[index] ) );
            }
        }

        [[nodiscard]] bool startServer( ValeServerLink& server )
        {
            return server.initialize( *_pServerPeers, _serverRouter, makeValeLinkTestRules(), makeValeLinkTestGrid(), make_unique<MemoryLocalStore>( &_database, LocalSealContext{} ) );
        }

        int32 getConnectionID( int32 clientIndex ) { return _cluster.getClient( clientIndex ).getClientIndex(); }

        /** @brief 망 · 라우터 · 링크를 @p seconds 만큼 돌립니다(1/60 초 걸음). */
        void run( ValeServerLink& server, float32 seconds )
        {
            const int32 stepCount = static_cast<int32>( seconds * 60.0f ) + 1;
            for ( int32 step = 0; step < stepCount; ++step )
            {
                _cluster.step();
                (void)_serverRouter.poll( *_pServerPeers );
                server.update( 1.0f / 60.0f );
                for ( int32 index = 0; index < _clientCount; ++index )
                {
                    (void)_arrClientRouter[index].poll( _cluster.getClient( index ) );
                    _arrClient[index].update();
                }
            }
        }

        /** @brief 짐승을 쓰러질 때까지 세 번 칩니다(재사용 대기 0.5 초를 넘겨 기다린다). */
        void defeat( ValeServerLink& server, int32 clientIndex, uint32 creatureID )
        {
            for ( int32 strike = 0; strike < 3; ++strike )
            {
                SW_EXPECT_NOT_EQUAL( 0u, _arrClient[clientIndex].callServerFunction( "attackCreature", { ReflectedValue::make( creatureID ) } ) );
                run( server, 0.6f );
            }
        }
    };
} // namespace

/** @brief [ValeNetLinkTest] 계정으로 든 아바타가 반사 함수 호출로 짐승을 쓰러뜨리고, 서버가 판정한 결과 · 진행 · MMO 복제가 클라이언트에 온다. */
SW_TEST_CASE( ValeNetLinkTest, ServerAuthorityLoopReachesTheClient )
{
    ValeLinkTestRig rig( 1 );
    SW_REQUIRE_TRUE( rig.startServer( rig._server ) );
    rig.run( rig._server, 0.5f );
    const ValeAvatarAgent& agent = rig._arrClient[0].getAgent();
    SW_REQUIRE_TRUE( agent._avatarID != 0u );
    SW_EXPECT_EQUAL( 1, agent._shownLevel );
    SW_EXPECT_EQUAL( 1, rig._server.getOnlineCount() );
    SW_EXPECT_TRUE( rig._arrClient[0].getView().findEntity( kNearCreatureID ) != nullptr );

    rig.defeat( rig._server, 0, kNearCreatureID );
    SW_EXPECT_EQUAL( 3, agent._resultCount );
    SW_EXPECT_TRUE( agent._lastResult == ValeActionResult::Ok );
    SW_EXPECT_EQUAL( 60, agent._shownExperience );
    SW_EXPECT_EQUAL( 5, agent._shownCoins );
    SW_EXPECT_EQUAL( 1, agent._shownDefeatCount );
    SW_EXPECT_EQUAL( uint64{ 3 }, rig._server.getRemoteCallStats()._invokedCount );
    SW_EXPECT_TRUE( rig._arrClient[0].getView().findEntity( kNearCreatureID ) == nullptr ); // 쓰러진 짐승은 복제에서 빠진다

    // 재사용 대기 중의 공격은 서버가 결과로 거절한다(끊지 않는다).
    SW_EXPECT_NOT_EQUAL( 0u, rig._arrClient[0].callServerFunction( "attackCreature", { ReflectedValue::make( kSideCreatureID ) } ) );
    SW_EXPECT_NOT_EQUAL( 0u, rig._arrClient[0].callServerFunction( "attackCreature", { ReflectedValue::make( kSideCreatureID ) } ) );
    rig.run( rig._server, 0.2f );
    SW_EXPECT_TRUE( agent._lastResult == ValeActionResult::CoolingDown );
    SW_EXPECT_EQUAL( 1, rig._server.getOnlineCount() );
}

/** @brief [ValeNetLinkTest] 파티 초대 · 수락이 서버에서 서고, 쓰러뜨린 짐승의 경험을 두 클라이언트가 나눠 받는다. */
SW_TEST_CASE( ValeNetLinkTest, PartyIsServerAuthoritative )
{
    ValeLinkTestRig rig( 2 );
    SW_REQUIRE_TRUE( rig.startServer( rig._server ) );
    rig.run( rig._server, 0.5f );
    const ValeAvatarAgent& leader = rig._arrClient[0].getAgent();
    const ValeAvatarAgent& member = rig._arrClient[1].getAgent();
    SW_REQUIRE_TRUE( leader._avatarID != 0u && member._avatarID != 0u );

    SW_EXPECT_NOT_EQUAL( 0u, rig._arrClient[0].callServerFunction( "inviteToParty", { ReflectedValue::make( member._avatarID ) } ) );
    rig.run( rig._server, 0.2f );
    SW_EXPECT_EQUAL( leader._avatarID, member._pendingInviteFromID );
    SW_EXPECT_NOT_EQUAL( 0u, rig._arrClient[1].callServerFunction( "acceptPartyInvite", { ReflectedValue::make( leader._avatarID ) } ) );
    rig.run( rig._server, 0.2f );
    SW_EXPECT_EQUAL( 2, leader._shownPartySize );
    SW_EXPECT_EQUAL( 2, member._shownPartySize );

    rig.defeat( rig._server, 0, kNearCreatureID );
    SW_EXPECT_EQUAL( 30, leader._shownExperience );
    SW_EXPECT_EQUAL( 30, member._shownExperience );
    SW_EXPECT_EQUAL( 5, leader._shownCoins );
    SW_EXPECT_EQUAL( 0, member._shownCoins );

    SW_EXPECT_NOT_EQUAL( 0u, rig._arrClient[1].callServerFunction( "leaveParty", {} ) );
    rig.run( rig._server, 0.2f );
    SW_EXPECT_EQUAL( 1, leader._shownPartySize );
    SW_EXPECT_EQUAL( 1, member._shownPartySize );
}

/** @brief [ValeNetLinkTest] 검증 짝이 지어낸 번호 · 자기 초대를 거르면 서버가 그 연결을 끊는다 — 몸(골짜기 규칙)은 돌지 않는다. */
SW_TEST_CASE( ValeNetLinkTest, ForgedTargetsAreDisconnected )
{
    ValeLinkTestRig rig( 1 );
    SW_REQUIRE_TRUE( rig.startServer( rig._server ) );
    rig.run( rig._server, 0.5f );
    SW_REQUIRE_TRUE( rig._arrClient[0].getAgent()._avatarID != 0u );
    SW_EXPECT_LOG( "RemoteCall.ValidationFailed" );
    SW_EXPECT_NOT_EQUAL( 0u, rig._arrClient[0].callServerFunction( "attackCreature", { ReflectedValue::make( uint32{ 7 } ) } ) );
    for ( int32 step = 0; step < 8; ++step )
    {
        rig._cluster.step();
        (void)rig._serverRouter.poll( *rig._pServerPeers ); // 서버만 — 끊긴 클라이언트 쪽 사건은 이 테스트가 보지 않는다
    }
    SW_EXPECT_EQUAL( uint64{ 1 }, rig._server.getRemoteCallStats()._validationFailedCount );
    SW_EXPECT_EQUAL( uint64{ 0 }, rig._server.getStats()._actionCount );
    SW_EXPECT_TRUE( rig._cluster.getServer().getConnectionState( rig.getConnectionID( 0 ) ) != NetConnectionState::Connected );
}

/** @brief [ValeNetLinkTest] 접속이 끝나면 진행을 저장하고, 같은 계정이 다시 들면(서버를 다시 세워도) 그 진행을 읽는다. 주체가 없는 연결은 받지 않는다. */
SW_TEST_CASE( ValeNetLinkTest, ProgressSurvivesReconnectAndServerRestart )
{
    ValeLinkTestRig rig( 1 );
    SW_REQUIRE_TRUE( rig.startServer( rig._server ) );
    rig.run( rig._server, 0.5f );
    rig.defeat( rig._server, 0, kNearCreatureID );
    const ValeAvatarAgent& agent = rig._arrClient[0].getAgent();
    SW_REQUIRE_EQUAL( 60, agent._shownExperience );
    const uint32 firstAvatarID = agent._avatarID;

    // 재접속: 연결이 닫혀 저장되고, 같은 연결 번호로 다시 든다.
    const int32 connectionID = rig.getConnectionID( 0 );
    rig._server.onConnectionClosed( connectionID, NetDisconnectReason::Remote );
    SW_EXPECT_EQUAL( 0, rig._server.getOnlineCount() );
    rig._server.acceptConnection( connectionID, 1000 );
    rig.run( rig._server, 0.3f );
    SW_EXPECT_NOT_EQUAL( firstAvatarID, agent._avatarID );
    SW_EXPECT_EQUAL( 60, agent._shownExperience );
    SW_EXPECT_EQUAL( 5, agent._shownCoins );
    SW_EXPECT_TRUE( rig._server.getProfiles().getSavedCount() >= uint64{ 1 } );

    // 서버 재시작: 내리며 저장하고, 새 서버가 같은 저장소에서 읽는다.
    rig._server.shutdown();
    ValeServerLink restarted;
    SW_REQUIRE_TRUE( rig.startServer( restarted ) );
    rig.run( restarted, 0.3f );
    SW_EXPECT_EQUAL( uint64{ 1 }, restarted.getStats()._admittedCount );
    SW_EXPECT_EQUAL( 60, agent._shownExperience );
    SW_EXPECT_EQUAL( 1, agent._shownDefeatCount );

    // 계정이 없는 연결(자격 없이 붙은)은 받지 않는다.
    SW_EXPECT_LOG( "LanternVale.NoAccount" );
    restarted.acceptConnection( connectionID + 1000, 0 );
    SW_EXPECT_EQUAL( uint64{ 1 }, restarted.getStats()._refusedCount );
    restarted.shutdown();
}
