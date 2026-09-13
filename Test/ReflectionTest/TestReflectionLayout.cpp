#include "pch.h"

#include "Engine/Common/EngineServices.h"
#include "Engine/Reflection/ReflectionCore.h"
#include "Engine/Serialization/Core/Serializer.h"
#include "Engine/Serialization/Format/BinarySerializer.h"

#include "ReflectionTest/TestReflectionFixtures.h"
#include "ReflectionTest/TestSampleActor.h"

#include "TestFramework/TestFramework.h"

// 리플렉션이 보는 메모리 배치 — 컨테이너 래퍼 · 중첩 타입 · 비트필드.
/**
 * @brief [ReflectionContainersTest] Vector 래퍼
 */

SW_TEST_CASE( ReflectionContainersTest, VectorWrapper )
{
    sw::vector<int32>                    vec = { 10, 20, 30 };
    sw::VectorWrapper<sw::vector<int32>> wrapper;

    SW_EXPECT_EQUAL( 3u, wrapper.getSize( &vec ) );
    SW_EXPECT_EQUAL( 10, *static_cast<int32*>( wrapper.getElement( &vec, 0 ) ) );
    SW_EXPECT_EQUAL( 30, *static_cast<int32*>( wrapper.getElement( &vec, 2 ) ) );

    wrapper.addElementDefault( &vec );
    SW_EXPECT_EQUAL( 4u, wrapper.getSize( &vec ) );
    SW_EXPECT_EQUAL( 0, *static_cast<int32*>( wrapper.getElement( &vec, 3 ) ) );

    wrapper.clear( &vec );
    SW_EXPECT_EQUAL( 0u, wrapper.getSize( &vec ) );
}

/**
 * @brief [ReflectionContainersTest] List 래퍼
 */
SW_TEST_CASE( ReflectionContainersTest, ListWrapper )
{
    sw::list<sw::string>                  lst = { "alpha", "beta" };
    sw::ListWrapper<sw::list<sw::string>> wrapper;

    SW_EXPECT_EQUAL( 2u, wrapper.getSize( &lst ) );
    SW_EXPECT_EQUAL( sw::string( "alpha" ), *static_cast<sw::string*>( wrapper.getElement( &lst, 0 ) ) );

    wrapper.clear( &lst );
    SW_EXPECT_EQUAL( 0u, wrapper.getSize( &lst ) );
}

/**
 * @brief [ReflectionContainersTest] Deque 래퍼
 */
SW_TEST_CASE( ReflectionContainersTest, DequeWrapper )
{
    sw::deque<float32>                   dq = { 1.5f, 2.5f, 3.5f };
    sw::DequeWrapper<sw::deque<float32>> wrapper;

    SW_EXPECT_EQUAL( 3u, wrapper.getSize( &dq ) );
    SW_EXPECT_NEAR_EQUAL( 2.5f, *static_cast<float32*>( wrapper.getElement( &dq, 1 ) ), 0.001f );
}

/**
 * @brief [ReflectionContainersTest] Set 래퍼
 */
SW_TEST_CASE( ReflectionContainersTest, SetWrapper )
{
    sw::set<int32>                 st = { 100, 200, 300 };
    sw::SetWrapper<sw::set<int32>> wrapper;

    SW_EXPECT_EQUAL( 3u, wrapper.getSize( &st ) );
    SW_EXPECT_EQUAL( 100, *static_cast<const int32*>( wrapper.getElementConst( &st, 0 ) ) );
}

/**
 * @brief [ReflectionContainersTest] Map 래퍼
 */
