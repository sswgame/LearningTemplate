#include "pch.h"

#include "Core/Testing/TestFramework.h"

#include "Engine/Reflection/ReflectUnits.h"
#include "Engine/Reflection/ReflectionContainers.h"
#include "Engine/Reflection/ReflectionMetadata.h"
#include "Engine/Reflection/ReflectionTypes.h"
#include "Engine/Reflection/Tests/ReflectionTestTypes.h"
#include "Engine/Serialization/Format/BinarySerializer.h"
#include "Engine/Serialization/Format/JSONSerializer.h"
#include "Engine/Serialization/Format/XMLSerializer.h"

// ReflectionDisplayMetaTest — 표시 메타(단위 · 슬라이더 범위 · HDR 색 · 여러 줄 · 파일 필터)와 C 고정 배열 프로퍼티. 편집 조건 판정은 편집기 쪽(Inspector/Model/Tests)이 본다.

/**
 * @brief [ReflectionDisplayMetaTest] 단위는 같은 차원끼리 바뀌고, `"150 cm"` 같은 글을 저장 단위로 읽는다
 */
SW_TEST_CASE( ReflectionDisplayMetaTest, UnitsConvertWithinADimension )
{
    float64 value{ 0.0 };
    SW_REQUIRE_TRUE( sw::ReflectUnitUtil::convert( 150.0, "cm", "m", value ) );
    SW_EXPECT_NEAR( 1.5, value, 1e-9 );
    SW_REQUIRE_TRUE( sw::ReflectUnitUtil::convert( 180.0, "deg", "rad", value ) );
    SW_EXPECT_NEAR( 3.14159265358979, value, 1e-9 );
    SW_REQUIRE_TRUE( sw::ReflectUnitUtil::convert( 36.0, "km/h", "m/s", value ) );
    SW_EXPECT_NEAR( 10.0, value, 1e-9 );
    SW_EXPECT_FALSE( sw::ReflectUnitUtil::convert( 1.0, "m", "s", value ) ); // 차원이 다르다
    SW_EXPECT_FALSE( sw::ReflectUnitUtil::convert( 1.0, "furlong", "m", value ) );

    SW_REQUIRE_TRUE( sw::ReflectUnitUtil::parseValueInUnit( "2 km", "cm", value ) );
    SW_EXPECT_NEAR( 200000.0, value, 1e-6 );
    SW_REQUIRE_TRUE( sw::ReflectUnitUtil::parseValueInUnit( "1.5e1m/s2", "m/s2", value ) );
    SW_EXPECT_NEAR( 15.0, value, 1e-9 );
    SW_REQUIRE_TRUE( sw::ReflectUnitUtil::parseValueInUnit( " 42 ", "ms", value ) ); // 단위를 적지 않으면 이미 저장 단위다
    SW_EXPECT_NEAR( 42.0, value, 1e-9 );
    SW_EXPECT_FALSE( sw::ReflectUnitUtil::parseValueInUnit( "3 s", "m", value ) );
    SW_EXPECT_FALSE( sw::ReflectUnitUtil::parseValueInUnit( "cm", "m", value ) );
}

/**
 * @brief [ReflectionDisplayMetaTest] C 고정 배열 프로퍼티(`int32 _arrSlot[3]`)는 세 형식에서 왕복한다 — `std::array` 와 같은 고정 시퀀스다
 */
