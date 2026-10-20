#include "pch.h"

#include "Core/Container/DynamicBitset.h"
#include "Core/Network/BitStream.h"

#include "Engine/Reflection/ReflectionCore.h"
#include "Engine/Reflection/ReflectionMetadata.h"
#include "Engine/Serialization/Base/SerializerUtil.h"
#include "Engine/Serialization/Fixed/FixedLayout.h"
#include "Engine/Serialization/Format/BinarySerializer.h"
#include "Engine/Serialization/Format/JSONSerializer.h"
#include "Engine/Serialization/Format/XMLSerializer.h"

#include "ReflectionTest/TestFixedLayoutTypes.h"
#include "ReflectionTest/TestSampleActor.h"

#include "TestFramework/TestFramework.h"

// 고정 배치형(B) 코덱을 봅니다. 생성된 쓰기 · 읽기 · 비교가 왕복하고, 다른 스키마 · 잘린 스트림 · 범위 밖 값을 거절해야 합니다.
// 명세는 docs/plans/ReflectionFormats.md 6 절입니다. 표본 타입은 TestFixedLayoutTypes.h 에 있고 손 코덱 · 비교 함수가 없습니다.

namespace
{
    template <typename T>
    sw::vector<uint8> writeBytes( const T& object )
    {
        sw::BitWriter writer;
        const bool    bWritten = sw::FixedLayout::write( object, writer );
        SW_EXPECT_TRUE( bWritten );
        return writer.releaseBytes();
    }

    template <typename T>
    sw::FixedReadResult readBytes( const sw::vector<uint8>& bytes, T& inoutObject )
    {
        return sw::FixedLayout::read( bytes.data(), static_cast<int32>( bytes.size() ), inoutObject );
    }

    /** @brief 쓰고 읽은 값입니다. `Step` 필드는 한 번 양자화되므로 두 번째 왕복부터 비트까지 같아야 합니다. */
    template <typename T>
    T roundTrip( const T& source )
    {
        T                         loaded{};
        const sw::FixedReadResult result = readBytes( writeBytes( source ), loaded );
        SW_EXPECT_TRUE_MSG( result == sw::FixedReadResult::Ok, sw::FixedLayout::toString( result ) );
        return loaded;
    }

    /** @brief 모든 짧은 접두가 Truncated 이고 대상이 그대로인지 봅니다. */
    template <typename T>
    void expectEveryPrefixTruncated( const T& source, const T& target )
    {
        const sw::vector<uint8> bytes = writeBytes( source );
        for ( size_t length = 0; length < bytes.size(); ++length )
        {
            T                         loaded = target;
            const sw::FixedReadResult result = sw::FixedLayout::read( bytes.data(), static_cast<int32>( length ), loaded );
            SW_EXPECT_TRUE_MSG( result == sw::FixedReadResult::Truncated, sw::FixedLayout::toString( result ) );
            SW_EXPECT_TRUE( sw::FixedLayout::equals( loaded, target ) );
        }
    }

    /** @brief 스키마 해시 뒤에 @p pWriteBody 가 쓴 본문을 붙인 바이트입니다. 손으로 깨진 스트림을 만들 때 씁니다. */
    template <typename TWriteBody>
    sw::vector<uint8> makeStream( const uint64 schemaHash, TWriteBody writeBody )
    {
        sw::BitWriter writer;
        sw::FixedCodecUtil::writeRawBits( writer, schemaHash, 64 );
        writeBody( writer );
        return writer.releaseBytes();
    }

    template <typename T>
    const utf8* findCanonical()
    {
        sw::string            reason;
        const sw::FixedCodec* pCodec = sw::FixedLayout::findCodec<T>( reason );
        SW_EXPECT_TRUE_MSG( pCodec != nullptr, reason.c_str() );
        return pCodec != nullptr ? pCodec->_pCanonical : "";
    }

    template <typename T>
    sw::string findIneligibleReason()
    {
        sw::string            reason;
        const sw::FixedCodec* pCodec = sw::FixedLayout::findCodec<T>( reason );
        SW_EXPECT_NULL( pCodec );
        return reason;
    }

    sw::string replaceAll( sw::string text, const sw::string& from, const sw::string& to )
    {
        size_t position = text.find( from.c_str() );
        while ( position != sw::string::npos )
        {
            text.replace( position, from.size(), to );
            position = text.find( from.c_str(), position + to.size() );
        }
        return text;
    }

    /** @brief 한 가지만 다른 짝 타입의 정규 문자열이 이름을 맞춘 뒤에도 다르고, 해시와 교차 읽기가 거절하는지 봅니다. */
    template <typename TA, typename TB>
    void expectSchemaChangeRejected( const sw::string& nameA, const sw::string& nameB, const sw::string& extraA, const sw::string& extraB )
    {
        const sw::string canonicalA = findCanonical<TA>();
        sw::string       canonicalB = replaceAll( findCanonical<TB>(), nameB, nameA );
        if ( extraA.empty() == false )
            canonicalB = replaceAll( canonicalB, extraB, extraA );
        SW_EXPECT_TRUE_MSG( canonicalA != canonicalB, canonicalA.c_str() );
        const uint64 hashA = sw::FixedLayout::computeSchemaHash( sw::FixedLayout::kVersion, canonicalA, {} );
        const uint64 hashB = sw::FixedLayout::computeSchemaHash( sw::FixedLayout::kVersion, canonicalB, {} );
        SW_EXPECT_TRUE( hashA != hashB );
        SW_EXPECT_EQUAL( hashA, sw::FixedLayout::getSchemaHash<TA>() );

        TB                        loaded{};
        const sw::FixedReadResult result = readBytes( writeBytes( TA{} ), loaded );
        SW_EXPECT_TRUE_MSG( result == sw::FixedReadResult::SchemaMismatch, sw::FixedLayout::toString( result ) );
    }

