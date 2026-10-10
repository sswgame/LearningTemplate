#include "pch.h"

#include "Engine/Common/EngineServices.h"
#include "Engine/Reflection/PropertyEditCondition.h"
#include "Engine/Reflection/ReflectUnits.h"
#include "Engine/Reflection/ReflectionCore.h"
#include "Engine/Serialization/Format/BinarySerializer.h"
#include "Engine/Serialization/Format/JsonSerializer.h"
#include "Engine/Serialization/Format/XmlSerializer.h"

#include "ReflectionTest/TestSampleActor.h"

#include "TestFramework/TestFramework.h"

// 표시 메타 — EditCondition · 단위 · 슬라이더 범위 · HDR 색 · 여러 줄 · 파일 필터, 그리고 C 고정 배열 프로퍼티.

namespace
{
    const sw::TypeInfo& getDisplayType()
    {
        const sw::TypeInfo* pType = sw::DisplayMetaActor::StaticType();
        SW_ASSERT( pType != nullptr );
        return *pType;
    }

    const sw::PropertyInfo& getDisplayProperty( const sw::hashed_string& name )
    {
        const sw::PropertyInfo* pProp = getDisplayType().findPropertyInHierarchy( name );
        SW_ASSERT( pProp != nullptr );
        return *pProp;
    }
} // namespace

/**
 * @brief [ReflectionDisplayMetaTest] 단위는 같은 차원끼리 바뀌고, `"150 cm"` 같은 글을 저장 단위로 읽는다
 */
SW_TEST_CASE( ReflectionDisplayMetaTest, UnitsConvertWithinADimension )
{
    float64 value{ 0.0 };
    SW_ASSERT_TRUE( sw::ReflectUnitUtil::convert( 150.0, "cm", "m", value ) );
    SW_EXPECT_NEAR_EQUAL( 1.5, value, 1e-9 );
    SW_ASSERT_TRUE( sw::ReflectUnitUtil::convert( 180.0, "deg", "rad", value ) );
    SW_EXPECT_NEAR_EQUAL( 3.14159265358979, value, 1e-9 );
    SW_ASSERT_TRUE( sw::ReflectUnitUtil::convert( 36.0, "km/h", "m/s", value ) );
    SW_EXPECT_NEAR_EQUAL( 10.0, value, 1e-9 );
    SW_EXPECT_FALSE( sw::ReflectUnitUtil::convert( 1.0, "m", "s", value ) ); // 차원이 다르다
    SW_EXPECT_FALSE( sw::ReflectUnitUtil::convert( 1.0, "furlong", "m", value ) );

    SW_ASSERT_TRUE( sw::ReflectUnitUtil::parseValueInUnit( "2 km", "cm", value ) );
    SW_EXPECT_NEAR_EQUAL( 200000.0, value, 1e-6 );
    SW_ASSERT_TRUE( sw::ReflectUnitUtil::parseValueInUnit( "1.5e1m/s2", "m/s2", value ) );
    SW_EXPECT_NEAR_EQUAL( 15.0, value, 1e-9 );
    SW_ASSERT_TRUE( sw::ReflectUnitUtil::parseValueInUnit( " 42 ", "ms", value ) ); // 단위를 적지 않으면 이미 저장 단위다
    SW_EXPECT_NEAR_EQUAL( 42.0, value, 1e-9 );
    SW_EXPECT_FALSE( sw::ReflectUnitUtil::parseValueInUnit( "3 s", "m", value ) );
    SW_EXPECT_FALSE( sw::ReflectUnitUtil::parseValueInUnit( "cm", "m", value ) );
}

/**
 * @brief [ReflectionDisplayMetaTest] C 고정 배열 프로퍼티(`int32 _arrSlot[3]`)는 세 형식에서 왕복한다 — `std::array` 와 같은 고정 시퀀스다
 */
