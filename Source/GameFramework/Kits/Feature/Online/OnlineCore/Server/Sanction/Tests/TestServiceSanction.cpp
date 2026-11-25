#include "pch.h"

#include "Core/Serialization/BitStream.h"
#include "Core/Testing/TestFramework.h"

#include "Engine/Serialization/Format/BinarySerializer.h"

#include "GameFramework/Runtime/Online/Sanction/Server/ServiceSanction.h"
#include "GameFramework/Runtime/Online/Store/Server/MemoryServiceStore.h"
#include "GameFramework/Runtime/Online/Store/Server/ServiceRecordTable.h"
#include "GameFramework/Runtime/Online/Store/Server/Tests/ServiceStoreEntryTestUtil.h"
#include "GameFramework/Runtime/Online/Store/ServiceKeyUtil.h"

// ServiceSanctionTest — 제재 레코드의 왕복과 로그인 차단(정지 · 영구 정지는 막고 채팅 금지는 막지 않는다), 상한 그대로인 사유,
// 깨진 레코드 거절, 모두 풀면 지우기(nogpu).

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
    state._reasonCode                                    = "sanction.spam";
    state._arrUntilMs[sw::ServiceSanctionKind::ChatMute] = kNowMs + 1000;
    sw::ServiceTransaction transaction;
    sw::ServiceSanction::stageWrite( transaction, kAccount, state );
    SW_REQUIRE_TRUE( database.commit( transaction ) == sw::ServiceStoreResult::Ok );

    sw::ServiceSanctionState read;
    SW_REQUIRE_TRUE( sw::ServiceSanction::readState( database, kAccount, read ) == sw::ServiceStoreResult::Ok );
    SW_EXPECT_EQUAL( state._reasonCode, read._reasonCode );
    SW_EXPECT_TRUE( sw::ServiceSanction::isActive( read, sw::ServiceSanctionKind::ChatMute, kNowMs ) );
    SW_EXPECT_FALSE( sw::ServiceSanction::isActive( read, sw::ServiceSanctionKind::ChatMute, kNowMs + 1000 ) );
    SW_EXPECT_EQUAL( int64( 0 ), sw::ServiceSanction::getLoginBlockedUntilMs( read, kNowMs ) );

    read._arrUntilMs[sw::ServiceSanctionKind::Suspend] = kNowMs + 5000;
    SW_EXPECT_EQUAL( kNowMs + 5000, sw::ServiceSanction::getLoginBlockedUntilMs( read, kNowMs ) );
    read._arrUntilMs[sw::ServiceSanctionKind::Ban] = sw::ServiceSanctionState::kPermanentMs;
    SW_EXPECT_EQUAL( sw::ServiceSanctionState::kPermanentMs, sw::ServiceSanction::getLoginBlockedUntilMs( read, kNowMs ) );

    sw::ServiceSanctionState none;
    SW_REQUIRE_TRUE( sw::ServiceSanction::readState( database, kOther, none ) == sw::ServiceStoreResult::Ok );
    SW_EXPECT_EQUAL( uint64( 0 ), none._version );
}

/**
 * @brief [ServiceSanctionTest] 상태 타입의 상한(관리자 프로토콜이 받는 길이) 그대로인 사유도 쓴 뒤 다시 읽힌다
 * @details 결함: 읽기가 따로 둔 상한(64)이 쓰기 · 프로토콜 상한(256)보다 작아, GM 이 65 바이트 이상 사유로 건 제재 레코드는 다시는 읽히지 않았다(Unavailable).
 */
SW_TEST_CASE( ServiceSanctionTest, ReasonAtTheStateLimitRoundTrips )
{
    // 틀리면: 읽기 상한이 쓰기 상한보다 작으면 readState 가 Unavailable 이라 두 번째 REQUIRE 가 진다.
    sw::MemoryServiceDatabase database;
    sw::ServiceSanctionState  state;
    state._reasonCode.assign( static_cast<size_t>( sw::ServiceSanctionState::kMaxReasonCodeSize ), 'r' );
    state._arrUntilMs[sw::ServiceSanctionKind::Suspend] = kNowMs + 1000;
    sw::ServiceTransaction transaction;
    sw::ServiceSanction::stageWrite( transaction, kAccount, state );
    SW_REQUIRE_TRUE( database.commit( transaction ) == sw::ServiceStoreResult::Ok );

    sw::ServiceSanctionState read;
    SW_REQUIRE_TRUE( sw::ServiceSanction::readState( database, kAccount, read ) == sw::ServiceStoreResult::Ok );
    SW_EXPECT_EQUAL( state._reasonCode, read._reasonCode );
    SW_EXPECT_EQUAL( kNowMs + 1000, sw::ServiceSanction::getLoginBlockedUntilMs( read, kNowMs ) );
}

