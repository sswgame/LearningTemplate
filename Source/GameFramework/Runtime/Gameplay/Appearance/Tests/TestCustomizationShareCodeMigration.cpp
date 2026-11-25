// CustomizationShareCodeMigrationTest — 공유 코드 판 1 ~ 4 의 손으로 쓴 표본이 지금 스키마 판으로 읽힌다, 왕복, 더 새 스키마 판 거절, 펼치기의 판 올림(nogpu).
#include "pch.h"

#include "Core/Encoding/Base64Util.h"
#include "Core/Hash/HashUtil.h"
#include "Core/Serialization/BitStream.h"
#include "Core/TestFramework/TestFramework.h"

#include "GameFramework/Runtime/Gameplay/Appearance/AppearanceDatabase.h"
#include "GameFramework/Runtime/Gameplay/Appearance/AppearanceSelection.h"
#include "GameFramework/Runtime/Gameplay/Appearance/CharacterEquipmentComponent.h"
#include "GameFramework/Runtime/Gameplay/Appearance/CustomizationSchema.h"
#include "GameFramework/Runtime/Gameplay/Appearance/UserAppearancePresetStore.h"
#include "GameFramework/Runtime/Gameplay/Inventory/ItemCatalog.h"

using namespace sw;

namespace
{
    // 몸 스키마 Human 은 판 2(HairTint → HairColor). 투구의 스키마 Cloak 은 판 4: 판 2 Tint → Primary, 판 3 Dye → Outer · Inner, 판 4 Legacy 지움.
    constexpr const utf8* kSchemaXML = R"(<CustomizationSchemaCatalog>
        <Schema id="Cloak" version="4">
          <Migration toVersion="2"><Rename from="Tint" to="Primary"/></Migration>
          <Migration toVersion="3"><Split from="Dye" to="Outer Inner"/></Migration>
          <Migration toVersion="4"><Remove parameter="Legacy"/></Migration>
          <Color name="Primary" default="1 1 1 1"/>
          <Color name="Outer" default="1 1 1 1"/>
          <Color name="Inner" default="1 1 1 1"/>
        </Schema>
        <Schema id="Human" version="2">
          <Migration toVersion="2"><Rename from="HairTint" to="HairColor"/></Migration>
          <Color name="HairColor" category="Hair" default="1 1 1 1"/>
        </Schema>
    </CustomizationSchemaCatalog>)";

    const float4 kRed{ 0.8f, 0.2f, 0.2f, 1.0f };
    const float4 kBlue{ 0.2f, 0.2f, 0.8f, 1.0f };
    const float4 kGreen{ 0.2f, 0.8f, 0.2f, 1.0f };

    struct CustomizationShareCodeMigrationTest
    {
        ItemCatalog        _catalog;
        AppearanceDatabase _database;

        CustomizationShareCodeMigrationTest()
            : _catalog{}
            , _database{}
        {
            SW_REQUIRE_TRUE( _catalog.loadFromXMLText( R"(<ItemCatalog><Item id="helm" category="Armor" slot="Head" maxStack="1" weight="1" visual="helm_visual"/></ItemCatalog>)", "test" ) );
            loadSection( R"(<SlotCatalog><Slot name="Head"/></SlotCatalog>)" );
            loadSection( R"(<ItemVisualCatalog>
                <ItemVisual id="body_visual"><Part name="Body" kind="Skinned" mesh="body.mesh" material="body.material"/></ItemVisual>
                <ItemVisual id="helm_visual" customization="Cloak"><Part name="Shell" kind="SocketPrefab" prefab="helm.prefab" socket="Head"/></ItemVisual>
            </ItemVisualCatalog>)" );
            loadSection( kSchemaXML );
            loadSection( R"(<CharacterAppearanceCatalog>
                <CharacterAppearance id="Base" schema="Human" bodyType="Male" bodyShape="Average" body="body_visual"><Equip slot="Head" item="helm"/></CharacterAppearance>
            </CharacterAppearanceCatalog>)" );
            SW_REQUIRE_TRUE( _database.finishLoad( &_catalog ) );
        }

        void loadSection( const utf8* pXML ) { SW_REQUIRE_TRUE( _database.loadSectionFromXMLText( pXML, "test" ) ); }

        static void writeHash( BitWriter& writer, const utf8* pName ) { writer.writeUint32( pName == nullptr ? 0u : static_cast<uint32>( hashed_string( pName ).getHash() ) ); }

        static void writeColor( BitWriter& writer, const utf8* pName, const float4& color )
        {
            writeHash( writer, pName );
            writer.writeBits( static_cast<uint32>( CustomizationKind::Color ), 2 );
            for ( const float32 channel : { color._x, color._y, color._z, color._w } )
            {
                writer.writeBits( CustomizationParamDef::quantizeColorChannel( channel ), 8 );
            }
        }

        /**
         * @brief 공유 코드를 손으로 씁니다(엔진의 쓰기를 거치지 않은 판마다의 표본). 몸 값 HairTint · 투구 값 Tint · Dye 는 스키마 판 1 의 이름이다.
         * @details 판 · 기준 프리셋 · 시드 · 스키마 · 몸 셋 · 카테고리 0 · [판 4: 몸 스키마 판] 몸 값 · 슬롯 1(칸 · 아이템 · 형상 · 상태 · 피해 8 비트 · 조건 숨김 ·
         *          [판 3: 플레이어 숨김] · 뗀 부품 0 · [판 4: 투구 스키마 판] 투구 값) · [판 3: 피부 층 0] · CRC32 를 base64url 로.
         */
        static string makeCode( uint32 formatVersion, uint32 bodySchemaVersion, uint32 itemSchemaVersion, bool bOrphanValue = false )
        {
            BitWriter writer;
            writer.writeBits( formatVersion, 8 );
            writeHash( writer, "Base" );
            writer.writeVarUint( 1 );
            writeHash( writer, "Human" );
            writeHash( writer, "Male" );
            writeHash( writer, "Average" );
            writeHash( writer, nullptr );
            writer.writeVarUint( 0 );
            if ( formatVersion >= 4 )
                writer.writeVarUint( bodySchemaVersion );
            writer.writeVarUint( 1 );
            writeColor( writer, "HairTint", kGreen );
            writer.writeVarUint( 1 );
            writeHash( writer, "Head" );
            writeHash( writer, "helm" );
            writeHash( writer, nullptr );
            writeHash( writer, nullptr );
            writer.writeBits( 0, 8 );
            writer.writeBool( false );
            if ( formatVersion >= 3 )
                writer.writeBool( false );
            writer.writeVarUint( 0 );
            if ( formatVersion >= 4 )
                writer.writeVarUint( itemSchemaVersion );
            writer.writeVarUint( bOrphanValue ? 3 : 2 );
            writeColor( writer, "Tint", kRed );
            writeColor( writer, "Dye", kBlue );
            if ( bOrphanValue )
                writeColor( writer, "Bogus", kGreen ); // 어느 판에도 없던 이름 — 단계 없이 지운 매개변수와 같다
            if ( formatVersion >= 3 )
                writer.writeVarUint( 0 );
            vector<uint8> bytes = writer.releaseBytes();
            const uint32  crc   = HashUtil::computeCRC32( bytes.data(), bytes.size() );
            for ( uint32 byteIndex = 0; byteIndex < 4; ++byteIndex )
            {
                bytes.push_back( static_cast<uint8>( ( crc >> ( byteIndex * 8 ) ) & 0xffu ) );
            }
            return Base64Util::encodeURL( bytes.data(), bytes.size() );
        }

        /** @brief 공유 코드는 색을 성분마다 8 비트로 싣는다. */
        static void expectColorNear( const CustomizationValueSet& values, const utf8* pName, const float4& expected )
        {
            const CustomizationValue* pValue = values.findValue( hashed_string( pName ) );
            SW_REQUIRE_TRUE_MSG( pValue != nullptr, pName );
            SW_EXPECT_NEAR( expected._x, pValue->_number._x, 1.0f / 255.0f );
            SW_EXPECT_NEAR( expected._y, pValue->_number._y, 1.0f / 255.0f );
            SW_EXPECT_NEAR( expected._z, pValue->_number._z, 1.0f / 255.0f );
        }

        /** @brief 몸 HairColor = 초록, 투구 Primary = 빨강 · Outer · Inner = 파랑이고 판이 지금 판인지 봅니다. */
        static void expectUpgraded( const AppearanceSelection& selection )
        {
            SW_EXPECT_EQUAL( uint32{ 2 }, selection._customization.getSchemaVersion() );
            expectColorNear( selection._customization, "HairColor", kGreen );
            SW_EXPECT_NULL( selection._customization.findValue( hashed_string( "HairTint" ) ) );
            SW_REQUIRE_EQUAL( size_t{ 1 }, selection._listSlot.size() );
            const CustomizationValueSet& item = selection._listSlot.front()._customization;
            SW_EXPECT_EQUAL( uint32{ 4 }, item.getSchemaVersion() );
            expectColorNear( item, "Primary", kRed );
            expectColorNear( item, "Outer", kBlue );
            expectColorNear( item, "Inner", kBlue );
        }
    };
} // namespace

