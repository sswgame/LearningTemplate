#include "pch.h"

#include "Core/Foundation/EnumUtil.h"
#include "Core/Testing/TestFramework.h"

#include "Engine/Foundation/EngineServices.h"
#include "Engine/Reflection/ReflectionEnumNames.h"
#include "Engine/Reflection/ReflectionTypes.h"
#include "Engine/Reflection/Tests/ReflectionTestTypes.h"
#include "Engine/Reflection/TypeRegistry.h"

// ReflectionEnumTest — EnumInfo 등록 · 조회 · 플래그 연산 · 비트플래그 문자열 · 이름 테이블 · 좁은 enum 의 부호.

namespace
{
    const sw::EnumInfo* findEnum( const utf8* pName )
    {
        return sw::engine::getSubsystem<sw::TypeRegistry>().findEnum( sw::hashed_string( pName ) );
    }
} // namespace

/**
 * @brief [ReflectionEnumBitFlagTest] 비트플래그 감지와 문자열 — 켜진 비트의 이름을 ` | ` 로 잇는다
 */
SW_TEST_CASE( ReflectionEnumBitFlagTest, BitFlagDetectionAndToString )
{
    const sw::EnumInfo* pInfo = findEnum( "sw::DummyBitFlag" );
    SW_REQUIRE_NOT_NULL( pInfo );
    SW_EXPECT_TRUE( pInfo->_bIsBitFlag == SW_TRUE );

    const int64 flagValue = static_cast<int64>( sw::DummyBitFlag::OptionA ) | static_cast<int64>( sw::DummyBitFlag::OptionC );
    SW_EXPECT_EQUAL( sw::string( "OptionA | OptionC" ), sw::string( pInfo->toStringFlags( flagValue ).c_str() ) );
}

/**
 * @brief [ReflectionEnumBitFlagTest] 비트플래그 글 → 값
 */
SW_TEST_CASE( ReflectionEnumBitFlagTest, StringFlagsToValue )
{
    const sw::EnumInfo* pInfo = findEnum( "sw::DummyBitFlag" );
    SW_REQUIRE_NOT_NULL( pInfo );

    int64 value{ 0 };
    SW_EXPECT_TRUE( pInfo->tryParseText( "OptionB | OptionC", value ) );
    SW_EXPECT_EQUAL( static_cast<int64>( sw::DummyBitFlag::OptionB ) | static_cast<int64>( sw::DummyBitFlag::OptionC ), value );
}

/**
 * @brief [ReflectionEnumInfoTest] 등록된 enum 을 FQN 으로 찾고, 값 → 이름, 모르는 값은 빈 이름
 */
SW_TEST_CASE( ReflectionEnumInfoTest, FindRegisteredEnumAndValueToString )
{
    const sw::EnumInfo* pInfo = findEnum( "sw::DummyType" );
    SW_REQUIRE_NOT_NULL( pInfo );
    SW_EXPECT_EQUAL( sw::string( "DummyType" ), sw::string( pInfo->_name.c_str() ) );
    SW_EXPECT_EQUAL( sw::string( "None" ), sw::string( pInfo->toString( 0 ).c_str() ) );
    SW_EXPECT_EQUAL( sw::string( "TypeA" ), sw::string( pInfo->toString( 1 ).c_str() ) );
    SW_EXPECT_EQUAL( sw::string( "TypeB" ), sw::string( pInfo->toString( 2 ).c_str() ) );
    SW_EXPECT_TRUE( pInfo->toString( 999 ) == sw::hashed_string() );
}

/**
 * @brief [ReflectionEnumInfoTest] 손으로 채운 EnumInfo 도 플래그 글을 읽고 쓴다(레지스트리에 올리지 않은 표)
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

    int64 value{ 0 };
    SW_EXPECT_TRUE( info.tryParseText( "FlagA | FlagC", value ) );
    SW_EXPECT_EQUAL( 5, value );
    SW_EXPECT_FALSE( info.toStringFlags( 5 ).empty() );
}

/**
 * @brief [ReflectionEnumFlagTest] ENUM(Flags) 연산자(|, |=)가 레지스트리의 `hasFlag` 와 `EnumUtil` 의 제네릭 함수와 함께 돈다
 */
