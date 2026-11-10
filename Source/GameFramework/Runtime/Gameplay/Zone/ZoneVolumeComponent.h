/**
 * @file ZoneVolumeComponent.h
 * @brief 지역 볼륨입니다 — 그린 영역(늪 · 유황 · 독가스 · 버프 샘 · 스킬 영역 · 즉사 구역 · 무중력 방 · 사구)이 지역 프로필(`*.zone.xml`)을 참조하고, 자리마다 바꿀 모듈만 덮어씁니다.
 * @details 입력은 겹침(보이지 않는 영역)이다 — 모양(상자 · 구 · 원기둥) 안에 든 물리 개체(캐릭터 컨트롤러의 발 · 움직이는 강체의 중심)가 대상이다. 대상마다 위치 값을 낸다:
 *          `zoneDepth01`(가장자리 0 → 중심 1, 모양 기준), `distanceToCenter`(당김 중심까지 수평 거리), `immersion`(윗면 아래로 잠긴 비율), 흐름 방향(흐름 점이 있으면).
 *          처리(진입 · 체류 · 이탈 · 조건 전환 · 대상 역할)는 `ZoneTracker` 하나가 하고, 건 효과의 출처는 `volume:<오브젝트 이름>` 이다(Ability 디버거).
 *          **런타임 영역**: 같은 컴포넌트에 수명(`_lifetime` — 끝나면 오브젝트를 지운다)과 퍼짐(`_growthRate` · `_maxGrowth` — 크기가 는다)을 두면 독가스 살포 프리팹이 된다.
 *          **세 층**: 프리셋 · 변형은 프로필 파일이, 이 자리의 차이는 `_listModuleOverride`(같은 형식, 이름 열쇠 · `_op`)가 맡는다 — 같은 사구라도 빠지는 속도 · NPC 반응을 자리마다.
 *          S6 의 `AbilityEffectVolumeComponent` 를 대신한다 — 이펙트 하나 · 진입/체류/이탈 정책이던 것이 프로필의 할 일 모듈이 됐다. 언리얼 `APhysicsVolume` · GAS 이펙트 볼륨, 유니티 트리거 + Effector 자리(기억).
 *          shortcut: 모양은 오너 월드 자리를 가운데로 한 축 정렬이다(회전 · 배율은 보지 않는다 — 지형과 같은 규칙). 기울인 영역이 필요하면 오너 역회전으로 자리를 옮겨 잰다.
 */
#pragma once
#include "Core/Container/vector.h"
#include "Core/Foundation/Types.h"
#include "Core/Math/Math.h"
#include "Core/Memory/Memory.h"
#include "Core/Name/hashed_string.h"

#include "Engine/Asset/AssetRef.h"
#include "Engine/Object/Component/Component.h"
#include "Engine/Reflection/ReflectionMacros.h"

#include "GameFramework/GameFrameworkExports.h"
#include "GameFramework/Runtime/Gameplay/Zone/ZoneModule.h"
#include "GameFramework/Runtime/Gameplay/Zone/ZoneOccupantIndex.h"
#include "GameFramework/Runtime/Gameplay/Zone/ZoneProfile.h"
#include "GameFramework/Runtime/Gameplay/Zone/ZoneTracker.h"

namespace sw
{
    class GameObjectManager;

    /** @brief 지역 볼륨의 모양입니다. 위치 값(`zoneDepth01`)은 모양으로 정규화된다. */
    ENUM()
    enum class ZoneVolumeShape : uint8
    {
        Box = 0, ///< 반 크기 `_halfExtents`(x · y · z). 깊이는 수평 두 축 중 가장자리에 가까운 쪽
        Sphere,  ///< 반지름 `_halfExtents.x`
        Cylinder ///< 반지름 `_halfExtents.x`, 반 높이 `_halfExtents.y`. 깊이는 수평 거리
    };

    /**
     * @class ZoneVolumeComponent
     * @brief 지역 볼륨입니다(파일 머리말 참고).
     */
    REFLECT( Category = "Gameplay", DisplayName = "Zone Volume", Tooltip = "Area that runs a zone profile (*.zone.xml) on the bodies inside: actions, physics fields, NPC and nav reactions" )
    class SW_GF_API ZoneVolumeComponent : public Component
    {
    public:
        REFLECT_BODY();

        ZoneVolumeComponent();
        ~ZoneVolumeComponent() override = default;

        /** @brief 씬의 컴포넌트 레지스트리에 오르고 씬 공간 색인의 물리 층 사용자로 세며(`ZoneOccupantIndex::addLayerReader`) 지역 프레임 시스템(`ZoneSystem`)을 붙입니다. */
        void onRegister( GameObjectManager& manager ) override;
        /** @brief 레지스트리에서 내리고, 마지막 볼륨이면 격자를, 마지막 지역 컴포넌트면 시스템을 뗍니다. */
        void onUnregister( GameObjectManager& manager ) override;
        void onBeginPlay() override;
        void onEndPlay() override;