    sw::FixedScalarSample makeScalarSample()
    {
        sw::FixedScalarSample sample;
        sample._bAlive   = true;
        sample._bFlag    = SW_TRUE;
        sample._hp       = 100;  // Max 경계
        sample._delta    = -500; // Min 경계
        sample._ammo     = 65535;
        sample._score    = std::numeric_limits<int64>::min();
        sample._seed     = 0;
        sample._speed    = std::numeric_limits<float32>::quiet_NaN(); // 범위 없는 실수는 비트 그대로 씁니다
        sample._halfSize = 1000.0f;
        sample._offset   = 12.5f; // Step 0.5 격자 위
        sample._ratio    = 1.0f;
        sample._time     = 1234.5678;
        return sample;
    }

    /** @brief '0' · '1' 리터럴을 비트셋으로 읽습니다. 읽기 실패는 테스트 실패입니다. */
    sw::DynamicBitset parseBits( const sw::string_view text )
    {
        sw::DynamicBitset value;
        SW_EXPECT_TRUE_MSG( sw::DynamicBitset::parse( text, value ), sw::string( text ) );
        return value;
    }

    sw::FixedContainerSample makeContainerSample()
    {
        sw::FixedContainerSample sample;
        sample._kind       = sw::EFixedSampleKind::Run;
        sample._listValue  = { 0, 1000, 5, 7 }; // MaxCount 개, 범위 경계
        sample._flags      = sw::EFixedSampleFlag::Hidden | sw::EFixedSampleFlag::Marked;
        sample._name       = "sixteen-chars-ok"; // MaxCount 글자
        sample._arrSlot[0] = -8;
        sample._arrSlot[1] = 8;
        sample._arrSlot[2] = 0;
        sample._arrWeight  = { 1.5f, -2.0f };
        sample._position   = sw::float3( -10.0f, 5.0f, 10.0f );
        sample._listKind   = { sw::EFixedSampleKind::Idle, sw::EFixedSampleKind::Run };
        sample._listBit    = parseBits( "101" );
        return sample;
    }

    sw::FixedNestedSample makeNestedSample()
    {
        sw::FixedNestedSample sample;
        sample._arenaHalfSize     = 8.0f;
        sample._inner._halfExtent = 2.0f;
        sample._inner._x          = 1.5f;
        sample._inner._id         = 255;
        sample._listInner.resize( 3 );
        sample._listInner[2]._id      = 7;
        sample._derived._baseValue    = -3;
        sample._derived._derivedValue = 9;
        sample._listPoint.resize( 2 );
        sample._listPoint[0]._y = -8.0f;
        sample._listPoint[1]._y = 3.25f;
        sample._transientCache  = 5;
        return sample;
    }
} // namespace

/**
 * @brief [FixedLayoutTest] 세 표본이 왕복하고 생성 비교가 같다고 답합니다. 경계값, 빈 컨테이너, MaxCount 개를 포함합니다
 */
SW_TEST_CASE( FixedLayoutTest, SamplesRoundTripAtTheBoundaries )
{
    const sw::FixedScalarSample scalar = makeScalarSample();
    SW_EXPECT_TRUE( sw::FixedLayout::equals( roundTrip( scalar ), scalar ) );

    const sw::FixedContainerSample container = makeContainerSample();
    SW_EXPECT_TRUE( sw::FixedLayout::equals( roundTrip( container ), container ) );
    // 빈 컨테이너입니다. `_position` 의 0 은 -10 에서 세는 Step 0.01 격자 위에 정확히 있지 않으므로 한 번 양자화한 값을 비교합니다.
    const sw::FixedContainerSample emptyContainer = roundTrip( sw::FixedContainerSample{} );
    SW_EXPECT_TRUE( emptyContainer._listValue.empty() && emptyContainer._name.empty() );
    SW_EXPECT_TRUE( sw::FixedLayout::equals( roundTrip( emptyContainer ), emptyContainer ) );

    // Step 필드는 한 번 양자화됩니다. 첫 왕복 값은 간격 안에 있고, 그 값은 다시 왕복해도 비트까지 같습니다.
    const sw::FixedNestedSample nested = makeNestedSample();
    const sw::FixedNestedSample once   = roundTrip( nested );
    SW_EXPECT_NEAR_EQUAL( once._inner._x, nested._inner._x, 0.05f );
    SW_EXPECT_EQUAL( once._derived._baseValue, -3 );
    SW_EXPECT_EQUAL( once._derived._derivedValue, 9 );
    SW_EXPECT_EQUAL( once._listInner.size(), static_cast<size_t>( 3 ) );
    SW_EXPECT_TRUE( sw::FixedLayout::equals( roundTrip( once ), once ) );
}

