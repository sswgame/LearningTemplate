#include "pch.h"

#include "Core/Math/PackedTypes.h"
#include "Core/Math/XorShift32.h"
#include "Core/String/hashed_string.h"

#include "Engine/Reflection/ReflectValue.h"
#include "Engine/Serialization/Base/SerializeContext.h"
#include "Engine/UI/Binding/UIBindingValue.h"

#include "TestFramework/TestFramework.h"

using namespace sw;

namespace
{
    /** @brief 압축 타입 직렬화 테스트의 도우미입니다. */
    struct PackedSerializationTestInternal
    {
        /** @brief xorshift32 의 다음 값입니다. 상태를 갱신합니다. */
        static uint32 nextRandom( uint32& inoutState )
        {
            inoutState = XorShift32::next( inoutState );
            return inoutState;
        }

        /** @brief 두 값의 바이트가 같은지입니다(압축 타입은 비트 복사 가능하고 패딩이 없다). */
        template <typename T>
        static bool hasSameBits( const T& left, const T& right )
        {
            return std::memcmp( &left, &right, sizeof( T ) ) == 0;
        }

        /** @brief 이진 처리기로 쓰고 읽어 같은 비트인지 봅니다. 크기는 `sizeof( T )` 이어야 한다. */
        template <typename T>
        static void expectBinaryRoundTrip( const SerializeContext& context, const utf8* pTypeName, const T& value )
        {
            const SerializeContext::BinaryWriteFn* pWriter = context.findBinaryWriter( hashed_string( pTypeName ) );
            const SerializeContext::BinaryReadFn*  pReader = context.findBinaryReader( hashed_string( pTypeName ) );
            SW_EXPECT_NOT_NULL( pWriter );
            SW_EXPECT_NOT_NULL( pReader );
            if ( pWriter == nullptr || pReader == nullptr )
                return;
            vector<uint8> bytes;
            ( *pWriter )( &value, bytes );
            SW_EXPECT_EQUAL( sizeof( T ), bytes.size() );
            T      readBack{};
            size_t offset{ 0 };
            SW_EXPECT_TRUE_MSG( ( *pReader )( &readBack, bytes.data(), bytes.size(), offset ), pTypeName );
            SW_EXPECT_EQUAL( bytes.size(), offset );
            SW_EXPECT_TRUE_MSG( hasSameBits( value, readBack ), pTypeName );
        }

        /** @brief 글 처리기로 쓰고 읽어 같은 비트인지 봅니다. @p outText 에 쓴 글을 돌려줍니다. */
        template <typename T>
        static void expectTextRoundTrip( const SerializeContext& context, const utf8* pTypeName, const T& value, string& outText )
        {
            const SerializeContext::TextWriteFn* pWriter = context.findTextWriter( hashed_string( pTypeName ) );
            const SerializeContext::TextReadFn*  pReader = context.findTextReader( hashed_string( pTypeName ) );
            SW_EXPECT_NOT_NULL( pWriter );
            SW_EXPECT_NOT_NULL( pReader );
            if ( pWriter == nullptr || pReader == nullptr )
                return;
            outText = ( *pWriter )( &value );
            T readBack{};
            SW_EXPECT_TRUE_MSG( ( *pReader )( &readBack, outText ), outText.c_str() );
            SW_EXPECT_TRUE_MSG( hasSameBits( value, readBack ), outText.c_str() );
        }

        template <typename T>
        static void expectTextRoundTrip( const SerializeContext& context, const utf8* pTypeName, const T& value )
        {
            string text;
            expectTextRoundTrip( context, pTypeName, value, text );
        }

        /** @brief 글 읽기가 거절되고 값이 그대로인지 봅니다. */
        template <typename T>
        static void expectTextRejected( const SerializeContext& context, const utf8* pTypeName, const T& original, const utf8* pText )
        {
            const SerializeContext::TextReadFn* pReader = context.findTextReader( hashed_string( pTypeName ) );
            SW_EXPECT_NOT_NULL( pReader );
            if ( pReader == nullptr )
                return;
            T value = original;
            SW_EXPECT_FALSE_MSG( ( *pReader )( &value, pText ), pText );
            SW_EXPECT_TRUE_MSG( hasSameBits( value, original ), pText );
        }
    };
} // namespace

