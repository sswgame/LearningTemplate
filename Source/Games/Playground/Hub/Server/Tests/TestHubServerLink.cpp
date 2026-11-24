#include "pch.h"

#include "Core/Container/unordered_map.h"
#include "Core/Network/Connection/NetEndpoint.h"
#include "Core/Network/Tests/LoopbackCluster.h"
#include "Core/Testing/TestFramework.h"

#include "GameFramework/Runtime/Online/Store/Server/MemoryServiceStore.h"

#include "Games/Playground/Hub/Server/HubServerLink.h"

// 허브 서버 · 클라이언트를 루프백으로 잇는다 — 계정으로 들이기 → 반사 함수 호출로 전투 · 파티 → 서버가 알린 진행 → 검증 실패 끊기 → 저장 후 재접속 · 서버 재시작.

using namespace sw;

namespace
{
    constexpr uint32 kNearCreatureID = HubWorld::kPlacementIDBase + 0;
    constexpr uint32 kSideCreatureID = HubWorld::kPlacementIDBase + 1;

    /**
     * @brief 서버 엔드포인트를 그대로 넘기되 연결의 주체(계정)만 테스트가 정합니다. 루프백 연결은 티켓이 없어 주체가 0 이다 — 게임 접속 티켓 서버의 계정을 흉내 낸다.
     */
    class HubAccountPeers final : public INetPeers
    {
    public:
        explicit HubAccountPeers( NetEndpoint& inner )
            : _inner{ inner }
            , _mapAccount{}
        {
        }

        void setAccount( int32 connectionID, uint64 accountID ) { _mapAccount[connectionID] = accountID; }

        bool               sendMessage( int32 connectionID, NetChannelKind channel, const uint8* pData, int32 size ) override { return _inner.sendMessage( connectionID, channel, pData, size ); }
        bool               trySendMessage( int32 connectionID, NetChannelKind channel, const uint8* pData, int32 size ) override { return _inner.trySendMessage( connectionID, channel, pData, size ); }
        bool               sendNotifiedMessage( int32 connectionID, const uint8* pData, int32 size, uint32 tag ) override { return _inner.sendNotifiedMessage( connectionID, pData, size, tag ); }
        int32              broadcast( NetChannelKind channel, const uint8* pData, int32 size, int32 exceptID ) override { return _inner.broadcast( channel, pData, size, exceptID ); }
        void               disconnect( int32 connectionID, NetLinger linger ) override { _inner.disconnect( connectionID, linger ); }
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
        optional<int32> getClientIndex() const override { return _inner.getClientIndex(); }
        int32           getConnectedCount() const override { return _inner.getConnectedCount(); }
        void            collectConnected( vector<int32>& outListConnection ) const override { _inner.collectConnected( outListConnection ); }
        bool            isConnectionSaturated( int32 connectionID ) const override { return _inner.isConnectionSaturated( connectionID ); }
        void            addSentKindStats( NetMessageKindStatsTable& outTable ) const override { _inner.addSentKindStats( outTable ); }

    private:
        NetEndpoint&                 _inner;
        unordered_map<int32, uint64> _mapAccount;
    };

    HubRules makeHubLinkTestRules()
    {
        HubRules rules;
        rules._listLevelExperience = { 100, 250, 450, 700 };
        rules._moveSpeed           = 6.0f;
        rules._rideSpeed           = 12.0f;
        rules._listAvatarStart.push_back( RespawnSpawnPoint{} );
        rules._remoteCallBurstCount         = 20; // 씬과 같다
        rules._remoteCallRefillMilliseconds = 50;
        HubPlacement spawn;
        spawn._kind     = HubPlacementKind::Creature;
        spawn._position = float3{ 2.0f, 0.0f, 0.0f };
        rules._listPlacement.push_back( spawn );
        spawn._position = float3{ -2.0f, 0.0f, 1.0f };
        rules._listPlacement.push_back( spawn );
        spawn._kind     = HubPlacementKind::Herb;
        spawn._position = float3{ 0.0f, 0.0f, 1.5f };
        rules._listPlacement.push_back( spawn );
        return rules;
    }