/**
 * @brief [FixedLayoutTest] `Count` 가 없는 열거형도 기반 타입 전체 폭이 아니라 [최소 열거자, 최대 열거자] 범위 비트로 씁니다
 * @details 예전에는 `Count` 를 적었는지에 따라 같은 세 열거자가 2 비트 또는 8 비트였습니다. 열거자 밖의 값은 쓰기가 실패합니다.
 */
SW_TEST_CASE( FixedLayoutTest, EnumsWithoutCountUseEnumeratorRangeBits )
{
    sw::FixedEnumBSample sample;
    sample._color = sw::EFixedColorB::Blue;
    sw::BitWriter writer;
    SW_ASSERT_TRUE( sw::FixedLayout::write( sample, writer ) );
    SW_EXPECT_EQUAL( 64 + 2, writer.getBitCount() ); // 스키마 해시 64 비트 + 열거자 셋(0..2)의 2 비트
    SW_EXPECT_TRUE( roundTrip( sample )._color == sw::EFixedColorB::Blue );

    sample._color = static_cast<sw::EFixedColorB>( 3 );
    sw::BitWriter outOfRange;
    SW_EXPECT_FALSE( sw::FixedLayout::write( sample, outOfRange ) );
}

/**
 * @brief [FixedLayoutTest] 코덱에 없는 필드(Transient)는 읽기 대상의 값이 남습니다. 읽기는 대상의 복사본에서 시작합니다
 */
SW_TEST_CASE( FixedLayoutTest, ReadKeepsFieldsOutsideTheCodec )
{
    sw::FixedNestedSample target;
    target._transientCache = 77;
    SW_EXPECT_TRUE( readBytes( writeBytes( makeNestedSample() ), target ) == sw::FixedReadResult::Ok );
    SW_EXPECT_EQUAL( target._transientCache, 77 );
    SW_EXPECT_EQUAL( target._inner._id, 255 );
}

/**
 * @brief [FixedLayoutTest] 비교는 비트 패턴입니다. NaN 은 NaN 과 같고 -0 은 +0 과 다릅니다(같은 바이트를 만드는지와 같은 뜻입니다)
 */
SW_TEST_CASE( FixedLayoutTest, EqualsComparesBitPatterns )
{
    sw::FixedScalarSample a = makeScalarSample();
    sw::FixedScalarSample b = makeScalarSample();
    SW_EXPECT_TRUE( sw::FixedLayout::equals( a, b ) );
    a._time = 0.0;
    b._time = -0.0;
    SW_EXPECT_FALSE( sw::FixedLayout::equals( a, b ) );
    b._time  = 0.0;
    b._bFlag = SW_FALSE;
    SW_EXPECT_FALSE( sw::FixedLayout::equals( a, b ) );
}

/**
 * @brief [FixedLayoutTest] 필드 순서 · 이름 · 열거자 · Step · Min 이 다르면 정규 문자열과 해시가 달라 다른 쪽 바이트를 SchemaMismatch 로 거절합니다
 */
SW_TEST_CASE( FixedLayoutTest, SchemaChangesAreRejected )
{
    expectSchemaChangeRejected<sw::FixedOrderASample, sw::FixedOrderBSample>( "FixedOrderASample", "FixedOrderBSample", {}, {} );
    expectSchemaChangeRejected<sw::FixedNameASample, sw::FixedNameBSample>( "FixedNameASample", "FixedNameBSample", {}, {} );
    expectSchemaChangeRejected<sw::FixedEnumASample, sw::FixedEnumBSample>( "FixedEnumASample", "FixedEnumBSample", "EFixedColorA", "EFixedColorB" );
    expectSchemaChangeRejected<sw::FixedStepASample, sw::FixedStepBSample>( "FixedStepASample", "FixedStepBSample", {}, {} );
    expectSchemaChangeRejected<sw::FixedMinASample, sw::FixedMinBSample>( "FixedMinASample", "FixedMinBSample", {}, {} );

    // 인코딩 규칙 버전을 올리면 모든 해시가 바뀝니다.
    const sw::string canonical = findCanonical<sw::FixedOrderASample>();
    SW_EXPECT_TRUE( sw::FixedLayout::computeSchemaHash( sw::FixedLayout::kVersion, canonical, {} ) !=
                    sw::FixedLayout::computeSchemaHash( sw::FixedLayout::kVersion + 1, canonical, {} ) );
}

/**
 * @brief [FixedLayoutTest] 이름 변경 리다이렉트(타입 · 열거형 Alias, 프로퍼티 Alias, ValueAlias, Removed)는 스키마 해시를 바꾸지 않습니다
 * @details 리다이렉트는 옛 데이터를 읽기 위한 선언이고 바이트 배치와 무관합니다(docs/plans/RenameRedirects.md 2 절 규칙 5). 선언만 다른 짝의 정규 문자열에서
 *          이름(`Declared` → `Plain`)만 맞추면 글자까지 같고, 그 해시가 선언 없는 쪽의 해시와 같아야 합니다.
 */
SW_TEST_CASE( FixedLayoutTest, RenameRedirectsDoNotChangeTheSchemaHash )
{
    const sw::string plainCanonical    = findCanonical<sw::FixedRedirectPlainSample>();
    const sw::string declaredCanonical = replaceAll( findCanonical<sw::FixedRedirectDeclaredSample>(), "Declared", "Plain" );
    SW_EXPECT_EQUAL( plainCanonical, declaredCanonical );
    SW_EXPECT_EQUAL( sw::FixedLayout::getSchemaHash<sw::FixedRedirectPlainSample>(),
                     sw::FixedLayout::computeSchemaHash( sw::FixedLayout::kVersion, declaredCanonical, {} ) );
}

