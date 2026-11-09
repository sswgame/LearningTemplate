// AppearanceSelectionTest — 외형 선택: 담기 · 부분 선택 · 펼치기(지워진 값 · 잠긴 아이템의 되돌림), 프리셋으로 바꾸기, 비트 코덱과 공유 코드(nogpu).
#include "pch.h"

#include "Core/Encoding/Base64Util.h"
#include "Core/Hash/HashUtil.h"
#include "Core/Serialization/BitStream.h"
#include "Core/Testing/TestFramework.h"

#include "GameFramework/Runtime/Gameplay/Appearance/AppearanceDatabase.h"
#include "GameFramework/Runtime/Gameplay/Appearance/AppearanceSelection.h"
#include "GameFramework/Runtime/Gameplay/Inventory/ItemCatalog.h"

using namespace sw;

namespace
{
    /** @brief 이름을 받아 정한 아이템만 잠긴 것으로 답합니다. */
    class LockOneItem final : public IAppearanceUnlockQuery
    {
    public:
        explicit LockOneItem( const utf8* pLocked )
            : _locked{ pLocked }
        {
        }

        bool isItemUnlocked( const hashed_string& itemID ) const override { return itemID != _locked; }

    private:
        hashed_string _locked;
    };

    struct AppearanceSelectionTest
    {
        static constexpr const utf8* kHeightSlider = R"(<Slider name="Height" min="0" max="2" default="1" category="Body"/>)";

        ItemCatalog             _catalog;
        AppearanceDatabase      _database;
        CharacterAppearanceSpec _current;

        AppearanceSelectionTest()
            : _catalog{}
            , _database{}
            , _current{}
        {
            SW_REQUIRE_TRUE( _catalog.loadFromXMLText( R"(<ItemCatalog>
                <Item id="iron_helm" category="Armor" slot="Head" maxStack="1" weight="1" visual="helm_visual"/>
                <Item id="gold_helm" category="Armor" slot="Head" maxStack="1" weight="1" visual="gold_visual"/>
            </ItemCatalog>)",
                                                       "test" ) );
            loadInto( _database, kHeightSlider );
            SW_REQUIRE_TRUE( expandBase( _current ) );
        }

        /** @brief 콘텐츠 판 하나를 읽습니다. 판마다 다른 것은 키 슬라이더(`Height`) 줄뿐이다(범위 · 기본값이 바뀐 뒤 판). */
        void loadInto( AppearanceDatabase& database, const utf8* pHeightSlider ) const
        {
            loadSection( database, R"(<SlotCatalog><Slot name="Head"/><Slot name="Torso"/></SlotCatalog>)" );
            loadSection( database, R"(<ItemVisualCatalog>
                <ItemVisual id="body_visual"><Part name="Body" kind="Skinned" mesh="body.mesh" material="body.material"/></ItemVisual>
                <ItemVisual id="helm_visual" customization="Metal"><Part name="Shell" kind="SocketPrefab" prefab="helm.prefab" socket="Head"/><State name="Up"/><State name="Down"/></ItemVisual>
                <ItemVisual id="gold_visual" customization="Metal"><Part name="Shell" kind="SocketPrefab" prefab="gold.prefab" socket="Head" breakable="true"/></ItemVisual>
            </ItemVisualCatalog>)" );
            loadSection( database, string( R"(<CustomizationSchemaCatalog>
                <Schema id="Metal"><Slider name="Wear" min="0" max="1" default="0"/></Schema>
                <Schema id="Human">
                  )" ) + pHeightSlider +
                                       R"(
                  <Color name="Dye" default="1 0 0 1" category="Hair"/>
                  <Choice name="Style" default="Short" category="Hair"><Option name="Short"/><Option name="Long"/></Choice>
                </Schema>
            </CustomizationSchemaCatalog>)" );
            loadSection( database, R"(<CharacterAppearanceCatalog>
                <CharacterAppearance id="Base" schema="Human" bodyType="Male" bodyShape="Average" body="body_visual">
                  <Equip slot="Head" item="iron_helm"/>
                </CharacterAppearance>
                <CharacterAppearance id="Other" schema="Human" bodyType="Female" bodyShape="Slim" body="body_visual"/>
            </CharacterAppearanceCatalog>)" );
            SW_REQUIRE_TRUE( database.finishLoad( &_catalog ) );
        }

