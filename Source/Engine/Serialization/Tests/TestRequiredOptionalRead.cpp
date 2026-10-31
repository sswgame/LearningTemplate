// 폴백 인자 없는 값 읽기(필수 · 선택)가 "있음 · 없음 · 깨짐" 을 가르는지 본다 — XML 속성 · 자식, JSON 의 null · 정수를 실수 칸에(nogpu).
// JSON 의 형 거절 · 없는 필수 멤버는 `JSONDocumentTest.TypedReadsRefuseWrongValues` 가 본다.
#include "pch.h"

#include "Core/Testing/TestFramework.h"

#include "Engine/Serialization/JSON/JSONDocument.h"
#include "Engine/Serialization/XML/XMLDocument.h"

/**
 * @brief [RequiredOptionalReadTest] XML 필수 읽기는 없거나 깨지면 nullopt, 선택 읽기는 없으면 비고 true · 깨지면 false 다 — 기본값을 받지 않는다
 */
SW_TEST_CASE( RequiredOptionalReadTest, XMLAttributeAndChild )
{
    sw::XMLDocument doc;
    SW_REQUIRE_TRUE( doc.parse( "<Unit hp=\"12\" speed=\"1.5\" flying=\"yes\" name=\"Orc\" bad=\"twelve\" huge=\"-3\"><Armor>4</Armor><Broken>x</Broken></Unit>" ) );
    const sw::XMLNode unit = doc.getRoot( "Unit" );
    SW_REQUIRE_TRUE( unit.isValid() );

    SW_EXPECT_EQUAL( 12, unit.readRequiredAttribute<int32>( "hp" ).value_or( 0 ) );
    SW_EXPECT_NEAR( 1.5f, unit.readRequiredAttribute<float32>( "speed" ).value_or( 0.0f ), 1e-6 );
    SW_EXPECT_TRUE( unit.readRequiredAttribute<bool>( "flying" ).value_or( false ) );
    SW_EXPECT_EQUAL( sw::string( "Orc" ), unit.readRequiredAttribute<sw::string>( "name" ).value_or( sw::string{} ) );
    SW_EXPECT_EQUAL( 4, unit.readRequiredChild<int32>( "Armor" ).value_or( 0 ) );

    std::optional<int32> optional;
    SW_EXPECT_TRUE( unit.readOptionalAttribute( "missing", optional ) );
    SW_EXPECT_FALSE( optional.has_value() );
    SW_EXPECT_TRUE( unit.readOptionalAttribute( "hp", optional ) );
    SW_EXPECT_EQUAL( 12, optional.value_or( 0 ) );

    SW_EXPECT_LOG( "Xml.MissingRequired" );
    SW_EXPECT_LOG( "Xml.AttributeTypeMismatch" );
    SW_EXPECT_LOG( "Xml.ChildTypeMismatch" );
    SW_EXPECT_FALSE( unit.readRequiredAttribute<int32>( "missing" ).has_value() );
    SW_EXPECT_FALSE( unit.readRequiredAttribute<int32>( "bad" ).has_value() );
    SW_EXPECT_FALSE( unit.readRequiredAttribute<uint32>( "huge" ).has_value() ); // 음수는 부호 없는 칸에 감기지 않는다
    SW_EXPECT_FALSE( unit.readOptionalAttribute( "bad", optional ) );
    SW_EXPECT_FALSE( optional.has_value() );
    std::optional<int32> child;
    SW_EXPECT_FALSE( unit.readOptionalChild( "Broken", child ) );
    SW_EXPECT_FALSE( unit.readRequiredChild<int32>( "Shield" ).has_value() );
}

/**
 * @brief [RequiredOptionalReadTest] JSON 의 null 은 없음이고, 정수는 실수 칸에 읽히며, 선택 읽기도 타입이 다르면 false 다
 */
SW_TEST_CASE( RequiredOptionalReadTest, JSONNullIsAbsentAndIntegerReadsAsFloat )
{
    sw::JSONDocument doc;
    SW_REQUIRE_TRUE( doc.parse( R"({ "count": 3, "ratio": 0.25, "fraction": 1.5, "text": "7", "none": null })" ) );
    const sw::JSONValue root = doc.getRoot();
    SW_EXPECT_NEAR( 0.25f, root.readRequired<float32>( "ratio" ).value_or( 0.0f ), 1e-6 );
    SW_EXPECT_NEAR( 3.0f, root.readRequired<float32>( "count" ).value_or( 0.0f ), 1e-6 ); // 정수는 실수 칸에 읽힌다

    std::optional<int32> optional{ 5 };
    SW_EXPECT_TRUE( root.readOptional( "none", optional ) );
    SW_EXPECT_FALSE( optional.has_value() );

    SW_EXPECT_LOG( "JSON.WrongType" );
    SW_EXPECT_FALSE( root.readRequired<int32>( "text" ).has_value() ); // 글로 쓴 숫자는 숫자가 아니다
    SW_EXPECT_FALSE( root.readRequired<bool>( "count" ).has_value() );
    SW_EXPECT_FALSE( root.readOptional( "fraction", optional ) );
}
