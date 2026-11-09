// TradeServiceTest — 거래 저장과 서비스(메모리 저장소 + 원장): 초대의 바쁨 · 지연 만료, 정산은 양쪽을 한 분개로 옮기고 한 번만(응답을 잃어도), 잠금 · 정산의 잔액 확인, 실패 닫기, 닫힌 거래,
// 계정이 떠날 때, 시한 · 저장소 실패 뒤 다시 시도, 기동 복구, 알림. 상태 기계 표는 TradeStateMachineTest 가 본다. 온라인 키트라 `UnitTests.OnlineKits` 가 돌린다(nogpu).
#include "pch.h"

#include "Core/Testing/TestFramework.h"

#include "GameFramework/Kits/Feature/Online/Trade/Server/TradeService.h"
#include "GameFramework/Kits/Feature/Online/Trade/Server/TradeStateMachine.h"
#include "GameFramework/Runtime/Online/Ledger/Ledger.h"
#include "GameFramework/Runtime/Online/Store/Server/MemoryServiceStore.h"

using namespace sw;

namespace
{
    constexpr AccountID kAlice  = 11;
    constexpr AccountID kBob    = 12;
    constexpr AccountID kCarol  = 13;
    constexpr uint64    kServer = 1;
    constexpr int64     kNowMs  = 1700000000000ll;

    /** @brief 서버 하나입니다. 영속 데이터는 빌립니다 — 같은 데이터로 둘째를 만들면 "다시 뜬 서버" 입니다. */
    struct TradeFixture
    {
        MemoryServiceStore _store;
        DefaultTradePolicy _policy;
        TradeService       _service;
        TradeServiceConfig _settings;
        int64              _nowMs{ kNowMs };
        uint64             _nextTag{ 1 };

        explicit TradeFixture( MemoryServiceDatabase& database )
            : _store{ &database }
            , _policy{}
            , _service{}
            , _settings{}
        {
            _service.initialize( &_store, &_policy, nullptr, kServer, _settings );
        }

        void pump()
        {
            while ( _store.pollCompletions() > 0 )
            {
            }
        }

        TradeCompletion take( uint64 tag )
        {
            pump();
            vector<TradeCompletion> listCompletion;
            _service.takeCompletions( listCompletion );
            SW_EXPECT_EQUAL( size_t( 1 ), listCompletion.size() );
            if ( listCompletion.empty() )
                return TradeCompletion{};
            SW_EXPECT_EQUAL( tag, listCompletion[0]._requestTag );
            return std::move( listCompletion[0] );
        }

        TradeCompletion invite( AccountID fromID, AccountID toID )
        {
            const uint64 tag = _nextTag++;
            _service.invite( fromID, toID, UnixMilliseconds{ _nowMs }, tag );
            return take( tag );
        }

        TradeCompletion respond( AccountID accountID, uint64 tradeID, bool bAccept = true )
        {
            const uint64 tag = _nextTag++;
            _service.respondInvite( accountID, tradeID, bAccept, UnixMilliseconds{ _nowMs }, tag );
            return take( tag );
        }

        TradeCompletion offer( AccountID accountID, uint64 tradeID, string_view assetID, int64 amount )
        {
            TradeLeg leg;
            leg._assetID     = string( assetID );
            leg._amount      = amount;
            const uint64 tag = _nextTag++;
            _service.setOffer( accountID, tradeID, vector<TradeLeg>{ leg }, UnixMilliseconds{ _nowMs }, tag );
            return take( tag );
        }

        TradeCompletion lock( AccountID accountID, uint64 tradeID )
        {
            const uint64 tag = _nextTag++;
            _service.lock( accountID, tradeID, UnixMilliseconds{ _nowMs }, tag );
            return take( tag );
        }

