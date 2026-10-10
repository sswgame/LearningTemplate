// 경제 클라이언트 — 루프백 스트림 위 계정 + 경제 서비스(메모리 저장소). 원장 응답이 로컬 지갑을 덮어쓴다(로컬 지갑은 원장의 읽기 사본 — kit-compose 결정 5),
// 같은 멱등 키 재시도는 한 번만 차감, 기능 플래그가 꺼지면 구매가 kFeatureDisabled, 로그인 전 요청은 NotSignedIn.
#include "pch.h"

#include "Core/Network/Transport/LoopbackStreamTransport.h"

#include "GameFramework/Base/Gameplay/Inventory/Shop.h"
#include "GameFramework/Base/Online/Audit/ServiceAuditLog.h"
#include "GameFramework/Base/Online/Config/RemoteConfig.h"
#include "GameFramework/Base/Online/Ledger/Ledger.h"
#include "GameFramework/Base/Online/Security/NetSecurity.h"
#include "GameFramework/Base/Online/Service/OnlineServiceClient.h"
#include "GameFramework/Base/Online/Service/OnlineServiceHost.h"
#include "GameFramework/Base/Online/Store/MemoryServiceStore.h"
#include "GameFramework/Base/Online/Store/ServiceKeyUtil.h"
#include "GameFramework/Kits/Feature/Online/Account/Server/NetSecurityLoginCrypto.h"
#include "GameFramework/Kits/Feature/Online/Account/Server/Service/AccountServer.h"
#include "GameFramework/Kits/Feature/Online/Account/Server/Service/LoginService.h"
#include "GameFramework/Kits/Feature/Online/Account/Shared/API/AccountClient.h"
#include "GameFramework/Kits/Feature/Online/Economy/Server/EconomyService.h"
#include "GameFramework/Kits/Feature/Online/Economy/Shared/API/EconomyClient.h"
#include "GameFramework/Kits/Feature/Online/Economy/Shared/Catalog/CurrencyCatalog.h"
#include "GameFramework/Kits/Feature/Online/Economy/Shared/Catalog/OfferCatalog.h"

#include "TestFramework/TestFramework.h"

using namespace sw;

namespace
{
    struct EconomyServerSide
    {
        static constexpr uint16 kPort = 7331;

        MemoryServiceStore           _store;
        NetSecurityLoginCrypto       _crypto;
        LoginService                 _loginService;
        AccountServer                _accountServer;
        CurrencyCatalog              _currencies;
        OfferCatalog                 _offers;
        EconomyService               _economy;
        unique_ptr<IStreamTransport> _transport;
        OnlineServiceHost            _host;

        EconomyServerSide( LoopbackStreamNetwork& network, MemoryServiceDatabase& database, RemoteConfig* pRemoteConfig )
            : _store{ &database }
            , _crypto{ &NetSecurity::getProvider() }
            , _loginService{}
            , _accountServer{}
            , _currencies{}
            , _offers{}
            , _economy{}
            , _transport{ network.createTransport() }
            , _host{}
        {
            LoginSettings settings;
            settings._passwordHashParams._memoryKiB                        = 256;
            settings._passwordHashParams._iterationCount                   = 1;
            const uint8 arrMasterKey[LoginTicketAuthority::kMasterKeySize] = { 5, 5, 5 };
            _loginService.initialize( &_store, &_crypto, settings, arrMasterKey );
            _accountServer.initialize( &_loginService, AccountServerSettings{} );
            SW_EXPECT_TRUE( _currencies.loadFromXMLText( R"(<CurrencyCatalog><Currency id="cur.gold"/></CurrencyCatalog>)", "currency" ) );
            SW_EXPECT_TRUE( _offers.loadFromXMLText(
                R"(<OfferCatalog><Offer id="potion" maxCount="5"><Price currency="cur.gold" amount="20"/><Grant asset="item.potion" amount="1"/></Offer></OfferCatalog>)",
                "offer" ) );
            EconomyServiceSettings economySettings;
            economySettings._pCurrencyCatalog = &_currencies;
            economySettings._pOfferCatalog    = &_offers;
            SW_EXPECT_TRUE( _economy.initialize( &_store, economySettings ) );
            SW_EXPECT_TRUE( _host.registerService( &_accountServer ) );
            SW_EXPECT_TRUE( _host.registerService( &_economy ) );
            OnlineServiceHostSettings hostSettings;
            hostSettings._transportSettings._ioThreadCount = 0;
            hostSettings._listenAddress                    = NetAddress::makeLoopback( kPort );
            hostSettings._pServiceStore                    = &_store;
            hostSettings._pRemoteConfig                    = pRemoteConfig;
            hostSettings._requestBurstPerRemote            = 1000;
            hostSettings._requestBurstPerAccount           = 1000;
            string error;
            SW_EXPECT_TRUE_MSG( _host.initialize( _transport.get(), hostSettings, error ), error.c_str() );
        }