    /** @brief 진행 저장소를 찾는 곳입니다 — 테스트는 늘 같은 메모리 저장소를 돌려준다(전용 서버는 온라인 조립 저장소를 맡길 때마다 찾는다). */
    HubServiceStoreSource makeStoreSource( ServiceStore& store )
    {
        return SW_DELEGATE_LAMBDA( HubServiceStoreSource, [&store]() -> ServiceStore*
        { return &store; } );
    }

    StreamingCellGridDef makeHubLinkTestGrid()
    {
        StreamingCellGridDef grid;
        grid._cellSize      = 32.0f;
        grid._loadingRadius = 96.0f;
        return grid;
    }

    /** @brief 서버 하나와 클라이언트 @p clientCount 개 — 클라이언트 i 의 계정은 1000 + i 입니다. 진행 저장소는 메모리이고 서버를 다시 세워도 데이터는 남습니다. */
    struct HubLinkTestRig
    {
        test::LoopbackCluster       _cluster{ 23u };
        MemoryServiceDatabase       _database;
        MemoryServiceStore          _store{ &_database }; ///< 서버를 다시 세워도 데이터는 남는다(전용 서버의 온라인 조립 저장소 자리)
        HubAccountPeers*            _pServerPeers{ nullptr };
        unique_ptr<HubAccountPeers> _pServerPeersOwner;
        NetMessageRouter            _serverRouter;
        NetMessageRouter            _arrClientRouter[2];
        HubServerLink               _server;
        HubClientLink               _arrClient[2];
        int32                       _clientCount{ 0 };

        explicit HubLinkTestRig( int32 clientCount )
            : _clientCount{ clientCount }
        {
            const NetEndpointConfig settings;
            (void)_cluster.addEndpoint( 4500, settings );
            for ( int32 index = 0; index < clientCount; ++index )
            {
                (void)_cluster.addEndpoint( static_cast<uint16>( 4600 + index ), settings );
            }
            SW_EXPECT_TRUE( _cluster.connectClients( 60 ) );
            _pServerPeersOwner = make_unique<HubAccountPeers>( _cluster.getServer() );
            _pServerPeers      = _pServerPeersOwner.get();
            for ( int32 index = 0; index < clientCount; ++index )
            {
                _pServerPeers->setAccount( _cluster.getClient( index ).getClientIndex().value(), 1000 + static_cast<uint64>( index ) );
                SW_EXPECT_TRUE( _arrClient[index].initialize( _cluster.getClient( index ), _arrClientRouter[index] ) );
                _arrClientRouter[index].setLocalScene( _cluster.getClient( index ), 0 );
            }
            _serverRouter.openWorld( *_pServerPeers, 0 ); // 클라이언트는 알림을 받고 같은 씬(0)으로 들어온다
        }

        [[nodiscard]] bool startServer( HubServerLink& server )
        {
            return server.initialize( *_pServerPeers, _serverRouter, makeHubLinkTestRules(), makeHubLinkTestGrid(), makeStoreSource( _store ) );
        }

        int32 getConnectionID( int32 clientIndex ) { return _cluster.getClient( clientIndex ).getClientIndex().value(); }

        /** @brief 망 · 라우터 · 링크를 @p seconds 만큼 돌립니다(1/60 초 걸음). */
        void run( HubServerLink& server, float32 seconds )
        {
            const int32 stepCount = static_cast<int32>( seconds * 60.0f ) + 1;
            for ( int32 step = 0; step < stepCount; ++step )
            {
                _cluster.step();
                (void)_serverRouter.poll( *_pServerPeers );
                (void)_store.pollCompletions(); // 전용 서버에서는 온라인 조립이 거둔다
                server.update( 1.0f / 60.0f );
                for ( int32 index = 0; index < _clientCount; ++index )
                {
                    (void)_arrClientRouter[index].poll( _cluster.getClient( index ) );
                    _arrClient[index].update();
                }
            }
        }