SW_TEST_CASE( ReflectionContainersTest, MapWrapper )
{
    sw::map<sw::string, int32> mp = {
        {"Atk", 50},
        {"Def", 20}
    };
    sw::MapWrapper<sw::map<sw::string, int32>> wrapper;

    SW_EXPECT_EQUAL( 2u, wrapper.getSize( &mp ) );

    int32 elementCount{ 0 };
    wrapper.forEach( &mp, [&]( const void* pKPtr, const void* pVPtr )
    {
        elementCount++;
        const sw::string* key = static_cast<const sw::string*>( pKPtr );
        const int32*      val = static_cast<const int32*>( pVPtr );
        if ( *key == "Atk" )
            SW_EXPECT_EQUAL( 50, *val );
        if ( *key == "Def" )
            SW_EXPECT_EQUAL( 20, *val );
    } );

    SW_EXPECT_EQUAL( 2, elementCount );

    sw::string newKey = "Speed";
    int32      newVal = 10;
    wrapper.insertKeyValue( &mp, &newKey, &newVal );
    SW_EXPECT_EQUAL( 3u, wrapper.getSize( &mp ) );
    SW_EXPECT_EQUAL( 10, mp["Speed"] );
}

/**
 * @brief [ReflectionContainersTest] 추가 컨테이너 래퍼
 */
SW_TEST_CASE( ReflectionContainersTest, AdditionalContainerWrappers )
{

    std::array<int32, 4>                   arr = { 1, 2, 3, 4 };
    sw::ArrayWrapper<std::array<int32, 4>> arrWrapper;
    SW_EXPECT_EQUAL( 4u, arrWrapper.getSize( &arr ) );
    SW_EXPECT_EQUAL( 3, *static_cast<int32*>( arrWrapper.getElement( &arr, 2 ) ) );

    sw::unordered_set<sw::string>                          uSet = { "alpha", "beta" };
    sw::UnorderedSetWrapper<sw::unordered_set<sw::string>> uSetWrapper;
    SW_EXPECT_EQUAL( 2u, uSetWrapper.getSize( &uSet ) );

    sw::unordered_map<sw::string, int32> uMap = {
        { "Hp", 100 }
    };
    sw::UnorderedMapWrapper<sw::unordered_map<sw::string, int32>> uMapWrapper;
    SW_EXPECT_EQUAL( 1u, uMapWrapper.getSize( &uMap ) );
    SW_EXPECT_EQUAL( sizeof( sw::string ), uMapWrapper.getKeySize() );
    SW_EXPECT_EQUAL( sizeof( int32 ), uMapWrapper.getValueSize() );
}

/**
 * @brief [ReflectionInnerTypesTest] 외부 구조체 조회
 */
SW_TEST_CASE( ReflectionInnerTypesTest, FindOuterStruct )
{
    const sw::TypeInfo* typeInfoFqn =
        sw::engine::getTypeRegistry().findType( sw::hashed_string( "sw::InnerNamespaceForTest::OuterStruct" ) );
    SW_EXPECT_TRUE( typeInfoFqn != nullptr );

    const sw::TypeInfo* typeInfoShort =
        sw::engine::getTypeRegistry().findType( sw::hashed_string( "OuterStruct" ) );
    SW_EXPECT_TRUE( typeInfoShort != nullptr );

    if ( typeInfoFqn == nullptr )
        return;

    sw::InnerNamespaceForTest::OuterStruct instance;
    instance._outerValue = 123;

    const sw::PropertyInfo* prop = typeInfoFqn->findProperty( sw::hashed_string( "_outerValue" ) );
    SW_EXPECT_TRUE( prop != nullptr );
    if ( prop != nullptr )
        SW_EXPECT_EQUAL( 123, *prop->getValuePtr<int32>( &instance ) );
}

/**
 * @brief [ReflectionInnerTypesTest] 내부 구조체 조회
 */
SW_TEST_CASE( ReflectionInnerTypesTest, FindInnerStruct )
{
    const sw::TypeInfo* typeInfoFqn =
        sw::engine::getTypeRegistry().findType( sw::hashed_string( "sw::InnerNamespaceForTest::OuterStruct::InnerStruct" ) );
    SW_EXPECT_TRUE( typeInfoFqn != nullptr );

    const sw::TypeInfo* typeInfoShort =
        sw::engine::getTypeRegistry().findType( sw::hashed_string( "InnerStruct" ) );
    SW_EXPECT_TRUE( typeInfoShort != nullptr );

    if ( typeInfoFqn == nullptr )
        return;

    sw::InnerNamespaceForTest::OuterStruct::InnerStruct instance;
    instance._innerData = "TestNested";
    instance._score     = 9.5f;

    const sw::PropertyInfo* propData  = typeInfoFqn->findProperty( sw::hashed_string( "_innerData" ) );
    const sw::PropertyInfo* propScore = typeInfoFqn->findProperty( sw::hashed_string( "_score" ) );
    SW_EXPECT_TRUE( propData != nullptr );
    SW_EXPECT_TRUE( propScore != nullptr );

    if ( propData && propScore )
    {
        SW_EXPECT_EQUAL( sw::string( "TestNested" ), *propData->getValuePtr<sw::string>( &instance ) );
        SW_EXPECT_NEAR_EQUAL( 9.5f, *propScore->getValuePtr<float32>( &instance ), 0.001f );
    }
}

