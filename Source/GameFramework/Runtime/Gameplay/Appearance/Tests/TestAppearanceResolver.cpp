#include "pch.h"

#include "Core/Testing/TestFramework.h"

#include "GameFramework/Runtime/Gameplay/Appearance/AppearanceDatabase.h"
#include "GameFramework/Runtime/Gameplay/Appearance/AppearanceResolver.h"
#include "GameFramework/Runtime/Gameplay/Inventory/Equipment.h"
#include "GameFramework/Runtime/Gameplay/Inventory/ItemCatalog.h"

using namespace sw;

namespace
{
    constexpr const utf8* kSlotsXML = R"(<SlotTable>
        <Slot name="Head"/><Slot name="Torso"/><Slot name="MainHand"/><Slot name="OffHand"/>
        <Occupancy id="TwoHanded" slots="MainHand,OffHand"/>
    </SlotTable>)";

    constexpr const utf8* kVisualsXML = R"(<ItemVisualCatalog>
        <ItemVisual id="body">
          <Part name="Body" kind="Skinned" mesh="body.mesh" material="body.material" sockets="body.sockets"><Variant name="Slim" mesh="slim.mesh" skeleton="slim.skeleton.xml"/></Part>
        </ItemVisual>
        <ItemVisual id="helmet" tags="Helmet.Full"><Part name="Shell" kind="SocketPrefab" prefab="helmet.prefab" socket="Head,Neck" offset="0 1 0"/></ItemVisual>
        <ItemVisual id="hood"><Part name="Cloth" kind="SocketPrefab" prefab="hood.prefab" socket="Head"/><Part name="Tie" kind="SocketPrefab" prefab="tie.prefab" socket="Neck"/></ItemVisual>
        <ItemVisual id="rifle" occupancy="TwoHanded" customization="Gun" tags="Weapon.Rifle">
          <Part name="Gun" kind="SocketPrefab" prefab="rifle.prefab" socket="Grip" sockets="rifle.sockets"><Variant name="Long" prefab="rifle_long.prefab"/><MaterialVariant name="Desert" material="desert.material"/></Part>
        </ItemVisual>
        <ItemVisual id="scope"><Part name="Lens" kind="SocketPrefab" prefab="lens.prefab" socket="Rail"/></ItemVisual>
        <ItemVisual id="shield" tags="Weapon.Shield"><Part name="Board" kind="SocketPrefab" prefab="shield.prefab" socket="Arm"/></ItemVisual>
        <ItemVisual id="armor" defaultState="Worn">
          <Part name="Plate" kind="Skinned" mesh="plate.mesh" material="plate.material">
            <Variant name="Torn" mesh="plate_torn.mesh"/><Variant name="Clean" material="plate_clean.material"/>
          </Part>
          <Part name="Strap" kind="SocketPrefab" prefab="strap.prefab" socket="Back" breakable="true" impulse="0 2 0"/>
          <Part name="Pad" kind="SocketPrefab" prefab="pad.prefab" socket="Shoulder" breakStage="Ruined"/>
          <Part name="Rivet" kind="SocketPrefab" prefab="rivet.prefab" socket="Chest"/>
          <State name="Worn"/>
          <State name="Stowed"><Place part="Strap" socket="Hip" offset="1 0 0"/><HidePart part="Rivet"/></State>
          <DamageStage name="Scuffed" threshold="0.25"><Variant part="Plate" name="Torn"/><Material part="Plate" name="_dirt" value="0.5"/></DamageStage>
          <DamageStage name="Ruined" threshold="0.75"/>
        </ItemVisual>
        <ItemVisual id="tattoo">
          <Part name="Ink" kind="BodyModification"><Morph name="Flat" weight="0.25"/><Material name="_ink" value="1 0 0 1"/><HideRegion name="Chest"/><HideRegion name="Arm"/></Part>
        </ItemVisual>
        <ItemVisual id="shirt"><Part name="Cover" kind="BodyModification"><HideRegion name="Chest"/></Part></ItemVisual>
        <ItemVisual id="cape_knight"><Part name="Crest" kind="SocketPrefab" prefab="crest.prefab" socket="Back"/></ItemVisual>
        <ItemVisual id="sectioned_a"><Part name="Suit" kind="Skinned" mesh="suit.mesh"><Section name="s0" material="red.material"/></Part></ItemVisual>
        <ItemVisual id="sectioned_b"><Part name="Suit" kind="Skinned" mesh="suit.mesh"><Section name="s0" material="blue.material"/></Part></ItemVisual>
    </ItemVisualCatalog>)";

    /** @brief 슬롯 · 외형을 읽은 데이터베이스와 요청 하나입니다. 장비는 형상 변경으로 입혀 아이템 표 없이 외형을 고릅니다. */
    struct AppearanceResolverTest
    {
        AppearanceDatabase      _database;
        CharacterAppearanceSpec _spec;
        ResolvedAppearance      _resolved;

        AppearanceResolverTest()
            : _database{}
            , _spec{}
            , _resolved{}
        {
            load( kSlotsXML );
            load( kVisualsXML );
            _spec._bodyVisual = hashed_string( "body" );
        }

        void load( const utf8* pXML ) { SW_REQUIRE_TRUE( _database.loadSectionFromXMLText( pXML, "test" ) ); }

        AppearanceSlotRequest& equip( const utf8* pSlot, const utf8* pVisual )
        {
            AppearanceSlotRequest request;
            request._slot          = hashed_string( pSlot );
            request._itemID        = hashed_string( pVisual );
            request._visibleVisual = hashed_string( pVisual );
            _spec._listSlot.push_back( request );
            return _spec._listSlot.back();
        }

        void resolve() { AppearanceResolver::resolve( _database, _spec, _resolved ); }

        [[nodiscard]] const ResolvedPart* findPart( const utf8* pOwner, const utf8* pPart ) const { return _resolved.findPart( hashed_string( pOwner ), hashed_string( pPart ) ); }
        [[nodiscard]] bool                hasOwner( const utf8* pOwner ) const { return _resolved.hasOwner( hashed_string( pOwner ) ); }
        [[nodiscard]] uint32              countTrace( const utf8* pSource ) const { return _resolved.countTrace( hashed_string( pSource ) ); }
    };
} // namespace

