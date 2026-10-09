// 거래 스트림 바인딩 — 루프백 스트림 위 계정 + 거래 + 원장(메모리 저장소). 두 클라이언트가 가입 · 로그인 · 거래 끝까지(초대 알림 · 닫힘 알림의 잔액),
// 확정 직후 스트림이 끊겨 다시 붙은 뒤 같은 멱등 키로 확정 재시도 → 정산 한 번, 정산 전 서버 재시작 → 재시작 복구(취소)와 재시도 WrongState · 잔액 그대로,
// 상대가 떠나면 PartyLeft 알림, 기능 플래그가 꺼지면 kFeatureDisabled, 인벤토리 칸 → 다리(인스턴스 상태 · 바꿀 수 없는 아이템 거절).
#include "pch.h"

#include "Core/Network/Transport/LoopbackStreamTransport.h"

#include "GameFramework/Base/Gameplay/Inventory/Inventory.h"
#include "GameFramework/Base/Gameplay/Inventory/ItemCatalog.h"
#include "GameFramework/Base/Online/Audit/ServiceAuditLog.h"
#include "GameFramework/Base/Online/Config/RemoteConfig.h"
#include "GameFramework/Base/Online/Ledger/Ledger.h"
#include "GameFramework/Base/Online/Ledger/LedgerAudit.h"
#include "GameFramework/Base/Online/Security/NetSecurity.h"
#include "GameFramework/Base/Online/Service/OnlineServiceClient.h"
#include "GameFramework/Base/Online/Service/OnlineServiceHost.h"
#include "GameFramework/Base/Online/Store/MemoryServiceStore.h"
#include "GameFramework/Base/Online/Store/ServiceKeyUtil.h"
#include "GameFramework/Kits/Feature/Online/Account/Server/NetSecurityLoginCrypto.h"
#include "GameFramework/Kits/Feature/Online/Account/Server/Service/AccountServer.h"
#include "GameFramework/Kits/Feature/Online/Account/Server/Service/LoginService.h"
#include "GameFramework/Kits/Feature/Online/Account/Shared/Api/AccountClient.h"
#include "GameFramework/Kits/Feature/Online/Trade/Server/TradeServer.h"
#include "GameFramework/Kits/Feature/Online/Trade/Shared/TradeClient.h"
#include "GameFramework/Kits/Feature/Online/Trade/Shared/TradeInventoryUtil.h"

#include "TestFramework/TestFramework.h"

using namespace sw;

namespace
{
    constexpr uint16 kTradePort = 7321;

    struct TradeServerSide
    {
        MemoryServiceStore           _store;
        NetSecurityLoginCrypto       _crypto;
        LoginService                 _loginService;
        AccountServer                _accountServer;
        DefaultTradePolicy           _tradePolicy;
        TradeService                 _tradeService;
        TradeServer                  _tradeServer;
        unique_ptr<IStreamTransport> _transport;
        OnlineServiceHost            _host;

        TradeServerSide( LoopbackStreamNetwork& network, MemoryServiceDatabase& database, RemoteConfig* pRemoteConfig, int64 nowMs )
            : _store{ &database }
            , _crypto{ &NetSecurity::getProvider() }
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
            const uint8 arrMasterKey[LoginTicketAuthority::kMasterKeySize] = { 4, 4, 4 };
            _loginService.initialize( &_store, &_crypto, settings, arrMasterKey );
            _accountServer.initialize( &_loginService, AccountServerSettings{} );
            _tradeService.initialize( &_store, &_tradePolicy, nullptr, 77, TradeSettings{} );
            _tradeServer.initialize( &_tradeService, &_loginService, nullptr );
            SW_EXPECT_TRUE( _host.registerService( &_accountServer ) );
            SW_EXPECT_TRUE( _host.registerService( &_tradeServer ) );
            OnlineServiceHostSettings hostSettings;
            hostSettings._transportSettings._ioThreadCount = 0;
            hostSettings._listenAddress                    = NetAddress::makeLoopback( kTradePort );
            hostSettings._pServiceStore                    = &_store;
            hostSettings._pRemoteConfig                    = pRemoteConfig;
            hostSettings._requestBurstPerRemote            = 1000;
            hostSettings._requestBurstPerAccount           = 1000;
            string error;
            SW_EXPECT_TRUE_MSG( _host.initialize( _transport.get(), hostSettings, error ), error.c_str() );
            _tradeService.recoverOwnedTrades( nowMs ); // 기동 — 이 서버가 연 열린 거래를 닫는다
        }