/** @brief [ServiceSanctionTest] 저장소 바이트는 믿지 않는다: 모르는 형식 · 잘린 몸 · 상한을 넘는 사유는 빈 제재(Ok)가 아니라 Unavailable 이고, 계정 0 은 Invalid 다 */
SW_TEST_CASE( ServiceSanctionTest, CorruptRecordIsNotReadAsNoSanction )
{
    // 틀리면: 깨진 레코드를 "제재 없음" 으로 읽으면 정지된 계정이 들어온다 — 해당 Unavailable 단언이, 계정 0 검사를 빼면 Invalid 단언이 진다.
    SW_EXPECT_LOG( "ServiceRecordUtil.DurableRecordVersionBytes" );
    sw::MemoryServiceDatabase database;
    const sw::string          key      = sw::ServiceKeyUtil::makeHex64( kAccount );
    auto                      readWith = [&]( sw::vector<uint8> bytes )
    {
        sw::ServiceTransaction transaction;
        sw::ServiceStoreTestAccess::putBytes( transaction, sw::ServiceSanction::getTable(), key, std::move( bytes ) );
        SW_EXPECT_TRUE( database.commit( transaction ) == sw::ServiceStoreResult::Ok );
        sw::ServiceSanctionState read;
        return sw::ServiceSanction::readState( database, kAccount, read );
    };
    SW_EXPECT_TRUE( readWith( sw::vector<uint8>{ 9 } ) == sw::ServiceStoreResult::Unavailable ); // 이 빌드가 모르는 형식
    SW_EXPECT_TRUE( readWith( sw::vector<uint8>{ 0 } ) == sw::ServiceStoreResult::Unavailable ); // 형식 머리만 있고 몸이 잘렸다

    sw::ServiceSanctionState overlong; // 형식은 맞고 사유 길이만 상한 + 1 — 선언 범위(MaxCount)로 거절한다
    overlong._reasonCode = sw::string( static_cast<size_t>( sw::ServiceSanctionState::kMaxReasonCodeSize ) + 1, 'r' );
    sw::vector<uint8> overlongBytes;
    SW_REQUIRE_TRUE( sw::BinarySerializer::serializeVersionedKeepingUnknown( 0, &overlong, *sw::ServiceSanctionState::StaticType(), {}, overlongBytes ) );
    SW_EXPECT_TRUE( readWith( overlongBytes ) == sw::ServiceStoreResult::Unavailable );

    sw::ServiceSanctionState none;
    SW_EXPECT_TRUE( sw::ServiceSanction::readState( database, 0, none ) == sw::ServiceStoreResult::Invalid );
}

/** @brief [ServiceSanctionTest] 모든 종류를 풀면 레코드를 지우고, 레코드가 없던 계정(버전 0)에는 아무것도 쓰지 않는다. 범위 밖 종류는 걸려 있지 않다 */
SW_TEST_CASE( ServiceSanctionTest, LiftingEverythingErasesTheRecord )
{
    // 틀리면: 빈 상태를 지우지 않고 쓰면 countRecords 단언이, 버전 0 에 지우기를 넣으면 isEmpty 단언이, 종류 범위 검사를 빼면 Count 단언이 진다.
    sw::MemoryServiceDatabase database;
    sw::ServiceSanctionState  state;
    state._arrUntilMs[sw::ServiceSanctionKind::Ban] = sw::ServiceSanctionState::kPermanentMs;
    sw::ServiceTransaction write;
    sw::ServiceSanction::stageWrite( write, kAccount, state );
    SW_REQUIRE_TRUE( database.commit( write ) == sw::ServiceStoreResult::Ok );

    sw::ServiceSanctionState read;
    SW_REQUIRE_TRUE( sw::ServiceSanction::readState( database, kAccount, read ) == sw::ServiceStoreResult::Ok );
    SW_REQUIRE_TRUE( read._version != 0 );
    read._arrUntilMs[sw::ServiceSanctionKind::Ban] = 0;
    sw::ServiceTransaction lift;
    sw::ServiceSanction::stageWrite( lift, kAccount, read );
    SW_REQUIRE_TRUE( database.commit( lift ) == sw::ServiceStoreResult::Ok );
    SW_EXPECT_EQUAL( 0, database.countRecords( sw::ServiceSanction::getTable() ) );

    sw::ServiceTransaction nothing;
    sw::ServiceSanction::stageWrite( nothing, kOther, sw::ServiceSanctionState{} );
    SW_EXPECT_TRUE( nothing.isEmpty() );

    SW_EXPECT_FALSE( sw::ServiceSanction::isActive( state, sw::ServiceSanctionKind::Count, kNowMs ) );
}