SW_TEST_CASE( ReflectionDisplayMetaTest, FixedCArrayRoundTripsInEveryFormat )
{
    const sw::PropertyInfo& slot = getDisplayProperty( "_arrSlot" );
    SW_ASSERT_TRUE( slot._bIsContainer == SW_TRUE );
    SW_ASSERT_NOT_NULL( slot._containerWrapper );
    SW_ASSERT_NOT_NULL( slot._containerWrapper->asSequence() );
    SW_EXPECT_TRUE( slot._containerWrapper->asSequence()->isFixedSize() );

    sw::DisplayMetaActor source;
    source._arrSlot[0]  = 7;
    source._arrSlot[1]  = 8;
    source._arrSlot[2]  = 9;
    source._arrPoint[1] = sw::float3( 1.0f, 2.0f, 3.0f );
    source._after       = 42;
    const auto isSame   = []( const sw::DisplayMetaActor& actor )
    {
        return actor._arrSlot[0] == 7 && actor._arrSlot[1] == 8 && actor._arrSlot[2] == 9 && actor._arrPoint[1]._z == 3.0f && actor._after == 42;
    };

    const sw::TypeInfo&  type = getDisplayType();
    const sw::string     xml  = sw::XmlSerializer::serialize( &source, type );
    sw::DisplayMetaActor fromXml;
    SW_EXPECT_TRUE_MSG( sw::XmlSerializer::deserialize( &fromXml, type, xml ), xml.c_str() );
    SW_EXPECT_TRUE_MSG( isSame( fromXml ), xml.c_str() );

    const sw::string     json = sw::JsonSerializer::serialize( &source, type );
    sw::DisplayMetaActor fromJson;
    SW_EXPECT_TRUE_MSG( sw::JsonSerializer::deserialize( &fromJson, type, json ), json.c_str() );
    SW_EXPECT_TRUE_MSG( isSame( fromJson ), json.c_str() );

    sw::vector<uint8> bytes;
    sw::BinarySerializer::serialize( &source, type, bytes );
    sw::DisplayMetaActor fromBinary;
    SW_EXPECT_TRUE( sw::BinarySerializer::deserialize( &fromBinary, type, bytes.data(), bytes.size() ) );
    SW_EXPECT_TRUE( isSame( fromBinary ) );
}

#if !defined( SW_SHIPPING )

/**
 * @brief [ReflectionDisplayMetaTest] 표시 애노테이션이 프로퍼티 메타에 닿는다 — 단위는 커스텀 메타 `Units` 로, 슬라이더 범위는 허용 범위와 따로
 */
SW_TEST_CASE( ReflectionDisplayMetaTest, AnnotationsReachPropertyMetadata )
{
    const sw::PropertyInfo& height = getDisplayProperty( "_height" );
    const sw::string*       pUnits = height.findCustomMeta( "Units" );
    SW_ASSERT_NOT_NULL( pUnits );
    SW_EXPECT_EQUAL( sw::string( "cm" ), *pUnits );
    SW_EXPECT_TRUE( height._metadata.hasFullRange() && height._metadata.hasFullUIRange() );
    SW_EXPECT_NEAR_EQUAL( 1000.0f, height._metadata._maxRange, 1e-4f );
    SW_EXPECT_NEAR_EQUAL( 250.0f, height._metadata._uiMaxRange, 1e-4f );

    SW_EXPECT_TRUE( getDisplayProperty( "_emissive" )._metadata._bColorHdr == SW_TRUE );
    SW_EXPECT_TRUE( getDisplayProperty( "_notes" )._metadata._bMultiline == SW_TRUE );
    SW_EXPECT_EQUAL( sw::string( "*.png;*.dds" ), getDisplayProperty( "_texture" )._metadata._fileFilter );
    SW_EXPECT_EQUAL( sw::string( "!_bEnabled" ), getDisplayProperty( "_fallback" )._metadata._editCondition );
    SW_EXPECT_TRUE( getDisplayProperty( "_fallback" )._metadata._bEditConditionHides == SW_TRUE );
}

/**
 * @brief [ReflectionDisplayMetaTest] EditCondition 이 다른 프로퍼티 값을 따라 막고 · 숨긴다 — bool · 부정 · 열거형 같음 · 다름
 */