        /** @brief 짐승을 쓰러질 때까지 세 번 칩니다(재사용 대기 0.5 초를 넘겨 기다린다). */
        void defeat( HubServerLink& server, int32 clientIndex, uint32 creatureID )
        {
            for ( int32 strike = 0; strike < 3; ++strike )
            {
                SW_EXPECT_NOT_EQUAL( 0u, _arrClient[clientIndex].callServerFunction( "attackCreature", { ReflectedValue::make( creatureID ) } ) );
                run( server, 0.6f );
            }
        }
    };
} // namespace

/** @brief [HubNetLinkTest] 계정으로 든 아바타가 반사 함수 호출로 짐승을 쓰러뜨리고, 서버가 판정한 결과 · 진행 · MMO 복제가 클라이언트에 온다. */
SW_TEST_CASE( HubNetLinkTest, ServerAuthorityLoopReachesTheClient )
{
    HubLinkTestRig rig( 1 );
    SW_REQUIRE_TRUE( rig.startServer( rig._server ) );
    rig.run( rig._server, 0.5f );
    const HubAvatarAgent& agent = rig._arrClient[0].getAgent();
    SW_REQUIRE_TRUE( agent._avatarID != 0u );
    SW_EXPECT_EQUAL( 1, agent._shownLevel );
    SW_EXPECT_EQUAL( 1, rig._server.getOnlineCount() );
    SW_EXPECT_TRUE( rig._arrClient[0].getView().findEntity( kNearCreatureID ) != nullptr );

    rig.defeat( rig._server, 0, kNearCreatureID );
    SW_EXPECT_EQUAL( 3, agent._resultCount );
    SW_EXPECT_TRUE( agent._lastResult == HubActionResult::Ok );
    SW_EXPECT_EQUAL( 60, agent._shownExperience );
    SW_EXPECT_EQUAL( 5, agent._shownCoins );
    SW_EXPECT_EQUAL( 1, agent._shownDefeatCount );
    SW_EXPECT_EQUAL( uint64{ 3 }, rig._server.getRemoteCallStats()._invokedCount );
    SW_EXPECT_TRUE( rig._arrClient[0].getView().findEntity( kNearCreatureID ) == nullptr ); // 쓰러진 짐승은 복제에서 빠진다

    // 재사용 대기 중의 공격은 서버가 결과로 거절한다(끊지 않는다).
    SW_EXPECT_NOT_EQUAL( 0u, rig._arrClient[0].callServerFunction( "attackCreature", { ReflectedValue::make( kSideCreatureID ) } ) );
    SW_EXPECT_NOT_EQUAL( 0u, rig._arrClient[0].callServerFunction( "attackCreature", { ReflectedValue::make( kSideCreatureID ) } ) );
    rig.run( rig._server, 0.2f );
    SW_EXPECT_TRUE( agent._lastResult == HubActionResult::CoolingDown );
    // 결과에는 대기가 끝나는 월드 서버 틱이 실린다 — 이 묶음의 서버 시계는 0 에 서 있어 대기 0.5 초(30 틱)가 곧 그 틱이다.
    SW_EXPECT_TRUE( agent._attackReadyTick >= 30u && agent._attackReadyTick <= 32u );
    SW_EXPECT_EQUAL( 1, rig._server.getOnlineCount() );
}

/** @brief [HubNetLinkTest] 가만히 있는 허브는 복제기에 다시 올리지 않고, 쓰러뜨린 짐승만 바뀐 것으로 올린다(감사 K-13). */
SW_TEST_CASE( HubNetLinkTest, OnlyChangedEntitiesArePublished )
{
    HubLinkTestRig rig( 1 );
    SW_REQUIRE_TRUE( rig.startServer( rig._server ) );
    rig.run( rig._server, 0.5f );
    const uint64 settledCount = rig._server.getStats()._publishCount;
    SW_EXPECT_EQUAL( uint64{ 4 }, settledCount ); // 아바타 하나 · 짐승 둘 · 버섯 하나를 한 번씩
    rig.run( rig._server, 0.5f );
    SW_EXPECT_EQUAL( settledCount, rig._server.getStats()._publishCount ); // 30 틱 동안 바뀐 것이 없다

    // 한 번 친 짐승은 체력이 바뀌어 한 번 더 올라간다.
    SW_EXPECT_NOT_EQUAL( 0u, rig._arrClient[0].callServerFunction( "attackCreature", { ReflectedValue::make( kNearCreatureID ) } ) );
    rig.run( rig._server, 0.3f );
    SW_EXPECT_EQUAL( settledCount + 1u, rig._server.getStats()._publishCount );
}