// 몸 · 슬롯 순서로 나오고, 소켓 이름 공간은 주인을 앞에 붙인 출처로 남는다.
SW_TEST_CASE_F( AppearanceResolverTest, BodyThenSlotsInOrder )
{
    equip( "Head", "helmet" );
    equip( "MainHand", "rifle" );
    resolve();

    SW_REQUIRE_EQUAL( _resolved._listPart.size(), size_t{ 3 } );
    SW_EXPECT_TRUE( _resolved._listPart[0]._partName == hashed_string( "Body" ) );
    SW_EXPECT_TRUE( _resolved._listPart[1]._owner == hashed_string( "Head" ) );
    SW_EXPECT_TRUE( _resolved._listPart[2]._asset == hashed_string( "rifle.prefab" ) );
    SW_EXPECT_TRUE( _resolved._listPart[2]._kind == AppearancePartKind::SocketPrefab );
    SW_EXPECT_TRUE( _resolved._listPart[1]._placement._listSocket.size() == 2 );
    SW_EXPECT_NEAR( _resolved._listPart[1]._placement._offset._y, 1.0f, 1.0e-6f );

    SW_REQUIRE_EQUAL( _resolved._listSocketSource.size(), size_t{ 2 } );
    SW_EXPECT_TRUE( _resolved._listSocketSource[0]._owner.empty() );
    SW_EXPECT_TRUE( _resolved._listSocketSource[1]._owner == hashed_string( "MainHand" ) );
    SW_EXPECT_TRUE( _resolved._listSocketSource[1]._socketSet == hashed_string( "rifle.sockets" ) );
}

// 아이템 표를 거쳐 외형을 고르고, 표에 없으면 "외형 없음" 을 설명에 남긴다.
SW_TEST_CASE_F( AppearanceResolverTest, ItemCatalogPicksTheVisual )
{
    ItemCatalog catalog;
    SW_REQUIRE_TRUE( catalog.loadFromXMLText( R"(<ItemCatalog><Item id="iron_helm" category="Armor" slot="Head" maxStack="1" weight="1" visual="helmet"/></ItemCatalog>)", "test" ) );
    load( R"(<CustomizationSchemaCatalog><Schema id="Gun"><Slider name="Wear" min="0" max="1" default="0"/></Schema></CustomizationSchemaCatalog>)" );
    SW_REQUIRE_TRUE( _database.finishLoad( &catalog ) );

    AppearanceSlotRequest& helm = equip( "Head", "helmet" );
    helm._itemID                = hashed_string( "iron_helm" );
    helm._visibleVisual         = hashed_string{};
    AppearanceSlotRequest& odd  = equip( "Torso", "armor" );
    odd._itemID                 = hashed_string( "no_such_item" );
    odd._visibleVisual          = hashed_string{};
    resolve();

    const ResolvedPart* pShell = findPart( "Head", "Shell" );
    SW_REQUIRE_NOT_NULL( pShell );
    SW_EXPECT_TRUE( pShell->_itemID == hashed_string( "iron_helm" ) );
    SW_EXPECT_TRUE( pShell->_visualID == hashed_string( "helmet" ) );
    SW_EXPECT_FALSE( hasOwner( "Torso" ) );
    SW_EXPECT_EQUAL( countTrace( "Torso" ), 1u );
    SW_EXPECT_TRUE( _database.findItemVisual( hashed_string( "iron_helm" ) ) != nullptr );
}

