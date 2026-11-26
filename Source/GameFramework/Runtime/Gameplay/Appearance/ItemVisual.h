/**
 * @file ItemVisual.h
 * @brief 아이템 외형입니다. 게임플레이 정의(`ItemDef`)와 따로 둔 부품 목록, 상태별 소켓 배치, 피해 단계를 담습니다. `ItemDef` 는 이 외형의 id 만 가리킵니다.
 */
#pragma once
#include "Core/Container/vector.h"
#include "Core/Foundation/Expected.h"
#include "Core/Foundation/Macros.h"
#include "Core/Foundation/Types.h"
#include "Core/Math/Math.h"
#include "Core/String/Name/hashed_string.h"

#include "Engine/Asset/AssetRef.h"
#include "Engine/Asset/PrefabAssetKind.h"
#include "Engine/Graphics/Material/Material.h"
#include "Engine/Graphics/Mesh/MeshAssetFormat.h"
#include "Engine/Object/Component/TagSystem.h"
#include "Engine/Reflection/ReflectionMacros.h"

#include "GameFramework/GameFrameworkExports.h"
#include "GameFramework/Runtime/Gameplay/Appearance/AppearanceLoadReport.h"
#include "GameFramework/Runtime/Gameplay/Appearance/AppearancePart.h"
#include "GameFramework/Runtime/Gameplay/Appearance/BodyChannel.h"

namespace sw
{
    class BodyShapeSet;
    class FitPartDef;
    class FitTables;
    class RigAsset;
    class Skeleton;
    class SocketSet;
    class SpriteClipAsset;
    class XMLNode;

    /**
     * @brief 몸마다 다른 외형입니다(`<ForBody bodies="body_ubc_male body_ubc_female" visual="cape_heraldic_ubc"/>`) — 이 몸 외형이면 이 외형으로 그린다.
     * @details 골격이 다른 몸(KayKit · UBC)은 같은 아이템도 다른 메시 · 리그를 입어야 한다. 몸 종류(Male · Female)는 골격을 가르지 않는다.
     *          WoW 의 종족 · 성별별 아이템 모델, 언리얼의 골격별 메시 변형과 같은 자리다.
     */
    struct ItemVisualBodyVariant
    {
        vector<hashed_string> _listBody{}; ///< 몸 외형 id(`CharacterAppearance` 의 `body`)
        hashed_string         _visual{};   ///< 그 몸이 입을 외형(자기 `<ForBody>` 가 없어야 한다 — 한 단계만)
    };

    /** @brief 아이템 외형 하나입니다. */
    REFLECT()
    struct SW_GF_API ItemVisualDef
    {
        REFLECT_BODY();

        vector<AppearancePartDef>        _listPart{};
        vector<AppearanceStateDef>       _listState{};
        vector<AppearanceDamageStageDef> _listDamageStage{}; ///< 임계값 오름차순
        vector<BodyChannelLayerDef>      _listBodyChannel{}; ///< 입고 있는 동안 몸 채널에 얹는 층(`<BodyChannel>` — 가슴 조임 · 어깨 폭 · 굽 높이). 뼈 이름은 모른다
        vector<ItemVisualBodyVariant>    _listBodyVariant{}; ///< 몸마다 다른 외형(`<ForBody>`)
        TagContainer                     _tags{};            ///< 외형 규칙이 참조하는 태그(`Helmet.FullFace`, `Sleeve.Long`). 형상 변경이면 보이는 외형의 태그를 쓴다
        hashed_string                    _id{};
        PROPERTY()
        hashed_string _occupancy{}; ///< `SlotCatalog` 의 슬롯 점유 그룹. 비어 있으면 자기 슬롯만 차지한다
        PROPERTY()
        hashed_string _customization{}; ///< 꾸미기 스키마 id. 비어 있으면 꾸미기 없음
        PROPERTY()
        hashed_string _defaultState{}; ///< 비어 있으면 첫 상태. 상태가 없으면 기본 배치

        const AppearancePartDef* findPart( const hashed_string& name ) const;
        /** @brief 어느 부품의 구간이 이름 @p tag(날 이름 · 별칭 · 묶음)와 맞으면 true 입니다. 드라이브의 `include` · `exclude` 검사가 쓴다. */
        bool                            hasSectionTag( const hashed_string& tag ) const;
        const AppearanceStateDef*       findState( const hashed_string& name ) const;
        const AppearanceDamageStageDef* findDamageStage( const hashed_string& name ) const;
        /** @brief 피해 @p damage 의 단계 번호입니다. 첫 임계값 아래이면 -1 이고, 멀쩡한 상태입니다. */
        int32 computeDamageStageIndex( float32 damage ) const;
        /** @brief 단계 이름의 번호입니다. 없으면 -1 입니다. */
        [[nodiscard]] optional<int32> findDamageStageIndex( const hashed_string& name ) const;
    };

    /**
     * @class ItemVisualCatalog
     * @brief `<ItemVisualCatalog><ItemVisual id="rifle_m4" occupancy="TwoHanded" customization="Rifle" tags="Weapon.Rifle" defaultState="Drawn">` 아래
     *        `<Part>`, `<State>`, `<DamageStage>` 를 읽습니다.
     */
    class SW_GF_API ItemVisualCatalog
    {
    public:
        [[nodiscard]] bool loadFromXML( const XMLNode& root, AppearanceLoadReport& report, string_view sourceName );
        void               clear() { _listVisual.clear(); }

        const ItemVisualDef* findVisual( const hashed_string& id ) const;
        /** @brief @p id 를 몸 외형 @p bodyVisual 이 입는 외형입니다 — 그 몸의 `<ForBody>` 가 있으면 그 외형, 없으면 @p id 자신. 없는 id 면 nullptr 입니다. */
        const ItemVisualDef*         findVisualForBody( const hashed_string& id, const hashed_string& bodyVisual ) const;
        const vector<ItemVisualDef>& getVisuals() const { return _listVisual; }

    private:
        vector<ItemVisualDef> _listVisual{};
    };
} // namespace sw