        static void loadSection( AppearanceDatabase& database, const string& xml ) { SW_REQUIRE_TRUE( database.loadSectionFromXMLText( xml.c_str(), "test" ) ); }

        [[nodiscard]] bool expandBase( CharacterAppearanceSpec& outSpec ) const
        {
            return _database.getPresets().expand( hashed_string( "Base" ), 1, _database.getSlotCatalog(), _database.getSets(), _database.getSchemas(), outSpec );
        }

        /** @brief 지금 모습을 담은 전체 선택입니다. */
        AppearanceSelection capture() const
        {
            AppearanceSelection selection;
            AppearanceSelection::captureSelection( _current, selection );
            return selection;
        }

        static AppearanceSlotRequest makeSlot( const utf8* pSlot, const utf8* pItem )
        {
            AppearanceSlotRequest request;
            request._slot   = hashed_string( pSlot );
            request._itemID = hashed_string( pItem );
            return request;
        }
    };
} // namespace

/** @brief [AppearanceSelectionTest] 담기는 모습을 그대로 옮기고, 그것을 펼치면 보고할 것이 없다 */
SW_TEST_CASE_F( AppearanceSelectionTest, CaptureThenApplyIsClean )
{
    _current._customization.setNumber( hashed_string( "Height" ), 1.5f );
    const AppearanceSelection selection = capture();
    SW_EXPECT_FALSE( selection.isPartial() );
    SW_EXPECT_TRUE( selection._basePresetID == hashed_string( "Base" ) );
    SW_EXPECT_TRUE( selection._bodyType == hashed_string( "Male" ) );

    CharacterAppearanceSpec   applied;
    AppearanceSelectionReport report;
    AppearanceSelection::applySelection( _database, selection, CharacterAppearanceSpec{}, nullptr, applied, report );
    SW_EXPECT_TRUE( report.isClean() );
    SW_EXPECT_TRUE( applied._bodyType == hashed_string( "Male" ) );
    SW_REQUIRE_TRUE( applied.findSlot( hashed_string( "Head" ) ) != nullptr );
    SW_EXPECT_TRUE( applied.findSlot( hashed_string( "Head" ) )->_itemID == hashed_string( "iron_helm" ) );
}

/** @brief [AppearanceSelectionTest] 부분 선택은 그 카테고리의 값만 지금 모습에 덮고 몸 종류와 슬롯은 건드리지 않는다 */
SW_TEST_CASE_F( AppearanceSelectionTest, PartialTouchesOnlyItsCategory )
{
    _current._customization.setNumber( hashed_string( "Height" ), 1.5f );
    _current._customization.setOption( hashed_string( "Style" ), hashed_string( "Long" ) );

    AppearanceSelection partial;
    AppearanceSelection::makePartial( capture(), _database, { hashed_string( "Hair" ) }, partial );
    SW_EXPECT_TRUE( partial.isPartial() );
    SW_EXPECT_TRUE( partial.includesCategory( hashed_string( "Hair" ) ) );
    SW_EXPECT_FALSE( partial.includesCategory( hashed_string( "Body" ) ) );
    SW_EXPECT_EQUAL( size_t( 1 ), partial._customization.getValues().size() );
    SW_EXPECT_TRUE( partial._listSlot.empty() );
    SW_EXPECT_TRUE( partial._bodyType.empty() );

    CharacterAppearanceSpec target;
    SW_REQUIRE_TRUE( _database.getPresets().expand( hashed_string( "Other" ), 1, _database.getSlotCatalog(), _database.getSets(), _database.getSchemas(), target ) );
    CharacterAppearanceSpec   applied;
    AppearanceSelectionReport report;
    AppearanceSelection::applySelection( _database, partial, target, nullptr, applied, report );
    SW_EXPECT_TRUE( report.isClean() );
    SW_EXPECT_TRUE( applied._bodyType == hashed_string( "Female" ) ); // 받는 쪽의 몸 그대로다
    SW_REQUIRE_TRUE( applied._customization.findValue( hashed_string( "Style" ) ) != nullptr );
    SW_EXPECT_TRUE( applied._customization.findValue( hashed_string( "Style" ) )->_option == hashed_string( "Long" ) );
}

