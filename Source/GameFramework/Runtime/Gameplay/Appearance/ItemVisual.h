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
#include "Core/Name/hashed_string.h"

#include "Engine/Object/Component/TagSystem.h"
#include "Engine/Reflection/ReflectionMacros.h"

#include "GameFramework/GameFrameworkExports.h"
#include "GameFramework/Runtime/Gameplay/Appearance/AppearanceTypes.h"

namespace sw
{
    class XMLNode;

    /** @brief 부품 종류입니다. 해석기는 차원을 구분하지 않으므로 메시 부품과 스프라이트 부품이 같은 경로를 탑니다. */
    ENUM()
    enum class AppearancePartKind : uint8
    {
        Skinned = 0,      ///< 캐릭터 스켈레톤을 따르는 메시(피팅과 병합을 거친다)
        SocketPrefab,     ///< 소켓에 붙이는 프리팹(견갑, 칼집, 총)
        BodyModification, ///< 몸 수정. 모프, 머티리얼 값, 숨김 영역을 바꾸며 직접 그려지는 것은 없다
        Sprite            ///< 2D 종이 인형 조각. `sprite` 는 스프라이트 클립(`.sprite.xml`)이고, 소켓에 붙어 `layer` 순서로 그린다(유니티 2D 종이 인형의 Sprite Renderer 묶음 자리)
    };

    /**
     * @brief 메시 구간 하나의 머티리얼입니다(`<Section name material>`). `_name` 은 메시의 구간 이름이다 — 임포터가 머티리얼마다 나눈 구간(`ModelImportMaterial::readSectionPBRMaterials`).
     * @details 언리얼 Material Slot(슬롯 이름으로 덮기) · 유니티 `Renderer.materials`(서브메시 번호)의 자리다. 번호가 아니라 이름으로 짚어 메시를 다시 임포트해 구간 순서가 바뀌어도 데이터가 그대로 맞다.
     */
    REFLECT()
    struct AppearanceSectionDef
    {
        REFLECT_BODY();

        PROPERTY( Required )
        hashed_string _name{};
        PROPERTY( AssetPath, Required )
        hashed_string _material{};
        /** @brief 합친 메시의 LOD 에서 이 구간을 늦게(> 1) 또는 빨리(< 1) 줄입니다(`CharacterLODSectionRule::_detail`). 적지 않으면 부품의 값입니다. */
        PROPERTY( Min = 0.05 )
        float32 _lodDetail{ 1.0f };
        /** @brief 이 LOD 단계(1 부터)부터 이 구간을 그리지 않습니다(`CharacterLODSectionRule::_dropLevel`). 0 이면 부품의 값입니다. */
        PROPERTY()
        uint32 _lodDropLevel{ 0 };
    };
} // namespace sw

namespace sw
{
    /** @brief 머티리얼 값 하나입니다. 스칼라이면 x 만 씁니다. */
    REFLECT()
    struct AppearanceMaterialValueDef
    {
        REFLECT_BODY();

        float4 _value{};
        PROPERTY()
        hashed_string _part{}; ///< 피해 단계의 대상 부품(비어 있으면 아이템의 모든 부품)
        PROPERTY()
        hashed_string _section{}; ///< 대상 메시 구간 이름(비어 있으면 부품의 모든 구간). 구간마다 셰이더 효과를 달리할 때 쓴다
        PROPERTY( Required )
        hashed_string _name{};
    };
} // namespace sw

namespace sw
{
    /** @brief 이름 붙은 변형입니다. 메시(또는 스프라이트) · 머티리얼 · 스켈레톤 · 구간 머티리얼 · 머티리얼 값을 바꿉니다. 비워 둔 항목은 기본값을 씁니다. */
    REFLECT()
    struct AppearancePartVariantDef
    {
        REFLECT_BODY();