        ~EconomyServerSide()
        {
            _store.shutdown();
            _host.shutdown();
            (void)_store.pollCompletions();
            _economy.shutdown();
            _accountServer.shutdown();
            _loginService.shutdown();
        }
    };

    struct EconomyClientSide
    {
        unique_ptr<IStreamTransport> _transport;
        AccountClient                _account;
        EconomyClient                _economy;
        OnlineServiceClient          _client;

        explicit EconomyClientSide( LoopbackStreamNetwork& network )
            : _transport{ network.createTransport() }
            , _account{}
            , _economy{}
            , _client{}
        {
            _account.initialize( &_client, AccountClientInfo{} );
            _economy.initialize( &_client );
            SW_EXPECT_TRUE( _client.registerClientService( &_account ) );
            SW_EXPECT_TRUE( _client.registerClientService( &_economy ) );
            OnlineServiceClientSettings settings;
            settings._transportSettings._ioThreadCount = 0;
            settings._serverAddress                    = NetAddress::makeLoopback( EconomyServerSide::kPort );
            settings._maxBackoffMs                     = 200;
            string error;
            SW_EXPECT_TRUE_MSG( _client.initialize( _transport.get(), settings, error ), error.c_str() );
        }

        ~EconomyClientSide() { _client.shutdown(); }
    };

    /** @brief 응답 하나를 받아 둡니다. */
    struct EconomyReplyCapture
    {
        EconomyClientReply _reply{};
        int32              _count{ 0 };

        void onReply( const EconomyClientReply& reply )
        {
            _reply = reply;
            ++_count;
        }

        EconomyClient::ReplyDelegate makeDelegate() { return EconomyClient::ReplyDelegate::create<&EconomyReplyCapture::onReply>( this ); }
    };

    struct EconomyRig
    {
        LoopbackStreamNetwork _network;
        MemoryServiceDatabase _database;
        RemoteConfig          _remoteConfig;
        EconomyServerSide     _server;
        EconomyClientSide     _player;
        AccountId             _playerId;
        int64                 _nowMs;

        EconomyRig()
            : _network{ 43u }
            , _database{}
            , _remoteConfig{}
            , _server{ _network, _database, &_remoteConfig }
            , _player{ _network }
            , _playerId{ kInvalidAccountId }
            , _nowMs{ 500000 }
        {
        }

        void step()
        {
            _nowMs += 20;
            _player._client.tick( _nowMs );
            _server._host.tick( _nowMs );
        }

        bool waitFor( const EconomyReplyCapture& capture )
        {
            for ( int32 attempt = 0; attempt < 400 && capture._count == 0; ++attempt )
            {
                step();
            }
            return capture._count == 1;
        }

        AccountClientReply waitAccount( uint64 requestId )
        {
            for ( int32 attempt = 0; attempt < 400; ++attempt )
            {
                vector<AccountClientReply> listReply;
                (void)_player._account.pollReplies( listReply );
                for ( AccountClientReply& reply : listReply )
                {
                    if ( reply._requestId == requestId )
                        return std::move( reply );
                }
                step();
            }
            return AccountClientReply{};
        }

        bool login()
        {
            for ( int32 attempt = 0; attempt < 400 && _player._client.isReady() == false; ++attempt )
            {
                step();
            }
            const bool               bRegistered = waitAccount( _player._account.registerAccount( "buyer", "password123" ) )._result == LoginResult::Ok;
            const AccountClientReply loggedIn    = waitAccount( _player._account.login( "buyer", "password123" ) );
            _playerId                            = loggedIn._grant._identity._accountId;
            return bRegistered && loggedIn._result == LoginResult::Ok;
        }

        void grantGold( int64 amount )
        {
            LedgerTransferRequest request;
            request._journalKey = string( "test/gold." ) + ServiceKeyUtil::makeHex64( _playerId );
            request._reason     = "test.grant";
            request._listPosting.push_back( LedgerPosting{ LedgerHolder::makeMint(), LedgerHolder::makeAccount( _playerId ), "cur.gold", amount } );
            LedgerTransferOutcome outcome;
            (void)Ledger::executeTransfer( _database, request, outcome );
        }
    };
} // namespace

