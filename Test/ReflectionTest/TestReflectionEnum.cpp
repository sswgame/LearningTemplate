#include "pch.h"

#include "Core/Common/EnumUtil.h"

#include "Engine/Common/EngineServices.h"
#include "Engine/Reflection/ReflectionCore.h"
#include "Engine/Reflection/ReflectionEnumNames.h"

#include "ReflectionTest/TestReflectionFixtures.h"
#include "ReflectionTest/TestSampleActor.h"

#include "TestFramework/TestFramework.h"

// 리플렉션 열거형 — EnumInfo 등록·조회 · 플래그 연산 · 비트플래그 문자열 · 이름 테이블.
/**
 * @brief [ReflectionEnumBitFlagTest] 비트플래그 감지와 ToString
 */

SW_TEST_CASE( ReflectionEnumBitFlagTest, BitFlagDetectionAndToString )
{
    const sw::EnumInfo* info =
        sw::engine::getTypeRegistry().findEnum( sw::hashed_string( "sw::DummyBitFlag" ) );

    SW_EXPECT_TRUE( info != nullptr );
    if ( info == nullptr )
        return;

    SW_EXPECT_TRUE( info->_bIsBitFlag );

    int64             flagVal = static_cast<int64>( sw::DummyBitFlag::OptionA ) | static_cast<int64>( sw::DummyBitFlag::OptionC );
    sw::hashed_string flagStr = info->toStringFlags( flagVal );

    SW_EXPECT_EQUAL( sw::string( "OptionA | OptionC" ), sw::string( flagStr.c_str() ) );
}

/**
 * @brief [ReflectionEnumBitFlagTest] 문자열 플래그 → 값
 */
SW_TEST_CASE( ReflectionEnumBitFlagTest, StringFlagsToValue )
{
    const sw::EnumInfo* info =
        sw::engine::getTypeRegistry().findEnum( sw::hashed_string( "sw::DummyBitFlag" ) );

    SW_EXPECT_TRUE( info != nullptr );
    if ( info == nullptr )
        return;

    int64 val      = info->stringFlagsToValue( "OptionB | OptionC" );
    int64 expected = static_cast<int64>( sw::DummyBitFlag::OptionB ) | static_cast<int64>( sw::DummyBitFlag::OptionC );

    SW_EXPECT_EQUAL( expected, val );
}

/**
 * @brief [ReflectionEnumInfoTest] 등록된 enum 조회
 */
SW_TEST_CASE( ReflectionEnumInfoTest, FindRegisteredEnum )
{
    const sw::EnumInfo* info =
        sw::engine::getTypeRegistry().findEnum( sw::hashed_string( "sw::DummyType" ) );

    SW_EXPECT_TRUE( info != nullptr );
    if ( info == nullptr )
        return;

    SW_EXPECT_EQUAL( sw::string( "DummyType" ), sw::string( info->_name.c_str() ) );
}

/**
 * @brief [ReflectionEnumInfoTest] 값 → 문자열
 */
SW_TEST_CASE( ReflectionEnumInfoTest, ValueToString )
{
    const sw::EnumInfo* info =
        sw::engine::getTypeRegistry().findEnum( sw::hashed_string( "sw::DummyType" ) );
    SW_EXPECT_TRUE( info != nullptr );
    if ( info == nullptr )
        return;

    SW_EXPECT_EQUAL( sw::string( "None" ), sw::string( info->toString( 0 ).c_str() ) );
    SW_EXPECT_EQUAL( sw::string( "TypeA" ), sw::string( info->toString( 1 ).c_str() ) );
    SW_EXPECT_EQUAL( sw::string( "TypeB" ), sw::string( info->toString( 2 ).c_str() ) );
}

/**
 * @brief [ReflectionEnumInfoTest] 잘못된 값은 기본값
 */
SW_TEST_CASE( ReflectionEnumInfoTest, InvalidValueReturnsDefault )
{
    const sw::EnumInfo* info =
        sw::engine::getTypeRegistry().findEnum( sw::hashed_string( "sw::DummyType" ) );
    SW_EXPECT_TRUE( info != nullptr );
    if ( info == nullptr )
        return;

    sw::hashed_string name = info->toString( 999 );
    SW_EXPECT_TRUE( name == sw::hashed_string() );
}

/**
 * @brief [ReflectionEnumInfoTest] EnumInfo 플래그 문자열 변환
 */
SW_TEST_CASE( ReflectionEnumInfoTest, EnumInfoFlagsStringConversion )
{
    sw::EnumInfo info;
    info._name           = sw::hashed_string( "ESampleFlags" );
    info._bIsBitFlag     = SW_TRUE;
    info._mapNameToValue = {
        { sw::hashed_string( "None" ), 0},
        {sw::hashed_string( "FlagA" ), 1},
        {sw::hashed_string( "FlagB" ), 2},
        {sw::hashed_string( "FlagC" ), 4}
    };
    info._mapValueToName = {
        {0,  sw::hashed_string( "None" )},
        {1, sw::hashed_string( "FlagA" )},
        {2, sw::hashed_string( "FlagB" )},
        {4, sw::hashed_string( "FlagC" )}
    };

    int64 val = info.stringFlagsToValue( "FlagA | FlagC" );
    SW_EXPECT_EQUAL( 5, val );

    sw::hashed_string flagsStr = info.toStringFlags( 5 );
    SW_EXPECT_TRUE( flagsStr.empty() == false );
}