        vector<AppearanceSectionDef>       _listSection{};       ///< 이 변형을 고르면 이름이 같은 구간의 머티리얼을 바꾼다(`<Section name material>` — 부품이 적은 구간만)
        vector<AppearanceMaterialValueDef> _listMaterialValue{}; ///< 이 변형을 고르면 이 부품에 거는 머티리얼 값(`<Material name value section>`)
        PROPERTY( Required )
        hashed_string _name{};
        /** @brief 부품 종류가 고르는 에셋 칸입니다 — `Skinned` 는 `mesh`, `SocketPrefab` 은 `prefab`, `Sprite` 는 `sprite` 하나만 적는다. 읽기가 그중 하나를 `_asset` 으로 고른다. */
        PROPERTY( AssetPath, Name = "mesh" )
        hashed_string _meshAsset{};
        PROPERTY( AssetPath, Name = "prefab" )
        hashed_string _prefabAsset{};
        PROPERTY( AssetPath, Name = "sprite" )
        hashed_string _spriteAsset{};
        hashed_string _asset{}; ///< 종류가 고른 에셋(읽기가 위 칸에서 채운다 — 데이터 칸이 아니다)
        PROPERTY( AssetPath )
        hashed_string _material{}; ///< 구간이 있는 부품이면 모든 구간의 머티리얼을 바꾼다
        /** @brief 스킨드 부품(`<Variant>`)이 이 변형에서 쓰는 스켈레톤입니다 — 스켈레톤 변형(`SkeletonVariant`)을 적으면 외형이 런타임에 뼈 비율을 고릅니다(꾸미기 선택 · 규칙 · 피해 단계가 고르는 변형과 같은 길). */
        PROPERTY( AssetPath )
        hashed_string _skeleton{};
    };
} // namespace sw

namespace sw
{
    /** @brief 모프 가중치 하나입니다. */
    REFLECT()
    struct AppearanceMorphDef
    {
        REFLECT_BODY();

        PROPERTY( Required )
        hashed_string _name{};
        PROPERTY()
        float32 _weight{ 1.0f }; ///< `weight` 를 적지 않으면 모프를 다 건다
    };
} // namespace sw

namespace sw
{
    /** @brief 부품 하나입니다. */
    REFLECT()
    struct SW_GF_API AppearancePartDef
    {
        REFLECT_BODY();

