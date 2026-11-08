// TradeStateMachineTest — 거래 상태 기계: 초대 → 수락(받은 쪽만) → 제시 · 잠금 · 확정(양쪽 잠금, 본 버전이 지금 버전), 제시를 바꾸면 양쪽 잠금 · 확정이 풀림,
// 확정은 멱등, 취소 · 거절은 닫음, 닫힌 거래 · 당사자 아님 · 제시 규칙(항목 수 · 수량 · 인스턴스 아이템 1 개 · 거래 가능 · 같은 자산 중복). 저장소를 모르는 순수 함수다.
// 온라인 키트라 `UnitTests.OnlineKits` 가 돌린다(nogpu).
#include "pch.h"

#include "Core/Testing/TestFramework.h"

#include "GameFramework/Kits/Feature/Online/Trade/Server/TradeStateMachine.h"
#include "GameFramework/Runtime/Online/Ledger/LedgerItemInstance.h"
#include "GameFramework/Runtime/Online/Ledger/LedgerTypes.h"

using namespace sw;

namespace
{
    constexpr AccountID kInviter = 11;
    constexpr AccountID kInvitee = 22;

    TradeSnapshot makeTrade( TradeState state )
    {
        TradeSnapshot trade;
        trade._arrSide[0]._accountID = kInviter;
        trade._arrSide[1]._accountID = kInvitee;
        trade._state                 = state;
        return trade;
    }

    TradeCommand makeCommand( TradeCommandKind kind, AccountID actorID )
    {
        TradeCommand command;
        command._kind    = kind;
        command._actorID = actorID;
        return command;
    }

    TradeCommand makeOffer( AccountID actorID, vector<TradeLeg> listLeg )
    {
        TradeCommand command = makeCommand( TradeCommandKind::SetOffer, actorID );
        command._listLeg     = std::move( listLeg );
        return command;
    }

    TradeCommand makeConfirm( const TradeSnapshot& trade, AccountID actorID )
    {
        const int32  sideIndex    = trade.findSideIndex( actorID ).value();
        TradeCommand command      = makeCommand( TradeCommandKind::Confirm, actorID );
        command._seenOwnRevision  = trade._arrSide[sideIndex]._offerRevision;
        command._seenPeerRevision = trade._arrSide[1 - sideIndex]._offerRevision;
        return command;
    }

    TradeLeg makeLeg( string assetID, int64 amount )
    {
        TradeLeg leg;
        leg._assetID = std::move( assetID );
        leg._amount  = amount;
        return leg;
    }

    const DefaultTradePolicy& getPolicy()
    {
        static const DefaultTradePolicy s_policy;
        return s_policy;
    }
} // namespace

/** @brief [TradeStateMachineTest] 초대는 받은 쪽만 수락 · 거절한다 — 신청한 쪽의 수락은 WrongState, 거절은 Declined 로 닫는다 */
SW_TEST_CASE( TradeStateMachineTest, OnlyTheInviteeAnswersTheInvite )
{
    TradeSnapshot trade = makeTrade( TradeState::Invited );
    SW_EXPECT_TRUE( TradeStateMachine::apply( trade, makeCommand( TradeCommandKind::Accept, kInviter ), getPolicy(), 10 ) == TradeResult::WrongState );
    SW_EXPECT_TRUE( trade._state == TradeState::Invited );
    SW_EXPECT_TRUE( TradeStateMachine::apply( trade, makeCommand( TradeCommandKind::Accept, kInvitee ), getPolicy(), 20 ) == TradeResult::Ok );
    SW_EXPECT_TRUE( trade._state == TradeState::Open );
    SW_EXPECT_EQUAL( trade._updatedMs, 20 );

    TradeSnapshot declined = makeTrade( TradeState::Invited );
    SW_EXPECT_TRUE( TradeStateMachine::apply( declined, makeCommand( TradeCommandKind::Decline, kInvitee ), getPolicy(), 30 ) == TradeResult::Ok );
    SW_EXPECT_TRUE( declined._state == TradeState::Cancelled && declined._closeReason == TradeCloseReason::Declined );
    SW_EXPECT_TRUE( TradeStateMachine::apply( declined, makeCommand( TradeCommandKind::Accept, kInvitee ), getPolicy(), 40 ) == TradeResult::WrongState ); // 닫혔다
}