// 조건이 깨진 장비는 숨고, 숨은 장비는 점유하지 않는다. 먼저 보이는 장비의 점유가 이긴다.
SW_TEST_CASE_F( AppearanceResolverTest, SuppressedAndOccupiedSlotsHide )
{
    equip( "MainHand", "rifle" );
    equip( "OffHand", "shield" );
    resolve();
    SW_EXPECT_TRUE( hasOwner( "MainHand" ) );
    SW_EXPECT_FALSE( hasOwner( "OffHand" ) );
    SW_EXPECT_EQUAL( countTrace( "MainHand" ), 2u ); // 형상 변경 · 점유

    _spec.findSlot( hashed_string( "MainHand" ) )->_bSuppressed = SW_TRUE;
    resolve();
    SW_EXPECT_FALSE( hasOwner( "MainHand" ) );
    SW_EXPECT_TRUE( hasOwner( "OffHand" ) ); // 숨은 총은 보조 손을 가리지 않는다
}

// 세트를 다 갖추면 그 칸이 완성 외형으로 바뀌고, 숨은 조각은 세지 않으며, 몸 종류별 조각을 쓴다.
SW_TEST_CASE_F( AppearanceResolverTest, SetCompletionReplacesItsSlots )
{
    load( R"(<EquipmentSetCatalog><Set id="Knight">
        <Piece slot="Head" items="helmet"/><Piece slot="OffHand" items="shield"/>
        <Variant bodyType="Giant"><Piece slot="Head" items="hood"/></Variant>
        <Complete visual="cape_knight" slots="OffHand"/>
    </Set></EquipmentSetCatalog>)" );
    equip( "Head", "helmet" );
    equip( "OffHand", "shield" );
    resolve();
    SW_EXPECT_TRUE( findPart( "OffHand", "Crest" ) != nullptr );
    SW_EXPECT_TRUE( findPart( "OffHand", "Board" ) == nullptr );
    SW_EXPECT_TRUE( findPart( "Head", "Shell" ) != nullptr );
    SW_EXPECT_EQUAL( countTrace( "Knight" ), 1u );

    _spec._bodyType = hashed_string( "Giant" );
    resolve();
    SW_EXPECT_TRUE( findPart( "OffHand", "Board" ) != nullptr ); // 거인은 두건이 조각이다

    _spec._bodyType                                         = hashed_string{};
    _spec.findSlot( hashed_string( "Head" ) )->_bSuppressed = SW_TRUE;
    resolve();
    SW_EXPECT_TRUE( findPart( "OffHand", "Board" ) != nullptr );
}

// 조건은 태그(칸 · 아무 곳) · 캐릭터 태그 · 점유 · 몸 종류 · 체형이고 not 으로 뒤집는다. 조건이 모두 맞아야 동작한다.
SW_TEST_CASE_F( AppearanceResolverTest, RuleConditionsAllHaveToMatch )
{
    load( R"(<AppearanceRuleTable>
        <Rule id="TargetTag"><When target="Head" tag="Helmet.Full"/><Hide region="R1"/></Rule>
        <Rule id="WrongTarget"><When target="Torso" tag="Helmet.Full"/><Hide region="R2"/></Rule>
        <Rule id="AnyTag"><When tag="Helmet.Full"/><Hide region="R3"/></Rule>
        <Rule id="Occupied"><When occupied="Head"/><Hide region="R4"/></Rule>
        <Rule id="NotOccupied"><When occupied="Head" not="true"/><Hide region="R5"/></Rule>
        <Rule id="BodyType"><When bodyType="Giant,Dwarf"/><Hide region="R6"/></Rule>
        <Rule id="BodyShape"><When bodyShape="Heavy"/><Hide region="R7"/></Rule>
        <Rule id="CharacterTag"><When characterTag="Faction.Army"/><Hide region="R8"/></Rule>
        <Rule id="Both"><When occupied="Head"/><When occupied="Torso"/><Hide region="R9"/></Rule>
    </AppearanceRuleTable>)" );
    equip( "Head", "helmet" );
    _spec._bodyType = hashed_string( "Dwarf" );
    _spec._tags.addTag( TagID::request( "Faction.Army" ) );
    resolve();

    const auto isHidden = [this]( const utf8* pRegion )
    { return _resolved.isRegionHidden( hashed_string( pRegion ) ); };
    SW_EXPECT_TRUE( isHidden( "R1" ) );
    SW_EXPECT_FALSE( isHidden( "R2" ) );
    SW_EXPECT_TRUE( isHidden( "R3" ) );
    SW_EXPECT_TRUE( isHidden( "R4" ) );
    SW_EXPECT_FALSE( isHidden( "R5" ) );
    SW_EXPECT_TRUE( isHidden( "R6" ) );
    SW_EXPECT_FALSE( isHidden( "R7" ) );
    SW_EXPECT_TRUE( isHidden( "R8" ) );
    SW_EXPECT_FALSE( isHidden( "R9" ) );
    SW_EXPECT_EQUAL( countTrace( "WrongTarget" ), 0u );
}