SW_TEST_CASE( ReflectionDisplayMetaTest, FixedCArrayRoundTripsInEveryFormat )
{
    const sw::TypeInfo* pType = sw::DisplayMetaActor::StaticType();
    SW_REQUIRE_NOT_NULL( pType );
    const sw::PropertyInfo* pSlot = pType->findPropertyInHierarchy( "_arrSlot" );
    SW_REQUIRE_NOT_NULL( pSlot );
    SW_REQUIRE_TRUE( pSlot->_bIsContainer == SW_TRUE );
    SW_REQUIRE_NOT_NULL( pSlot->_containerWrapper );
    SW_REQUIRE_NOT_NULL( pSlot->_containerWrapper->asSequence() );
    SW_EXPECT_TRUE( pSlot->_containerWrapper->asSequence()->isFixedSize() );

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

    const sw::string     xml = sw::XMLSerializer::serialize( &source, *pType );
    sw::DisplayMetaActor fromXML;
    SW_EXPECT_TRUE_MSG( sw::XMLSerializer::deserialize( &fromXML, *pType, xml ), xml.c_str() );
    SW_EXPECT_TRUE_MSG( isSame( fromXML ), xml.c_str() );

    const sw::string     json = sw::JSONSerializer::serialize( &source, *pType );
    sw::DisplayMetaActor fromJSON;
    SW_EXPECT_TRUE_MSG( sw::JSONSerializer::deserialize( &fromJSON, *pType, json ), json.c_str() );
    SW_EXPECT_TRUE_MSG( isSame( fromJSON ), json.c_str() );

    sw::vector<uint8> bytes;
    SW_EXPECT_TRUE( sw::BinarySerializer::serialize( &source, *pType, bytes ) );
    sw::DisplayMetaActor fromBinary;
    SW_EXPECT_TRUE( sw::BinarySerializer::deserialize( &fromBinary, *pType, bytes.data(), bytes.size() ) );
    SW_EXPECT_TRUE( isSame( fromBinary ) );
}

/**
 * @brief [ReflectionDisplayMetaTest] 표시 애노테이션이 프로퍼티 메타에 닿는다 — 단위는 커스텀 메타 `Units` 로, 슬라이더 범위는 허용 범위와 따로
 */
SW_TEST_CASE( ReflectionDisplayMetaTest, AnnotationsReachPropertyMetadata )
{
    const sw::TypeInfo* pType = sw::DisplayMetaActor::StaticType();
    SW_REQUIRE_NOT_NULL( pType );
    const sw::PropertyInfo* pHeight   = pType->findPropertyInHierarchy( "_height" );
    const sw::PropertyInfo* pEmissive = pType->findPropertyInHierarchy( "_emissive" );
    const sw::PropertyInfo* pNotes    = pType->findPropertyInHierarchy( "_notes" );
    const sw::PropertyInfo* pTexture  = pType->findPropertyInHierarchy( "_texture" );
    const sw::PropertyInfo* pFallback = pType->findPropertyInHierarchy( "_fallback" );
    SW_REQUIRE_NOT_NULL( pHeight );
    SW_REQUIRE_NOT_NULL( pEmissive );
    SW_REQUIRE_NOT_NULL( pNotes );
    SW_REQUIRE_NOT_NULL( pTexture );
    SW_REQUIRE_NOT_NULL( pFallback );

    const sw::string* pUnits = pHeight->findCustomMeta( "Units" );
    SW_REQUIRE_NOT_NULL( pUnits );
    SW_EXPECT_EQUAL( sw::string( "cm" ), *pUnits );
    SW_EXPECT_TRUE( pHeight->_metadata->hasFullRange() && pHeight->_metadata->hasFullUIRange() );
    SW_EXPECT_NEAR( 1000.0f, pHeight->_metadata->_maxRange, 1e-4f );
    SW_EXPECT_NEAR( 250.0f, pHeight->_metadata->_uiMaxRange, 1e-4f );

    SW_EXPECT_TRUE( pEmissive->_metadata->_bColorHDR == SW_TRUE );
    SW_EXPECT_TRUE( pNotes->_metadata->_bMultiline == SW_TRUE );
    SW_EXPECT_EQUAL( sw::string( "*.png;*.dds" ), pTexture->_metadata->_fileFilter );
    SW_EXPECT_EQUAL( sw::string( "!_bEnabled" ), pFallback->_metadata->_editCondition );
    SW_EXPECT_TRUE( pFallback->_metadata->_bEditConditionHides == SW_TRUE );
}
