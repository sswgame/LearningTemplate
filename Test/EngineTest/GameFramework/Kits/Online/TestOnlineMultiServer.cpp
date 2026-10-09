// 서버 여럿 — 두 서비스 호스트가 저장소(메모리 서비스 DB) · 캐시(메모리 휘발 DB) · 버스(프로세스 안 허브)를 나눠 쓴다.
// 같은 계정이 다른 서버에 로그인하면 옛 서버의 연결이 버스로 바로 닫히고(세션 다시 읽기를 기다리지 않는다), 운영 끊기의 사유 코드가 다른 서버까지 가고,
// 다른 서버에 붙은 상대와 거래가 끝까지 가며(초대 · 닫힘 알림이 버스로 건너간다), 캐시가 비어도 다음 다시 적기 주기에 접속 상태가 돌아온다.
// 접속 상태 창구는 계정 id · 이름으로 붙은 서버를 찾고, 같이 찾는 둘이 서로의 결과를 가져가지 않는다.
#include "pch.h"

#include "Core/Network/Transport/LoopbackStreamTransport.h"

#include "Engine/Network/EngineNetSecurity.h"

#include "GameFramework/Base/Online/Bus/LocalServerBus.h"
#include "GameFramework/Base/Online/Cache/MemoryEphemeralStore.h"
#include "GameFramework/Base/Online/Ledger/Ledger.h"
#include "GameFramework/Base/Online/Ledger/LedgerAudit.h"
#include "GameFramework/Base/Online/Service/OnlineServiceClient.h"
#include "GameFramework/Base/Online/Service/OnlineServiceHost.h"
#include "GameFramework/Base/Online/Store/MemoryServiceStore.h"
#include "GameFramework/Base/Online/Store/ServiceKeyUtil.h"
#include "GameFramework/Kits/Online/Account/Api/AccountClient.h"
#include "GameFramework/Kits/Online/Server/Account/NetSecurityLoginCrypto.h"
#include "GameFramework/Kits/Online/Server/Account/Service/AccountServer.h"
#include "GameFramework/Kits/Online/Server/Account/Service/LoginService.h"
#include "GameFramework/Kits/Online/Server/Trade/TradeServer.h"
#include "GameFramework/Kits/Online/Trade/TradeClient.h"

#include "TestFramework/TestFramework.h"

using namespace sw;

namespace
{
    constexpr uint16 kFirstPort = 7341;

    struct ServerNode
    {
        MemoryServiceStore           _store;
        MemoryEphemeralStore         _cache;
        LocalServerBus               _bus;
        NetSecurityLoginCrypto       _crypto;
        LoginService                 _loginService;
        AccountServer                _accountServer;
        DefaultTradePolicy           _tradePolicy;
        TradeService                 _tradeService;
        TradeServer                  _tradeServer;
        unique_ptr<IStreamTransport> _transport;
        OnlineServiceHost            _host;