        vector<AppearancePartVariantDef> _listVariant{};         ///< 메시 변형(기본, 짧은 소매, 모자 밑 머리, 망토 구멍)
        vector<AppearancePartVariantDef> _listMaterialVariant{}; ///< 머티리얼 변형(위장, 사막)
        /**
         * @brief 스킨드 부품의 메시 구간마다 머티리얼입니다(`<Section name material>` — `_name` 은 구간 이름). 적으면 이 구간들만 그린다.
         * @details 머티리얼이 여럿인 모델(몸 · 머리카락 · 눈)을 임포터가 구간으로 나눈 메시다(`ModelImportMaterial::readSectionPBRMaterials`). 주 메시가 첫 구간을,
         *          `MeshSectionComponent` 가 나머지를 그린다(임포터의 구간 프리팹과 같은 모양). 부품의 `material` 과 함께 쓸 수 없다(어느 구간의 것인지 모른다).
         *          변형(`<Variant>` · `<MaterialVariant>` 의 `<Section>`)과 규칙(`<SwapMaterial section>`)이 구간 이름으로 머티리얼을 바꾸고, 머티리얼 값(`section`)이 구간 하나에만 걸린다.
         */
        vector<AppearanceSectionDef>       _listSection{};
        vector<AppearanceMorphDef>         _listMorph{};         ///< 몸 수정: 몸에 적용하는 모프
        vector<AppearanceMaterialValueDef> _listMaterialValue{}; ///< 몸 수정: 몸 머티리얼 값
        vector<hashed_string>              _listHiddenRegion{};  ///< 몸 수정: 숨길 몸 영역
        AppearancePlacement                _placement{};         ///< 소켓 부착과 스프라이트의 기본 배치
        float3                             _breakImpulse{};      ///< 떨어져 나갈 때의 충격 힌트(부품 로컬)
        PROPERTY( Required )
        hashed_string _name{};
        /** @brief 부품 종류가 고르는 에셋 칸입니다 — `Skinned` 는 `mesh`, `SocketPrefab` 은 `prefab`, `Sprite` 는 `sprite` 하나만 적는다. 읽기가 그중 하나를 `_asset` 으로 고른다. */
        PROPERTY( AssetPath, Name = "mesh" )
        hashed_string _meshAsset{};
        PROPERTY( AssetPath, Name = "prefab" )
        hashed_string _prefabAsset{};
        PROPERTY( AssetPath, Name = "sprite" )
        hashed_string _spriteAsset{};
        hashed_string _asset{}; ///< 메시, 프리팹, 스프라이트 경로 — 종류가 위 칸에서 고른다(읽기가 채운다 — 데이터 칸이 아니다)
        PROPERTY( AssetPath )
        hashed_string _material{}; ///< 비어 있으면 메시의 것
        PROPERTY( AssetPath )
        hashed_string _skeleton{}; ///< 자기 스켈레톤. 비어 있으면 루트 본 하나짜리 암묵 스켈레톤
        PROPERTY( AssetPath, Name = "sockets" )
        hashed_string _socketSet{}; ///< 부품의 소켓 에셋(`*.sockets.xml`)
        PROPERTY( AssetPath )
        hashed_string _rig{}; ///< 스킨드 부품의 자기 후처리 리그(`*.rig.xml`). 있으면 몸을 리더로 따르지 않고 자기 스켈레톤을 리그가 움직인다(스프링 망토)
        PROPERTY( AssetPath )
        hashed_string _fit{}; ///< 스킨드 부품의 피팅 데이터(`*.partfit.xml` — 겹 · 프로필 · 숨김 영역). 있으면 몸과 함께 피팅해 베이크한 메시를 그린다
        PROPERTY( AssetPath )
        hashed_string _fitTables{}; ///< 몸 부품의 피팅 표(`*.fit.xml` — 겹 · 영역 · 프로필 · 상호작용). 몸에만 적는다
        PROPERTY( AssetPath )
        hashed_string _bodyShapes{}; ///< 몸 부품의 체형 축 데이터(`*.bodyshape.xml`). 꾸미기의 `BoneProportion` 대상 이름이 이 축 이름이다. 몸에만 적는다
        PROPERTY()
        hashed_string _breakStage{}; ///< 이 피해 단계에 이르면 저절로 떨어진다. 비어 있으면 맞아서만 떨어진다
        PROPERTY()
        int32 _layer{ 0 }; ///< 스프라이트 부품의 레이어 안 그리기 순서(클수록 위). 스프라이트만 적는다
        PROPERTY( Min = 0.05 )
        float32 _lodDetail{ 1.0f }; ///< 병합하는 스킨드 부품: 합친 메시 LOD 에서 늦게(> 1, 얼굴) 또는 빨리(< 1) 줄인다(`CharacterLODSectionRule::_detail`)
        PROPERTY()
        uint32 _lodDropLevel{ 0 }; ///< 병합하는 스킨드 부품: 이 LOD 단계(1 부터)부터 그리지 않는다(눈썹 · 장식). 0 이면 빼지 않는다
        PROPERTY( Required )
        AppearancePartKind _kind{ AppearancePartKind::Skinned };
        uint8              _bDeforms{ SW_TRUE };    ///< 스킨드로 휘는지 여부. 스킨드만 휘며, `deforms="false"` 인 스킨드는 소켓(본 이름 포함) 하나를 따르는 단단한 메시 조각이다(소켓 필수)
        uint8              _bBreakable{ SW_FALSE }; ///< 떨어져 나갈 수 있는지 여부(맞아서, 또는 피해 단계로)

        const AppearancePartVariantDef* findVariant( const hashed_string& name ) const;
        const AppearancePartVariantDef* findMaterialVariant( const hashed_string& name ) const;
        const AppearanceSectionDef*     findSection( const hashed_string& name ) const;
    };
} // namespace sw

