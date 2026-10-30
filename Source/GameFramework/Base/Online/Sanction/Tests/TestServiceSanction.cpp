#include "pch.h"

#include "Core/Testing/TestFramework.h"

#include "GameFramework/Base/Online/Sanction/ServiceSanction.h"
#include "GameFramework/Base/Online/Store/MemoryServiceStore.h"

// ServiceSanctionTest — 제재 레코드의 왕복과 로그인 차단(정지 · 영구 정지는 막고 채팅 금지는 막지 않는다)(nogpu).

namespace
{
    constexpr uint64 kAccount = 22;
    constexpr uint64 kOther   = 11;
    constexpr int64  kNowMs   = 1700000000000ll;
} // namespace

/**
 * @brief [ServiceSanctionTest] 제재 레코드는 왕복하고, 기간 정지는 그때까지 · 영구 정지는 영원히 로그인을 막으며, 채팅 금지는 로그인을 막지 않는다
 */
SW_TEST_CASE( ServiceSanctionTest, SanctionsBlockLogin )
{
    sw::MemoryServiceDatabase database;
    sw::ServiceSanctionState  state;
    state._reasonCode                                                          = "sanction.spam";
    state._arrUntilMs[static_cast<int32>( sw::ServiceSanctionKind::ChatMute )] = kNowMs + 1000;
    sw::ServiceTransaction transaction;
    sw::ServiceSanction::stageWrite( transaction, kAccount, state );
    SW_REQUIRE_TRUE( database.commit( transaction ) == sw::ServiceStoreResult::Ok );

    sw::ServiceSanctionState read;
    SW_REQUIRE_TRUE( sw::ServiceSanction::readState( database, kAccount, read ) == sw::ServiceStoreResult::Ok );
    SW_EXPECT_EQUAL( state._reasonCode, read._reasonCode );
    SW_EXPECT_TRUE( sw::ServiceSanction::isActive( read, sw::ServiceSanctionKind::ChatMute, kNowMs ) );
    SW_EXPECT_FALSE( sw::ServiceSanction::isActive( read, sw::ServiceSanctionKind::ChatMute, kNowMs + 1000 ) );
    SW_EXPECT_EQUAL( int64( 0 ), sw::ServiceSanction::getLoginBlockedUntilMs( read, kNowMs ) );

    read._arrUntilMs[static_cast<int32>( sw::ServiceSanctionKind::Suspend )] = kNowMs + 5000;
    SW_EXPECT_EQUAL( kNowMs + 5000, sw::ServiceSanction::getLoginBlockedUntilMs( read, kNowMs ) );
    read._arrUntilMs[static_cast<int32>( sw::ServiceSanctionKind::Ban )] = sw::ServiceSanctionState::kPermanentMs;
    SW_EXPECT_EQUAL( sw::ServiceSanctionState::kPermanentMs, sw::ServiceSanction::getLoginBlockedUntilMs( read, kNowMs ) );

    sw::ServiceSanctionState none;
    SW_REQUIRE_TRUE( sw::ServiceSanction::readState( database, kOther, none ) == sw::ServiceStoreResult::Ok );
    SW_EXPECT_EQUAL( uint64( 0 ), none._version );
}