        ServerNode( LoopbackStreamNetwork& network, MemoryServiceDatabase& database, MemoryEphemeralDatabase& cacheDatabase, LocalServerBusHub& hub, uint64 serverId )
            : _store{ &database }
            , _cache{ &cacheDatabase }
            , _bus{ &hub, serverId }
            , _crypto{ &EngineNetSecurity::getProvider() }
            , _loginService{}
            , _accountServer{}
            , _tradePolicy{}
            , _tradeService{}
            , _tradeServer{}
            , _transport{ network.createTransport() }
            , _host{}
        {
            LoginSettings settings;
            settings._passwordHashParams._memoryKiB                        = 256;
            settings._passwordHashParams._iterationCount                   = 1;
            const uint8 arrMasterKey[LoginTicketAuthority::kMasterKeySize] = { 5, 5, 5 };
            _loginService.initialize( &_store, &_crypto, settings, arrMasterKey );
            AccountServerSettings accountSettings;
            accountSettings._refreshIntervalMs           = 1000000000; // 세션 다시 읽기는 끈다 — 버스만으로 닫혀야 한다
            accountSettings._presence._refreshIntervalMs = 1000;
            _accountServer.initialize( &_loginService, accountSettings );
            _tradeService.initialize( &_store, &_tradePolicy, nullptr, serverId, TradeSettings{} );
            _tradeServer.initialize( &_tradeService, &_loginService, _accountServer.getPresence() );
            SW_EXPECT_TRUE( _host.registerService( &_accountServer ) );
            SW_EXPECT_TRUE( _host.registerService( &_tradeServer ) );
            OnlineServiceHostSettings hostSettings;
            hostSettings._transportSettings._ioThreadCount = 0;
            hostSettings._listenAddress                    = NetAddress::makeLoopback( static_cast<uint16>( kFirstPort + serverId ) );
            hostSettings._pServiceStore                    = &_store;
            hostSettings._pEphemeralStore                  = &_cache;
            hostSettings._pServerBus                       = &_bus;
            hostSettings._requestBurstPerRemote            = 1000;
            hostSettings._requestBurstPerAccount           = 1000;
            string error;
            SW_EXPECT_TRUE_MSG( _host.initialize( _transport.get(), hostSettings, error ), error.c_str() );
        }

        ~ServerNode()
        {
            _store.shutdown();
            _host.shutdown(); // 캐시 답을 기다리는 요청이 여기서 Unavailable 로 끝난다 — 서비스보다 먼저
            (void)_store.pollCompletions();
            _tradeServer.shutdown();
            _tradeService.shutdown();
            _accountServer.shutdown();
            _loginService.shutdown();
            _cache.shutdown();
        }
    };

    struct ClientNode
    {
        unique_ptr<IStreamTransport> _transport;
        AccountClient                _account;
        TradeClient                  _trade;
        OnlineServiceClient          _client;

        ClientNode( LoopbackStreamNetwork& network, uint64 serverId )
            : _transport{ network.createTransport() }
            , _account{}
            , _trade{}
            , _client{}
        {
            _account.initialize( &_client, AccountClientInfo{} );
            _trade.initialize( &_client );
            SW_EXPECT_TRUE( _client.registerClientService( &_account ) );
            SW_EXPECT_TRUE( _client.registerClientService( &_trade ) );
            OnlineServiceClientSettings settings;
            settings._transportSettings._ioThreadCount = 0;
            settings._serverAddress                    = NetAddress::makeLoopback( static_cast<uint16>( kFirstPort + serverId ) );
            settings._maxBackoffMs                     = 200;
            string error;
            SW_EXPECT_TRUE_MSG( _client.initialize( _transport.get(), settings, error ), error.c_str() );
        }

        ~ClientNode() { _client.shutdown(); }
    };

    struct MultiServerRig
    {
        LoopbackStreamNetwork   _network;
        MemoryServiceDatabase   _database;
        MemoryEphemeralDatabase _cacheDatabase;
        LocalServerBusHub       _hub;
        ServerNode              _serverA;
        ServerNode              _serverB;
        ClientNode              _aliceOnA;
        ClientNode              _bobOnB;
        ClientNode              _aliceOnB;
        int64                   _nowMs;

        MultiServerRig()
            : _network{ 43u }
            , _database{}
            , _cacheDatabase{}
            , _hub{}
            , _serverA{ _network, _database, _cacheDatabase, _hub, 1 }
            , _serverB{ _network, _database, _cacheDatabase, _hub, 2 }
            , _aliceOnA{ _network, 1 }
            , _bobOnB{ _network, 2 }
            , _aliceOnB{ _network, 2 }
            , _nowMs{ 800000 }
        {
            _cacheDatabase.setManualTimeMs( _nowMs );
        }