        TradeCompletion confirm( AccountID accountID, const TradeSnapshot& trade )
        {
            const int32  sideIndex = trade.findSideIndex( accountID ).value();
            const uint64 tag       = _nextTag++;
            _service.confirm( accountID, trade._tradeID, trade._arrSide[sideIndex]._offerRevision, trade._arrSide[1 - sideIndex]._offerRevision, UnixMilliseconds{ _nowMs }, tag );
            return take( tag );
        }

        /** @brief 앨리스 금 @p gold ↔ 밥 보석 @p gem 을 제시하고 양쪽이 잠근 거래입니다. */
        TradeSnapshot makeLockedTrade( int64 gold, int64 gem )
        {
            const uint64 tradeID = invite( kAlice, kBob )._snapshot._tradeID;
            SW_EXPECT_TRUE( respond( kBob, tradeID )._result == TradeResult::Ok );
            SW_EXPECT_TRUE( offer( kAlice, tradeID, "cur.gold", gold )._result == TradeResult::Ok );
            SW_EXPECT_TRUE( offer( kBob, tradeID, "cur.gem", gem )._result == TradeResult::Ok );
            SW_EXPECT_TRUE( lock( kAlice, tradeID )._result == TradeResult::Ok );
            const TradeCompletion locked = lock( kBob, tradeID );
            SW_EXPECT_TRUE( locked._result == TradeResult::Ok );
            return locked._snapshot;
        }

        vector<TradeSnapshot> takeUpdates()
        {
            pump();
            vector<TradeSnapshot> listUpdate;
            _service.pollUpdates( listUpdate );
            return listUpdate;
        }
    };

    /** @brief 원장 데이터와 서버 하나의 묶음입니다. */
    struct TradeWorld
    {
        MemoryServiceDatabase _database;
        TradeFixture          _rig;

        TradeWorld()
            : _database{}
            , _rig{ _database }
        {
            grant( kAlice, "cur.gold", 100 );
            grant( kBob, "cur.gem", 50 );
        }

        void grant( AccountID accountID, string_view assetID, int64 amount )
        {
            LedgerTransferRequest request;
            request._listPosting.push_back( LedgerPosting{ LedgerHolder::makeMint(), LedgerHolder::makeAccount( accountID ), string( assetID ), amount } );
            request._journalKey = "test/grant/" + to_string( accountID ) + "/" + string( assetID );
            request._reason     = "test.grant";
            LedgerTransferResult outcome;
            SW_EXPECT_TRUE( Ledger::executeTransfer( _database, request, outcome ) == LedgerResult::Ok );
        }

        void spend( AccountID accountID, string_view assetID, int64 amount )
        {
            LedgerTransferRequest request;
            request._listPosting.push_back( LedgerPosting{ LedgerHolder::makeAccount( accountID ), LedgerHolder::makeSink(), string( assetID ), amount } );
            request._journalKey = "test/spend/" + to_string( accountID ) + "/" + string( assetID );
            request._reason     = "test.spend";
            LedgerTransferResult outcome;
            SW_EXPECT_TRUE( Ledger::executeTransfer( _database, request, outcome ) == LedgerResult::Ok );
        }

        int64 balanceOf( AccountID accountID, string_view assetID )
        {
            LedgerBalance balance;
            SW_EXPECT_TRUE( Ledger::readBalance( _database, LedgerHolder::makeAccount( accountID ), assetID, balance ) == ServiceStoreResult::Ok );
            return balance._amount;
        }
    };
} // namespace

