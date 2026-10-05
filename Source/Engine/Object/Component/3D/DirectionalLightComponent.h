/**
 * @file DirectionalLightComponent.h
 * @brief 씬의 주광(directional key light)입니다. 방향 · 색 · 그림자 볼륨을 선언으로 다룹니다.
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"

#include "Engine/Object/Component/3D/LightComponent.h"
#include "Engine/Reflection/ReflectionMacros.h"

namespace sw
{
    /**
     * @brief 방향광 그림자 투영 하나입니다 — 행렬과, 바이어스를 월드 단위로 환산하는 데 필요한 볼륨 크기입니다.
     * @details 바이어스를 NDC 상수로 두면 볼륨이 커질수록 월드 길이도 커진다 — 깊이 360 m 볼륨에서 0.02 는 7.2 m 라, 받는 면 위 2.5 m 미만의
     *          물체(벤치 · 레일)는 그림자가 통째로 사라지고 나무 그림자는 밑동에서 떨어진다. 그래서 바이어스는 텍셀 수로 정하고 이 크기로 환산한다
     *          (유니티 URP 의 Depth · Normal Bias, 언리얼 CSM 의 캐스케이드별 바이어스와 같은 자리).
     */
    struct SW_API DirectionalShadowProjection
    {
        float4x4 _viewProj{};
        float32  _texelWorldSize{ 0.0f }; ///< 그림자 맵 텍셀 하나가 덮는 월드 길이(m) — 가로 · 세로 중 큰 쪽
        float32  _depthRange{ 1.0f };     ///< 깊이 구간 길이(m) — far - near
        uint32   _resolution{ 1 };        ///< 그림자 맵 한 변의 텍셀 수

        /** @brief 셰이더의 `g_ShadowParams` 입니다 — x 깊이 바이어스(NDC) · y 그늘 세기 · z 노멀 오프셋(m) · w 텍셀 하나의 UV 폭. */
        float4 computeShaderParams() const;
    };
} // namespace sw

namespace sw
{
    /**
     * @class DirectionalLightComponent
     * @brief 방향광 하나입니다. 빛 방향은 이 컴포넌트의 월드 트랜스폼에서 나옵니다.
     * @details 주광의 방향 · 색 · 세기와 그림자 직교 볼륨(`_shadowExtent`)을 씬이 정합니다. 카메라가 CameraComponent 로
     *          선언되듯 빛도 컴포넌트로 선언합니다.
     * @note 방향은 기본 방향(위에서 비스듬히)을 이 컴포넌트의 월드 회전으로 돌린 것입니다(`LightComponent` 의 방향 규약).
     *       회전이 없는 루트면 기본 방향 그대로이고, 부모가 돌면 따라 돕니다.
     */
    REFLECT( Category = "Rendering 3D", DisplayName = "Directional Light", Tooltip = "Scene key light and shadow volume" )
    class SW_API DirectionalLightComponent : public LightComponent
    {
    public:
        REFLECT_BODY();

        /** @brief 기본 주광 값으로 만듭니다. */
        DirectionalLightComponent();
        /** @brief 기본 소멸자입니다. */
        virtual ~DirectionalLightComponent() override = default;

        /** @brief 빛이 나아가는 방향(정규화)입니다. 기본 방향(위에서 비스듬히)을 월드 회전으로 돌린 것입니다. */
        float3 getLightDirection() const;

        /** @brief 환경광 세기입니다. */
        float32 getAmbient() const { return _ambient; }
        /** @brief 환경광 세기를 설정합니다. */
        void setAmbient( float32 ambient );

        /** @brief 그림자 직교 볼륨의 한 변 절반입니다. 씬을 덮을 만큼 커야 합니다. */
        float32 getShadowExtent() const { return _shadowExtent; }
        /** @brief 그림자 직교 볼륨 크기를 설정합니다. */
        void setShadowExtent( float32 extent );

        /** @brief 그림자 카메라를 원점에서 얼마나 뒤로 뺄지입니다. */
        float32 getShadowDistance() const { return _shadowDistance; }
        /** @brief 그림자 카메라 거리를 설정합니다. */
        void setShadowDistance( float32 distance );

        /** @brief 그림자를 드리우면 true 입니다. */
        bool castsShadow() const { return _bCastShadow == SW_TRUE; }
        /** @brief 그림자 사용 여부를 설정합니다. */
        void setCastShadow( bool bCastShadow );

        /** @brief 이 라이트의 그림자 view-projection 행렬을 만듭니다. */
        float4x4 buildShadowViewProj() const;
        /** @brief 고정 볼륨(원점 둘레 반경 `_shadowExtent`)의 그림자 투영입니다. `shadowMapResolution` 은 그림자 맵 한 변의 텍셀 수입니다. */
        DirectionalShadowProjection buildShadowProjection( uint32 shadowMapResolution ) const;

    protected:
        /** @brief 방향(`_directionType.xyz`)을 씁니다. */
        void writeGpuLightKindFields( GpuLight& outLight ) const override;

    private:
        PROPERTY( Category = "Light", DisplayName = "Ambient", Min = 0.0, Tooltip = "Ambient term" )
        float32 _ambient;
        PROPERTY( Category = "Shadow", DisplayName = "Shadow Extent", Min = 0.0, Tooltip = "Half size of the shadow ortho volume", Units = m )
        float32 _shadowExtent;
        PROPERTY( Category = "Shadow", DisplayName = "Shadow Distance", Min = 0.0, Tooltip = "Shadow camera pullback along the light direction", Units = m )
        float32 _shadowDistance;
        PROPERTY( Category = "Shadow", DisplayName = "Cast Shadow", Tooltip = "Render this light into the shadow map" )
        uint8                  _bCastShadow   : 1;
        [[maybe_unused]] uint8 _reservedLight : 7;
    };
} // namespace sw
