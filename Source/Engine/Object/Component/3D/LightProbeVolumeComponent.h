/**
 * @file LightProbeVolumeComponent.h
 * @brief 라이트 프로브 볼륨 하나입니다. 축 정렬 상자 안에 균일 격자로 프로브를 두고, 베이크 결과(`.probes`)를 읽어 간접 조명으로 씁니다(IndirectLighting.md P1).
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"
#include "Core/Container/string.h"
#include "Core/Math/VectorMath.h"

#include "Engine/Graphics/Lighting/LightProbeGrid.h"
#include "Engine/Object/Component/SceneComponent.h"
#include "Engine/Reflection/ReflectionMacros.h"

namespace sw
{
    class ValidationContext;

    /**
     * @class LightProbeVolumeComponent
     * @brief 축 정렬 상자(크기 `_extent`, 중심은 이 컴포넌트의 월드 위치) 안의 균일 프로브 격자입니다. 유니티 Adaptive Probe Volume, 언리얼 Volumetric Lightmap 의 자리입니다.
     * @details 격자의 모양은 트랜스폼과 두 칸(`_extent`, `_spacing`)에서 나옵니다(`computeLayout`). 회전과 크기 배율은 쓰지 않습니다 — 격자는 월드 축 정렬입니다.
     *          베이크(P2)가 `.probes` 를 쓰고 `_probeDataPath` 에 그 경로를 둡니다. 읽은 격자의 모양이 지금 볼륨과 다르면(옮기거나 크기를 바꾸고 다시 굽지 않았다)
     *          그 데이터를 쓰지 않고 오류를 남깁니다 — 옛 위치의 빛을 새 위치에 비추지 않습니다(폴백 없음). 굽지 않은 볼륨(경로가 빔)은 맵 검사가 경고합니다.
     *          셰이더 소비(P3)는 레지스트리로 볼륨을 모아 라이트 그리드의 항목으로 넣습니다(ClusteredLighting.md S5).
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
        /** @brief 베이크 결과(`.probes`) 경로입니다. 비었으면 굽지 않은 볼륨입니다. */
        const string& getProbeDataPath() const { return _probeDataPath; }

        /**
         * @brief 지금 트랜스폼과 칸으로 격자의 모양을 구합니다. 축마다 프로브 수는 floor(크기 / 간격) + 1 이고, 상자의 양 끝에 프로브가 놓입니다.
         * @details 베이크가 이 모양으로 굽고, 읽은 데이터가 이 모양과 같아야 씁니다.
         */
        LightProbeGridLayout computeLayout() const;
        /**
         * @brief 쓸 수 있는 베이크 데이터입니다. 굽지 않았거나, 읽지 못했거나, 모양이 지금 볼륨과 다르면 nullptr 입니다(그 이유는 읽을 때 로그에 남는다).
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
        /** @brief 굽지 않은 볼륨(경로가 빔)을 경고하고, 크기 · 간격이 0 인 볼륨을 오류로 적습니다(맵 검사). */
        void validateProbeData( ValidationContext& context ) const;

        PROPERTY( Category = "Light Probe", DisplayName = "Extent", Min = 0.0, Tooltip = "Full size of the axis-aligned probe box", Units = m )
        float3 _extent;
        PROPERTY( Category = "Light Probe", DisplayName = "Spacing", Min = 0.1, Tooltip = "Target distance between neighbouring probes", Units = m )
        float32 _spacing;
        PROPERTY( Category = "Light Probe", DisplayName = "Baked Probes", Tooltip = "Baked .probes resource path (written by the probe bake)", Validate = validateProbeData )
        string _probeDataPath;

        LightProbeGrid _probeData; ///< 읽은 베이크 데이터(비었으면 굽지 않았거나 읽지 못했다)
    };
} // namespace sw