// 숨김은 대상 · 부품 · 아이템 태그로 걸고 몸은 숨지 않는다. 규칙은 서로의 결과를 조건으로 보지 않는다.
SW_TEST_CASE_F( AppearanceResolverTest, HideRulesDoNotChainAndSpareTheBody )
{
    load( R"(<AppearanceRuleTable>
        <Rule id="HideHelmet"><When occupied="Torso"/><Hide target="Head"/></Rule>
        <Rule id="SeesHelmet"><When occupied="Head"/><Hide region="SawHelmet"/></Rule>
        <Rule id="HideTie"><When occupied="Torso"/><Hide target="Hood" part="Tie"/></Rule>
        <Rule id="HideRifles"><When occupied="Torso"/><Hide itemTag="Weapon.Rifle"/></Rule>
    </AppearanceRuleTable>)" );
    equip( "Head", "helmet" );
    equip( "Torso", "armor" );
    equip( "MainHand", "rifle" );
    equip( "Hood", "hood" );
    resolve();

    SW_EXPECT_FALSE( hasOwner( "Head" ) );
    SW_EXPECT_TRUE( _resolved.isRegionHidden( hashed_string( "SawHelmet" ) ) ); // 앞 규칙이 숨겼어도 조건은 숨기기 전을 본다
    SW_EXPECT_FALSE( hasOwner( "MainHand" ) );
    SW_EXPECT_TRUE( findPart( "Hood", "Tie" ) == nullptr );
    SW_EXPECT_TRUE( findPart( "Hood", "Cloth" ) != nullptr );
    SW_EXPECT_TRUE( findPart( "", "Body" ) != nullptr );
}

// 같은 대상은 우선순위가 높은 규칙이 이기고 진 규칙도 설명에 남는다. 부품을 지목한 규칙이 전체 규칙보다 앞선다.
SW_TEST_CASE_F( AppearanceResolverTest, HigherPriorityRuleWinsAndLoserIsExplained )
{
    load( R"(<AppearanceRuleTable>
        <Rule id="Low" priority="1"><When occupied="Torso"/><SwapMaterial target="Torso" material="low.material"/></Rule>
        <Rule id="High" priority="5"><When occupied="Torso"/><SwapMaterial target="Torso" material="high.material"/></Rule>
        <Rule id="Part" priority="1"><When occupied="Torso"/><SwapMesh target="Torso" part="Plate" mesh="part.mesh"/></Rule>
        <Rule id="All" priority="2"><When occupied="Torso"/><SwapMesh target="Torso" mesh="all.mesh"/></Rule>
    </AppearanceRuleTable>)" );
    equip( "Torso", "armor" );
    resolve();

    const ResolvedPart* pPlate = findPart( "Torso", "Plate" );
    SW_REQUIRE_NOT_NULL( pPlate );
    SW_EXPECT_TRUE( pPlate->_material == hashed_string( "high.material" ) );
    SW_EXPECT_TRUE( pPlate->_asset == hashed_string( "part.mesh" ) );
    SW_EXPECT_TRUE( findPart( "Torso", "Strap" )->_asset == hashed_string( "all.mesh" ) );
    SW_EXPECT_TRUE( findPart( "Torso", "Strap" )->_material == hashed_string( "high.material" ) );
    SW_EXPECT_EQUAL( countTrace( "Low" ), 2u ); // 발동 · 짐
}

// 메시 변형은 규칙 > 피해 단계 > 꾸미기 순이고, 부품에 없는 변형은 그 부품에 영향이 없다.
SW_TEST_CASE_F( AppearanceResolverTest, VariantOrderIsRuleThenDamageThenCustomization )
{
    load( R"(<AppearanceRuleTable><Rule id="Clean"><When characterTag="Tidy"/><Variant target="Torso" name="Clean"/></Rule></AppearanceRuleTable>)" );
    AppearanceSlotRequest& armor = equip( "Torso", "armor" );
    armor._damage                = 0.5f;
    resolve();
    SW_EXPECT_TRUE( findPart( "Torso", "Plate" )->_variant == hashed_string( "Torn" ) );
    SW_EXPECT_TRUE( findPart( "Torso", "Plate" )->_asset == hashed_string( "plate_torn.mesh" ) );
    SW_EXPECT_TRUE( findPart( "Torso", "Plate" )->_damageStage == hashed_string( "Scuffed" ) );
    SW_EXPECT_TRUE( findPart( "Torso", "Strap" )->_variant.empty() );

    _spec._tags.addTag( TagID::request( "Tidy" ) );
    resolve();
    const ResolvedPart* pPlate = findPart( "Torso", "Plate" );
    SW_EXPECT_TRUE( pPlate->_variant == hashed_string( "Clean" ) );
    SW_EXPECT_TRUE( pPlate->_asset == hashed_string( "plate.mesh" ) ); // 비워 둔 메시는 기본이다
    SW_EXPECT_TRUE( pPlate->_material == hashed_string( "plate_clean.material" ) );
}