/** @brief [AppearanceSelectionTest] 장비 카테고리를 담은 부분 선택만 슬롯을 나른다 */
SW_TEST_CASE_F( AppearanceSelectionTest, LoadoutCategoryCarriesSlots )
{
    AppearanceSelection partial;
    AppearanceSelection::makePartial( capture(), _database, { hashed_string( AppearanceSelection::kLoadoutCategory ) }, partial );
    SW_EXPECT_EQUAL( capture()._listSlot.size(), partial._listSlot.size() );
    SW_EXPECT_TRUE( partial._customization.isEmpty() );
}

/** @brief [AppearanceSelectionTest] 지워진 매개변수 · 항목은 버리고 보고한다. 있는 값은 그대로 들어간다 */
SW_TEST_CASE_F( AppearanceSelectionTest, DroppedValuesAreReported )
{
    SW_EXPECT_LOG( "AppearanceSelection.AppearanceValueNoLonger" );
    AppearanceSelection selection = capture();
    selection._customization.setNumber( hashed_string( "Gone" ), 1.0f );
    selection._customization.setOption( hashed_string( "Style" ), hashed_string( "GoneStyle" ) );
    selection._customization.setNumber( hashed_string( "Height" ), 0.5f );

    CharacterAppearanceSpec   applied;
    AppearanceSelectionReport report;
    AppearanceSelection::applySelection( _database, selection, _current, nullptr, applied, report );
    SW_EXPECT_EQUAL( size_t( 2 ), report._listDroppedParameter.size() );
    SW_EXPECT_TRUE( applied._customization.findValue( hashed_string( "Gone" ) ) == nullptr );
    SW_REQUIRE_TRUE( applied._customization.findValue( hashed_string( "Height" ) ) != nullptr );
    SW_EXPECT_NEAR( 0.5f, applied._customization.findValue( hashed_string( "Height" ) )->_number._x, 1.0e-5f );
}

/** @brief [AppearanceSelectionTest] 기준 프리셋이 지워졌으면 보고하고 지금 모습 위에 덮는다 */
SW_TEST_CASE_F( AppearanceSelectionTest, UnknownBasePresetKeepsCurrent )
{
    SW_EXPECT_LOG( "AppearanceSelection.AppearanceBasePresetNo" );
    AppearanceSelection selection = capture();
    selection._basePresetID       = hashed_string( "Gone" );
    selection._bodyType           = hashed_string{};
    CharacterAppearanceSpec   applied;
    AppearanceSelectionReport report;
    AppearanceSelection::applySelection( _database, selection, _current, nullptr, applied, report );
    SW_EXPECT_EQUAL( SW_TRUE, report._bUnknownBasePreset );
    SW_EXPECT_FALSE( report.isClean() );
    SW_EXPECT_TRUE( applied._bodyType == _current._bodyType );
}

/** @brief [AppearanceSelectionTest] 지워진 아이템과 잠긴 아이템은 슬롯을 기준 프리셋의 것으로 통째로 되돌린다 - 잠긴 아이템의 형상 변경 · 상태 · 꾸미기가 남으면 잠긴 외형이 그대로 보인다 */
SW_TEST_CASE_F( AppearanceSelectionTest, LockedItemRevertsTheWholeSlot )
{
    SW_EXPECT_LOG( "AppearanceSelection.AppearanceSlotUsing" );
    AppearanceSelection selection = capture();
    selection._listSlot.clear();
    AppearanceSlotRequest locked = makeSlot( "Head", "gold_helm" );
    locked._visibleVisual        = hashed_string( "gold_visual" );
    locked._state                = hashed_string( "Up" );
    locked._damage               = 0.5f;
    locked._bSuppressed          = SW_TRUE;
    locked._customization.setNumber( hashed_string( "Wear" ), 0.5f );
    locked._listDetachedPart.push_back( hashed_string( "Shell" ) );
    selection._listSlot.push_back( locked );

    CharacterAppearanceSpec   applied;
    AppearanceSelectionReport report;
    const LockOneItem         lock( "gold_helm" );
    AppearanceSelection::applySelection( _database, selection, _current, &lock, applied, report );

    SW_REQUIRE_EQUAL( size_t( 1 ), report._listSlotFallback.size() );
    SW_EXPECT_TRUE( report._listSlotFallback[0]._reason == AppearanceFallbackReason::LockedItem );
    SW_EXPECT_TRUE( report._listSlotFallback[0]._fallbackItem == hashed_string( "iron_helm" ) );

    const AppearanceSlotRequest* pSlot = applied.findSlot( hashed_string( "Head" ) );
    SW_REQUIRE_TRUE( pSlot != nullptr );
    SW_EXPECT_TRUE( pSlot->_itemID == hashed_string( "iron_helm" ) );
    SW_EXPECT_TRUE( pSlot->_visibleVisual.empty() );
    SW_EXPECT_TRUE( pSlot->_state.empty() );
    SW_EXPECT_NEAR( 0.0f, pSlot->_damage, 1.0e-5f );
    SW_EXPECT_EQUAL( SW_FALSE, pSlot->_bSuppressed );
    SW_EXPECT_TRUE( pSlot->_customization.isEmpty() );
    SW_EXPECT_TRUE( pSlot->_listDetachedPart.empty() );

    // 풀린 아이템이면 그대로 입는다
    AppearanceSelection::applySelection( _database, selection, _current, nullptr, applied, report );
    SW_EXPECT_TRUE( report._listSlotFallback.empty() );
    SW_EXPECT_TRUE( applied.findSlot( hashed_string( "Head" ) )->_itemID == hashed_string( "gold_helm" ) );
    SW_EXPECT_TRUE( applied.findSlot( hashed_string( "Head" ) )->_visibleVisual == hashed_string( "gold_visual" ) );
}

