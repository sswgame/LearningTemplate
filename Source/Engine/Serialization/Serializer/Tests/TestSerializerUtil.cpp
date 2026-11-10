#include "pch.h"

#include "Core/Name/hashed_string.h"
#include "Core/Serialization/LittleEndianUtil.h"
#include "Core/Testing/TestFramework.h"

#include "Engine/Reflection/ReflectionTypes.h"
#include "Engine/Serialization/Format/BinarySerializer.h"
#include "Engine/Serialization/Format/JSONSerializer.h"
#include "Engine/Serialization/Serializer/ContainerVisitor.h"
#include "Engine/Serialization/Serializer/DataPath.h"
#include "Engine/Serialization/Serializer/SchemaMigrate.h"
#include "Engine/Serialization/Serializer/SerializeContext.h"
#include "Engine/Serialization/Serializer/SerializerUtil.h"
#include "Engine/Serialization/Serializer/Tests/SerializerTestTypes.h"

// SerializerUtilTest · SchemaMigrateTest · DataPathTest — 직렬화 공통 도우미: 값 텍스트 왕복 · 범위 밖 정수 거절 · 키 철자 일치 · 모르는 이름 · 비트필드 한 칸만 쓰기 ·
// 페이로드 끝까지 읽기 · 선언 범위(경계 포함) · 선언 기본값, 판 정확 일치(옛 판은 이관하지 않고 거절) · orphan 정책(텍스트는 버리고 알림, 바이너리는 거절) ·
// 값으로만 옮기는 강제 변환(스칼라 · 다른 enum · 글 blob · 비트필드는 그 비트만) · 스크래치 인스턴스 · 기록 타입 이름 찾기, 데이터 경로 칸(읽지 않은 칸 · 앞뒤 공백 · 빈 글은 비움)(nogpu).

using namespace sw;

namespace
{
    /** @brief 샘플 타입입니다. 코드젠이 올린 타입이라 늘 있다. */
    const TypeInfo& getSampleType()
    {
        return *SerializerSample::StaticType();
    }

    /** @brief 샘플의 프로퍼티입니다. 이름이 틀리면 테스트 결함이라 멈춘다. */
    const PropertyInfo& getSampleProperty( const utf8* pName )
    {
        const PropertyInfo* pProp = getSampleType().findProperty( hashed_string( pName ) );
        if ( pProp == nullptr )
            SW_FATAL( "SerializerSample has no property '%#'", pName );
        return *pProp;
    }

    const SerializeContext& getContext()
    {
        return SerializeContext::getDefault( SerializePurpose::Authoring );
    }

    template <typename T>
    vector<uint8> toBytes( const T& value )
    {
        vector<uint8> bytes( sizeof( T ) );
        Memory::copy( bytes.data(), &value, sizeof( T ) );
        return bytes;
    }

    /** @brief 글 blob(`[길이 u32][바이트]`)입니다. */
    vector<uint8> makeTextBlob( string_view text )
    {
        vector<uint8> bytes;
        LittleEndianUtil::appendUint32( bytes, static_cast<uint32>( text.size() ) );
        bytes.insert( bytes.end(), text.begin(), text.end() );
        return bytes;
    }

    SchemaOrphanValue makeTextOrphan( const utf8* pWrittenName )
    {
        SchemaOrphanValue orphan;
        orphan._writtenName = pWrittenName;
        orphan._nameHash    = static_cast<uint32>( hashed_string::computeHash( string_view{ pWrittenName } ) );
        orphan._text        = "1";
        return orphan;
    }
} // namespace

/** @brief [SerializerUtilTest] 값 하나를 글로 쓰고 다시 읽으면 같은 값이고, 읽지 못하는 글은 값을 그대로 둔다 */
SW_TEST_CASE( SerializerUtilTest, ValueTextRoundTripsAndBadTextKeepsTheValue )
{
    int32 count = 42;
    SW_EXPECT_EQUAL( string( "42" ), SerializerUtil::formatValueText( ValueAddress{ &count }, hashed_string( "int32" ), getContext() ) );
    int32 parsed = 0;
    SW_REQUIRE_TRUE( SerializerUtil::parseTextValue( MutableValueAddress{ &parsed }, hashed_string( "int32" ), "-17", getContext() ) );
    SW_EXPECT_EQUAL( -17, parsed );
    SW_EXPECT_FALSE( SerializerUtil::parseTextValue( MutableValueAddress{ &parsed }, hashed_string( "int32" ), "abc", getContext() ) );
    SW_EXPECT_EQUAL( -17, parsed );

    float32 ratio = 0.0f;
    SW_REQUIRE_TRUE( SerializerUtil::parseTextValue( MutableValueAddress{ &ratio }, hashed_string( "float32" ), "0.25", getContext() ) );
    SW_EXPECT_NEAR( 0.25f, ratio, 1e-6 );
    string name;
    SW_REQUIRE_TRUE( SerializerUtil::parseTextValue( MutableValueAddress{ &name }, hashed_string( "string" ), "Orc Chief", getContext() ) );
    SW_EXPECT_EQUAL( string( "Orc Chief" ), name );
}