/**
 * @brief [FixedLayoutTest] FQN 이 같고 필드 순서만 바뀐 빌드가 쓴 바이트도 SchemaMismatch 입니다
 * @details 한 빌드에 같은 FQN 의 두 선언을 둘 수 없으므로, `FixedOrderBSample` 의 정규 문자열에서 타입 이름만 `FixedOrderASample` 로 바꿔
 *          "필드 순서를 바꾼 A" 의 해시를 만들고 그 해시와 B 의 본문으로 스트림을 씁니다. 해시에 이름이 아닌 순서가 들어가 있어야 거절됩니다.
 */
SW_TEST_CASE( FixedLayoutTest, ReorderedFieldsWithTheSameNameAreRejected )
{
    const sw::string reorderedCanonical = replaceAll( findCanonical<sw::FixedOrderBSample>(), "FixedOrderBSample", "FixedOrderASample" );
    const uint64     reorderedHash      = sw::FixedLayout::computeSchemaHash( sw::FixedLayout::kVersion, reorderedCanonical, {} );
    SW_EXPECT_TRUE( reorderedHash != sw::FixedLayout::getSchemaHash<sw::FixedOrderASample>() );

    sw::FixedOrderBSample source;
    source._first                  = 1;
    source._second                 = 2;
    const sw::vector<uint8> stream = makeStream( reorderedHash, [&source]( sw::BitWriter& writer )
    {
        const bool bWritten = sw::FixedCodecUtil::writeVarInteger( writer, source._second, sw::FixedCodecUtil::kNoLowerBound, sw::FixedCodecUtil::kNoUpperBound ) &&
                              sw::FixedCodecUtil::writeVarInteger( writer, source._first, sw::FixedCodecUtil::kNoLowerBound, sw::FixedCodecUtil::kNoUpperBound );
        SW_EXPECT_TRUE( bWritten );
    } );
    sw::FixedOrderASample   target;
    SW_EXPECT_TRUE( readBytes( stream, target ) == sw::FixedReadResult::SchemaMismatch );
    SW_EXPECT_EQUAL( target._first, 0 );
}

/**
 * @brief [FixedLayoutTest] 포인터 · MaxCount 없는 vector · 바깥 필드 범위 · vector<string> 을 가진 타입은 코덱 대신 이유가 있습니다
 */
SW_TEST_CASE( FixedLayoutTest, IneligibleTypesReportTheReason )
{
    sw::string reason = findIneligibleReason<sw::FixedPointerSample>();
    SW_EXPECT_TRUE_MSG( reason.find( "_listOwned" ) != sw::string::npos && reason.find( "pointer" ) != sw::string::npos, reason.c_str() );

    reason = findIneligibleReason<sw::FixedUnboundedVectorSample>();
    SW_EXPECT_TRUE_MSG( reason.find( "_listValue" ) != sw::string::npos && reason.find( "MaxCount" ) != sw::string::npos, reason.c_str() );

    reason = findIneligibleReason<sw::FixedPointSample>();
    SW_EXPECT_TRUE_MSG( reason.find( "_arenaHalfSize" ) != sw::string::npos, reason.c_str() );

    reason = findIneligibleReason<sw::FixedVectorOfStringSample>();
    SW_EXPECT_TRUE_MSG( reason.find( "_listName" ) != sw::string::npos && reason.find( "nested variable-length" ) != sw::string::npos, reason.c_str() );

    // 기반 · 펼치지 못한 중첩 타입은 실행 시간에 찾아 확인합니다.
    reason.clear();
    SW_EXPECT_NOT_NULL( sw::FixedLayout::findCodec<sw::FixedNestedSample>( reason ) );
}

/**
 * @brief [FixedLayoutTest] 생성 코드가 다른 타입의 코덱을 찾는 길은 등록되지 않았거나 적격이 아닌 타입에 대신 쓸 코덱을 돌려주지 않습니다
 */
SW_TEST_CASE( FixedLayoutTest, NestedCodecLookupHasNoSubstitute )
{
    const sw::TypeLookupCache missingCache;
    SW_EXPECT_NULL( sw::FixedLayout::findNestedCodec( missingCache, sw::hashed_string( "sw::FixedNoSuchSample" ) ) );
    const sw::TypeLookupCache ineligibleCache;
    SW_EXPECT_NULL( sw::FixedLayout::findNestedCodec( ineligibleCache, sw::hashed_string( "sw::FixedPointerSample" ) ) );
    const sw::TypeLookupCache eligibleCache;
    SW_EXPECT_NOT_NULL( sw::FixedLayout::findNestedCodec( eligibleCache, sw::hashed_string( "sw::FixedInnerBaseSample" ) ) );
}

/**
 * @brief [FixedLayoutTest] 바이트를 하나씩 줄인 모든 접두는 Truncated, 남는 바이트는 TrailingData 이고 실패 뒤 대상은 그대로입니다
 */