SW_TEST_CASE( ReflectionEnumFlagTest, GeneratedOperatorsWithRegistryAndEnumUtil )
{
    const sw::TypeRegistry& registry = sw::engine::getSubsystem<sw::TypeRegistry>();
    sw::TestFlag            flag     = sw::TestFlag::Read | sw::TestFlag::Write;
    SW_EXPECT_TRUE( registry.hasFlag( flag, sw::TestFlag::Read ) );
    SW_EXPECT_TRUE( registry.hasFlag( flag, sw::TestFlag::Write ) );
    SW_EXPECT_FALSE( registry.hasFlag( flag, sw::TestFlag::Execute ) );
    SW_EXPECT_TRUE( sw::EnumUtil::hasFlag( flag, sw::TestFlag::Read ) );
    SW_EXPECT_FALSE( sw::EnumUtil::hasFlag( flag, sw::TestFlag::Execute ) );

    flag |= sw::TestFlag::Execute;
    SW_EXPECT_TRUE( registry.hasFlag( flag, sw::TestFlag::Execute ) );
    SW_EXPECT_TRUE( sw::EnumUtil::hasFlag( flag, sw::TestFlag::Execute ) );

    flag = sw::EnumUtil::clearFlag( flag, sw::TestFlag::Read );
    SW_EXPECT_FALSE( sw::EnumUtil::hasFlag( flag, sw::TestFlag::Read ) );
    SW_EXPECT_TRUE( sw::EnumUtil::hasAnyFlag( flag, sw::TestFlag::Write | sw::TestFlag::Execute ) );
}

/**
 * @brief [ReflectionEnumNamesTest] ContainerKind · FunctionNetRole 이름 변환과 파싱
 */
SW_TEST_CASE( ReflectionEnumNamesTest, ContainerKindAndNetRoleNames )
{
    sw::ContainerKind parsedKind = sw::ContainerKind::None;
    SW_EXPECT_TRUE( sw::tryParseContainerKind( "Sequence", parsedKind ) );
    SW_EXPECT_TRUE( parsedKind == sw::ContainerKind::Sequence );
    SW_EXPECT_TRUE( sw::tryParseContainerKind( "Map", parsedKind ) );
    SW_EXPECT_TRUE( parsedKind == sw::ContainerKind::Map );
    SW_EXPECT_FALSE( sw::tryParseContainerKind( "InvalidKind", parsedKind ) );

    SW_EXPECT_EQUAL( sw::string( "Sequence" ), sw::string( sw::toString( sw::ContainerKind::Sequence ) ) );
    SW_EXPECT_EQUAL( sw::string( "Map" ), sw::string( sw::toString( sw::ContainerKind::Map ) ) );
    SW_EXPECT_EQUAL( sw::string( "sw::ContainerKind::Sequence" ), sw::string( sw::toCppExpr( sw::ContainerKind::Sequence ) ) );
    SW_EXPECT_EQUAL( sw::string( "Vector" ), sw::string( sw::defaultContainerWrapperStem( sw::ContainerKind::Sequence ) ) );
    SW_EXPECT_EQUAL( sw::string( "Map" ), sw::string( sw::defaultContainerWrapperStem( sw::ContainerKind::Map ) ) );

    sw::FunctionNetRole parsedRole = sw::FunctionNetRole::Local;
    SW_EXPECT_TRUE( sw::tryParseFunctionNetRole( "Server", parsedRole ) );
    SW_EXPECT_TRUE( parsedRole == sw::FunctionNetRole::Server );
    SW_EXPECT_TRUE( sw::tryParseFunctionNetRole( "Client", parsedRole ) );
    SW_EXPECT_TRUE( parsedRole == sw::FunctionNetRole::Client );
    SW_EXPECT_FALSE( sw::tryParseFunctionNetRole( "InvalidRole", parsedRole ) );

    SW_EXPECT_EQUAL( sw::string( "Server" ), sw::string( sw::toString( sw::FunctionNetRole::Server ) ) );
    SW_EXPECT_EQUAL( sw::string( "Client" ), sw::string( sw::toString( sw::FunctionNetRole::Client ) ) );
    SW_EXPECT_EQUAL( sw::string( "sw::FunctionNetRole::Server" ), sw::string( sw::toCppExpr( sw::FunctionNetRole::Server ) ) );
}

