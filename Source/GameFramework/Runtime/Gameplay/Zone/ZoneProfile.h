/**
 * @file ZoneProfile.h
 * @brief 지역 프로필(`*.zone.xml`)입니다 — 지역 효과의 정본 데이터. 이름 붙은 모듈 목록이고, 모듈은 효과 하나(감속 · 가라앉음 · 중심 당김 · 주기 피해 · 젖음 · 내비 비용 · 무중력 …)와 그 매개변수입니다.
 * @details **상속보다 조합.** 독 늪 = 늪의 모듈 + 독 피해 모듈이다. 두 프리셋을 함께 물려받는 다중 상속(같은 칸이 부딪히는 모호함)을 두지 않는다 — `_base` 는 출발 목록 하나이고,
 *          변형 · 배치는 모듈을 이름으로 더하고 · 빼고(`_op="Remove"`) · 고친다. 처리기는 모듈 종류마다 하나이고 서로를 모른다(`ZoneModuleProcessor`) — 새 효과는 모듈 종류 하나다.
 *          언리얼 GAS 이펙트의 Component 목록 · 유니티 Volume Profile 의 Override 목록 자리다(기억).
 *
 *          **세 층** — 모두 기존 데이터 상속(`_base` · 열쇠 목록 · `Replace` · `Remove` · 자리 옮기기, `docs/agents/DataInheritance.md`)이 합친다:
 *          ① 기본 지형 프리셋(`sand.zone.xml`), ② 변형 프리셋(`_base` + 바뀌는 칸만 — 느린 사구), ③ 배치한 볼륨 · 표면 영역의 덮어쓰기(같은 형식의 모듈 목록 — `ZoneVolumeComponent`,
 *          `applyOverrides`). 섹션은 모두 선택(적지 않으면 물려받음)이고 모듈 · 매개변수 · 경우는 이름이 열쇠다. 위치 값이 볼륨 모양으로 정규화돼 같은 프리셋이 어떤 모양에도 맞는다.
 *          여러 번 쓰는 모듈은 에셋(`*.zonemodule.xml`, 루트 `ZoneModuleDef`)으로 두고 `_modulePath` 로 가리킨다 — 프로필의 칸이 에셋의 칸을 덮는다.
 *          로드 때 합친 결과를 검증한다(모듈 종류 · 매개변수 이름 · 식 · 동작 이름 · 카탈로그 참조, `ZoneCompiledProfile::compile`). 코드에 지형 이름이 나오지 않는다 — 새 지형은 `.zone.xml` 하나다.
 * @code
 *   <ZoneProfile _base="common/zones/sand.zone.xml">
 *     <_listModule>
 *       <ZoneModuleDef _name="sink"><_listParam><ZoneParamDef _name="interval" _value="0.5"/></_listParam></ZoneModuleDef>   <!-- 느린 사구: 칸 하나만 -->
 *       <ZoneModuleDef _name="npcAvoid" _op="Remove"/>                                                                      <!-- NPC 가 반응하지 않는 사구 -->
 *       <ZoneModuleDef _name="poison" _modulePath="common/zones/poisondamage.zonemodule.xml"/>                               <!-- 독 피해 모듈을 더한다 -->
 *     </_listModule>
 *   </ZoneProfile>
 * @endcode
 */
#pragma once
#include "Core/Container/string.h"
#include "Core/Container/vector.h"
#include "Core/Foundation/Types.h"
#include "Core/Name/hashed_string.h"

#include "Engine/Asset/Cache/SharedAssetCache.h"
#include "Engine/Content/AssetID.h"
#include "Engine/Content/AssetTypeRegistry.h"
#include "Engine/Reflection/ReflectionMacros.h"
#include "Engine/Serialization/Serializer/DataPath.h"

#include "GameFramework/GameFrameworkExports.h"

namespace sw
{
    /** @brief 할 일 · 반응이 손대는 대상입니다(들어온 개체를 기준으로 고른다). */
    ENUM()
    enum class ZoneTargetRole : uint8
    {
        Inherit = 0, ///< 적지 않음 — 덮어쓰기에서는 물려받고, 합친 결과에서는 들어온 개체다
        Occupant,    ///< 들어온 개체(캐릭터 · NPC · 탈것 · 물체)
        Vehicle,     ///< 들어온 개체가 탄 탈것(개체가 탈것이면 그 자신)
        Riders,      ///< 들어온 탈것의 탑승자 모두
        Nearby       ///< 들어온 개체 근처의 개체(반경은 모듈의 `radius`)
    };

