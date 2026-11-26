// CustomizationMigrationTest — 꾸미기 스키마 판 올림(`CustomizationSchemaDef::migrate`): 판마다 단계(Rename · Split · PaletteIndexToColor · Remove)와 음성 대조,
// 더 새 판 거절(원본을 덮지 않는다), 공유 코드 자리 이름 맞춤, 값 넣기 · 정규화의 판 찍기, 스키마 읽기 오류(nogpu).
#include "pch.h"

#include "Core/TestFramework/TestFramework.h"

#include "Engine/Serialization/XML/XMLDocument.h"

#include "GameFramework/Runtime/Gameplay/Appearance/AppearanceTypes.h"
#include "GameFramework/Runtime/Gameplay/Appearance/CustomizationSchema.h"

#include <algorithm>

using namespace sw;

namespace
{
    // 판 0 → 1: Tint 를 Primary 로. 판 1 → 2: 염색 채널 하나(Dye)를 겉 · 안 둘로. 판 2 → 3: 팔레트 번호(Clasp, 슬라이더에 담던 번호)를 색으로, 쓰지 않는 Legacy 를 지운다.
    constexpr const utf8* kSchemaXML = R"(<CustomizationSchemaCatalog>
        <Palette id="Metals"><Swatch name="Iron" color="0.5 0.5 0.5 1"/><Swatch name="Gold" color="1 0.8 0.2 1"/></Palette>
        <Schema id="Cloak" version="3">
          <Migration toVersion="1"><Rename from="Tint" to="Primary"/></Migration>
          <Migration toVersion="2"><Split from="Dye" to="Outer Inner"/></Migration>
          <Migration toVersion="3"><PaletteIndexToColor parameter="Clasp" palette="Metals"/><Remove parameter="Legacy"/></Migration>
          <Color name="Primary" default="1 1 1 1"/>
          <Color name="Outer" default="1 1 1 1"/>
          <Color name="Inner" default="1 1 1 1"/>
          <Color name="Clasp" palette="Metals" default="0.5 0.5 0.5 1"/>
          <Slider name="Shine" min="0" max="1" default="0.5"/>
        </Schema>
        <Schema id="Plain"><Slider name="Shine" min="0" max="1" default="0.5"/></Schema>
    </CustomizationSchemaCatalog>)";

    const float4 kRed{ 0.9f, 0.1f, 0.1f, 1.0f };
    const float4 kBlue{ 0.1f, 0.2f, 0.9f, 1.0f };
    const float4 kGreen{ 0.1f, 0.9f, 0.2f, 1.0f };

    [[nodiscard]] bool loadSchemas( const utf8* pText, CustomizationSchemaCatalog& outCatalog )
    {
        XMLDocument document;
        if ( document.parse( pText ) == false )
            return false;
        AppearanceLoadReport report;
        return outCatalog.loadFromXML( document.getRoot( "CustomizationSchemaCatalog" ), report, "test" );
    }

    /** @brief 공유 코드가 모르는 해시를 두는 자리 이름(`#` + 32 비트 해시 16 진 8 자리)입니다. */
    hashed_string makePlaceholder( const utf8* pName )
    {
        const uint32 hash          = static_cast<uint32>( hashed_string( pName ).getHash() );
        utf8         arrText[10]   = {};
        const utf8   kArrDigit[17] = "0123456789abcdef";
        arrText[0]                 = '#';
        for ( int32 index = 0; index < 8; ++index )
        {
            arrText[1 + index] = kArrDigit[( hash >> ( 28 - 4 * index ) ) & 0xFu];
        }
        return hashed_string( string_view( arrText, 9 ) );
    }

    /** @brief 첫 판(0) 세이브의 값: Tint 빨강, Dye 파랑, Clasp 견본 번호 1(Gold), Legacy, Shine. */
    CustomizationValueSet makeFirstVersionValues()
    {
        CustomizationValueSet values;
        values.setColor( hashed_string( "Tint" ), kRed );
        values.setColor( hashed_string( "Dye" ), kBlue );
        values.setNumber( hashed_string( "Clasp" ), 1.0f );
        values.setNumber( hashed_string( "Legacy" ), 0.3f );
        values.setNumber( hashed_string( "Shine" ), 0.7f );
        values.setSchemaVersion( 0 );
        return values;
    }

    struct CustomizationMigrationTest
    {
        CustomizationSchemaCatalog    _catalog;
        const CustomizationSchemaDef* _pSchema;

        CustomizationMigrationTest()
            : _catalog{}
            , _pSchema{ nullptr }
        {
            SW_REQUIRE_TRUE( loadSchemas( kSchemaXML, _catalog ) );
            _pSchema = _catalog.findSchema( hashed_string( "Cloak" ) );
            SW_REQUIRE_NOT_NULL( _pSchema );
        }
    };

    void expectColor( const CustomizationValueSet& values, const utf8* pName, const float4& expected )
    {
        const CustomizationValue* pValue = values.findValue( hashed_string( pName ) );
        SW_REQUIRE_TRUE_MSG( pValue != nullptr, pName );
        SW_EXPECT_NEAR( expected._x, pValue->_number._x, 1.0e-5f );
        SW_EXPECT_NEAR( expected._y, pValue->_number._y, 1.0e-5f );
        SW_EXPECT_NEAR( expected._z, pValue->_number._z, 1.0e-5f );
        SW_EXPECT_NEAR( expected._w, pValue->_number._w, 1.0e-5f );
    }
} // namespace

