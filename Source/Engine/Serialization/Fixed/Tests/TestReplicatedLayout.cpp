// ReplicatedLayoutTest — 복제 스키마(`Replicated` 칸만의 고정 배치형 표). 칸 고르기 · 스키마 해시가 가르는 것 · 양자화 왕복 · 바뀐 칸 비트 · 쓰기 비용.
#include "pch.h"

#include "Core/Serialization/BitStream.h"
#include "Core/Testing/TestFramework.h"
#include "Core/Time/MonotonicClock.h"

#include "Engine/Serialization/Fixed/FixedLayout.h"
#include "Engine/Serialization/Fixed/ReplicatedLayout.h"
#include "Engine/Serialization/Fixed/Tests/FixedLayoutTestTypes.h"

using namespace sw;

namespace
{
    /** @brief 정규 문자열을 `|` 로 나눈 조각입니다(끝의 빈 조각은 뺀다). */
    vector<string> splitCanonical( const string& canonical )
    {
        vector<string> listPart;
        size_t         start = 0;
        while ( start < canonical.size() )
        {
            const size_t end = canonical.find( '|', start );
            listPart.push_back( canonical.substr( start, end - start ) );
            if ( end == string::npos )
                break;
            start = end + 1;
        }
        return listPart;
    }
} // namespace

/**
 * @brief [ReplicatedLayoutTest] 복제 칸만 쓴다 — 비트 수는 복제 칸의 것이고, 읽은 쪽의 복제하지 않는 칸은 그대로다. 타입 전체가 적격이 아니어도 스키마가 있다
 */
SW_TEST_CASE( ReplicatedLayoutTest, OnlyReplicatedPropertiesAreWritten )
{
    SW_EXPECT_FALSE( FixedLayout::findCodec<ReplicatedLayoutSample>().has_value() ); // 전체 코덱은 길이 없는 vector 때문에 적격이 아니다
    const expected<const FixedCodec*> schema = ReplicatedLayout::findSchema<ReplicatedLayoutSample>();
    SW_REQUIRE_TRUE( schema.has_value() );
    SW_EXPECT_EQUAL( ( *schema )->_fieldCount, 4u );

    ReplicatedLayoutSample source;
    source._health    = 42;
    source._speed     = 12.5f;
    source._bAlive    = false;
    source._position  = float3{ 1.0f, -2.0f, 3.0f };
    source._debugName = "server";
    source._listCache = { 1, 2, 3 };
    BitWriter writer;
    SW_REQUIRE_TRUE( ReplicatedLayout::writeFull( source, writer ) );
    // 범위 정수 [0, 100] 7 비트 + 양자화 [-100, 100] / 0.01 = 20000 칸 15 비트 + bool 1 비트 + 범위 실수 셋(원본 32 비트씩) 96 비트.
    SW_EXPECT_EQUAL( writer.getBitCount(), 7 + 15 + 1 + 96 );

    const vector<uint8>    bytes = writer.releaseBytes();
    ReplicatedLayoutSample target;
    target._debugName = "client";
    BitReader     reader( bytes.data(), static_cast<int32>( bytes.size() ) );
    DynamicBitset changed;
    SW_REQUIRE_TRUE( ReplicatedLayout::readFull( reader, target, &changed ) == FixedReadResult::Ok );
    SW_EXPECT_EQUAL( target._health, 42 );
    SW_EXPECT_FALSE( target._bAlive );
    SW_EXPECT_EQUAL( target._position._y, -2.0f );
    SW_EXPECT_TRUE( target._debugName == "client" );
    SW_EXPECT_TRUE( target._listCache.empty() );
    SW_REQUIRE_EQUAL( changed.size(), 4u );
    SW_EXPECT_TRUE( changed.test( 0 ) && changed.test( 1 ) && changed.test( 2 ) && changed.test( 3 ) );

    // 같은 값을 다시 읽으면 바뀐 칸이 없다.
    BitReader again( bytes.data(), static_cast<int32>( bytes.size() ) );
    SW_REQUIRE_TRUE( ReplicatedLayout::readFull( again, target, &changed ) == FixedReadResult::Ok );
    SW_EXPECT_FALSE( changed.test( 0 ) || changed.test( 1 ) || changed.test( 2 ) || changed.test( 3 ) );
}

/**
 * @brief [ReplicatedLayoutTest] 칸 순서가 스키마 해시의 일부다 — 정규 문자열의 두 칸을 바꾼 해시는 다르다
 */