        void step( int32 count = 1 )
        {
            for ( int32 index = 0; index < count; ++index )
            {
                _nowMs += 20;
                _cacheDatabase.advanceTimeMs( 20 );
                _aliceOnA._client.tick( _nowMs );
                _bobOnB._client.tick( _nowMs );
                _aliceOnB._client.tick( _nowMs );
                _serverA._host.tick( _nowMs );
                _serverB._host.tick( _nowMs );
            }
        }

        AccountClientReply waitAccount( ClientNode& client, uint64 requestId )
        {
            for ( int32 attempt = 0; attempt < 400; ++attempt )
            {
                vector<AccountClientReply> listReply;
                (void)client._account.pollReplies( listReply );
                for ( AccountClientReply& reply : listReply )
                {
                    if ( reply._requestId == requestId )
                        return std::move( reply );
                }
                step();
            }
            return AccountClientReply{};
        }

        TradeClientReply waitTrade( ClientNode& client, uint64 requestId )
        {
            for ( int32 attempt = 0; attempt < 400; ++attempt )
            {
                vector<TradeClientReply> listReply;
                (void)client._trade.pollReplies( listReply );
                for ( TradeClientReply& reply : listReply )
                {
                    if ( reply._requestId == requestId )
                        return std::move( reply );
                }
                step();
            }
            TradeClientReply lost;
            lost._result = TradeResult::Invalid;
            return lost;
        }

        /** @brief 앨리스는 A, 밥은 B 에 가입 · 로그인합니다. 계정 id 를 채운다. */
        bool loginAliceOnAAndBobOnB( AccountId& outAliceId, AccountId& outBobId )
        {
            for ( int32 attempt = 0; attempt < 400 && ( _aliceOnA._client.isReady() == false || _bobOnB._client.isReady() == false ); ++attempt )
            {
                step();
            }
            const bool bRegistered = waitAccount( _aliceOnA, _aliceOnA._account.registerAccount( "alice", "password123" ) )._result == LoginResult::Ok &&
                                     waitAccount( _bobOnB, _bobOnB._account.registerAccount( "bob", "password123" ) )._result == LoginResult::Ok;
            const AccountClientReply aliceLogin = waitAccount( _aliceOnA, _aliceOnA._account.login( "alice", "password123" ) );
            const AccountClientReply bobLogin   = waitAccount( _bobOnB, _bobOnB._account.login( "bob", "password123" ) );
            outAliceId                          = aliceLogin._grant._identity._accountId;
            outBobId                            = bobLogin._grant._identity._accountId;
            _aliceOnA._trade.setAccountId( outAliceId );
            _bobOnB._trade.setAccountId( outBobId );
            step( 4 ); // 접속 상태가 캐시에 적힌다
            return bRegistered && aliceLogin._result == LoginResult::Ok && bobLogin._result == LoginResult::Ok;
        }

        void grant( AccountId accountId, const utf8* pAsset, int64 amount )
        {
            LedgerTransferRequest request;
            request._journalKey = string( "test/grant." ) + pAsset + "." + ServiceKeyUtil::makeHex64( accountId );
            request._reason     = "test.grant";
            request._listPosting.push_back( LedgerPosting{ LedgerHolder::makeMint(), LedgerHolder::makeAccount( accountId ), pAsset, amount } );
            LedgerTransferOutcome outcome;
            (void)Ledger::executeTransfer( _database, request, outcome );
        }

        int64 readAmount( AccountId accountId, const utf8* pAsset )
        {
            LedgerBalance balance;
            // 실패면 balance 가 0 으로 남아 호출한 단언이 틀린 값으로 잡는다
            (void)Ledger::readBalance( _database, LedgerHolder::makeAccount( accountId ), pAsset, balance );
            return balance._amount;
        }

        static bool hasUpdateKind( const vector<TradeClientUpdate>& listUpdate, uint16 kind )
        {
            for ( const TradeClientUpdate& update : listUpdate )
            {
                if ( update._kind == kind )
                    return true;
            }
            return false;
        }
    };