/** @brief [CustomizationShareCodeMigrationTest] 판 1 · 2 · 3 공유 코드(스키마 판 칸 없음 — 스키마 판 1)와 판 4 의 옛 스키마 판 값이 모두 지금 스키마 판으로 올라 읽힌다 */
SW_TEST_CASE_F( CustomizationShareCodeMigrationTest, OlderCodesReachCurrentSchema )
{
    for ( const uint32 formatVersion : { uint32{ 1 }, uint32{ 2 }, uint32{ 3 }, uint32{ 4 } } )
    {
        AppearanceSelection  decoded;
        const expected<void> result = AppearanceSelection::decode( makeCode( formatVersion, 1, 1 ), _database, decoded );
        SW_REQUIRE_TRUE_MSG( result.has_value(), result.has_value() ? "" : result.error().message().data() );
        expectUpgraded( decoded );
    }
    // 음성 대조 — 판 4 의 스키마 판 3 값: Split(→3) 은 지난 판이라 Dye 를 옮기지 않는다(그 판에 Dye 는 없던 이름이다)
    SW_EXPECT_LOG( "AppearanceSelection.SavedValueDropped" ); // 판 2 의 HairTint · 판 3 의 Tint · Dye 는 그 판에 없는 이름이다
    AppearanceSelection decoded;
    SW_REQUIRE_TRUE( AppearanceSelection::decode( makeCode( 4, 2, 3 ), _database, decoded ).has_value() );
    SW_EXPECT_NULL( decoded._listSlot.front()._customization.findValue( hashed_string( "Outer" ) ) );
}