/** @brief [SerializerUtilTest] 경계 값: 좁은 정수 칸은 범위 끝까지 받고, 넘거나 음수인 글은 감지 않고 거절한다 */
SW_TEST_CASE( SerializerUtilTest, OutOfRangeIntegerTextIsRejectedNotWrapped )
{
    uint8 small = 9;
    SW_REQUIRE_TRUE( SerializerUtil::parseTextValue( MutableValueAddress{ &small }, hashed_string( "uint8" ), "255", getContext() ) );
    SW_EXPECT_EQUAL( uint8( 255 ), small );
    SW_REQUIRE_TRUE( SerializerUtil::parseTextValue( MutableValueAddress{ &small }, hashed_string( "uint8" ), "0", getContext() ) );
    SW_EXPECT_EQUAL( uint8( 0 ), small );
    SW_EXPECT_LOG( "Serialization.IntegerOutOfRange" );
    SW_EXPECT_FALSE( SerializerUtil::parseTextValue( MutableValueAddress{ &small }, hashed_string( "uint8" ), "256", getContext() ) );
    SW_EXPECT_FALSE( SerializerUtil::parseTextValue( MutableValueAddress{ &small }, hashed_string( "uint8" ), "-1", getContext() ) );
    SW_EXPECT_EQUAL( uint8( 0 ), small );
}

/** @brief [SerializerUtilTest] 키는 철자 그대로 맞춘다 — 대소문자가 다르면 다른 키이고, 모르는 이름 해시는 목록 끝(없음)이다 */
SW_TEST_CASE( SerializerUtilTest, KeysMatchExactlyAndUnknownNamesAreNotFound )
{
    SW_EXPECT_TRUE( SerializerUtil::keysEqual( "_count", "_count" ) );
    SW_EXPECT_FALSE( SerializerUtil::keysEqual( "_count", "_Count" ) );
    const vector<PropertyInfo>& listProp = getSampleType()._listProperty;
    const PropertyInfo*         pCount   = SerializerUtil::matchProperty( listProp, "_count" );
    SW_REQUIRE_NOT_NULL( pCount );
    SW_EXPECT_TRUE( pCount->_name == hashed_string( "_count" ) );
    SW_EXPECT_NULL( SerializerUtil::matchProperty( listProp, "_Count" ) );
    SW_EXPECT_NULL( SerializerUtil::matchProperty( listProp, "_missing" ) );
    const uint32 unknownHash = static_cast<uint32>( hashed_string::computeHash( string_view{ "_notAField" } ) );
    SW_EXPECT_FALSE( SerializerUtil::findPropertyIndexByNameHash( listProp, unknownHash ).has_value() );
    const optional<uint32> countIndex = SerializerUtil::findPropertyIndexByNameHash( listProp, hashed_string( "_count" ).getHash() );
    SW_REQUIRE_TRUE( countIndex.has_value() );
    SW_EXPECT_TRUE( listProp[*countIndex]._name == hashed_string( "_count" ) );
}

