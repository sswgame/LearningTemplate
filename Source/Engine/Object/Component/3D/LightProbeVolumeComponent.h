/**
 * @file LightProbeVolumeComponent.h
 * @brief 라이트 프로브 볼륨 하나입니다. 축 정렬 상자 안에 균일 격자로 프로브를 두고, 베이크 결과(`.probes`)를 읽어 간접 조명으로 씁니다.
 */
#pragma once
#include "Core/Container/string.h"
#include "Core/Foundation/Macros.h"
#include "Core/Foundation/Types.h"
#include "Core/Math/AABB.h"
#include "Core/Math/VectorMath.h"
#include "Core/Memory/Memory.h"

#include "Engine/Graphics/Lighting/LightProbeGrid.h"
#include "Engine/Graphics/Lighting/SceneVoxelGrid.h"
#include "Engine/Object/Component/SceneComponent.h"
#include "Engine/Reflection/ReflectionMacros.h"

namespace sw
{
    class ValidationContext;

    /**
     * @class LightProbeVolumeComponent
     * @brief 축 정렬 상자(크기 `_extent`, 중심은 이 컴포넌트의 월드 위치) 안의 균일 프로브 격자입니다. 유니티 Adaptive Probe Volume, 언리얼 Volumetric Lightmap 의 자리입니다.
     * @details 격자의 모양은 트랜스폼과 두 칸(`_extent`, `_spacing`)에서 나옵니다(`computeLayout`). 회전과 크기 배율은 쓰지 않습니다 — 격자는 월드 축 정렬입니다.
     *          베이크(P2)가 `.probes` 를 쓰고 `_probeDataPath` 에 그 경로를 둡니다. 읽은 격자의 모양이 지금 볼륨과 다르면(옮기거나 크기를 바꾸고 다시 베이크하지 않았다)
     *          그 데이터를 쓰지 않고 오류를 남깁니다 — 옛 위치의 빛을 새 위치에 비추지 않습니다(폴백 없음). 베이크하지 않은 볼륨(경로가 빔)은 맵 검사가 경고합니다.
     *          셰이더 소비(P3)는 레지스트리로 볼륨을 모아 라이트 그리드의 항목으로 넣습니다.
     */
    REFLECT( Category = "Rendering 3D", DisplayName = "Light Probe Volume", Tooltip = "Grid of baked spherical-harmonics light probes for indirect lighting", Meta = "Icon=LightPoint, IconColor=Light" )
    class SW_API LightProbeVolumeComponent : public SceneComponent
    {
    public:
        REFLECT_BODY();

        /** @brief 기본 볼륨 값으로 만듭니다. */
        LightProbeVolumeComponent();
        /** @brief 기본 소멸자입니다. */
        virtual ~LightProbeVolumeComponent() override = default;

        /** @brief 상자 크기(축마다 전체 길이, m)입니다. */
        const float3& getExtent() const { return _extent; }
        /** @brief 상자 크기를 설정합니다. 음수는 0 으로 자릅니다. */
        void setExtent( const float3& extent );
        /** @brief 프로브 사이 목표 간격(m)입니다. 축마다 실제 간격은 상자를 고르게 나눈 값입니다(`computeLayout`). */
        float32 getSpacing() const { return _spacing; }
        /** @brief 목표 간격을 설정합니다. */
        void setSpacing( float32 spacing );
        /** @brief 베이크 결과(`.probes`) 경로입니다. 비었으면 베이크하지 않은 볼륨입니다. */
        const string& getProbeDataPath() const { return _probeDataPath; }
        /** @brief 베이크가 프로브마다 쏘는 균일 구 방향 수입니다(`LightProbeBaker`). */
        uint32 getBakeSampleCount() const { return _bakeSampleCount; }
        /**
         * @brief 베이크가 레이가 닿은 면에 두는 반사율입니다. 물리 씬은 표면의 머티리얼을 모르므로 볼륨이 한 값으로 정한다(회색 0.5 가 기본).
         */
        float32 getBounceAlbedo() const { return _bounceAlbedo; }

