#include "pch.h"

#include "Core/Serialization/LittleEndianUtil.h"
#include "Core/String/Name/hashed_string.h"
#include "Core/String/StringBuilder.h"
#include "Core/Testing/TestFramework.h"

#include "Engine/Content/AssetRegistry.h"
#include "Engine/Reflection/ReflectValueBinding.h"
#include "Engine/Reflection/ReflectedValue.h"
#include "Engine/Reflection/TypeInfo.h"
#include "Engine/Reflection/TypeRegistry.h"
#include "Engine/Serialization/Format/Archive.h"
#include "Engine/Serialization/Format/BinarySerializer.h"
#include "Engine/Serialization/Format/JSONSerializer.h"
#include "Engine/Serialization/Format/XMLSerializer.h"
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
            reportFatalError( "SerializerSample has no property '%#'", pName );
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

/** @brief [SerializerUtilTest] 큰따옴표로 감싼 글 값(JSON 글 `"BC1"`)도 쓰고 읽으면 같은 값이다 — 읽기가 따옴표 한 쌍을 벗기니 쓰기가 한 쌍을 더 씌운다 */
SW_TEST_CASE( SerializerUtilTest, QuotedStringValueRoundTrips )
{
    const string quoted = "\"BC1\"";
    const string text   = SerializerUtil::formatValueText( ValueAddress{ &quoted }, hashed_string( "string" ), getContext() );
    string       parsed;
    SW_REQUIRE_TRUE( SerializerUtil::parseTextValue( MutableValueAddress{ &parsed }, hashed_string( "string" ), text, getContext() ) );
    SW_EXPECT_EQUAL( quoted, parsed );
    const string plain = "Orc Chief";
    SW_EXPECT_EQUAL( plain, SerializerUtil::formatValueText( ValueAddress{ &plain }, hashed_string( "string" ), getContext() ) );
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
    SW_EXPECT_TRUE( SerializerUtil::isKeyEqual( "_count", "_count" ) );
    SW_EXPECT_FALSE( SerializerUtil::isKeyEqual( "_count", "_Count" ) );
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

/** @brief [SerializerUtilTest] 값 비교는 형식대로 견준다 — 숫자 · 글(철자까지) · 열거형 · 처리기 값 · 구조체 안의 칸 · 개수가 같은 목록의 원소가 다르면 다르다 */
SW_TEST_CASE( SerializerUtilTest, PropertyValuesCompareByType )
{
    const auto isEqualField = []( const utf8* pName, const SerializerSample& a, const SerializerSample& b )
    { return SerializerUtil::arePropertyValuesEqual( getSampleProperty( pName ), ObjectAddress{ &a }, ObjectAddress{ &b }, getContext() ); };
    const SerializerSample base;
    SerializerSample       changed;
    for ( const utf8* pName : { "_count", "_ratio", "_name", "_small", "_mode", "_bFirst", "_path" } )
    {
        SW_EXPECT_TRUE_MSG( isEqualField( pName, base, changed ), pName );
    }
    changed._count  = 1;
    changed._ratio  = 0.5f;
    changed._name   = "a";
    changed._small  = 2;
    changed._mode   = SerializerSampleMode::Fast;
    changed._bFirst = 1;
    SW_REQUIRE_TRUE( SerializerUtil::applyPropertyText( getSampleProperty( "_path" ), MutableObjectAddress{ &changed }, "Game/A.xml", getContext() ) );
    for ( const utf8* pName : { "_count", "_ratio", "_name", "_small", "_mode", "_bFirst", "_path" } )
    {
        SW_EXPECT_FALSE_MSG( isEqualField( pName, base, changed ), pName );
    }
    SerializerSample upper = changed;
    upper._name            = "A";
    SW_EXPECT_FALSE( isEqualField( "_name", changed, upper ) );

    const TypeInfo&     groupType = *SerializerSampleGroup::StaticType();
    const PropertyInfo* pList     = groupType.findProperty( hashed_string( "_listText" ) );
    const PropertyInfo* pPart     = groupType.findProperty( hashed_string( "_part" ) );
    SW_REQUIRE_NOT_NULL( pList );
    SW_REQUIRE_NOT_NULL( pPart );
    SerializerSampleGroup left;
    left._listText = { "a", "b" };
    SerializerSampleGroup right{ left };
    SW_EXPECT_TRUE( SerializerUtil::arePropertyValuesEqual( *pList, ObjectAddress{ &left }, ObjectAddress{ &right }, getContext() ) );
    SW_EXPECT_TRUE( SerializerUtil::arePropertyValuesEqual( *pPart, ObjectAddress{ &left }, ObjectAddress{ &right }, getContext() ) );
    right._listText[1] = "c"; // 개수는 같다
    right._part._extra = 2.0f;
    SW_EXPECT_FALSE( SerializerUtil::arePropertyValuesEqual( *pList, ObjectAddress{ &left }, ObjectAddress{ &right }, getContext() ) );
    SW_EXPECT_FALSE( SerializerUtil::arePropertyValuesEqual( *pPart, ObjectAddress{ &left }, ObjectAddress{ &right }, getContext() ) );
    right._listText = { "a" };
    SW_EXPECT_FALSE( SerializerUtil::arePropertyValuesEqual( *pList, ObjectAddress{ &left }, ObjectAddress{ &right }, getContext() ) );
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
    sample._ratio = 0.5f;
    SW_EXPECT_LOG( "Schema.FieldsDropped" );
    SW_REQUIRE_TRUE( JSONSerializer::deserializeVersioned( &sample, getSampleType(), R"({"_schemaVersion":3,"_count":12,"_unknownKey":1,"_ratio":"abc"})", 3 ) );
    SW_EXPECT_EQUAL( 12, sample._count );
    SW_EXPECT_NEAR( 0.5f, sample._ratio, 1e-6 ); // 읽지 못한 칸은 읽기 전 값

    SerializerSample old;
    SW_EXPECT_LOG( "Schema.VersionMismatch" );
    SW_EXPECT_FALSE( JSONSerializer::deserializeVersioned( &old, getSampleType(), R"({"_schemaVersion":2,"_count":12})", 3 ) );
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
    SW_REQUIRE_TRUE( BinarySerializer::deserializeVersioned( &target, getSampleType(), bytes.data(), bytes.size(), 5 ) );
    SW_EXPECT_EQUAL( 33, target._count );
    SW_EXPECT_EQUAL( string( "bin" ), target._name );

    SerializerSample other;
    SW_EXPECT_LOG( "Schema.VersionMismatch" );
    SW_EXPECT_FALSE( BinarySerializer::deserializeVersioned( &other, getSampleType(), bytes.data(), bytes.size(), 6 ) );
    SW_EXPECT_EQUAL( 0, other._count );
}

/** @brief [SchemaMigrateTest] 아카이브의 판 읽기는 판이 다르면 대상을 건드리지 않는다 — 읽은 판을 출력 인자로 받지 않고, 거절은 본문을 읽기 전이다 */
SW_TEST_CASE( SchemaMigrateTest, ArchiveVersionedReadLeavesTheTargetOnVersionMismatch )
{
    SerializerSample source;
    source._count = 33;
    source._name  = "bin";
    Archive written;
    SW_REQUIRE_TRUE( written.serializeVersionedObject( 5, &source, getSampleType() ) );

    SerializerSample target;
    target._count = 7;
    target._name  = "kept";
    Archive mismatched( written.getData(), written.getSize() );
    SW_EXPECT_LOG( "Schema.VersionMismatch" );
    SW_EXPECT_FALSE( mismatched.deserializeVersionedObject( &target, getSampleType(), 6 ) );
    SW_EXPECT_EQUAL( 7, target._count );
    SW_EXPECT_EQUAL( string( "kept" ), target._name );

    Archive matched( written.getData(), written.getSize() );
    SW_REQUIRE_TRUE( matched.deserializeVersionedObject( &target, getSampleType(), 5 ) );
    SW_EXPECT_EQUAL( 33, target._count );
    SW_EXPECT_EQUAL( string( "bin" ), target._name );
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

/** @brief [BinarySerializerTest] 쿠킹 배치 스트림은 태그 스트림과 같은 값을 읽고 더 작으며, 이어 쓴 Archive 에서 끝을 알고, 켜지 않은 읽기와 배치가 다른 읽기는 하나도 쓰지 않고 거절한다 */
SW_TEST_CASE( BinarySerializerTest, SchemaLayoutStreamMatchesTheTaggedValues )
{
    SerializerSampleGroup source;
    source._listText    = { "alpha", "beta" };
    source._part._count = 11;
    source._part._name  = "part";
    source._part._extra = 2.25f;
    source._count       = 42;

    const TypeInfo&  type        = *SerializerSampleGroup::StaticType();
    SerializeContext cookContext = SerializeContext::deriveFromDefault( SerializePurpose::Authoring );
    cookContext.setSchemaLayout( true );

    vector<uint8> taggedBytes;
    vector<uint8> layoutBytes;
    SW_REQUIRE_TRUE( BinarySerializer::serialize( &source, type, taggedBytes ) );
    SW_REQUIRE_TRUE( BinarySerializer::serialize( &source, type, layoutBytes, cookContext ) );
    SW_EXPECT_TRUE( layoutBytes.size() < taggedBytes.size() );

    const auto expectSame = [&source]( const SerializerSampleGroup& read )
    {
        SW_EXPECT_TRUE( read._listText == source._listText );
        SW_EXPECT_EQUAL( source._part._count, read._part._count );
        SW_EXPECT_EQUAL( source._part._name, read._part._name );
        SW_EXPECT_EQUAL( source._part._extra, read._part._extra );
        SW_EXPECT_EQUAL( source._count, read._count );
    };
    // 켠 문맥은 머리로 두 꼴을 가린다. 소프트 읽기도 모르는 칸이 없다.
    SerializerSampleGroup strictRead;
    SW_REQUIRE_TRUE( BinarySerializer::deserialize( &strictRead, type, layoutBytes.data(), layoutBytes.size(), cookContext ) );
    expectSame( strictRead );
    SerializerSampleGroup     softRead;
    vector<SchemaOrphanValue> listOrphan;
    SW_EXPECT_TRUE( BinarySerializer::deserializeSoft( &softRead, type, layoutBytes.data(), layoutBytes.size(), &listOrphan, cookContext ) == SoftReadResult::Read );
    SW_EXPECT_TRUE( listOrphan.empty() );
    expectSame( softRead );

    // 한 Archive 에 배치 스트림과 태그 스트림을 이어 써도 스스로 끝을 안다.
    Archive archive;
    SW_REQUIRE_TRUE( BinarySerializer::serialize( &source, type, archive, cookContext ) );
    SW_REQUIRE_TRUE( BinarySerializer::serialize( &source, type, archive ) );
    Archive               reader( archive.getData(), archive.getSize() );
    SerializerSampleGroup first;
    SerializerSampleGroup second;
    SW_REQUIRE_TRUE( BinarySerializer::deserialize( &first, type, reader, cookContext ) );
    SW_REQUIRE_TRUE( BinarySerializer::deserialize( &second, type, reader, cookContext ) );
    expectSame( first );
    expectSame( second );

    // 켜지 않은 문맥(세이브 · 메시지 · 스냅샷)은 모르는 버전으로 거절한다 — 값을 하나도 쓰지 않는다.
    SerializerSampleGroup untouched;
    for ( const SerializePurpose purpose : { SerializePurpose::Authoring, SerializePurpose::Snapshot, SerializePurpose::SaveGame } )
    {
        SW_EXPECT_LOG( "BinarySerializer.BinaryStreamWireVersion" );
        SW_EXPECT_FALSE( BinarySerializer::deserialize( &untouched, type, layoutBytes.data(), layoutBytes.size(), SerializeContext::getDefault( purpose ) ) );
    }
    // 배치 해시가 다르면(다른 빌드의 쿠킹본) 값을 하나도 쓰지 않고 거절한다.
    vector<uint8> otherLayout = layoutBytes;
    otherLayout[sizeof( uint32 )] ^= 1;
    SW_EXPECT_LOG( "BinarySerializer.SchemaLayoutMismatch" );
    SW_EXPECT_FALSE( BinarySerializer::deserialize( &untouched, type, otherLayout.data(), otherLayout.size(), cookContext ) );
    listOrphan.clear();
    SW_EXPECT_TRUE( BinarySerializer::deserializeSoft( &untouched, type, otherLayout.data(), otherLayout.size(), &listOrphan, cookContext ) == SoftReadResult::Rejected );
    // 잘린 스트림은 읽지 않는다.
    SW_EXPECT_FALSE( BinarySerializer::deserialize( &untouched, type, layoutBytes.data(), 2, cookContext ) );
    SW_EXPECT_EQUAL( 0, untouched._count );
    SW_EXPECT_TRUE( untouched._listText.empty() );
    SW_EXPECT_FALSE( BinarySerializer::deserialize( &untouched, type, layoutBytes.data(), layoutBytes.size() - 1, cookContext ) );
}

/** @brief [BinarySerializerTest] 위조된 값 크기(32 비트 size_t 에서 자리와 더하면 넘치는 크기 포함)와 머리 개수가 틀린 배치 스트림은 읽지 않는다 — 값은 그대로다 */
SW_TEST_CASE( BinarySerializerTest, CorruptPayloadSizeAndCountAreRejected )
{
    SerializerSampleGroup source;
    source._listText = { "alpha" };
    source._count    = 42;

    const TypeInfo&  type        = *SerializerSampleGroup::StaticType();
    SerializeContext cookContext = SerializeContext::deriveFromDefault( SerializePurpose::Authoring );
    cookContext.setSchemaLayout( true );
    vector<uint8> taggedBytes;
    vector<uint8> layoutBytes;
    SW_REQUIRE_TRUE( BinarySerializer::serialize( &source, type, taggedBytes ) );
    SW_REQUIRE_TRUE( BinarySerializer::serialize( &source, type, layoutBytes, cookContext ) );

    const auto writeWordAt = []( vector<uint8>& inoutBytes, const size_t offset, const uint32 value )
    { Memory::copy( inoutBytes.data() + offset, &value, sizeof( value ) ); };
    const auto expectRejected = [&type]( const vector<uint8>& bytes, const SerializeContext& context )
    {
        SerializerSampleGroup     read;
        vector<SchemaOrphanValue> listOrphan;
        SW_EXPECT_FALSE( BinarySerializer::deserialize( &read, type, bytes.data(), bytes.size(), context ) );
        SW_EXPECT_TRUE( BinarySerializer::deserializeSoft( &read, type, bytes.data(), bytes.size(), &listOrphan, context ) == SoftReadResult::NotParsed );
        SW_EXPECT_TRUE( read._listText.empty() );
        SW_EXPECT_EQUAL( 0, read._count );
    };
    // 첫 값의 크기 칸 — 태그 스트림은 머리 · 이름 해시 · 형식 해시 뒤, 배치 스트림은 머리 · 배치 해시 뒤다.
    for ( const uint32 corruptSize : { 0xFFFFFFFFu, 0xFFFFFFFFu - 15u, static_cast<uint32>( taggedBytes.size() ) } )
    {
        vector<uint8> corruptTagged = taggedBytes;
        writeWordAt( corruptTagged, sizeof( uint32 ) * 3, corruptSize );
        expectRejected( corruptTagged, getContext() );
        vector<uint8> corruptLayout = layoutBytes;
        writeWordAt( corruptLayout, sizeof( uint32 ) * 2, corruptSize );
        expectRejected( corruptLayout, cookContext );
    }

    uint32 header = 0;
    Memory::copy( &header, layoutBytes.data(), sizeof( header ) );
    vector<uint8> corruptCount = layoutBytes;
    writeWordAt( corruptCount, 0, header + 1 );
    SerializerSampleGroup read;
    SW_EXPECT_FALSE( BinarySerializer::deserialize( &read, type, corruptCount.data(), corruptCount.size(), cookContext ) );
}

/** @brief [BinarySerializerTest] 배치 스트림이 담지 못하는 쓰기(모르는 칸을 다시 싣기 · 저작이 아닌 목적)는 오류로 멈춘다 */
SW_TEST_CASE( BinarySerializerTest, SchemaLayoutRejectsWhatItCannotHold )
{
    const SerializerSampleGroup source;
    const TypeInfo&             type        = *SerializerSampleGroup::StaticType();
    SerializeContext            cookContext = SerializeContext::deriveFromDefault( SerializePurpose::Authoring );
    cookContext.setSchemaLayout( true );
    SerializeContext saveContext = SerializeContext::deriveFromDefault( SerializePurpose::SaveGame );
    saveContext.setSchemaLayout( true );

    SchemaOrphanValue unknown;
    unknown._nameHash = hashed_string::computeHash( "_fieldOfAnotherBuild" );
    vector<uint8> bytes;
    SW_EXPECT_LOG( "BinarySerializer.SchemaLayoutCannotHold" );
    SW_EXPECT_FALSE( BinarySerializer::serializeKeepingUnknown( &source, type, vector<SchemaOrphanValue>{ unknown }, bytes, cookContext ) );
    SW_EXPECT_FALSE( BinarySerializer::serialize( &source, type, bytes, saveContext ) );
}

/** @brief [XMLSerializerPropertyTest] 되풀이된 원소와 자리가 틀린 칸(속성 값을 원소로, 원소 칸을 속성으로)은 모르는 칸으로 알린다 — 알림 없이 초기값이 되지 않는다 */
SW_TEST_CASE( XMLSerializerPropertyTest, RepeatedAndMisplacedPropertiesAreReported )
{
    const auto readOrphanCount = []( string_view text ) -> size_t
    {
        SerializerSampleGroup     group;
        vector<SchemaOrphanValue> listOrphan;
        SW_EXPECT_TRUE( XMLSerializer::deserializeSoft( &group, *SerializerSampleGroup::StaticType(), text, &listOrphan ) == SoftReadResult::Read );
        return listOrphan.size();
    };
    SW_EXPECT_EQUAL( size_t{ 0 }, readOrphanCount( R"(<SerializerSampleGroup _count="3"><_listText><item>a</item></_listText><_part _count="2" /></SerializerSampleGroup>)" ) );
    SW_EXPECT_EQUAL( size_t{ 1 }, readOrphanCount( R"(<SerializerSampleGroup><_listText><item>a</item></_listText><_listText><item>b</item></_listText></SerializerSampleGroup>)" ) ); // 둘째 목록은 읽지 않는다
    SW_EXPECT_EQUAL( size_t{ 1 }, readOrphanCount( R"(<SerializerSampleGroup><_part _count="1" /><_part _count="2" /></SerializerSampleGroup>)" ) );
    SW_EXPECT_EQUAL( size_t{ 1 }, readOrphanCount( R"(<SerializerSampleGroup><_count>5</_count></SerializerSampleGroup>)" ) );                // 속성이어야 할 값
    SW_EXPECT_EQUAL( size_t{ 1 }, readOrphanCount( R"(<SerializerSampleGroup _listText="a" />)" ) );                                          // 원소여야 할 값
    SW_EXPECT_EQUAL( size_t{ 1 }, readOrphanCount( R"(<SerializerSampleGroup><_part><_count>5</_count></_part></SerializerSampleGroup>)" ) ); // 안쪽 원소도 같다

    SerializerSampleGroup group;
    SW_EXPECT_FALSE( XMLSerializer::deserialize( &group, *SerializerSampleGroup::StaticType(), R"(<SerializerSampleGroup><_count>5</_count></SerializerSampleGroup>)" ) );
}

/** @brief [DataPathTest] 만든 직후의 칸은 읽지 않음이고, 데이터 읽기가 앞뒤 공백을 뺀 경로를 넣으며, 빈 글은 칸을 비운다 */
SW_TEST_CASE( DataPathTest, TextHandlerTrimsAndEmptyTextClears )
{
    const hashed_string typeName( "DataPath" );
    DataPath            path;
    SW_EXPECT_FALSE( path.isSet() );
    SW_EXPECT_TRUE( path == "" );
    SW_REQUIRE_TRUE( SerializerUtil::parseTextValue( MutableValueAddress{ &path }, typeName, "  engine/shaders/unindexed.hlsl \t", getContext() ) );
    SW_REQUIRE_TRUE( path.isSet() );
    SW_EXPECT_EQUAL( string( "engine/shaders/unindexed.hlsl" ), path.get() );
    SW_EXPECT_EQUAL( string( "engine/shaders/unindexed.hlsl" ), SerializerUtil::formatValueText( ValueAddress{ &path }, typeName, getContext() ) );
    SW_REQUIRE_TRUE( SerializerUtil::parseTextValue( MutableValueAddress{ &path }, typeName, "   ", getContext() ) );
    SW_EXPECT_FALSE( path.isSet() );
}

/** @brief [DataPathTest] 저장 꼴은 정체성이다 — GUID 글을 지금 경로로 읽고, 색인에 있는 경로 글도 GUID 로 쓴다. 색인에 없는 GUID 는 읽기 오류이고 값은 그대로다 */
SW_TEST_CASE( DataPathTest, ReferenceIsStoredAsAGUID )
{
    const hashed_string           typeName( "DataPath" );
    const AssetGUID               guid     = *AssetGUID::tryParse( "0000000000000000000000000000da7a" );
    const expected<AssetRegistry> registry = AssetRegistry::merge( vector<AssetRegistryLayer>{
        AssetRegistryLayer{ "one", { AssetRegistryEntry{ guid, "engine/shaders/moved/toon.hlsl" } } }
    } );
    SW_REQUIRE_TRUE( registry.has_value() );
    AssetRegistry previous = AssetRegistry::install( *registry );

    DataPath path;
    SW_EXPECT_TRUE( SerializerUtil::parseTextValue( MutableValueAddress{ &path }, typeName, "guid:0000000000000000000000000000da7a", getContext() ) );
    SW_EXPECT_TRUE( path == "engine/shaders/moved/toon.hlsl" );
    DataPath fromPath;
    SW_EXPECT_TRUE( SerializerUtil::parseTextValue( MutableValueAddress{ &fromPath }, typeName, "engine/shaders/moved/toon.hlsl", getContext() ) );
    SW_EXPECT_EQUAL( string( "guid:0000000000000000000000000000da7a" ), SerializerUtil::formatValueText( ValueAddress{ &fromPath }, typeName, getContext() ) );
    SW_EXPECT_LOG( "DataPath.ReferenceNotResolved" );
    SW_EXPECT_FALSE( SerializerUtil::parseTextValue( MutableValueAddress{ &path }, typeName, "guid:0000000000000000000000000000dead", getContext() ) );
    SW_EXPECT_TRUE( path == "engine/shaders/moved/toon.hlsl" );

    (void)AssetRegistry::install( std::move( previous ) );
}

/** @brief [DataPathTest] 데이터 경로 칸은 세 형식(JSON · 바이너리)으로 왕복하고, 문서에 없으면 읽지 않은 칸으로 남는다 */
SW_TEST_CASE( DataPathTest, RoundTripsAndStaysUnsetWhenAbsent )
{
    SerializerSample source;
    SW_REQUIRE_TRUE( SerializerUtil::applyPropertyText( getSampleProperty( "_path" ), MutableObjectAddress{ &source }, "common/materials/a.material", getContext() ) );
    SerializerSample fromJSON;
    SW_REQUIRE_TRUE( JSONSerializer::deserialize( &fromJSON, getSampleType(), JSONSerializer::serialize( &source, getSampleType() ).value() ) );
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

/**
 * @brief [SerializerUtilTest] 이름이 같은 칸끼리만 값이 옮겨 간다 — 형식이 달라도 이름 · 글 형식이 같으면 옮기고, 짝이 없는 칸은 양쪽 다 그대로다
 */
SW_TEST_CASE( SerializerUtilTest, SameNamedPropertiesMoveBetweenTypes )
{
    SerializerSample source;
    source._count = 42;
    source._name  = "knight";
    source._ratio = 0.25f;
    SerializerSamplePart target;
    SW_REQUIRE_TRUE( SerializerUtil::copySameNamedProperties( &source, getSampleType(), &target, *SerializerSamplePart::StaticType() ) );
    SW_EXPECT_EQUAL( 42, target._count );
    SW_EXPECT_EQUAL( string( "knight" ), target._name );
    SW_EXPECT_EQUAL( 1.5f, target._extra ); // 짝이 없는 칸은 그대로다
}

/**
 * @brief [SerializerUtilTest] 프로퍼티에 묶은 사실(`ReflectValueBinding`)은 이름으로 찾은 것과 같다 — 내장형은 색인, 열거형은 `EnumInfo`, 구조체는 `TypeInfo`
 * @details 직렬화기가 값마다 이름으로 처리기 · 열거형 · 구조체를 다시 찾던 길을 세대마다 한 번 찾는 묶음으로 바꿨다. 두 번째 조회는 캐시이고 답이 같다.
 */
SW_TEST_CASE( SerializerUtilTest, ValueBindingMatchesNameLookup )
{
    const TypeRegistry& registry = getContext().getTypeRegistry();
    const PropertyInfo& count    = getSampleProperty( "_count" );
    const PropertyInfo& mode     = getSampleProperty( "_mode" );
    SW_REQUIRE_TRUE( count.findValueBinding() != nullptr && mode.findValueBinding() != nullptr );

    const ReflectValueFacts countFacts = count.findValueBinding()->resolve( count._typeName, registry );
    SW_EXPECT_TRUE( countFacts._builtinIndex == ReflectBuiltinOf<int32>::kIndex );
    SW_EXPECT_TRUE( countFacts._pEnum == nullptr );

    const ReflectValueFacts modeFacts = mode.findValueBinding()->resolve( mode._typeName, registry );
    SW_EXPECT_TRUE( modeFacts._pEnum != nullptr && modeFacts._pEnum == registry.findEnum( mode._typeName ) );
    SW_EXPECT_TRUE( modeFacts._builtinIndex == ReflectBuiltinIndex::kNone );
    // 프로퍼티가 답하는 사실은 묶음의 것과 같고, 레지스트리를 따로 준 자리는 그 레지스트리에서 찾는다(묶음이 다른 레지스트리의 답을 담지 않는다).
    SW_EXPECT_TRUE( count.resolveValueFacts( registry )._builtinIndex == countFacts._builtinIndex );
    SW_EXPECT_TRUE( mode.resolveValueFacts( registry )._pEnum == modeFacts._pEnum );
    SW_EXPECT_TRUE( mode.resolveValueFacts( registry ).findStructType() == nullptr );
    const TypeRegistry emptyRegistry;
    SW_EXPECT_TRUE( mode.resolveValueFacts( emptyRegistry )._pEnum == nullptr );
    SW_EXPECT_TRUE( mode.resolveValueFacts( registry )._pEnum == modeFacts._pEnum );
    const ReflectValueFacts modeAgain = mode.findValueBinding()->resolve( mode._typeName, registry );
    SW_EXPECT_TRUE( modeAgain._pEnum == modeFacts._pEnum );

    // 묶음으로 쓴 바이트와 이름으로 쓴 바이트가 같고, 묶음으로 다시 읽힌다.
    const SerializerSampleMode fast = SerializerSampleMode::Fast;
    vector<uint8>              byName;
    vector<uint8>              byBinding;
    SW_REQUIRE_TRUE( SerializerUtil::serializeValueBinary( ValueAddress{ &fast }, mode._typeName, byName, getContext() ) );
    SW_REQUIRE_TRUE( SerializerUtil::serializeValueBinary( ValueAddress{ &fast }, mode._typeName, mode.findValueBinding(), byBinding, getContext() ) );
    SW_EXPECT_TRUE( byName == byBinding );
    SerializerSampleMode read   = SerializerSampleMode::Off;
    size_t               offset = 0;
    SW_EXPECT_TRUE( SerializerUtil::deserializeValueBinary( MutableValueAddress{ &read }, mode._typeName, mode.findValueBinding(), byBinding.data(), byBinding.size(), offset,
                                                            getContext(), kCurrentBinaryWireVersion ) == FieldReadResult::Read );
    SW_EXPECT_TRUE( read == SerializerSampleMode::Fast );
}

/** @brief [SerializerUtilTest] 내장형 처리기는 색인으로도 이름으로도 같은 것이 나오고, 빌린 문맥은 자기 등록이 빌린 표를 이긴다 */
SW_TEST_CASE( SerializerUtilTest, BuiltinHandlerIndexMatchesName )
{
    const ReflectBuiltinIndex int32Index = ReflectBuiltinOf<int32>::kIndex;
    SW_REQUIRE_TRUE( getContext().findBinaryWriter( int32Index ) != nullptr );
    SW_EXPECT_TRUE( getContext().findBinaryReader( int32Index ) != nullptr );
    SW_EXPECT_TRUE( getContext().findBinaryWriter( ReflectBuiltinIndex::kNone ) == nullptr );

    const int32   value = 0x01020304;
    vector<uint8> byIndex;
    vector<uint8> byName;
    ( *getContext().findBinaryWriter( int32Index ) )( &value, byIndex );
    ( *getContext().findBinaryWriter( hashed_string( "int32" ) ) )( &value, byName );
    SW_EXPECT_TRUE( byIndex == byName );

    SerializeContext derived = SerializeContext::deriveFromDefault( SerializePurpose::Authoring );
    derived.registerBinaryHandler(
        hashed_string( "int32" ), []( const void*, vector<uint8>& outBuffer )
    { outBuffer.push_back( 0xAB ); },
        []( void*, const uint8*, size_t, size_t& )
    { return false; } );
    vector<uint8> custom;
    ( *derived.findBinaryWriter( int32Index ) )( &value, custom );
    SW_EXPECT_EQUAL( static_cast<size_t>( 1 ), custom.size() );
    SW_EXPECT_TRUE( getContext().findBinaryWriter( int32Index ) != derived.findBinaryWriter( int32Index ) );
}

/** @brief [SerializerUtilTest] 이름 값 형식은 정규 이름으로 가른다 — 파서가 모은 `AssetRef` · `CatalogKey` 와 이름 · 경로 형식은 이름 값, 글(`string`)은 아니다 */
SW_TEST_CASE( SerializerUtilTest, NameValueTypeByCanonicalName )
{
    SW_EXPECT_TRUE( SerializerUtil::isNameValueType( hashed_string( "hashed_string" ) ) );
    SW_EXPECT_TRUE( SerializerUtil::isNameValueType( hashed_string( "AssetRef" ) ) );
    SW_EXPECT_TRUE( SerializerUtil::isNameValueType( hashed_string( "CatalogKey" ) ) );
    SW_EXPECT_TRUE( SerializerUtil::isNameValueType( hashed_string( "DataPath" ) ) );
    SW_EXPECT_FALSE( SerializerUtil::isNameValueType( hashed_string( "string" ) ) );
    SW_EXPECT_FALSE( SerializerUtil::isNameValueType( hashed_string( "int32" ) ) );
}

/** @brief [SerializerUtilTest] 태그 찾기는 기대 자리(직전 + 1)를 먼저 보고, 맞지 않으면 훑어 같은 답을 낸다 — 기대 자리가 범위 밖이어도 된다 */
SW_TEST_CASE( SerializerUtilTest, NameHashLookupTriesExpectedIndexFirst )
{
    const vector<PropertyInfo>& listProp  = getSampleType()._listProperty;
    const uint32                countHash = hashed_string( "_count" ).getHash();
    const optional<uint32>      scanned   = SerializerUtil::findPropertyIndexByNameHash( listProp, countHash );
    SW_REQUIRE_TRUE( scanned.has_value() );
    // 기대 자리가 맞을 때, 다른 자리일 때, 범위 밖일 때 모두 훑기와 같은 답이다.
    SW_EXPECT_TRUE( SerializerUtil::findPropertyIndexByNameHash( listProp, countHash, *scanned ) == scanned );
    SW_EXPECT_TRUE( SerializerUtil::findPropertyIndexByNameHash( listProp, countHash, *scanned + 1 ) == scanned );
    SW_EXPECT_TRUE( SerializerUtil::findPropertyIndexByNameHash( listProp, countHash, static_cast<uint32>( listProp.size() ) + 3 ) == scanned );
    const uint32 unknownHash = static_cast<uint32>( hashed_string::computeHash( string_view{ "_notAField" } ) );
    SW_EXPECT_FALSE( SerializerUtil::findPropertyIndexByNameHash( listProp, unknownHash, 0 ).has_value() );
}

/** @brief [SerializerUtilTest] 텍스트 길도 묶음을 쓴다 — 묶음으로 쓴 글이 이름으로 쓴 글과 같고, 묶음으로 다시 읽힌다(JSON · XML 이 같은 길을 탄다) */
SW_TEST_CASE( SerializerUtilTest, TextPathUsesValueBinding )
{
    const PropertyInfo& mode  = getSampleProperty( "_mode" );
    const PropertyInfo& count = getSampleProperty( "_count" );
    SW_REQUIRE_TRUE( mode.findValueBinding() != nullptr && count.findValueBinding() != nullptr );

    const SerializerSampleMode                fast = SerializerSampleMode::Fast;
    StringBuilder<constant::buffer::kMax8192> byName;
    StringBuilder<constant::buffer::kMax8192> byBinding;
    SerializerUtil::valueToText( byName, ValueAddress{ &fast }, mode._typeName, getContext() );
    SerializerUtil::valueToText( byBinding, ValueAddress{ &fast }, mode._typeName, mode.findValueBinding(), getContext() );
    SW_EXPECT_TRUE( byName.view() == byBinding.view() );

    SerializerSampleMode read = SerializerSampleMode::Off;
    SW_EXPECT_TRUE( SerializerUtil::parseTextValue( MutableValueAddress{ &read }, mode._typeName, mode.findValueBinding(), byBinding.view(), getContext() ) );
    SW_EXPECT_TRUE( read == SerializerSampleMode::Fast );

    int32 number = 0;
    SW_EXPECT_TRUE( SerializerUtil::parseTextValue( MutableValueAddress{ &number }, count._typeName, count.findValueBinding(), "42", getContext() ) );
    SW_EXPECT_EQUAL( 42, number );
}
