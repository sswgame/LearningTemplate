/**
 * @file AppearanceDatabase.h
 * @brief 외형 데이터 한 벌입니다. 슬롯 테이블, 장비 세트, 아이템 외형, 꾸미기 스키마, 외형 규칙, 캐릭터 프리셋을 읽고 서로의 이름을 대조합니다.
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"
#include "Core/Container/string.h"
#include "Core/Container/vector.h"
#include "Core/Delegate/Delegate.h"
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
    class XMLNode;

    /**
     * @class AppearanceDatabase
     * @brief 외형 데이터 폴더 하나(`<게임>/data/appearance/`)를 읽습니다. 파일 하나가 한 종류이고, 루트 원소 이름으로 종류를 구분합니다.
     * @details 파일은 다음과 같다. `slots.xml`(`SlotTable`, 반드시 필요), `sets.xml`(`EquipSetCatalog`), `itemvisuals.xml`(`ItemVisualCatalog`),
     *          `customization.xml`(`CustomizationSchemaCatalog`), `rules.xml`(`AppearanceRuleTable`), `presets.xml`(`CharacterAppearanceCatalog`).
     *          프리셋은 에셋 파일 하나에 하나씩도 둘 수 있다. `presets/` 아래의 `*.appearance.xml`(루트 `<CharacterAppearance>`)을 `presets.xml` 뒤에 읽는다.
     *          캐릭터 편집 창이 내보내는 곳이고(언리얼과 유니티의 프리셋 에셋에 해당), 씬의 `CharacterAppearanceComponent` 는 그 id 를 가리킨다.
     *          모두 읽은 뒤 `finishLoad` 가 알 수 없는 이름(슬롯, 세트, 아이템, 외형, 스키마, 매개변수, 항목, 변형, 부품, 부모)과 장착 조건 순환, 프리셋 부모 순환을 오류로 모은다.
     *          파일을 고쳐 다시 읽으면 `getRevision` 이 늘어나고, 외형 상태(`CharacterAppearanceState`)가 외형을 다시 해석한다.
     */
    class SW_GF_API AppearanceDatabase
    {
    public:
        /** @brief 프리셋 에셋 폴더(외형 폴더 아래 이름)와 파일 끝입니다. 캐릭터 편집 창이 같은 위치와 이름으로 내보냅니다. */
        static constexpr const utf8* kPresetFolder    = "presets";
        static constexpr const utf8* kPresetExtension = ".appearance.xml";

        /** @brief 데이터 종류 이름입니다(`SW_RESOURCE_DATA_KIND` 등록 줄과 팩 경로 `GamePackDataUtil::tryMakeKindPath` 가 이것을 쓴다). */
        static constexpr const utf8* kDataKind = "appearance";

        AppearanceDatabase();
        /** @brief 복사하지 않는다 — `getInstanceID` 가 이 객체 하나를 가리켜야 한다(외형 상태가 데이터가 바뀌었는지 알아보는 표). */
        AppearanceDatabase( const AppearanceDatabase& )            = delete;
        AppearanceDatabase& operator=( const AppearanceDatabase& ) = delete;

        /** @brief 폴더의 데이터를 모두 읽고 검사합니다. 오류가 없으면 true 입니다. @p pItemCatalog 는 소유하지 않고 참조만 하므로 데이터보다 오래 살아 있어야 합니다. */
        [[nodiscard]] bool loadFromFolder( string_view folder, const ItemCatalog* pItemCatalog );
        /** @brief 데이터 한 덩어리를 읽습니다. 루트 원소로 종류를 정하며, 테스트와 미리보기에 씁니다. 검사는 `finishLoad` 에서 합니다. */
        [[nodiscard]] bool loadSectionFromXMLText( string_view xmlText, string_view sourceName );
        /**
         * @brief 프리셋 에셋 파일 하나(`*.appearance.xml`)를 읽어 더하거나 같은 id 를 바꾸고 프리셋을 다시 검사합니다. 새 오류가 없으면 true 입니다.
         * @details 폴더를 읽을 때 `presets/` 의 파일마다 부르고, 에디터가 내보낸 파일을 실행 중인 데이터에 올릴 때도 부른다(`getRevision` 이 오른다).
         */
        [[nodiscard]] bool loadPresetFile( string_view path );
        /**
         * @brief 읽은 데이터끼리 이름을 대조합니다. 오류가 없으면 true 입니다.
         * @param pListKnownBodyRegion 몸 영역 테이블(피팅 데이터)의 이름 목록입니다. 주면 숨김 영역 이름도 대조합니다.
         */
        [[nodiscard]] bool finishLoad( const ItemCatalog* pItemCatalog, const vector<hashed_string>* pListKnownBodyRegion = nullptr );
        void               clear();
        /**
         * @brief 실행 중인 데이터를 폴더에서 다시 읽고 구독자(`registerReloaded`)에게 알립니다(핫 리로드).
         * @details 먼저 임시 데이터로 읽어 검사를 통과할 때만 이 데이터를 바꾼다 — 고치는 중의 깨진 파일이 돌고 있는 외형을 지우지 않는다.
         *          통과하지 못하면 그 보고를 @p outReport 에 담고 false 이며 지금 데이터는 그대로다.
         */
        [[nodiscard]] bool reloadFromFolder( string_view folder, const ItemCatalog* pItemCatalog, string& outReport );
        /** @brief 다시 읽은 뒤(`reloadFromFolder` 성공) 부를 델리게이트를 겁니다. 외형 컴포넌트가 다시 조립합니다. 돌려준 구독이 사라지면 뗍니다. */
        [[nodiscard]] Subscription registerReloaded( const Delegate<void()>& delegate );

        const SlotTable&                  getSlotTable() const { return _slotTable; }
        const EquipSetCatalog&            getSets() const { return _sets; }
        const ItemVisualCatalog&          getVisuals() const { return _visuals; }
        const CustomizationSchemaCatalog& getSchemas() const { return _schemas; }
        const AppearanceRuleTable&        getRules() const { return _rules; }
        const CharacterAppearanceCatalog& getPresets() const { return _presets; }
        const ItemCatalog*                getItemCatalog() const { return _pItemCatalog; }
        const AppearanceLoadReport&       getReport() const { return _report; }
        uint32                            getRevision() const { return _revision; }
        /**
         * @brief 이 객체의 번호입니다(만들 때마다 새 번호 — 주소가 재사용돼도 겹치지 않는다).
         * @details 외형 상태(`CharacterAppearanceState`)는 데이터베이스를 포인터로 들지 않고 이 번호만 기억해 "같은 데이터인가" 를 알아본다. 서비스가 먼저 내려가도 허공을 가리키는 포인터가 남지 않는다.
         */
        uint64 getInstanceID() const { return _instanceID; }

        /** @brief 아이템의 외형입니다. 형상 변경이 없을 때의 외형이며, 없으면 nullptr 입니다. */
        const ItemVisualDef* findItemVisual( const hashed_string& itemID ) const;
        /** @brief 이름이 외형 주인(슬롯 또는 어떤 스키마의 매개변수)이면 true 입니다. */
        bool isOwnerName( const hashed_string& name ) const;

    private:
        [[nodiscard]] bool loadSectionFromNode( const XMLNode& root, string_view sourceName );
        /** @brief 프리셋 에셋 파일 하나를 읽어 카탈로그에 더합니다(검사 없음). 읽지 못하면 false 이고 오류는 `_report` 에 있습니다. */
        [[nodiscard]] bool readPresetFile( string_view path );
        /**
         * @brief 몸 부품이 가리키는 피팅 표(`fitTables`)들의 영역 이름을 모읍니다. 읽지 못한 표는 오류로 남깁니다.
         * @return 피팅 표를 가리키는 부품이 하나라도 있으면 true 입니다(그때만 숨김 영역 이름을 대조한다).
         */
        [[nodiscard]] bool collectBodyRegions( vector<hashed_string>& outListRegion );
        void               validateItems();
        void               validateSets();
        void               validateVisuals( const vector<hashed_string>* pListKnownBodyRegion );
        void               validateSchemas();
        void               validateRules( const vector<hashed_string>* pListKnownBodyRegion );
        void               validatePresets();
        void               validateItemValues( const CustomizationValueSet& values, const hashed_string& schemaID, const utf8* pWhere );
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
        MulticastDelegate<void()>  _reloadedMulticast;
        uint32                     _revision;
        uint64                     _instanceID;
    };
} // namespace sw