        /**
         * @brief 켜져 있으면 베이크 대신 GPU 가 프로브를 프레임마다 조금씩 다시 계산합니다. 베이크 데이터는 쓰지 않는다.
         * @details 장면은 정적 메시의 점유 복셀(`SceneVoxelGrid`)이다 — 움직이는 물체와 스킨 메시, 반투명은 빛을 가리지 않는다. 직접광이 닿은 면이 한 번 튕긴 빛(반사율 = `getBounceAlbedo`)과
         *          하늘만 담는다(베이커와 같은 규칙).
         */
        bool isRealtime() const { return _bRealtime == SW_TRUE; }
        /** @brief 실시간 갱신을 켜고 끕니다(인스펙터 편집과 같은 효과). */
        void setRealtime( bool bRealtime );
        /** @brief 실시간 갱신이 장면을 옮겨 담는 복셀 한 변의 목표 길이(m)입니다. 상자가 크면 칸 수 상한(`SceneVoxelGrid::kMaxCellsPerAxis`)에 맞춰 커진다. */
        float32 getVoxelSize() const { return _voxelSize; }
        /** @brief 실시간 갱신이 프로브 하나를 한 번 갱신할 때 쏘는 광선 수입니다. */
        uint32 getRealtimeRayCount() const { return _realtimeRayCount; }
        /** @brief 모든 프로브가 한 번씩 갱신되는 데 걸리는 프레임 수입니다(프로브를 이 수만큼의 조로 나눠 프레임마다 한 조씩 갱신한다). */
        uint32 getRealtimeUpdatePeriod() const { return _realtimeUpdatePeriod; }
        /** @brief 갱신한 값이 기록에 섞이는 비율입니다(1 이면 새 값으로 바로 바뀐다). 낮을수록 잡음이 시간에 걸쳐 평균된다. */
        float32 getRealtimeBlend() const { return _realtimeBlend; }
        /**
         * @brief 실시간 갱신이 광선을 쏘는 복셀 상자입니다 — 볼륨 상자에 광선이 닿을 바깥 여유(간격 + 고정 여유)를 더한 것입니다. 이 밖의 면은 보지 않는다(광선은 하늘로 나간다).
         */
        AABB computeRealtimeVoxelBounds() const;
        /**
         * @brief 지금 상자 · 칸 크기 · 프리미티브 집합 세대(@p primitiveSetGeneration)에 맞는 복셀 격자 캐시를 돌려줍니다. 없거나 낡았으면 nullptr 입니다(부르는 쪽이 다시 만들어 `setRealtimeVoxelGrid`).
         * @details 수집 경로(`collectSceneLights`, 게임 스레드)에서만 부른다 — 캐시는 const 수집 길에서 채우므로 mutable 이다.
         */
        shared_ptr<const SceneVoxelGrid> findRealtimeVoxelGrid( uint64 primitiveSetGeneration ) const;
        /** @brief `findRealtimeVoxelGrid` 의 캐시를 채웁니다. @p primitiveSetGeneration 은 만들 때의 프리미티브 집합 세대입니다. */
        void setRealtimeVoxelGrid( shared_ptr<const SceneVoxelGrid> grid, uint64 primitiveSetGeneration ) const;

        /**
         * @brief 지금 트랜스폼과 칸으로 격자의 모양을 구합니다. 축마다 프로브 수는 floor(크기 / 간격) + 1 이고, 상자의 양 끝에 프로브가 놓입니다.
         * @details 베이크가 이 모양으로 베이크하고, 읽은 데이터가 이 모양과 같아야 씁니다.
         */
        LightProbeGridLayout computeLayout() const;
        /**
         * @brief 쓸 수 있는 베이크 데이터입니다. 베이크하지 않았거나, 읽지 못했거나, 모양이 지금 볼륨과 다르면 nullptr 입니다(그 이유는 읽을 때 로그에 남는다).
         */
        const LightProbeGrid* findProbeData() const;
        /**
         * @brief 베이크 결과를 받아 들고 경로를 적습니다(베이크 도구가 파일을 쓴 뒤 부른다). 데이터의 모양이 지금 볼륨과 다르면 받지 않고 false 입니다.
         * @param path 그 데이터를 쓴 `.probes` 리소스 경로
         */
        [[nodiscard]] bool adoptProbeData( LightProbeGrid&& grid, string_view path );

