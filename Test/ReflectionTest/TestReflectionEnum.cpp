#include "pch.h"

#include "Core/Common/EnumUtil.h"

#include "Engine/Common/EngineServices.h"
#include "Engine/Reflection/ReflectionCore.h"
#include "Engine/Reflection/ReflectionEnumNames.h"
#include "Engine/Serialization/Format/JsonSerializer.h"
#include "Engine/Serialization/Format/XmlSerializer.h"

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

    int64 val{ 0 };
    SW_EXPECT_TRUE( info->tryParseText( "OptionB | OptionC", val ) );
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

    int64 val{ 0 };
    SW_EXPECT_TRUE( info.tryParseText( "FlagA | FlagC", val ) );
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

/**
 * @brief [ReflectionEnumBitFlagTest] 평범한 연속 열거형은 비트플래그로 등록되지 않는다
 * @details 자동 감지는 "0 이 아닌 값이 모두 2의 거듭제곱" 이었는데, 그 조건은 `{ 0, 1, 2 }` 같은
 *          **평범한 연속 열거형**에도 그대로 맞는다. 그래서 `CameraRole` · `PackEncryptionType` ·
 *          `SampleStatus` 셋이 비트플래그로 등록돼 있었다 — 문자열 변환이 `toStringFlags` 로 가고
 *          인스펙터가 콤보 대신 체크박스를 그린다. 값이 셋 이상이어야 켜지게 바꿨다(1·2·4 처럼
 *          연속 열거형이라면 있어야 할 3 이 빠진 모양이라야 한다).
 *
 *          `ENUM( Flags )` 를 명시한 열거형은 값 모양과 무관하게 계속 비트플래그다 — 아래에서
 *          그쪽도 함께 본다.
 */
SW_TEST_CASE( ReflectionEnumBitFlagTest, PlainSequentialEnumIsNotABitFlag )
{
    const utf8* arrPlainEnum[] = { "sw::CameraRole", "sw::PackEncryptionType", "sw::PackCompressionType", "sw::SampleStatus" };
    for ( const utf8* pName : arrPlainEnum )
    {
        const sw::EnumInfo* info = sw::engine::getTypeRegistry().findEnum( sw::hashed_string( pName ) );
        SW_ASSERT_NOT_NULL( info );
        SW_EXPECT_TRUE_MSG( info->_bIsBitFlag == SW_FALSE,
                            "연속 열거형이 비트플래그로 등록됐습니다 — 인스펙터와 문자열 변환이 달라집니다" );
    }

    // 명시한 쪽은 그대로여야 한다.
    const sw::EnumInfo* pFlagInfo = sw::engine::getTypeRegistry().findEnum( sw::hashed_string( "sw::PackFlag" ) );
    SW_ASSERT_NOT_NULL( pFlagInfo );
    SW_EXPECT_TRUE( pFlagInfo->_bIsBitFlag != SW_FALSE );
}

/**
 * @brief [ReflectionEnumInfoTest] 좁은 enum 의 높은 비트 · 음수 값도 메모리에서 읽은 값과 이름표의 값이 같다
 * @details 코드젠은 열거자 값을 libclang 의 **부호 있는** 값으로 적었고, 런타임은 1 · 2 바이트 enum 을 **부호 없이** 읽었다. 둘이
 *          엇갈린 값은 이름을 잃었다 — `ShaderStageFlag::Amplification`(0x80)은 이름표에 -128 로, 메모리에서는 128 로 읽혀
 *          `toString` 이 비었고 `All` 을 저장하면 그 비트가 빠졌다. 부호 있는 좁은 enum 의 음수는 반대로 어긋났다.
 */