SW_TEST_CASE( FixedLayoutTest, TruncatedAndTrailingStreamsAreRejected )
{
    expectEveryPrefixTruncated( makeScalarSample(), sw::FixedScalarSample{} );
    expectEveryPrefixTruncated( makeContainerSample(), sw::FixedContainerSample{} );
    expectEveryPrefixTruncated( makeNestedSample(), sw::FixedNestedSample{} );

    sw::vector<uint8> bytes = writeBytes( makeContainerSample() );
    bytes.push_back( 0 );
    sw::FixedContainerSample target;
    SW_EXPECT_TRUE( readBytes( bytes, target ) == sw::FixedReadResult::TrailingData );
    SW_EXPECT_TRUE( sw::FixedLayout::equals( target, sw::FixedContainerSample{} ) );
}

/**
 * @brief [FixedLayoutTest] 범위 밖 열거자 · 개수 · 실수 · 양자화 칸 번호는 자르지 않고 OutOfRange 이며 대상은 그대로입니다
 * @details `FixedContainerSample` 의 첫 필드는 `ENUM( Count )` 열거형(2 비트), 둘째는 MaxCount 4 인 vector 의 개수(3 비트)입니다.
 *          `FixedNestedSample` 의 첫 필드는 [1, 1000] 실수(32 비트), `FixedStepASample` 의 필드는 [0, 1] 을 0.1 간격으로 나눈 칸 번호(4 비트, 0 ~ 10)입니다.
 */
SW_TEST_CASE( FixedLayoutTest, OutOfRangeValuesAreRejected )
{
    const uint64             containerHash = sw::FixedLayout::getSchemaHash<sw::FixedContainerSample>();
    sw::FixedContainerSample container     = makeContainerSample();
    SW_EXPECT_TRUE( readBytes( makeStream( containerHash, []( sw::BitWriter& writer )
    { writer.writeBits( 3, 2 ); } ),
                               container ) ==
                    sw::FixedReadResult::OutOfRange ); // Count 자리는 열거자가 아닙니다
    SW_EXPECT_TRUE( readBytes( makeStream( containerHash,
                                           []( sw::BitWriter& writer )
    {
        writer.writeBits( 0, 2 );
        writer.writeBits( 7, 3 ); // MaxCount 4 를 넘는 개수
    } ),
                               container ) == sw::FixedReadResult::OutOfRange );
    SW_EXPECT_TRUE( sw::FixedLayout::equals( container, makeContainerSample() ) );

    sw::FixedNestedSample nested;
    SW_EXPECT_TRUE( readBytes( makeStream( sw::FixedLayout::getSchemaHash<sw::FixedNestedSample>(), []( sw::BitWriter& writer )
    { writer.writeFloat( 0.5f ); } ),
                               nested ) == sw::FixedReadResult::OutOfRange );
    SW_EXPECT_EQUAL( nested._arenaHalfSize, 10.0f );

    sw::FixedStepASample step;
    step._value = 0.3f;
    SW_EXPECT_TRUE( readBytes( makeStream( sw::FixedLayout::getSchemaHash<sw::FixedStepASample>(), []( sw::BitWriter& writer )
    { writer.writeBits( 15, 4 ); } ),
                               step ) == sw::FixedReadResult::OutOfRange ); // 칸 수 10 을 넘는 칸 번호
    SW_EXPECT_EQUAL( step._value, 0.3f );
}

/**
 * @brief [FixedLayoutTest] 범위 밖 정수 · MaxCount 초과 · 등록되지 않은 열거자 · 허용 범위 밖 실수는 쓰기가 실패합니다(잘라 쓰지 않습니다)
 */
SW_TEST_CASE( FixedLayoutTest, OutOfRangeValuesFailToWrite )
{
    sw::BitWriter writer;

    sw::FixedScalarSample scalar = makeScalarSample();
    scalar._hp                   = 101;
    SW_EXPECT_FALSE( sw::FixedLayout::write( scalar, writer ) );
    scalar           = makeScalarSample();
    scalar._halfSize = 0.5f; // 범위 필드는 자기 Min 을 강제합니다
    SW_EXPECT_FALSE( sw::FixedLayout::write( scalar, writer ) );

    sw::FixedContainerSample container = makeContainerSample();
    container._listValue.push_back( 1 );
    SW_EXPECT_FALSE( sw::FixedLayout::write( container, writer ) );
    container       = makeContainerSample();
    container._name = "seventeen-chars!!";
    SW_EXPECT_FALSE( sw::FixedLayout::write( container, writer ) );
    container               = makeContainerSample();
    container._listValue[1] = 1001;
    SW_EXPECT_FALSE( sw::FixedLayout::write( container, writer ) );
    container       = makeContainerSample();
    container._kind = sw::EFixedSampleKind::Count;
    SW_EXPECT_FALSE( sw::FixedLayout::write( container, writer ) );
    container        = makeContainerSample();
    container._flags = static_cast<sw::EFixedSampleFlag>( 8 );
    SW_EXPECT_FALSE( sw::FixedLayout::write( container, writer ) );
    container          = makeContainerSample();
    container._listBit = parseBits( "101010101" ); // MaxCount 8 을 넘는 비트 수
    SW_EXPECT_FALSE( sw::FixedLayout::write( container, writer ) );
}

/**
 * @brief [FixedLayoutTest] `DynamicBitset` 필드는 비트 수와 비트 열을 그대로 왕복하고, 정규 문자열은 `bits` 입니다
 * @details MaxCount 개(8 비트) · 빈 비트 열 · 위치마다 다른 값을 봅니다. 비트를 값으로 읽고 쓰므로 원소 주소를 쓰지 않습니다(`vector<bool>` 은 막혀 있다).
 */
