#include "pch.h"

#include "EngineTest/GameFramework/Online/ServerBusTestTypes.h"

#include "GameFramework/Base/Online/Store/ServiceRecordUtil.h"

#include "TestFramework/TestFramework.h"

// 영속 레코드의 모르는 칸 보관(docs/09_Decisions.md 5-9 영속 레코드의 모르는 필드). 옛 빌드와 새 빌드는 필드 이름이 겹치는 두 타입으로 흉내 낸다(`_extra` 는 새 빌드만 안다).
// 레코드 타입이 `_unknownFields` 를 들고, `readDurable` 이 담고 `encode` 가 다시 싣는다 — 서비스 코드는 레코드를 복사해 고쳐 쓰기만 한다.

using namespace sw;

/**
 * @brief [ServiceRecordUnknownFieldsTest] 옛 빌드가 새 빌드의 레코드를 읽어 고쳐 다시 써도 새 빌드가 더한 칸의 값이 남는다
 * @details 예전 읽기는 모르는 칸을 거절했고(요청 실패), 버리고 읽게 하면 다시 쓸 때 그 칸이 지워졌다.
 */
SW_TEST_CASE( ServiceRecordUnknownFieldsTest, AnOldBuildRewriteKeepsTheNewBuildField )
{
    BusTestNoticeNew written;
    written._label     = "kick";
    written._accountID = 77;
    written._extra     = 5;
    vector<uint8> bytes;
    SW_ASSERT_TRUE( ServiceRecordUtil::encode( BusTestNoticeNew::kBusVersion, written, bytes ) );

    BusTestNoticeOld oldRecord;
    SW_ASSERT_TRUE( ServiceRecordUtil::readDurable( "test/record", bytes, BusTestNoticeOld::kBusVersion, oldRecord ) );
    SW_EXPECT_EQUAL( uint64( 77 ), oldRecord._accountID );
    SW_EXPECT_EQUAL( size_t( 1 ), oldRecord._unknownFields._listField.size() );

    BusTestNoticeOld edited = oldRecord; // 서비스는 레코드를 복사해 고친다 — 모르는 칸이 따라간다
    edited._accountID       = 88;
    vector<uint8> rewritten;
    SW_ASSERT_TRUE( ServiceRecordUtil::encode( BusTestNoticeOld::kBusVersion, edited, rewritten ) );

    BusTestNoticeNew reread;
    SW_ASSERT_TRUE( ServiceRecordUtil::readDurable( "test/record", rewritten, BusTestNoticeNew::kBusVersion, reread ) );
    SW_EXPECT_TRUE( reread._unknownFields.isEmpty() );
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
    BusTestNoticeOld oldRecord;
    SW_ASSERT_TRUE( ServiceRecordUtil::readDurable( "test/record", bytes, BusTestNoticeOld::kBusVersion, oldRecord ) );

    BusTestNoticeNew newRecord; // 새 빌드는 `_extra` 를 안다 — 옛 빌드가 담은 칸을 옮겨 싣는 것은 호출부 결함이다
    newRecord._unknownFields = oldRecord._unknownFields;
    vector<uint8> rewritten;
    {
        test::ScopedLogSuppressor suppressor; // 쓰기 거절 오류
        SW_EXPECT_FALSE( ServiceRecordUtil::encode( BusTestNoticeNew::kBusVersion, newRecord, rewritten ) );
    }
    SW_EXPECT_TRUE( rewritten.empty() );
}
