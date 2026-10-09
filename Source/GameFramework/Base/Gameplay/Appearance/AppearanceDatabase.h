/**
 * @file AppearanceDatabase.h
 * @brief 외형 데이터 한 벌 — 슬롯 표 · 장비 세트 · 아이템 외형 · 꾸미기 스키마 · 외형 규칙 · 캐릭터 프리셋을 읽고 서로의 이름을 대조합니다.
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"
#include "Core/Container/vector.h"
#include "Core/String/hashed_string.h"

#include "GameFramework/Base/Gameplay/Appearance/AppearanceRule.h"
#include "GameFramework/Base/Gameplay/Appearance/AppearanceTypes.h"
#include "GameFramework/Base/Gameplay/Appearance/CharacterAppearance.h"
#include "GameFramework/Base/Gameplay/Appearance/CustomizationSchema.h"
#include "GameFramework/Base/Gameplay/Appearance/EquipSetCatalog.h"
#include "GameFramework/Base/Gameplay/Appearance/ItemVisual.h"
#include "GameFramework/Base/Gameplay/Appearance/SlotTable.h"
#include "GameFramework/GameFrameworkExports.h"

namespace sw
{
    class ItemCatalog;
    class XmlNode;

    /**
     * @class AppearanceDatabase
     * @brief 외형 데이터 폴더 하나(`<게임>/data/appearance/`)를 읽습니다. 파일 하나가 한 종류이고 루트 원소 이름으로 종류를 압니다.
     * @details 파일: `slots.xml`(`SlotTable`, 반드시) · `sets.xml`(`EquipSetCatalog`) · `itemvisuals.xml`(`ItemVisualCatalog`) · `customization.xml`
     *          (`CustomizationSchemaCatalog`) · `rules.xml`(`AppearanceRuleTable`) · `presets.xml`(`CharacterAppearanceCatalog`). 다 읽은 뒤 `finishLoad` 가
     *          모르는 이름(칸 · 세트 · 아이템 · 외형 · 스키마 · 매개변수 · 항목 · 변형 · 부품 · 부모)과 장착 조건 순환, 프리셋 부모 순환을 오류로 모읍니다.
     *          파일을 고쳐 다시 읽으면 `getRevision` 이 올라 외형 상태(`CharacterAppearanceState`)가 다시 해석합니다.
     */
    class SW_GF_API AppearanceDatabase
    {
    public:
        AppearanceDatabase();

        /** @brief 폴더의 데이터를 모두 읽고 검사합니다. 오류가 없으면 true 입니다. @p pItemCatalog 는 빌려 씁니다(데이터보다 오래 살아야 한다). */
        [[nodiscard]] bool loadFromFolder( string_view folder, const ItemCatalog* pItemCatalog );
        /** @brief 데이터 한 덩이(루트 원소로 종류를 고른다)를 읽습니다(시험 · 미리보기). 검사는 `finishLoad` 에서 합니다. */
        [[nodiscard]] bool loadSectionFromXmlText( string_view xmlText, string_view sourceName );
        /**
         * @brief 읽은 데이터끼리 이름을 대조합니다. 오류가 없으면 true 입니다.
         * @param pListKnownBodyRegion 몸 영역 표(피팅 데이터)의 이름 — 주면 숨김 영역 이름도 대조합니다.
         */
        [[nodiscard]] bool finishLoad( const ItemCatalog* pItemCatalog, const vector<hashed_string>* pListKnownBodyRegion = nullptr );
        void               clear();

        const SlotTable&                  getSlotTable() const { return _slotTable; }
        const EquipSetCatalog&            getSets() const { return _sets; }
        const ItemVisualCatalog&          getVisuals() const { return _visuals; }
        const CustomizationSchemaCatalog& getSchemas() const { return _schemas; }
        const AppearanceRuleTable&        getRules() const { return _rules; }
        const CharacterAppearanceCatalog& getPresets() const { return _presets; }
        const ItemCatalog*                getItemCatalog() const { return _pItemCatalog; }
        const AppearanceLoadReport&       getReport() const { return _report; }
        uint32                            getRevision() const { return _revision; }

        /** @brief 아이템의 외형입니다(형상 변경이 없을 때). 없으면 nullptr 입니다. */
        const ItemVisualDef* findItemVisual( const hashed_string& itemId ) const;
        /** @brief 이름이 외형 주인(칸 또는 어떤 스키마의 매개변수)인가입니다. */
        bool isOwnerName( const hashed_string& name ) const;

    private:
        [[nodiscard]] bool loadSectionFromNode( const XmlNode& root, string_view sourceName );
        void               validateItems();
        void               validateSets();
        void               validateVisuals( const vector<hashed_string>* pListKnownBodyRegion );
        void               validateSchemas();
        void               validateRules( const vector<hashed_string>* pListKnownBodyRegion );
        void               validatePresets();
        void               validateItemValues( const CustomizationValueSet& values, const hashed_string& schemaId, const utf8* pWhere );
        bool               isVariantDeclared( const hashed_string& variant ) const;
        bool               isPartDeclared( const hashed_string& part ) const;

        SlotTable                  _slotTable;
        EquipSetCatalog            _sets;
        ItemVisualCatalog          _visuals;
        CustomizationSchemaCatalog _schemas;
        AppearanceRuleTable        _rules;
        CharacterAppearanceCatalog _presets;
        AppearanceLoadReport       _report;
        const ItemCatalog*         _pItemCatalog;
        uint32                     _revision;
    };
} // namespace sw
