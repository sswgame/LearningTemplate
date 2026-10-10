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
     *          선언되듯 빛도 컴포넌트로 선언합니다. 볼륨을 카메라가 보는 곳에 맞추려면 `_shadowViewDistance` 를 줍니다(`computeShadowProjectionForView`).
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
        float4x4 computeShadowViewProj() const;
        /** @brief 고정 볼륨(원점 둘레 반경 `_shadowExtent`)의 그림자 투영입니다. `shadowMapResolution` 은 그림자 맵 한 변의 텍셀 수입니다. */
        DirectionalShadowProjection computeShadowProjection( uint32 shadowMapResolution ) const;
        /**
         * @brief 카메라 뷰-투영에 맞춘 그림자 투영입니다. `_shadowViewDistance` 가 0 이면 고정 볼륨(`computeShadowProjection`)입니다.
         * @details 볼륨 = 카메라 절두체(가까운 면에서 `_shadowViewDistance` 까지) ∩ 받는 높이 띠(`_shadowReceiverMinHeight` ~ `_shadowReceiverMaxHeight`,
         *          두 값이 같으면 띠 없음)의 빛 공간 상자. 원점은 텍셀 격자에 스냅하고 크기는 2 m 단위로 올린다. 빛 쪽 깊이는 `_shadowDistance` 만큼
         *          당겨 화면 밖 · 띠 위의 가리는 물체를 담는다. 직교 탑다운 카메라는 절두체가 수백 m 깊이라 띠가 없으면 볼륨이 바닥 몇 배로 커진다.
         */
        DirectionalShadowProjection computeShadowProjectionForView( const float4x4& cameraViewProj, uint32 shadowMapResolution ) const;

        /** @brief 볼륨을 카메라에 맞출 때 카메라에서 잰 그림자 거리입니다. 0 이면 고정 볼륨입니다. */
        float32 getShadowViewDistance() const { return _shadowViewDistance; }
        /** @brief 그림자 거리를 설정합니다(0 = 고정 볼륨). */
        void setShadowViewDistance( float32 distance );
        /** @brief 그림자를 받는 높이 띠의 아래 끝입니다. */
        float32 getShadowReceiverMinHeight() const { return _shadowReceiverMinHeight; }
        /** @brief 그림자를 받는 높이 띠의 위 끝입니다. */
        float32 getShadowReceiverMaxHeight() const { return _shadowReceiverMaxHeight; }
        /** @brief 그림자를 받는 높이 띠를 설정합니다. 두 값이 같으면 띠로 자르지 않습니다. */
        void setShadowReceiverHeightRange( float32 minHeight, float32 maxHeight );

    protected:
        /** @brief 방향(`_directionType.xyz`)을 씁니다. */
        void writeGPULightKindFields( GPULight& outLight ) const override;

    private:
        PROPERTY( Category = "Light", DisplayName = "Ambient", Min = 0.0, Tooltip = "Ambient term" )
        float32 _ambient;
        PROPERTY( Category = "Shadow", DisplayName = "Shadow Extent", Min = 0.0, Tooltip = "Half size of the shadow ortho volume", Units = m )
        float32 _shadowExtent;
        PROPERTY( Category = "Shadow", DisplayName = "Shadow Distance", Min = 0.0, Tooltip = "Pullback toward the light: fixed volume camera distance, or how far casters beyond the fitted view are kept", Units = m )
        float32 _shadowDistance;
        PROPERTY( Category = "Shadow", DisplayName = "Shadow View Distance", Min = 0.0, Tooltip = "Fit the shadow volume to the camera view up to this distance (0 = fixed volume around the origin)", Units = m )
        float32 _shadowViewDistance;
        PROPERTY( Category = "Shadow", DisplayName = "Receiver Min Height", Tooltip = "Lowest world height that receives shadows when fitting to the view", Units = m )
        float32 _shadowReceiverMinHeight;
        PROPERTY( Category = "Shadow", DisplayName = "Receiver Max Height", Tooltip = "Highest world height that receives shadows when fitting to the view (equal to min = no band)", Units = m )
        float32 _shadowReceiverMaxHeight;
        PROPERTY( Category = "Shadow", DisplayName = "Cast Shadow", Tooltip = "Render this light into the shadow map" )
        uint8                  _bCastShadow   : 1;
        [[maybe_unused]] uint8 _reservedLight : 7;
    };
} // namespace sw
