/**
 * @file AppearanceTestFixture.h
 * @brief 외형 시험의 공통 데이터 — 기사 세트 · 로브 · 양손검 · 망토(세트 조건) · 칼(꾸미기 · 상태 · 피해 단계) · 사람 스키마 · 규칙 · 프리셋입니다.
 */
#pragma once
#include "GameFramework/Appearance/AppearanceDatabase.h"
#include "GameFramework/Appearance/AppearanceResolver.h"
#include "GameFramework/Inventory/Equipment.h"
#include "GameFramework/Inventory/ItemCatalog.h"

namespace appearancetest
{
    inline constexpr const utf8* kItemXml = R"(
<ItemCatalog>
  <Item id="helm" slot="Head" visual="helm_full" tags="Armor.Heavy"/>
  <Item id="cap" slot="Head" visual="cap"/>
  <Item id="plate" slot="Body" visual="plate" tags="Armor.Heavy"/>
  <Item id="plate_f" slot="Body" visual="plate" tags="Armor.Heavy"/>
  <Item id="shirt" slot="Body" visual="shirt" tags="Cloth"/>
  <Item id="robe" slot="Body" visual="robe" tags="Cloth"/>
  <Item id="pants" slot="Legs" visual="pants"/>
  <Item id="greaves" slot="Legs" visual="greaves" tags="Armor.Heavy"/>
  <Item id="sword" slot="Weapon" visual="sword" durability="100"/>
  <Item id="greatsword" slot="Weapon" visual="greatsword"/>
  <Item id="shield" slot="Weapon" visual="shield"/>
  <Item id="cape_hidden" slot="Back" visual="cape" breakPolicy="KeepHidden"><Stats charisma="3"/><Requires set="Knight"/></Item>
  <Item id="cape_together" slot="Back" visual="cape" breakPolicy="UnequipTogether"><Requires set="Knight"/></Item>
  <Item id="cape_refuse" slot="Back" visual="cape" breakPolicy="RefuseUnequip"><Requires set="Knight"/></Item>
  <Item id="banner" slot="Back" visual="cape"><Requires set="Knight" pieces="2"/></Item>
  <Item id="sigil" slot="Ring"><Requires equippedTag="Armor"/></Item>
  <Item id="crown" slot="Head"><Requires characterTag="Class.Knight"/></Item>
  <Item id="belt_big" slot="Belt"><Requires bodyShape="Heavy,Huge"/></Item>
</ItemCatalog>
)";

    inline constexpr const utf8* kSlotXml = R"(
<SlotTable>
  <Slot name="Head"/><Slot name="Body"/><Slot name="Legs"/>
  <Slot name="MainHand" accept="Weapon"/><Slot name="OffHand" accept="Weapon"/>
  <Slot name="Back"/><Slot name="Ring"/><Slot name="Belt"/>
  <Occupancy id="Robe" slots="Body,Legs"/>
  <Occupancy id="TwoHanded" slots="MainHand,OffHand"/>
</SlotTable>
)";

    inline constexpr const utf8* kSetXml = R"(
<EquipSetCatalog>
  <Set id="Knight">
    <Piece slot="Head" items="helm"/><Piece slot="Body" items="plate"/><Piece slot="Legs" items="greaves"/>
    <Variant bodyType="Female"><Piece slot="Body" items="plate_f"/></Variant>
    <Complete visual="knight_full" slots="Body,Legs"/>
  </Set>
</EquipSetCatalog>
)";

    inline constexpr const utf8* kSchemaXml = R"(