/**
 * @brief [ReflectionEnumBitFlagTest] 평범한 연속 열거형은 비트플래그로 등록되지 않는다 — 값 모양(0 이 아닌 값이 모두 2 의 거듭제곱)으로 감지하지 않는다
 * @details 값 모양으로 감지하면 `{ 0, 1, 2 }` 같은 연속 열거형이 비트플래그가 되어 문자열 변환이 `toStringFlags` 로 가고 인스펙터가 콤보 대신 체크박스를 그린다.
 *          `ENUM( Flags )` 를 적은 열거형은 값 모양과 무관하게 비트플래그다.
 */
SW_TEST_CASE( ReflectionEnumBitFlagTest, PlainSequentialEnumIsNotABitFlag )
{
    for ( const utf8* pName : { "sw::CameraRole", "sw::PackEncryptionType", "sw::PackCompressionType", "sw::SampleStatus" } )
    {
        const sw::EnumInfo* pInfo = findEnum( pName );
        SW_REQUIRE_NOT_NULL( pInfo );
        SW_EXPECT_TRUE_MSG( pInfo->_bIsBitFlag == SW_FALSE, pName );
    }

    const sw::EnumInfo* pFlagInfo = findEnum( "sw::PackFlag" );
    SW_REQUIRE_NOT_NULL( pFlagInfo );
    SW_EXPECT_TRUE( pFlagInfo->_bIsBitFlag != SW_FALSE );
}

/**
 * @brief [ReflectionEnumInfoTest] 좁은 enum 의 높은 비트 · 음수 값도 메모리에서 읽은 값과 이름의 값이 같다
 * @details 코드젠이 열거자 값을 부호 있는 값으로 적고 런타임이 1 · 2 바이트 enum 을 부호 없이 읽으면, 둘이 엇갈린 값은 이름을 잃는다
 *          (`ShaderStageFlag::Amplification`(0x80)이 이름에 -128 로, 메모리에서는 128 로 읽혀 `toString` 이 빈다). 부호 있는 좁은 enum 의 음수는 반대로 어긋난다.
 */
SW_TEST_CASE( ReflectionEnumInfoTest, NarrowEnumValuesMatchTheirNamesInMemory )
{
    const auto expectRoundTrip = []( const utf8* pEnumName, auto value, const utf8* pExpectedName )
    {
        const sw::EnumInfo* pInfo = findEnum( pEnumName );
        SW_REQUIRE_NOT_NULL( pInfo );
        auto        storage = value;
        const int64 read    = pInfo->readValueFromMemory( &storage );
        SW_EXPECT_EQUAL( static_cast<int64>( value ), read );
        SW_EXPECT_EQUAL( sw::string( pExpectedName ), sw::string( pInfo->toString( read ).c_str() ) );

        auto written = decltype( value ){};
        pInfo->writeValueToMemory( &written, read );
        SW_EXPECT_TRUE( written == value );
    };

    expectRoundTrip( "sw::TestHighBitEnum", sw::TestHighBitEnum::Low, "Low" );
    expectRoundTrip( "sw::TestHighBitEnum", sw::TestHighBitEnum::High, "High" );
    expectRoundTrip( "sw::TestHighBitEnum", sw::TestHighBitEnum::Max, "Max" );
    expectRoundTrip( "sw::TestSignedNarrowEnum", sw::TestSignedNarrowEnum::Negative, "Negative" );
    expectRoundTrip( "sw::TestSignedNarrowEnum", sw::TestSignedNarrowEnum::Positive, "Positive" );

    // 엔진의 실제 예: 셰이더 단계 플래그의 맨 위 비트
    const sw::EnumInfo* pStage = findEnum( "sw::ShaderStageFlag" );
    SW_REQUIRE_NOT_NULL( pStage );
    const uint8 amplification = 0x80;
    SW_EXPECT_EQUAL( sw::string( "Amplification" ), sw::string( pStage->toString( pStage->readValueFromMemory( &amplification ) ).c_str() ) );
    const uint8 all = 0xFF;
    SW_EXPECT_TRUE( pStage->toStringFlags( pStage->readValueFromMemory( &all ) ).view().find( "Amplification" ) != sw::string_view::npos );
}