/** @brief [SerializerUtilTest] 비트필드 하나를 글로 쓰거나 옮겨도 같은 바이트의 이웃 비트는 그대로이고, 불리언이 아닌 글은 거절한다 */
SW_TEST_CASE( SerializerUtilTest, BitFieldHelpersTouchOnlyTheirOwnBit )
{
    const PropertyInfo& first  = getSampleProperty( "_bFirst" );
    const PropertyInfo& second = getSampleProperty( "_bSecond" );
    SerializerSample    sample;
    sample._bFirst = 1;
    SW_REQUIRE_TRUE( SerializerUtil::applyPropertyText( second, MutableObjectAddress{ &sample }, "true", getContext() ) );
    SW_EXPECT_EQUAL( 1, static_cast<int32>( sample._bFirst ) );
    SW_EXPECT_EQUAL( 1, static_cast<int32>( sample._bSecond ) );
    SW_EXPECT_FALSE( SerializerUtil::applyPropertyText( second, MutableObjectAddress{ &sample }, "maybe", getContext() ) );
    SW_EXPECT_EQUAL( 1, static_cast<int32>( sample._bSecond ) );
    SW_EXPECT_EQUAL( string( "true" ), SerializerUtil::formatPropertyText( first, ObjectAddress{ &sample }, getContext() ) );

    SerializerSample target;
    target._bSecond = 1;
    SW_EXPECT_FALSE( SerializerUtil::arePropertyValuesEqual( first, ObjectAddress{ &sample }, ObjectAddress{ &target }, getContext() ) );
    SW_REQUIRE_TRUE( SerializerUtil::copyPropertyValue( first, ObjectAddress{ &sample }, MutableObjectAddress{ &target }, getContext() ) );
    SW_EXPECT_TRUE( SerializerUtil::arePropertyValuesEqual( first, ObjectAddress{ &sample }, ObjectAddress{ &target }, getContext() ) );
    SW_EXPECT_EQUAL( 1, static_cast<int32>( target._bFirst ) );
    SW_EXPECT_EQUAL( 1, static_cast<int32>( target._bSecond ) ); // 이웃 비트는 옮기지 않았다

    SerializerSample cleared;
    cleared._bSecond = 1;
    SW_REQUIRE_TRUE( applyPropertyTextCoerced( first, MutableObjectAddress{ &cleared }, "1", getContext() ) );
    SW_EXPECT_EQUAL( 1, static_cast<int32>( cleared._bFirst ) );
    SW_EXPECT_EQUAL( 1, static_cast<int32>( cleared._bSecond ) );
}

/** @brief [SerializerUtilTest] 바이너리 실수 칸은 NaN · 무한대를 거절하고 값 · 위치를 그대로 둔다 — 텍스트 읽기 · `Archive` 와 같은 규칙이다 */
SW_TEST_CASE( SerializerUtilTest, BinaryFloatRefusesNonFinite )
{
    const SerializeContext::BinaryReadFn* pReadFloat  = getContext().findBinaryReader( hashed_string( "float32" ) );
    const SerializeContext::BinaryReadFn* pReadDouble = getContext().findBinaryReader( hashed_string( "float64" ) );
    SW_REQUIRE_NOT_NULL( pReadFloat );
    SW_REQUIRE_NOT_NULL( pReadDouble );

    const uint32 arrFloatBits[] = { 0x7FC00000u, 0x7F800000u, 0xFF800000u };
    for ( const uint32 bits : arrFloatBits )
    {
        vector<uint8> bytes;
        LittleEndianUtil::appendUint32( bytes, bits );
        float32 value  = 1.0f;
        size_t  offset = 0;
        SW_EXPECT_FALSE( ( *pReadFloat )( &value, bytes.data(), bytes.size(), offset ) );
        SW_EXPECT_EQUAL( 1.0f, value );
        SW_EXPECT_EQUAL( size_t{ 0 }, offset );
    }
    vector<uint8> nanDouble;
    LittleEndianUtil::appendUint64( nanDouble, 0x7FF8000000000000ull );
    float64 doubleValue = 1.0;
    size_t  offset      = 0;
    SW_EXPECT_FALSE( ( *pReadDouble )( &doubleValue, nanDouble.data(), nanDouble.size(), offset ) );
    SW_EXPECT_EQUAL( 1.0, doubleValue );

    vector<uint8> finite;
    LittleEndianUtil::appendFloat32( finite, 2.5f );
    float32 value = 0.0f;
    offset        = 0;
    SW_EXPECT_TRUE( ( *pReadFloat )( &value, finite.data(), finite.size(), offset ) );
    SW_EXPECT_EQUAL( 2.5f, value );
    SW_EXPECT_EQUAL( size_t{ 4 }, offset );
}