<CustomizationSchemaCatalog>
  <Schema id="Human">
    <Slider name="Height" category="Body" min="0" max="2" default="1"><Drive kind="BoneProportion" target="Height"/></Slider>
    <Slider name="Fat" category="Body" min="0" max="1" default="0"><Drive kind="Morph" target="Fat" from="0" to="0.5"/></Slider>
    <Slider name="Ear.L" category="Face" symmetry="Ears" min="0" max="1"><Drive kind="Morph" target="Ear.L"/></Slider>
    <Slider name="Ear.R" category="Face" symmetry="Ears" min="0" max="1"><Drive kind="Morph" target="Ear.R"/></Slider>
    <Color name="Skin" category="Skin" default="1 0.8 0.6 1"><Drive kind="MaterialColor" target="SkinTint"/></Color>
    <Choice name="Hair" category="Hair" default="Long"><Option name="Long" visual="hair_long"/><Option name="Bald"/></Choice>
    <Color name="HairDye" category="Hair" default="0.2 0.1 0 1"><Drive kind="DyeChannel" target="Hair" channel="2"/></Color>
    <Choice name="Beard" category="Hair" default="Clean"><Option name="Clean"/><Option name="Full" visual="beard_full"/></Choice>
    <Slider name="BeardLength" category="Hair" min="0" max="1" default="0.5"><Condition parameter="Beard" options="Full"/><Drive kind="Morph" target="BeardLength"/></Slider>
    <Slider name="BeardBraid" category="Hair" min="0" max="1" default="0"><Condition parameter="BeardLength" min="0.5" max="1"/><Drive kind="Morph" target="BeardBraid"/></Slider>
    <Slider name="Muscle" category="Body" min="0" max="1" default="0.5"><Condition parameter="Fat" min="0" max="0.5"/><Drive kind="Morph" target="Muscle"/></Slider>
    <Choice name="Physique" category="Body" default="Normal"><Option name="Normal"/><Option name="Lean" variant="Lean"/></Choice>
    <Attachment name="Earring" category="Face" socket="ear.l" default="Off"><Option name="Off"/><Option name="Hoop" prefab="p/hoop.prefab.xml"/></Attachment>
  </Schema>
  <Schema id="Sword">
    <Attachment name="Gem" socket="GemSlot" default="Empty"><Option name="Empty"/><Option name="Ruby" prefab="p/ruby.prefab.xml" offset="0 0.1 0"/></Attachment>
    <Choice name="Finish" default="Steel"><Option name="Steel"/><Option name="Gold" materialVariant="Gold"/><Option name="Chipped" variant="Chipped"/></Choice>
    <Color name="Hilt" default="0.5 0.2 0.1 1"><Drive kind="DyeChannel" target="Hilt" channel="0"/></Color>
  </Schema>
</CustomizationSchemaCatalog>
)";

    inline constexpr const utf8* kVisualXml = R"(