    /** @brief 접속 상태 찾기 결과를 모읍니다(서비스 하나의 몫). */
    struct PresenceRecorder
    {
        vector<AccountPresenceResult> _listResult{};

        void onFound( const AccountPresenceResult& result ) { _listResult.push_back( result ); }
    };
} // namespace

SW_TEST_CASE( OnlineMultiServerTest, LoginOnAnotherServerClosesTheOldConnectionThroughTheBus )
{
    MultiServerRig rig;
    AccountId      aliceId = kInvalidAccountId;
    AccountId      bobId   = kInvalidAccountId;
    SW_ASSERT_TRUE( rig.loginAliceOnAAndBobOnB( aliceId, bobId ) );
    SW_ASSERT_TRUE( rig._serverA._accountServer.findSessionId( aliceId ) != 0 );

    for ( int32 attempt = 0; attempt < 400 && rig._aliceOnB._client.isReady() == false; ++attempt )
    {
        rig.step();
    }
    const AccountClientReply secondLogin = rig.waitAccount( rig._aliceOnB, rig._aliceOnB._account.login( "alice", "password123" ) );
    SW_ASSERT_TRUE( secondLogin._result == LoginResult::Ok );
    vector<AccountClientEvent> listEvent;
    for ( int32 attempt = 0; attempt < 50 && listEvent.empty(); ++attempt )
    {
        rig.step();
        (void)rig._aliceOnA._account.pollEvents( listEvent );
    }
    SW_ASSERT_EQUAL( size_t( 1 ), listEvent.size() ); // 세션 다시 읽기는 꺼져 있다 — 버스가 닫았다
    SW_EXPECT_EQUAL( int32( LoginRevokeReason::DuplicateLogin ), int32( listEvent[0]._reason ) );
    SW_EXPECT_EQUAL( uint64( 0 ), rig._serverA._accountServer.findSessionId( aliceId ) );
    SW_EXPECT_EQUAL( secondLogin._grant._token._sessionId, rig._serverB._accountServer.findSessionId( aliceId ) );
    rig.step( 4 );
    SW_EXPECT_TRUE( rig._aliceOnB._account.isLoggedIn() ); // 옛 서버의 떠남이 새 서버의 접속 표시를 지우지 않는다
    AccountIdentity found;
    SW_EXPECT_TRUE( rig._serverB._loginService.findIdentityByDisplayName( "alice", found ) );
}

SW_TEST_CASE( OnlineMultiServerTest, AdministrativeRevokeOnOneServerReachesTheOtherWithItsReasonCode )
{
    MultiServerRig rig;
    AccountId      aliceId = kInvalidAccountId;
    AccountId      bobId   = kInvalidAccountId;
    SW_ASSERT_TRUE( rig.loginAliceOnAAndBobOnB( aliceId, bobId ) );
    rig._serverA._accountServer.revokeAccountSessions( bobId, "gm.kick", rig._nowMs ); // 밥은 B 에 붙어 있다
    vector<AccountClientEvent> listEvent;
    for ( int32 attempt = 0; attempt < 50 && listEvent.empty(); ++attempt )
    {
        rig.step();
        (void)rig._bobOnB._account.pollEvents( listEvent );
    }
    SW_ASSERT_EQUAL( size_t( 1 ), listEvent.size() );
    SW_EXPECT_EQUAL( int32( LoginRevokeReason::Administrative ), int32( listEvent[0]._reason ) );
    SW_EXPECT_TRUE( listEvent[0]._reasonCode == "gm.kick" );
    SW_EXPECT_EQUAL( uint64( 0 ), rig._serverB._accountServer.findSessionId( bobId ) );
}

