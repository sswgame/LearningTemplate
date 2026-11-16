/**
 * @file SurfaceKind.h
 * @brief 표면 종류 표입니다. 바닥이 무엇인지(눈 · 모래 · 돌 · 물)와 그 표면이 캐릭터 · 탈것에게 하는 일(발소리 분류 · 이동 계수 · 구름 저항 · 그립 · 발자국 도장 · 지역 프로필)을 한 곳에 둡니다.
 * @details 표면 종류의 이름은 물리 재질 이름과 같다(`physicssettings.xml` 의 `_listMaterial`) — 콜라이더는 재질을, 지형 레이어는 그 레이어가 뜻하는 재질
 *          (`TerrainLayer::_physicsMaterial`)을, 물은 표의 `_waterKind` 를 낸다(`SurfaceSample`). 언리얼 `UPhysicalMaterial::SurfaceType` · 랜드스케이프 레이어의
 *          물리 재질과 같은 자리다 — 형식(콜라이더 · 높이장 · 물)마다 데이터를 합치지 않고, 각 형식이 재질 이름 하나를 내는 어댑터가 된다.
 *          물리 마찰(`PhysicsMaterialDef::_friction`)과 게임 이동 계수(`_moveScale`)는 같은 값이라고 가정하지 않는다 — 얼음은 미끄럽지만 걷는 속도는 그대로다.
 *          지역 효과(빙판 · 용암 · 바다 · 가시덩굴)는 종류의 `_zoneProfile` 이 가리키는 지역 프로필(`*.zone.xml`, GameFramework `ZoneProfile`)이다 — 지형 레이어 · 아무 메시든
 *          콜라이더 재질이 곧 프로필을 부른다(전용 컴포넌트 없음, 언리얼 Physical Material · 유니티 PhysicMaterial 자리). 엔진은 경로만 들고 프로필을 읽지 않는다(층 방향).
 *          종류 이름이 목록의 열쇠라 게임이 덮어쓰기 층(`_listResourceLayer`)으로 종류 하나의 칸만 얹을 수 있다(Empty 가 `_zoneProfile` 을 단다).
 *          모든 물리 재질은 표에 종류가 있어야 한다(테스트 `SurfaceKindTest.ShippedTableCoversPhysicsMaterials`) — 모르는 재질을 다른 종류로 대신하지 않는다.
 *
 *          XML 예(`ReflectedXMLFile`):
 *          @code
 *          <SurfaceKindTable _waterKind="Water" _deformResolution="512" _deformExtent="16" _deformDepthRange="0.15">
 *            <_listKind>
 *              <SurfaceKindDef _name="Snow" _footstep="snow" _moveScale="0.85">
 *                <_footprint _shape="Print" _size="0.14,0.28" _depth="0.06" _fillSpeed="0.0025"/>
 *              </SurfaceKindDef>
 *            </_listKind>
 *          </SurfaceKindTable>
 *          @endcode
 *          발자국은 표면 변형 층(`SurfaceDeformSystem`)의 도장이다 — 눈 전용 오브젝트가 없고, 어느 종류든 `_footprint` 를 적으면 자국이 남는다.
 *          변형 층의 격자 크기 · 창 너비 · 깊이 범위도 이 표가 정한다(지면 머티리얼이 읽는 텍스처 하나).
 */
#pragma once
#include "Core/Container/vector.h"
#include "Core/Foundation/Macros.h"
#include "Core/Foundation/Types.h"
#include "Core/Memory/Memory.h"
#include "Core/Name/hashed_string.h"

#include "Engine/Asset/Cache/SharedAssetCache.h"
#include "Engine/Content/AssetID.h"
#include "Engine/Environment/Surface/SurfaceStamp.h"
#include "Engine/Reflection/ReflectionMacros.h"
#include "Engine/Serialization/Serializer/DataPath.h"

namespace sw
{
    /** @brief 표면 종류 하나입니다. 이름은 물리 재질 이름과 같습니다. */
    REFLECT()
    struct SW_API SurfaceKindDef
    {
        REFLECT_BODY();

