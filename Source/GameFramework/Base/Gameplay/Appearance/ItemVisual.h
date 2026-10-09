/**
 * @file ItemVisual.h
 * @brief 아이템 외형 — 게임플레이 정의(`ItemDef`)와 따로 둔 부품 목록 · 상태별 소켓 배치 · 피해 단계입니다. `ItemDef` 는 그 id 만 가리킵니다.
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"
#include "Core/Container/vector.h"
#include "Core/Math/Math.h"
#include "Core/String/hashed_string.h"

#include "Engine/Object/Component/TagSystem.h"

#include "GameFramework/Base/Gameplay/Appearance/AppearanceTypes.h"
#include "GameFramework/GameFrameworkExports.h"

namespace sw
{
    class XmlNode;

    /** @brief 부품 종류입니다. 해석기는 차원을 모릅니다 — 메시 부품과 스프라이트 부품이 같은 길을 탑니다. */
    enum class AppearancePartKind : uint8
    {
        Skinned = 0,      ///< 캐릭터 스켈레톤을 따르는 메시(피팅 · 병합으로 간다)
        SocketPrefab,     ///< 소켓에 붙이는 프리팹(견갑 · 칼집 · 총)
        BodyModification, ///< 몸 수정 — 모프 · 머티리얼 값 · 숨김 영역(그려지는 것이 없다)
        Sprite            ///< 2D 종이 인형 조각(스프라이트 + 그리기 순서 + 소켓 기준점)
    };

    SW_GF_API const utf8* toString( AppearancePartKind kind );

    /** @brief 이름 붙은 변형 — 메시(또는 스프라이트) · 머티리얼을 바꿉니다. 비운 칸은 기본 것을 둡니다. */
    struct AppearancePartVariantDef
    {
        hashed_string _name{};
        hashed_string _asset{};
        hashed_string _material{};
    };
} // namespace sw

namespace sw
{
    /** @brief 모프 가중치 하나입니다. */
    struct AppearanceMorphDef
    {
        hashed_string _name{};
        float32       _weight{ 0.0f };
    };
} // namespace sw

namespace sw
{
    /** @brief 머티리얼 값 하나입니다(스칼라는 x). */
    struct AppearanceMaterialValueDef
    {
        float4        _value{};
        hashed_string _part{}; ///< 피해 단계에서 대상 부품(비면 아이템의 모든 부품)
        hashed_string _name{};
    };
} // namespace sw

namespace sw
{
    /** @brief 부품 하나입니다. */
    struct SW_GF_API AppearancePartDef
    {
        vector<AppearancePartVariantDef>   _listVariant{};         ///< 메시 변형(기본 · 짧은 소매 · 모자 밑 머리 · 망토 구멍)
        vector<AppearancePartVariantDef>   _listMaterialVariant{}; ///< 머티리얼 변형(위장 · 사막)
        vector<AppearanceMorphDef>         _listMorph{};           ///< 몸 수정: 몸에 거는 모프
        vector<AppearanceMaterialValueDef> _listMaterialValue{};   ///< 몸 수정: 몸 머티리얼 값
        vector<hashed_string>              _listHiddenRegion{};    ///< 몸 수정: 숨길 몸 영역
        AppearancePlacement                _placement{};           ///< 소켓 부착 · 스프라이트의 기본 자리
        float3                             _breakImpulse{};        ///< 떨어져 나갈 때 충격 힌트(부품 로컬)
        hashed_string                      _name{};
        hashed_string                      _asset{};      ///< 메시 · 프리팹 · 스프라이트 경로
        hashed_string                      _material{};   ///< 비면 메시의 것
        hashed_string                      _skeleton{};   ///< 자기 스켈레톤 — 비면 뿌리 본 하나의 암묵 스켈레톤
        hashed_string                      _socketSet{};  ///< 부품의 소켓 에셋(`*.sockets.xml`)
        hashed_string                      _breakStage{}; ///< 이 피해 단계에 이르면 저절로 떨어진다(비면 맞아서만)
        int32                              _layer{ 0 };   ///< 스프라이트 그리기 순서
        AppearancePartKind                 _kind{ AppearancePartKind::Skinned };
        uint8                              _bDeforms{ SW_TRUE };    ///< 스킨드로 휘는가(아니면 본 하나를 따르는 단단한 조각)
        uint8                              _bBreakable{ SW_FALSE }; ///< 떨어져 나갈 수 있는가(맞아서 · 피해 단계로)