/**
 * @brief [PackedValueSerializationTest] 표의 모든 내장 타입은 이진 · 글 처리기를 둘 다 가진다
 * @details `ReflectBuiltins.xxx` 의 줄(`TextConv` 가 `none` 인 줄은 손으로 등록한다)마다 처리기가 있어야 한다. 줄만 더하고 글 처리기를 빠뜨리면 그 타입의
 *          프로퍼티가 저장 단계에서야 "읽지 못함" 으로 드러난다.
 */
SW_TEST_CASE( PackedValueSerializationTest, EveryBuiltinTypeHasBinaryAndTextHandler )
{
    const SerializeContext context = SerializeContext::deriveFromDefault( SerializePurpose::Authoring );
    uint32                 rowCount{ 0 };
#define SW_REFLECT_BUILTIN_TYPE( Canon, CppType, TextConv, Ns, ... ) \
    {                                                                \
        const hashed_string typeName( #Canon );                      \
        SW_EXPECT_NOT_NULL( context.findBinaryWriter( typeName ) );  \
        SW_EXPECT_NOT_NULL( context.findBinaryReader( typeName ) );  \
        SW_EXPECT_NOT_NULL( context.findTextWriter( typeName ) );    \
        SW_EXPECT_NOT_NULL( context.findTextReader( typeName ) );    \
        ++rowCount;                                                  \
    }
#define SW_REFLECT_BUILTIN_CONTAINER( ... )
#include "Engine/Reflection/ReflectBuiltins.xxx"
#undef SW_REFLECT_BUILTIN_TYPE
#undef SW_REFLECT_BUILTIN_CONTAINER
    SW_EXPECT_EQUAL( static_cast<uint32>( ReflectBuiltinIndex::kCount ), rowCount );
}

/**
 * @brief [PackedValueSerializationTest] 이진 처리기는 압축 타입의 비트를 그대로 옮긴다 — NaN 페이로드와 snorm 의 정수 최솟값도
 */
SW_TEST_CASE( PackedValueSerializationTest, BinaryRoundTripKeepsEveryBit )
{
    const SerializeContext context = SerializeContext::deriveFromDefault( SerializePurpose::Authoring );
    using Test                     = PackedSerializationTestInternal;

    Test::expectBinaryRoundTrip( context, "half", half::fromBits( 0x7C01u ) ); // 신호 NaN 페이로드는 글로는 못 가지만 이진은 비트 그대로다
    Test::expectBinaryRoundTrip( context, "half", half::fromBits( 0xFC00u ) );
    Test::expectBinaryRoundTrip( context, "half2", half2{ half::fromBits( 0x3C00u ), half::fromBits( 0xBC01u ) } );
    Test::expectBinaryRoundTrip( context, "half3", half3{ half::fromBits( 1u ), half::fromBits( 2u ), half::fromBits( 3u ) } );
    Test::expectBinaryRoundTrip( context, "half4", half4{ half::fromBits( 1u ), half::fromBits( 2u ), half::fromBits( 3u ), half::fromBits( 0x7FFFu ) } );
    Test::expectBinaryRoundTrip( context, "unorm8", unorm8::fromRaw( 200 ) );
    Test::expectBinaryRoundTrip( context, "unorm16", unorm16::fromRaw( 54321 ) );
    Test::expectBinaryRoundTrip( context, "snorm8", snorm8::fromRaw( -128 ) );
    Test::expectBinaryRoundTrip( context, "snorm16", snorm16::fromRaw( -32768 ) );
    unorm8x4 color{};
    color._arrValue[0] = unorm8::fromRaw( 1 );
    color._arrValue[3] = unorm8::fromRaw( 255 );
    Test::expectBinaryRoundTrip( context, "unorm8x4", color );
    snorm16x4 vector4{};
    vector4._arrValue[1] = snorm16::fromRaw( -32768 );
    vector4._arrValue[2] = snorm16::fromRaw( 32767 );
    Test::expectBinaryRoundTrip( context, "snorm16x4", vector4 );
    Test::expectBinaryRoundTrip( context, "OctahedralNormal", OctahedralNormal{ snorm16::fromRaw( -32768 ), snorm16::fromRaw( 7 ) } );
    Test::expectBinaryRoundTrip( context, "RGB10A2", RGB10A2::fromPacked( 0xDEADBEEFu ) );
}

/**
 * @brief [PackedValueSerializationTest] 글 처리기는 유한한 half 65536 개 가운데 NaN · 무한대를 뺀 모든 비트 패턴을 같은 비트로 되돌린다
 */
SW_TEST_CASE( PackedValueSerializationTest, TextRoundTripKeepsEveryFiniteHalf )
{
    const SerializeContext               context = SerializeContext::deriveFromDefault( SerializePurpose::Authoring );
    const SerializeContext::TextWriteFn* pWriter = context.findTextWriter( hashed_string( "half" ) );
    const SerializeContext::TextReadFn*  pReader = context.findTextReader( hashed_string( "half" ) );
    SW_ASSERT_NOT_NULL( pWriter );
    SW_ASSERT_NOT_NULL( pReader );

    uint32 mismatchCount{ 0 };
    for ( uint32 bits = 0; bits <= 0xFFFFu; ++bits )
    {
        const half value = half::fromBits( static_cast<uint16>( bits ) );
        if ( value.isFinite() == false )
            continue;
        const string text = ( *pWriter )( &value );
        half         readBack{};
        const bool   bRead = ( *pReader )( &readBack, text );
        if ( bRead == false || readBack.getBits() != value.getBits() )
            ++mismatchCount;
    }
    SW_EXPECT_EQUAL( uint32{ 0 }, mismatchCount );

    // 무한대 글은 실수 읽기가 유한한 값만 받아 거절된다 - 값은 그대로다(이진은 비트 그대로, 위 테스트).
    PackedSerializationTestInternal::expectTextRejected( context, "half", half::fromFloat( 2.0f ), "inf" );
}

/**
 * @brief [PackedValueSerializationTest] 정규화 정수와 묶음, 옥타헤드럴 좌표, RGB10A2 는 글 왕복에서 비트가 같다
 * @details snorm 의 정수 최솟값(-128 · -32768)은 풀면 -1 이라 글로는 -최대 로 간다(`fromFloat` 가 최솟값을 만들지 않는다) — 여기서는 -최대 이상만 본다.
 */
SW_TEST_CASE( PackedValueSerializationTest, TextRoundTripKeepsNormalizedAndPackedBits )
{
    const SerializeContext context = SerializeContext::deriveFromDefault( SerializePurpose::Authoring );
    using Test                     = PackedSerializationTestInternal;

    for ( uint32 raw = 0; raw <= 255; ++raw )
    {
        Test::expectTextRoundTrip( context, "unorm8", unorm8::fromRaw( static_cast<uint8>( raw ) ) );
    }
    for ( int32 raw = -127; raw <= 127; ++raw )
    {
        Test::expectTextRoundTrip( context, "snorm8", snorm8::fromRaw( static_cast<int8>( raw ) ) );
    }
    for ( const uint16 raw : { uint16{ 0 }, uint16{ 1 }, uint16{ 255 }, uint16{ 12345 }, uint16{ 40000 }, uint16{ 65534 }, uint16{ 65535 } } )
    {
        Test::expectTextRoundTrip( context, "unorm16", unorm16::fromRaw( raw ) );
    }
    for ( const int16 raw : { int16{ -32767 }, int16{ -12345 }, int16{ -1 }, int16{ 0 }, int16{ 1 }, int16{ 32766 }, int16{ 32767 } } )
    {
        Test::expectTextRoundTrip( context, "snorm16", snorm16::fromRaw( raw ) );
    }

    // 묶음과 그 밖의 타입은 결정적 난수 비트로 본다. 정수 최솟값은 글로 -최대 가 되므로 뺀다.
    uint32 state{ 0x2468ACE1u };
    for ( uint32 sampleIndex = 0; sampleIndex < 500; ++sampleIndex )
    {
        unorm8x4 color{};
        for ( unorm8& channel : color._arrValue )
        {
            channel = unorm8::fromRaw( static_cast<uint8>( Test::nextRandom( state ) % 256u ) );
        }
        Test::expectTextRoundTrip( context, "unorm8x4", color );

        snorm8x4 signedColor{};
        for ( snorm8& channel : signedColor._arrValue )
        {
            channel = snorm8::fromRaw( static_cast<int8>( static_cast<int32>( Test::nextRandom( state ) % 255u ) - 127 ) );
        }
        Test::expectTextRoundTrip( context, "snorm8x4", signedColor );

        snorm16x4 vector4{};
        for ( snorm16& component : vector4._arrValue )
        {
            component = snorm16::fromRaw( static_cast<int16>( static_cast<int32>( Test::nextRandom( state ) % 65535u ) - 32767 ) );
        }
        Test::expectTextRoundTrip( context, "snorm16x4", vector4 );

        const float32 directionX = static_cast<float32>( Test::nextRandom( state ) % 2001u ) - 1000.0f;
        const float32 directionY = static_cast<float32>( Test::nextRandom( state ) % 2001u ) - 1000.0f;
        const float32 directionZ = static_cast<float32>( Test::nextRandom( state ) % 2001u ) - 1000.0f;
        Test::expectTextRoundTrip( context, "OctahedralNormal", OctahedralNormal::fromNormal( float3{ directionX, directionY, directionZ } ) );
        Test::expectTextRoundTrip( context, "RGB10A2", RGB10A2::fromPacked( Test::nextRandom( state ) ) );
        const float32 scaled = static_cast<float32>( Test::nextRandom( state ) % 2001u ) * 0.37f - 300.0f;
        Test::expectTextRoundTrip( context, "half4", half4::fromFloat4( float4{ scaled, 0.1f, -0.0f, 65504.0f } ) );
    }

    // 쓴 글은 성분 실수의 쉼표 목록이다.
    string text;
    Test::expectTextRoundTrip( context, "half3", half3::fromFloat3( float3{ 0.5f, 1.0f, 0.25f } ), text );
    SW_EXPECT_STREQ( "0.5,1,0.25", text.c_str() );
    Test::expectTextRoundTrip( context, "unorm16x2", unorm16x2::fromFloat2( float2{ 0.0f, 1.0f } ), text );
    SW_EXPECT_STREQ( "0,1", text.c_str() );
}

/**
 * @brief [PackedValueSerializationTest] 글 읽기는 개수가 틀리거나 범위를 넘거나 숫자가 아니면 거절하고 값을 그대로 둔다
 * @details `unorm8` 에 "5" 가 말없이 1 로 잘리지 않아야 한다(정수 필드가 범위를 넘는 글을 거절하는 것과 같은 규칙).
 */
SW_TEST_CASE( PackedValueSerializationTest, TextReadRejectsWrongCountOutOfRangeAndNonNumbers )
{
    const SerializeContext context = SerializeContext::deriveFromDefault( SerializePurpose::Authoring );
    using Test                     = PackedSerializationTestInternal;

    Test::expectTextRejected( context, "unorm8", unorm8::fromRaw( 9 ), "5" );
    Test::expectTextRejected( context, "unorm8", unorm8::fromRaw( 9 ), "-0.5" );
    Test::expectTextRejected( context, "snorm16", snorm16::fromRaw( 9 ), "1.5" );
    Test::expectTextRejected( context, "half", half::fromFloat( 1.0f ), "70000" );
    Test::expectTextRejected( context, "half", half::fromFloat( 1.0f ), "abc" );
    Test::expectTextRejected( context, "half", half::fromFloat( 1.0f ), "" );
    Test::expectTextRejected( context, "half2", half2::fromFloat2( float2{ 1.0f, 2.0f } ), "0.5" );
    Test::expectTextRejected( context, "half2", half2::fromFloat2( float2{ 1.0f, 2.0f } ), "0.5,1,2" );
    Test::expectTextRejected( context, "unorm16x2", unorm16x2::fromFloat2( float2{ 0.25f, 0.5f } ), "0.5,2" );
    Test::expectTextRejected( context, "OctahedralNormal", OctahedralNormal{}, "0.5,x" );
    Test::expectTextRejected( context, "RGB10A2", RGB10A2::fromPacked( 77u ), "0,0,0" );

    // 경계 값과 앞뒤 공백은 받는다.
    const SerializeContext::TextReadFn* pReadUnorm = context.findTextReader( hashed_string( "unorm8" ) );
    SW_ASSERT_NOT_NULL( pReadUnorm );
    unorm8 value{};
    SW_EXPECT_TRUE( ( *pReadUnorm )( &value, " 1 " ) );
    SW_EXPECT_EQUAL( uint8{ 255 }, value.getRaw() );
    SW_EXPECT_TRUE( ( *pReadUnorm )( &value, "0" ) );
    SW_EXPECT_EQUAL( uint8{ 0 }, value.getRaw() );
}

/**
 * @brief [PackedValueSerializationTest] 인자 변환은 압축 타입을 글 · 숫자에서 받고, 성분이 하나면 숫자로 내보낸다
 * @details 콘솔 명령 · 기믹 배선은 값에 타입 이름을 붙여 인자 자리마다 바꾼다(`ReflectTypeOpsOf<T>::convert`). 범위를 넘는 값은 거절한다.
 */
SW_TEST_CASE( PackedValueSerializationTest, ArgumentConversionReadsAndWritesPackedValues )
{
    TaskValue converted;

    // 같은 이름 - 압축 타입의 이름은 표의 정규 이름이다.
    SW_EXPECT_TRUE( getReflectTypeName<unorm8x4>() == hashed_string( "unorm8x4" ) );
    SW_EXPECT_TRUE( ReflectBuiltinOf<OctahedralNormal>::kIndex == ReflectBuiltinIndex::kOctahedralNormal );

    // 글 -> half (숫자 글)
    SW_EXPECT_TRUE( ReflectTypeOpsOf<half>::convert( ReflectValue::makeText( "0.5" ), converted ) );
    SW_EXPECT_EQUAL( half::fromFloat( 0.5f ).getBits(), converted.getValue<half>().getBits() );
    // float64 -> unorm8, 반올림하고 범위를 넘으면 거절한다.
    SW_EXPECT_TRUE( ReflectTypeOpsOf<unorm8>::convert( ReflectValue::make( float64{ 0.5 } ), converted ) );
    SW_EXPECT_EQUAL( uint8{ 128 }, converted.getValue<unorm8>().getRaw() );
    SW_EXPECT_FALSE( ReflectTypeOpsOf<unorm8>::convert( ReflectValue::make( float64{ 5.0 } ), converted ) );
    SW_EXPECT_FALSE( ReflectTypeOpsOf<half>::convert( ReflectValue::make( float64{ 1.0e300 } ), converted ) );
    // 글 -> half2, 개수가 맞아야 한다.
    SW_EXPECT_TRUE( ReflectTypeOpsOf<half2>::convert( ReflectValue::makeText( "0.25, 0.5" ), converted ) );
    const float2 pair = converted.getValue<half2>().toFloat2();
    SW_EXPECT_EQUAL( 0.25f, pair._x );
    SW_EXPECT_EQUAL( 0.5f, pair._y );
    SW_EXPECT_FALSE( ReflectTypeOpsOf<half2>::convert( ReflectValue::makeText( "0.25" ), converted ) );
    // 성분이 여럿인 타입은 숫자 하나를 받지 않는다.
    SW_EXPECT_FALSE( ReflectTypeOpsOf<unorm8x4>::convert( ReflectValue::make( float64{ 0.5 } ), converted ) );
    // half -> float32 (성분 하나는 숫자로 읽힌다)
    SW_EXPECT_TRUE( ReflectTypeOpsOf<float32>::convert( ReflectValue::make( half::fromFloat( 1.5f ) ), converted ) );
    SW_EXPECT_EQUAL( 1.5f, converted.getValue<float32>() );
    // 무한대 half 는 숫자로 읽히지 않는다.
    SW_EXPECT_FALSE( ReflectTypeOpsOf<float32>::convert( ReflectValue::make( half::fromBits( 0x7C00u ) ), converted ) );
    // 글로 보이기
    const string pairText = ReflectValue::make( half2::fromFloat2( float2{ 0.25f, 0.5f } ) ).toText();
    SW_EXPECT_STREQ( "0.250000, 0.500000", pairText.c_str() );
    const string unitText = ReflectValue::make( unorm8::fromRaw( 255 ) ).toText();
    SW_EXPECT_STREQ( "1.000000", unitText.c_str() );
}

/**
 * @brief [PackedValueSerializationTest] 쉼표 목록 글은 칸이 정확히 맞아야 읽히고, 거절하면 값을 그대로 둔다 — 벡터 · 쿼터니언 · 행렬도 같다
 * @details `float3` 가 `1,2,3,4` 를 받아 네 번째 칸을 버리면 잘못 적은 데이터가 말없이 읽힌다.
 */
SW_TEST_CASE( PackedValueSerializationTest, NumberListRejectsExtraAndMissingCells )
{
    const SerializeContext context = SerializeContext::deriveFromDefault( SerializePurpose::Authoring );
    using Test                     = PackedSerializationTestInternal;

    const float3 original{ 7.0f, 8.0f, 9.0f };
    Test::expectTextRejected( context, "float3", original, "1,2,3,4" );
    Test::expectTextRejected( context, "float3", original, "1,2,3," );
    Test::expectTextRejected( context, "float3", original, "1,2" );
    Test::expectTextRejected( context, "float3", original, "1,2,x" ); // 앞 칸을 읽은 뒤 거절해도 값은 그대로다
    Test::expectTextRejected( context, "float2", float2{ 7.0f, 8.0f }, "1,2,3" );
    Test::expectTextRejected( context, "float4", float4{ 7.0f, 8.0f, 9.0f, 10.0f }, "1,2,3,4,5" );
    Test::expectTextRejected( context, "quaternion", quaternion{}, "0,0,0,1,1" );
    Test::expectTextRejected( context, "float4x4", float4x4{}, "1,0,0,0,0,1,0,0,0,0,1,0,0,0,0,1,1" );

    const SerializeContext::TextReadFn* pReadFloat3 = context.findTextReader( hashed_string( "float3" ) );
    SW_ASSERT_NOT_NULL( pReadFloat3 );
    float3 value{};
    SW_EXPECT_TRUE( ( *pReadFloat3 )( &value, "1, 2 ,3" ) );
    SW_EXPECT_EQUAL( 1.0f, value._x );
    SW_EXPECT_EQUAL( 2.0f, value._y );
    SW_EXPECT_EQUAL( 3.0f, value._z );
}

/**
 * @brief [PackedValueSerializationTest] 무한대 · NaN · int64 범위 밖의 실수는 정수 변환을 거치지 않고 거절된다
 * @details `static_cast<int64>` 는 범위 밖과 NaN 에서 정의되지 않는다 — 변환은 범위 · 유한 검사 뒤에만 한다.
 */
SW_TEST_CASE( PackedValueSerializationTest, FloatArgumentConversionRejectsNonFiniteAndOutOfRange )
{
    TaskValue          converted;
    const float64      infinity = std::numeric_limits<float64>::infinity();
    const float64      nan      = std::numeric_limits<float64>::quiet_NaN();
    const ReflectValue arrBad[] = { ReflectValue::make( infinity ), ReflectValue::make( -infinity ), ReflectValue::make( nan ),
                                    ReflectValue::make( std::numeric_limits<float32>::infinity() ), ReflectValue::make( std::numeric_limits<float32>::quiet_NaN() ) };
    for ( const ReflectValue& bad : arrBad )
    {
        SW_EXPECT_FALSE( ReflectTypeOpsOf<int32>::convert( bad, converted ) );
        SW_EXPECT_FALSE( ReflectTypeOpsOf<int64>::convert( bad, converted ) );
        SW_EXPECT_FALSE( ReflectTypeOpsOf<uint64>::convert( bad, converted ) );
        SW_EXPECT_FALSE( ReflectTypeOpsOf<half>::convert( bad, converted ) );
    }
    // int64 범위 밖의 유한값은 정수가 아닌 숫자로만 읽혀 정수 자리가 거절하고, 실수 자리는 받는다.
    SW_EXPECT_FALSE( ReflectTypeOpsOf<int64>::convert( ReflectValue::make( float64{ 1.0e300 } ), converted ) );
    SW_EXPECT_FALSE( ReflectTypeOpsOf<int64>::convert( ReflectValue::make( float64{ 9223372036854775808.0 } ), converted ) );
    SW_EXPECT_TRUE( ReflectTypeOpsOf<float64>::convert( ReflectValue::make( float32{ 1.0e30f } ), converted ) );
    // 범위 안의 정수 값은 그대로 받는다.
    SW_EXPECT_TRUE( ReflectTypeOpsOf<int32>::convert( ReflectValue::make( float64{ -42.0 } ), converted ) );
    SW_EXPECT_EQUAL( int32{ -42 }, converted.getValue<int32>() );
    SW_EXPECT_FALSE( ReflectTypeOpsOf<int32>::convert( ReflectValue::make( float64{ 0.5 } ), converted ) );
}

/**
 * @brief [PackedValueSerializationTest] 내장 타입의 갈래(숫자 · 글 · 불리언 · 그 밖)는 표에서 나오고, 압축 타입은 성분이 하나일 때만 숫자다
 * @details 스키마 이주와 UI 바인딩이 같은 갈래를 본다. 압축 타입을 이름 목록에 더하지 않아도 분류가 따라온다.
 */
SW_TEST_CASE( PackedValueSerializationTest, BuiltinClassFollowsTheTable )
{
    const auto classOf = []( const utf8* pName )
    { return ReflectValueUtil::getBuiltinClass( ReflectValueUtil::findBuiltinIndex( hashed_string( pName ) ) ); };
    for ( const utf8* pName : { "int8", "uint64", "float32", "float64", "half", "unorm8", "unorm16", "snorm8", "snorm16" } )
    {
        SW_EXPECT_TRUE_MSG( classOf( pName ) == ReflectBuiltinClass::Number, pName );
    }
    for ( const utf8* pName : { "half2", "half3", "half4", "unorm8x4", "snorm8x4", "unorm16x2", "snorm16x2", "snorm16x4", "OctahedralNormal", "RGB10A2", "float2", "float3",
                                "float4", "float4x4", "quaternion", "TagID", "ComponentHandle", "atomic_bool" } )
    {
        SW_EXPECT_TRUE_MSG( classOf( pName ) == ReflectBuiltinClass::Other, pName );
    }
    SW_EXPECT_TRUE( classOf( "bool" ) == ReflectBuiltinClass::Bool );
    SW_EXPECT_TRUE( classOf( "string" ) == ReflectBuiltinClass::Text );
    SW_EXPECT_TRUE( classOf( "hashed_string" ) == ReflectBuiltinClass::Text );
    SW_EXPECT_TRUE( ReflectValueUtil::getBuiltinClass( -1 ) == ReflectBuiltinClass::Other );

    // UI 바인딩의 종류도 같은 갈래에서 나온다 - 성분이 하나인 압축 타입은 숫자 바인딩이 된다.
    SW_EXPECT_TRUE( UIBindingValueUtil::classifyType( hashed_string( "unorm8" ) ) == UIBindingValueKind::Number );
    SW_EXPECT_TRUE( UIBindingValueUtil::classifyType( hashed_string( "half" ) ) == UIBindingValueKind::Number );
    SW_EXPECT_TRUE( UIBindingValueUtil::classifyType( hashed_string( "half2" ) ) == UIBindingValueKind::Other );
    SW_EXPECT_TRUE( UIBindingValueUtil::classifyType( hashed_string( "float32" ) ) == UIBindingValueKind::Number );
    SW_EXPECT_TRUE( UIBindingValueUtil::classifyType( hashed_string( "bool" ) ) == UIBindingValueKind::Bool );
    SW_EXPECT_TRUE( UIBindingValueUtil::classifyType( hashed_string( "string" ) ) == UIBindingValueKind::Text );
}

/**
 * @brief [PackedValueSerializationTest] 값 하나를 숫자로 읽는 길(`readNumber` · `readInteger`)은 내장 타입 표에서 나온다 — 범위 검사 · EditCondition · 탐침이 함께 쓴다
 * @details 예전에는 부르는 곳마다 타입 이름 글 사슬이라 float 이 빠지고 uint64 를 int64 로 읽고 압축 타입의 Min · Max 가 조용히 통과했다.
 */
SW_TEST_CASE( PackedValueSerializationTest, ReadNumberFollowsTheBuiltinTable )
{
    const auto indexOf = []( const utf8* pName )
    { return ReflectValueUtil::findBuiltinIndex( hashed_string( pName ) ); };
    float64 number{ 0.0 };
    int64   integer{ 0 };

    // 실수 · 큰 uint64 는 값 그대로 숫자다.
    const float32 floatValue = 2.5f;
    SW_EXPECT_TRUE( ReflectValueUtil::readNumber( indexOf( "float32" ), &floatValue, number ) );
    SW_EXPECT_EQUAL( 2.5, number );
    const uint64 bigValue = 1ull << 63;
    SW_EXPECT_TRUE( ReflectValueUtil::readNumber( indexOf( "uint64" ), &bigValue, number ) );
    SW_EXPECT_EQUAL( 9223372036854775808.0, number );

    // 성분이 하나인 압축 타입은 숫자다 - 범위 검사가 이제 이 값을 본다.
    const half halfValue = half::fromFloat( 0.75f );
    SW_EXPECT_TRUE( ReflectValueUtil::readNumber( indexOf( "half" ), &halfValue, number ) );
    SW_EXPECT_EQUAL( 0.75, number );
    const unorm8 unitValue = unorm8::fromRaw( 255 );
    SW_EXPECT_TRUE( ReflectValueUtil::readNumber( indexOf( "unorm8" ), &unitValue, number ) );
    SW_EXPECT_EQUAL( 1.0, number );

    // 숫자가 아닌 갈래(bool · 글 · 성분이 여럿인 압축 타입)와 표 밖 자리는 읽지 않고 값을 그대로 둔다.
    number                = -1.0;
    const bool   flag     = true;
    const string text     = "3";
    const half2  halfPair = half2::fromFloat2( float2{ 0.25f, 0.5f } );
    SW_EXPECT_FALSE( ReflectValueUtil::readNumber( indexOf( "bool" ), &flag, number ) );
    SW_EXPECT_FALSE( ReflectValueUtil::readNumber( indexOf( "string" ), &text, number ) );
    SW_EXPECT_FALSE( ReflectValueUtil::readNumber( indexOf( "half2" ), &halfPair, number ) );
    SW_EXPECT_FALSE( ReflectValueUtil::readNumber( -1, &floatValue, number ) );
    SW_EXPECT_FALSE( ReflectValueUtil::readNumber( indexOf( "float32" ), nullptr, number ) );
    SW_EXPECT_EQUAL( -1.0, number );

    // 정수판은 정수 · bool 만 읽는다. 실수 · 압축 타입은 소수부를 버리지 않게 거절한다.
    const int16 smallValue = -7;
    SW_EXPECT_TRUE( ReflectValueUtil::readInteger( indexOf( "int16" ), &smallValue, integer ) );
    SW_EXPECT_EQUAL( int64{ -7 }, integer );
    SW_EXPECT_TRUE( ReflectValueUtil::readInteger( indexOf( "bool" ), &flag, integer ) );
    SW_EXPECT_EQUAL( int64{ 1 }, integer );
    SW_EXPECT_FALSE( ReflectValueUtil::readInteger( indexOf( "float32" ), &floatValue, integer ) );
    SW_EXPECT_FALSE( ReflectValueUtil::readInteger( indexOf( "half" ), &halfValue, integer ) );

    // 글 갈래는 표의 서식으로 보인다(탐침 `Object.Property` 의 글).
    SW_EXPECT_STREQ( "3", ReflectValueUtil::formatBuiltin( indexOf( "string" ), &text ).c_str() );
    SW_EXPECT_TRUE( ReflectValueUtil::formatBuiltin( -1, &text ).empty() );
}
