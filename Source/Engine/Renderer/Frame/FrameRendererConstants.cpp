#include "pch.h"

#include "Core/Math/MatrixMath.h"

#include "Engine/Graphics/RHI/IRHIDevice.h"
#include "Engine/Graphics/RHI/IRHIResourceFactory.h"
#include "Engine/Graphics/Shader/Binding/ShaderBindingSlots.h"
#include "Engine/Object/Component/3D/DirectionalLightComponent.h"
#include "Engine/Object/Component/CameraComponent.h"
#include "Engine/Renderer/Frame/FrameRenderer.h"
#include "Engine/Renderer/Frame/FrameRendererUtil.h"

namespace sw
{
    namespace
    {
        constexpr float4 kDefaultBloomParams{ 0.55f, 0.65f, 0.25f, 0.0f };
        constexpr float4 kDefaultOutlineColor{ 0.08f, 0.05f, 0.12f, 0.85f };

        /// @brief 그림자용 라이트 카메라를 원점에서 얼마나 떨어뜨릴지입니다. 직교 깊이 범위도 이 값을 씁니다.
        constexpr float32 kLightDistance = 2.0f;
        /// @brief 라이트 직교 투영이 담는 가로 · 세로 범위입니다(약 2.222).
        constexpr float32 kLightOrthoExtent = 2.0f / 0.9f;

    } // namespace

    void FrameRenderer::updatePassConstants( FramePassContext& context )
    {
        // **프레임당 한 번** 프레임 시드(_frameContext)에만 채운다. 패스 컨텍스트는 이 시드를 복사해
        // 가므로(onGraphPassExecute) 패스마다 다시 계산할 필요가 없고, 드로우마다는 더더욱 없다.
        //
        // 뷰 · 라이트 행렬은 씬이 없으면(렌더 스레드 패킷 경로) 폴백으로 세운다. 패킷이 자기
        // 뷰 행렬을 갖고 있으면 executePacket 이 그 위에 덮어쓴다.
        float4x4 lightViewProj{};
        float4   shadowParams{};
        if ( _frameLight._bHasShadowViewProj != SW_FALSE )
        {
            lightViewProj = _frameLight._shadowViewProj;
            shadowParams  = _frameLight._shadowParams;
        }
        else
        {
            computeLightViewProj( context, lightViewProj );
            // 폴백 볼륨(한 변 kLightOrthoExtent · 깊이 같은 길이)의 크기로 바이어스를 환산한다 — 빛이 있는 경로와 같은 식이다.
            DirectionalShadowProjection fallback{};
            fallback._resolution     = getShadowMapResolution();
            fallback._texelWorldSize = kLightOrthoExtent / static_cast<float32>( fallback._resolution );
            fallback._depthRange     = kLightOrthoExtent;
            shadowParams             = fallback.computeShaderParams();
        }
        context._passValues.setMatrix( passConstantNames()._lightViewProj, lightViewProj );
        view( RenderViewType::Shadow ).setViewProjection( lightViewProj );

        CameraComponent* pCam = ( _pScene != nullptr ) ? _pScene->getActiveGameCamera() : nullptr;
        if ( pCam != nullptr )
        {
            applyViewFromCamera( context, pCam );
        }
        else
        {
            float4x4 viewProj = float4x4::Identity;
            computeViewProj( viewProj );
            applyViewProjection( context, viewProj );
        }
        context._passValues.setMatrix( passConstantNames()._world, context._world );

        context._passValues.setFloat4( passConstantNames()._keyLightDirIntensity, _frameLight._dirIntensity );
        context._passValues.setFloat4( passConstantNames()._keyLightColor, _frameLight._colorAmbient );
        context._passValues.setFloat4( passConstantNames()._shadowParams, shadowParams );
        context._passValues.setFloat4( passConstantNames()._bloomParams, kDefaultBloomParams );
        context._passValues.setFloat4( passConstantNames()._outlineColor, kDefaultOutlineColor );
        applyViewPassConstants( context );
    }

    void FrameRenderer::applyViewPassConstants( FramePassContext& context )
    {
        // 화면 텍셀 크기는 지금 그리는 뷰의 풀 크기다(뷰마다 해상도가 다르다).
        const TransientAttachmentPool& pool     = activePool();
        const float32                  outlineY = pool.getWidth() > 0 ? ( 1.0f / static_cast<float32>( pool.getWidth() ) ) : 0.001f;
        const float32                  outlineZ = pool.getHeight() > 0 ? ( 1.0f / static_cast<float32>( pool.getHeight() ) ) : 0.001f;
        context._passValues.setFloat4( passConstantNames()._outlineParams, float4{ 0.02f, outlineY, outlineZ, 0.0f } );
        // 원본 텍셀의 기본값은 프레임 텍셀이다 — 원본 역할 입력을 거는 패스가 그 첨부의 실제 크기로 덮는다(registerPassTexture).
        context._passValues.setFloat4( passConstantNames()._sourceTexel,
                                       float4{ outlineY, outlineZ, static_cast<float32>( pool.getWidth() ), static_cast<float32>( pool.getHeight() ) } );
        // 패스 플래그 — 비트는 bindingslots.hlsli 의 SW_PASS_FLAG_*(C++ 는 shaderslot::kPassFlag*)가 정본이다. 후처리는 뷰마다 끌 수 있다(CCTV).
        uint32 flags = ( _pDevice != nullptr && _pDevice->supportsNativeBindlessSampling() ) ? shaderslot::kPassFlagNativeBindless : 0u;
        if ( _pActiveView->_settings._bPostProcess == SW_FALSE )
            flags |= shaderslot::kPassFlagSkipPost;
        context._passValues.setUint( passConstantNames()._flags, flags );
    }