/** @brief [AppearanceSelectionTest] 지워진 아이템 · 슬롯 · 보이는 외형은 저마다의 이유로 보고한다. 형상 변경만 지워졌으면 아이템은 남는다 */
SW_TEST_CASE_F( AppearanceSelectionTest, SlotFallbackReasons )
{
    SW_EXPECT_LOG( "AppearanceSelection.AppearanceSlotUsing" );
    AppearanceSelection selection = capture();
    selection._listSlot.clear();
    selection._listSlot.push_back( makeSlot( "Head", "gone_item" ) );
    selection._listSlot.push_back( makeSlot( "Tail", "iron_helm" ) );
    AppearanceSlotRequest goneVisual = makeSlot( "Torso", "iron_helm" );
    goneVisual._visibleVisual        = hashed_string( "gone_visual" );
    selection._listSlot.push_back( goneVisual );

    CharacterAppearanceSpec   applied;
    AppearanceSelectionReport report;
    AppearanceSelection::applySelection( _database, selection, _current, nullptr, applied, report );
    SW_REQUIRE_EQUAL( size_t( 3 ), report._listSlotFallback.size() );
    SW_EXPECT_TRUE( report._listSlotFallback[0]._reason == AppearanceFallbackReason::MissingItem );
    SW_EXPECT_TRUE( report._listSlotFallback[1]._reason == AppearanceFallbackReason::UnknownSlot );
    SW_EXPECT_TRUE( report._listSlotFallback[2]._reason == AppearanceFallbackReason::MissingVisual );
    SW_EXPECT_TRUE( applied.findSlot( hashed_string( "Head" ) )->_itemID == hashed_string( "iron_helm" ) );
    SW_EXPECT_TRUE( applied.findSlot( hashed_string( "Torso" ) )->_itemID == hashed_string( "iron_helm" ) );
    SW_EXPECT_TRUE( applied.findSlot( hashed_string( "Torso" ) )->_visibleVisual.empty() );
}

/** @brief [AppearanceSelectionTest] 아이템 인스턴스 값은 보이는 외형의 스키마로 거른다 */
SW_TEST_CASE_F( AppearanceSelectionTest, ItemValuesAreFilteredBySchema )
{
    SW_EXPECT_LOG( "AppearanceSelection.AppearanceValueNoLonger" );
    AppearanceSelection selection = capture();
    selection._listSlot.clear();
    AppearanceSlotRequest request = makeSlot( "Head", "iron_helm" );
    request._customization.setNumber( hashed_string( "Wear" ), 0.25f );
    request._customization.setNumber( hashed_string( "Gone" ), 0.25f );
    selection._listSlot.push_back( request );

    CharacterAppearanceSpec   applied;
    AppearanceSelectionReport report;
    AppearanceSelection::applySelection( _database, selection, _current, nullptr, applied, report );
    SW_EXPECT_EQUAL( size_t( 1 ), report._listDroppedParameter.size() );
    const CustomizationValueSet& values = applied.findSlot( hashed_string( "Head" ) )->_customization;
    SW_EXPECT_TRUE( values.findValue( hashed_string( "Wear" ) ) != nullptr );
    SW_EXPECT_TRUE( values.findValue( hashed_string( "Gone" ) ) == nullptr );
}

