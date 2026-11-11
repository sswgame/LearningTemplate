// AdminStoreLogicTest — GM 명령 저장 규칙(메모리 저장소): 등급이 모자라면 Forbidden 이고 아무것도 바꾸지 않으며, 지급 · 회수는 같은 멱등 키로 한 번만 적용되고,
// 회수는 잔액 아래로 내려가지 않으며(InsufficientFunds), 상태를 바꾸는 명령은 메모와 멱등 키가 필수다. 온라인 키트라 `UnitTests.OnlineKits` 가 돌린다(nogpu).
#include "pch.h"

#include "Core/Testing/TestFramework.h"

#include "GameFramework/Kits/Feature/Online/Admin/Server/AdminStoreLogic.h"
#include "GameFramework/Runtime/Online/Ledger/Server/Ledger.h"
#include "GameFramework/Runtime/Online/Store/Server/MemoryServiceStore.h"

using namespace sw;

namespace
{
    constexpr AccountID kOperator = 7;
    constexpr AccountID kViewer   = 8;
    constexpr AccountID kPlayer   = 99;
    constexpr int64     kNowMs    = 1700000000000ll;

    AdminCommand makeAdjust( AccountID adminID, int64 amount, uint64 keyLow )
    {
        AdminCommand command;
        command._method             = AdminMethod::AdjustAsset::kID;
        command._adminID            = adminID;
        command._keyHigh            = 1;
        command._keyLow             = keyLow;
        command._nowMs              = kNowMs;
        command._request._accountID = kPlayer;
        command._request._assetID   = "cur.gold";
        command._request._amount    = amount;
        command._request._memo      = "ticket-123";
        return command;
    }

    int64 getGoldOf( MemoryServiceDatabase& database )
    {
        LedgerBalance balance;
        SW_EXPECT_TRUE( Ledger::readBalance( database, LedgerHolder::makeAccount( kPlayer ), "cur.gold", balance ) == ServiceStoreResult::Ok );
        return balance._amount;
    }

    struct Fixture
    {
        MemoryServiceDatabase database;

        Fixture()
        {
            SW_EXPECT_TRUE( AdminStoreLogic::seedRole( database, kOperator, AdminRole::Operator, kNowMs ) == AdminResult::Ok );
            SW_EXPECT_TRUE( AdminStoreLogic::seedRole( database, kViewer, AdminRole::Viewer, kNowMs ) == AdminResult::Ok );
        }
    };
} // namespace

/** @brief [AdminStoreLogicTest] 조회 등급(Viewer)은 지급할 수 없다 — Forbidden 이고 저장소는 그대로다. 등급이 없는 계정도 같다 */
SW_TEST_CASE( AdminStoreLogicTest, ViewerCannotAdjustAssets )
{
    Fixture      fixture;
    const uint64 hashBefore = fixture.database.computeContentHash();
    AdminReply   reply;
    SW_EXPECT_TRUE( AdminStoreLogic::execute( fixture.database, makeAdjust( kViewer, 100, 1 ), reply ).hasError( OnlineError::kForbidden ) );
    SW_EXPECT_TRUE( AdminStoreLogic::execute( fixture.database, makeAdjust( kPlayer, 100, 2 ), reply ).hasError( OnlineError::kForbidden ) );
    SW_EXPECT_EQUAL( fixture.database.computeContentHash(), hashBefore );
}

/** @brief [AdminStoreLogicTest] 운영 등급의 지급은 같은 멱등 키로 다시 보내도 한 번만 적용된다(감사 기록도 하나) */
SW_TEST_CASE( AdminStoreLogicTest, AdjustIsIdempotent )
{
    Fixture    fixture;
    AdminReply reply;
    SW_REQUIRE_TRUE( AdminStoreLogic::execute( fixture.database, makeAdjust( kOperator, 100, 3 ), reply ) == AdminResult::Ok );
    const uint64 hashAfterFirst = fixture.database.computeContentHash();
    SW_EXPECT_TRUE( AdminStoreLogic::execute( fixture.database, makeAdjust( kOperator, 100, 3 ), reply ) == AdminResult::Ok );
    SW_EXPECT_EQUAL( fixture.database.computeContentHash(), hashAfterFirst );
    SW_EXPECT_EQUAL( getGoldOf( fixture.database ), int64( 100 ) );
}

/** @brief [AdminStoreLogicTest] 회수는 잔액 아래로 내려가지 않는다 — 넘치는 회수는 InsufficientFunds 이고 잔액은 그대로다 */
SW_TEST_CASE( AdminStoreLogicTest, RevokeCannotGoNegative )
{
    Fixture    fixture;
    AdminReply reply;
    SW_REQUIRE_TRUE( AdminStoreLogic::execute( fixture.database, makeAdjust( kOperator, 50, 4 ), reply ) == AdminResult::Ok );
    SW_EXPECT_TRUE( AdminStoreLogic::execute( fixture.database, makeAdjust( kOperator, -80, 5 ), reply ) == AdminResult::InsufficientFunds );
    SW_EXPECT_EQUAL( getGoldOf( fixture.database ), int64( 50 ) );
    SW_EXPECT_TRUE( AdminStoreLogic::execute( fixture.database, makeAdjust( kOperator, -50, 6 ), reply ) == AdminResult::Ok );
    SW_EXPECT_EQUAL( getGoldOf( fixture.database ), int64( 0 ) );
}

/** @brief [AdminStoreLogicTest] 상태를 바꾸는 명령은 메모(티켓)와 멱등 키가 필수다 — 없으면 InvalidRequest 이고 아무것도 바꾸지 않는다 */
SW_TEST_CASE( AdminStoreLogicTest, MutationsNeedMemoAndKey )
{
    Fixture      fixture;
    const uint64 hashBefore = fixture.database.computeContentHash();
    AdminReply   reply;
    AdminCommand noMemo = makeAdjust( kOperator, 10, 7 );
    noMemo._request._memo.clear();
    SW_EXPECT_TRUE( AdminStoreLogic::execute( fixture.database, noMemo, reply ).hasError( OnlineError::kInvalidRequest ) );
    AdminCommand noKey = makeAdjust( kOperator, 10, 0 );
    noKey._keyHigh     = 0;
    SW_EXPECT_TRUE( AdminStoreLogic::execute( fixture.database, noKey, reply ).hasError( OnlineError::kInvalidRequest ) );
    SW_EXPECT_EQUAL( fixture.database.computeContentHash(), hashBefore );
}