        ~TradeServerSide()
        {
            // 죽은 프로세스 흉내 — 저장소를 먼저 내려 연결이 닫히며 생기는 쓰기(떠남 · 유예)가 남지 않게 한다.
            _store.shutdown();
            _host.shutdown();
            (void)_store.pollCompletions();
            _tradeServer.shutdown();
            _tradeService.shutdown();
            _accountServer.shutdown();
            _loginService.shutdown();
        }
    };

    struct TradeClientSide
    {
        unique_ptr<IStreamTransport> _transport;
        AccountClient                _account;
        TradeClient                  _trade;
        OnlineServiceClient          _client;

        explicit TradeClientSide( LoopbackStreamNetwork& network )
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
            settings._serverAddress                    = NetAddress::makeLoopback( kTradePort );
            settings._maxBackoffMs                     = 200;
            string error;
            SW_EXPECT_TRUE_MSG( _client.initialize( _transport.get(), settings, error ), error.c_str() );
        }

        ~TradeClientSide() { _client.shutdown(); }
    };

    struct TradeRig
    {
        LoopbackStreamNetwork       _network;
        MemoryServiceDatabase       _database;
        RemoteConfig                _remoteConfig;
        unique_ptr<TradeServerSide> _server;
        TradeClientSide             _alice;
        TradeClientSide             _bob;
        AccountId                   _aliceId;
        AccountId                   _bobId;
        int64                       _nowMs;

        TradeRig()
            : _network{ 41u }
            , _database{}
            , _remoteConfig{}
            , _server{}
            , _alice{ _network }
            , _bob{ _network }
            , _aliceId{ kInvalidAccountId }
            , _bobId{ kInvalidAccountId }
            , _nowMs{ 500000 }
        {
            restartServer();
        }

        void restartServer()
        {
            _server.reset();
            _server = make_unique<TradeServerSide>( _network, _database, &_remoteConfig, _nowMs );
        }

        void step( int32 count = 1 )
        {
            for ( int32 index = 0; index < count; ++index )
            {
                _nowMs += 20;
                _alice._client.tick( _nowMs );
                _bob._client.tick( _nowMs );
                _server->_host.tick( _nowMs );
            }
        }

        AccountClientReply waitAccount( TradeClientSide& client, uint64 requestId )
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

        TradeClientReply waitTrade( TradeClientSide& client, uint64 requestId )
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

        bool loginBoth()
        {
            for ( int32 attempt = 0; attempt < 400 && ( _alice._client.isReady() == false || _bob._client.isReady() == false ); ++attempt )
            {
                step();
            }
            const bool bRegistered = waitAccount( _alice, _alice._account.registerAccount( "alice", "password123" ) )._result == LoginResult::Ok &&
                                     waitAccount( _bob, _bob._account.registerAccount( "bob", "password123" ) )._result == LoginResult::Ok;
            const AccountClientReply aliceLogin = waitAccount( _alice, _alice._account.login( "alice", "password123" ) );
            const AccountClientReply bobLogin   = waitAccount( _bob, _bob._account.login( "bob", "password123" ) );
            _aliceId                            = aliceLogin._grant._identity._accountId;
            _bobId                              = bobLogin._grant._identity._accountId;
            _alice._trade.setAccountId( _aliceId );
            _bob._trade.setAccountId( _bobId );
            grant( _aliceId, "item.sword", 1 );
            grant( _bobId, "cur.gold", 500 );
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

        /** @brief 초대 → 수락 → 칼 ↔ 금 300 → 둘 다 잠금까지. 거래 id 입니다(실패면 0). */
        uint64 openLockedTrade()
        {
            const TradeClientReply invited = waitTrade( _alice, _alice._trade.invite( "BOB" ) );
            if ( invited._result != TradeResult::Ok )
                return 0;
            const uint64 tradeId = invited._snapshot._tradeId;
            step( 4 );
            const bool bOpened = waitTrade( _bob, _bob._trade.respond( tradeId, true ) )._result == TradeResult::Ok &&
                                 waitTrade( _alice, _alice._trade.setOffer( tradeId, {
                                                                                         TradeLeg{ "item.sword", 1 }
            } ) )
                                         ._result == TradeResult::Ok &&
                                 waitTrade( _bob, _bob._trade.setOffer( tradeId, { TradeLeg{ "cur.gold", 300 } } ) )._result == TradeResult::Ok && waitTrade( _alice, _alice._trade.lock( tradeId ) )._result == TradeResult::Ok && waitTrade( _bob, _bob._trade.lock( tradeId ) )._result == TradeResult::Ok;
            step( 4 );
            return bOpened ? tradeId : 0;
        }
    };

    bool isTradableItem( const hashed_string& itemId, string& outAssetId )
    {
        if ( itemId == hashed_string( "quest_key" ) )
            return false;
        outAssetId = string( "item." ) + itemId.c_str();
        return true;
    }
} // namespace