/** @brief [HubNetLinkTest] 진행 저장소는 저장소 바이트(읽었거나 쓴 것)와 같은 진행을 다시 쓰지 않는다 — 자동 저장이 그대로인 진행을 쓰지 않는다(감사 K-13). */
SW_TEST_CASE( HubNetLinkTest, UnchangedProfileIsNotWrittenAgain )
{
    MemoryServiceDatabase  database;
    MemoryServiceStore     store( &database );
    HubProfileStore        profiles;
    vector<HubProfileLoad> listLoad;
    profiles.bindSource( makeStoreSource( store ) );
    profiles.requestLoad( 7 );
    (void)store.pollCompletions();
    profiles.poll( listLoad );
    SW_REQUIRE_EQUAL( size_t{ 1 }, listLoad.size() );
    SW_REQUIRE_TRUE( listLoad[0]._bOk == SW_TRUE && listLoad[0]._bFound == SW_FALSE ); // 새 계정
    HubProfile profile;
    profile._coins = 5;
    SW_EXPECT_TRUE( profiles.requestSave( 7, profile ) );
    SW_EXPECT_FALSE( profiles.requestSave( 7, profile ) ); // 쓰는 중인 바이트와 같다
    SW_EXPECT_EQUAL( uint64{ 1 }, profiles.getSkippedSaveCount() );
    profile._coins = 6;
    SW_EXPECT_TRUE( profiles.requestSave( 7, profile ) ); // 앞 쓰기가 끝나면 그 판으로 맡긴다
    for ( int32 round = 0; round < 3; ++round )
    {
        (void)store.pollCompletions();
        profiles.poll( listLoad );
    }
    SW_EXPECT_EQUAL( uint64{ 2 }, profiles.getSavedCount() );
    SW_EXPECT_EQUAL( 0, profiles.getPendingCount() );

    // 다른 저장소 객체(서버 재시작)는 읽은 바이트를 저장소 값으로 안다 — 읽은 그대로는 쓰지 않는다.
    listLoad.clear();
    HubProfileStore restarted;
    restarted.bindSource( makeStoreSource( store ) );
    restarted.requestLoad( 7 );
    (void)store.pollCompletions();
    restarted.poll( listLoad );
    SW_REQUIRE_EQUAL( size_t{ 1 }, listLoad.size() );
    SW_REQUIRE_TRUE( listLoad[0]._bFound == SW_TRUE );
    SW_EXPECT_EQUAL( 6, listLoad[0]._profile._coins );
    SW_EXPECT_FALSE( restarted.requestSave( 7, listLoad[0]._profile ) );
    SW_EXPECT_LOG( "Hub.ProfileSaveUnread" );
    SW_EXPECT_FALSE( restarted.requestSave( 8, listLoad[0]._profile ) ); // 읽지 않은 계정은 판을 몰라 쓰지 않는다
}