/** @brief [CustomizationShareCodeMigrationTest] 지금 판으로 쓰고 읽으면 값과 판이 그대로다. 옛 판 값을 든 선택도 쓸 때 지금 판으로 올려 쓴다 */
SW_TEST_CASE_F( CustomizationShareCodeMigrationTest, RoundTripWritesCurrentSchema )
{
    AppearanceSelection fromOld;
    SW_REQUIRE_TRUE( AppearanceSelection::decode( makeCode( 3, 1, 1 ), _database, fromOld ).has_value() );
    AppearanceSelection again;
    SW_REQUIRE_TRUE( AppearanceSelection::decode( AppearanceSelection::encode( fromOld, _database ), _database, again ).has_value() );
    expectUpgraded( again );

    // 옛 판 값을 그대로 든 선택(옛 세이브의 장비 인스턴스를 담은 모습)
    AppearanceSelection    stale = fromOld;
    CustomizationValueSet& item  = stale._listSlot.front()._customization;
    item.clear();
    item.setColor( hashed_string( "Tint" ), kRed );
    item.setColor( hashed_string( "Dye" ), kBlue );
    item.setSchemaVersion( 1 );
    SW_REQUIRE_TRUE( AppearanceSelection::decode( AppearanceSelection::encode( stale, _database ), _database, again ).has_value() );
    expectUpgraded( again );
}