// 피해 단계는 머티리얼 값을 더하고, 단계 아래면 멀쩡하다. 입력은 전송 정밀도로 맞춘다.
SW_TEST_CASE_F( AppearanceResolverTest, DamageStageAppliesAboveItsThreshold )
{
    AppearanceSlotRequest& armor = equip( "Torso", "armor" );
    armor._damage                = 0.2f;
    resolve();
    SW_EXPECT_TRUE( findPart( "Torso", "Plate" )->_damageStage.empty() );
    SW_EXPECT_TRUE( _resolved._listMaterialValue.empty() );

    _spec.findSlot( hashed_string( "Torso" ) )->_damage = 0.25f;
    resolve();
    SW_REQUIRE_EQUAL( _resolved._listMaterialValue.size(), size_t{ 1 } );
    SW_EXPECT_TRUE( _resolved._listMaterialValue[0]._part == hashed_string( "Plate" ) );
    SW_EXPECT_TRUE( _resolved._listMaterialValue[0]._owner == hashed_string( "Torso" ) );

    SW_EXPECT_NEAR( AppearanceResolver::snapDamage( -1.0f ), 0.0f, 1.0e-6f );
    SW_EXPECT_NEAR( AppearanceResolver::snapDamage( 2.0f ), 1.0f, 1.0e-6f );
    SW_EXPECT_NEAR( AppearanceResolver::snapDamage( 0.5f ), 128.0f / 255.0f, 1.0e-6f );
}

// 맞아서 · 단계로 떨어진 부품은 그리는 목록에서 떨어진 목록으로 옮겨 간다. 떨어질 수 없는 부품은 요청을 무시한다.
SW_TEST_CASE_F( AppearanceResolverTest, DetachedPartsLeaveTheDrawList )
{
    AppearanceSlotRequest& armor = equip( "Torso", "armor" );
    armor._listDetachedPart      = { hashed_string( "Strap" ), hashed_string( "Rivet" ) };
    resolve();
    SW_EXPECT_TRUE( findPart( "Torso", "Strap" ) == nullptr );
    SW_EXPECT_TRUE( findPart( "Torso", "Rivet" ) != nullptr );
    SW_REQUIRE_EQUAL( _resolved._listDetachedPart.size(), size_t{ 1 } );
    SW_EXPECT_TRUE( _resolved._listDetachedPart[0]._partName == hashed_string( "Strap" ) );
    SW_EXPECT_NEAR( _resolved._listDetachedPart[0]._impulse._y, 2.0f, 1.0e-6f );
    SW_EXPECT_TRUE( _resolved._listDetachedPart[0]._bFromDamageStage == SW_FALSE );

    _spec.findSlot( hashed_string( "Torso" ) )->_listDetachedPart.clear();
    _spec.findSlot( hashed_string( "Torso" ) )->_damage = 0.9f;
    resolve();
    SW_EXPECT_TRUE( findPart( "Torso", "Pad" ) == nullptr );
    SW_EXPECT_TRUE( findPart( "Torso", "Strap" ) != nullptr );
    SW_REQUIRE_EQUAL( _resolved._listDetachedPart.size(), size_t{ 1 } );
    SW_EXPECT_TRUE( _resolved._listDetachedPart[0]._bFromDamageStage == SW_TRUE );
}

// 외형 상태는 부품을 옮기거나 숨기고, 없는 상태는 기본 상태로 돌아간다.
SW_TEST_CASE_F( AppearanceResolverTest, StateMovesAndHidesParts )
{
    AppearanceSlotRequest& armor = equip( "Torso", "armor" );
    armor._state                 = hashed_string( "Stowed" );
    resolve();
    SW_EXPECT_TRUE( findPart( "Torso", "Rivet" ) == nullptr );
    SW_EXPECT_TRUE( findPart( "Torso", "Strap" )->_placement._listSocket[0] == hashed_string( "Hip" ) );
    SW_EXPECT_TRUE( findPart( "Torso", "Strap" )->_state == hashed_string( "Stowed" ) );

    _spec.findSlot( hashed_string( "Torso" ) )->_state = hashed_string( "Nope" );
    resolve();
    SW_EXPECT_TRUE( findPart( "Torso", "Rivet" ) != nullptr );
    SW_EXPECT_TRUE( findPart( "Torso", "Strap" )->_placement._listSocket[0] == hashed_string( "Back" ) );
    SW_EXPECT_TRUE( findPart( "Torso", "Strap" )->_state == hashed_string( "Worn" ) );
}

// 몸 수정 부품은 그리지 않고 몸에 모프 · 값 · 숨김 영역을 건다. 숨김 영역은 처음 자리를 지켜 한 번만 남는다.
SW_TEST_CASE_F( AppearanceResolverTest, BodyModificationAppliesToTheBody )
{
    equip( "Torso", "tattoo" );
    equip( "Head", "shirt" );
    resolve();
    SW_EXPECT_FALSE( hasOwner( "Torso" ) );
    const ResolvedMorph* pMorph = _resolved.findMorph( hashed_string{}, hashed_string( "Flat" ) );
    SW_REQUIRE_NOT_NULL( pMorph );
    SW_EXPECT_NEAR( pMorph->_weight, 0.25f, 1.0e-6f );
    SW_REQUIRE_EQUAL( _resolved._listMaterialValue.size(), size_t{ 1 } );
    SW_EXPECT_TRUE( _resolved._listMaterialValue[0]._owner.empty() );
    SW_EXPECT_NEAR( _resolved._listMaterialValue[0]._value._x, 1.0f, 1.0e-6f );
    SW_REQUIRE_EQUAL( _resolved._listHiddenRegion.size(), size_t{ 2 } );
    SW_EXPECT_TRUE( _resolved._listHiddenRegion[0] == hashed_string( "Chest" ) );
    SW_EXPECT_TRUE( _resolved._listHiddenRegion[1] == hashed_string( "Arm" ) );
}