        const AppearancePartVariantDef* findVariant( const hashed_string& name ) const;
        const AppearancePartVariantDef* findMaterialVariant( const hashed_string& name ) const;
    };
} // namespace sw

namespace sw
{
    /** @brief 외형 상태 하나(뽑음 · 꽂음 · 켬 · 끔) — 부품을 다른 소켓으로 옮기거나 숨깁니다. 같은 인스턴스를 옮기므로 다시 스폰하지 않습니다. */
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
    /** @brief 피해 단계 하나(멀쩡 → 상함 → 부서짐) — 피해가 문턱 이상이면 이 단계입니다. 부품마다 메시 변형 · 머티리얼 값(먼지 · 녹 · 찢김)을 고릅니다. */
    struct AppearanceDamageStageDef
    {
        struct PartVariant
        {
            hashed_string _part{};
            hashed_string _variant{};
        };

        vector<PartVariant>                _listVariant{};
        vector<AppearanceMaterialValueDef> _listMaterialValue{};
        hashed_string                      _name{};
        float32                            _threshold{ 0.0f };
    };
} // namespace sw

namespace sw
{
    /** @brief 아이템 외형 하나입니다. */
    struct SW_GF_API ItemVisualDef
    {
        vector<AppearancePartDef>        _listPart{};
        vector<AppearanceStateDef>       _listState{};
        vector<AppearanceDamageStageDef> _listDamageStage{}; ///< 문턱 오름차순
        TagContainer                     _tags{};            ///< 외형 규칙이 보는 태그(`Helmet.FullFace` · `Sleeve.Long`) — 형상 변경이면 보이는 외형의 것
        hashed_string                    _id{};
        hashed_string                    _occupancy{};     ///< `SlotTable` 의 칸 묶음 — 비면 자기 칸만
        hashed_string                    _customization{}; ///< 꾸미기 스키마 id — 비면 꾸미기 없음
        hashed_string                    _defaultState{};  ///< 비면 첫 상태(상태가 없으면 기본 배치)

        const AppearancePartDef*        findPart( const hashed_string& name ) const;
        const AppearanceStateDef*       findState( const hashed_string& name ) const;
        const AppearanceDamageStageDef* findDamageStage( const hashed_string& name ) const;
        /** @brief 피해 @p damage 의 단계 번호입니다. 첫 문턱 아래면 −1(멀쩡)입니다. */
        int32 computeDamageStageIndex( float32 damage ) const;
        /** @brief 단계 이름의 번호입니다. 없으면 −1 입니다. */
        int32 findDamageStageIndex( const hashed_string& name ) const;
    };
} // namespace sw

namespace sw
{
    /**
     * @class ItemVisualCatalog
     * @brief `<ItemVisualCatalog><ItemVisual id="rifle_m4" occupancy="TwoHanded" customization="Rifle" tags="Weapon.Rifle" defaultState="Drawn">` 아래
     *        `<Part>` · `<State>` · `<DamageStage>` 를 읽습니다(형식은 `Appearance/README.md`).
     */
    class SW_GF_API ItemVisualCatalog
    {
    public:
        [[nodiscard]] bool loadFromNode( const XmlNode& root, AppearanceLoadReport& report, string_view sourceName );
        void               clear() { _listVisual.clear(); }

        const ItemVisualDef*         findVisual( const hashed_string& id ) const;
        const vector<ItemVisualDef>& getVisuals() const { return _listVisual; }

    private:
        vector<ItemVisualDef> _listVisual{};
    };
} // namespace sw