/**
 * @brief [ReflectionInnerTypesTest] 내부 클래스 조회
 */
SW_TEST_CASE( ReflectionInnerTypesTest, FindInnerClass )
{
    const sw::TypeInfo* typeInfoFqn =
        sw::engine::getTypeRegistry().findType( sw::hashed_string( "sw::InnerNamespaceForTest::OuterStruct::InnerClass" ) );
    SW_EXPECT_TRUE( typeInfoFqn != nullptr );

    const sw::TypeInfo* typeInfoShort =
        sw::engine::getTypeRegistry().findType( sw::hashed_string( "InnerClass" ) );
    SW_EXPECT_TRUE( typeInfoShort != nullptr );

    if ( typeInfoFqn == nullptr )
        return;

    sw::InnerNamespaceForTest::OuterStruct::InnerClass instance;
    const sw::PropertyInfo*                            propId = typeInfoFqn->findProperty( sw::hashed_string( "_id" ) );
    SW_EXPECT_TRUE( propId != nullptr );
    if ( propId != nullptr )
    {
        int64 newId = 8888;
        propId->setValue( &instance, newId );
        SW_EXPECT_EQUAL( 8888, *propId->getValuePtr<int64>( &instance ) );
    }
}

/**
 * @brief [ReflectionInnerTypesTest] 내부 enum 조회
 */
SW_TEST_CASE( ReflectionInnerTypesTest, FindInnerEnum )
{
    const sw::EnumInfo* enumInfoFqn =
        sw::engine::getTypeRegistry().findEnum( sw::hashed_string( "sw::InnerNamespaceForTest::OuterStruct::InnerEnum" ) );
    SW_EXPECT_TRUE( enumInfoFqn != nullptr );

    const sw::EnumInfo* enumInfoShort =
        sw::engine::getTypeRegistry().findEnum( sw::hashed_string( "InnerEnum" ) );
    SW_EXPECT_TRUE( enumInfoShort != nullptr );

    if ( enumInfoFqn == nullptr )
        return;

    SW_EXPECT_TRUE( enumInfoFqn->_bIsBitFlag );
    uint32     combined = 1 | 4;
    sw::string flagStr  = enumInfoFqn->toStringFlags( combined ).c_str();
    SW_EXPECT_TRUE( flagStr.find( "OptionA" ) != sw::string::npos );
    SW_EXPECT_TRUE( flagStr.find( "OptionC" ) != sw::string::npos );
}

/**
 * @brief [ReflectionInnerTypesTest] 내부 구조체 직렬화 라운드트립
 */
SW_TEST_CASE( ReflectionInnerTypesTest, InnerStructSerializationRoundtrip )
{
    const sw::TypeInfo* typeInfo =
        sw::engine::getTypeRegistry().findType( sw::hashed_string( "sw::InnerNamespaceForTest::OuterStruct::InnerStruct" ) );
    SW_EXPECT_TRUE( typeInfo != nullptr );
    if ( typeInfo == nullptr )
        return;

    sw::InnerNamespaceForTest::OuterStruct::InnerStruct src;
    src._innerData = "SerializationTest";
    src._score     = 12.34f;

    sw::string json = sw::JsonSerializer::serialize( &src, *typeInfo );
    SW_EXPECT_TRUE( json.empty() == false );

    sw::InnerNamespaceForTest::OuterStruct::InnerStruct dstJson;
    bool                                                jsonOk = sw::JsonSerializer::deserialize( &dstJson, *typeInfo, json );
    SW_EXPECT_TRUE( jsonOk );
    SW_EXPECT_EQUAL( sw::string( "SerializationTest" ), dstJson._innerData );
    SW_EXPECT_NEAR_EQUAL( 12.34f, dstJson._score, 0.01f );

    sw::string xml = sw::XmlSerializer::serialize( &src, *typeInfo );
    SW_EXPECT_TRUE( xml.empty() == false );

    sw::InnerNamespaceForTest::OuterStruct::InnerStruct dstXml;
    bool                                                xmlOk = sw::XmlSerializer::deserialize( &dstXml, *typeInfo, xml );
    SW_EXPECT_TRUE( xmlOk );
    SW_EXPECT_EQUAL( sw::string( "SerializationTest" ), dstXml._innerData );
    SW_EXPECT_NEAR_EQUAL( 12.34f, dstXml._score, 0.01f );
}