/** @brief [AppearanceSelectionTest] 프리셋으로 바꾸기: 값의 종류를 스키마로 가르고 빈 슬롯은 비움이 된다. 담지 못하는 것은 조용히 버리지 않고 거절한다 */
SW_TEST_CASE_F( AppearanceSelectionTest, MakePresetDefRefusesWhatItCannotCarry )
{
    AppearanceSelection selection = capture();
    selection._customization.setNumber( hashed_string( "Height" ), 1.5f );
    selection._customization.setOption( hashed_string( "Style" ), hashed_string( "Long" ) );
    selection._listSlot.clear();
    selection._listSlot.push_back( makeSlot( "Head", "iron_helm" ) );
    selection._listSlot.push_back( makeSlot( "Torso", "" ) );

    CharacterAppearanceDef def;
    SW_REQUIRE_TRUE( AppearanceSelection::makePresetDef( _database, selection, hashed_string( "Mine" ), def ) );
    SW_EXPECT_TRUE( def._parent == hashed_string( "Base" ) );
    SW_REQUIRE_EQUAL( size_t( 2 ), def._listSlot.size() );
    SW_EXPECT_EQUAL( SW_FALSE, def._listSlot[0]._bClear );
    SW_EXPECT_EQUAL( SW_TRUE, def._listSlot[1]._bClear );
    SW_EXPECT_EQUAL( selection._customization.getValues().size(), def._listValue.size() );

    SW_EXPECT_FALSE( AppearanceSelection::makePresetDef( _database, selection, hashed_string{}, def ) );

    AppearanceSelection partial;
    AppearanceSelection::makePartial( selection, _database, { hashed_string( "Hair" ) }, partial );
    SW_EXPECT_FALSE( AppearanceSelection::makePresetDef( _database, partial, hashed_string( "Mine" ), def ) );

    AppearanceSelection unknownValue = selection;
    unknownValue._customization.setNumber( hashed_string( "Gone" ), 1.0f );
    SW_EXPECT_FALSE( AppearanceSelection::makePresetDef( _database, unknownValue, hashed_string( "Mine" ), def ) );

    AppearanceSelection detached = selection;
    detached._listSlot[0]._listDetachedPart.push_back( hashed_string( "Shell" ) );
    SW_EXPECT_FALSE( AppearanceSelection::makePresetDef( _database, detached, hashed_string( "Mine" ), def ) );

    AppearanceSelection instanceValue = selection;
    instanceValue._listSlot[0]._customization.setNumber( hashed_string( "Wear" ), 0.5f );
    SW_EXPECT_FALSE( AppearanceSelection::makePresetDef( _database, instanceValue, hashed_string( "Mine" ), def ) );

    AppearanceSelection suppressed       = selection;
    suppressed._listSlot[0]._bSuppressed = SW_TRUE;
    SW_EXPECT_FALSE( AppearanceSelection::makePresetDef( _database, suppressed, hashed_string( "Mine" ), def ) );
}