/** @brief [TradeServiceTest] 초대: 자신 · 0 은 Invalid, 이미 거래 중이면 AlreadyTrading · PeerBusy 이고, 거절하면 다시 초대할 수 있다 */
SW_TEST_CASE( TradeServiceTest, InviteRules )
{
    TradeWorld    world;
    TradeFixture& rig = world._rig;
    SW_EXPECT_TRUE( rig.invite( kAlice, kAlice )._result.hasError( OnlineError::kInvalidRequest ) );
    SW_EXPECT_TRUE( rig.invite( kAlice, kInvalidAccountID )._result.hasError( OnlineError::kInvalidRequest ) );

    const TradeCompletion invited = rig.invite( kAlice, kBob );
    SW_EXPECT_TRUE( invited._result == TradeResult::Ok );
    SW_EXPECT_TRUE( invited._snapshot._state == TradeState::Invited );
    SW_EXPECT_TRUE( invited._snapshot._tradeID != 0 );
    SW_EXPECT_TRUE( rig.invite( kAlice, kCarol )._result == TradeResult::AlreadyTrading );
    SW_EXPECT_TRUE( rig.invite( kCarol, kBob )._result == TradeResult::PeerBusy );

    SW_EXPECT_TRUE( rig.respond( kCarol, invited._snapshot._tradeID )._result == TradeResult::NotParty );
    const TradeCompletion declined = rig.respond( kBob, invited._snapshot._tradeID, false );
    SW_EXPECT_TRUE( declined._result == TradeResult::Ok );
    SW_EXPECT_TRUE( declined._snapshot.isClosed() );
    SW_EXPECT_TRUE( rig.invite( kAlice, kCarol )._result == TradeResult::Ok );

    SW_EXPECT_TRUE( rig.respond( kBob, 999 )._result == TradeResult::NotFound );
}

/** @brief [TradeServiceTest] 정산: 양쪽이 확정하면 재화가 서로에게 옮겨지고, 닫힌 뒤 또 확정해도 결과만 돌려주며 두 번 옮기지 않는다 */
SW_TEST_CASE( TradeServiceTest, SettlementSwapsBothSidesOnce )
{
    TradeWorld          world;
    TradeFixture&       rig    = world._rig;
    const TradeSnapshot locked = rig.makeLockedTrade( 40, 20 );

    const TradeCompletion firstConfirm = rig.confirm( kAlice, locked );
    SW_EXPECT_TRUE( firstConfirm._result == TradeResult::Ok );
    SW_EXPECT_FALSE( firstConfirm._snapshot.isClosed() );
    SW_EXPECT_EQUAL( int64( 100 ), world.balanceOf( kAlice, "cur.gold" ) ); // 한쪽 확정으로는 옮기지 않는다

    const TradeCompletion settled = rig.confirm( kBob, firstConfirm._snapshot );
    SW_EXPECT_TRUE( settled._result == TradeResult::Ok );
    SW_EXPECT_TRUE( settled._snapshot._state == TradeState::Settled );
    SW_EXPECT_EQUAL( int64( 60 ), world.balanceOf( kAlice, "cur.gold" ) );
    SW_EXPECT_EQUAL( int64( 20 ), world.balanceOf( kAlice, "cur.gem" ) );
    SW_EXPECT_EQUAL( int64( 40 ), world.balanceOf( kBob, "cur.gold" ) );
    SW_EXPECT_EQUAL( int64( 30 ), world.balanceOf( kBob, "cur.gem" ) );

    SW_EXPECT_TRUE( rig.confirm( kBob, firstConfirm._snapshot )._result == TradeResult::Ok ); // 응답을 잃은 재시도
    SW_EXPECT_EQUAL( int64( 60 ), world.balanceOf( kAlice, "cur.gold" ) );

    SW_EXPECT_TRUE( rig.invite( kAlice, kBob )._result == TradeResult::Ok ); // 끝난 거래의 링크는 남지 않는다
}