/** @brief [CustomizationMigrationTest] 판 0 값이 판 1 · 2 · 3 단계를 차례로 지나 지금 판이 된다 */
SW_TEST_CASE_F( CustomizationMigrationTest, FirstVersionReachesCurrent )
{
    SW_EXPECT_EQUAL( uint32{ 3 }, _pSchema->getVersion() );
    CustomizationValueSet upgraded;
    SW_REQUIRE_TRUE( _pSchema->migrate( makeFirstVersionValues(), upgraded ).has_value() );
    SW_EXPECT_TRUE( upgraded.getSchemaVersion() == uint32{ 3 } );
    expectColor( upgraded, "Primary", kRed );                           // Rename
    expectColor( upgraded, "Outer", kBlue );                            // Split
    expectColor( upgraded, "Inner", kBlue );                            // Split
    expectColor( upgraded, "Clasp", float4{ 1.0f, 0.8f, 0.2f, 1.0f } ); // 번호 1 → Gold
    SW_EXPECT_NULL( upgraded.findValue( hashed_string( "Tint" ) ) );
    SW_EXPECT_NULL( upgraded.findValue( hashed_string( "Dye" ) ) );
    SW_EXPECT_NULL( upgraded.findValue( hashed_string( "Legacy" ) ) ); // Remove
    SW_REQUIRE_NOT_NULL( upgraded.findValue( hashed_string( "Shine" ) ) );
    SW_EXPECT_NEAR( 0.7f, upgraded.findValue( hashed_string( "Shine" ) )->_number._x, 1.0e-6f );
}

/** @brief [CustomizationMigrationTest] 단계는 그 판보다 옛 값에만 걸린다 — 판 2 값은 판 3 단계만, 지금 판 · 판을 찍지 않은 값은 그대로(음성 대조) */
SW_TEST_CASE_F( CustomizationMigrationTest, StepsRunOnlyAboveStoredVersion )
{
    // 판 2: Rename(→1) · Split(→2) 은 이미 지난 판이라 걸리지 않는다. 판 2 에서 Tint · Dye 는 뜻이 없는 이름이라 그 값만 빠지고 이름이 남는다
    CustomizationValueSet versionTwo = makeFirstVersionValues();
    versionTwo.setSchemaVersion( 2 );
    CustomizationValueSet upgraded;
    vector<hashed_string> listDropped;
    SW_REQUIRE_TRUE( _pSchema->migrate( versionTwo, upgraded, &listDropped ).has_value() );
    SW_EXPECT_NULL( upgraded.findValue( hashed_string( "Tint" ) ) );
    SW_EXPECT_TRUE( std::find( listDropped.begin(), listDropped.end(), hashed_string( "Tint" ) ) != listDropped.end() );
    SW_EXPECT_TRUE( std::find( listDropped.begin(), listDropped.end(), hashed_string( "Dye" ) ) != listDropped.end() );
    SW_EXPECT_NULL( upgraded.findValue( hashed_string( "Primary" ) ) );
    SW_EXPECT_NULL( upgraded.findValue( hashed_string( "Outer" ) ) );
    expectColor( upgraded, "Clasp", float4{ 1.0f, 0.8f, 0.2f, 1.0f } ); // 판 3 단계는 걸린다
    SW_EXPECT_NULL( upgraded.findValue( hashed_string( "Legacy" ) ) );

    // 판 1: Split 은 걸리고 Rename 은 걸리지 않는다
    CustomizationValueSet versionOne = makeFirstVersionValues();
    versionOne.setSchemaVersion( 1 );
    SW_REQUIRE_TRUE( _pSchema->migrate( versionOne, upgraded ).has_value() );
    SW_EXPECT_NULL( upgraded.findValue( hashed_string( "Primary" ) ) );
    expectColor( upgraded, "Outer", kBlue );

    // 지금 판 · 판을 찍지 않은 값(이 실행이 지금 스키마로 만든 값): 아무 단계도 걸리지 않고 판만 찍는다 — 뜻 있는 Clasp · Shine 만 남는다
    for ( const optional<uint32>& version : { optional<uint32>{ 3 }, optional<uint32>{} } )
    {
        CustomizationValueSet current = makeFirstVersionValues();
        current.setSchemaVersion( version );
        SW_REQUIRE_TRUE( _pSchema->migrate( current, upgraded ).has_value() );
        SW_EXPECT_TRUE( upgraded.getSchemaVersion() == uint32{ 3 } );
        SW_EXPECT_EQUAL( size_t{ 2 }, upgraded.getCount() );
        SW_EXPECT_NEAR( 1.0f, upgraded.findValue( hashed_string( "Clasp" ) )->_number._x, 1.0e-6f ); // 번호 그대로(색으로 바꾸지 않았다)
    }
}