SW_TEST_CASE( ReflectionEnumInfoTest, NarrowEnumValuesMatchTheirNamesInMemory )
{
    const sw::TypeRegistry& registry = sw::engine::getTypeRegistry();

    const auto expectRoundTrip = [&registry]( const utf8* pEnumName, auto value, const utf8* pExpectedName )
    {
        const sw::EnumInfo* pInfo = registry.findEnum( sw::hashed_string( pEnumName ) );
        SW_ASSERT_NOT_NULL( pInfo );
        auto        storage = value;
        const int64 read    = pInfo->readValueFromMemory( &storage );
        SW_EXPECT_EQUAL( static_cast<int64>( value ), read );
        SW_EXPECT_STREQ( pExpectedName, pInfo->toString( read ).c_str() );

        auto written = decltype( value ){};
        pInfo->writeValueToMemory( &written, read );
        SW_EXPECT_TRUE( written == value );
    };

    expectRoundTrip( "TestHighBitEnum", TestHighBitEnum::Low, "Low" );
    expectRoundTrip( "TestHighBitEnum", TestHighBitEnum::High, "High" );
    expectRoundTrip( "TestHighBitEnum", TestHighBitEnum::Max, "Max" );
    expectRoundTrip( "TestSignedNarrowEnum", TestSignedNarrowEnum::Negative, "Negative" );
    expectRoundTrip( "TestSignedNarrowEnum", TestSignedNarrowEnum::Positive, "Positive" );

    // 엔진의 실제 예: 셰이더 단계 플래그의 맨 위 비트
    const sw::EnumInfo* pStage = registry.findEnum( sw::hashed_string( "ShaderStageFlag" ) );
    SW_ASSERT_NOT_NULL( pStage );
    const uint8 amplification = 0x80;
    SW_EXPECT_STREQ( "Amplification", pStage->toString( pStage->readValueFromMemory( &amplification ) ).c_str() );
    const uint8 all = 0xFF;
    SW_EXPECT_TRUE( pStage->toStringFlags( pStage->readValueFromMemory( &all ) ).view().find( "Amplification" ) != sw::string_view::npos );
}

/**
 * @brief [ReflectionEnumInfoTest] 텍스트를 enum 으로 읽을 때 모르는 이름은 실패다 — 조용히 0 이 되지 않는다
 * @details 예전 직렬화는 모르는 이름 · 대소문자만 다른 이름 · 숫자를 모두 0 으로 읽어 썼다(orphan 도 로그도 없이). 이제 이름은 대소문자를 가리지
 *          않고, 알려진 값의 숫자도 받고, 비트플래그는 토큰마다 알려진 이름이어야 한다.
 */
SW_TEST_CASE( ReflectionEnumInfoTest, TextParseRejectsUnknownNamesInsteadOfZero )
{
    const sw::TypeRegistry& registry = sw::engine::getTypeRegistry();
    const sw::EnumInfo*     pRole    = registry.findEnum( sw::hashed_string( "CameraRole" ) );
    const sw::EnumInfo*     pFlag    = registry.findEnum( sw::hashed_string( "TestFlag" ) );
    SW_ASSERT_NOT_NULL( pRole );
    SW_ASSERT_NOT_NULL( pFlag );

    int64 value = -1;
    SW_EXPECT_TRUE( pRole->tryParseText( "Editor", value ) && value == 1 );
    SW_EXPECT_TRUE( pRole->tryParseText( "editor", value ) && value == 1 ); // 대소문자 무시
    SW_EXPECT_TRUE( pRole->tryParseText( " 2 ", value ) && value == 2 );    // 알려진 값의 숫자
    SW_EXPECT_FALSE( pRole->tryParseText( "Bogus", value ) );
    SW_EXPECT_FALSE( pRole->tryParseText( "99", value ) );
    SW_EXPECT_FALSE( pRole->tryParseText( "", value ) );

    SW_EXPECT_TRUE( pFlag->tryParseText( "Read | Write", value ) && value == 3 );
    SW_EXPECT_TRUE( pFlag->tryParseText( "execute", value ) && value == 4 );
    SW_EXPECT_FALSE( pFlag->tryParseText( "Read | Bogus", value ) );
    SW_EXPECT_FALSE( pFlag->tryParseText( "64", value ) ); // 모르는 비트

    // 직렬화 경로(JSON · XML): 못 읽으면 그 필드는 쓰지 않는다(값은 그대로 — 예전에는 0 이 됐다). 대소문자만 다른 이름은 읽는다.
    const sw::TypeInfo* pHostType = registry.findType( sw::hashed_string( "sw::NarrowEnumHost" ) );
    SW_ASSERT_NOT_NULL( pHostType );
    {
        test::ScopedDefensiveTestLog expected( "enum text that names no enumerator" );
        sw::NarrowEnumHost           fromJson;
        fromJson._mode = sw::NarrowEnum::One;
        (void)sw::JsonSerializer::deserialize( &fromJson, *pHostType, R"({"_mode":"Bogus"})" );
        SW_EXPECT_TRUE( fromJson._mode == sw::NarrowEnum::One );

        sw::NarrowEnumHost source;
        source._mode         = sw::NarrowEnum::Two;
        sw::string   xml     = sw::XmlSerializer::serialize( &source, *pHostType );
        const size_t namePos = xml.find( "\"Two\"" );
        SW_ASSERT_TRUE_MSG( namePos != sw::string::npos, xml.c_str() );
        xml.replace( namePos, 5, "\"Bogus\"" );
        sw::NarrowEnumHost fromXml;
        fromXml._mode = sw::NarrowEnum::One;
        (void)sw::XmlSerializer::deserialize( &fromXml, *pHostType, xml );
        SW_EXPECT_TRUE( fromXml._mode == sw::NarrowEnum::One );
    }
    sw::NarrowEnumHost caseInsensitive;
    SW_EXPECT_TRUE( sw::JsonSerializer::deserialize( &caseInsensitive, *pHostType, R"({"_mode":"two"})" ) );
    SW_EXPECT_TRUE( caseInsensitive._mode == sw::NarrowEnum::Two );

    // 쓰는 쪽이 적는 글은 모두 다시 읽힌다 — 등록된 모든 enum 의 모든 값과, 비트플래그의 0(`None`) · 모든 비트 합.
    sw::string report;
    registry.forEachEnum( [&report]( const sw::EnumInfo& info )
    {
        const auto checkRoundTrip = [&report, &info]( int64 expected )
        {
            const sw::hashed_string written = info._bIsBitFlag ? info.toStringFlags( expected ) : info.toString( expected );
            int64                   parsed  = -1;
            if ( info.tryParseText( written.c_str(), parsed ) == false || parsed != expected )
                report += sw::string( "\n  " ) + info._fullyQualifiedName.c_str() + " " + sw::to_string( expected ) + " -> '" + written.c_str() + "'";
        };
        int64 allBits = 0;
        for ( const auto& [enumValue, name] : info._mapValueToName )
        {
            checkRoundTrip( enumValue );
            allBits |= enumValue;
        }
        if ( info._bIsBitFlag )
        {
            checkRoundTrip( 0 );
            checkRoundTrip( allBits );
        }
    } );
    SW_EXPECT_TRUE_MSG( report.empty(), ( "enum text that does not read back:" + report ).c_str() );
}