// 슬라이더 · 색은 정해진 곳으로 가고, 스키마에 없는 값은 버려지며, 조건이 안 맞는 값은 쓰지 않는다.
SW_TEST_CASE_F( AppearanceResolverTest, CharacterCustomizationDrivesItsTargets )
{
    load( R"(<CustomizationSchemaCatalog><Schema id="Human">
        <Slider name="Height" min="0" max="2" default="1">
          <Drive kind="Morph" target="Tall" from="0" to="10"/><Drive kind="BoneProportion" target="Spine" from="1" to="2"/><Drive kind="MaterialScalar" target="_rough"/>
        </Slider>
        <Color name="Dye" default="1 0 0 1"><Drive kind="MaterialColor" target="_color"/><Drive kind="DyeChannel" target="_dye" channel="2"/><Drive kind="PaletteSwap" target="_pal"/></Color>
        <Choice name="Build" default="Normal"><Option name="Normal"/><Option name="Slim" variant="Slim"/></Choice>
        <Choice name="Hood" default="Bare"><Option name="Bare"/><Option name="Up" visual="hood"/></Choice>
        <Slider name="Beard" min="0" max="1" default="0.5"><Condition parameter="Build" options="Slim"/><Drive kind="Morph" target="Beard"/></Slider>
    </Schema></CustomizationSchemaCatalog>)" );
    _spec._schema = hashed_string( "Human" );
    _spec._customization.setNumber( hashed_string( "Height" ), 2.0f );
    _spec._customization.setNumber( hashed_string( "Stranger" ), 1.0f );
    resolve();

    SW_EXPECT_NEAR( _resolved.findMorph( hashed_string{}, hashed_string( "Tall" ) )->_weight, 10.0f, 1.0e-5f );
    SW_REQUIRE_EQUAL( _resolved._listBoneProportion.size(), size_t{ 1 } );
    SW_EXPECT_NEAR( _resolved._listBoneProportion[0]._value, 2.0f, 1.0e-5f );
    SW_EXPECT_EQUAL( countTrace( "Stranger" ), 1u );
    SW_EXPECT_TRUE( _resolved.findMorph( hashed_string{}, hashed_string( "Beard" ) ) == nullptr );
    SW_EXPECT_EQUAL( countTrace( "Beard" ), 1u ); // 꺼진 이유가 남는다

    uint32 scalarCount  = 0;
    uint32 dyeChannel   = 0;
    uint32 paletteCount = 0;
    for ( const ResolvedMaterialValue& value : _resolved._listMaterialValue )
    {
        scalarCount += value._shape == ResolvedMaterialShape::Scalar ? 1u : 0u;
        dyeChannel += value._target == ResolvedMaterialTarget::DyeChannel ? static_cast<uint32>( value._channel ) : 0u;
        paletteCount += value._target == ResolvedMaterialTarget::PaletteSwap ? 1u : 0u;
    }
    SW_EXPECT_EQUAL( scalarCount, 1u );
    SW_EXPECT_EQUAL( dyeChannel, 2u );
    SW_EXPECT_EQUAL( paletteCount, 1u );

    // 고르기: 몸의 변형을 바꾸고, 외형을 고르면 매개변수 이름이 주인인 항목이 생긴다.
    _spec._customization.setOption( hashed_string( "Build" ), hashed_string( "Slim" ) );
    _spec._customization.setOption( hashed_string( "Hood" ), hashed_string( "Up" ) );
    resolve();
    SW_EXPECT_TRUE( findPart( "", "Body" )->_asset == hashed_string( "slim.mesh" ) );
    SW_EXPECT_TRUE( findPart( "", "Body" )->_skeleton == hashed_string( "slim.skeleton.xml" ) ); // 변형이 스켈레톤도 고른다 — 외형이 런타임에 뼈 비율을 고르는 길이다
    SW_EXPECT_TRUE( findPart( "Hood", "Cloth" ) != nullptr );
    SW_EXPECT_TRUE( _resolved.findMorph( hashed_string{}, hashed_string( "Beard" ) ) != nullptr );
}