/** @brief [CustomizationShareCodeMigrationTest] 이 콘텐츠보다 새 스키마 판의 값을 실은 코드는 거절한다(기본값으로 바꿔 담지 않는다) */
SW_TEST_CASE_F( CustomizationShareCodeMigrationTest, NewerSchemaVersionIsRefused )
{
    SW_EXPECT_LOG( "AppearanceSelection.SavedValueDropped" ); // 판 4 표본의 HairTint · Tint · Dye 는 지금 판에 없는 이름이다
    AppearanceSelection  decoded;
    const expected<void> futureItem = AppearanceSelection::decode( makeCode( 4, 2, 5 ), _database, decoded );
    SW_REQUIRE_FALSE( futureItem.has_value() );
    SW_EXPECT_TRUE( futureItem.error().kind() == ErrorKind::Unsupported );
    const expected<void> futureBody = AppearanceSelection::decode( makeCode( 4, 3, 4 ), _database, decoded );
    SW_REQUIRE_FALSE( futureBody.has_value() );
    SW_EXPECT_TRUE( futureBody.error().kind() == ErrorKind::Unsupported );
    // 음성 대조: 같은 모양의 지금 판 코드는 읽힌다
    SW_EXPECT_TRUE( AppearanceSelection::decode( makeCode( 4, 2, 4 ), _database, decoded ).has_value() );
}

/** @brief [CustomizationShareCodeMigrationTest] 풀지 못하는 프리셋(더 새 스키마 판)은 여는 것만 실패하고, 다른 프리셋을 저장해도 코드 그대로 다시 써진다 */
SW_TEST_CASE_F( CustomizationShareCodeMigrationTest, PresetTheContentCannotDecodeSurvivesSaving )
{
    UserAppearancePresetStore store;
    SW_EXPECT_FALSE( store.savePreset( " ", AppearanceSelection{}, _database ) ); // 다듬으면 빈 이름
    SW_REQUIRE_TRUE( store.savePreset( "Later", AppearanceSelection{}, _database ) );
    const string           placeholderCode = store.findPresetByName( "Later" )->_code;
    const string           futureCode      = makeCode( 4, 3, 4 ); // 몸 스키마 판 3 은 이 콘텐츠(판 2)보다 새것이다
    const expected<string> written         = store.saveToText();
    SW_REQUIRE_TRUE( written.has_value() );
    string       text    = *written;
    const size_t codePos = text.find( placeholderCode );
    SW_REQUIRE_TRUE( codePos != string::npos );
    text.replace( codePos, placeholderCode.size(), futureCode );

    UserAppearancePresetStore loaded;
    SW_REQUIRE_TRUE( loaded.loadFromXMLText( text ) );
    AppearanceSelection  selection;
    const expected<void> decoded = loaded.decodePreset( "Later", _database, selection );
    SW_REQUIRE_FALSE( decoded.has_value() );
    SW_EXPECT_TRUE( decoded.error().kind() == ErrorKind::Unsupported );
    SW_REQUIRE_TRUE( loaded.savePreset( "Now", AppearanceSelection{}, _database ) );
    SW_EXPECT_TRUE( loaded.decodePreset( "Now", _database, selection ).has_value() );

    const expected<string> saved = loaded.saveToText();
    SW_REQUIRE_TRUE( saved.has_value() );
    UserAppearancePresetStore again;
    SW_REQUIRE_TRUE( again.loadFromXMLText( *saved ) );
    SW_REQUIRE_EQUAL( size_t{ 2 }, again.getPresets().size() );
    SW_EXPECT_TRUE( again.findPresetByName( "Later" )->_code == futureCode );
}