namespace sw
{
    /** @brief 외형 상태 하나입니다(뽑음, 꽂음, 켬, 끔). 부품을 다른 소켓으로 옮기거나 숨깁니다. 같은 인스턴스를 옮기므로 다시 스폰하지 않습니다. */
    struct AppearanceStateDef
    {
        struct Placement
        {
            AppearancePlacement _placement{};
            hashed_string       _part{};
        };

        vector<Placement>     _listPlacement{};
        vector<hashed_string> _listHiddenPart{};
        hashed_string         _name{};
    };
} // namespace sw

namespace sw
{
    /** @brief 피해 단계 하나입니다(멀쩡, 상함, 부서짐). 피해가 임계값 이상이면 이 단계입니다. 부품마다 메시 변형과 머티리얼 값(먼지, 녹, 찢김)을 고릅니다. */
    REFLECT()
    struct AppearanceDamageStageDef
    {
        REFLECT_BODY();

        struct PartVariant
        {
            hashed_string _part{};
            hashed_string _variant{};
        };

        vector<PartVariant>                _listVariant{};
        vector<AppearanceMaterialValueDef> _listMaterialValue{};
        PROPERTY( Required )
        hashed_string _name{};
        PROPERTY( Required, Max = 1 )
        float32 _threshold{ 0.0f };
    };
} // namespace sw

namespace sw
{
    /** @brief 아이템 외형 하나입니다. */
    REFLECT()
    struct SW_GF_API ItemVisualDef
    {
        REFLECT_BODY();

        vector<AppearancePartDef>        _listPart{};
        vector<AppearanceStateDef>       _listState{};
        vector<AppearanceDamageStageDef> _listDamageStage{}; ///< 임계값 오름차순
        TagContainer                     _tags{};            ///< 외형 규칙이 참조하는 태그(`Helmet.FullFace`, `Sleeve.Long`). 형상 변경이면 보이는 외형의 태그를 쓴다
        hashed_string                    _id{};
        PROPERTY()
        hashed_string _occupancy{}; ///< `SlotCatalog` 의 슬롯 점유 그룹. 비어 있으면 자기 슬롯만 차지한다
        PROPERTY()
        hashed_string _customization{}; ///< 꾸미기 스키마 id. 비어 있으면 꾸미기 없음
        PROPERTY()
        hashed_string _defaultState{}; ///< 비어 있으면 첫 상태. 상태가 없으면 기본 배치

        const AppearancePartDef*        findPart( const hashed_string& name ) const;
        const AppearanceStateDef*       findState( const hashed_string& name ) const;
        const AppearanceDamageStageDef* findDamageStage( const hashed_string& name ) const;
        /** @brief 피해 @p damage 의 단계 번호입니다. 첫 임계값 아래이면 -1 이고, 멀쩡한 상태입니다. */
        int32 computeDamageStageIndex( float32 damage ) const;
        /** @brief 단계 이름의 번호입니다. 없으면 -1 입니다. */
        [[nodiscard]] optional<int32> findDamageStageIndex( const hashed_string& name ) const;
    };
} // namespace sw

namespace sw
{
    /**
     * @class ItemVisualCatalog
     * @brief `<ItemVisualCatalog><ItemVisual id="rifle_m4" occupancy="TwoHanded" customization="Rifle" tags="Weapon.Rifle" defaultState="Drawn">` 아래
     *        `<Part>`, `<State>`, `<DamageStage>` 를 읽습니다. 형식은 `Appearance/README.md` 에 있습니다.
     */
    class SW_GF_API ItemVisualCatalog
    {
    public:
        [[nodiscard]] bool loadFromXML( const XMLNode& root, AppearanceLoadReport& report, string_view sourceName );
        void               clear() { _listVisual.clear(); }

        const ItemVisualDef*         findVisual( const hashed_string& id ) const;
        const vector<ItemVisualDef>& getVisuals() const { return _listVisual; }

    private:
        vector<ItemVisualDef> _listVisual{};
    };
} // namespace sw