SW_TEST_CASE( TradeStreamTest, TwoClientsTradeToTheEndAndBothSeeTheBalances )
{
    TradeRig rig;
    SW_ASSERT_TRUE( rig.loginBoth() );
    const uint64 tradeId = rig.openLockedTrade();
    SW_ASSERT_TRUE( tradeId != 0 );
    vector<TradeClientUpdate> listBobUpdate;
    (void)rig._bob._trade.pollUpdates( listBobUpdate );
    SW_ASSERT_TRUE( listBobUpdate.empty() == false );
    SW_EXPECT_EQUAL( TradeMethod::kPushInvited, listBobUpdate.front()._kind ); // 초대 알림

    SW_ASSERT_TRUE( rig.waitTrade( rig._alice, rig._alice._trade.confirm( tradeId ) )._result == TradeResult::Ok );
    rig.step( 4 );
    const TradeClientReply settled = rig.waitTrade( rig._bob, rig._bob._trade.confirm( tradeId ) );
    SW_ASSERT_TRUE( settled._result == TradeResult::Ok );
    SW_EXPECT_TRUE( settled._snapshot._state == TradeState::Settled );
    SW_EXPECT_EQUAL( size_t( 2 ), settled._listBalance.size() ); // 요청한 쪽의 이동 뒤 잔액(칼 · 금)
    rig.step( 6 );
    vector<TradeClientUpdate> listAliceUpdate;
    (void)rig._alice._trade.pollUpdates( listAliceUpdate );
    SW_ASSERT_TRUE( listAliceUpdate.empty() == false );
    SW_EXPECT_EQUAL( TradeMethod::kPushClosed, listAliceUpdate.back()._kind );
    int64 aliceGold = -1;
    for ( const TradeBalance& balance : listAliceUpdate.back()._listBalance )
    {
        if ( balance._assetId == "cur.gold" )
            aliceGold = balance._amount;
    }
    SW_EXPECT_EQUAL( int64( 300 ), aliceGold ); // 상대 쪽 알림에도 그 계정의 잔액
    SW_EXPECT_EQUAL( int64( 1 ), rig.readAmount( rig._bobId, "item.sword" ) );
    SW_EXPECT_EQUAL( int64( 200 ), rig.readAmount( rig._bobId, "cur.gold" ) );
    LedgerAuditReport report;
    SW_ASSERT_TRUE( LedgerAudit::computeReport( rig._database, report ) == ServiceStoreResult::Ok );
    SW_EXPECT_TRUE( report.isBalanced() );
}