/** @brief [AppearanceSelectionTest] 비트 코덱: 값 · 슬롯이 되돌아오고, 받는 쪽이 모르는 이름은 임시 이름이 되어 펼칠 때 지워진 것으로 보고된다 */
SW_TEST_CASE_F( AppearanceSelectionTest, CodecRoundTripsAndKeepsUnknownNames )
{
    AppearanceSelection selection = capture();
    selection._seed               = 77;
    selection._customization.setNumber( hashed_string( "Height" ), 2.0f );
    selection._customization.setColor( hashed_string( "Dye" ), float4{ 0.0f, 1.0f, 0.0f, 1.0f } );
    selection._customization.setOption( hashed_string( "Style" ), hashed_string( "Long" ) );
    selection._customization.setNumber( hashed_string( "NotInSchema" ), 1.0f ); // 스키마에 없으면 싣지 않는다
    selection._listSlot.clear();
    AppearanceSlotRequest slot = makeSlot( "Head", "iron_helm" );
    slot._state                = hashed_string( "Down" );
    slot._damage               = 1.0f;
    slot._bSuppressed          = SW_TRUE;
    slot._customization.setNumber( hashed_string( "Wear" ), 1.0f );
    selection._listSlot.push_back( slot );
    selection._listSlot.push_back( makeSlot( "Tail", "gone_item" ) );

    BitWriter writer;
    AppearanceSelection::writeSelection( writer, selection, _database );
    const vector<uint8> bytes = writer.releaseBytes();

    BitReader           reader( bytes.data(), static_cast<int32>( bytes.size() ) );
    AppearanceSelection decoded;
    SW_REQUIRE_TRUE( AppearanceSelection::readSelection( reader, _database, decoded ) );
    SW_EXPECT_EQUAL( uint32( 77 ), decoded._seed );
    SW_EXPECT_TRUE( decoded._basePresetID == hashed_string( "Base" ) );
    SW_EXPECT_TRUE( decoded._schema == hashed_string( "Human" ) );
    SW_EXPECT_TRUE( decoded._bodyType == hashed_string( "Male" ) );
    SW_EXPECT_EQUAL( size_t( 3 ), decoded._customization.getValues().size() );
    SW_REQUIRE_TRUE( decoded._customization.findValue( hashed_string( "Height" ) ) != nullptr );
    SW_EXPECT_NEAR( 2.0f, decoded._customization.findValue( hashed_string( "Height" ) )->_number._x, 1.0e-3f );
    SW_REQUIRE_TRUE( decoded._customization.findValue( hashed_string( "Dye" ) ) != nullptr );
    SW_EXPECT_NEAR( 1.0f, decoded._customization.findValue( hashed_string( "Dye" ) )->_number._y, 1.0e-3f );
    SW_REQUIRE_TRUE( decoded._customization.findValue( hashed_string( "Style" ) ) != nullptr );
    SW_EXPECT_TRUE( decoded._customization.findValue( hashed_string( "Style" ) )->_option == hashed_string( "Long" ) );

    SW_REQUIRE_EQUAL( size_t( 2 ), decoded._listSlot.size() );
    SW_EXPECT_TRUE( decoded._listSlot[0]._itemID == hashed_string( "iron_helm" ) );
    SW_EXPECT_TRUE( decoded._listSlot[0]._state == hashed_string( "Down" ) );
    SW_EXPECT_NEAR( 1.0f, decoded._listSlot[0]._damage, 1.0e-3f );
    SW_EXPECT_EQUAL( SW_TRUE, decoded._listSlot[0]._bSuppressed );
    SW_EXPECT_TRUE( decoded._listSlot[0]._customization.findValue( hashed_string( "Wear" ) ) != nullptr );
    // 이 빌드가 모르는 이름은 임시 이름(`#해시`)이다
    SW_EXPECT_TRUE( decoded._listSlot[1]._slot.c_str()[0] == '#' );
    SW_EXPECT_TRUE( decoded._listSlot[1]._itemID.c_str()[0] == '#' );
}

/** @brief [AppearanceSelectionTest] 잘린 비트와 비정상 개수는 거절한다 */
SW_TEST_CASE_F( AppearanceSelectionTest, CodecRejectsBadInput )
{
    BitWriter writer;
    AppearanceSelection::writeSelection( writer, capture(), _database );
    const vector<uint8> bytes = writer.releaseBytes();
    AppearanceSelection decoded;
    for ( const size_t cut : { size_t( 1 ), bytes.size() / 2, bytes.size() - 1 } )
    {
        BitReader reader( bytes.data(), static_cast<int32>( cut ) );
        SW_EXPECT_FALSE( AppearanceSelection::readSelection( reader, _database, decoded ) );
    }

    BitWriter huge;
    huge.writeUint32( 0 );     // 기준 프리셋
    huge.writeVarUint( 0 );    // 시드
    huge.writeUint32( 0 );     // 스키마
    huge.writeUint32( 0 );     // 몸 종류
    huge.writeUint32( 0 );     // 체형
    huge.writeUint32( 0 );     // 얼굴
    huge.writeVarUint( 1025 ); // 카테고리 수 - 상한을 넘는다
    const vector<uint8> hugeBytes = huge.releaseBytes();
    BitReader           hugeReader( hugeBytes.data(), static_cast<int32>( hugeBytes.size() ) );
    SW_EXPECT_FALSE( AppearanceSelection::readSelection( hugeReader, _database, decoded ) );
}