SW_TEST_CASE( ReflectionDisplayMetaTest, EditConditionFollowsTheOtherProperty )
{
    const sw::TypeInfo&  type = getDisplayType();
    sw::DisplayMetaActor actor;

    SW_EXPECT_TRUE( sw::PropertyEditCondition::getEditState( type, getDisplayProperty( "_speed" ), &actor ) == sw::PropertyEditState::Disabled );
    SW_EXPECT_TRUE( sw::PropertyEditCondition::getEditState( type, getDisplayProperty( "_fallback" ), &actor ) == sw::PropertyEditState::Enabled );
    SW_EXPECT_TRUE( sw::PropertyEditCondition::getEditState( type, getDisplayProperty( "_orbitRadius" ), &actor ) == sw::PropertyEditState::Disabled );
    SW_EXPECT_TRUE( sw::PropertyEditCondition::getEditState( type, getDisplayProperty( "_blend" ), &actor ) == sw::PropertyEditState::Disabled );
    SW_EXPECT_TRUE( sw::PropertyEditCondition::getEditState( type, getDisplayProperty( "_height" ), &actor ) == sw::PropertyEditState::Enabled );

    actor._bEnabled = true;
    actor._mode     = sw::DisplayMetaMode::Orbit;
    SW_EXPECT_TRUE( sw::PropertyEditCondition::getEditState( type, getDisplayProperty( "_speed" ), &actor ) == sw::PropertyEditState::Enabled );
    SW_EXPECT_TRUE( sw::PropertyEditCondition::getEditState( type, getDisplayProperty( "_fallback" ), &actor ) == sw::PropertyEditState::Hidden );
    SW_EXPECT_TRUE( sw::PropertyEditCondition::getEditState( type, getDisplayProperty( "_orbitRadius" ), &actor ) == sw::PropertyEditState::Enabled );
    SW_EXPECT_TRUE( sw::PropertyEditCondition::getEditState( type, getDisplayProperty( "_blend" ), &actor ) == sw::PropertyEditState::Enabled );

    actor._mode = sw::DisplayMetaMode::Follow;
    SW_EXPECT_TRUE( sw::PropertyEditCondition::getEditState( type, getDisplayProperty( "_orbitRadius" ), &actor ) == sw::PropertyEditState::Disabled );
    SW_EXPECT_TRUE( sw::PropertyEditCondition::getEditState( type, getDisplayProperty( "_blend" ), &actor ) == sw::PropertyEditState::Enabled );

    // 풀지 못하는 식은 막지 않고 이유를 말한다
    sw::PropertyInfo broken         = getDisplayProperty( "_speed" );
    broken._metadata._editCondition = "_mode == Sideways";
    sw::PropertyEditConditionExpr expr;
    sw::string                    error;
    SW_EXPECT_FALSE( sw::PropertyEditCondition::parse( type, broken, expr, error ) );
    SW_EXPECT_TRUE_MSG( error.find( "Sideways" ) != sw::string::npos, error.c_str() );
    broken._metadata._editCondition = "_noSuchProperty";
    SW_EXPECT_FALSE( sw::PropertyEditCondition::parse( type, broken, expr, error ) );
    SW_EXPECT_TRUE( sw::PropertyEditCondition::getEditState( type, broken, &actor ) == sw::PropertyEditState::Enabled );
}

/**
 * @brief [ReflectionDisplayMetaTest] 등록된 모든 타입의 EditCondition 이 풀린다 — 이름은 기반에 있을 수 있어 파서가 아니라 여기서 본다
 */
SW_TEST_CASE( ReflectionDisplayMetaTest, EveryEditConditionResolves )
{
    sw::string failures;
    uint32     checkedCount = 0;
    sw::engine::getTypeRegistry().forEachType( [&failures, &checkedCount]( const sw::TypeInfo& type )
    {
        for ( const sw::PropertyInfo& prop : type.getPropertiesWithBase() )
        {
            if ( prop._metadata._editCondition.empty() )
                continue;
            ++checkedCount;
            sw::PropertyEditConditionExpr expr;
            sw::string                    error;
            if ( sw::PropertyEditCondition::parse( type, prop, expr, error ) == false )
                failures += sw::string( type._fullyQualifiedName.c_str() ) + "::" + prop._name.c_str() + ": " + error + "\n";
        }
    } );
    SW_EXPECT_TRUE( checkedCount >= 4 );
    SW_EXPECT_TRUE_MSG( failures.empty(), failures.c_str() );
}

#endif // !SW_SHIPPING
