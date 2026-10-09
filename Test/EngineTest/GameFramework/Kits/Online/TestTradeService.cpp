// 거래 서비스 — 메모리 저장소 + 원장 + 감사(결정적). 행복한 길(분개 하나 · 감사 줄 하나 · 보존), 잠금 뒤 제시 바꿈은 잠금 · 확정을 풀고 옛 판 확정은 StaleOffer,
// 정산 때 모자라면 아무것도 안 움직이고 Failed, 한 거래만(AlreadyTrading · PeerBusy) · GM 회수가 먼저면 정산 실패, 응답 유실에도 정산 한 번, 거절된 커밋은 아무것도
// 안 남기고 다시 확정하면 정산, 재시작한 서버는 자기 열린 거래를 닫는다, 떠남 · 시한 취소, 거래 불가 · 다리 초과, 같은 확정 두 번, 상태 기계 표.
#include "pch.h"

#include "GameFramework/Base/Online/Audit/ServiceAuditLog.h"
#include "GameFramework/Base/Online/Ledger/Ledger.h"
#include "GameFramework/Base/Online/Ledger/LedgerAudit.h"
#include "GameFramework/Base/Online/Store/MemoryServiceStore.h"
#include "GameFramework/Base/Online/Store/ServiceKeyUtil.h"
#include "GameFramework/Kits/Online/Server/Trade/TradeService.h"
#include "GameFramework/Kits/Online/Server/Trade/TradeStateMachine.h"

#include "TestFramework/TestFramework.h"

using namespace sw;

namespace
{
    constexpr AccountId kAlice = 0xA11CE;
    constexpr AccountId kBob   = 0xB0B;
    constexpr AccountId kCarol = 0xCA201;

    /** @brief 서버 한 대 — 앞(MemoryServiceStore) + 거래 서비스. 데이터는 밖에서 빌린다. */
    struct TradeNode
    {
        MemoryServiceStore       _store;
        DefaultTradePolicy       _policy;
        unique_ptr<TradeService> _service;
        uint64                   _serverId;
        uint64                   _nextTag;

        TradeNode( MemoryServiceDatabase* pDatabase, uint64 serverId )
            : _store{ pDatabase }
            , _policy{}
            , _service{}
            , _serverId{ serverId }
            , _nextTag{ 1 }
        {
            restart();
        }

        ~TradeNode()
        {
            _store.shutdown();
            (void)_store.pollCompletions();
        }

        void restart()
        {
            (void)_store.pollCompletions();
            _service = make_unique<TradeService>();
            _service->initialize( &_store, &_policy, nullptr, _serverId, TradeSettings{} );
        }

        TradeCompletion settle()
        {
            (void)_store.pollCompletions();
            vector<TradeCompletion> listCompletion;
            _service->drainCompletions( listCompletion );
            return listCompletion.empty() ? TradeCompletion{} : listCompletion.back();
        }

        TradeCompletion invite( AccountId fromId, AccountId toId, int64 nowMs )
        {
            _service->invite( fromId, toId, nowMs, _nextTag++ );
            return settle();
        }
        TradeCompletion respond( AccountId responderId, uint64 tradeId, bool bAccept, int64 nowMs )
        {
            _service->respondInvite( responderId, tradeId, bAccept, nowMs, _nextTag++ );
            return settle();
        }
        TradeCompletion setOffer( AccountId actorId, uint64 tradeId, vector<TradeLeg> listLeg, int64 nowMs )
        {
            _service->setOffer( actorId, tradeId, listLeg, nowMs, _nextTag++ );
            return settle();
        }
        TradeCompletion lock( AccountId actorId, uint64 tradeId, int64 nowMs )
        {
            _service->lock( actorId, tradeId, nowMs, _nextTag++ );
            return settle();
        }
        TradeCompletion confirm( AccountId actorId, uint64 tradeId, uint32 own, uint32 peer, int64 nowMs )
        {
            _service->confirm( actorId, tradeId, own, peer, nowMs, _nextTag++ );
            return settle();
        }
        TradeCompletion cancel( AccountId actorId, uint64 tradeId, int64 nowMs )
        {
            _service->cancel( actorId, tradeId, nowMs, _nextTag++ );
            return settle();
        }
        vector<TradeSnapshot> drainUpdates()
        {
            vector<TradeSnapshot> listUpdate;
            _service->drainUpdates( listUpdate );
            return listUpdate;
        }
    };

