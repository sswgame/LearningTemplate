#include "pch.h"

#include "Core/Container/VarIntUtil.h"
#include "Core/Testing/TestFramework.h"

#include "Engine/Serialization/Format/Archive.h"
#include "Engine/Serialization/Serializer/StringPool.h"

using namespace sw;

// StringPoolTest — 직렬화 문자열 풀: 사전 정의 이름은 제 번호, 동적 문자열은 한 번만 등록, 비우기, Archive · 바이트 버퍼 왕복(동적 표만),
// 잘린 버퍼 · 상한을 넘는 개수 거절(nogpu).

namespace
{
    constexpr const utf8* kAlpha = "StringPoolTestAlpha";
    constexpr const utf8* kBeta  = "StringPoolTestBeta";

    void fillPool( StringPool& outPool )
    {
        SW_EXPECT_EQUAL( StringPool::kPredefinedCount, outPool.internString( kAlpha ) );
        SW_EXPECT_EQUAL( StringPool::kPredefinedCount + 1, outPool.internString( kBeta ) );
    }
} // namespace

/**
 * @brief [StringPoolTest] 사전 정의 이름은 목록의 줄 번호로 돌아오고 동적 표에 들지 않는다
 */
SW_TEST_CASE( StringPoolTest, PredefinedNamesKeepTheirIndex )
{
    StringPool pool;
    SW_EXPECT_EQUAL( size_t( StringPool::kPredefinedCount ), pool.getCount() );
    SW_EXPECT_TRUE( pool.empty() );
    SW_EXPECT_EQUAL( 4u, pool.internString( "int32" ) ); // PredefinedNameType.xxx 의 4 번 줄
    SW_EXPECT_EQUAL( string( "int32" ), string( pool.getString( 4 ) ) );
    SW_EXPECT_TRUE( pool.empty() );
}

/**
 * @brief [StringPoolTest] 동적 문자열은 사전 정의 다음 번호부터 한 번만 등록되고, 범위 밖 번호는 빈 글, 비우면 사전 정의만 남는다
 */
SW_TEST_CASE( StringPoolTest, DynamicStringsAreInternedOnce )
{
    StringPool pool;
    fillPool( pool );
    SW_EXPECT_EQUAL( StringPool::kPredefinedCount, pool.internString( kAlpha ) );
    SW_EXPECT_EQUAL( size_t( 2 ), pool.getDynamicCount() );
    SW_EXPECT_EQUAL( string( kBeta ), string( pool.getString( StringPool::kPredefinedCount + 1 ) ) );
    SW_EXPECT_TRUE( pool.getString( StringPool::kPredefinedCount + 2 ).empty() );

    pool.clear();
    SW_EXPECT_TRUE( pool.empty() );
    SW_EXPECT_EQUAL( size_t( StringPool::kPredefinedCount ), pool.getCount() );
    SW_EXPECT_EQUAL( StringPool::kPredefinedCount, pool.internString( kBeta ) ); // 비운 뒤에는 다시 첫 동적 번호
}

/**
 * @brief [StringPoolTest] Archive 로 쓰고 읽으면 동적 표가 같은 번호로 돌아오고, 읽은 풀에서 다시 등록해도 새 번호를 만들지 않는다
 */
SW_TEST_CASE( StringPoolTest, ArchiveRoundTripKeepsIndices )
{
    StringPool pool;
    fillPool( pool );
    Archive written;
    pool.saveToArchive( written );

    StringPool loaded;
    (void)loaded.internString( "StringPoolTestStale" ); // 읽기는 앞 내용을 비운다
    Archive reading( written.getData(), written.getSize() );
    SW_REQUIRE_TRUE( loaded.loadFromArchive( reading ) );
    SW_EXPECT_EQUAL( size_t( 2 ), loaded.getDynamicCount() );
    SW_EXPECT_EQUAL( string( kAlpha ), string( loaded.getString( StringPool::kPredefinedCount ) ) );
    SW_EXPECT_EQUAL( string( kBeta ), string( loaded.getString( StringPool::kPredefinedCount + 1 ) ) );
    SW_EXPECT_EQUAL( StringPool::kPredefinedCount + 1, loaded.internString( kBeta ) );
}

/**
 * @brief [StringPoolTest] 바이트 버퍼 왕복은 오프셋을 버퍼 끝까지 옮기고, 한 바이트 잘린 버퍼 · 상한을 넘는 개수는 거절한다
 */
SW_TEST_CASE( StringPoolTest, BinaryBufferRoundTripAndRejectsBadInput )
{
    StringPool pool;
    fillPool( pool );
    vector<uint8> bytes;
    pool.saveToBinaryBuffer( bytes );

    StringPool loaded;
    size_t     offset = 0;
    SW_REQUIRE_TRUE( loaded.loadFromBinaryBuffer( bytes.data(), bytes.size(), offset ) );
    SW_EXPECT_EQUAL( bytes.size(), offset );
    SW_EXPECT_EQUAL( string( kBeta ), string( loaded.getString( StringPool::kPredefinedCount + 1 ) ) );

    size_t truncatedOffset = 0;
    SW_EXPECT_FALSE( loaded.loadFromBinaryBuffer( bytes.data(), bytes.size() - 1, truncatedOffset ) );

    vector<uint8> tooMany;
    VarIntUtil::encodeVarUint64( uint64( StringPool::kMaxDynamicStrings ) + 1, tooMany );
    size_t tooManyOffset = 0;
    SW_EXPECT_FALSE( loaded.loadFromBinaryBuffer( tooMany.data(), tooMany.size(), tooManyOffset ) );
}
