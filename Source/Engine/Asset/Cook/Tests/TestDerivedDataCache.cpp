// DerivedDataCacheTest — DDC 층 목록: 뒤(공유) 층 히트는 앞(로컬) 층에 채우고, 키 서술이 다르거나 본문이 깨진 항목은 미스이며, 읽기 전용 층에는 쓰지 않는다(nogpu).
#include "pch.h"

#include "Core/File/FileUtil.h"
#include "Core/Testing/TestFramework.h"

#include "Engine/Asset/Cook/CookRecord.h"

using namespace sw;

namespace
{
    struct DerivedDataCacheFixture
    {
        string _localRoot{ test::makeScratchPath( "DDC/Local" ) };
        string _sharedRoot{ test::makeScratchPath( "DDC/Shared" ) };

        /** @brief 로컬(쓰기) + 공유(@p sharedAccess) 두 층입니다. */
        DerivedDataCache makeLayered( DerivedDataAccess sharedAccess ) const
        {
            return DerivedDataCache{
                vector<DerivedDataLayer>{ DerivedDataLayer{ _localRoot, DerivedDataAccess::ReadWrite }, DerivedDataLayer{ _sharedRoot, sharedAccess } }
            };
        }
        /** @brief 공유 층 하나만 쓰는 캐시입니다(다른 머신이 공유 층을 채운 것). */
        DerivedDataCache makeSharedWriter() const { return DerivedDataCache{ vector<DerivedDataLayer>{ DerivedDataLayer{ _sharedRoot, DerivedDataAccess::ReadWrite } } }; }
    };

    const vector<uint8> kPayload{ 1, 2, 3, 4, 5 };
    constexpr uint64    kKey = 0xabcdef0123456789ull;
} // namespace

/**
 * @brief [DerivedDataCacheTest] 공유 층에만 있는 항목을 찾으면 같은 바이트를 돌려주고 로컬 층에 채운다
 */
SW_TEST_CASE( DerivedDataCacheTest, SharedHitFillsLocal )
{
    const DerivedDataCacheFixture fixture;
    SW_REQUIRE_TRUE( fixture.makeSharedWriter().store( kKey, "job-a", kPayload ) );

    const DerivedDataCache cache = fixture.makeLayered( DerivedDataAccess::ReadOnly );
    SW_EXPECT_FALSE( FileUtil::exists( cache.makePath( 0, kKey ) ) );
    vector<uint8> bytes;
    SW_REQUIRE_TRUE( cache.find( kKey, "job-a", bytes ) );
    SW_EXPECT_TRUE( bytes == kPayload );
    SW_EXPECT_TRUE( FileUtil::exists( cache.makePath( 0, kKey ) ) );

    // 채운 로컬 항목만으로도 히트다(공유 층이 사라져도).
    const DerivedDataCache localOnly{ vector<DerivedDataLayer>{ DerivedDataLayer{ fixture._localRoot, DerivedDataAccess::ReadWrite } } };
    vector<uint8>          localBytes;
    SW_REQUIRE_TRUE( localOnly.find( kKey, "job-a", localBytes ) );
    SW_EXPECT_TRUE( localBytes == kPayload );
}

/**
 * @brief [DerivedDataCacheTest] 같은 64 비트 키라도 키 서술이 다르면 미스다 — 키 충돌이 조용히 틀린 산출물이 되지 않는다
 */
SW_TEST_CASE( DerivedDataCacheTest, KeyCollisionIsAMiss )
{
    const DerivedDataCacheFixture fixture;
    const DerivedDataCache        cache = fixture.makeLayered( DerivedDataAccess::ReadOnly );
    SW_REQUIRE_TRUE( cache.store( kKey, "job-a", kPayload ) );

    vector<uint8> bytes;
    SW_EXPECT_FALSE( cache.find( kKey, "job-b", bytes ) );
    SW_EXPECT_TRUE( bytes.empty() );
    SW_EXPECT_TRUE( cache.find( kKey, "job-a", bytes ) );
}

/**
 * @brief [DerivedDataCacheTest] 본문이 머리의 해시와 다르면 미스다 — 공유 층(다른 머신이 쓴 바이트)은 신뢰 경계다
 */
SW_TEST_CASE( DerivedDataCacheTest, CorruptPayloadIsAMiss )
{
    const DerivedDataCacheFixture fixture;
    const DerivedDataCache        cache = fixture.makeLayered( DerivedDataAccess::ReadOnly );
    SW_REQUIRE_TRUE( cache.store( kKey, "job-a", kPayload ) );

    const string  path = cache.makePath( 0, kKey );
    vector<uint8> entry;
    SW_REQUIRE_TRUE( FileUtil::readFile( path, entry ) );
    entry.back() ^= 0xff; // 본문 마지막 바이트를 뒤집는다
    SW_REQUIRE_TRUE( FileUtil::writeFile( path, entry.data(), entry.size() ) );

    vector<uint8> bytes;
    SW_EXPECT_FALSE( cache.find( kKey, "job-a", bytes ) );

    // 머리가 잘린 항목도 미스다.
    SW_REQUIRE_TRUE( FileUtil::writeFile( path, entry.data(), 5 ) );
    SW_EXPECT_FALSE( cache.find( kKey, "job-a", bytes ) );
}

/**
 * @brief [DerivedDataCacheTest] 저장은 쓰기 가능 층에만 한다 — 읽기 전용 공유 층에는 파일이 생기지 않는다
 */
SW_TEST_CASE( DerivedDataCacheTest, ReadOnlyLayerIsNotWritten )
{
    const DerivedDataCacheFixture fixture;
    const DerivedDataCache        cache = fixture.makeLayered( DerivedDataAccess::ReadOnly );
    SW_REQUIRE_TRUE( cache.store( kKey, "job-a", kPayload ) );
    SW_EXPECT_TRUE( FileUtil::exists( cache.makePath( 0, kKey ) ) );
    SW_EXPECT_FALSE( FileUtil::exists( cache.makePath( 1, kKey ) ) );

    const DerivedDataCache writable = fixture.makeLayered( DerivedDataAccess::ReadWrite );
    SW_REQUIRE_TRUE( writable.store( kKey, "job-a", kPayload ) );
    SW_EXPECT_TRUE( FileUtil::exists( writable.makePath( 1, kKey ) ) );
}