<ItemVisualCatalog>
  <ItemVisual id="body_human"><Part name="Body" kind="Skinned" mesh="m/body.mesh" sockets="s/body.sockets.xml"><Variant name="Lean" mesh="m/body_lean.mesh"/></Part></ItemVisual>
  <ItemVisual id="hair_long"><Part name="Hair" kind="Skinned" mesh="m/hair_long.mesh"><Variant name="UnderHat" mesh="m/hair_flat.mesh"/></Part></ItemVisual>
  <ItemVisual id="beard_full"><Part name="Beard" kind="Skinned" mesh="m/beard.mesh"/></ItemVisual>
  <ItemVisual id="helm_full" tags="Helmet.FullFace">
    <Part name="Shell" kind="Skinned" mesh="m/helm.mesh" deforms="false"/>
    <Part name="Press" kind="BodyModification"><Morph name="HeadSquash" weight="0.4"/><HideRegion name="Scalp"/></Part>
  </ItemVisual>
  <ItemVisual id="cap" tags="Hat.Soft"><Part name="Cap" kind="Skinned" mesh="m/cap.mesh"/></ItemVisual>
  <ItemVisual id="plate" tags="Armor.Heavy,Sleeve.Long">
    <Part name="Plate" kind="Skinned" mesh="m/plate.mesh"><Variant name="CapeHole" mesh="m/plate_capehole.mesh"/><Variant name="Short" mesh="m/plate_short.mesh"/></Part>
  </ItemVisual>
  <ItemVisual id="shirt" tags="Sleeve.Long"><Part name="Shirt" kind="Skinned" mesh="m/shirt.mesh"><Variant name="Short" mesh="m/shirt_short.mesh"/></Part></ItemVisual>
  <ItemVisual id="robe" occupancy="Robe" tags="Cloth.Robe"><Part name="Robe" kind="Skinned" mesh="m/robe.mesh"/></ItemVisual>
  <ItemVisual id="pants"><Part name="Pants" kind="Skinned" mesh="m/pants.mesh"/></ItemVisual>
  <ItemVisual id="greaves"><Part name="Greaves" kind="Skinned" mesh="m/greaves.mesh"/></ItemVisual>
  <ItemVisual id="knight_full"><Part name="Suit" kind="Skinned" mesh="m/knight_full.mesh"/></ItemVisual>
  <ItemVisual id="cape" tags="Back.Cape"><Part name="Cape" kind="Skinned" mesh="m/cape.mesh"/></ItemVisual>
  <ItemVisual id="sword" customization="Sword" defaultState="Drawn" tags="Weapon.Blade">
    <Part name="Blade" kind="SocketPrefab" prefab="p/sword.prefab.xml" sockets="s/sword.sockets.xml" socket="hand.r">
      <Variant name="Chipped" prefab="p/sword_chipped.prefab.xml"/>
      <Variant name="Notched" prefab="p/sword_notched.prefab.xml"/>
      <MaterialVariant name="Gold" material="mat/gold.material"/>
    </Part>
    <Part name="Tassel" kind="SocketPrefab" prefab="p/tassel.prefab.xml" socket="MainHand.Pommel" breakable="true" impulse="0 1 0"/>
    <Part name="Pommel" kind="SocketPrefab" prefab="p/pommel.prefab.xml" socket="hand.r" breakStage="Broken" impulse="1 0 0"/>
    <State name="Drawn"><Place part="Blade" socket="hand.r"/></State>
    <State name="Sheathed"><Place part="Blade" socket="Belt.Hook,hip.l" offset="0 -0.1 0"/><HidePart part="Tassel"/></State>
    <DamageStage name="Worn" threshold="0.3"><Variant part="Blade" name="Notched"/><Material part="Blade" name="Rust" value="0.4"/></DamageStage>
    <DamageStage name="Broken" threshold="0.8"><Material part="Blade" name="Rust" value="1"/></DamageStage>
  </ItemVisual>
  <ItemVisual id="sword_gold_skin" tags="Weapon.Blade"><Part name="Blade" kind="SocketPrefab" prefab="p/sword_gold.prefab.xml" socket="hand.r"/></ItemVisual>
  <ItemVisual id="greatsword" occupancy="TwoHanded"><Part name="Blade" kind="SocketPrefab" prefab="p/greatsword.prefab.xml" socket="hand.r"/></ItemVisual>
  <ItemVisual id="shield"><Part name="Shield" kind="SocketPrefab" prefab="p/shield.prefab.xml" socket="hand.l"/></ItemVisual>
</ItemVisualCatalog>
)";

    inline constexpr const utf8* kRuleXml = R"(
<AppearanceRuleTable>
  <Rule id="HelmHidesCape" priority="0"><When target="Head" tag="Helmet.FullFace"/><Hide target="Back"/></Rule>
  <Rule id="FullHelmHidesHair" priority="10"><When target="Head" tag="Helmet.FullFace"/><Hide target="Hair"/><Hide target="Beard"/></Rule>
  <Rule id="HatFlattensHair" priority="5"><When target="Head" tag="Hat"/><Variant target="Hair" name="UnderHat"/></Rule>
  <Rule id="CapeCutsPlate" priority="1"><When occupied="Back"/><Variant target="Body" name="CapeHole"/></Rule>
  <Rule id="DwarfShortSleeves" priority="2"><When characterTag="Race.Dwarf"/><Variant target="Body" name="Short"/></Rule>
  <Rule id="HeavyLoosensArmor" priority="0"><When bodyShape="Heavy"/><Morph target="Body" name="Loosen" weight="0.5"/></Rule>
  <Rule id="FemalePlateMaterial" priority="0"><When bodyType="Female"/><When target="Body" tag="Cloth.Robe" not="true"/><SwapMaterial target="Body" material="mat/fem.material"/></Rule>
  <Rule id="FemaleRobeMaterial" priority="0"><When bodyType="Female"/><When target="Body" tag="Cloth.Robe"/><SwapMaterial target="Body" material="mat/fem_robe.material"/></Rule>
  <Rule id="CapeMovesHook" priority="0"><When occupied="Back"/><OverrideSocket name="Belt.Hook" parent="hip.r" offset="0 0 0.1"/></Rule>
  <Rule id="FullHelmSkullMesh" priority="3"><When target="Head" tag="Helmet.FullFace"/><SwapMesh target="Head" part="Shell" mesh="m/helm_skull.mesh"/></Rule>
