/**
 * @file ReflectionProbeComponent.h
 * @brief 반사 프로브 하나입니다. 이 위치에서 찍어 거른 둘레의 빛(`.reflprobe`)을 축 정렬 상자 안의 표면 반사로 씁니다.
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"
#include "Core/Container/string.h"
#include "Core/Math/VectorMath.h"

#include "Engine/Graphics/Lighting/ReflectionProbeData.h"
#include "Engine/Object/Component/SceneComponent.h"
#include "Engine/Reflection/ReflectionMacros.h"

namespace sw
{
    class ValidationContext;

    /**
     * @class ReflectionProbeComponent
     * @brief 축 정렬 상자(크기 `_extent`, 중심은 이 컴포넌트의 월드 위치) 안의 표면이 반사로 받는 빛입니다. 언리얼 Box Reflection Capture, 유니티 Reflection Probe 의 자리입니다.
     * @details 베이크(`ReflectionProbeBaker`, 에디터 `lighting.bakeReflectionProbes`)가 이 위치에서 면 여섯을 GPU 로 찍어 접고 거른 `.reflprobe` 를 쓰고 `_probeDataPath` 에 적는다.
     *          읽은 데이터의 찍은 위치가 지금 위치와 다르면(옮기고 다시 베이크하지 않았다) 쓰지 않고 오류를 남긴다 — 옛 자리의 반사를 새 자리에 비추지 않는다(폴백 없음).
     *          셰이더 소비는 라이트 목록의 `SW_LIGHT_TYPE_REFLECTION_PROBE` 원소(그리드 항목)다(`collectSceneLights`). 상자 밖은 받지 않고, 경계 안쪽 `_blendDistance` 동안
     *          앰비언트 반사로 섞여 사라진다. 시차 보정(상자에 닿는 점으로 반사 방향을 고치기)은 하지 않는다 — shortcut: 상자가 크면 가까운 벽의 반사가 어긋난다, 실내 씬을 굽게 되면 더한다.
     */
    REFLECT( Category = "Rendering 3D", DisplayName = "Reflection Probe", Tooltip = "Baked reflection of the surroundings for surfaces inside a box", Meta = "Icon=LightPoint, IconColor=Light" )
    class SW_API ReflectionProbeComponent : public SceneComponent
    {
    public:
        REFLECT_BODY();

        /** @brief 기본 프로브 값으로 만듭니다. */
        ReflectionProbeComponent();
        /** @brief 기본 소멸자입니다. */
        virtual ~ReflectionProbeComponent() override = default;

        /** @brief 상자 크기(축마다 전체 길이, m)입니다. */
        const float3& getExtent() const { return _extent; }
        /** @brief 상자 크기를 설정합니다. 음수는 0 으로 자릅니다. */
        void setExtent( const float3& extent );
        /** @brief 상자 경계 안쪽에서 반사가 앰비언트로 섞여 사라지는 폭(m)입니다. */
        float32 getBlendDistance() const { return _blendDistance; }
        /** @brief 옥타헤드럴 한 변 텍셀 수(베이크가 쓰는 크기)입니다. */
        uint32 getResolution() const { return _resolution; }
        /** @brief 베이크가 찍는 면 하나의 한 변 픽셀 수입니다. */
        uint32 getCaptureResolution() const { return _captureResolution; }
        /** @brief 베이크가 거칠기 단계마다 텍셀 하나에 쓰는 GGX 표본 수입니다. */
        uint32 getBakeSampleCount() const { return _bakeSampleCount; }
        /** @brief 베이크 결과(`.reflprobe`) 경로입니다. 비었으면 베이크하지 않은 프로브입니다. */
        const string& getProbeDataPath() const { return _probeDataPath; }

        /** @brief 쓸 수 있는 베이크 데이터입니다. 베이크하지 않았거나, 읽지 못했거나, 찍은 위치가 지금 위치와 다르면 nullptr 입니다. */
        const ReflectionProbeData* findProbeData() const;
        /**
         * @brief 베이크 결과를 받아 들고 경로를 적습니다(베이크 도구가 파일을 쓴 뒤 부른다). 데이터가 깨졌거나 찍은 위치가 지금 위치와 다르면 받지 않고 false 입니다.
         * @param path 그 데이터를 쓴 `.reflprobe` 리소스 경로
         */
        [[nodiscard]] bool adoptProbeData( ReflectionProbeData&& data, string_view path );

        /** @brief 씬에 붙을 때 레지스트리에 자기를 등록합니다. */
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
        /** @brief 데이터의 찍은 위치가 지금 위치와 같은지 봅니다. */
        bool matchesCapturePosition( const ReflectionProbeData& data ) const;
        /** @brief 베이크하지 않은 프로브(경로가 빔)를 경고하고, 크기가 0 인 상자를 오류로 적습니다(맵 검사). */
        void validateProbeData( ValidationContext& context ) const;

        PROPERTY( Category = "Reflection Probe", DisplayName = "Extent", Min = 0.0, Tooltip = "Full size of the axis-aligned box that receives this reflection", Units = m )
        float3 _extent;
        PROPERTY( Category = "Reflection Probe", DisplayName = "Blend Distance", Min = 0.0, Tooltip = "Width inside the box border where the reflection fades to the ambient one", Units = m )
        float32 _blendDistance;
        PROPERTY( Category = "Reflection Probe", DisplayName = "Baked Reflection", Tooltip = "Baked .reflprobe resource path (written by the reflection bake)", Validate = validateProbeData )
        string _probeDataPath;
        PROPERTY( Category = "Reflection Probe Bake", DisplayName = "Resolution", Min = 4, Max = 256, Tooltip = "Octahedral texels per side of each roughness level" )
        uint32 _resolution;
        PROPERTY( Category = "Reflection Probe Bake", DisplayName = "Capture Resolution", Min = 16, Max = 1024, Tooltip = "Pixels per side of each of the six captured faces" )
        uint32 _captureResolution;
        PROPERTY( Category = "Reflection Probe Bake", DisplayName = "Samples Per Texel", Min = 1, Max = 4096, Tooltip = "GGX samples each prefiltered texel takes" )
        uint32 _bakeSampleCount;

        ReflectionProbeData _probeData; ///< 읽은 베이크 데이터(비었으면 베이크하지 않았거나 읽지 못했다)
    };
} // namespace sw