    struct TestTradeServiceInternal
    {
        static void grant( MemoryServiceDatabase& database, AccountId accountId, const utf8* pAsset, int64 amount )
        {
            LedgerTransferRequest request;
            request._journalKey = string( "test/" ) + pAsset + "." + ServiceKeyUtil::makeHex64( accountId );
            request._reason     = "test.grant";
            request._listPosting.push_back( LedgerPosting{ LedgerHolder::makeMint(), LedgerHolder::makeAccount( accountId ), pAsset, amount } );
            LedgerTransferOutcome outcome;
            (void)Ledger::executeTransfer( database, request, outcome );
        }

        static int64 readAmount( MemoryServiceDatabase& database, AccountId accountId, const utf8* pAsset )
        {
            LedgerBalance balance;
            // 실패면 balance 가 0 으로 남아 호출한 단언이 틀린 값으로 잡는다
            (void)Ledger::readBalance( database, LedgerHolder::makeAccount( accountId ), pAsset, balance );
            return balance._amount;
        }

        static bool isBalanced( MemoryServiceDatabase& database )
        {
            LedgerAuditReport report;
            return LedgerAudit::computeReport( database, report ) == ServiceStoreResult::Ok && report.isBalanced();
        }

        /** @brief A: 칼 1 · 금 100, B: 금 500 — 그리고 A ↔ B 거래를 Open 까지. */
        static uint64 openTrade( MemoryServiceDatabase& database, TradeNode& node, int64 nowMs )
        {
            grant( database, kAlice, "item.sword", 1 );
            grant( database, kAlice, "cur.gold", 100 );
            grant( database, kBob, "cur.gold", 500 );
            const TradeCompletion invited = node.invite( kAlice, kBob, nowMs );
            if ( invited._result != TradeResult::Ok )
                return 0;
            const TradeCompletion opened = node.respond( kBob, invited._snapshot._tradeId, true, nowMs );
            return opened._result == TradeResult::Ok ? invited._snapshot._tradeId : 0;
        }

        /** @brief 칼 ↔ 금 300 을 걸고 둘 다 잠근다. A · B 의 제시 판을 돌려준다. */
        static bool offerAndLock( TradeNode& node, uint64 tradeId, int64 nowMs )
        {
            return node.setOffer( kAlice, tradeId, {
                                                       TradeLeg{ "item.sword", 1 }
            },
                                  nowMs )
                           ._result == TradeResult::Ok &&
                   node.setOffer( kBob, tradeId, { TradeLeg{ "cur.gold", 300 } }, nowMs )._result == TradeResult::Ok && node.lock( kAlice, tradeId, nowMs )._result == TradeResult::Ok && node.lock( kBob, tradeId, nowMs )._result == TradeResult::Ok;
        }
    };
} // namespace

SW_TEST_CASE( TradeServiceTest, HappyPathMovesBothSidesInOneJournal )
{
    using Internal = TestTradeServiceInternal;
    MemoryServiceDatabase database;
    TradeNode             node{ &database, 1 };
    const uint64          tradeId = Internal::openTrade( database, node, 1000 );
    SW_ASSERT_TRUE( tradeId != 0 );
    SW_ASSERT_TRUE( Internal::offerAndLock( node, tradeId, 1100 ) );
    const int32 journalBefore = database.countRecords( Ledger::getJournalTable() );
    SW_EXPECT_TRUE( node.confirm( kAlice, tradeId, 1, 1, 1200 )._result == TradeResult::Ok );
    const TradeCompletion settled = node.confirm( kBob, tradeId, 1, 1, 1300 );
    SW_ASSERT_TRUE( settled._result == TradeResult::Ok );
    SW_EXPECT_TRUE( settled._snapshot._state == TradeState::Settled );
    SW_EXPECT_EQUAL( size_t( 4 ), settled._ledger._listHolderBalance.size() ); // 두 계정 × 두 자산
    SW_EXPECT_EQUAL( int64( 0 ), Internal::readAmount( database, kAlice, "item.sword" ) );
    SW_EXPECT_EQUAL( int64( 400 ), Internal::readAmount( database, kAlice, "cur.gold" ) );
    SW_EXPECT_EQUAL( int64( 1 ), Internal::readAmount( database, kBob, "item.sword" ) );
    SW_EXPECT_EQUAL( int64( 200 ), Internal::readAmount( database, kBob, "cur.gold" ) );
    SW_EXPECT_EQUAL( journalBefore + 1, database.countRecords( Ledger::getJournalTable() ) ); // 분개 하나
    SW_EXPECT_EQUAL( 1, database.countRecords( ServiceAuditLog::getTable() ) );
    SW_EXPECT_EQUAL( 0, database.countRecords( TradeStoreLogic::getActiveTable() ) );
    SW_EXPECT_EQUAL( 0, database.countRecords( TradeStoreLogic::getOwnerTable() ) );
    SW_EXPECT_TRUE( Internal::isBalanced( database ) );
    const vector<TradeSnapshot> listUpdate = node.drainUpdates();
    SW_ASSERT_TRUE( listUpdate.empty() == false );
    SW_EXPECT_TRUE( listUpdate.back()._state == TradeState::Settled ); // 바인딩이 두 당사자에게 알린다
}