/** @brief [TradeServiceTest] 정산 커밋의 응답을 잃어도 원장이 가려 Settled 로 끝나고 한 번만 옮긴다 */
SW_TEST_CASE( TradeServiceTest, LostSettlementReplyMovesOnce )
{
    TradeWorld          world;
    TradeFixture&       rig    = world._rig;
    const TradeSnapshot locked = rig.makeLockedTrade( 40, 20 );
    const TradeSnapshot first  = rig.confirm( kAlice, locked )._snapshot;

    world._database.armFault( ServiceStoreFault::LoseCommitReply );
    const TradeCompletion settled = rig.confirm( kBob, first );
    SW_EXPECT_TRUE( settled._result == TradeResult::Ok );
    SW_EXPECT_TRUE( settled._snapshot._state == TradeState::Settled );
    SW_EXPECT_EQUAL( int64( 60 ), world.balanceOf( kAlice, "cur.gold" ) );
    SW_EXPECT_EQUAL( int64( 40 ), world.balanceOf( kBob, "cur.gold" ) );
}

/** @brief [TradeServiceTest] 잔액: 잠글 때 모자라면 거절하고, 잠근 뒤 써 버렸으면 정산이 Failed 로 닫히며 아무도 재화를 잃지 않는다 */
SW_TEST_CASE( TradeServiceTest, FundsAreCheckedAtLockAndSettlement )
{
    TradeWorld    world;
    TradeFixture& rig     = world._rig;
    const uint64  tradeID = rig.invite( kAlice, kBob )._snapshot._tradeID;
    SW_EXPECT_TRUE( rig.respond( kBob, tradeID )._result == TradeResult::Ok );
    SW_EXPECT_TRUE( rig.offer( kAlice, tradeID, "cur.gold", 101 )._result == TradeResult::Ok ); // 제시는 잔액을 보지 않는다
    SW_EXPECT_TRUE( rig.lock( kAlice, tradeID )._result == TradeResult::InsufficientFunds );
    rig._service.cancel( kAlice, tradeID, UnixMilliseconds{ rig._nowMs }, 90 );
    SW_EXPECT_TRUE( rig.take( 90 )._result == TradeResult::Ok );

    const TradeSnapshot locked = rig.makeLockedTrade( 40, 20 );
    const TradeSnapshot first  = rig.confirm( kAlice, locked )._snapshot;
    world.spend( kAlice, "cur.gold", 90 ); // 잠근 뒤 다른 곳에서 썼다

    const TradeCompletion failed = rig.confirm( kBob, first );
    SW_EXPECT_TRUE( failed._result == TradeResult::InsufficientFunds );
    SW_EXPECT_TRUE( failed._snapshot._state == TradeState::Failed );
    SW_EXPECT_TRUE( failed._snapshot._closeReason == TradeCloseReason::InsufficientFunds );
    SW_EXPECT_EQUAL( int64( 10 ), world.balanceOf( kAlice, "cur.gold" ) );
    SW_EXPECT_EQUAL( int64( 50 ), world.balanceOf( kBob, "cur.gem" ) );

    SW_EXPECT_TRUE( rig.confirm( kBob, first )._result == TradeResult::InsufficientFunds ); // 재시도도 같은 답이다
    SW_EXPECT_TRUE( rig.invite( kAlice, kBob )._result == TradeResult::Ok );
}

/** @brief [TradeServiceTest] 낸 뒤 제시가 바뀌면 옛 내용을 본 확정은 StaleOffer 다 — 바뀐 조건으로 정산되지 않는다 */
SW_TEST_CASE( TradeServiceTest, ConfirmOfStaleOfferIsRefused )
{
    TradeWorld          world;
    TradeFixture&       rig    = world._rig;
    const TradeSnapshot locked = rig.makeLockedTrade( 40, 20 );
    SW_EXPECT_TRUE( rig.offer( kBob, locked._tradeID, "cur.gem", 1 )._result == TradeResult::Ok );
    SW_EXPECT_TRUE( rig.lock( kAlice, locked._tradeID )._result == TradeResult::Ok );
    const TradeSnapshot relocked = rig.lock( kBob, locked._tradeID )._snapshot;
    SW_EXPECT_TRUE( rig.confirm( kAlice, locked )._result == TradeResult::StaleOffer );
    SW_EXPECT_TRUE( rig.confirm( kAlice, relocked )._result == TradeResult::Ok );
    SW_EXPECT_EQUAL( int64( 100 ), world.balanceOf( kAlice, "cur.gold" ) );
}