SW_TEST_CASE( TradeStreamTest, ConfirmRetriedWithTheSameKeyAfterADroppedStreamSettlesOnce )
{
    TradeRig rig;
    SW_ASSERT_TRUE( rig.loginBoth() );
    const uint64 tradeId = rig.openLockedTrade();
    SW_ASSERT_TRUE( tradeId != 0 );
    SW_ASSERT_TRUE( rig.waitTrade( rig._alice, rig._alice._trade.confirm( tradeId ) )._result == TradeResult::Ok );
    rig.step( 4 );

    const NetIdempotencyKey key  = NetIdempotencyKey::makeRandom();
    const TradeSnapshot     seen = rig._bob._trade.getSnapshot();
    const int32             side = seen.findSideIndex( rig._bobId );
    SW_ASSERT_TRUE( side >= 0 );
    (void)rig._bob._trade.confirmSeen( tradeId, seen._arrSide[side]._offerRevision, seen._arrSide[1 - side]._offerRevision, key );
    rig._bob._client.tick( rig._nowMs ); // 요청은 선에 올랐다
    rig._server->_host.tick( rig._nowMs );
    rig._bob._transport->close( rig._bob._client.getConnection(), StreamCloseMode::Abort ); // 응답 전에 끊겼다
    rig.step( 2 );
    for ( int32 attempt = 0; attempt < 400 && rig._bob._account.isLoggedIn() == false; ++attempt )
    {
        rig.step(); // 다시 붙어 재접속
    }
    SW_ASSERT_TRUE( rig._bob._account.isLoggedIn() );
    const TradeClientReply retried =
        rig.waitTrade( rig._bob, rig._bob._trade.confirmSeen( tradeId, seen._arrSide[side]._offerRevision, seen._arrSide[1 - side]._offerRevision, key ) );
    SW_EXPECT_EQUAL( 0, int32( retried._errorCode ) );
    SW_EXPECT_EQUAL( int32( TradeResult::Ok ), int32( retried._result ) );
    SW_EXPECT_TRUE( retried._snapshot._state == TradeState::Settled );
    SW_EXPECT_EQUAL( int64( 1 ), rig.readAmount( rig._bobId, "item.sword" ) );
    SW_EXPECT_EQUAL( int64( 200 ), rig.readAmount( rig._bobId, "cur.gold" ) ); // 한 번만
    SW_EXPECT_EQUAL( 1, rig._database.countRecords( ServiceAuditLog::getTable() ) );
}

SW_TEST_CASE( TradeStreamTest, ServerRestartBeforeSettleCancelsWithoutMovement )
{
    TradeRig rig;
    SW_ASSERT_TRUE( rig.loginBoth() );
    const uint64 tradeId = rig.openLockedTrade();
    SW_ASSERT_TRUE( tradeId != 0 );
    SW_ASSERT_TRUE( rig.waitTrade( rig._alice, rig._alice._trade.confirm( tradeId ) )._result == TradeResult::Ok );

    rig.restartServer(); // 정산 전에 서버가 죽었다 — 새 서버가 자기 열린 거래를 닫는다
    rig.step( 2 );       // 클라이언트가 끊김을 안다
    for ( int32 attempt = 0; attempt < 400 && ( rig._alice._account.isLoggedIn() == false || rig._bob._account.isLoggedIn() == false ); ++attempt )
    {
        rig.step();
    }
    SW_ASSERT_TRUE( rig._bob._account.isLoggedIn() );
    const TradeClientReply late = rig.waitTrade( rig._bob, rig._bob._trade.confirm( tradeId ) );
    SW_EXPECT_EQUAL( 0, int32( late._errorCode ) );
    SW_EXPECT_EQUAL( int32( TradeResult::WrongState ), int32( late._result ) );
    SW_EXPECT_TRUE( late._snapshot._state == TradeState::Cancelled );
    SW_EXPECT_TRUE( late._snapshot._closeReason == TradeCloseReason::ServerRestart );
    SW_EXPECT_EQUAL( int64( 1 ), rig.readAmount( rig._aliceId, "item.sword" ) );
    SW_EXPECT_EQUAL( int64( 500 ), rig.readAmount( rig._bobId, "cur.gold" ) );
    SW_EXPECT_TRUE( rig.waitTrade( rig._alice, rig._alice._trade.invite( "bob" ) )._result == TradeResult::Ok ); // 링크가 풀려 새 거래를 연다
}