/**
 * @brief [ReflectionBitfieldTest] 비트필드 프로퍼티 메타데이터 및 오프셋/마스크 검증
 */
SW_TEST_CASE( ReflectionBitfieldTest, BitfieldPropertyMetadata )
{
    const sw::TypeInfo* pType = sw::engine::getTypeRegistry().findType<sw::BitfieldTestActor>();
    SW_ASSERT_NOT_NULL( pType );

    const sw::PropertyInfo* pPropActive = pType->findProperty( sw::hashed_string( "_bActive" ) );
    SW_ASSERT_NOT_NULL( pPropActive );
    SW_EXPECT_EQUAL( SW_TRUE, pPropActive->_bIsBitField );
    SW_EXPECT_EQUAL( 0x01, pPropActive->_bitMask );

    const sw::PropertyInfo* pPropInvuln = pType->findProperty( sw::hashed_string( "_bInvulnerable" ) );
    SW_ASSERT_NOT_NULL( pPropInvuln );
    SW_EXPECT_EQUAL( SW_TRUE, pPropInvuln->_bIsBitField );
    SW_EXPECT_EQUAL( 0x02, pPropInvuln->_bitMask );

    const sw::PropertyInfo* pPropCanJump = pType->findProperty( sw::hashed_string( "_bCanJump" ) );
    SW_ASSERT_NOT_NULL( pPropCanJump );
    SW_EXPECT_EQUAL( SW_TRUE, pPropCanJump->_bIsBitField );
    SW_EXPECT_EQUAL( 0x04, pPropCanJump->_bitMask );

    const sw::PropertyInfo* pPropScore = pType->findProperty( sw::hashed_string( "_score" ) );
    SW_ASSERT_NOT_NULL( pPropScore );
    SW_EXPECT_EQUAL( SW_FALSE, pPropScore->_bIsBitField );
    SW_EXPECT_EQUAL( 0xFF, pPropScore->_bitMask );
}

/**
 * @brief [ReflectionBitfieldTest] 비트필드 getValue/setValue 독립성 검증
 */