</AppearanceRuleTable>
)";

    inline constexpr const utf8* kPresetXml = R"(
<CharacterAppearanceCatalog>
  <CharacterAppearance id="Human" schema="Human" bodyType="Male" bodyShape="Average" face="face_a" body="body_human" tags="Race.Human"/>
  <CharacterAppearance id="Knight" parent="Human" tags="Class.Knight">
    <Value name="Height" value="1.2"/>
    <Equip set="Knight"/>
    <Equip slot="MainHand" item="sword"><Value name="Gem" option="Ruby"/></Equip>
  </CharacterAppearance>
  <CharacterAppearance id="Villager" parent="Human" bodyShape="Average,Heavy,Thin" face="face_a,face_b,face_c">
    <Value name="Height" min="0.8" max="1.4"/>
    <Value name="Hair" options="Long,Bald"/>
    <Value name="Skin" colors="1 0.8 0.6 1; 0.6 0.4 0.3 1"/>
    <Equip slot="Body" items="shirt,robe"/>
    <Equip slot="Legs" item="pants"/>
  </CharacterAppearance>
  <CharacterAppearance id="Squire" parent="Knight">
    <Value name="Height" value="0.9"/>
    <Equip slot="Head" item=""/>
    <Equip slot="Body" item="shirt"/>
  </CharacterAppearance>
</CharacterAppearanceCatalog>
)";

    /** @brief 아이템 카탈로그와 외형 데이터 한 벌입니다. 데이터는 아이템을 빌려 쓰므로 함께 산다. */
    struct Fixture
    {
        sw::ItemCatalog        _items;
        sw::AppearanceDatabase _database;

        /** @brief 위 데이터를 읽습니다. @p pRuleXml 로 규칙 표를 바꿀 수 있습니다. */
        bool load( const utf8* pRuleXml = kRuleXml )
        {
            if ( _items.loadFromXmlText( kItemXml, "AppearanceTest.items" ) == false )
                return false;
            const bool bSections = _database.loadSectionFromXmlText( kSlotXml, "AppearanceTest.slots" ) && _database.loadSectionFromXmlText( kSetXml, "AppearanceTest.sets" ) && _database.loadSectionFromXmlText( kSchemaXml, "AppearanceTest.schemas" ) && _database.loadSectionFromXmlText( kVisualXml, "AppearanceTest.visuals" ) && _database.loadSectionFromXmlText( pRuleXml, "AppearanceTest.rules" ) && _database.loadSectionFromXmlText( kPresetXml, "AppearanceTest.presets" );
            return bSections && _database.finishLoad( &_items );
        }

        /** @brief 프리셋을 펼칩니다. */
        bool expand( const sw::hashed_string& presetId, uint32 seed, sw::CharacterAppearanceSpec& outSpec ) const
        {
            return _database.getPresets().expand( presetId, seed, _database.getSlotTable(), _database.getSets(), _database.getSchemas(), outSpec );
        }

        /** @brief 슬롯 표 그대로의 장비를 만듭니다(세트 조건은 이 데이터의 세트로 본다). */
        void makeEquipment( sw::Equipment& outEquipment ) const
        {
            outEquipment.initialize( &_items, _database.getSlotTable().makeEquipmentLayout() );
            outEquipment.setSetLookup( &_database.getSets() );
        }

        /** @brief 칸에 아이템 하나를 둡니다(인스턴스 상태 없이). */
        static void setSlot( sw::CharacterAppearanceSpec& inoutSpec, const sw::hashed_string& slot, const sw::hashed_string& itemId )
        {
            sw::AppearanceSlotRequest* pRequest = inoutSpec.findSlot( slot );
            if ( pRequest == nullptr )
                return;
            *pRequest         = sw::AppearanceSlotRequest{};
            pRequest->_slot   = slot;
            pRequest->_itemId = itemId;
        }
    };
} // namespace appearancetest