SW_TEST_CASE( TradeStreamTest, PartnerLeavingClosesTheTradeAndFeatureFlagCanTurnTradingOff )
{
    TradeRig rig;
    SW_ASSERT_TRUE( rig.loginBoth() );
    const uint64 tradeId = rig.openLockedTrade();
    SW_ASSERT_TRUE( tradeId != 0 );
    (void)rig.waitAccount( rig._bob, rig._bob._account.logout() ); // 서버가 연결을 닫는다 → 떠남
    rig.step( 10 );
    vector<TradeClientUpdate> listUpdate;
    (void)rig._alice._trade.pollUpdates( listUpdate );
    SW_ASSERT_TRUE( listUpdate.empty() == false );
    SW_EXPECT_EQUAL( TradeMethod::kPushClosed, listUpdate.back()._kind );
    SW_EXPECT_TRUE( listUpdate.back()._snapshot._closeReason == TradeCloseReason::PartyLeft );

    ServiceAuditEntry audit;
    audit._actor   = "gm.0000000000000001";
    audit._action  = "config.set";
    audit._subject = "config";
    audit._timeMs  = 1;
    RemoteConfigValue off;
    off._type               = RemoteConfigValueType::Flag;
    off._integer            = 1;
    off._rolloutBasisPoints = 0; // 아무 계정에도 켜지 않는다
    rig._remoteConfig.submitSet( rig._server->_store, nullptr, TradeServer::kFeatureFlag, off, audit );
    (void)rig._server->_store.pollCompletions();
    const TradeClientReply disabled = rig.waitTrade( rig._alice, rig._alice._trade.invite( "bob" ) );
    SW_EXPECT_EQUAL( OnlineError::kFeatureDisabled, disabled._errorCode );
}

SW_TEST_CASE( TradeStreamTest, InventorySlotsBecomeLegs )
{
    ItemCatalog items;
    SW_ASSERT_TRUE( items.loadFromXmlText( R"(<ItemCatalog>
  <Item id="potion" category="Consumable" maxStack="10" value="12"/>
  <Item id="sword" category="Weapon" maxStack="1" value="100"/>
  <Item id="quest_key" category="Quest" maxStack="1" value="0"/>
</ItemCatalog>)",
                                           "TradeStreamTest" ) );
    Inventory inventory;
    inventory.initialize( &items, 6 );
    SW_ASSERT_EQUAL( 12, inventory.addItem( "potion", 12 ) ); // 두 칸(10 + 2)
    SW_ASSERT_EQUAL( 1, inventory.addItem( "quest_key", 1 ) );
    InventorySlot damaged;
    damaged._itemId = "sword";
    damaged._count  = 1;
    damaged._damage = 0.5f;
    SW_ASSERT_TRUE( inventory.addStack( damaged ) );
    const int32   firstPotion = inventory.findFirstSlot( "potion" );
    const int32   questSlot   = inventory.findFirstSlot( "quest_key" );
    const int32   swordSlot   = inventory.findFirstSlot( "sword" );
    vector<int32> listPotionSlot;
    for ( int32 slot = 0; slot < inventory.getSlotCount(); ++slot )
    {
        if ( inventory.getSlot( slot )._itemId == hashed_string( "potion" ) )
            listPotionSlot.push_back( slot );
    }
    SW_ASSERT_EQUAL( size_t( 2 ), listPotionSlot.size() );
    vector<TradeLeg> listLeg;
    SW_ASSERT_TRUE( TradeInventoryUtil::makeLegs( inventory, listPotionSlot, &isTradableItem, listLeg ) == TradeResult::Ok );
    SW_ASSERT_EQUAL( size_t( 1 ), listLeg.size() ); // 같은 아이템 칸은 한 다리로
    SW_EXPECT_EQUAL( string( "item.potion" ), listLeg[0]._assetId );
    SW_EXPECT_EQUAL( int64( 12 ), listLeg[0]._amount );
    SW_EXPECT_TRUE( TradeInventoryUtil::makeLegs( inventory, { questSlot }, &isTradableItem, listLeg ) == TradeResult::NotTradable );
    SW_EXPECT_TRUE( TradeInventoryUtil::makeLegs( inventory, { swordSlot }, &isTradableItem, listLeg ) == TradeResult::NotTradable ); // 외형 피해 = 인스턴스 상태
    SW_EXPECT_TRUE( TradeInventoryUtil::makeLegs( inventory, { firstPotion, firstPotion }, &isTradableItem, listLeg ) == TradeResult::Invalid );
    SW_EXPECT_TRUE( TradeInventoryUtil::makeLegs( inventory, { 99 }, &isTradableItem, listLeg ) == TradeResult::Invalid );
}