// 아이템 꾸미기: 변형 · 머티리얼 변형 · 같은 주인의 추가 외형 · 주인 앞 이름이 붙은 소켓의 부착물.
SW_TEST_CASE_F( AppearanceResolverTest, ItemCustomizationChangesThatItemOnly )
{
    load( R"(<CustomizationSchemaCatalog><Schema id="Gun">
        <Choice name="Barrel" default="Short"><Option name="Short"/><Option name="Long" variant="Long" materialVariant="Desert"/></Choice>
        <Choice name="Sight" default="Iron"><Option name="Iron"/><Option name="Scope" visual="scope"/></Choice>
        <Attachment name="Rail" socket="Rail" default="Red"><Option name="Red" prefab="red_dot.prefab" offset="0 0 1"/></Attachment>
    </Schema></CustomizationSchemaCatalog>)" );
    AppearanceSlotRequest& rifle = equip( "MainHand", "rifle" );
    rifle._customization.setOption( hashed_string( "Barrel" ), hashed_string( "Long" ) );
    rifle._customization.setOption( hashed_string( "Sight" ), hashed_string( "Scope" ) );
    rifle._customization.setOption( hashed_string( "Rail" ), hashed_string( "Red" ) );
    resolve();

    const ResolvedPart* pGun = findPart( "MainHand", "Gun" );
    SW_REQUIRE_NOT_NULL( pGun );
    SW_EXPECT_TRUE( pGun->_asset == hashed_string( "rifle_long.prefab" ) );
    SW_EXPECT_TRUE( pGun->_materialVariant == hashed_string( "Desert" ) );
    SW_EXPECT_TRUE( pGun->_material == hashed_string( "desert.material" ) );
    SW_EXPECT_TRUE( findPart( "MainHand", "Lens" ) != nullptr );
    SW_REQUIRE_EQUAL( _resolved._listAttachment.size(), size_t{ 1 } );
    SW_EXPECT_TRUE( _resolved._listAttachment[0]._placement._listSocket[0] == hashed_string( "MainHand.Rail" ) );
    SW_EXPECT_NEAR( _resolved._listAttachment[0]._placement._offset._z, 1.0f, 1.0e-6f );
}

// 규칙의 소켓 덮어쓰기는 요청의 같은 이름을 바꾸고, 새 이름은 뒤에 붙는다. 규칙 모프는 값을 덮어쓴다.
SW_TEST_CASE_F( AppearanceResolverTest, RuleOverridesSocketsAndMorphs )
{
    load( R"(<AppearanceRuleTable>
        <Rule id="Socket"><When occupied="Torso"/><OverrideSocket name="Grip" parent="Hand" offset="0 1 0"/><OverrideSocket name="Extra" parent="Neck"/></Rule>
        <Rule id="Morph"><When occupied="Torso"/><Morph name="Flat" weight="0.75"/></Rule>
    </AppearanceRuleTable>)" );
    AppearanceSocketOverride existing;
    existing._name = hashed_string( "Grip" );
    existing._placement._listSocket.push_back( hashed_string( "Old" ) );
    _spec._listSocketOverride.push_back( existing );
    equip( "Torso", "tattoo" );
    resolve();

    SW_REQUIRE_EQUAL( _resolved._listSocketOverride.size(), size_t{ 2 } );
    SW_EXPECT_TRUE( _resolved._listSocketOverride[0]._placement._listSocket[0] == hashed_string( "Hand" ) );
    SW_EXPECT_TRUE( _resolved._listSocketOverride[1]._name == hashed_string( "Extra" ) );
    SW_EXPECT_NEAR( _resolved.findMorph( hashed_string{}, hashed_string( "Flat" ) )->_weight, 0.75f, 1.0e-6f );
    SW_EXPECT_EQUAL( _resolved._listMorph.size(), size_t{ 1 } );
}

// 같은 입력은 같은 해시다. 외형 상태만 바뀌면 병합 메시 해시는 그대로이고 전체 해시만 바뀐다.
SW_TEST_CASE_F( AppearanceResolverTest, MeshHashIgnoresWhatTheBakeDoesNotUse )
{
    AppearanceSlotRequest& armor = equip( "Torso", "armor" );
    armor._state                 = hashed_string( "Worn" );
    resolve();
    const uint64 hash     = _resolved._hash;
    const uint64 meshHash = _resolved._meshHash;
    SW_EXPECT_EQUAL( AppearanceResolver::computeHash( _resolved ), hash );
    resolve();
    SW_EXPECT_EQUAL( _resolved._hash, hash );

    _spec.findSlot( hashed_string( "Torso" ) )->_state = hashed_string( "Stowed" );
    resolve();
    SW_EXPECT_EQUAL( _resolved._meshHash, meshHash );
    SW_EXPECT_NOT_EQUAL( _resolved._hash, hash );

    _resolved._listTrace.clear(); // 설명은 해시에 들지 않는다
    SW_EXPECT_EQUAL( AppearanceResolver::computeHash( _resolved ), _resolved._hash );

    _spec.findSlot( hashed_string( "Torso" ) )->_damage = 0.5f; // 메시 변형이 바뀐다
    resolve();
    SW_EXPECT_NOT_EQUAL( _resolved._meshHash, meshHash );
}

// 결함: 구간 머티리얼이 병합 메시에 들어가는데 해시에 없어, 같은 메시의 다른 구간 머티리얼로 갈아입어도 옛 베이크를 다시 썼다.
SW_TEST_CASE_F( AppearanceResolverTest, MeshHashSeesSectionMaterials )
{
    equip( "Torso", "sectioned_a" );
    resolve();
    const uint64 meshHash = _resolved._meshHash;

    _spec.findSlot( hashed_string( "Torso" ) )->_visibleVisual = hashed_string( "sectioned_b" );
    resolve();
    SW_EXPECT_NOT_EQUAL( _resolved._meshHash, meshHash );
}