SW_TEST_CASE( OnlineMultiServerTest, TradeWithAPeerOnAnotherServerSettlesAndBothSidesArePushed )
{
    MultiServerRig rig;
    AccountId      aliceId = kInvalidAccountId;
    AccountId      bobId   = kInvalidAccountId;
    SW_ASSERT_TRUE( rig.loginAliceOnAAndBobOnB( aliceId, bobId ) );
    rig.grant( aliceId, "item.sword", 1 );
    rig.grant( bobId, "cur.gold", 500 );

    const TradeClientReply invited = rig.waitTrade( rig._aliceOnA, rig._aliceOnA._trade.invite( "BOB" ) ); // A 의 디렉터리에 없다 — 캐시로 찾는다
    SW_ASSERT_EQUAL( int32( TradeResult::Ok ), int32( invited._result ) );
    const uint64 tradeId = invited._snapshot._tradeId;
    rig.step( 6 );
    vector<TradeClientUpdate> listBobUpdate;
    (void)rig._bobOnB._trade.pollUpdates( listBobUpdate );
    SW_ASSERT_TRUE( MultiServerRig::hasUpdateKind( listBobUpdate, TradeMethod::kPushInvited ) ); // 버스로 건너온 초대 알림

    SW_ASSERT_TRUE( rig.waitTrade( rig._bobOnB, rig._bobOnB._trade.respond( tradeId, true ) )._result == TradeResult::Ok );
    rig.step( 6 );
    const vector<TradeLeg> listAliceLeg = {
        TradeLeg{ "item.sword", 1 }
    };
    const vector<TradeLeg> listBobLeg = {
        TradeLeg{ "cur.gold", 300 }
    };
    SW_ASSERT_TRUE( rig.waitTrade( rig._aliceOnA, rig._aliceOnA._trade.setOffer( tradeId, listAliceLeg ) )._result == TradeResult::Ok );
    rig.step( 6 );
    SW_ASSERT_TRUE( rig.waitTrade( rig._bobOnB, rig._bobOnB._trade.setOffer( tradeId, listBobLeg ) )._result == TradeResult::Ok );
    rig.step( 6 );
    SW_ASSERT_TRUE( rig.waitTrade( rig._aliceOnA, rig._aliceOnA._trade.lock( tradeId ) )._result == TradeResult::Ok );
    rig.step( 6 );
    SW_ASSERT_TRUE( rig.waitTrade( rig._bobOnB, rig._bobOnB._trade.lock( tradeId ) )._result == TradeResult::Ok );
    rig.step( 6 ); // 두 쪽 모두 상대의 잠금 · 판을 알림으로 봤다
    SW_ASSERT_TRUE( rig.waitTrade( rig._aliceOnA, rig._aliceOnA._trade.confirm( tradeId ) )._result == TradeResult::Ok );
    rig.step( 6 );
    const TradeClientReply settled = rig.waitTrade( rig._bobOnB, rig._bobOnB._trade.confirm( tradeId ) );
    SW_ASSERT_EQUAL( int32( TradeResult::Ok ), int32( settled._result ) );
    SW_EXPECT_TRUE( settled._snapshot._state == TradeState::Settled );
    rig.step( 6 );
    vector<TradeClientUpdate> listAliceUpdate;
    (void)rig._aliceOnA._trade.pollUpdates( listAliceUpdate );
    SW_ASSERT_TRUE( listAliceUpdate.empty() == false );
    SW_EXPECT_EQUAL( TradeMethod::kPushClosed, listAliceUpdate.back()._kind ); // B 에서 정산 — A 의 앨리스에게 버스로
    SW_EXPECT_EQUAL( int64( 1 ), rig.readAmount( bobId, "item.sword" ) );
    SW_EXPECT_EQUAL( int64( 300 ), rig.readAmount( aliceId, "cur.gold" ) );
    LedgerAuditReport report;
    SW_ASSERT_TRUE( LedgerAudit::computeReport( rig._database, report ) == ServiceStoreResult::Ok );
    SW_EXPECT_TRUE( report.isBalanced() );
}