/** @brief [CustomizationMigrationTest] 이 콘텐츠보다 새 판 값(다운그레이드)은 오류이고 받는 쪽을 덮지 않는다. 옮길 수 없는 값 하나는 그 값만 빠진다 */
SW_TEST_CASE_F( CustomizationMigrationTest, NewerVersionIsRefusedWithoutOverwrite )
{
    CustomizationValueSet future = makeFirstVersionValues();
    future.setSchemaVersion( 4 );
    CustomizationValueSet kept;
    kept.setColor( hashed_string( "Primary" ), kGreen );
    const expected<void> result = _pSchema->migrate( future, kept );
    SW_REQUIRE_FALSE( result.has_value() );
    SW_EXPECT_TRUE( result.error().kind() == ErrorKind::Unsupported );
    SW_EXPECT_EQUAL( size_t{ 1 }, kept.getCount() );
    expectColor( kept, "Primary", kGreen );

    // 견본 번호가 묶음 밖이면 어느 색인지 모른다 — 가까운 색으로 맞추지 않고 그 값만 뺀다(나머지는 올린다 — 패치가 세이브 전체를 버리지 않는다)
    for ( const float32 index : { 2.0f, -1.0f } )
    {
        CustomizationValueSet outside = makeFirstVersionValues();
        outside.setNumber( hashed_string( "Clasp" ), index );
        vector<hashed_string> listDropped;
        SW_REQUIRE_TRUE( _pSchema->migrate( outside, kept, &listDropped ).has_value() );
        SW_EXPECT_NULL( kept.findValue( hashed_string( "Clasp" ) ) );
        expectColor( kept, "Primary", kRed );
        SW_REQUIRE_EQUAL( size_t{ 1 }, listDropped.size() );
        SW_EXPECT_TRUE( listDropped.front() == hashed_string( "Clasp" ) );
    }

    // 값 넣기도 올리지 못한 집합을 건드리지 않는다
    CustomizationValue shine;
    shine._parameter = hashed_string( "Shine" );
    shine._number    = float4( 0.2f, 0.0f, 0.0f, 0.0f );
    SW_EXPECT_FALSE( _pSchema->applyValue( shine, false, future ) );
    SW_EXPECT_TRUE( future.getSchemaVersion() == uint32{ 4 } );
    SW_EXPECT_NEAR( 0.7f, future.findValue( hashed_string( "Shine" ) )->_number._x, 1.0e-6f );
}

