#include "pch.h"

#include "Core/Network/BitStream.h"

#include "Engine/Reflection/ReflectionCore.h"
#include "Engine/Serialization/Fixed/FixedLayout.h"

#include "ReflectionTest/TestFixedLayoutTypes.h"

#include "TestFramework/TestFramework.h"

// 고정 배치형(B) 코덱 — 생성된 쓰기 · 읽기 · 비교가 왕복하고, 다른 스키마 · 잘린 스트림 · 범위 밖 값을 거절하는가.
// 명세는 docs/plans/ReflectionFormats.md 6 절이다. 표본 타입은 TestFixedLayoutTypes.h 이고 손 코덱 · 비교가 없다.

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

    /** @brief 쓰고 읽은 값입니다. `Step` 필드는 한 번 양자화되므로 두 번째 왕복부터 비트까지 같아야 한다. */
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

    template <typename T>
    const utf8* findCanonical()
    {
        sw::string            reason;
        const sw::FixedCodec* pCodec = sw::FixedLayout::findCodec<T>( reason );
        SW_EXPECT_TRUE_MSG( pCodec != nullptr, reason.c_str() );
        return pCodec != nullptr ? pCodec->_pCanonical : "";
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
        sample._speed    = std::numeric_limits<float32>::quiet_NaN(); // 범위 없는 실수는 비트 그대로
        sample._halfSize = 1000.0f;
        sample._offset   = 12.5f; // Step 0.5 격자 위
        sample._ratio    = 1.0f;
        sample._time     = 1234.5678;
        return sample;
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
        sample._listBit    = { true, false, true };
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
 * @brief [FixedLayoutTest] 세 표본이 왕복하고 생성 비교가 같다고 답한다 — 경계값 · 빈 컨테이너 · MaxCount 개 포함
 */
SW_TEST_CASE( FixedLayoutTest, SamplesRoundTripAtTheBoundaries )
{
    const sw::FixedScalarSample scalar = makeScalarSample();
    SW_EXPECT_TRUE( sw::FixedLayout::equals( roundTrip( scalar ), scalar ) );

    const sw::FixedContainerSample container = makeContainerSample();
    SW_EXPECT_TRUE( sw::FixedLayout::equals( roundTrip( container ), container ) );
    // 빈 컨테이너. `_position` 의 0 은 Step 0.01 격자(-10 에서 센다) 위에 정확히 있지 않으므로 한 번 양자화한 값을 비교한다.
    const sw::FixedContainerSample emptyContainer = roundTrip( sw::FixedContainerSample{} );
    SW_EXPECT_TRUE( emptyContainer._listValue.empty() && emptyContainer._name.empty() );
    SW_EXPECT_TRUE( sw::FixedLayout::equals( roundTrip( emptyContainer ), emptyContainer ) );

    // Step 필드는 한 번 양자화된다 — 첫 왕복 값은 간격 안에 있고, 그 값은 다시 왕복해도 비트까지 같다.
    const sw::FixedNestedSample nested = makeNestedSample();
    const sw::FixedNestedSample once   = roundTrip( nested );
    SW_EXPECT_NEAR_EQUAL( once._inner._x, nested._inner._x, 0.05f );
    SW_EXPECT_EQUAL( once._derived._baseValue, -3 );
    SW_EXPECT_EQUAL( once._derived._derivedValue, 9 );
    SW_EXPECT_EQUAL( once._listInner.size(), static_cast<size_t>( 3 ) );
    SW_EXPECT_TRUE( sw::FixedLayout::equals( roundTrip( once ), once ) );
}

/**
 * @brief [FixedLayoutTest] 코덱에 없는 필드(Transient)는 읽기 대상의 값이 남는다 — 읽기는 대상 복사본에서 시작한다
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
 * @brief [FixedLayoutTest] 비교는 비트 패턴이다 — NaN 은 NaN 과 같고 -0 은 +0 과 다르다(같은 바이트를 만드는가와 같은 뜻)
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
 * @brief [FixedLayoutTest] 필드 순서 · 이름 · 열거자 · Step · Min 이 다르면 정규 문자열과 해시가 달라 다른 쪽 바이트를 SchemaMismatch 로 거절한다
 */
SW_TEST_CASE( FixedLayoutTest, SchemaChangesAreRejected )
{
    expectSchemaChangeRejected<sw::FixedOrderASample, sw::FixedOrderBSample>( "FixedOrderASample", "FixedOrderBSample", {}, {} );
    expectSchemaChangeRejected<sw::FixedNameASample, sw::FixedNameBSample>( "FixedNameASample", "FixedNameBSample", {}, {} );
    expectSchemaChangeRejected<sw::FixedEnumASample, sw::FixedEnumBSample>( "FixedEnumASample", "FixedEnumBSample", "EFixedColorA", "EFixedColorB" );
    expectSchemaChangeRejected<sw::FixedStepASample, sw::FixedStepBSample>( "FixedStepASample", "FixedStepBSample", {}, {} );
    expectSchemaChangeRejected<sw::FixedMinASample, sw::FixedMinBSample>( "FixedMinASample", "FixedMinBSample", {}, {} );

    // 인코딩 규칙 버전을 올리면 모든 해시가 바뀐다.
    const sw::string canonical = findCanonical<sw::FixedOrderASample>();
    SW_EXPECT_TRUE( sw::FixedLayout::computeSchemaHash( sw::FixedLayout::kVersion, canonical, {} ) !=
                    sw::FixedLayout::computeSchemaHash( sw::FixedLayout::kVersion + 1, canonical, {} ) );
}

/**
 * @brief [FixedLayoutTest] 포인터 필드 · MaxCount 없는 vector · 바깥 필드 범위를 가진 타입은 코덱이 없고, 진입점이 이유와 함께 멈춘다
 */
SW_TEST_CASE( FixedLayoutTest, IneligibleTypesStopWithTheReason )
{
    sw::string reason;
    SW_EXPECT_NULL( sw::FixedLayout::findCodec<sw::FixedPointerSample>( reason ) );
    SW_EXPECT_TRUE_MSG( reason.find( "_listOwned" ) != sw::string::npos && reason.find( "pointer" ) != sw::string::npos, reason.c_str() );

    reason.clear();
    SW_EXPECT_NULL( sw::FixedLayout::findCodec<sw::FixedUnboundedVectorSample>( reason ) );
    SW_EXPECT_TRUE_MSG( reason.find( "_listValue" ) != sw::string::npos && reason.find( "MaxCount" ) != sw::string::npos, reason.c_str() );

    reason.clear();
    SW_EXPECT_NULL( sw::FixedLayout::findCodec<sw::FixedPointSample>( reason ) );
    SW_EXPECT_TRUE_MSG( reason.find( "_arenaHalfSize" ) != sw::string::npos, reason.c_str() );

    // 기반 · 펼치지 못한 중첩 타입은 실행 시간에 찾아 확인한다.
    reason.clear();
    SW_EXPECT_NOT_NULL( sw::FixedLayout::findCodec<sw::FixedNestedSample>( reason ) );

    SW_TEST_REQUIRES_FEATURE( DebugAsserts );
    SW_TEST_DEFENSIVE_SCOPE( "writing a type without a fixed layout codec logs the reason and asserts" );
    test::ScopedAssertCapture assertCapture;
    sw::BitWriter             writer;
    SW_EXPECT_FALSE( sw::FixedLayout::write( sw::FixedPointerSample{}, writer ) );
    SW_EXPECT_EQUAL( assertCapture.getCount(), 1u );
}

/**
 * @brief [FixedLayoutTest] 바이트를 하나씩 줄인 모든 접두는 Truncated, 남는 바이트는 TrailingData 이고 실패 뒤 대상은 그대로다
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
 * @brief [FixedLayoutTest] 범위 밖 열거자 · 개수는 OutOfRange 이고 대상은 그대로다
 * @details 표본의 첫 필드는 `ENUM( Count )` 열거형(2 비트), 둘째는 MaxCount 4 인 vector 의 개수(3 비트)다.
 */
SW_TEST_CASE( FixedLayoutTest, OutOfRangeValuesAreRejected )
{
    const uint64 schemaHash = sw::FixedLayout::getSchemaHash<sw::FixedContainerSample>();

    sw::BitWriter badEnum;
    sw::FixedCodecUtil::writeRawBits( badEnum, schemaHash, 64 );
    badEnum.writeBits( 3, 2 ); // Count 자리 — 열거자가 아니다
    sw::FixedContainerSample target = makeContainerSample();
    SW_EXPECT_TRUE( readBytes( badEnum.getBytes(), target ) == sw::FixedReadResult::OutOfRange );

    sw::BitWriter badCount;
    sw::FixedCodecUtil::writeRawBits( badCount, schemaHash, 64 );
    badCount.writeBits( 0, 2 );
    badCount.writeBits( 7, 3 ); // MaxCount 4 를 넘는 개수
    SW_EXPECT_TRUE( readBytes( badCount.getBytes(), target ) == sw::FixedReadResult::OutOfRange );
    SW_EXPECT_TRUE( sw::FixedLayout::equals( target, makeContainerSample() ) );
}

/**
 * @brief [FixedLayoutTest] 범위 밖 정수 · MaxCount 초과 · 등록되지 않은 열거자 · 허용 범위 밖 실수는 쓰기가 실패한다(잘라 쓰지 않는다)
 */
SW_TEST_CASE( FixedLayoutTest, OutOfRangeValuesFailToWrite )
{
    sw::BitWriter writer;

    sw::FixedScalarSample scalar = makeScalarSample();
    scalar._hp                   = 101;
    SW_EXPECT_FALSE( sw::FixedLayout::write( scalar, writer ) );
    scalar           = makeScalarSample();
    scalar._halfSize = 0.5f; // 범위 필드는 자기 Min 을 강제한다
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
}
