// 제재 레코드 — 쓰고 읽기 · 활성 판정, 낡은 판은 충돌, 영구 정지 · 모두 풀면 지움, 로그인을 막는 끝 시각.
#include "pch.h"

#include "GameFramework/Base/Online/Sanction/ServiceSanction.h"
#include "GameFramework/Base/Online/Store/MemoryServiceStore.h"

#include "TestFramework/TestFramework.h"

using namespace sw;

SW_TEST_CASE( ServiceSanctionTest, WrittenSanctionIsActiveUntilItsEnd )
{
    MemoryServiceDatabase database;
    ServiceSanctionState  state;
    SW_ASSERT_TRUE( ServiceSanction::readState( database, 7, state ) == ServiceStoreResult::Ok );
    SW_EXPECT_FALSE( ServiceSanction::isActive( state, ServiceSanctionKind::ChatMute, 0 ) );
    state._arrUntilMs[static_cast<int32>( ServiceSanctionKind::ChatMute )] = 5000;
    state._reasonCode                                                      = "sanction.spam";
    ServiceTransaction transaction;
    ServiceSanction::stageWrite( transaction, 7, state );
    SW_ASSERT_TRUE( database.commit( transaction ) == ServiceStoreResult::Ok );
    ServiceSanctionState read;
    SW_ASSERT_TRUE( ServiceSanction::readState( database, 7, read ) == ServiceStoreResult::Ok );
    SW_EXPECT_TRUE( ServiceSanction::isActive( read, ServiceSanctionKind::ChatMute, 4999 ) );
    SW_EXPECT_FALSE( ServiceSanction::isActive( read, ServiceSanctionKind::ChatMute, 5000 ) );
    SW_EXPECT_FALSE( ServiceSanction::isActive( read, ServiceSanctionKind::Suspend, 0 ) );
    SW_EXPECT_EQUAL( read._reasonCode, string( "sanction.spam" ) );
    SW_EXPECT_EQUAL( ServiceSanction::getLoginBlockedUntilMs( read, 0 ), int64( 0 ) ); // 채팅 금지는 로그인을 막지 않는다
}

SW_TEST_CASE( ServiceSanctionTest, StaleVersionConflicts )
{
    MemoryServiceDatabase database;
    ServiceSanctionState  state;
    state._arrUntilMs[static_cast<int32>( ServiceSanctionKind::Suspend )] = 100;
    ServiceTransaction first;
    ServiceSanction::stageWrite( first, 9, state ); // 판 0 = 없어야 한다
    SW_ASSERT_TRUE( database.commit( first ) == ServiceStoreResult::Ok );
    ServiceTransaction stale;
    ServiceSanction::stageWrite( stale, 9, state ); // 다른 GM 이 같은 옛 판(0)으로
    SW_EXPECT_TRUE( database.commit( stale ) == ServiceStoreResult::Conflict );
    ServiceSanctionState read;
    SW_ASSERT_TRUE( ServiceSanction::readState( database, 9, read ) == ServiceStoreResult::Ok );
    SW_EXPECT_EQUAL( ServiceSanction::getLoginBlockedUntilMs( read, 50 ), int64( 100 ) );
    SW_EXPECT_EQUAL( ServiceSanction::getLoginBlockedUntilMs( read, 100 ), int64( 0 ) );
}

SW_TEST_CASE( ServiceSanctionTest, BanIsPermanentAndClearingErases )
{
    MemoryServiceDatabase database;
    ServiceSanctionState  state;
    state._arrUntilMs[static_cast<int32>( ServiceSanctionKind::Ban )] = ServiceSanctionState::kPermanentMs;
    ServiceTransaction ban;
    ServiceSanction::stageWrite( ban, 11, state );
    SW_ASSERT_TRUE( database.commit( ban ) == ServiceStoreResult::Ok );
    ServiceSanctionState read;
    SW_ASSERT_TRUE( ServiceSanction::readState( database, 11, read ) == ServiceStoreResult::Ok );
    SW_EXPECT_TRUE( ServiceSanction::isActive( read, ServiceSanctionKind::Ban, 0x7FFFFFFFFFFFFFF0ll ) );
    SW_EXPECT_EQUAL( ServiceSanction::getLoginBlockedUntilMs( read, 0 ), ServiceSanctionState::kPermanentMs );
    read._arrUntilMs[static_cast<int32>( ServiceSanctionKind::Ban )] = 0;
    ServiceTransaction lift;
    ServiceSanction::stageWrite( lift, 11, read );
    SW_ASSERT_TRUE( database.commit( lift ) == ServiceStoreResult::Ok );
    SW_EXPECT_EQUAL( database.countRecords( ServiceSanction::getTable() ), 0 );
}