SW_TEST_CASE( ReflectionBitfieldTest, BitfieldGetSetValue )
{
    const sw::TypeInfo* pType = sw::engine::getTypeRegistry().findType<sw::BitfieldTestActor>();
    SW_ASSERT_NOT_NULL( pType );

    const sw::PropertyInfo* pPropActive  = pType->findProperty( sw::hashed_string( "_bActive" ) );
    const sw::PropertyInfo* pPropInvuln  = pType->findProperty( sw::hashed_string( "_bInvulnerable" ) );
    const sw::PropertyInfo* pPropCanJump = pType->findProperty( sw::hashed_string( "_bCanJump" ) );
    SW_ASSERT_NOT_NULL( pPropActive );
    SW_ASSERT_NOT_NULL( pPropInvuln );
    SW_ASSERT_NOT_NULL( pPropCanJump );

    sw::BitfieldTestActor actor;
    SW_EXPECT_EQUAL( SW_FALSE, actor._bActive );
    SW_EXPECT_EQUAL( SW_FALSE, actor._bInvulnerable );
    SW_EXPECT_EQUAL( SW_FALSE, actor._bCanJump );

    // 1) _bActive = true
    pPropActive->setValue<bool>( &actor, true );
    SW_EXPECT_EQUAL( SW_TRUE, actor._bActive );
    SW_EXPECT_EQUAL( SW_FALSE, actor._bInvulnerable );
    SW_EXPECT_EQUAL( SW_FALSE, actor._bCanJump );
    SW_EXPECT_TRUE( pPropActive->getValue<bool>( &actor ) );
    SW_EXPECT_FALSE( pPropInvuln->getValue<bool>( &actor ) );

    // 2) _bCanJump = true (인접 비트 _bActive 유지 검증)
    pPropCanJump->setValue<bool>( &actor, true );
    SW_EXPECT_EQUAL( SW_TRUE, actor._bActive );
    SW_EXPECT_EQUAL( SW_FALSE, actor._bInvulnerable );
    SW_EXPECT_EQUAL( SW_TRUE, actor._bCanJump );
    SW_EXPECT_TRUE( pPropCanJump->getValue<bool>( &actor ) );

    // 3) _bActive = false
    pPropActive->setValue<bool>( &actor, false );
    SW_EXPECT_EQUAL( SW_FALSE, actor._bActive );
    SW_EXPECT_EQUAL( SW_FALSE, actor._bInvulnerable );
    SW_EXPECT_EQUAL( SW_TRUE, actor._bCanJump );
    SW_EXPECT_FALSE( pPropActive->getValue<bool>( &actor ) );
    SW_EXPECT_TRUE( pPropCanJump->getValue<bool>( &actor ) );
}

/**
 * @brief [ReflectionBitfieldTest] 비트필드 JSON, XML, Binary 직렬화 라운드트립 검증
 */
SW_TEST_CASE( ReflectionBitfieldTest, BitfieldSerializationRoundtrip )
{
    const sw::TypeInfo* pType = sw::engine::getTypeRegistry().findType<sw::BitfieldTestActor>();
    SW_ASSERT_NOT_NULL( pType );

    sw::BitfieldTestActor source;
    source._bActive       = SW_TRUE;
    source._bInvulnerable = SW_FALSE;
    source._bCanJump      = SW_TRUE;
    source._score         = 777;

    // 1) JSON 직렬화 & 역직렬화
    const sw::string json = sw::JsonSerializer::serialize( &source, *pType );
    SW_EXPECT_TRUE( json.find( "_bActive" ) != sw::string::npos );
    SW_EXPECT_TRUE( json.find( "_bCanJump" ) != sw::string::npos );

    sw::BitfieldTestActor jsonTarget;
    SW_EXPECT_TRUE( sw::JsonSerializer::deserialize( &jsonTarget, *pType, json ) );
    SW_EXPECT_EQUAL( SW_TRUE, jsonTarget._bActive );
    SW_EXPECT_EQUAL( SW_FALSE, jsonTarget._bInvulnerable );
    SW_EXPECT_EQUAL( SW_TRUE, jsonTarget._bCanJump );
    SW_EXPECT_EQUAL( 777, jsonTarget._score );

    // 2) XML 직렬화 & 역직렬화
    const sw::string      xml = sw::XmlSerializer::serialize( &source, *pType );
    sw::BitfieldTestActor xmlTarget;
    SW_EXPECT_TRUE( sw::XmlSerializer::deserialize( &xmlTarget, *pType, xml ) );
    SW_EXPECT_EQUAL( SW_TRUE, xmlTarget._bActive );
    SW_EXPECT_EQUAL( SW_FALSE, xmlTarget._bInvulnerable );
    SW_EXPECT_EQUAL( SW_TRUE, xmlTarget._bCanJump );
    SW_EXPECT_EQUAL( 777, xmlTarget._score );

    // 3) Binary 직렬화 & 역직렬화
    sw::vector<uint8> listBin;
    sw::BinarySerializer::serialize( &source, *pType, listBin );
    sw::BitfieldTestActor binTarget;
    SW_EXPECT_TRUE( sw::BinarySerializer::deserialize( &binTarget, *pType, listBin.data(), listBin.size() ) );
    SW_EXPECT_EQUAL( SW_TRUE, binTarget._bActive );
    SW_EXPECT_EQUAL( SW_FALSE, binTarget._bInvulnerable );
    SW_EXPECT_EQUAL( SW_TRUE, binTarget._bCanJump );
    SW_EXPECT_EQUAL( 777, binTarget._score );
}