/** @brief [TradeStateMachineTest] 당사자가 아니면 NotParty 이고 거래는 그대로다 */
SW_TEST_CASE( TradeStateMachineTest, StrangersCannotTouchTheTrade )
{
    TradeSnapshot       trade  = makeTrade( TradeState::Open );
    const TradeSnapshot before = trade;
    SW_EXPECT_TRUE( TradeStateMachine::apply( trade, makeCommand( TradeCommandKind::Cancel, 99 ), getPolicy(), 10 ) == TradeResult::NotParty );
    SW_EXPECT_TRUE( trade._state == before._state && trade._updatedMs == before._updatedMs );
}

/** @brief [TradeStateMachineTest] 양쪽이 잠그고 본 버전으로 확정한다 — 한쪽만 잠갔으면 WrongState, 확정은 두 번 해도 Ok(멱등) */
SW_TEST_CASE( TradeStateMachineTest, ConfirmNeedsBothLocksAndIsIdempotent )
{
    TradeSnapshot trade = makeTrade( TradeState::Open );
    SW_EXPECT_TRUE( TradeStateMachine::apply( trade, makeOffer( kInviter, { makeLeg( "cur.gold", 100 ) } ), getPolicy(), 1 ) == TradeResult::Ok );
    SW_EXPECT_TRUE( TradeStateMachine::apply( trade, makeOffer( kInvitee, { makeLeg( "item.sword", 1 ) } ), getPolicy(), 2 ) == TradeResult::Ok );
    SW_EXPECT_TRUE( TradeStateMachine::apply( trade, makeCommand( TradeCommandKind::Lock, kInviter ), getPolicy(), 3 ) == TradeResult::Ok );
    SW_EXPECT_TRUE( TradeStateMachine::apply( trade, makeConfirm( trade, kInviter ), getPolicy(), 4 ) == TradeResult::WrongState ); // 상대가 아직 잠그지 않았다
    SW_EXPECT_TRUE( TradeStateMachine::apply( trade, makeCommand( TradeCommandKind::Lock, kInvitee ), getPolicy(), 5 ) == TradeResult::Ok );
    SW_EXPECT_TRUE( TradeStateMachine::apply( trade, makeConfirm( trade, kInviter ), getPolicy(), 6 ) == TradeResult::Ok );
    SW_EXPECT_TRUE( TradeStateMachine::apply( trade, makeConfirm( trade, kInviter ), getPolicy(), 7 ) == TradeResult::Ok );
    SW_EXPECT_TRUE( trade.isBothConfirmed() == false );
    SW_EXPECT_TRUE( TradeStateMachine::apply( trade, makeConfirm( trade, kInvitee ), getPolicy(), 8 ) == TradeResult::Ok );
    SW_EXPECT_TRUE( trade.isBothConfirmed() ); // 정산은 서비스가 한다 — 상태 기계는 Open 에 둔다
    SW_EXPECT_TRUE( trade._state == TradeState::Open );
}

/** @brief [TradeStateMachineTest] 제시를 바꾸면 버전이 오르고 양쪽 잠금 · 확정이 풀린다. 예전 버전을 본 확정은 StaleOffer 다 */
SW_TEST_CASE( TradeStateMachineTest, ChangingAnOfferUnlocksBothSidesAndStalesConfirms )
{
    TradeSnapshot trade = makeTrade( TradeState::Open );
    SW_EXPECT_TRUE( TradeStateMachine::apply( trade, makeOffer( kInviter, { makeLeg( "cur.gold", 100 ) } ), getPolicy(), 1 ) == TradeResult::Ok );
    SW_EXPECT_TRUE( TradeStateMachine::apply( trade, makeCommand( TradeCommandKind::Lock, kInviter ), getPolicy(), 2 ) == TradeResult::Ok );
    SW_EXPECT_TRUE( TradeStateMachine::apply( trade, makeCommand( TradeCommandKind::Lock, kInvitee ), getPolicy(), 3 ) == TradeResult::Ok );
    const TradeCommand seenBefore = makeConfirm( trade, kInvitee );

    SW_EXPECT_TRUE( TradeStateMachine::apply( trade, makeOffer( kInviter, { makeLeg( "cur.gold", 1 ) } ), getPolicy(), 4 ) == TradeResult::Ok );
    SW_EXPECT_EQUAL( trade._arrSide[0]._offerRevision, 2u );
    for ( const TradeSide& side : trade._arrSide )
        SW_EXPECT_TRUE( side._bLocked == SW_FALSE && side._bConfirmed == SW_FALSE );

    SW_EXPECT_TRUE( TradeStateMachine::apply( trade, makeCommand( TradeCommandKind::Lock, kInviter ), getPolicy(), 5 ) == TradeResult::Ok );
    SW_EXPECT_TRUE( TradeStateMachine::apply( trade, makeCommand( TradeCommandKind::Lock, kInvitee ), getPolicy(), 6 ) == TradeResult::Ok );
    SW_EXPECT_TRUE( TradeStateMachine::apply( trade, seenBefore, getPolicy(), 7 ) == TradeResult::StaleOffer );
    SW_EXPECT_TRUE( trade._arrSide[1]._bConfirmed == SW_FALSE );
}