SW_TEST_CASE( TradeServiceTest, ChangingTheOfferAfterLockClearsLocksAndConfirms )
{
    using Internal = TestTradeServiceInternal;
    MemoryServiceDatabase database;
    TradeNode             node{ &database, 1 };
    const uint64          tradeId = Internal::openTrade( database, node, 1000 );
    SW_ASSERT_TRUE( Internal::offerAndLock( node, tradeId, 1100 ) );
    SW_ASSERT_TRUE( node.confirm( kAlice, tradeId, 1, 1, 1150 )._result == TradeResult::Ok );
    const TradeCompletion changed = node.setOffer( kBob, tradeId, {
                                                                      TradeLeg{ "cur.gold", 200 }
    },
                                                   1200 ); // 미끼 바꿔치기
    SW_ASSERT_TRUE( changed._result == TradeResult::Ok );
    SW_EXPECT_EQUAL( uint32( 2 ), changed._snapshot._arrSide[1]._offerRevision );
    for ( const TradeSide& side : changed._snapshot._arrSide )
    {
        SW_EXPECT_TRUE( side._bLocked == SW_FALSE );
        SW_EXPECT_TRUE( side._bConfirmed == SW_FALSE );
    }
    SW_EXPECT_TRUE( node.confirm( kAlice, tradeId, 1, 1, 1300 )._result == TradeResult::WrongState ); // 잠금이 풀렸다
    SW_ASSERT_TRUE( node.lock( kAlice, tradeId, 1310 )._result == TradeResult::Ok );
    SW_ASSERT_TRUE( node.lock( kBob, tradeId, 1320 )._result == TradeResult::Ok );
    SW_EXPECT_TRUE( node.confirm( kAlice, tradeId, 1, 1, 1330 )._result == TradeResult::StaleOffer ); // 옛 판을 본 확정
    SW_EXPECT_TRUE( node.confirm( kAlice, tradeId, 1, 2, 1340 )._result == TradeResult::Ok );
}

SW_TEST_CASE( TradeServiceTest, InsufficientAtSettleFailsWithoutMovement )
{
    using Internal = TestTradeServiceInternal;
    MemoryServiceDatabase database;
    TradeNode             node{ &database, 1 };
    const uint64          tradeId = Internal::openTrade( database, node, 1000 );
    SW_ASSERT_TRUE( Internal::offerAndLock( node, tradeId, 1100 ) );
    // 잠근 뒤 A 의 칼이 다른 데로 갔다(GM 회수) — 잠금 때의 미리 보기는 안내일 뿐, 정산이 다시 본다.
    LedgerTransferRequest clawback;
    clawback._journalKey = "test/clawback";
    clawback._reason     = "admin.revoke";
    clawback._listPosting.push_back( LedgerPosting{ LedgerHolder::makeAccount( kAlice ), LedgerHolder::makeSink(), "item.sword", 1 } );
    LedgerTransferOutcome outcome;
    SW_ASSERT_TRUE( Ledger::executeTransfer( database, clawback, outcome ) == LedgerResult::Ok );
    const int64 bobGold = Internal::readAmount( database, kBob, "cur.gold" );
    SW_ASSERT_TRUE( node.confirm( kAlice, tradeId, 1, 1, 1200 )._result == TradeResult::Ok );
    const TradeCompletion failed = node.confirm( kBob, tradeId, 1, 1, 1300 );
    SW_EXPECT_TRUE( failed._result == TradeResult::InsufficientFunds );
    SW_EXPECT_TRUE( failed._snapshot._state == TradeState::Failed );
    SW_EXPECT_TRUE( failed._snapshot._closeReason == TradeCloseReason::InsufficientFunds );
    SW_EXPECT_EQUAL( bobGold, Internal::readAmount( database, kBob, "cur.gold" ) );
    SW_EXPECT_EQUAL( int64( 0 ), Internal::readAmount( database, kBob, "item.sword" ) );
    SW_EXPECT_EQUAL( 0, database.countRecords( TradeStoreLogic::getActiveTable() ) );                      // 실패도 닫혀 링크가 풀린다
    SW_EXPECT_TRUE( node.confirm( kBob, tradeId, 1, 1, 1400 )._result == TradeResult::InsufficientFunds ); // 재시도는 같은 결과
    SW_EXPECT_TRUE( Internal::isBalanced( database ) );
}