SW_TEST_CASE( EconomyClientTest, LedgerResponseOverwritesTheLocalWallet )
{
    EconomyRig rig;
    SW_ASSERT_TRUE( rig.login() );
    rig.grantGold( 100 );
    Wallet wallet;
    wallet.add( "gold", 999 );  // 로컬에서 바꾼 값(치트 · 오래된 사본) — 원장이 덮어쓴다
    wallet.add( "tickets", 5 ); // 원장에 없는 통화 — 0 이 된다
    EconomyReplyCapture walletReply;
    (void)rig._player._economy.requestWallet( walletReply.makeDelegate() );
    SW_ASSERT_TRUE( rig.waitFor( walletReply ) );
    SW_ASSERT_TRUE( walletReply._reply._reply._result == EconomyResult::Ok );
    rig._player._economy.applyLedgerBalances( wallet );
    SW_EXPECT_EQUAL( wallet.getBalance( "gold" ), int64( 100 ) );
    SW_EXPECT_EQUAL( wallet.getBalance( "tickets" ), int64( 0 ) );

    EconomyReplyCapture    purchase;
    EconomyPurchaseRequest request;
    request._offerId = "potion";
    request._count   = 2;
    (void)rig._player._economy.requestPurchase( request, NetIdempotencyKey{}, purchase.makeDelegate() );
    SW_ASSERT_TRUE( rig.waitFor( purchase ) );
    SW_ASSERT_TRUE( purchase._reply._reply._result == EconomyResult::Ok );
    SW_EXPECT_EQUAL( rig._player._economy.getBalance( "cur.gold" ), int64( 60 ) ); // 응답의 바뀐 잔액이 캐시에
    SW_EXPECT_EQUAL( rig._player._economy.getBalance( "item.potion" ), int64( 2 ) );
    rig._player._economy.applyLedgerBalances( wallet );
    SW_EXPECT_EQUAL( wallet.getBalance( "gold" ), int64( 60 ) );
}

SW_TEST_CASE( EconomyClientTest, RetryWithTheSameKeyChargesOnce )
{
    EconomyRig rig;
    SW_ASSERT_TRUE( rig.login() );
    rig.grantGold( 100 );
    EconomyPurchaseRequest request;
    request._offerId = "potion";
    EconomyReplyCapture first;
    (void)rig._player._economy.requestPurchase( request, NetIdempotencyKey{}, first.makeDelegate() );
    SW_ASSERT_TRUE( rig.waitFor( first ) );
    SW_ASSERT_TRUE( first._reply._idempotencyKey.isValid() );
    EconomyReplyCapture retry;
    (void)rig._player._economy.requestPurchase( request, first._reply._idempotencyKey, retry.makeDelegate() );
    SW_ASSERT_TRUE( rig.waitFor( retry ) );
    SW_EXPECT_TRUE( retry._reply._reply._result == EconomyResult::Ok );
    SW_EXPECT_EQUAL( rig._player._economy.getBalance( "cur.gold" ), int64( 80 ) );
    LedgerBalance gold;
    SW_ASSERT_TRUE( Ledger::readBalance( rig._database, LedgerHolder::makeAccount( rig._playerId ), "cur.gold", gold ) == ServiceStoreResult::Ok );
    SW_EXPECT_EQUAL( gold._amount, int64( 80 ) );
}

SW_TEST_CASE( EconomyClientTest, FeatureFlagAndSignInGateRequests )
{
    EconomyRig rig;
    for ( int32 attempt = 0; attempt < 400 && rig._player._client.isReady() == false; ++attempt )
    {
        rig.step();
    }
    EconomyReplyCapture beforeLogin;
    (void)rig._player._economy.requestWallet( beforeLogin.makeDelegate() );
    SW_ASSERT_TRUE( rig.waitFor( beforeLogin ) );
    SW_EXPECT_TRUE( beforeLogin._reply._reply._result == EconomyResult::NotSignedIn );
    SW_ASSERT_TRUE( rig.login() );
    rig.grantGold( 100 );
    ServiceAuditEntry audit;
    audit._actor   = "gm.0000000000000001";
    audit._action  = "config.set";
    audit._subject = "config";
    audit._timeMs  = 1;
    RemoteConfigValue off;
    off._type               = RemoteConfigValueType::Flag;
    off._integer            = 1;
    off._rolloutBasisPoints = 0; // 아무 계정에도 켜지 않는다
    rig._remoteConfig.submitSet( rig._server._store, nullptr, EconomyService::kFeatureFlag, off, audit );
    (void)rig._server._store.pollCompletions();
    EconomyPurchaseRequest request;
    request._offerId = "potion";
    EconomyReplyCapture disabled;
    (void)rig._player._economy.requestPurchase( request, NetIdempotencyKey{}, disabled.makeDelegate() );
    SW_ASSERT_TRUE( rig.waitFor( disabled ) );
    SW_EXPECT_EQUAL( disabled._reply._errorCode, OnlineError::kFeatureDisabled );
    SW_EXPECT_TRUE( disabled._reply._reply._result == EconomyResult::Unavailable );
}