/** @brief [AppearanceSelectionTest] 공유 코드: 되돌아오고 앞뒤 공백은 무시한다. 망가진 글자 · 체크섬 · 빈 글은 이유와 함께 거절한다 */
SW_TEST_CASE_F( AppearanceSelectionTest, ShareCodeRoundTripAndRejects )
{
    AppearanceSelection selection = capture();
    selection._customization.setOption( hashed_string( "Style" ), hashed_string( "Long" ) );
    const string code = AppearanceSelection::encode( selection, _database );
    SW_REQUIRE_FALSE( code.empty() );

    AppearanceSelection decoded;
    SW_REQUIRE_TRUE( AppearanceSelection::decode( string( "  " ) + code + "\n", _database, decoded ) );
    SW_EXPECT_TRUE( decoded._basePresetID == hashed_string( "Base" ) );
    SW_EXPECT_EQUAL( code, AppearanceSelection::encode( decoded, _database ) );

    const auto reasonOf = []( const expected<void>& result )
    { return result.has_value() ? string{} : string( result.error().message() ); };
    SW_EXPECT_EQUAL( string( "not a share code" ), reasonOf( AppearanceSelection::decode( "", _database, decoded ) ) );
    SW_EXPECT_EQUAL( string( "not a share code" ), reasonOf( AppearanceSelection::decode( "***", _database, decoded ) ) );

    string tampered               = code;
    tampered[tampered.size() / 2] = tampered[tampered.size() / 2] == 'A' ? 'B' : 'A';
    SW_EXPECT_EQUAL( string( "checksum mismatch" ), reasonOf( AppearanceSelection::decode( tampered, _database, decoded ) ) );
}

/** @brief [AppearanceSelectionTest] 공유 코드의 슬라이더는 실제 값이다 — 뒤 판이 범위를 넓혀도 같은 키로 읽고, 범위를 좁히면 끝에 붙이고 보고한다 */
SW_TEST_CASE_F( AppearanceSelectionTest, ShareCodeSliderSurvivesARangeChange )
{
    AppearanceSelection selection = capture();
    selection._customization.setNumber( hashed_string( "Height" ), 1.5f );
    const string code = AppearanceSelection::encode( selection, _database );

    AppearanceDatabase wider;
    loadInto( wider, R"(<Slider name="Height" min="0" max="4" default="1" category="Body"/>)" );
    AppearanceSelection decoded;
    SW_REQUIRE_TRUE( AppearanceSelection::decode( code, wider, decoded ) );
    SW_REQUIRE_TRUE( decoded._customization.findValue( hashed_string( "Height" ) ) != nullptr );
    SW_EXPECT_NEAR( 1.5f, decoded._customization.findValue( hashed_string( "Height" ) )->_number._x, 1.0e-6f ); // 판 1 은 범위의 비율로 읽어 3.0 이 됐다

    AppearanceDatabase narrower;
    loadInto( narrower, R"(<Slider name="Height" min="0" max="1" default="0.5" category="Body"/>)" );
    SW_REQUIRE_TRUE( AppearanceSelection::decode( code, narrower, decoded ) );
    CharacterAppearanceSpec   applied;
    AppearanceSelectionReport report;
    SW_EXPECT_LOG( "AppearanceSelection.AppearanceValueOutOfRange" );
    AppearanceSelection::applySelection( narrower, decoded, CharacterAppearanceSpec{}, nullptr, applied, report );
    SW_REQUIRE_EQUAL( size_t( 1 ), report._listClampedParameter.size() );
    SW_EXPECT_TRUE( report._listClampedParameter[0] == hashed_string( "Height" ) );
    SW_EXPECT_FALSE( report.isClean() );
    SW_REQUIRE_TRUE( applied._customization.findValue( hashed_string( "Height" ) ) != nullptr );
    SW_EXPECT_NEAR( 1.0f, applied._customization.findValue( hashed_string( "Height" ) )->_number._x, 1.0e-6f );
}