SW_TEST_CASE( TradeServiceTest, OneOpenTradePerAccountAcrossServers )
{
    using Internal = TestTradeServiceInternal;
    MemoryServiceDatabase database;
    TradeNode             serverA{ &database, 1 };
    TradeNode             serverB{ &database, 2 }; // 앞 둘 · 데이터 하나
    const uint64          tradeId = Internal::openTrade( database, serverA, 1000 );
    SW_ASSERT_TRUE( tradeId != 0 );
    SW_EXPECT_TRUE( serverB.invite( kAlice, kCarol, 1100 )._result == TradeResult::AlreadyTrading );
    SW_EXPECT_TRUE( serverB.invite( kCarol, kBob, 1100 )._result == TradeResult::PeerBusy );
    SW_EXPECT_TRUE( serverB.invite( kCarol, kCarol, 1100 )._result == TradeResult::Invalid );
    // 상대가 다른 서버에 붙어 있어도 상대 서버가 같은 레코드를 바꾼다
    SW_EXPECT_TRUE( serverB.setOffer( kBob, tradeId, {
                                                         TradeLeg{ "cur.gold", 10 }
    },
                                      1200 )
                        ._result == TradeResult::Ok );
    SW_EXPECT_TRUE( serverA.cancel( kAlice, tradeId, 1300 )._result == TradeResult::Ok );
    SW_EXPECT_TRUE( serverB.invite( kAlice, kCarol, 1400 )._result == TradeResult::Ok ); // 닫히면 다시 열 수 있다
}

SW_TEST_CASE( TradeServiceTest, LostCommitReplyStillSettlesOnce )
{
    using Internal = TestTradeServiceInternal;
    MemoryServiceDatabase database;
    TradeNode             node{ &database, 1 };
    const uint64          tradeId = Internal::openTrade( database, node, 1000 );
    SW_ASSERT_TRUE( Internal::offerAndLock( node, tradeId, 1100 ) );
    SW_ASSERT_TRUE( node.confirm( kAlice, tradeId, 1, 1, 1200 )._result == TradeResult::Ok );
    database.armFault( ServiceStoreFault::LoseCommitReply );
    const TradeCompletion settled = node.confirm( kBob, tradeId, 1, 1, 1300 );
    SW_EXPECT_TRUE( settled._result == TradeResult::Ok );
    SW_EXPECT_TRUE( settled._snapshot._state == TradeState::Settled );
    SW_EXPECT_TRUE( node.confirm( kBob, tradeId, 1, 1, 1400 )._result == TradeResult::Ok ); // 클라이언트의 확정 재시도
    SW_EXPECT_EQUAL( int64( 1 ), Internal::readAmount( database, kBob, "item.sword" ) );
    SW_EXPECT_EQUAL( int64( 200 ), Internal::readAmount( database, kBob, "cur.gold" ) ); // 한 번만
    SW_EXPECT_TRUE( Internal::isBalanced( database ) );
}

SW_TEST_CASE( TradeServiceTest, RejectedCommitLeavesNothingAndRetrySettles )
{
    using Internal = TestTradeServiceInternal;
    MemoryServiceDatabase database;
    TradeNode             node{ &database, 1 };
    const uint64          tradeId = Internal::openTrade( database, node, 1000 );
    SW_ASSERT_TRUE( Internal::offerAndLock( node, tradeId, 1100 ) );
    SW_ASSERT_TRUE( node.confirm( kAlice, tradeId, 1, 1, 1200 )._result == TradeResult::Ok );
    const uint64 before = database.computeContentHash();
    database.armFault( ServiceStoreFault::RejectCommit );
    SW_EXPECT_TRUE( node.confirm( kBob, tradeId, 1, 1, 1300 )._result == TradeResult::Unavailable );
    SW_EXPECT_EQUAL( before, database.computeContentHash() );
    SW_EXPECT_TRUE( node.confirm( kBob, tradeId, 1, 1, 1400 )._result == TradeResult::Ok );
    SW_EXPECT_EQUAL( int64( 1 ), Internal::readAmount( database, kBob, "item.sword" ) );
    SW_EXPECT_TRUE( Internal::isBalanced( database ) );
}