        /** @brief 모양의 수평 반 크기 중 큰 쪽(퍼짐 포함, m)입니다 — 지역 후보 격자의 셀을 정한다. */
        float32 getHorizontalReach() const;
        /** @brief 이 볼륨이 건 효과의 출처(`volume:<오브젝트 이름>`)입니다. */
        hashed_string getSourceName() const;
        /** @brief 프로필 · 덮어쓰기를 합치고 컴파일했으면 true 입니다(실패는 시작할 때 오류로 남는다). */
        bool                       isReady() const { return _bReady == SW_TRUE; }
        const ZoneTracker&         getTracker() const { return _tracker; }
        const ZoneCompiledProfile& getCompiledProfile() const { return _compiled; }
        /** @brief 지금 퍼진 크기(m, 반 크기 · 반지름에 더한다)입니다. */
        float32 getGrowthDistance() const { return _growth; }
        /** @brief 덮어쓰기 목록입니다(인스펙터가 물려받은 값과 바꾼 값을 가른다). */
        const vector<ZoneModuleDef>& getModuleOverrides() const { return _listModuleOverride; }
        vector<ZoneModuleDef>&       getModuleOverrides() { return _listModuleOverride; }
        /** @brief 참조한 프로필 경로입니다. */
        const AssetRef<ZoneProfile>& getProfileRef() const { return _zoneProfile; }

        /**
         * @brief 프로필에 덮어쓰기를 얹은 합친 결과를 만듭니다(시작 · 인스펙터 · 테스트). 프로필을 읽지 못하거나 덮어쓰기가 틀리면 오류를 남기고 false 입니다.
         */
        [[nodiscard]] bool composeProfile( ZoneProfile& outProfile ) const;
        /** @brief 코드로 만든 볼륨 · 테스트가 칸을 정합니다. 씬에 놓은 볼륨은 PROPERTY 가 채운다. */
        void configure( string_view profilePath, ZoneVolumeShape shape, const float3& halfExtents );
        /**
         * @brief 이번 프레임을 처리합니다 — 안에 든 대상을 고르고 위치 값을 내 `ZoneTracker` 에 넘깁니다(`ZoneSystem` 이 틱 뒤 게임 스레드에서 부른다). 준비되지 않았으면 할 일이 없다.
         * @details 후보는 씬을 돌지 않고 씬 공간 색인(`ZoneOccupantIndex` → `SceneSpatialGrid`)에서 자기 수평 사각형에 든 것만 받는다 — 움직인 개체만 색인에서 옮긴다.
         */
        void updateZone( GameObjectManager& manager, float32 deltaTime );
        /** @brief 월드 점 @p position(발)이 안이면 위치 값을 채우고 true 입니다. @p bodyHeight 는 잠긴 비율의 분모입니다. */
        [[nodiscard]] bool sampleAt( const float3& position, float32 bodyHeight, ZoneSample& outSample ) const;

    private:
        /** @brief 프로필을 합치고 컴파일해 입력을 엽니다. */
        void activateZone();
        /** @brief 흐름 점이 있으면 @p position 에서 가장 가까운 구간의 방향(단위, 월드)입니다. 없으면 0 입니다. */
        float3 computeFlowDirection( const float3& center, const float3& position ) const;

    private:
        PROPERTY( Required, Category = "Zone", DisplayName = "Zone Profile", Tooltip = "Zone profile (*.zone.xml) this volume runs" )
        AssetRef<ZoneProfile> _zoneProfile;
        PROPERTY( Category = "Zone", DisplayName = "Module Overrides", Tooltip = "This placement's changes: modules by name (_op Merge/Replace/Remove), only the fields that differ" )
        vector<ZoneModuleDef> _listModuleOverride;
        PROPERTY( Category = "Shape", DisplayName = "Shape" )
        ZoneVolumeShape _shape;
        PROPERTY( Category = "Shape", DisplayName = "Half Extents", Min = 0.01, Units = m, Tooltip = "Box half size; x is the radius of a sphere or cylinder, y the cylinder half height" )
        float3 _halfExtents;
        PROPERTY( Category = "Shape", DisplayName = "Pull Center", Units = m, Tooltip = "Offset of the pull center from the owner (zone values distanceToCenter, ToCenter and Swirl fields)" )
        float3 _pullCenterOffset;
        PROPERTY( Category = "Shape", DisplayName = "Flow Points", Units = m, Tooltip = "Path of the flow relative to the owner (Flow fields follow the nearest segment)" )
        vector<float3> _listFlowPoint;
        PROPERTY( Category = "Runtime", DisplayName = "Lifetime", Min = 0.0, Units = s, Tooltip = "Seconds before the volume's object is destroyed (0: stays) - spread gas, temporary fields" )
        float32 _lifetime;
        PROPERTY( Category = "Runtime", DisplayName = "Growth Rate", Min = 0.0, Units = "m/s", Tooltip = "How fast the size grows (added to the half extents and radius)" )
        float32 _growthRate;
        PROPERTY( Category = "Runtime", DisplayName = "Max Growth", Min = 0.0, Units = m, Tooltip = "Most the size grows" )
        float32 _maxGrowth;

        ZoneCompiledProfile           _compiled;
        ZoneTracker                   _tracker;
        vector<ZoneOccupantCandidate> _listCandidateScratch; ///< 격자 질의 결과 재사용 버퍼
        vector<Component*>            _listKeyScratch;       ///< 색인 질의의 열쇠 버퍼(재사용)
        float32                       _age;
        float32                       _growth;
        uint8                         _bReady   : 1;
        uint8                         _bOpened  : 1; ///< 시작을 시도했다(실패도 — 다시 오류를 내지 않는다)
        uint8                         _reserved : 6;
    };
} // namespace sw