SW_TEST_CASE( FixedLayoutTest, BitsetFieldKeepsEveryBit )
{
    SW_EXPECT_TRUE_MSG( sw::string( findCanonical<sw::FixedContainerSample>() ).find( ":maxcount=8:bits|" ) != sw::string::npos,
                        findCanonical<sw::FixedContainerSample>() );

    for ( const utf8* pBits : { "", "1", "10010110", "01111111" } )
    {
        sw::FixedContainerSample source       = makeContainerSample();
        source._listBit                       = parseBits( pBits );
        const sw::FixedContainerSample loaded = roundTrip( source );
        SW_EXPECT_EQUAL( loaded._listBit.size(), source._listBit.size() );
        SW_EXPECT_TRUE_MSG( loaded._listBit == source._listBit, loaded._listBit.to_string() + " != " + pBits );
    }

    sw::FixedContainerSample changed = makeContainerSample();
    changed._listBit.flip( 1 );
    SW_EXPECT_FALSE( sw::FixedLayout::equals( changed, makeContainerSample() ) );
}

/**
 * @brief [FixedLayoutTest] `DynamicBitset` 필드는 자기 기술형(JSON · XML · 바이너리)으로도 비트 열 그대로 왕복하고, '0' · '1' 이 아닌 글은 읽기 실패입니다
 */
SW_TEST_CASE( FixedLayoutTest, BitsetFieldRoundTripsSelfDescribingFormats )
{
    const sw::TypeInfo&      typeInfo = *sw::FixedContainerSample::StaticType();
    sw::FixedContainerSample source   = makeContainerSample();
    source._listBit                   = parseBits( "0010110" );

    sw::FixedContainerSample fromJSON;
    const sw::string         json = sw::JSONSerializer::serialize( &source, typeInfo );
    SW_EXPECT_TRUE_MSG( sw::JSONSerializer::deserialize( &fromJSON, typeInfo, json ), json );
    SW_EXPECT_TRUE_MSG( fromJSON._listBit == source._listBit, json );

    sw::FixedContainerSample fromXML;
    const sw::string         xml = sw::XMLSerializer::serialize( &source, typeInfo );
    SW_EXPECT_TRUE_MSG( sw::XMLSerializer::deserialize( &fromXML, typeInfo, xml ), xml );
    SW_EXPECT_TRUE_MSG( fromXML._listBit == source._listBit, xml );

    sw::FixedContainerSample fromBinary;
    sw::vector<uint8>        bytes;
    SW_ASSERT_TRUE( sw::BinarySerializer::serialize( &source, typeInfo, bytes ) );
    SW_EXPECT_TRUE( sw::BinarySerializer::deserialize( &fromBinary, typeInfo, bytes.data(), bytes.size() ) );
    SW_EXPECT_TRUE( fromBinary._listBit == source._listBit );

    sw::FixedContainerSample fromBadJSON;
    fromBadJSON._listBit     = parseBits( "11" );
    const sw::string badJSON = replaceAll( json, "0010110", "0010210" );
    SW_EXPECT_FALSE_MSG( sw::JSONSerializer::deserialize( &fromBadJSON, typeInfo, badJSON ), badJSON );
    SW_EXPECT_TRUE( fromBadJSON._listBit == parseBits( "11" ) );
}

/**
 * @brief [FixedLayoutTest] 같은 타입의 필드를 가리키는 범위는 생성 코드가 그 필드를 읽는 함수로 내고, 바깥 타입의 필드는 내지 않는다
 * @details 인스펙터가 `Min = "-_halfSize"` 를 그 객체의 지금 값으로 막는 근거다(`PropertyMetadata::_pReadMinRange`). 숫자 경계는 숫자로만 남고,
 *          바깥 타입의 필드(`FixedPointSample::_y` 의 `_arenaHalfSize`)는 그 객체만으로 값을 모르므로 읽기 함수가 없다.
 */
SW_TEST_CASE( FixedLayoutTest, OwnFieldRangeBecomesAReaderForTheInspector )
{
    const sw::TypeInfo* pScalarType = sw::FixedScalarSample::StaticType();
    SW_ASSERT_NOT_NULL( pScalarType );
    const sw::PropertyInfo* pOffset = pScalarType->findProperty( sw::hashed_string( "_offset" ) );
    SW_ASSERT_NOT_NULL( pOffset );
    SW_ASSERT_NOT_NULL( pOffset->_metadata->_pReadMinRange );
    SW_ASSERT_NOT_NULL( pOffset->_metadata->_pReadMaxRange );
    SW_EXPECT_TRUE( pOffset->_metadata->_bHasMinRange == SW_FALSE );

    sw::FixedScalarSample sample;
    sample._halfSize = 12.5f;
    SW_EXPECT_NEAR_EQUAL( -12.5, pOffset->_metadata->_pReadMinRange( &sample ), 1e-9 );
    SW_EXPECT_NEAR_EQUAL( 12.5, pOffset->_metadata->_pReadMaxRange( &sample ), 1e-9 );

    const sw::PropertyInfo* pHalfSize = pScalarType->findProperty( sw::hashed_string( "_halfSize" ) );
    SW_ASSERT_NOT_NULL( pHalfSize );
    SW_EXPECT_TRUE( pHalfSize->_metadata->_pReadMinRange == nullptr );
    SW_EXPECT_TRUE( pHalfSize->_metadata->_bHasMinRange == SW_TRUE );

    const sw::TypeInfo* pPointType = sw::FixedPointSample::StaticType();
    SW_ASSERT_NOT_NULL( pPointType );
    const sw::PropertyInfo* pY = pPointType->findProperty( sw::hashed_string( "_y" ) );
    SW_ASSERT_NOT_NULL( pY );
    SW_EXPECT_TRUE( pY->_metadata->_pReadMinRange == nullptr && pY->_metadata->_pReadMaxRange == nullptr );
    SW_EXPECT_TRUE( pY->_metadata->_bHasMinRange == SW_FALSE );
}