/**
 * @brief [ReflectionEnumInfoTest] `findEnum` 이 준 포인터는 enum 이 더 등록돼도 그대로이고, 이름 · FQN · 별칭이 같은 객체를 가리킨다
 * @details 레지스트리는 EnumInfo 를 밀집 배열 해시맵에 **값으로** 들었다. enum 이 하나 더 오르며 배열이 커지면 모든 EnumInfo 가 옮겨져, 워커가 씬을
 *          읽으며 들고 있던 포인터가 그 자리에서 죽었다. 짧은 이름 · 별칭은 각자 복사본이라, 다시 등록돼 열거자가 늘어도 별칭으로 찾으면 옛 목록이었다.
 */
SW_TEST_CASE( ReflectionEnumInfoTest, EnumInfoAddressIsStableAndShared )
{
    sw::TypeRegistry&   registry = sw::engine::getTypeRegistry();
    const sw::EnumInfo* pRole    = registry.findEnum( sw::hashed_string( "sw::CameraRole" ) );
    SW_ASSERT_NOT_NULL( pRole );
    SW_EXPECT_TRUE( registry.findEnum( sw::hashed_string( "CameraRole" ) ) == pRole );

    const auto makeEnum = []( const sw::string& fqn, int64 valueCount )
    {
        sw::EnumInfo info;
        info._fullyQualifiedName = sw::hashed_string( fqn.c_str() );
        info._name               = sw::hashed_string( fqn.substr( fqn.rfind( ':' ) + 1 ).c_str() );
        info._moduleName         = sw::hashed_string( "TestEnumGrowth" );
        info._size               = 1;
        for ( int64 value = 0; value < valueCount; ++value )
        {
            const sw::hashed_string name( ( "V" + sw::to_string( value ) ).c_str() );
            info._mapNameToValue.insert_or_assign( name, value );
            info._mapValueToName.insert_or_assign( value, name );
        }
        return info;
    };

    // 많이 올려 밀집 배열이 여러 번 커지게 한다.
    for ( int32 index = 0; index < 256; ++index )
        registry.registerEnum( makeEnum( "swtest::GrowthEnum" + sw::to_string( index ), 1 ) );
    SW_EXPECT_TRUE( registry.findEnum( sw::hashed_string( "sw::CameraRole" ) ) == pRole );

    // 별칭은 같은 객체다 — 다시 등록돼 열거자가 늘면 별칭으로 찾아도 늘어 있다.
    registry.registerEnumAlias( "OldGrowthEnum0", "swtest::GrowthEnum0" );
    const sw::EnumInfo* pGrowth = registry.findEnum( sw::hashed_string( "swtest::GrowthEnum0" ) );
    SW_ASSERT_NOT_NULL( pGrowth );
    SW_EXPECT_TRUE( registry.findEnum( sw::hashed_string( "OldGrowthEnum0" ) ) == pGrowth );
    registry.registerEnum( makeEnum( "swtest::GrowthEnum0", 3 ) );
    SW_EXPECT_TRUE( registry.findEnum( sw::hashed_string( "swtest::GrowthEnum0" ) ) == pGrowth );
    const sw::EnumInfo* pByAlias = registry.findEnum( sw::hashed_string( "OldGrowthEnum0" ) );
    SW_ASSERT_NOT_NULL( pByAlias );
    SW_EXPECT_EQUAL( 3u, static_cast<uint32>( pByAlias->_mapValueToName.size() ) );

#if !defined( SW_SHIPPING )
    // 모듈이 내려가면 이름으로는 못 찾고, 다시 오르면 같은 자리에 되살아난다.
    registry.unregisterTypesByModule( "TestEnumGrowth" );
    SW_EXPECT_NULL( registry.findEnum( sw::hashed_string( "swtest::GrowthEnum0" ) ) );
    SW_EXPECT_NULL( registry.findEnum( sw::hashed_string( "OldGrowthEnum0" ) ) );
    registry.registerEnum( makeEnum( "swtest::GrowthEnum0", 2 ) );
    SW_EXPECT_TRUE( registry.findEnum( sw::hashed_string( "swtest::GrowthEnum0" ) ) == pGrowth );
    registry.unregisterTypesByModule( "TestEnumGrowth" );
#endif
}