/** @brief [SerializerUtilTest] 프로퍼티 바이너리는 끝까지 읽어야 Read 다 — 남는 바이트 · 잘린 바이트는 이 필드만 실패이고 값은 그대로다 */
SW_TEST_CASE( SerializerUtilTest, PropertyBinaryMustBeReadToTheEnd )
{
    const PropertyInfo& nameProp = getSampleProperty( "_name" );
    SerializerSample    source;
    source._name = "payload";
    vector<uint8> bytes;
    SW_REQUIRE_TRUE( SerializerUtil::writePropertyBinary( nameProp, ObjectAddress{ &source }, bytes, getContext() ) );

    SerializerSample target;
    SW_EXPECT_TRUE( SerializerUtil::applyPropertyBinary( nameProp, MutableObjectAddress{ &target }, bytes.data(), bytes.size(), getContext() ) == FieldReadResult::Read );
    SW_EXPECT_EQUAL( string( "payload" ), target._name );

    SerializerSample trailing;
    vector<uint8>    trailingBytes = bytes;
    trailingBytes.push_back( 0 );
    SW_EXPECT_TRUE( SerializerUtil::applyPropertyBinary( nameProp, MutableObjectAddress{ &trailing }, trailingBytes.data(), trailingBytes.size(), getContext() ) ==
                    FieldReadResult::FieldFailed );
    SerializerSample truncated;
    truncated._name = "before";
    SW_EXPECT_TRUE( SerializerUtil::applyPropertyBinary( nameProp, MutableObjectAddress{ &truncated }, bytes.data(), bytes.size() - 1, getContext() ) ==
                    FieldReadResult::FieldFailed );
    SW_EXPECT_EQUAL( string( "before" ), truncated._name );

    SW_EXPECT_TRUE( SerializerUtil::hasBinaryValueCodec( hashed_string( "int32" ), getContext() ) );
    SW_EXPECT_FALSE( SerializerUtil::hasBinaryValueCodec( hashed_string( "SerializerUtilTest.NoSuchType" ), getContext() ) );
    vector<uint8> nothing;
    int32         value = 3;
    SW_EXPECT_FALSE( SerializerUtil::serializeValueBinary( ValueAddress{ &value }, hashed_string( "SerializerUtilTest.NoSuchType" ), nothing, getContext() ) );
    SW_EXPECT_TRUE( nothing.empty() ); // 실패한 쓰기는 0 바이트를 남기지 않는다
}

/** @brief [SerializerUtilTest] 선언 범위는 경계를 포함하고, 넘으면 칸마다 오류 한 줄과 함께 거절한다 */
SW_TEST_CASE( SerializerUtilTest, DeclaredRangeIsInclusiveAndViolationsAreListed )
{
    SerializerSample sample;
    sample._count = 0;
    SW_EXPECT_TRUE( SerializerUtil::isWithinDeclaredRanges( getSampleType(), ObjectAddress{ &sample } ) );
    sample._count = 100;
    SW_EXPECT_TRUE( SerializerUtil::isWithinDeclaredRanges( getSampleType(), ObjectAddress{ &sample } ) );

    sample._count = 101;
    vector<string> listError;
    SerializerUtil::collectRangeErrors( getSampleType(), ObjectAddress{ &sample }, "", listError );
    SW_REQUIRE_EQUAL( size_t( 1 ), listError.size() );
    SW_EXPECT_TRUE( listError[0].find( "_count" ) != string::npos );
    SW_EXPECT_LOG( "Serialization.ValueOutOfRange" );
    SW_EXPECT_FALSE( SerializerUtil::isWithinDeclaredRanges( getSampleType(), ObjectAddress{ &sample } ) );
}

/** @brief [SerializerUtilTest] 선언된 기본값(`Default=`)은 읽기가 그 칸을 채우지 않을 때 들어가고, 기본값이 없는 칸은 건드리지 않는다 */
SW_TEST_CASE( SerializerUtilTest, DeclaredDefaultIsAppliedOnlyWhereDeclared )
{
    SerializerSample sample;
    sample._count = 5;
    SerializerUtil::applyPropertyDefault( getSampleProperty( "_withDefault" ), MutableObjectAddress{ &sample } );
    SerializerUtil::applyPropertyDefault( getSampleProperty( "_count" ), MutableObjectAddress{ &sample } );
    SW_EXPECT_EQUAL( 7, sample._withDefault );
    SW_EXPECT_EQUAL( 5, sample._count );
}