SW_TEST_CASE( TradeServiceTest, ServerRestartCancelsOwnedOpenTradesWithoutMovement )
{
    using Internal = TestTradeServiceInternal;
    MemoryServiceDatabase database;
    TradeNode             serverA{ &database, 1 };
    TradeNode             serverB{ &database, 2 };
    const uint64          tradeId = Internal::openTrade( database, serverA, 1000 );
    SW_ASSERT_TRUE( Internal::offerAndLock( serverA, tradeId, 1100 ) );
    const TradeCompletion other = serverB.invite( kCarol, 0xD0D, 1100 ); // 다른 서버 주인의 거래
    SW_ASSERT_TRUE( other._result == TradeResult::Ok );

    serverA.restart();
    serverA._service->recoverOwnedTrades( 2000 );
    (void)serverA.settle();
    const vector<TradeSnapshot> listUpdate = serverA.drainUpdates();
    SW_ASSERT_EQUAL( size_t( 1 ), listUpdate.size() );
    SW_EXPECT_EQUAL( tradeId, listUpdate[0]._tradeId );
    SW_EXPECT_TRUE( listUpdate[0]._closeReason == TradeCloseReason::ServerRestart );
    SW_EXPECT_TRUE( serverA.confirm( kAlice, tradeId, 1, 1, 2100 )._result == TradeResult::WrongState );
    SW_EXPECT_EQUAL( int64( 1 ), Internal::readAmount( database, kAlice, "item.sword" ) );
    SW_EXPECT_EQUAL( 2, database.countRecords( TradeStoreLogic::getActiveTable() ) ); // 다른 서버의 거래는 그대로
}

SW_TEST_CASE( TradeServiceTest, PartyLeavingAndIdleTimeoutCancel )
{
    using Internal = TestTradeServiceInternal;
    MemoryServiceDatabase database;
    TradeNode             node{ &database, 1 };
    const uint64          tradeId = Internal::openTrade( database, node, 1000 );
    node._service->closeForAccount( kBob, TradeCloseReason::PartyLeft, 1100 );
    (void)node.settle();
    vector<TradeSnapshot> listUpdate = node.drainUpdates();
    SW_ASSERT_TRUE( listUpdate.empty() == false );
    SW_EXPECT_TRUE( listUpdate.back()._closeReason == TradeCloseReason::PartyLeft );
    SW_EXPECT_TRUE( node.lock( kAlice, tradeId, 1200 )._result == TradeResult::WrongState );

    const TradeCompletion idle = node.invite( kAlice, kBob, 2000 );
    SW_ASSERT_TRUE( idle._result == TradeResult::Ok );
    (void)node.drainUpdates();                                         // 신청 알림
    node._service->tick( 2000 + TradeConstant::kInviteTimeoutMs - 1 ); // 아직
    (void)node.settle();
    SW_EXPECT_TRUE( node.drainUpdates().empty() );
    node._service->tick( 2000 + TradeConstant::kInviteTimeoutMs );
    (void)node.settle();
    listUpdate = node.drainUpdates();
    SW_ASSERT_EQUAL( size_t( 1 ), listUpdate.size() );
    SW_EXPECT_TRUE( listUpdate[0]._closeReason == TradeCloseReason::Timeout );
    SW_EXPECT_EQUAL( 0, database.countRecords( TradeStoreLogic::getActiveTable() ) );

    // 시한이 지났는데 아무도 닫지 않은 거래(다른 서버가 열었다) — 다음 신청이 그 자리에서 정리한다.
    TradeNode    otherServer{ &database, 9 };
    const uint64 staleId = otherServer.invite( kAlice, kCarol, 5000 )._snapshot._tradeId;
    SW_ASSERT_TRUE( staleId != 0 );
    SW_EXPECT_TRUE( node.invite( kAlice, kBob, 5000 + TradeConstant::kInviteTimeoutMs - 1 )._result == TradeResult::AlreadyTrading );
    SW_EXPECT_TRUE( node.invite( kAlice, kBob, 5000 + TradeConstant::kInviteTimeoutMs )._result == TradeResult::Ok );
}