/** @brief [TradeServiceTest] 시한이 지난 거래: 명령을 내면 닫히며 WrongState 이고, 새 초대는 남은 링크를 정리하고 진행한다 */
SW_TEST_CASE( TradeServiceTest, ExpiredTradesCloseLazily )
{
    TradeWorld    world;
    TradeFixture& rig     = world._rig;
    const uint64  tradeID = rig.invite( kAlice, kBob )._snapshot._tradeID;

    rig._nowMs += rig._settings._inviteTimeoutMs;
    const TradeCompletion late = rig.respond( kBob, tradeID );
    SW_EXPECT_TRUE( late._result == TradeResult::WrongState );
    SW_EXPECT_TRUE( late._snapshot._closeReason == TradeCloseReason::Timeout );

    const uint64 secondID = rig.invite( kAlice, kBob )._snapshot._tradeID;
    SW_EXPECT_TRUE( secondID != 0 );
    rig._nowMs += rig._settings._inviteTimeoutMs;
    const TradeCompletion replaced = rig.invite( kBob, kCarol ); // 밥의 묵은 거래를 닫고 진행한다
    SW_EXPECT_TRUE( replaced._result == TradeResult::Ok );
    SW_EXPECT_TRUE( rig.respond( kBob, secondID )._result == TradeResult::WrongState );
    SW_EXPECT_TRUE( rig.invite( kAlice, kCarol )._result == TradeResult::PeerBusy );
}

/** @brief [TradeServiceTest] 서비스 시한: update 가 시한이 된 거래를 닫고 알린다. 활동이 있으면 시한이 밀린다 */
SW_TEST_CASE( TradeServiceTest, UpdateExpiresIdleTrades )
{
    TradeWorld    world;
    TradeFixture& rig     = world._rig;
    const uint64  tradeID = rig.invite( kAlice, kBob )._snapshot._tradeID;
    (void)rig.takeUpdates();

    rig._service.update( UnixMilliseconds{ kNowMs + rig._settings._inviteTimeoutMs - 1 } );
    SW_EXPECT_TRUE( rig.takeUpdates().empty() );

    rig._nowMs += rig._settings._inviteTimeoutMs - 1;
    SW_EXPECT_TRUE( rig.respond( kBob, tradeID )._result == TradeResult::Ok );
    (void)rig.takeUpdates();
    rig._service.update( UnixMilliseconds{ kNowMs + rig._settings._inviteTimeoutMs } ); // 수락으로 밀렸다
    SW_EXPECT_TRUE( rig.takeUpdates().empty() );

    rig._service.update( UnixMilliseconds{ rig._nowMs + rig._settings._idleTimeoutMs } );
    const vector<TradeSnapshot> listClosed = rig.takeUpdates();
    SW_REQUIRE_EQUAL( size_t( 1 ), listClosed.size() );
    SW_EXPECT_TRUE( listClosed[0]._closeReason == TradeCloseReason::Timeout );
    SW_EXPECT_EQUAL( 0, rig._service.getPendingCount() );
}

/** @brief [TradeServiceTest] 시한 처리가 저장소 실패로 지면 잊지 않고 다시 시도해 닫는다 */
SW_TEST_CASE( TradeServiceTest, ExpiryRetriesAfterStoreFailure )
{
    TradeWorld    world;
    TradeFixture& rig = world._rig;
    (void)rig.invite( kAlice, kBob );
    (void)rig.takeUpdates();
    const int64 deadlineMs = kNowMs + rig._settings._inviteTimeoutMs;

    world._database.armFault( ServiceStoreFault::RejectRead );
    rig._service.update( UnixMilliseconds{ deadlineMs } );
    SW_EXPECT_TRUE( rig.takeUpdates().empty() );

    rig._service.update( UnixMilliseconds{ deadlineMs + TradeConstant::kExpiryRetryMs } );
    const vector<TradeSnapshot> listClosed = rig.takeUpdates();
    SW_REQUIRE_EQUAL( size_t( 1 ), listClosed.size() );
    SW_EXPECT_TRUE( listClosed[0]._closeReason == TradeCloseReason::Timeout );
}