/** @brief [SchemaMigrateTest] 판은 정확히 같아야 한다 — 옛 판(앞선 빌드의 데이터)은 이관하지 않고 거절하며, 오류에 텍스트 orphan 의 적힌 이름이 실린다 */
SW_TEST_CASE( SchemaMigrateTest, AnotherVersionIsRejectedWithoutMigration )
{
    const vector<SchemaOrphanValue> listOrphan{ makeTextOrphan( "_oldName" ) };
    SW_EXPECT_LOG( "Schema.VersionMismatch" );
    SW_EXPECT_FALSE( finishVersionedRead( 1, 2, getSampleType(), listOrphan, SchemaOrphanPolicy::Ignore ) );
    SW_EXPECT_FALSE( finishVersionedRead( 3, 2, getSampleType(), {}, SchemaOrphanPolicy::Ignore ) ); // 앞선 판도 받지 않는다
    SW_EXPECT_TRUE( finishVersionedRead( 2, 2, getSampleType(), {}, SchemaOrphanPolicy::Reject ) );
}

/** @brief [SchemaMigrateTest] 판이 같을 때 orphan 은 정책을 따른다 — 텍스트(Ignore)는 버리고 한 번 알리고, 바이너리(Reject)는 하나라도 있으면 거절한다 */
SW_TEST_CASE( SchemaMigrateTest, OrphanPolicyDropsWithAWarningOrRejects )
{
    const vector<SchemaOrphanValue> listOrphan{ makeTextOrphan( "_typo" ), makeTextOrphan( "_typo" ), makeTextOrphan( "_gone" ) };
    SW_EXPECT_LOG( "Schema.FieldsDropped" );
    SW_EXPECT_LOG( "Schema.OrphansRejected" );
    SW_EXPECT_TRUE( finishVersionedRead( 0, 0, getSampleType(), listOrphan, SchemaOrphanPolicy::Ignore ) );
    SW_EXPECT_FALSE( finishVersionedRead( 0, 0, getSampleType(), listOrphan, SchemaOrphanPolicy::Reject ) );
}

/** @brief [SchemaMigrateTest] JSON 읽기: 모르는 키와 틀린 형식의 값은 넘기고 아는 칸은 읽는다. 틀린 칸은 읽기 전 값으로 남고, 옛 판 문서는 거절한다 */
SW_TEST_CASE( SchemaMigrateTest, JSONReadSkipsUnknownKeysAndWrongTypesAndRejectsAnotherVersion )
{
    SerializerSample sample;
    sample._ratio  = 0.5f;
    uint32 version = 0;
    SW_EXPECT_LOG( "Schema.FieldsDropped" );
    SW_REQUIRE_TRUE( JSONSerializer::deserializeVersioned( version, &sample, getSampleType(), R"({"_schemaVersion":3,"_count":12,"_unknownKey":1,"_ratio":"abc"})", 3 ) );
    SW_EXPECT_EQUAL( uint32( 3 ), version );
    SW_EXPECT_EQUAL( 12, sample._count );
    SW_EXPECT_NEAR( 0.5f, sample._ratio, 1e-6 ); // 읽지 못한 칸은 읽기 전 값

    SerializerSample old;
    SW_EXPECT_LOG( "Schema.VersionMismatch" );
    SW_EXPECT_FALSE( JSONSerializer::deserializeVersioned( version, &old, getSampleType(), R"({"_schemaVersion":2,"_count":12})", 3 ) );
    SW_EXPECT_EQUAL( uint32( 2 ), version );
}

/** @brief [SchemaMigrateTest] 바이너리 읽기는 같은 빌드의 산출물만 받는다 — 판이 다르면 거절하고, 같으면 왕복한다 */
SW_TEST_CASE( SchemaMigrateTest, BinaryVersionedReadAcceptsOnlyTheSameVersion )
{
    SerializerSample source;
    source._count = 33;
    source._name  = "bin";
    vector<uint8> bytes;
    SW_REQUIRE_TRUE( BinarySerializer::serializeVersioned( 5, &source, getSampleType(), bytes, getContext() ) );
    SerializerSample target;
    uint32           version = 0;
    SW_REQUIRE_TRUE( BinarySerializer::deserializeVersioned( version, &target, getSampleType(), bytes.data(), bytes.size(), 5 ) );
    SW_EXPECT_EQUAL( 33, target._count );
    SW_EXPECT_EQUAL( string( "bin" ), target._name );

    SerializerSample other;
    SW_EXPECT_LOG( "Schema.VersionMismatch" );
    SW_EXPECT_FALSE( BinarySerializer::deserializeVersioned( version, &other, getSampleType(), bytes.data(), bytes.size(), 6 ) );
    SW_EXPECT_EQUAL( 0, other._count );
}

