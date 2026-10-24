#include "pch.h"

#include "Core/Diagnostics/CacheStats.h"

#include "Engine/Text/TextLayout.h"
#include "Engine/Text/TextMeasureCache.h"

#include "EngineTest/Text/FakeFontRasterizer.h"

#include "TestFramework/TestFramework.h"

#include <string>

// TextMeasureCache — 두 세대 교대: 상한 근처에서 전체를 비우지 않고, 자주 쓰는 항목이 남고, 항목 수가 세대 용량의 두 배를 넘지 않는다. 계측 칸에 센 값이 맞는다.
// `TextLayoutEngine::measure` 가 그 캐시를 쓰는지(상한을 넘는 순간 모든 글을 다시 재지 않는지)도 가짜 글꼴 시스템으로 확인한다(nogpu).

namespace
{
    struct TextMeasureCacheTestInternal
    {
        /** @brief 키 @p key 에 어울리는 크기입니다(값으로 승격 · 보존을 확인한다). */
        static sw::float2 makeSize( uint64 key ) { return sw::float2{ static_cast<float32>( key ), static_cast<float32>( key ) + 0.5f }; }

        /** @brief 칸 @p name 의 스냅샷입니다. 없으면 빈 값입니다. */
        static sw::CacheStatsSnapshot readSnapshot( sw::string_view name )
        {
            sw::vector<sw::CacheStatsSnapshot> listSnapshot;
            sw::CacheStats::collect( listSnapshot );
            for ( const sw::CacheStatsSnapshot& snapshot : listSnapshot )
            {
                if ( sw::string_view{ snapshot._name } == name )
                    return snapshot;
            }
            return sw::CacheStatsSnapshot{};
        }
    };
} // namespace

/**
 * @brief [TextMeasureCacheTest] 한 세대가 차도 전체를 비우지 않는다 — 넘친 순간 앞 세대의 항목은 모두 남고 항목 수는 두 세대의 합이다
 * @details 옛 구현은 상한에서 전부 비워, 그 순간 쓰이던 모든 글이 같은 프레임에 다시 측정됐다(thrash).
 */
SW_TEST_CASE( TextMeasureCacheTest, FullGenerationRotatesInsteadOfClearingEverything )
{
    sw::TextMeasureCache cache( 8, "TestTextMeasureCache.Rotate" );
    for ( uint64 key = 0; key < 8; ++key )
    {
        cache.insert( key, TextMeasureCacheTestInternal::makeSize( key ) );
    }
    SW_EXPECT_EQUAL( static_cast<size_t>( 8 ), cache.getCount() );

    cache.insert( 8, TextMeasureCacheTestInternal::makeSize( 8 ) ); // 현재가 가득 — 세대를 교대한다
    SW_EXPECT_EQUAL( static_cast<size_t>( 9 ), cache.getCount() );  // 비우지 않았다: 앞 여덟 + 새 하나
    for ( uint64 key = 0; key <= 8; ++key )
    {
        const sw::float2* pSize = cache.find( key );
        SW_ASSERT_NOT_NULL( pSize );
        SW_EXPECT_EQUAL( TextMeasureCacheTestInternal::makeSize( key )._x, pSize->_x );
        SW_EXPECT_EQUAL( TextMeasureCacheTestInternal::makeSize( key )._y, pSize->_y );
    }
}

/**
 * @brief [TextMeasureCacheTest] 이전 세대에서 찾은 항목은 현재로 승격하고(항목 수는 그대로), 쓰지 않은 항목은 두 번 교대하면 사라진다
 */
SW_TEST_CASE( TextMeasureCacheTest, PromotionKeepsCountAndUnusedEntriesAgeOut )
{
    sw::TextMeasureCache cache( 4, "TestTextMeasureCache.Promote" );
    for ( uint64 key = 0; key < 5; ++key )
    {
        cache.insert( key, TextMeasureCacheTestInternal::makeSize( key ) ); // 0..3 은 이전으로 넘어가고 4 가 현재에 선다
    }
    SW_EXPECT_EQUAL( static_cast<size_t>( 5 ), cache.getCount() );

    SW_EXPECT_NOT_NULL( cache.find( 2 ) ); // 이전 → 현재로 승격한다
    SW_EXPECT_EQUAL( static_cast<size_t>( 5 ), cache.getCount() );

    cache.insert( 5, TextMeasureCacheTestInternal::makeSize( 5 ) ); // 현재 {4, 2, 5}
    cache.insert( 6, TextMeasureCacheTestInternal::makeSize( 6 ) ); // 현재 {4, 2, 5, 6} 가득
    cache.insert( 7, TextMeasureCacheTestInternal::makeSize( 7 ) ); // 교대: 이전 {4, 2, 5, 6} · 옛 이전 {0, 1, 3} 은 버림
    SW_EXPECT_TRUE( cache.getCount() <= 2 * cache.getGenerationCapacity() );
    SW_EXPECT_NULL( cache.find( 0 ) );
    SW_EXPECT_NULL( cache.find( 1 ) );
    SW_EXPECT_NULL( cache.find( 3 ) );
    SW_EXPECT_NOT_NULL( cache.find( 2 ) ); // 승격했던 항목은 한 번 더 살아남았다
}

/**
 * @brief [TextMeasureCacheTest] 세대 용량 근처에서 자주 쓰는 항목은 차가운 항목이 계속 쏟아져도 남고, 항목 수는 언제나 세대 용량의 두 배 이내다
 */
