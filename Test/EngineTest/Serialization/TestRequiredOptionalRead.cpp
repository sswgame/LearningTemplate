#include "pch.h"

#include "Core/File/KeyValueFile.h"

#include "Engine/Serialization/JSON/JSONDocument.h"
#include "Engine/Serialization/XML/XMLDocument.h"

#include "TestFramework/TestFramework.h"

// 폴백 인자 없는 값 읽기(필수 · 선택)가 "있음 · 없음 · 깨짐" 을 가르는지 본다 — XML 속성 · 자식, JSON 멤버, key=value(결정 기록 5-4).

/**
 * @brief [RequiredOptionalReadTest] XML 필수 읽기는 없거나 깨지면 nullopt, 선택 읽기는 없으면 비고 true · 깨지면 false 다 — 기본값을 받지 않는다
 */
SW_TEST_CASE( RequiredOptionalReadTest, XMLAttributeAndChild )
{
    sw::XMLDocument doc;
    SW_ASSERT_TRUE( doc.parse( "<Unit hp=\"12\" speed=\"1.5\" flying=\"yes\" name=\"Orc\" bad=\"twelve\" huge=\"-3\"><Armor>4</Armor><Broken>x</Broken></Unit>" ) );
    const sw::XMLNode unit = doc.getRoot( "Unit" );
    SW_ASSERT_TRUE( unit.isValid() );

    SW_EXPECT_EQUAL( 12, unit.readRequiredAttribute<int32>( "hp" ).value_or( 0 ) );
    SW_EXPECT_NEAR_EQUAL( 1.5f, unit.readRequiredAttribute<float32>( "speed" ).value_or( 0.0f ), 1e-6f );
    SW_EXPECT_TRUE( unit.readRequiredAttribute<bool>( "flying" ).value_or( false ) );
    SW_EXPECT_EQUAL( sw::string( "Orc" ), unit.readRequiredAttribute<sw::string>( "name" ).value_or( sw::string{} ) );
    SW_EXPECT_EQUAL( 4, unit.readRequiredChild<int32>( "Armor" ).value_or( 0 ) );

    std::optional<int32> optional;
    SW_EXPECT_TRUE( unit.readOptionalAttribute( "missing", optional ) );
    SW_EXPECT_FALSE( optional.has_value() );
    SW_EXPECT_TRUE( unit.readOptionalAttribute( "hp", optional ) );
    SW_EXPECT_EQUAL( 12, optional.value_or( 0 ) );
    {
        SW_TEST_DEFENSIVE_SCOPE( "missing and unreadable XML values" );
        SW_EXPECT_FALSE( unit.readRequiredAttribute<int32>( "missing" ).has_value() );
        SW_EXPECT_FALSE( unit.readRequiredAttribute<int32>( "bad" ).has_value() );
        SW_EXPECT_FALSE( unit.readRequiredAttribute<uint32>( "huge" ).has_value() ); // 음수는 부호 없는 칸에 감기지 않는다
        SW_EXPECT_FALSE( unit.readOptionalAttribute( "bad", optional ) );
        SW_EXPECT_FALSE( optional.has_value() );
        std::optional<int32> child;
        SW_EXPECT_FALSE( unit.readOptionalChild( "Broken", child ) );
        SW_EXPECT_FALSE( unit.readRequiredChild<int32>( "Shield" ).has_value() );
    }
}

/**
 * @brief [RequiredOptionalReadTest] JSON 은 타입이 다른 값(정수 칸의 실수 · 32 비트 범위 밖 · 문자열 숫자)을 깨짐으로 보고, null 은 없음이다
 */
SW_TEST_CASE( RequiredOptionalReadTest, JSONMember )
{
    sw::JSONDocument doc;
    SW_ASSERT_TRUE( doc.parse( R"({ "count": 3, "ratio": 0.25, "on": true, "label": "a", "fraction": 1.5, "big": 5000000000, "text": "7", "none": null })" ) );
    const sw::JSONValue root = doc.getRoot();
    SW_EXPECT_EQUAL( 3, root.readRequired<int32>( "count" ).value_or( 0 ) );
    SW_EXPECT_NEAR_EQUAL( 0.25f, root.readRequired<float32>( "ratio" ).value_or( 0.0f ), 1e-6f );
    SW_EXPECT_NEAR_EQUAL( 3.0f, root.readRequired<float32>( "count" ).value_or( 0.0f ), 1e-6f ); // 정수는 실수 칸에 읽힌다
    SW_EXPECT_TRUE( root.readRequired<bool>( "on" ).value_or( false ) );
    SW_EXPECT_EQUAL( sw::string( "a" ), root.readRequired<sw::string>( "label" ).value_or( sw::string{} ) );
    SW_EXPECT_EQUAL( int64( 5000000000 ), root.readRequired<int64>( "big" ).value_or( 0 ) );

    std::optional<int32> optional;
    SW_EXPECT_TRUE( root.readOptional( "none", optional ) );
    SW_EXPECT_FALSE( optional.has_value() );
    SW_EXPECT_TRUE( root.readOptional( "absent", optional ) );
    SW_EXPECT_FALSE( optional.has_value() );
    {
        SW_TEST_DEFENSIVE_SCOPE( "JSON values of the wrong type" );
        SW_EXPECT_FALSE( root.readRequired<int32>( "fraction" ).has_value() );
        SW_EXPECT_FALSE( root.readRequired<int32>( "big" ).has_value() );
        SW_EXPECT_FALSE( root.readRequired<int32>( "text" ).has_value() );
        SW_EXPECT_FALSE( root.readRequired<bool>( "count" ).has_value() );
        SW_EXPECT_FALSE( root.readRequired<int32>( "absent" ).has_value() );
        SW_EXPECT_FALSE( root.readOptional( "fraction", optional ) );
    }
}

/**
 * @brief [RequiredOptionalReadTest] key=value 의 필수 · 선택 읽기 — 빈 값은 없음, 깨진 값은 오류다
 */
SW_TEST_CASE( RequiredOptionalReadTest, KeyValueFile )
{
    sw::KeyValueMap map;
    SW_ASSERT_TRUE( sw::KeyValueFile::parse( "width=1280\nscale=1.25\nfullscreen=on\nname=Hero\nempty=\nbroken=wide\n", map ) );
    SW_EXPECT_EQUAL( 1280, sw::KeyValueFile::readRequired<int32>( map, "width" ).value_or( 0 ) );
    SW_EXPECT_NEAR_EQUAL( 1.25f, sw::KeyValueFile::readRequired<float32>( map, "scale" ).value_or( 0.0f ), 1e-6f );
    SW_EXPECT_TRUE( sw::KeyValueFile::readRequired<bool>( map, "fullscreen" ).value_or( false ) );
    SW_EXPECT_EQUAL( sw::string( "Hero" ), sw::KeyValueFile::readRequired<sw::string>( map, "name" ).value_or( sw::string{} ) );

    std::optional<int32> optional;
    SW_EXPECT_TRUE( sw::KeyValueFile::readOptional( map, "empty", optional ) );
    SW_EXPECT_FALSE( optional.has_value() );
    {
        SW_TEST_DEFENSIVE_SCOPE( "missing and unreadable key=value entries" );
        SW_EXPECT_FALSE( sw::KeyValueFile::readRequired<int32>( map, "height" ).has_value() );
        SW_EXPECT_FALSE( sw::KeyValueFile::readRequired<int32>( map, "broken" ).has_value() );
        SW_EXPECT_FALSE( sw::KeyValueFile::readOptional( map, "broken", optional ) );
    }
}