/** @brief [TradeServiceTest] 계정이 떠나면 그 거래가 닫히고 알림이 간다. 거래가 없으면 아무 일도 없다 */
SW_TEST_CASE( TradeServiceTest, CloseForAccountWhenLeaving )
{
    TradeWorld    world;
    TradeFixture& rig = world._rig;
    (void)rig.invite( kAlice, kBob );
    (void)rig.takeUpdates();

    rig._service.closeForAccount( kCarol, TradeCloseReason::PartyLeft, UnixMilliseconds{ kNowMs } );
    SW_EXPECT_TRUE( rig.takeUpdates().empty() );

    rig._service.closeForAccount( kBob, TradeCloseReason::PartyLeft, UnixMilliseconds{ kNowMs } );
    const vector<TradeSnapshot> listClosed = rig.takeUpdates();
    SW_REQUIRE_EQUAL( size_t( 1 ), listClosed.size() );
    SW_EXPECT_TRUE( listClosed[0]._closeReason == TradeCloseReason::PartyLeft );
    SW_EXPECT_TRUE( rig.invite( kAlice, kCarol )._result == TradeResult::Ok );
}

/** @brief [TradeServiceTest] 기동 복구: 다시 뜬 서버는 자기가 연 열린 거래를 닫아 당사자를 풀어 준다. 재화는 그대로다 */
SW_TEST_CASE( TradeServiceTest, RestartRecoversOwnedTrades )
{
    TradeWorld          world;
    const TradeSnapshot locked = world._rig.makeLockedTrade( 40, 20 );

    TradeFixture restarted( world._database );
    restarted._service.recoverOwnedTrades( UnixMilliseconds{ kNowMs } );
    const vector<TradeSnapshot> listClosed = restarted.takeUpdates();
    SW_REQUIRE_EQUAL( size_t( 1 ), listClosed.size() );
    SW_EXPECT_EQUAL( locked._tradeID, listClosed[0]._tradeID );
    SW_EXPECT_TRUE( listClosed[0]._closeReason == TradeCloseReason::ServerRestart );
    SW_EXPECT_EQUAL( int64( 100 ), world.balanceOf( kAlice, "cur.gold" ) );
    SW_EXPECT_TRUE( restarted.invite( kAlice, kBob )._result == TradeResult::Ok );
    (void)restarted.takeUpdates();

    restarted._service.recoverOwnedTrades( UnixMilliseconds{ kNowMs } ); // 다시 돌아도 닫힌 것은 그대로다 — 방금 연 거래만 닫는다
    SW_EXPECT_EQUAL( size_t( 1 ), restarted.takeUpdates().size() );
    restarted._service.recoverOwnedTrades( UnixMilliseconds{ kNowMs } );
    SW_EXPECT_TRUE( restarted.takeUpdates().empty() );
}

/** @brief [TradeServiceTest] 저장소 실패: 명령은 Unavailable 이고 거래 내용이 그대로다 */
SW_TEST_CASE( TradeServiceTest, StoreFailureLeavesTradeIntact )
{
    TradeWorld    world;
    TradeFixture& rig     = world._rig;
    const uint64  tradeID = rig.invite( kAlice, kBob )._snapshot._tradeID;
    world._database.armFault( ServiceStoreFault::RejectCommit );
    SW_EXPECT_TRUE( rig.respond( kBob, tradeID )._result.hasError( OnlineError::kUnavailable ) );
    SW_EXPECT_TRUE( rig.respond( kBob, tradeID )._result == TradeResult::Ok );
}