/**
 * @brief [ReflectionEnumInfoTest] 이름 해시가 같은데 값이 다른 열거자 둘은 등록 때 알린다 — 같은 값의 별칭은 괜찮다
 * @details 바이너리는 enum 을 열거자 이름 해시(대소문자 무시)로 싣는다. `Red` · `RED` 가 다른 값이면 저장된 데이터가 어느 쪽으로 읽힐지 정해지지 않는데,
 *          등록이 그것을 보지 않았다.
 */
SW_TEST_CASE( ReflectionEnumInfoTest, EnumeratorsWhoseNameHashesClashAreReported )
{
    sw::TypeRegistry& registry = sw::engine::getTypeRegistry();
    const auto        makeEnum = []( const utf8* pFqn, std::initializer_list<std::pair<const utf8*, int64>> listEnumerator )
    {
        sw::EnumInfo info;
        info._fullyQualifiedName = sw::hashed_string( pFqn );
        info._name               = info._fullyQualifiedName;
        info._moduleName         = sw::hashed_string( "TestEnumClash" );
        info._size               = 1;
        for ( const auto& [pName, value] : listEnumerator )
        {
            const sw::hashed_string name( pName );
            info._mapNameToValue.insert_or_assign( name, value );
            info._mapValueToName.try_emplace( value, name );
        }
        return info;
    };

    test::ScopedLogCollector logs;
    registry.registerEnum( makeEnum( "swtest::ClashFreeEnum", {
                                                                  {    "Red", 0},
                                                                  {    "RED", 0},
                                                                  {"Crimson", 0},
                                                                  {   "Blue", 1}
    } ) ); // 같은 값의 별칭
    SW_EXPECT_TRUE_MSG( logs.countContaining( "same name hash" ) == 0, logs.joined().c_str() );
    {
        test::ScopedDefensiveTestLog expected( "an enum with two enumerators that differ only in case" );
        registry.registerEnum( makeEnum( "swtest::ClashingEnum", {
                                                                     {"Red", 0},
                                                                     {"RED", 1}
        } ) );
    }
    SW_EXPECT_TRUE_MSG( logs.countContaining( "swtest::ClashingEnum has enumerators" ) == 1, logs.joined().c_str() );
#if !defined( SW_SHIPPING )
    registry.unregisterTypesByModule( "TestEnumClash" );
#endif
}