/** @brief [CustomizationShareCodeMigrationTest] 펼치기는 옛 판 값을 올린 뒤 스키마로 거른다 — 올리지 않으면 Tint 가 "지워진 값" 으로 버려진다 */
SW_TEST_CASE_F( CustomizationShareCodeMigrationTest, ApplySelectionUpgradesBeforeFiltering )
{
    SW_EXPECT_LOG( "AppearanceSelection.SavedValueDropped" );
    AppearanceSelection selection;
    SW_REQUIRE_TRUE( AppearanceSelection::decode( makeCode( 4, 2, 4 ), _database, selection ).has_value() );
    CustomizationValueSet& item = selection._listSlot.front()._customization;
    item.clear();
    item.setColor( hashed_string( "Tint" ), kRed );
    item.setSchemaVersion( 1 );
    selection._customization.clear();
    selection._customization.setColor( hashed_string( "HairTint" ), kGreen );
    selection._customization.setSchemaVersion( 1 );

    CharacterAppearanceSpec   applied;
    AppearanceSelectionReport report;
    AppearanceSelection::applySelection( _database, selection, CharacterAppearanceSpec{}, nullptr, applied, report );
    SW_EXPECT_TRUE( report.isClean() );
    expectColorNear( applied._customization, "HairColor", kGreen );
    const AppearanceSlotRequest* pHead = applied.findSlot( hashed_string( "Head" ) );
    SW_REQUIRE_NOT_NULL( pHead );
    expectColorNear( pHead->_customization, "Primary", kRed );
    SW_EXPECT_EQUAL( uint32{ 4 }, pHead->_customization.getSchemaVersion() );

    // 더 새 판 값은 버린 것으로 보고하고 펼치기는 끝난다
    item.setSchemaVersion( 9 );
    SW_EXPECT_LOG( "AppearanceSelection.ValuesNotMigrated" );
    SW_EXPECT_LOG( "AppearanceSelection.AppearanceValueNoLonger" );
    AppearanceSelection::applySelection( _database, selection, CharacterAppearanceSpec{}, nullptr, applied, report );
    SW_EXPECT_FALSE( report.isClean() );
    SW_REQUIRE_NOT_NULL( applied.findSlot( hashed_string( "Head" ) ) );
    SW_EXPECT_TRUE( applied.findSlot( hashed_string( "Head" ) )->_customization.isEmpty() );
}

/** @brief [CustomizationShareCodeMigrationTest] 뜻이 없는 값이 든 옛 공유 코드: 그 값만 빠지고 오류 로그, 나머지는 올라 읽힌다(코드 전체를 거절하지 않는다) */
SW_TEST_CASE_F( CustomizationShareCodeMigrationTest, OrphanValueIsDroppedAlone )
{
    SW_EXPECT_LOG( "AppearanceSelection.SavedValueDropped" );
    AppearanceSelection  decoded;
    const expected<void> result = AppearanceSelection::decode( makeCode( 3, 1, 1, true ), _database, decoded );
    SW_REQUIRE_TRUE( result.has_value() );
    expectUpgraded( decoded );
    SW_EXPECT_NULL( decoded._listSlot.front()._customization.findValue( hashed_string( "Bogus" ) ) );
}

/** @brief [CustomizationShareCodeMigrationTest] 세이브의 장비 인스턴스(`upgradeSavedItem` — 세이브 길의 입구): 옛 판 값은 올리고 뜻이 없는 값만 빼며 알린다. 더 새 판은 읽지 않고 값을 그대로 둔다 */
SW_TEST_CASE_F( CustomizationShareCodeMigrationTest, SavedItemUpgradesAndDropsOnlyOrphans )
{
    CustomizationValueSet values;
    values.setColor( hashed_string( "Tint" ), kRed );
    values.setColor( hashed_string( "Dye" ), kBlue );
    values.setColor( hashed_string( "Bogus" ), kGreen );
    values.setSchemaVersion( 1 );
    InventorySlot helm;
    helm._itemID = hashed_string( "helm" );
    helm._count  = 1;
    SW_REQUIRE_TRUE( helm.setCustomization( values ) );
    SW_EXPECT_LOG( "CharacterEquipment.SavedValueDropped" );
    SW_REQUIRE_TRUE( CharacterEquipmentComponent::upgradeSavedItem( _database, helm ) );
    SW_EXPECT_EQUAL( uint32{ 4 }, helm.getCustomization().getSchemaVersion() );
    expectColorNear( helm.getCustomization(), "Primary", kRed );
    expectColorNear( helm.getCustomization(), "Outer", kBlue );
    SW_EXPECT_NULL( helm.getCustomization().findValue( hashed_string( "Bogus" ) ) );

    // 다운그레이드: 이 콘텐츠보다 새 판 — 읽지 않고(부르는 쪽이 세이브 읽기를 거절한다) 값은 그대로다
    values.setSchemaVersion( 5 );
    SW_REQUIRE_TRUE( helm.setCustomization( values ) );
    SW_EXPECT_LOG( "CharacterEquipment.SavedItemFromNewerSchema" );
    SW_EXPECT_FALSE( CharacterEquipmentComponent::upgradeSavedItem( _database, helm ) );
    SW_EXPECT_EQUAL( uint32{ 5 }, helm.getCustomization().getSchemaVersion() );
}