/** @brief [SchemaMigrateTest] 값으로만 옮기는 쌍: 기록 타입을 아는 스칼라 → 다른 스칼라 · 글, 다른 enum → 이름. 같은 타입 · 모르는 기록 타입 · 글 → 수는 아니다 */
SW_TEST_CASE( SchemaMigrateTest, ValueOnlyCoercionPairs )
{
    const hashed_string modeType = getSampleProperty( "_mode" )._typeName;
    SW_EXPECT_TRUE( isValueOnlyCoercion( hashed_string( "int32" ), hashed_string( "float32" ) ) );
    SW_EXPECT_TRUE( isValueOnlyCoercion( hashed_string( "string" ), hashed_string( "int32" ) ) );
    SW_EXPECT_TRUE( isValueOnlyCoercion( hashed_string( "string" ), modeType ) );
    SW_EXPECT_FALSE( isValueOnlyCoercion( hashed_string( "int32" ), hashed_string( "int32" ) ) );
    SW_EXPECT_FALSE( isValueOnlyCoercion( hashed_string( "int32" ), hashed_string{} ) );
    SW_EXPECT_FALSE( isValueOnlyCoercion( hashed_string( "int32" ), hashed_string( "string" ) ) );
    SW_EXPECT_FALSE( isValueOnlyCoercion( modeType, modeType ) );
}

/** @brief [SchemaMigrateTest] 형식이 바뀐 칸: 수는 값으로 옮기고(비트 재해석 없음), 값이 맞지 않거나 크기가 틀린 페이로드는 실패이며 대상은 그대로다 */
SW_TEST_CASE( SchemaMigrateTest, CoercedBinaryPayloadMovesTheValue )
{
    const SerializeContext& context = getContext();

    const vector<uint8> hundred = toBytes( int32( 100 ) );
    float32             asFloat = 0.0f;
    SW_REQUIRE_TRUE( tryCoerceBinaryPayload( MutableValueAddress{ &asFloat }, hashed_string( "float32" ), hundred.data(), hundred.size(), context, hashed_string( "int32" ) ) ==
                     FieldReadResult::Read );
    SW_EXPECT_NEAR( 100.0f, asFloat, 1e-6 );

    const vector<uint8> oneAndHalf = toBytes( 1.5f );
    string              asText;
    SW_REQUIRE_TRUE( tryCoerceBinaryPayload( MutableValueAddress{ &asText }, hashed_string( "string" ), oneAndHalf.data(), oneAndHalf.size(), context, hashed_string( "float32" ) ) ==
                     FieldReadResult::Read );
    float32 reread = 0.0f;
    SW_REQUIRE_TRUE( SerializerUtil::parseTextValue( MutableValueAddress{ &reread }, hashed_string( "float32" ), asText, context ) );
    SW_EXPECT_NEAR( 1.5f, reread, 1e-6 ); // 비트값(1069547520)이 아니라 수

    int32 asInt = -9;
    SW_EXPECT_TRUE( tryCoerceBinaryPayload( MutableValueAddress{ &asInt }, hashed_string( "int32" ), oneAndHalf.data(), oneAndHalf.size(), context, hashed_string( "float32" ) ) ==
                    FieldReadResult::FieldFailed );
    SW_EXPECT_TRUE( tryCoerceBinaryPayload( MutableValueAddress{ &asInt }, hashed_string( "int32" ), hundred.data(), 3, context, hashed_string( "float64" ) ) ==
                    FieldReadResult::FieldFailed ); // 기록 타입과 크기가 맞지 않는 손상
    SW_EXPECT_EQUAL( -9, asInt );

    const vector<uint8> textBlob = makeTextBlob( "42" );
    SW_REQUIRE_TRUE( tryCoerceBinaryPayload( MutableValueAddress{ &asInt }, hashed_string( "int32" ), textBlob.data(), textBlob.size(), context ) == FieldReadResult::Read );
    SW_EXPECT_EQUAL( 42, asInt );
    SW_EXPECT_TRUE( tryCoerceBinaryPayload( MutableValueAddress{ &asInt }, hashed_string( "int32" ), nullptr, 0, context ) == FieldReadResult::FieldFailed );
}