SW_TEST_CASE( TextMeasureCacheTest, HotEntrySurvivesAColdStreamAndCountStaysBounded )
{
    constexpr size_t kCapacity  = 16;
    constexpr uint64 kHotKey    = 1000000;
    constexpr uint64 kColdCount = 2000;

    sw::TextMeasureCache cache( kCapacity, "TestTextMeasureCache.Hot" );
    cache.insert( kHotKey, TextMeasureCacheTestInternal::makeSize( kHotKey ) );
    for ( uint64 cold = 0; cold < kColdCount; ++cold )
    {
        cache.insert( cold, TextMeasureCacheTestInternal::makeSize( cold ) );
        SW_ASSERT_TRUE( cache.getCount() <= 2 * kCapacity );
        if ( cold % 4 == 0 )
        {
            const sw::float2* pHot = cache.find( kHotKey );
            SW_ASSERT_NOT_NULL( pHot );
            SW_ASSERT_EQUAL( TextMeasureCacheTestInternal::makeSize( kHotKey )._x, pHot->_x );
        }
    }
    SW_EXPECT_NULL( cache.find( 0 ) ); // 한 번 쓰고 안 쓴 항목은 사라졌다
}

/**
 * @brief [TextMeasureCacheTest] 적중 · 실패 · 비우기와 항목 수가 계측 칸에 맞게 센다(승격 · 교대에서도 항목 수가 어긋나지 않고, 캐시가 사라지면 몫을 뺀다)
 */
SW_TEST_CASE( TextMeasureCacheTest, StatsCountLookupsClearsAndItems )
{
    constexpr sw::string_view    kName  = "TestTextMeasureCache.Stats";
    const sw::CacheStatsSnapshot before = TextMeasureCacheTestInternal::readSnapshot( kName );
    {
        sw::TextMeasureCache cache( 4, kName );
        SW_EXPECT_NULL( cache.find( 1 ) ); // 실패 1
        for ( uint64 key = 0; key < 6; ++key )
        {
            cache.insert( key, TextMeasureCacheTestInternal::makeSize( key ) ); // 교대 한 번(4 → 이전)
        }
        SW_EXPECT_NOT_NULL( cache.find( 5 ) ); // 현재에서 적중 1
        SW_EXPECT_NOT_NULL( cache.find( 1 ) ); // 이전에서 승격, 적중 2
        SW_EXPECT_NULL( cache.find( 99 ) );    // 실패 2

        const sw::CacheStatsSnapshot during = TextMeasureCacheTestInternal::readSnapshot( kName );
        SW_EXPECT_EQUAL( before._hitCount + 2, during._hitCount );
        SW_EXPECT_EQUAL( before._missCount + 2, during._missCount );
        SW_EXPECT_EQUAL( before._clearCount, during._clearCount );
        SW_EXPECT_EQUAL( before._itemCount + static_cast<int64>( cache.getCount() ), during._itemCount ); // 칸의 항목 수가 캐시의 실제 수와 같다

        cache.clear();
        const sw::CacheStatsSnapshot cleared = TextMeasureCacheTestInternal::readSnapshot( kName );
        SW_EXPECT_EQUAL( before._clearCount + 1, cleared._clearCount );
        SW_EXPECT_EQUAL( before._itemCount, cleared._itemCount );
        SW_EXPECT_EQUAL( static_cast<size_t>( 0 ), cache.getCount() );

        cache.insert( 7, TextMeasureCacheTestInternal::makeSize( 7 ) ); // 남겨 두고 소멸자가 몫을 빼는지 본다
    }
    const sw::CacheStatsSnapshot after = TextMeasureCacheTestInternal::readSnapshot( kName );
    SW_EXPECT_EQUAL( before._itemCount, after._itemCount );
}

/**
 * @brief [TextMeasureCacheTest] 측정 캐시가 상한(세대 용량)을 막 넘겨도 `TextLayoutEngine::measure` 는 앞 세대의 글을 다시 재지 않는다
 * @details 서로 다른 글 용량 + 1 개를 재면 세대가 한 번 교대한다. 옛 구현이면 그 순간 전부 비워져 첫 글을 다시 재면 실패(miss)지만, 두 세대면 이전에서 적중한다.
 */
SW_TEST_CASE( TextMeasureCacheTest, MeasureDoesNotRecomputeEarlierTextWhenTheCacheFills )
{
    constexpr uint32 kTextCount = 4097; // TextLayout.cpp 의 세대 용량 4096 + 1

    sw::test::FakeFontServiceFixture fonts;
    sw::TextLayoutEngine             layout{ *fonts._fontService };
    sw::FontCatalogDesc              catalog{};
    catalog._defaultFamily = "Latin";
    sw::test::FakeFontServiceFixture::addFamily( catalog, "Latin", "test/fonts/latin.ttf" );
    SW_ASSERT_TRUE( fonts.initialize( catalog ) );

    sw::TextLayoutStyle style{};
    style._fontSize = 10.0f;

    const sw::CacheStatsSnapshot before = TextMeasureCacheTestInternal::readSnapshot( "TextMeasure" );
    for ( uint32 index = 0; index < kTextCount; ++index )
    {
        const std::string text = "w" + std::to_string( index );
        (void)layout.measure( text.c_str(), style, 0.0f );
    }
    const sw::CacheStatsSnapshot filled = TextMeasureCacheTestInternal::readSnapshot( "TextMeasure" );
    SW_EXPECT_EQUAL( before._missCount + kTextCount, filled._missCount );
    SW_EXPECT_EQUAL( before._itemCount + static_cast<int64>( kTextCount ), filled._itemCount ); // 비우지 않았다 — 앞 세대가 그대로 있다

    (void)layout.measure( "w0", style, 0.0f ); // 앞 세대의 첫 글
    const sw::CacheStatsSnapshot again = TextMeasureCacheTestInternal::readSnapshot( "TextMeasure" );
    SW_EXPECT_EQUAL( filled._hitCount + 1, again._hitCount );
    SW_EXPECT_EQUAL( filled._missCount, again._missCount ); // 다시 재지 않았다
}
