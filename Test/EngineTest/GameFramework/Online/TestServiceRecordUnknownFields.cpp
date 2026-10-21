#include "pch.h"

#include "EngineTest/GameFramework/Online/ServerBusTestTypes.h"

#include "GameFramework/Base/Online/Store/ServiceRecordUtil.h"

#include "TestFramework/TestFramework.h"

// 영속 레코드의 모르는 칸 보관(계획 PersistentRecordUnknownFields). 옛 빌드와 새 빌드는 필드 이름이 겹치는 두 타입으로 흉내 낸다(`_extra` 는 새 빌드만 안다).

using namespace sw;

/**
 * @brief [ServiceRecordUnknownFieldsTest] 옛 빌드가 새 빌드의 레코드를 읽어 고쳐 다시 써도 새 빌드가 더한 칸의 값이 남는다
 * @details 모르는 칸을 버리고 다시 쓰면(`encode`) 새 빌드가 다시 읽을 때 그 칸이 기본값이 된다 — 보관 경로(`encodeKeepingUnknown`)는 받은 바이트를 그대로 싣는다.
 */
SW_TEST_CASE( ServiceRecordUnknownFieldsTest, AnOldBuildRewriteKeepsTheNewBuildField )
{
    BusTestNoticeNew written;
    written._label     = "kick";
    written._accountID = 77;
    written._extra     = 5;
    vector<uint8> bytes;
    SW_ASSERT_TRUE( ServiceRecordUtil::encode( BusTestNoticeNew::kBusVersion, written, bytes ) );

    // 지금의 영속 읽기는 모르는 칸을 거절한다(담아 다시 쓰지 않는 호출부를 지킨다).
    BusTestNoticeOld rejected;
    {
        test::ScopedLogSuppressor suppressor; // 손상 오류
        SW_EXPECT_FALSE( ServiceRecordUtil::readDurable( "test/record", bytes, BusTestNoticeOld::kBusVersion, rejected ) );
    }

    BusTestNoticeOld           oldRecord;
    ServiceRecordUnknownFields unknown;
    SW_ASSERT_TRUE( ServiceRecordUtil::readDurableKeepingUnknown( "test/record", bytes, BusTestNoticeOld::kBusVersion, oldRecord, unknown ) );
    SW_EXPECT_EQUAL( uint64( 77 ), oldRecord._accountID );
    SW_EXPECT_EQUAL( size_t( 1 ), unknown._listField.size() );

    oldRecord._accountID = 88; // 옛 빌드가 고친다
    vector<uint8> rewritten;
    SW_ASSERT_TRUE( ServiceRecordUtil::encodeKeepingUnknown( BusTestNoticeOld::kBusVersion, oldRecord, unknown, rewritten ) );

    BusTestNoticeNew           reread;
    ServiceRecordUnknownFields none;
    SW_ASSERT_TRUE( ServiceRecordUtil::readDurableKeepingUnknown( "test/record", rewritten, BusTestNoticeNew::kBusVersion, reread, none ) );
    SW_EXPECT_TRUE( none.isEmpty() );
    SW_EXPECT_EQUAL( uint64( 88 ), reread._accountID );
    SW_EXPECT_EQUAL( 5, reread._extra );
    SW_EXPECT_TRUE( reread._label == "kick" );
}

/**
 * @brief [ServiceRecordUnknownFieldsTest] 아는 칸의 이름을 모르는 칸으로 다시 실을 수 없다 — 같은 이름이 두 번 적히는 레코드는 쓰지 않는다
 */
SW_TEST_CASE( ServiceRecordUnknownFieldsTest, AKnownFieldIsNotKeptAsUnknown )
{
    BusTestNoticeNew written;
    written._extra = 5;
    vector<uint8> bytes;
    SW_ASSERT_TRUE( ServiceRecordUtil::encode( BusTestNoticeNew::kBusVersion, written, bytes ) );
    BusTestNoticeOld           oldRecord;
    ServiceRecordUnknownFields unknown;
    SW_ASSERT_TRUE( ServiceRecordUtil::readDurableKeepingUnknown( "test/record", bytes, BusTestNoticeOld::kBusVersion, oldRecord, unknown ) );

    BusTestNoticeNew newRecord; // 새 빌드는 `_extra` 를 안다 — 옛 빌드가 담은 칸을 넘기는 것은 호출부 결함이다
    vector<uint8>    rewritten;
    {
        test::ScopedLogSuppressor suppressor; // 쓰기 거절 오류
        SW_EXPECT_FALSE( ServiceRecordUtil::encodeKeepingUnknown( BusTestNoticeNew::kBusVersion, newRecord, unknown, rewritten ) );
    }
    SW_EXPECT_TRUE( rewritten.empty() );
}