/** @brief [SchemaMigrateTest] 형식이 바뀐 비트필드 칸은 그 비트만 쓴다 — 같은 바이트의 이웃 비트가 지워지지 않고, 옮기지 못하면 비트도 그대로다 */
SW_TEST_CASE( SchemaMigrateTest, CoercedBitFieldWritesOnlyItsBit )
{
    const PropertyInfo& second = getSampleProperty( "_bSecond" );
    SerializerSample    sample;
    sample._bFirst = 1;

    const vector<uint8> one = toBytes( int32( 1 ) );
    SW_REQUIRE_TRUE( tryCoercePropertyBinary( second, MutableObjectAddress{ &sample }, one.data(), one.size(), getContext(), hashed_string( "int32" ), kCurrentBinaryWireVersion ) ==
                     FieldReadResult::Read );
    SW_EXPECT_EQUAL( 1, static_cast<int32>( sample._bFirst ) );
    SW_EXPECT_EQUAL( 1, static_cast<int32>( sample._bSecond ) );

    const vector<uint8> textBlob = makeTextBlob( "maybe" );
    SW_EXPECT_TRUE( tryCoercePropertyBinary( second, MutableObjectAddress{ &sample }, textBlob.data(), textBlob.size(), getContext(), hashed_string( "string" ),
                                             kCurrentBinaryWireVersion ) == FieldReadResult::FieldFailed );
    SW_EXPECT_EQUAL( 1, static_cast<int32>( sample._bFirst ) );
    SW_EXPECT_EQUAL( 1, static_cast<int32>( sample._bSecond ) );
}

/** @brief [SchemaMigrateTest] 스크래치 인스턴스는 기본 생성된 값으로 서고(글 칸은 빈 글), 짝을 맞춰 파괴된다 */
SW_TEST_CASE( SchemaMigrateTest, ScratchInstanceStartsDefaultAndIsDestroyed )
{
    vector<uint8> listStorage;
    void*         pScratch = createScratchInstance( getSampleType(), listStorage );
    SW_REQUIRE_NOT_NULL( pScratch );
    const SerializerSample& scratch = *static_cast<const SerializerSample*>( pScratch );
    SW_EXPECT_TRUE( scratch._name.empty() );
    SW_EXPECT_EQUAL( 0, static_cast<int32>( scratch._bFirst ) );
    destroyScratchInstance( pScratch, getSampleType() );
}

/** @brief [SchemaMigrateTest] 다른 enum 으로 적힌 값은 열거자 이름으로 옮긴다 — 글 칸은 이름을 받고, 이름 해시가 수로 읽히지 않는다 */
SW_TEST_CASE( SchemaMigrateTest, EnumOfAnotherTypeMovesByEnumeratorName )
{
    const hashed_string        modeType = getSampleProperty( "_mode" )._typeName;
    const SerializerSampleMode fast     = SerializerSampleMode::Fast;
    vector<uint8>              payload;
    SW_REQUIRE_TRUE( SerializerUtil::serializeValueBinary( ValueAddress{ &fast }, modeType, payload, getContext() ) );
    string asText;
    SW_REQUIRE_TRUE( tryCoerceBinaryPayload( MutableValueAddress{ &asText }, hashed_string( "string" ), payload.data(), payload.size(), getContext(), modeType ) ==
                     FieldReadResult::Read );
    SW_EXPECT_TRUE( asText.find( "Fast" ) != string::npos );
    int32 asInt = 4;
    SW_EXPECT_TRUE( tryCoerceBinaryPayload( MutableValueAddress{ &asInt }, hashed_string( "int32" ), payload.data(), payload.size(), getContext(), modeType ) ==
                    FieldReadResult::FieldFailed );
    SW_EXPECT_EQUAL( 4, asInt );
}