/** @brief [HubNetLinkTest] 서버 둘이 한 계정을 쓰면 뒤 서버의 쓰기는 판 조건에 걸려 앞 서버의 진행을 덮지 않는다(저장소 계약 — 서버 여럿이 DB 하나를 나눈다). */
SW_TEST_CASE( HubNetLinkTest, SecondServerDoesNotOverwriteNewerProgress )
{
    MemoryServiceDatabase  database;
    MemoryServiceStore     storeA( &database );
    MemoryServiceStore     storeB( &database );
    HubProfileStore        serverA;
    HubProfileStore        serverB;
    vector<HubProfileLoad> listLoad;
    serverA.bindSource( makeStoreSource( storeA ) );
    serverB.bindSource( makeStoreSource( storeB ) );
    serverA.requestLoad( 7 );
    serverB.requestLoad( 7 );
    (void)storeA.pollCompletions();
    (void)storeB.pollCompletions();
    serverA.poll( listLoad );
    serverB.poll( listLoad );
    SW_REQUIRE_EQUAL( size_t{ 2 }, listLoad.size() );
    HubProfile profile;
    profile._coins = 10;
    SW_REQUIRE_TRUE( serverA.requestSave( 7, profile ) );
    (void)storeA.pollCompletions();
    serverA.poll( listLoad );
    SW_EXPECT_LOG( "Hub.ProfileSaveFailed" );
    profile._coins = 1;
    SW_REQUIRE_TRUE( serverB.requestSave( 7, profile ) ); // B 는 새 계정(없음)으로 읽었다 — "없어야 한다" 조건이 걸린다
    (void)storeB.pollCompletions();
    serverB.poll( listLoad );
    SW_EXPECT_EQUAL( uint64{ 0 }, serverB.getSavedCount() );

    HubProfileStore reader;
    listLoad.clear();
    reader.bindSource( makeStoreSource( storeA ) );
    reader.requestLoad( 7 );
    (void)storeA.pollCompletions();
    reader.poll( listLoad );
    SW_REQUIRE_EQUAL( size_t{ 1 }, listLoad.size() );
    SW_EXPECT_EQUAL( 10, listLoad[0]._profile._coins );
}

/** @brief [HubNetLinkTest] 세션이 끝난 계정의 기억은 마지막 쓰기가 끝나면 지운다 — 들고 있는 바이트가 접속 중인 계정 수를 따른다 */
SW_TEST_CASE( HubNetLinkTest, ReleasedAccountIsForgottenAfterItsLastWrite )
{
    MemoryServiceDatabase  database;
    MemoryServiceStore     store( &database );
    HubProfileStore        profiles;
    vector<HubProfileLoad> listLoad;
    profiles.bindSource( makeStoreSource( store ) );
    profiles.requestLoad( 7 );
    profiles.requestLoad( 8 );
    (void)store.pollCompletions();
    profiles.poll( listLoad );
    HubProfile profile;
    profile._coins = 5;
    SW_REQUIRE_TRUE( profiles.requestSave( 7, profile ) );
    profiles.releaseAccount( 7 );
    SW_EXPECT_EQUAL( size_t{ 2 }, profiles.getKnownAccountCount() ); // 7 의 쓰기가 아직 거두어지지 않았다

    (void)store.pollCompletions();
    profiles.poll( listLoad );
    SW_EXPECT_EQUAL( size_t{ 1 }, profiles.getKnownAccountCount() );
    profiles.releaseAccount( 8 ); // 쓰는 중이 아니면 바로 지운다
    SW_EXPECT_EQUAL( size_t{ 0 }, profiles.getKnownAccountCount() );
}