/** @brief [CustomizationMigrationTest] 공유 코드가 해시 자리 이름(`#1a2b3c4d`)으로 둔 옛 이름도 단계가 옮긴다 · 새 이름에 이미 값이 있으면 옛 값으로 덮지 않는다 */
SW_TEST_CASE_F( CustomizationMigrationTest, MatchesPlaceholderNamesAndKeepsNewerValue )
{
    SW_EXPECT_TRUE( CustomizationSchemaDef::matchesStoredName( makePlaceholder( "Tint" ), hashed_string( "Tint" ) ) );
    SW_EXPECT_FALSE( CustomizationSchemaDef::matchesStoredName( makePlaceholder( "Tint" ), hashed_string( "Dye" ) ) );
    SW_EXPECT_FALSE( CustomizationSchemaDef::matchesStoredName( hashed_string( "#zzzzzzzz" ), hashed_string( "Tint" ) ) );

    CustomizationValueSet fromCode;
    fromCode.setColor( makePlaceholder( "Tint" ), kRed );
    fromCode.setNumber( makePlaceholder( "Clasp" ), 0.0f ); // 옛 판의 슬라이더 — 지금은 색이라 종류가 달라 자리 이름으로 왔다
    fromCode.setSchemaVersion( 0 );
    CustomizationValueSet upgraded;
    SW_REQUIRE_TRUE( _pSchema->migrate( fromCode, upgraded ).has_value() );
    expectColor( upgraded, "Primary", kRed );
    expectColor( upgraded, "Clasp", float4{ 0.5f, 0.5f, 0.5f, 1.0f } );

    CustomizationValueSet both = makeFirstVersionValues();
    both.setColor( hashed_string( "Primary" ), kGreen );
    SW_REQUIRE_TRUE( _pSchema->migrate( both, upgraded ).has_value() );
    expectColor( upgraded, "Primary", kGreen );
}

/** @brief [CustomizationMigrationTest] 값 넣기는 옛 판 집합을 먼저 올리고, 정규화는 결과에 지금 판을 찍는다 */
SW_TEST_CASE_F( CustomizationMigrationTest, ApplyValueAndNormalizeStampVersion )
{
    CustomizationValueSet values = makeFirstVersionValues();
    CustomizationValue    shine;
    shine._parameter = hashed_string( "Shine" );
    shine._number    = float4( 0.2f, 0.0f, 0.0f, 0.0f );
    SW_REQUIRE_TRUE( _pSchema->applyValue( shine, false, values ) );
    SW_EXPECT_TRUE( values.getSchemaVersion() == uint32{ 3 } );
    expectColor( values, "Primary", kRed );
    SW_EXPECT_NEAR( 0.2f, values.findValue( hashed_string( "Shine" ) )->_number._x, 1.0e-6f );

    CustomizationValueSet normalized;
    SW_REQUIRE_TRUE( _pSchema->normalize( values, normalized, nullptr ).has_value() );
    SW_EXPECT_TRUE( normalized.getSchemaVersion() == uint32{ 3 } );

    // 판을 적지 않은 스키마는 첫 판(0)이다
    const CustomizationSchemaDef* pPlain = _catalog.findSchema( hashed_string( "Plain" ) );
    SW_REQUIRE_NOT_NULL( pPlain );
    SW_EXPECT_EQUAL( uint32{ 0 }, pPlain->getVersion() );
}

/** @brief [CustomizationMigrationTest] 판에 빠진 단계 · 판 순서 · 나눌 이름 수 · 모르는 견본 묶음은 읽기 오류다 */
SW_TEST_CASE( CustomizationMigrationLoadTest, RejectsBrokenMigrationData )
{
    CustomizationSchemaCatalog catalog;
    SW_EXPECT_LOG( "Appearance.MigrationMissing" );
    SW_EXPECT_FALSE( loadSchemas( R"(<CustomizationSchemaCatalog><Schema id="A" version="2">
        <Migration toVersion="1"><Remove parameter="Old"/></Migration><Slider name="S"/></Schema></CustomizationSchemaCatalog>)",
                                  catalog ) );
    SW_EXPECT_LOG( "Appearance.MigrationOrder" );
    SW_EXPECT_FALSE( loadSchemas( R"(<CustomizationSchemaCatalog><Schema id="A" version="2">
        <Migration toVersion="2"><Remove parameter="Old"/></Migration><Migration toVersion="1"><Remove parameter="Old"/></Migration><Slider name="S"/></Schema></CustomizationSchemaCatalog>)",
                                  catalog ) );
    SW_EXPECT_LOG( "Appearance.MigrationTargets" );
    SW_EXPECT_FALSE( loadSchemas( R"(<CustomizationSchemaCatalog><Schema id="A" version="1">
        <Migration toVersion="1"><Split from="Dye" to="Outer"/></Migration><Slider name="S"/></Schema></CustomizationSchemaCatalog>)",
                                  catalog ) );
    SW_EXPECT_LOG( "Appearance.PaletteUnknown" );
    SW_EXPECT_FALSE( loadSchemas( R"(<CustomizationSchemaCatalog><Schema id="A" version="1">
        <Migration toVersion="1"><PaletteIndexToColor parameter="Clasp" palette="Nowhere"/></Migration><Slider name="S"/></Schema></CustomizationSchemaCatalog>)",
                                  catalog ) );
}