        PROPERTY( Required, InheritKey, Tooltip = "Surface kind name; equals the physics material name that stands for it" )
        hashed_string _name{};
        PROPERTY( Required, Tooltip = "Footstep class put into the footstep sound path ({surface})" )
        hashed_string _footstep{};
        PROPERTY( Min = 0.0, Max = 1.0, Tooltip = "Multiplier on the requested walk speed while standing on this surface (not the physics friction; resistances take the smallest)" )
        float32 _moveScale{ 1.0f };
        PROPERTY( Min = 0.0, Tooltip = "Rolling resistance coefficient for wheels and runners (deceleration = coefficient * gravity); ice low, sand and snow high" )
        float32 _rollingResistance{ 0.015f };
        PROPERTY( Min = 0.0, Max = 1.0, Tooltip = "Side grip scale for vehicles without a physics body (ice below 1)" )
        float32 _gripScale{ 1.0f };
        PROPERTY( Tooltip = "Stamp a foot leaves on the surface deform layer (no depth and no rim: this surface keeps no footprints)" )
        SurfaceStampDef _footprint;
        PROPERTY( Tooltip = "Zone profile (*.zone.xml) applied to what stands on, touches or is submerged in this surface (empty: none)" )
        DataPath _zoneProfile;

        /** @brief 발자국을 남기는 표면인지입니다. */
        bool hasFootprints() const { return _footprint.isSet(); }
        /** @brief 지역 프로필이 붙은 표면인지입니다. */
        bool hasZoneProfile() const { return _zoneProfile.isSet(); }
    };
} // namespace sw

namespace sw
{
    /**
     * @class SurfaceKindTable
     * @brief 표면 종류 표입니다. 엔진 기본 에셋 표의 `_surfaceKinds` 경로 하나를 공유 캐시로 읽습니다(`findDefault`).
     * @details 읽을 때 모르는 칸 · 범위 밖 값은 `ReflectedXMLFile` 이, 빈 표 · 이름 없음 · 겹친 이름 · 표에 없는 물 종류 · 틀린 발자국 도장(`SurfaceStampDef::validate`)은
     *          `finishLoad` 가 오류로 낸다.
     */
    REFLECT()
    struct SW_API SurfaceKindTable
    {
        REFLECT_BODY();
        friend struct AssetTypeLoader;

        PROPERTY( Required, Tooltip = "Surface kind a foot reports when it stands in water deeper than zero" )
        hashed_string _waterKind{};
        PROPERTY( Required, Min = 16, Max = 2048, Tooltip = "Texels along each side of the surface deform layer (one texture the ground materials read)" )
        uint32 _deformResolution{ 0 };
        PROPERTY( Required, Min = 1.0, Units = m, Tooltip = "Side of the square window the surface deform layer covers ahead of the camera" )
        float32 _deformExtent{ 0.0f };
        PROPERTY( Required, Min = 0.01, Units = m, Tooltip = "Deepest sink and highest rim the deform texture can hold; deeper stamps clamp to it" )
        float32 _deformDepthRange{ 0.0f };
        PROPERTY( Tooltip = "Surface kinds; one per physics material" )
        vector<SurfaceKindDef> _listKind;

        /** @brief 표를 **디스크 파일**에서 읽습니다(테스트의 임시 파일). 검사는 리소스 읽기와 같습니다. */
        [[nodiscard]] bool loadFromDiskFile( string_view diskPath );
        /** @brief 이름의 종류입니다. 없으면 nullptr 입니다. */
        const SurfaceKindDef* findKind( const hashed_string& name ) const;
        /** @brief 물 종류입니다(읽기가 표에 있는지 검사했다). */
        const SurfaceKindDef& getWaterKind() const;

        /**
         * @brief 엔진 기본 에셋 표(`EngineDefaultAssets::_surfaceKinds`)의 표입니다. 기본 에셋 표가 없거나 읽지 못하면 오류를 남기고 nullptr 입니다.
         * @details 공유 캐시가 경로마다 한 벌을 나눠 주므로 발걸음마다 다시 읽지 않는다. 파일을 고치면 제자리에서 다시 읽힌다.
         */
        static shared_ptr<const SurfaceKindTable> findDefault();

    private:
        /** @brief 리소스 파일에서 읽습니다. 종류의 로더(공유 캐시)만 부른다. */
        [[nodiscard]] bool loadFromAsset( const AssetID& assetID );
        /** @brief 읽은 표를 검사합니다. 읽기(@p bRead)나 검사가 실패하면 표를 비우고 false 입니다. */
        [[nodiscard]] bool finishLoad( string_view path, bool bRead );
    };

    /** @brief 표면 종류 표를 경로마다 한 벌 나눠 줍니다("surfacekinds", 인스턴스화는 `SurfaceKind.cpp`). */
    SW_DECLARE_SHARED_ASSET_CACHE( SW_API_EXTERN_TEMPLATE, SurfaceKindTable );
} // namespace sw