    void FrameRenderer::applyViewFromCamera( FramePassContext& context, CameraComponent* pCamera )
    {
        if ( pCamera == nullptr )
            return;
        const TransientAttachmentPool& pool     = activePool();
        const float32                  aspect   = ( pool.getHeight() > 0 ) ? ( static_cast<float32>( pool.getWidth() ) / static_cast<float32>( pool.getHeight() ) )
                                                                           : ( 16.0f / 9.0f );
        const float4x4                 viewProj = pCamera->getViewProjectionMatrix( aspect );
        applyViewProjection( context, viewProj );
    }

    void FrameRenderer::applyViewProjection( FramePassContext& context, const float4x4& viewProj )
    {
        context._passValues.setMatrix( passConstantNames()._viewProj, viewProj );
        // 역행렬은 **여기서만** 만든다. 디퍼드 조명이 깊이에서 월드 위치를 복원하는 데 쓰는데,
        // 뷰와 따로 채우면 언젠가 한쪽만 갱신되고 그 증상은 "빛이 한 프레임 늦게 따라온다" 다.
        context._passValues.setMatrix( passConstantNames()._invViewProj, viewProj.invert() );
        // 컬링은 기록 시작 전에 도는데 그때는 상수버퍼에서 도로 꺼낼 수 없다. 지금 그리는 뷰에 같은 값을 남긴다.
        activeCullInput().setViewProjection( viewProj );
    }

    void FrameRenderer::computeLightViewProj( const FramePassContext& context, float4x4& outMat ) const
    {
        (void)context;
        // **이번 프레임의 라이트**를 쓴다. 주의: 여기서 따로 둔 상수를 보면 패킷이 다른 방향을
        // 실어 줄 때 셰이딩(_frameLight 를 쓴다)과 그림자 행렬이 서로 다른 빛을 보게 된다. 기본값은
        // FrameLightState 의 멤버 초기값 하나뿐이다(값을 두 군데 두면 언젠가 갈라진다).
        const float4& dirIntensity = _frameLight._dirIntensity;
        float3        lightDir     = float3{ dirIntensity._x, dirIntensity._y, dirIntensity._z }.normalize();
        if ( lightDir.getLengthSquared() < MathUtil::kEpsilon )
            lightDir = float3{ 0.57735f, -0.57735f, 0.57735f };

        // 라이트를 원점 위(빛이 오는 쪽)에 두고 빛 방향을 따라 원점을 내려다본다. up 이 라이트와
        // 거의 나란하면 side 축이 사라지므로 다른 축으로 갈아탄다.
        const float3 up  = MathUtil::abs( lightDir._y ) > 0.99f ? float3::Forward : float3::Up;
        const float3 eye = lightDir * -kLightDistance;

        // 행렬은 makeLookAt · makeOrthographic 으로 만든다. view · ortho 성분을 손으로 쓰면 어떤 규약(좌수,
        // 행벡터)인지 읽어서 알아내야 하고, CameraComponent 가 쓰는 규약과 어긋나도 드러나지 않는다.
        // 깊이 범위는 **눈을 기준으로** 잡는다(DirectionalLightComponent::computeShadowViewProj 와 같은 규칙).
        constexpr float32 kLightOrthoHalf = kLightOrthoExtent * 0.5f;
        outMat                            = float4x4::makeLookAt( eye, float3::Zero, up ) *
                 float4x4::makeOrthographic( kLightOrthoExtent, kLightOrthoExtent,
                                             kLightDistance - kLightOrthoHalf, kLightDistance + kLightOrthoHalf );
    }

    void FrameRenderer::computeViewProj( float4x4& outMat ) const
    {
        // CameraComponent 가 없을 때만 쓰는 폴백 궤도 카메라다. 원점을 바라본다.
        // CameraComponent::getViewMatrix 와 같은 makeLookAt 으로 만든다. 주의: view 행렬을 직접 채우며 z축을
        // eye.normalize() 로 잡으면 원점을 등지고 보는 셈이라, 이 엔진이 쓰는 좌수 투영
        // (makePerspectiveFieldOfView, w' = z_view)에서는 원점이 뷰 z = -|eye| 로 카메라 뒤에
        // 떨어져 아무것도 그려지지 않는다.
        constexpr float3 eye{ 2.15f, 1.55f, 2.65f };

        const TransientAttachmentPool& pool   = activePool();
        const float32                  aspect = ( pool.getHeight() > 0 ) ? ( static_cast<float32>( pool.getWidth() ) / static_cast<float32>( pool.getHeight() ) )
                                                                         : ( 16.0f / 9.0f );

        outMat = float4x4::makeLookAt( eye, float3::Zero, float3::Up ) * float4x4::makePerspectiveFieldOfView( CameraComponent::kDefaultFovY, aspect, CameraComponent::kDefaultNearZ, CameraComponent::kDefaultFarZ );
    }

    void FrameRenderer::setIdentityWorld( FramePassContext& context )
    {
        context._world = float4x4::Identity;
    }
} // namespace sw
