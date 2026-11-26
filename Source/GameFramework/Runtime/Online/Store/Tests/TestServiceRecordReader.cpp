#include "pch.h"

#include "Core/TestFramework/TestFramework.h"

#include "GameFramework/Runtime/Online/Store/Server/MemoryServiceStore.h"
#include "GameFramework/Runtime/Online/Store/Server/ServiceRecordReader.h"
#include "GameFramework/Runtime/Online/Store/Server/Tests/ServiceStoreEntryTestUtil.h"

// ServiceRecordReaderTest — 목록 읽기의 유일한 입구(감사 O-18): 한 쪽 읽기는 가득 찬 쪽의 다음 커서를 싣고, 다 읽기는 쪽을 넘겨 끝까지 읽거나 상한에서 잘림을 싣는다.

using namespace sw;

namespace
{
    const hashed_string kTable{ "reader_test" };

    void seedRows( MemoryServiceDatabase& database, int32 count )
    {
        ServiceTransaction transaction;
        for ( int32 index = 0; index < count; ++index )
        {
            StringBuilder<constant::buffer::kMax32> key;
            key.appendFormat( "row/%#", 1000 + index );
            ServiceStoreTestAccess::putBytes( transaction, kTable, string( key.view() ), vector<uint8>{ 1 } );
        }
        SW_REQUIRE_TRUE( database.commit( transaction ) == ServiceStoreResult::Ok );
    }
} // namespace

/** @brief [ServiceRecordReaderTest] 한 쪽 읽기: 가득 찬 쪽은 다음 커서를 싣고, 그 커서로 이어 읽으면 겹치지 않으며, 마지막 쪽은 커서가 비었다 */
SW_TEST_CASE( ServiceRecordReaderTest, PageCarriesTheNextCursor )
{
    MemoryServiceDatabase database;
    seedRows( database, 5 );
    vector<ServiceRecord>   listRecord;
    const ServiceRecordPage first = ServiceRecordReader::readPage( database, kTable, "row/", "", 3, false, listRecord );
    SW_REQUIRE_TRUE( first.isOk() );
    SW_EXPECT_TRUE( first.hasMore() );
    SW_EXPECT_EQUAL( string( "row/1002" ), first._nextCursor );
    const ServiceRecordPage second = ServiceRecordReader::readPage( database, kTable, "row/", first._nextCursor, 3, false, listRecord );
    SW_REQUIRE_TRUE( second.isOk() );
    SW_EXPECT_FALSE( second.hasMore() );
    SW_EXPECT_EQUAL( size_t( 5 ), listRecord.size() );
}

/** @brief [ServiceRecordReaderTest] 다 읽기: 쪽 크기보다 많아도 끝까지 읽고, 상한에 닿으면 상한까지만 붙이고 잘림을 싣는다 — 정확히 상한이면 잘림이 아니다. 앞에 든 것은 남는다 */
SW_TEST_CASE( ServiceRecordReaderTest, ReadAllPagesThroughOrReportsTruncation )
{
    MemoryServiceDatabase database;
    seedRows( database, 7 );
    {
        vector<ServiceRecord>   listRecord;
        const ServiceRecordRead read = ServiceRecordReader::readAll( database, kTable, "row/", 2, 100, false, listRecord );
        SW_REQUIRE_TRUE( read.isOk() );
        SW_EXPECT_FALSE( read._bTruncated );
        SW_EXPECT_EQUAL( size_t( 7 ), listRecord.size() );
    }
    {
        vector<ServiceRecord>   listRecord;
        const ServiceRecordRead read = ServiceRecordReader::readAll( database, kTable, "row/", 2, 5, false, listRecord );
        SW_REQUIRE_TRUE( read.isOk() );
        SW_EXPECT_TRUE( read._bTruncated );
        SW_REQUIRE_EQUAL( size_t( 5 ), listRecord.size() );
        SW_EXPECT_EQUAL( string( "row/1004" ), listRecord.back()._key );
    }
    {
        vector<ServiceRecord>   listRecord;
        const ServiceRecordRead read = ServiceRecordReader::readAll( database, kTable, "row/", 3, 7, false, listRecord );
        SW_REQUIRE_TRUE( read.isOk() );
        SW_EXPECT_FALSE( read._bTruncated );
        SW_EXPECT_EQUAL( size_t( 7 ), listRecord.size() );
    }
    {
        vector<ServiceRecord>   listRecord( 2 ); // 부른 쪽이 이미 든 것
        const ServiceRecordRead read = ServiceRecordReader::readAll( database, kTable, "row/", 4, 3, true, listRecord );
        SW_REQUIRE_TRUE( read.isOk() );
        SW_EXPECT_TRUE( read._bTruncated );
        SW_REQUIRE_EQUAL( size_t( 5 ), listRecord.size() );
        SW_EXPECT_EQUAL( string( "row/1006" ), listRecord[2]._key ); // 내림차순
    }
}