/**
 * @brief [FixedLayoutTest] 읽기 공통 끝은 필드 경계(`Min = "-_halfSize"`)도 그 객체의 읽은 값으로 지킵니다(C-62)
 * @details 예전에는 숫자 경계만 봐서 인스펙터가 막는 값이 파일로는 그대로 들어왔다. 두 쪽이 `PropertyMetadata::computeAllowedRange` 하나로 범위를 푼다.
 */
SW_TEST_CASE( FixedLayoutTest, ReadsKeepTheFieldBoundTheInspectorKeeps )
{
    SW_TEST_DEFENSIVE_SCOPE( "reads reject a value outside a field bound" );
    const sw::TypeInfo&   typeInfo = *sw::FixedScalarSample::StaticType();
    sw::FixedScalarSample source   = makeScalarSample();
    source._speed                  = 1.0f;
    source._halfSize               = 2.0f;
    source._offset                 = 2.5f; // Max = "_halfSize"

    const sw::PropertyInfo* pOffset = typeInfo.findProperty( sw::hashed_string( "_offset" ) );
    SW_ASSERT_NOT_NULL( pOffset );
    const sw::PropertyAllowedRange range = pOffset->_metadata->computeAllowedRange( &source );
    SW_EXPECT_TRUE( range._bHasMin && range._bHasMax );
    SW_EXPECT_FALSE( range.contains( 2.5 ) );
    SW_EXPECT_TRUE( range.contains( -2.0 ) );

    sw::FixedScalarSample fromXML;
    SW_EXPECT_FALSE( sw::XMLSerializer::deserialize( &fromXML, typeInfo, sw::XMLSerializer::serialize( &source, typeInfo ) ) );
    sw::FixedScalarSample fromJSON;
    SW_EXPECT_FALSE( sw::JSONSerializer::deserialize( &fromJSON, typeInfo, sw::JSONSerializer::serialize( &source, typeInfo ) ) );

    sw::vector<sw::string> listRangeError;
    sw::SerializerUtil::collectRangeErrors( typeInfo, &source, sw::string(), listRangeError );
    SW_ASSERT_EQUAL( size_t{ 1 }, listRangeError.size() );
    SW_EXPECT_TRUE( listRangeError[0].find( "_offset" ) != sw::string::npos );

    // 경계 값은 읽힌다(경계는 같은 파일에서 읽은 `_halfSize` 다).
    source._offset = 2.0f;
    sw::FixedScalarSample fromBound;
    SW_EXPECT_TRUE( sw::XMLSerializer::deserialize( &fromBound, typeInfo, sw::XMLSerializer::serialize( &source, typeInfo ) ) );
    source._offset = -2.0f;
    sw::FixedScalarSample fromInRange;
    SW_EXPECT_TRUE( sw::XMLSerializer::deserialize( &fromInRange, typeInfo, sw::XMLSerializer::serialize( &source, typeInfo ) ) );
    SW_EXPECT_NEAR_EQUAL( -2.0f, fromInRange._offset, 1e-6f );
}

/**
 * @brief [FixedLayoutTest] 자기 기술형 세 포맷의 일반 읽기도 `Min` · `Max` 밖 값을 성공으로 돌려주지 않습니다(A-19)
 * @details 예전에는 설정 · 데이터 · 격투 세이브처럼 `collectRangeErrors` 를 직접 부른 호출부만 범위를 봤습니다. 이제 읽기 공통 끝이 봅니다.
 */
SW_TEST_CASE( FixedLayoutTest, SelfDescribingFormatsRejectValuesOutsideDeclaredRange )
{
    SW_TEST_DEFENSIVE_SCOPE( "reads reject a value outside PROPERTY( Min, Max )" );
    const sw::TypeInfo&   typeInfo = *sw::FixedScalarSample::StaticType();
    sw::FixedScalarSample source   = makeScalarSample();
    source._hp                     = 101;  // Max = 100
    source._speed                  = 1.0f; // NaN 은 JSON 이 다른 이유로 실패할 수 있다

    sw::vector<uint8> binary;
    SW_ASSERT_TRUE( sw::BinarySerializer::serialize( &source, typeInfo, binary ) ); // 쓰기는 범위를 보지 않는다
    sw::FixedScalarSample fromBinary;
    SW_EXPECT_FALSE( sw::BinarySerializer::deserialize( &fromBinary, typeInfo, binary.data(), binary.size() ) );

    sw::FixedScalarSample fromJSON;
    SW_EXPECT_FALSE( sw::JSONSerializer::deserialize( &fromJSON, typeInfo, sw::JSONSerializer::serialize( &source, typeInfo ) ) );

    sw::FixedScalarSample fromXML;
    SW_EXPECT_FALSE( sw::XMLSerializer::deserialize( &fromXML, typeInfo, sw::XMLSerializer::serialize( &source, typeInfo ) ) );

    // 경계 값은 읽힌다.
    source._hp = 100;
    sw::vector<uint8> inRange;
    SW_ASSERT_TRUE( sw::BinarySerializer::serialize( &source, typeInfo, inRange ) );
    sw::FixedScalarSample fromInRange;
    SW_EXPECT_TRUE( sw::BinarySerializer::deserialize( &fromInRange, typeInfo, inRange.data(), inRange.size() ) );
    SW_EXPECT_EQUAL( 100, fromInRange._hp );
}