/** @brief [TradeStateMachineTest] 제시 규칙 — 항목 수 상한, 수량 1 이상, 인스턴스 아이템은 1 개, 정책 밖 자산, 같은 자산 두 항목. 거절된 제시는 거래를 바꾸지 않는다 */
SW_TEST_CASE( TradeStateMachineTest, OfferRulesRejectBadLegs )
{
    vector<TradeLeg> listTooMany;
    for ( int32 index = 0; index <= TradeConstant::kMaxLegsPerSide; ++index )
        listTooMany.push_back( makeLeg( "item.gem" + to_string( index ), 1 ) );
    SW_EXPECT_TRUE( TradeStateMachine::validateOffer( listTooMany, getPolicy() ) == TradeResult::TooManyLegs );
    SW_EXPECT_TRUE( TradeStateMachine::validateOffer( { makeLeg( "cur.gold", 0 ) }, getPolicy() ).hasError( OnlineError::kInvalidRequest ) );
    SW_EXPECT_TRUE( TradeStateMachine::validateOffer( { makeLeg( "cur.gold", LedgerConstant::kMaxAmount + 1 ) }, getPolicy() ).hasError( OnlineError::kInvalidRequest ) );
    SW_EXPECT_TRUE( TradeStateMachine::validateOffer( { makeLeg( "xp.level", 1 ) }, getPolicy() ) == TradeResult::NotTradable );
    SW_EXPECT_TRUE( TradeStateMachine::validateOffer( { makeLeg( "cur.gold", 1 ), makeLeg( "cur.gold", 2 ) }, getPolicy() ).hasError( OnlineError::kInvalidRequest ) );
    SW_EXPECT_TRUE( TradeStateMachine::validateOffer( {}, getPolicy() ) == TradeResult::Ok ); // 빈 제시(선물 받기)도 된다

    string instanceID;
    SW_EXPECT_TRUE( LedgerItemInstance::makeAssetID( "sword", 0x1234, instanceID ) );
    SW_EXPECT_TRUE( TradeStateMachine::validateOffer( { makeLeg( instanceID, 1 ) }, getPolicy() ) == TradeResult::Ok );
    SW_EXPECT_TRUE( TradeStateMachine::validateOffer( { makeLeg( instanceID, 2 ) }, getPolicy() ).hasError( OnlineError::kInvalidRequest ) );

    TradeSnapshot trade = makeTrade( TradeState::Open );
    SW_EXPECT_TRUE( TradeStateMachine::apply( trade, makeOffer( kInviter, { makeLeg( "xp.level", 1 ) } ), getPolicy(), 9 ) == TradeResult::NotTradable );
    SW_EXPECT_EQUAL( trade._arrSide[0]._offerRevision, 0u );
    SW_EXPECT_EQUAL( trade._updatedMs, 0 );
}

/** @brief [TradeStateMachineTest] 취소는 어느 상태에서든 닫고, 닫은 뒤의 닫기는 이유를 바꾸지 않는다 */
SW_TEST_CASE( TradeStateMachineTest, CancelClosesOnceAndKeepsTheFirstReason )
{
    TradeSnapshot trade  = makeTrade( TradeState::Open );
    TradeCommand  cancel = makeCommand( TradeCommandKind::Cancel, kInviter );
    cancel._reason       = TradeCloseReason::PartyLeft;
    SW_EXPECT_TRUE( TradeStateMachine::apply( trade, cancel, getPolicy(), 5 ) == TradeResult::Ok );
    SW_EXPECT_TRUE( trade._state == TradeState::Cancelled && trade._closeReason == TradeCloseReason::PartyLeft );
    TradeStateMachine::close( trade, TradeState::Cancelled, TradeCloseReason::Timeout, 9 );
    SW_EXPECT_TRUE( trade._closeReason == TradeCloseReason::PartyLeft );
    SW_EXPECT_EQUAL( trade._updatedMs, 5 );
    SW_EXPECT_TRUE( TradeStateMachine::apply( trade, cancel, getPolicy(), 10 ) == TradeResult::WrongState );
}