/**
 * @brief [ReflectionEnumFlagTest] enum 플래그 연산자
 */
SW_TEST_CASE( ReflectionEnumFlagTest, EnumFlagOperators )
{
    TestFlag flag = TestFlag::Read | TestFlag::Write;
    SW_EXPECT_TRUE( sw::engine::getTypeRegistry().hasFlag( flag, TestFlag::Read ) );
    SW_EXPECT_TRUE( sw::engine::getTypeRegistry().hasFlag( flag, TestFlag::Write ) );
    SW_EXPECT_FALSE( sw::engine::getTypeRegistry().hasFlag( flag, TestFlag::Execute ) );

    flag |= TestFlag::Execute;
    SW_EXPECT_TRUE( sw::engine::getTypeRegistry().hasFlag( flag, TestFlag::Execute ) );
}

/**
 * @brief [ReflectionEnumFlagTest] ENUM(Flags) 코드젠 연산자(|, &, ^, ~, |=, &=, ^=)가 sw::EnumUtil의
 *        제네릭 hasFlag/hasAnyFlag/setFlag/clearFlag(Core/Common/EnumUtil.h)와 함께 정상 동작하는지
 *        검증합니다. EnumUtil 자체의 단위 테스트는 Test/CoreTest/TestEnumUtil.cpp에 있습니다
 *        (리플렉션과 무관하게 동작함을 증명하기 위해 일부러 CoreTest에 둡니다).
 */
SW_TEST_CASE( ReflectionEnumFlagTest, GeneratedOperatorsWithEnumUtil )
{
    TestFlag flag = TestFlag::Read | TestFlag::Write;

    SW_EXPECT_TRUE( sw::EnumUtil::hasFlag( flag, TestFlag::Read ) );
    SW_EXPECT_TRUE( sw::EnumUtil::hasFlag( flag, TestFlag::Write ) );
    SW_EXPECT_FALSE( sw::EnumUtil::hasFlag( flag, TestFlag::Execute ) );

    flag |= TestFlag::Execute;
    SW_EXPECT_TRUE( sw::EnumUtil::hasFlag( flag, TestFlag::Execute ) );

    flag = sw::EnumUtil::clearFlag( flag, TestFlag::Read );
    SW_EXPECT_FALSE( sw::EnumUtil::hasFlag( flag, TestFlag::Read ) );
    SW_EXPECT_TRUE( sw::EnumUtil::hasAnyFlag( flag, TestFlag::Write | TestFlag::Execute ) );
}

/**
 * @brief [ReflectionEnumNamesTest] ContainerKind 및 FunctionNetRole 이름 변환 및 파싱 검증
 */
SW_TEST_CASE( ReflectionEnumNamesTest, ContainerKindAndNetRoleNames )
{
    // 1) ContainerKind 변환 및 파싱
    sw::ContainerKind parsedKind = sw::ContainerKind::None;
    SW_EXPECT_TRUE( sw::tryParseContainerKind( "Sequence", parsedKind ) );
    SW_EXPECT_EQUAL( static_cast<uint32>( sw::ContainerKind::Sequence ), static_cast<uint32>( parsedKind ) );

    SW_EXPECT_TRUE( sw::tryParseContainerKind( "Map", parsedKind ) );
    SW_EXPECT_EQUAL( static_cast<uint32>( sw::ContainerKind::Map ), static_cast<uint32>( parsedKind ) );

    SW_EXPECT_FALSE( sw::tryParseContainerKind( "InvalidKind", parsedKind ) );

    SW_EXPECT_STREQ( "Sequence", sw::toString( sw::ContainerKind::Sequence ) );
    SW_EXPECT_STREQ( "Map", sw::toString( sw::ContainerKind::Map ) );
    SW_EXPECT_STREQ( "sw::ContainerKind::Sequence", sw::toCppExpr( sw::ContainerKind::Sequence ) );
    SW_EXPECT_STREQ( "Vector", sw::defaultContainerWrapperStem( sw::ContainerKind::Sequence ) );
    SW_EXPECT_STREQ( "Map", sw::defaultContainerWrapperStem( sw::ContainerKind::Map ) );

    // 2) FunctionNetRole 변환 및 파싱
    sw::FunctionNetRole parsedRole = sw::FunctionNetRole::Local;
    SW_EXPECT_TRUE( sw::tryParseFunctionNetRole( "Server", parsedRole ) );
    SW_EXPECT_EQUAL( static_cast<uint32>( sw::FunctionNetRole::Server ), static_cast<uint32>( parsedRole ) );

    SW_EXPECT_TRUE( sw::tryParseFunctionNetRole( "Client", parsedRole ) );
    SW_EXPECT_EQUAL( static_cast<uint32>( sw::FunctionNetRole::Client ), static_cast<uint32>( parsedRole ) );

    SW_EXPECT_FALSE( sw::tryParseFunctionNetRole( "InvalidRole", parsedRole ) );

    SW_EXPECT_STREQ( "Server", sw::toString( sw::FunctionNetRole::Server ) );
    SW_EXPECT_STREQ( "Client", sw::toString( sw::FunctionNetRole::Client ) );
    SW_EXPECT_STREQ( "sw::FunctionNetRole::Server", sw::toCppExpr( sw::FunctionNetRole::Server ) );
}