    /** @brief 배치 덮어쓰기(③ 층)의 모듈 · 경우 작업입니다. 파일 층은 같은 이름의 `_op` 속성을 합치기가 먼저 소비한다. */
    ENUM()
    enum class ZoneOverrideOp : uint8
    {
        Merge = 0, ///< 있으면 적은 칸만 고치고, 없으면 더한다
        Replace,   ///< 물려받지 않고 바꾼다(없으면 오류)
        Remove     ///< 뺀다(없으면 오류)
    };

    /** @brief 모듈의 매개변수 하나입니다. 값은 글 — 수치 칸이면 수나 식(`20 * (1 - target.FireResist)`)이고, 처리기가 읽을 때 형식을 정한다. */
    REFLECT()
    struct SW_GF_API ZoneParamDef
    {
        REFLECT_BODY();

        PROPERTY( Required, InheritKey, Tooltip = "Parameter name the module kind declares" )
        hashed_string _name{};
        PROPERTY( Meta = "NotLocalizable", Tooltip = "Value: a number or an expression (numeric fields), or text (names, action lists)" )
        string _value{};
    };

    /** @brief 경우 목록의 한 줄입니다 — 같은 사건에 위에서 처음 맞는 하나만 한다(화염 면역 → 없음, 저항 → 약한 화상, 탈것 → 내구 피해, 그 밖 → 강한 화상). */
    REFLECT()
    struct SW_GF_API ZoneCaseDef
    {
        REFLECT_BODY();

        PROPERTY( Required, InheritKey, Tooltip = "Case name (key for variants and placement overrides)" )
        hashed_string _name{};
        PROPERTY( Meta = "NotLocalizable", Tooltip = "Condition expression; empty always matches (put the catch-all last)" )
        string _condition{};
        PROPERTY( Meta = "NotLocalizable", Tooltip = "Actions run when this case is the first match, separated by ';' (empty: do nothing)" )
        string _do{};
        PROPERTY( Tooltip = "Placement overrides only: merge, replace or remove the inherited case" )
        ZoneOverrideOp _op{ ZoneOverrideOp::Merge };
    };

    /** @brief 모듈 하나입니다(효과 하나와 그 매개변수). 파일(`*.zonemodule.xml`)로 두면 여러 프로필이 함께 쓴다. */
    REFLECT()
    struct SW_GF_API ZoneModuleDef
    {
        REFLECT_BODY();

        /** @brief 모듈 에셋 종류입니다(`*.zonemodule.xml`). 공유 캐시는 `ZoneProfile.cpp` 가 인스턴스화한다. */
        static constexpr AssetTypeDef kAssetType = AssetTypeDef::makeDataFile( "zonemodule", AssetPathRule{ {}, {}, ".zonemodule.xml" }, {} );

        PROPERTY( Required, InheritKey, Tooltip = "Module name in the profile (key for variants and placement overrides)" )
        hashed_string _name{};
        PROPERTY( Tooltip = "Module kind (processor): Action, PhysicsField, Blackboard, NavArea, SurfaceChannel ... (empty: from the module asset)" )
        hashed_string _kind{};
        PROPERTY( Tooltip = "Reusable module asset (*.zonemodule.xml); the fields written here override it" )
        DataPath _modulePath;
        PROPERTY( Meta = "NotLocalizable", Tooltip = "Condition expression the target must meet (empty: always); zone values (zoneDepth01, timeInside, immersion, impactSpeed ...) and tag() are readable" )
        string _condition{};
        PROPERTY( Tooltip = "Who the module acts on" )
        ZoneTargetRole _target{ ZoneTargetRole::Inherit };
        PROPERTY( Tooltip = "Parameters by name" )
        vector<ZoneParamDef> _listParam;
        PROPERTY( Tooltip = "First-match cases (Action modules)" )
        vector<ZoneCaseDef> _listCase;
        PROPERTY( Tooltip = "Placement overrides only: merge, replace or remove the inherited module" )
        ZoneOverrideOp _op{ ZoneOverrideOp::Merge };

        /** @brief 모듈 에셋을 읽습니다(종류의 로더 · 공유 캐시). 읽지 못하면 오류를 남기고 false 입니다. */
        [[nodiscard]] bool loadFromAsset( const AssetID& assetID );
        /** @brief 이름의 매개변수입니다. 없으면 nullptr 입니다. */
        const ZoneParamDef* findParam( const hashed_string& name ) const;
        /** @brief 배치 덮어쓰기 항목으로서 아무것도 바꾸지 않는지입니다(합치기인데 적은 칸 · 매개변수 · 경우가 없다). 되돌리기가 빈 항목을 남기지 않게 지운다. */
        bool isEmptyOverride() const;