/**
 * @brief [ReflectionBitfieldTest] uint16, uint32, uint64 비트필드 플래그 getValue/setValue 독립성 검증
 */
SW_TEST_CASE( ReflectionBitfieldTest, WideBitfieldGetSetValue )
{
    const sw::TypeInfo* pType = sw::engine::getTypeRegistry().findType<sw::WideBitfieldTestActor>();
    SW_ASSERT_NOT_NULL( pType );

    const sw::PropertyInfo* pProp16A = pType->findProperty( sw::hashed_string( "_bFlag16_A" ) );
    const sw::PropertyInfo* pProp16B = pType->findProperty( sw::hashed_string( "_bFlag16_B" ) );
    const sw::PropertyInfo* pProp32A = pType->findProperty( sw::hashed_string( "_bFlag32_A" ) );
    const sw::PropertyInfo* pProp32B = pType->findProperty( sw::hashed_string( "_bFlag32_B" ) );
    const sw::PropertyInfo* pProp64A = pType->findProperty( sw::hashed_string( "_bFlag64_A" ) );
    const sw::PropertyInfo* pProp64B = pType->findProperty( sw::hashed_string( "_bFlag64_B" ) );

    SW_ASSERT_NOT_NULL( pProp16A );
    SW_ASSERT_NOT_NULL( pProp16B );
    SW_ASSERT_NOT_NULL( pProp32A );
    SW_ASSERT_NOT_NULL( pProp32B );
    SW_ASSERT_NOT_NULL( pProp64A );
    SW_ASSERT_NOT_NULL( pProp64B );

    SW_EXPECT_EQUAL( SW_TRUE, pProp16A->_bIsBitField );
    SW_EXPECT_EQUAL( SW_TRUE, pProp16B->_bIsBitField );
    SW_EXPECT_EQUAL( SW_TRUE, pProp32A->_bIsBitField );
    SW_EXPECT_EQUAL( SW_TRUE, pProp32B->_bIsBitField );
    SW_EXPECT_EQUAL( SW_TRUE, pProp64A->_bIsBitField );
    SW_EXPECT_EQUAL( SW_TRUE, pProp64B->_bIsBitField );

    sw::WideBitfieldTestActor actor;

    // 1) uint16 비트필드 쓰기 및 읽기
    pProp16A->setValue<bool>( &actor, true );
    pProp16B->setValue<bool>( &actor, false );
    SW_EXPECT_EQUAL( SW_TRUE, actor._bFlag16_A );
    SW_EXPECT_EQUAL( SW_FALSE, actor._bFlag16_B );
    SW_EXPECT_TRUE( pProp16A->getValue<bool>( &actor ) );
    SW_EXPECT_FALSE( pProp16B->getValue<bool>( &actor ) );

    // 2) uint32 비트필드 쓰기 및 읽기
    pProp32A->setValue<bool>( &actor, false );
    pProp32B->setValue<bool>( &actor, true );
    SW_EXPECT_EQUAL( SW_FALSE, actor._bFlag32_A );
    SW_EXPECT_EQUAL( SW_TRUE, actor._bFlag32_B );
    SW_EXPECT_FALSE( pProp32A->getValue<bool>( &actor ) );
    SW_EXPECT_TRUE( pProp32B->getValue<bool>( &actor ) );

    // 3) uint64 비트필드 쓰기 및 읽기
    pProp64A->setValue<bool>( &actor, true );
    pProp64B->setValue<bool>( &actor, true );
    SW_EXPECT_EQUAL( SW_TRUE, actor._bFlag64_A );
    SW_EXPECT_EQUAL( SW_TRUE, actor._bFlag64_B );
    SW_EXPECT_TRUE( pProp64A->getValue<bool>( &actor ) );
    SW_EXPECT_TRUE( pProp64B->getValue<bool>( &actor ) );

    // 4) 독립성 검증: uint16_B를 true로 수정해도 다른 필드 영향 없음
    pProp16B->setValue<bool>( &actor, true );
    SW_EXPECT_EQUAL( SW_TRUE, actor._bFlag16_A );
    SW_EXPECT_EQUAL( SW_TRUE, actor._bFlag16_B );
    SW_EXPECT_EQUAL( SW_FALSE, actor._bFlag32_A );
    SW_EXPECT_EQUAL( SW_TRUE, actor._bFlag32_B );
    SW_EXPECT_EQUAL( SW_TRUE, actor._bFlag64_A );
    SW_EXPECT_EQUAL( SW_TRUE, actor._bFlag64_B );
}