/** @brief [HubNetLinkTest] 파티 초대 · 수락이 서버에서 서고, 쓰러뜨린 짐승의 경험을 두 클라이언트가 나눠 받는다. */
SW_TEST_CASE( HubNetLinkTest, PartyIsServerAuthoritative )
{
    HubLinkTestRig rig( 2 );
    SW_REQUIRE_TRUE( rig.startServer( rig._server ) );
    rig.run( rig._server, 0.5f );
    const HubAvatarAgent& leader = rig._arrClient[0].getAgent();
    const HubAvatarAgent& member = rig._arrClient[1].getAgent();
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

/** @brief [HubNetLinkTest] 검증 짝이 지어낸 번호 · 자기 초대를 거르면 서버가 그 연결을 끊는다 — 몸(허브 규칙)은 돌지 않는다. */
SW_TEST_CASE( HubNetLinkTest, CraftedTargetsAreDisconnected )
{
    HubLinkTestRig rig( 1 );
    SW_REQUIRE_TRUE( rig.startServer( rig._server ) );
    rig.run( rig._server, 0.5f );
    SW_REQUIRE_TRUE( rig._arrClient[0].getAgent()._avatarID != 0u );
    const int32 connectionID = rig.getConnectionID( 0 ); // 끊긴 뒤에는 클라이언트 번호가 없다 — 끊기 전에 뜬다
    SW_REQUIRE_TRUE( rig._cluster.getServer().getConnectionState( connectionID ) == NetConnectionState::Connected );
    SW_EXPECT_LOG( "RemoteCall.ValidationFailed" );
    SW_EXPECT_NOT_EQUAL( 0u, rig._arrClient[0].callServerFunction( "attackCreature", { ReflectedValue::make( uint32{ 7 } ) } ) );
    for ( int32 step = 0; step < 8; ++step )
    {
        rig._cluster.step();
        (void)rig._serverRouter.poll( *rig._pServerPeers ); // 서버만 — 끊긴 클라이언트 쪽 사건은 이 테스트가 보지 않는다
    }
    SW_EXPECT_EQUAL( uint64{ 1 }, rig._server.getRemoteCallStats()._validationFailedCount );
    SW_EXPECT_EQUAL( uint64{ 0 }, rig._server.getStats()._actionCount );
    SW_EXPECT_TRUE( rig._cluster.getServer().getConnectionState( connectionID ) != NetConnectionState::Connected );
}

/** @brief [HubNetLinkTest] 접속이 끝나면 진행을 저장하고, 같은 계정이 다시 들면(서버를 다시 세워도) 그 진행을 읽는다. */
SW_TEST_CASE( HubNetLinkTest, ProgressSurvivesReconnectAndServerRestart )
{
    HubLinkTestRig rig( 1 );
    SW_REQUIRE_TRUE( rig.startServer( rig._server ) );
    rig.run( rig._server, 0.5f );
    rig.defeat( rig._server, 0, kNearCreatureID );
    const HubAvatarAgent& agent = rig._arrClient[0].getAgent();
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
    HubServerLink restarted;
    SW_REQUIRE_TRUE( rig.startServer( restarted ) );
    rig.run( restarted, 0.3f );
    SW_EXPECT_EQUAL( uint64{ 1 }, restarted.getStats()._admittedCount );
    SW_EXPECT_EQUAL( 60, agent._shownExperience );
    SW_EXPECT_EQUAL( 1, agent._shownDefeatCount );
    restarted.shutdown();
}

/** @brief [HubNetLinkTest] 주체(티켓의 계정)가 없는 연결은 경고하고 끊는다 — 계정이 있는 연결은 그대로 든다. */
SW_TEST_CASE( HubNetLinkTest, ConnectionWithoutAccountIsDisconnected )
{
    HubLinkTestRig rig( 2 );
    const int32    accountConnectionID = rig.getConnectionID( 0 );
    const int32    guestConnectionID   = rig.getConnectionID( 1 );
    rig._pServerPeers->setAccount( guestConnectionID, 0 );
    SW_EXPECT_LOG( "Hub.NoAccount" );
    SW_REQUIRE_TRUE( rig.startServer( rig._server ) ); // 붙어 있던 연결을 들인다
    for ( int32 step = 0; step < 8; ++step )
    {
        rig._cluster.step();
        (void)rig._serverRouter.poll( *rig._pServerPeers ); // 서버만 — 끊긴 클라이언트 쪽 사건은 이 테스트가 보지 않는다
        (void)rig._store.pollCompletions();
        rig._server.update( 1.0f / 60.0f );
    }
    SW_EXPECT_EQUAL( uint64{ 1 }, rig._server.getStats()._refusedCount );
    SW_EXPECT_EQUAL( uint64{ 1 }, rig._server.getStats()._admittedCount );
    SW_EXPECT_EQUAL( 1, rig._server.getOnlineCount() );
    SW_EXPECT_TRUE( rig._cluster.getServer().getConnectionState( guestConnectionID ) != NetConnectionState::Connected );
    SW_EXPECT_TRUE( rig._cluster.getServer().getConnectionState( accountConnectionID ) == NetConnectionState::Connected );
}