        /** @brief 씬에 붙을 때 볼륨 레지스트리에 자기를 등록합니다. */
        void onRegister( GameObjectManager& manager ) override;
        /** @brief 씬에서 떨어질 때 등록을 해제합니다. */
        void onUnregister( GameObjectManager& manager ) override;
        /** @brief 씬 · 프리팹에서 읽은 뒤 베이크 데이터를 읽습니다. */
        void onPostLoad() override;
        /** @brief 경로가 바뀌면 베이크 데이터를 다시 읽습니다. */
        void onPropertyChanged( hashed_string propertyName ) override;

    private:
        /** @brief `_probeDataPath` 의 데이터를 읽습니다. 경로가 비면 데이터를 비웁니다. 읽지 못하면 오류를 남기고 비웁니다. */
        void reloadProbeData();
        /** @brief 베이크하지 않은 볼륨(경로가 빔)을 경고하고, 크기 · 간격이 0 인 볼륨을 오류로 적습니다(맵 검사). */
        void validateProbeData( ValidationContext& context ) const;

        PROPERTY( Category = "Light Probe", DisplayName = "Extent", Min = 0.0, Tooltip = "Full size of the axis-aligned probe box", Units = m )
        float3 _extent;
        PROPERTY( Category = "Light Probe", DisplayName = "Spacing", Min = 0.1, Tooltip = "Target distance between neighbouring probes", Units = m )
        float32 _spacing;
        PROPERTY( Category = "Light Probe", DisplayName = "Baked Probes", Tooltip = "Baked .probes resource path (written by the probe bake)", Validate = validateProbeData )
        string _probeDataPath;
        PROPERTY( Category = "Light Probe Bake", DisplayName = "Samples Per Probe", Min = 16, Max = 65536, Tooltip = "Uniform sphere rays each probe casts while baking" )
        uint32 _bakeSampleCount;
        PROPERTY( Category = "Light Probe Bake", DisplayName = "Bounce Albedo", Min = 0.0, Max = 1.0, Tooltip = "Reflectance assumed for every surface a bake ray hits" )
        float32 _bounceAlbedo;
        PROPERTY( Category = "Light Probe Realtime", DisplayName = "Realtime", Tooltip = "Update the probes on the GPU every frame instead of using a bake (static opaque meshes occlude)" )
        uint8 _bRealtime : 1; ///< 비트 필드라 리플렉션이 불리언으로 읽는다(맨 uint8 은 숫자라 씬의 "true" 를 읽지 못했다)
        PROPERTY( Category = "Light Probe Realtime", DisplayName = "Voxel Size", Min = 0.1, Tooltip = "Target voxel edge of the scene occupancy grid the realtime update traces", Units = m )
        float32 _voxelSize;
        PROPERTY( Category = "Light Probe Realtime", DisplayName = "Rays Per Update", Min = 8, Max = 1024, Tooltip = "Rays a probe casts each time it is refreshed" )
        uint32 _realtimeRayCount;
        PROPERTY( Category = "Light Probe Realtime", DisplayName = "Update Period", Min = 1, Max = 64, Tooltip = "Frames until every probe was refreshed once (probes are split into this many groups)" )
        uint32 _realtimeUpdatePeriod;
        PROPERTY( Category = "Light Probe Realtime", DisplayName = "Blend", Min = 0.01, Max = 1.0, Tooltip = "Share of a fresh result mixed into the stored probe (1 replaces it)" )
        float32 _realtimeBlend;

        LightProbeGrid _probeData; ///< 읽은 베이크 데이터(비었으면 베이크하지 않았거나 읽지 못했다)
        /// @brief 실시간 갱신의 복셀 격자 캐시입니다(`findRealtimeVoxelGrid`). 게임 스레드의 수집 경로만 만진다.
        mutable shared_ptr<const SceneVoxelGrid> _pRealtimeVoxelGrid;
        mutable uint64                           _realtimeVoxelSetGeneration; ///< 캐시를 만들 때의 프리미티브 집합 세대
        mutable AABB                             _realtimeVoxelBounds;        ///< 캐시를 만들 때의 상자
        mutable float32                          _realtimeVoxelCellSize;      ///< 캐시를 만들 때의 목표 칸 크기
    };
} // namespace sw