// 결함: 적용 모양(스칼라 · 벡터)이 다른 같은 값, 떨어진 부품의 에셋 · 배치가 해시에 들지 않았다 — "결과 전체의 해시" 가 다른 결과를 같다고 했다.
SW_TEST_CASE( AppearanceResolverHashTest, HashSeesEveryOutputField )
{
    ResolvedAppearance    base;
    ResolvedMaterialValue value;
    value._name  = hashed_string( "_x" );
    value._value = float4( 1.0f, 0.0f, 0.0f, 0.0f );
    base._listMaterialValue.push_back( value );
    ResolvedDetachedPart detached;
    detached._partName = hashed_string( "Strap" );
    detached._asset    = hashed_string( "a.prefab" );
    base._listDetachedPart.push_back( detached );

    ResolvedAppearance scalar           = base;
    scalar._listMaterialValue[0]._shape = ResolvedMaterialShape::Scalar;
    SW_EXPECT_NOT_EQUAL( AppearanceResolver::computeMeshHash( scalar ), AppearanceResolver::computeMeshHash( base ) );

    ResolvedAppearance otherAsset          = base;
    otherAsset._listDetachedPart[0]._asset = hashed_string( "b.prefab" );
    SW_EXPECT_NOT_EQUAL( AppearanceResolver::computeHash( otherAsset ), AppearanceResolver::computeHash( base ) );

    ResolvedAppearance otherPlace                      = base;
    otherPlace._listDetachedPart[0]._placement._offset = float3{ 1.0f, 0.0f, 0.0f };
    SW_EXPECT_NOT_EQUAL( AppearanceResolver::computeHash( otherPlace ), AppearanceResolver::computeHash( base ) );

    ResolvedPart spritePart;
    spritePart._kind          = AppearancePartKind::Sprite;
    ResolvedAppearance sprite = base;
    sprite._listPart.push_back( spritePart );
    SW_EXPECT_EQUAL( AppearanceResolver::computeMeshHash( sprite ), AppearanceResolver::computeMeshHash( base ) ); // 스킨드가 아니면 병합에 들지 않는다
}

// 장비 상태를 요청에 옮긴다: 아이템 · 꾸미기 · 떨어진 부품 · 숨김, 피해는 맞은 값과 닳은 값 중 큰 쪽. 빈 칸은 비운다.
SW_TEST_CASE( AppearanceInputUtilTest, ApplyEquipmentCopiesTheInstanceState )
{
    ItemCatalog catalog;
    SW_REQUIRE_TRUE( catalog.loadFromXMLText( R"(<ItemCatalog><Item id="helm" category="Armor" slot="Head" maxStack="1" weight="1" durability="100"/></ItemCatalog>)", "test" ) );
    Equipment equipment;
    equipment.initialize( &catalog, "Head" );

    InventorySlot item;
    item._itemID     = hashed_string( "helm" );
    item._count      = 1;
    item._durability = 25.0f;
    item._damage     = 0.5f;
    item._listDetachedPart.push_back( hashed_string( "Visor" ) );
    item._customization.setNumber( hashed_string( "Tint" ), 0.5f );
    vector<InventorySlot> listRemoved;
    SW_REQUIRE_TRUE( equipment.equip( hashed_string( "Head" ), item, listRemoved ) == EquipResult::Ok );

    CharacterAppearanceSpec spec;
    spec._listSlot.resize( 2 );
    spec._listSlot[0]._slot   = hashed_string( "Head" );
    spec._listSlot[0]._state  = hashed_string( "Kept" );
    spec._listSlot[1]._slot   = hashed_string( "Unknown" );
    spec._listSlot[1]._itemID = hashed_string( "stale" );
    spec._listSlot[1]._damage = 1.0f;
    spec._listSlot[1]._listDetachedPart.push_back( hashed_string( "Stale" ) );
    AppearanceInputUtil::applyEquipment( equipment, spec );

    SW_EXPECT_TRUE( spec._listSlot[0]._itemID == hashed_string( "helm" ) );
    SW_EXPECT_NEAR( spec._listSlot[0]._damage, AppearanceResolver::snapDamage( 0.75f ), 1.0e-6f ); // 닳음 0.75 > 맞음 0.5
    SW_EXPECT_EQUAL( spec._listSlot[0]._listDetachedPart.size(), size_t{ 1 } );
    SW_EXPECT_TRUE( spec._listSlot[0]._customization.findValue( hashed_string( "Tint" ) ) != nullptr );
    SW_EXPECT_TRUE( spec._listSlot[0]._state == hashed_string( "Kept" ) ); // 외형 상태는 요청의 것이다

    SW_EXPECT_TRUE( spec._listSlot[1]._itemID.empty() );
    SW_EXPECT_NEAR( spec._listSlot[1]._damage, 0.0f, 1.0e-6f );
    SW_EXPECT_TRUE( spec._listSlot[1]._listDetachedPart.empty() );
}