SW_TEST_CASE( TradeServiceTest, UntradableAssetsTooManyLegsAndDuplicateConfirm )
{
    using Internal = TestTradeServiceInternal;
    MemoryServiceDatabase database;
    TradeNode             node{ &database, 1 };
    const uint64          tradeId = Internal::openTrade( database, node, 1000 );
    SW_EXPECT_TRUE( node.setOffer( kAlice, tradeId, {
                                                        TradeLeg{ "soul.bound", 1 }
    },
                                   1100 )
                        ._result == TradeResult::NotTradable );
    vector<TradeLeg> listTooMany;
    for ( int32 index = 0; index <= TradeConstant::kMaxLegsPerSide; ++index )
    {
        listTooMany.push_back( TradeLeg{ "item.gem" + to_string( index ), 1 } );
    }
    SW_EXPECT_TRUE( node.setOffer( kAlice, tradeId, listTooMany, 1100 )._result == TradeResult::TooManyLegs );
    SW_EXPECT_TRUE( node.setOffer( kAlice, tradeId, {
                                                        TradeLeg{"item.sword", 1},
                                                        TradeLeg{"item.sword", 1}
    },
                                   1100 )
                        ._result == TradeResult::Invalid );
    SW_EXPECT_TRUE( node.setOffer( kAlice, tradeId, {
                                                        TradeLeg{ "item.sword", 0 }
    },
                                   1100 )
                        ._result == TradeResult::Invalid );
    SW_EXPECT_TRUE( node.setOffer( kAlice, tradeId, {
                                                        TradeLeg{ "item.sword", 2 }
    },
                                   1100 )
                        ._result == TradeResult::Ok );
    SW_EXPECT_TRUE( node.lock( kAlice, tradeId, 1200 )._result == TradeResult::InsufficientFunds ); // 칼은 하나뿐 — 잠글 때 미리 본다
    SW_ASSERT_TRUE( Internal::offerAndLock( node, tradeId, 1300 ) );
    SW_EXPECT_TRUE( node.confirm( kAlice, tradeId, 2, 1, 1400 )._result == TradeResult::Ok );
    SW_EXPECT_TRUE( node.confirm( kAlice, tradeId, 2, 1, 1410 )._result == TradeResult::Ok ); // 같은 확정 두 번 — 멱등
    SW_EXPECT_TRUE( node.setOffer( kCarol, tradeId, {
                                                        TradeLeg{ "cur.gold", 1 }
    },
                                   1420 )
                        ._result == TradeResult::NotParty );
}

SW_TEST_CASE( TradeServiceTest, StateMachineTable )
{
    DefaultTradePolicy     policy;
    const TradeState       arrState[] = { TradeState::Invited, TradeState::Open, TradeState::Settled, TradeState::Failed, TradeState::Cancelled };
    const TradeCommandKind arrKind[]  = { TradeCommandKind::Accept, TradeCommandKind::Decline, TradeCommandKind::SetOffer, TradeCommandKind::Lock,
                                          TradeCommandKind::Confirm, TradeCommandKind::Cancel };
    for ( const TradeState state : arrState )
    {
        for ( const TradeCommandKind kind : arrKind )
        {
            for ( int32 sideIndex = 0; sideIndex < 2; ++sideIndex )
            {
                TradeSnapshot trade;
                trade._state                 = state;
                trade._arrSide[0]._accountId = kAlice;
                trade._arrSide[1]._accountId = kBob;
                trade._arrSide[0]._bLocked   = SW_TRUE; // Confirm 칸이 잠금 조건에 걸리지 않게
                trade._arrSide[1]._bLocked   = SW_TRUE;
                TradeCommand command;
                command._actorId = sideIndex == 0 ? kAlice : kBob;
                command._kind    = kind;
                command._listLeg = {
                    TradeLeg{ "cur.gold", 1 }
                };
                const TradeResult result   = TradeStateMachine::apply( trade, command, policy, 10 );
                const bool        bClosed  = state == TradeState::Settled || state == TradeState::Failed || state == TradeState::Cancelled;
                bool              bAllowed = false;
                if ( state == TradeState::Invited )
                    bAllowed = kind == TradeCommandKind::Cancel || ( sideIndex == 1 && ( kind == TradeCommandKind::Accept || kind == TradeCommandKind::Decline ) );
                else if ( state == TradeState::Open )
                    bAllowed = kind == TradeCommandKind::SetOffer || kind == TradeCommandKind::Lock || kind == TradeCommandKind::Confirm || kind == TradeCommandKind::Cancel;
                SW_EXPECT_TRUE( bClosed == false || bAllowed == false );
                SW_EXPECT_EQUAL( bAllowed ? int32( TradeResult::Ok ) : int32( TradeResult::WrongState ), int32( result ) );
            }
        }
    }
}