SW_TEST_CASE( OnlineMultiServerTest, WipedCacheComesBackOnTheNextPresenceRefresh )
{
    MultiServerRig rig;
    AccountId      aliceId = kInvalidAccountId;
    AccountId      bobId   = kInvalidAccountId;
    SW_ASSERT_TRUE( rig.loginAliceOnAAndBobOnB( aliceId, bobId ) );
    rig._cacheDatabase.clearData(); // 캐시 서버가 다시 떴다
    const TradeClientReply lost = rig.waitTrade( rig._aliceOnA, rig._aliceOnA._trade.invite( "bob" ) );
    SW_EXPECT_EQUAL( int32( TradeResult::PeerOffline ), int32( lost._result ) );
    rig.step( 60 ); // 다시 적기 주기(시험 1 초) 를 넘긴다
    const TradeClientReply found = rig.waitTrade( rig._aliceOnA, rig._aliceOnA._trade.invite( "bob" ) );
    SW_EXPECT_EQUAL( int32( TradeResult::Ok ), int32( found._result ) );
    SW_EXPECT_TRUE( found._snapshot.findSideIndex( bobId ) >= 0 );
}

SW_TEST_CASE( OnlineMultiServerTest, PresenceFindsTheServerOfAnAccountByIdAndByName )
{
    MultiServerRig rig;
    AccountId      aliceId = kInvalidAccountId;
    AccountId      bobId   = kInvalidAccountId;
    SW_ASSERT_TRUE( rig.loginAliceOnAAndBobOnB( aliceId, bobId ) );
    IAccountPresence* pPresence = rig._serverA._accountServer.getPresence();
    // 두 서비스가 같은 창구로 같이 찾는다 — 각자 맡긴 결과만 받는다.
    PresenceRecorder chatRecorder;
    PresenceRecorder socialRecorder;
    const uint64     bobLookup     = pPresence->submitFindByAccount( bobId, AccountPresenceDelegate::create<&PresenceRecorder::onFound>( &chatRecorder ) );
    const uint64     aliceLookup   = pPresence->submitFindByAccount( aliceId, AccountPresenceDelegate::create<&PresenceRecorder::onFound>( &socialRecorder ) );
    const uint64     nameLookup    = pPresence->submitFindByDisplayName( "BOB", AccountPresenceDelegate::create<&PresenceRecorder::onFound>( &socialRecorder ) );
    const uint64     offlineLookup = pPresence->submitFindByAccount( 0xDEAD, AccountPresenceDelegate::create<&PresenceRecorder::onFound>( &chatRecorder ) );
    SW_EXPECT_TRUE( chatRecorder._listResult.empty() ); // 맡긴 자리에서 부르지 않는다
    rig.step( 2 );

    SW_ASSERT_EQUAL( size_t( 2 ), chatRecorder._listResult.size() );
    SW_EXPECT_EQUAL( bobLookup, chatRecorder._listResult[0]._requestId );
    SW_EXPECT_EQUAL( bobId, chatRecorder._listResult[0]._identity._accountId );
    SW_EXPECT_EQUAL( uint64( 2 ), chatRecorder._listResult[0]._serverId );
    SW_EXPECT_EQUAL( offlineLookup, chatRecorder._listResult[1]._requestId );
    SW_EXPECT_FALSE( chatRecorder._listResult[1].isOnline() );
    SW_ASSERT_EQUAL( size_t( 2 ), socialRecorder._listResult.size() );
    SW_EXPECT_EQUAL( aliceLookup, socialRecorder._listResult[0]._requestId );
    SW_EXPECT_EQUAL( uint64( 1 ), socialRecorder._listResult[0]._serverId ); // 이 서버
    SW_EXPECT_EQUAL( nameLookup, socialRecorder._listResult[1]._requestId );
    SW_EXPECT_EQUAL( bobId, socialRecorder._listResult[1]._identity._accountId );
    SW_EXPECT_TRUE( socialRecorder._listResult[1]._identity._displayName == "bob" );
    SW_EXPECT_EQUAL( uint64( 2 ), socialRecorder._listResult[1]._serverId );
}