/**
 * @brief [FixedLayoutTest] 소프트 읽기는 범위 밖 값에서 멈추지 않고 orphan 을 끝까지 모은 뒤 `Rejected` 로 돌아오고, 본문을 못 읽은 것만 `NotParsed` 입니다(C-60)
 * @details 예전에는 결과가 `bool` 하나라 부르는 쪽이 실패에서 바로 돌아가 같은 파일의 모르는 키를 알리지 않았고, 범위 밖 값을 "JSON 이 아니다"로 알렸다.
 */
SW_TEST_CASE( FixedLayoutTest, SoftReadsCollectEveryOrphanBeforeRejecting )
{
    SW_TEST_DEFENSIVE_SCOPE( "soft reads with a value outside PROPERTY( Min, Max ) and an unknown key" );
    const sw::TypeInfo&   typeInfo = *sw::FixedScalarSample::StaticType();
    sw::FixedScalarSample source   = makeScalarSample();
    source._hp                     = 101; // Max = 100
    source._speed                  = 1.0f;

    sw::string json = sw::JSONSerializer::serialize( &source, typeInfo );
    SW_ASSERT_TRUE( json.empty() == false && json.front() == '{' );
    json.insert( 1, R"("_bogus":1,)" );
    sw::FixedScalarSample             fromJSON;
    sw::vector<sw::SchemaOrphanValue> listJSONOrphan;
    SW_EXPECT_TRUE( sw::JSONSerializer::deserializeSoft( &fromJSON, typeInfo, json, &listJSONOrphan ) == sw::SoftReadResult::Rejected );
    SW_EXPECT_EQUAL( size_t{ 1 }, listJSONOrphan.size() );
    SW_EXPECT_EQUAL( 101, fromJSON._hp ); // 자르지 않는다

    // 칸이 모두 속성이면 루트가 `<FixedScalarSample .../>` 로 닫힌다 — 그때는 자식을 넣을 자리를 연다.
    sw::string   xml        = sw::XMLSerializer::serialize( &source, typeInfo );
    const size_t closeIndex = xml.rfind( "</" );
    if ( closeIndex != sw::string::npos )
        xml.insert( closeIndex, "<_bogusChild>1</_bogusChild>" );
    else
    {
        const size_t selfCloseIndex = xml.rfind( "/>" );
        SW_ASSERT_TRUE_MSG( selfCloseIndex != sw::string::npos, xml.c_str() );
        xml.replace( selfCloseIndex, 2, sw::string( "><_bogusChild>1</_bogusChild></" ) + typeInfo._name.c_str() + ">" );
    }
    sw::FixedScalarSample             fromXML;
    sw::vector<sw::SchemaOrphanValue> listXMLOrphan;
    SW_EXPECT_TRUE( sw::XMLSerializer::deserializeSoft( &fromXML, typeInfo, xml, &listXMLOrphan ) == sw::SoftReadResult::Rejected );
    SW_EXPECT_EQUAL( size_t{ 1 }, listXMLOrphan.size() );

    sw::vector<uint8> binary;
    SW_ASSERT_TRUE( sw::BinarySerializer::serialize( &source, typeInfo, binary ) );
    sw::FixedScalarSample             fromBinary;
    sw::vector<sw::SchemaOrphanValue> listBinaryOrphan;
    SW_EXPECT_TRUE( sw::BinarySerializer::deserializeSoft( &fromBinary, typeInfo, binary.data(), binary.size(), &listBinaryOrphan ) == sw::SoftReadResult::Rejected );

    // 본문을 못 읽은 것만 `NotParsed` 다.
    sw::FixedScalarSample             notRead;
    sw::vector<sw::SchemaOrphanValue> listNotReadOrphan;
    SW_EXPECT_TRUE( sw::JSONSerializer::deserializeSoft( &notRead, typeInfo, "[1, 2]", &listNotReadOrphan ) == sw::SoftReadResult::NotParsed );
    SW_EXPECT_TRUE( sw::XMLSerializer::deserializeSoft( &notRead, typeInfo, "<FixedScalarSample>", &listNotReadOrphan ) == sw::SoftReadResult::NotParsed );
    const uint8 arrTruncated[2] = { 1, 2 };
    SW_EXPECT_TRUE( sw::BinarySerializer::deserializeSoft( &notRead, typeInfo, arrTruncated, 2, &listNotReadOrphan ) == sw::SoftReadResult::NotParsed );
    SW_EXPECT_TRUE( listNotReadOrphan.empty() );
}