SW_TEST_CASE( ReplicatedLayoutTest, FieldOrderChangesTheSchemaHash )
{
    const expected<const FixedCodec*> schema = ReplicatedLayout::findSchema<ReplicatedLayoutSample>();
    SW_REQUIRE_TRUE( schema.has_value() );
    const string canonical = ( *schema )->_pCanonical;
    SW_REQUIRE_EQUAL( ReplicatedLayout::getSchemaHash( **schema ), FixedLayout::computeSchemaHash( FixedLayout::kVersion, canonical, {} ) );

    vector<string> listPart = splitCanonical( canonical );
    SW_REQUIRE_TRUE( listPart.size() >= 3 ); // R=… 와 칸들
    std::swap( listPart[1], listPart[2] );
    string swapped;
    for ( const string& part : listPart )
    {
        swapped += part + "|";
    }
    SW_EXPECT_TRUE( swapped != canonical );
    SW_EXPECT_TRUE( FixedLayout::computeSchemaHash( FixedLayout::kVersion, swapped, {} ) != ReplicatedLayout::getSchemaHash( **schema ) );
}

/**
 * @brief [ReplicatedLayoutTest] 복제하지 않는 칸은 스키마에 없다 — 정규 문자열에 그 이름이 없고 해시는 그 글에서만 나온다(그 칸을 더하거나 바꿔도 해시가 같다)
 */
SW_TEST_CASE( ReplicatedLayoutTest, NonReplicatedFieldKeepsTheHash )
{
    const expected<const FixedCodec*> schema = ReplicatedLayout::findSchema<ReplicatedLayoutSample>();
    SW_REQUIRE_TRUE( schema.has_value() );
    const string canonical = ( *schema )->_pCanonical;
    SW_EXPECT_TRUE( canonical.find( "_debugName" ) == string::npos );
    SW_EXPECT_TRUE( canonical.find( "_listCache" ) == string::npos );
    SW_EXPECT_TRUE( canonical.find( "_health" ) != string::npos );
    SW_EXPECT_EQUAL( ReplicatedLayout::getSchemaHash( **schema ), FixedLayout::computeSchemaHash( FixedLayout::kVersion, canonical, {} ) );
}

/**
 * @brief [ReplicatedLayoutTest] `Step` 칸은 왕복하면 간격의 절반 안이다(경계 밖은 경계로 자른다)
 */
SW_TEST_CASE( ReplicatedLayoutTest, QuantizedFloatRoundTripsWithinHalfAStep )
{
    for ( const float32 speed : { -100.0f, -37.123f, 0.0f, 0.004f, 12.3456f, 99.995f, 100.0f } )
    {
        ReplicatedLayoutSample source;
        source._speed = speed;
        BitWriter writer;
        SW_REQUIRE_TRUE( ReplicatedLayout::writeFull( source, writer ) );
        const vector<uint8>    bytes = writer.releaseBytes();
        ReplicatedLayoutSample target;
        BitReader              reader( bytes.data(), static_cast<int32>( bytes.size() ) );
        SW_REQUIRE_TRUE( ReplicatedLayout::readFull( reader, target, nullptr ) == FixedReadResult::Ok );
        SW_EXPECT_NEAR( target._speed, speed, 0.005f + 1e-4f );
    }
}

/**
 * @brief [ReplicatedLayoutTest] 엔티티 하나의 복제 상태 쓰기 비용(ns) — 전체 코덱 `FixedLayout::write` 의 1.5 배 안이 목표다(docs/DesignBacklog.md 리플렉션 남은 몫 N1)
 * @details 재는 법: Release `App --test --test-filter=ReplicatedLayoutTest.EncodeCost` 5 번, 로그 `ReplicatedLayoutTest.EncodeCost` 의 중앙값. 넘으면 해석기의 칸 건너뛰기를 본다.
 */
SW_TEST_CASE( ReplicatedLayoutTest, EncodeCost, ._tags = "bench" )
{
    static constexpr int32 kRepeatCount = 100000;

    ReplicatedLayoutSample source;
    source._health   = 77;
    source._speed    = 3.5f;
    source._position = float3{ 10.0f, 0.5f, -4.0f };
    BitWriter   writer;
    const int64 startNanoseconds = MonotonicClock::nowNanoseconds();
    for ( int32 repeat = 0; repeat < kRepeatCount; ++repeat )
    {
        writer.clear();
        SW_REQUIRE_TRUE( ReplicatedLayout::writeFull( source, writer ) );
    }
    const int64 elapsedNanoseconds = MonotonicClock::nowNanoseconds() - startNanoseconds;
    SW_LOG_INFO_ID( "ReplicatedLayoutTest.EncodeCost", "Replicated state write: %# ns per entity", elapsedNanoseconds / kRepeatCount );
}