/** @brief [SchemaMigrateTest] 글 강제 변환: 따옴표 친 수 · 수로 적힌 글은 옮기고, 수가 아닌 글은 수 칸에 들어가지 않는다 */
SW_TEST_CASE( SchemaMigrateTest, TextCoercionStripsQuotesAndRejectsNonNumbers )
{
    int32 number = 0;
    SW_REQUIRE_TRUE( parseTextValueCoerced( MutableValueAddress{ &number }, hashed_string( "int32" ), "\"7\"", getContext() ) );
    SW_EXPECT_EQUAL( 7, number );
    SW_EXPECT_FALSE( parseTextValueCoerced( MutableValueAddress{ &number }, hashed_string( "int32" ), "abc", getContext() ) );
    SW_EXPECT_EQUAL( 7, number );
    string text;
    SW_REQUIRE_TRUE( parseTextValueCoerced( MutableValueAddress{ &text }, hashed_string( "string" ), "123", getContext() ) );
    SW_EXPECT_EQUAL( string( "123" ), text );
    SW_EXPECT_FALSE( parseTextValueCoerced( MutableValueAddress{ nullptr }, hashed_string( "int32" ), "1", getContext() ) );
}

/** @brief [SchemaMigrateTest] 기록 타입 해시는 등록 타입 · enum 이름으로 바뀌고, 모르는 해시는 빈 이름이다(크기로 짐작하게 둔다) */
SW_TEST_CASE( SchemaMigrateTest, WireTypeNameIsFoundOrEmpty )
{
    const hashed_string modeType = getSampleProperty( "_mode" )._typeName;
    SW_EXPECT_TRUE( findWireTypeName( static_cast<uint32>( hashed_string( "int32" ).getHash() ) ) == hashed_string( "int32" ) );
    SW_EXPECT_FALSE( findWireTypeName( static_cast<uint32>( modeType.getHash() ) ).empty() );
    SW_EXPECT_TRUE( findWireTypeName( static_cast<uint32>( hashed_string::computeHash( string_view{ "SchemaMigrateTest.NoSuchType" } ) ) ).empty() );
}

/** @brief [DataPathTest] 만든 직후의 칸은 읽지 않음이고, 데이터 읽기가 앞뒤 공백을 뺀 경로를 넣으며, 빈 글은 칸을 비운다 */
SW_TEST_CASE( DataPathTest, TextHandlerTrimsAndEmptyTextClears )
{
    const hashed_string typeName( "DataPath" );
    DataPath            path;
    SW_EXPECT_FALSE( path.isSet() );
    SW_EXPECT_TRUE( path == "" );
    SW_REQUIRE_TRUE( SerializerUtil::parseTextValue( MutableValueAddress{ &path }, typeName, "  engine/shaders/toon.hlsl \t", getContext() ) );
    SW_REQUIRE_TRUE( path.isSet() );
    SW_EXPECT_EQUAL( string( "engine/shaders/toon.hlsl" ), path.get() );
    SW_EXPECT_EQUAL( string( "engine/shaders/toon.hlsl" ), SerializerUtil::formatValueText( ValueAddress{ &path }, typeName, getContext() ) );
    SW_REQUIRE_TRUE( SerializerUtil::parseTextValue( MutableValueAddress{ &path }, typeName, "   ", getContext() ) );
    SW_EXPECT_FALSE( path.isSet() );
}

/** @brief [DataPathTest] 데이터 경로 칸은 세 형식(JSON · 바이너리)으로 왕복하고, 문서에 없으면 읽지 않은 칸으로 남는다 */
SW_TEST_CASE( DataPathTest, RoundTripsAndStaysUnsetWhenAbsent )
{
    SerializerSample source;
    SW_REQUIRE_TRUE( SerializerUtil::applyPropertyText( getSampleProperty( "_path" ), MutableObjectAddress{ &source }, "common/materials/a.material", getContext() ) );
    SerializerSample fromJSON;
    SW_REQUIRE_TRUE( JSONSerializer::deserialize( &fromJSON, getSampleType(), JSONSerializer::serialize( &source, getSampleType() ) ) );
    SW_EXPECT_TRUE( fromJSON._path == "common/materials/a.material" );

    vector<uint8> bytes;
    SW_REQUIRE_TRUE( BinarySerializer::serialize( &source, getSampleType(), bytes ) );
    SerializerSample fromBinary;
    SW_REQUIRE_TRUE( BinarySerializer::deserialize( &fromBinary, getSampleType(), bytes.data(), bytes.size() ) );
    SW_EXPECT_TRUE( fromBinary._path == "common/materials/a.material" );

    SerializerSample absent;
    SW_REQUIRE_TRUE( JSONSerializer::deserialize( &absent, getSampleType(), R"({"_count":1})" ) );
    SW_EXPECT_FALSE( absent._path.isSet() );
}