/** @brief [AppearanceSelectionTest] 정하지 않은 값도 저장할 때의 기본값으로 적는다 — 뒤 판이 기본값을 바꿔도 저장된 캐릭터는 그대로다 */
SW_TEST_CASE_F( AppearanceSelectionTest, ShareCodeKeepsTheLookWhenADefaultChanges )
{
    AppearanceSelection selection = capture();
    selection._customization.clear(); // 플레이어가 아무것도 건드리지 않았다
    const string code = AppearanceSelection::encode( selection, _database );

    AppearanceDatabase newerDefault;
    loadInto( newerDefault, R"(<Slider name="Height" min="0" max="2" default="1.5" category="Body"/>)" );
    AppearanceSelection decoded;
    SW_REQUIRE_TRUE( AppearanceSelection::decode( code, newerDefault, decoded ) );
    SW_EXPECT_EQUAL( size_t( 3 ), decoded._customization.getValues().size() );
    SW_REQUIRE_TRUE( decoded._customization.findValue( hashed_string( "Height" ) ) != nullptr );
    SW_EXPECT_NEAR( 1.0f, decoded._customization.findValue( hashed_string( "Height" ) )->_number._x, 1.0e-6f );
    SW_REQUIRE_TRUE( decoded._customization.findValue( hashed_string( "Style" ) ) != nullptr );
    SW_EXPECT_TRUE( decoded._customization.findValue( hashed_string( "Style" ) )->_option == hashed_string( "Short" ) );

    // 부분 선택은 담은 카테고리의 매개변수만 적는다
    AppearanceSelection hairOnly;
    AppearanceSelection::makePartial( selection, _database, { hashed_string( "Hair" ) }, hairOnly );
    SW_REQUIRE_TRUE( AppearanceSelection::decode( AppearanceSelection::encode( hairOnly, _database ), _database, decoded ) );
    SW_EXPECT_EQUAL( size_t( 2 ), decoded._customization.getValues().size() );
    SW_EXPECT_TRUE( decoded._customization.findValue( hashed_string( "Height" ) ) == nullptr );
}

/** @brief [AppearanceSelectionTest] 옛 판(1) 공유 코드도 읽는다 — 플레이어 프리셋 저장소가 이 문자열로 저장한다. 이 빌드보다 새 판은 거절한다 */
SW_TEST_CASE_F( AppearanceSelectionTest, ShareCodeReadsTheOlderVersion )
{
    // 판 1 의 바이트를 손으로 쓴다: 판 · 기준 프리셋 · 시드 · 스키마 · 몸 셋 · 카테고리 0 · 값(Height 를 범위 0~2 의 16 비트 단계로) · 슬롯 0
    const auto makeCode = []( uint32 version )
    {
        BitWriter writer;
        writer.writeBits( version, 8 );
        writer.writeUint32( static_cast<uint32>( hashed_string( "Base" ).getHash() ) );
        writer.writeVarUint( 1 );
        writer.writeUint32( static_cast<uint32>( hashed_string( "Human" ).getHash() ) );
        writer.writeUint32( static_cast<uint32>( hashed_string( "Male" ).getHash() ) );
        writer.writeUint32( static_cast<uint32>( hashed_string( "Average" ).getHash() ) );
        writer.writeUint32( 0 );
        writer.writeVarUint( 0 );
        writer.writeVarUint( 1 );
        writer.writeUint32( static_cast<uint32>( hashed_string( "Height" ).getHash() ) );
        writer.writeBits( static_cast<uint32>( CustomizationKind::Slider ), 2 );
        writer.writeBits( 49151u, 16 ); // 0~2 의 3/4 지점 = 1.5
        writer.writeVarUint( 0 );
        vector<uint8> bytes = writer.releaseBytes();
        const uint32  crc   = HashUtil::computeCRC32( bytes.data(), bytes.size() );
        for ( uint32 byteIndex = 0; byteIndex < 4; ++byteIndex )
        {
            bytes.push_back( static_cast<uint8>( ( crc >> ( byteIndex * 8 ) ) & 0xffu ) );
        }
        return Base64Util::encodeURL( bytes.data(), bytes.size() );
    };
    AppearanceSelection  decoded;
    const expected<void> first = AppearanceSelection::decode( makeCode( 1 ), _database, decoded );
    SW_REQUIRE_TRUE_MSG( first.has_value(), first.has_value() ? "" : first.error().message().data() );
    SW_EXPECT_TRUE( decoded._basePresetID == hashed_string( "Base" ) );
    SW_REQUIRE_TRUE( decoded._customization.findValue( hashed_string( "Height" ) ) != nullptr );
    SW_EXPECT_NEAR( 1.5f, decoded._customization.findValue( hashed_string( "Height" ) )->_number._x, 1.0e-3f );

    const expected<void> future = AppearanceSelection::decode( makeCode( AppearanceSelection::kShareCodeVersion + 1 ), _database, decoded );
    SW_EXPECT_FALSE( future );
    SW_EXPECT_EQUAL( string( "unsupported share code version" ), future.has_value() ? string{} : string( future.error().message() ) );
}