/**
 * @brief [ReflectionBitfieldTest] uint16, uint32, uint64 비트필드 플래그 JSON, XML, Binary 직렬화 라운드트립 검증
 */
SW_TEST_CASE( ReflectionBitfieldTest, WideBitfieldSerializationRoundtrip )
{
    const sw::TypeInfo* pType = sw::engine::getTypeRegistry().findType<sw::WideBitfieldTestActor>();
    SW_ASSERT_NOT_NULL( pType );

    sw::WideBitfieldTestActor source;
    source._bFlag16_A = SW_TRUE;
    source._bFlag16_B = SW_FALSE;
    source._bFlag32_A = SW_FALSE;
    source._bFlag32_B = SW_TRUE;
    source._bFlag64_A = SW_TRUE;
    source._bFlag64_B = SW_TRUE;

    // 1) JSON 직렬화 & 역직렬화
    const sw::string          json = sw::JsonSerializer::serialize( &source, *pType );
    sw::WideBitfieldTestActor jsonTarget;
    SW_EXPECT_TRUE( sw::JsonSerializer::deserialize( &jsonTarget, *pType, json ) );
    SW_EXPECT_EQUAL( SW_TRUE, jsonTarget._bFlag16_A );
    SW_EXPECT_EQUAL( SW_FALSE, jsonTarget._bFlag16_B );
    SW_EXPECT_EQUAL( SW_FALSE, jsonTarget._bFlag32_A );
    SW_EXPECT_EQUAL( SW_TRUE, jsonTarget._bFlag32_B );
    SW_EXPECT_EQUAL( SW_TRUE, jsonTarget._bFlag64_A );
    SW_EXPECT_EQUAL( SW_TRUE, jsonTarget._bFlag64_B );

    // 2) XML 직렬화 & 역직렬화
    const sw::string          xml = sw::XmlSerializer::serialize( &source, *pType );
    sw::WideBitfieldTestActor xmlTarget;
    SW_EXPECT_TRUE( sw::XmlSerializer::deserialize( &xmlTarget, *pType, xml ) );
    SW_EXPECT_EQUAL( SW_TRUE, xmlTarget._bFlag16_A );
    SW_EXPECT_EQUAL( SW_FALSE, xmlTarget._bFlag16_B );
    SW_EXPECT_EQUAL( SW_FALSE, xmlTarget._bFlag32_A );
    SW_EXPECT_EQUAL( SW_TRUE, xmlTarget._bFlag32_B );
    SW_EXPECT_EQUAL( SW_TRUE, xmlTarget._bFlag64_A );
    SW_EXPECT_EQUAL( SW_TRUE, xmlTarget._bFlag64_B );

    // 3) Binary 직렬화 & 역직렬화
    sw::vector<uint8> listBin;
    sw::BinarySerializer::serialize( &source, *pType, listBin );
    sw::WideBitfieldTestActor binTarget;
    SW_EXPECT_TRUE( sw::BinarySerializer::deserialize( &binTarget, *pType, listBin.data(), listBin.size() ) );
    SW_EXPECT_EQUAL( SW_TRUE, binTarget._bFlag16_A );
    SW_EXPECT_EQUAL( SW_FALSE, binTarget._bFlag16_B );
    SW_EXPECT_EQUAL( SW_FALSE, binTarget._bFlag32_A );
    SW_EXPECT_EQUAL( SW_TRUE, binTarget._bFlag32_B );
    SW_EXPECT_EQUAL( SW_TRUE, binTarget._bFlag64_A );
    SW_EXPECT_EQUAL( SW_TRUE, binTarget._bFlag64_B );
}