        /**
         * @brief 배치 덮어쓰기 목록 @p inoutListOverride 에서 모듈 @p moduleName 의 매개변수 @p paramName 을 @p value 로 덮어씁니다(인스펙터에서 물려받은 값을 고칠 때).
         * @details 그 모듈의 덮어쓰기 항목이 없으면 합치기 항목을 더하고, 있으면 그 항목의 매개변수를 고치거나 더한다. 통째로 바꾸기 · 빼기 항목에는 쓰지 않는다(false — 그 항목은 반사 칸에서 고친다).
         */
        [[nodiscard]] static bool setOverrideParam( vector<ZoneModuleDef>& inoutListOverride, const hashed_string& moduleName, const hashed_string& paramName, string_view value );
        /** @brief 모듈 @p moduleName 덮어쓰기에서 매개변수 @p paramName 을 지웁니다(물려받은 값으로 되돌림). 항목이 비면 항목째 지운다. 지운 것이 없으면 false 입니다. */
        static bool revertOverrideParam( vector<ZoneModuleDef>& inoutListOverride, const hashed_string& moduleName, const hashed_string& paramName );
        /** @brief 모듈 @p moduleName 의 덮어쓰기 항목을 통째로 지웁니다. 없으면 false 입니다. */
        static bool revertOverrideModule( vector<ZoneModuleDef>& inoutListOverride, const hashed_string& moduleName );
    };

    /**
     * @class ZoneProfile
     * @brief 지역 프로필 한 벌입니다(파일 머리말 참고). 볼륨 · 표면 종류 · 런타임 영역이 같은 프로필을 참조하고, 처리 · 검증은 `ZoneCompiledProfile` 이 맡습니다.
     */
    REFLECT()
    struct SW_GF_API ZoneProfile
    {
        REFLECT_BODY();

        /** @brief 이 데이터 종류입니다(`*.zone.xml`). 타입 있는 참조 `AssetRef<ZoneProfile>` 이 이 접미사로 가른다. */
        static constexpr AssetTypeDef kAssetType = AssetTypeDef::makeDataFile( "zone", AssetPathRule{ {}, {}, ".zone.xml" }, {} );

        PROPERTY( Tooltip = "Modules by name; the order is the processing order" )
        vector<ZoneModuleDef> _listModule;

        /** @brief 리소스를 읽고(상속 · 모듈 에셋까지 합쳐) 칸 사이 규칙을 봅니다. 실패면 오류를 남기고 비운 채 false 입니다. */
        [[nodiscard]] bool loadFromAsset( const AssetID& assetID );
        /** @brief 디스크 파일을 읽습니다(테스트). 규칙은 `loadFromAsset` 과 같습니다. */
        [[nodiscard]] bool loadFromDiskFile( string_view diskPath );

        /**
         * @brief 배치 덮어쓰기(③ 층)를 얹습니다. 모듈은 이름으로 합치고(`_op` — 합치기 · 바꾸기 · 빼기), 합칠 때 적은 칸만(종류 · 모듈 에셋 · 조건 · 대상이 비어 있지 않으면) 덮고
         *        매개변수는 이름으로 덮으며 경우는 이름으로 합칩니다. 없는 이름을 바꾸거나 빼면 오류를 남기고 false 입니다(합치기는 더한다).
         */
        [[nodiscard]] bool applyOverrides( const vector<ZoneModuleDef>& listOverride, string_view sourceName );
        /** @brief 이름의 모듈입니다. 없으면 nullptr 입니다. */
        const ZoneModuleDef* findModule( const hashed_string& name ) const;

    private:
        /** @brief 모듈 에셋(`_modulePath`)을 펼쳐 프로필 칸을 그 위에 얹고, 이름 · 종류 규칙을 봅니다. */
        [[nodiscard]] bool finishLoad( string_view path, bool bRead );
    };

    /** @brief 공유 캐시는 `ZoneProfile.cpp` 가 인스턴스화합니다. 쓰는 쪽은 `SharedAssetCache<ZoneProfile>::acquire` 로 한 벌을 받습니다. */
    SW_DECLARE_SHARED_ASSET_CACHE( SW_GF_API_EXTERN_TEMPLATE, ZoneProfile );
    SW_DECLARE_SHARED_ASSET_CACHE( SW_GF_API_EXTERN_TEMPLATE, ZoneModuleDef );
} // namespace sw
