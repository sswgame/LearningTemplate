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
    } // namespace

    void FrameRenderer::applyFrameShadow()
    {
        // 게임 스레드가 배치를 만들지 않은 프레임(씬 없음)이거나 그림자 품질이 그 사이 바뀌었으면 아틀라스 배치를 지금 크기로 다시 세운다.
        const uint32 atlasResolution = getShadowMapResolution();
        if ( _frameShadow._layout.getAtlasResolution() != atlasResolution )
            _frameShadow._layout = ShadowAtlasLayout::make( atlasResolution, _frameShadow._layout.getCascadeCount(), static_cast<uint32>( _frameShadow._listLocalTile.size() ) );
        // 주광이 없으면 프레임 키 라이트(기본 방향)의 작은 고정 볼륨으로 폴백한다 — 라이트 컴포넌트가 없는 씬도 그림자가 진다.
        if ( _frameShadow._mainView._bValid == SW_FALSE )
        {
            const float4& dirIntensity = _frameLight._dirIntensity;
            _frameShadow._mainView     = ShadowSetupUtil::makeFallbackShadow( float3{ dirIntensity._x, dirIntensity._y, dirIntensity._z }, _frameShadow );
        }
        _mainView._shadow = _frameShadow._mainView;
    }

    void FrameRenderer::applyViewShadowConstants( FramePassContext& context, const ViewShadowData& shadow )
    {
        const PassConstantNames& names = passConstantNames();
        context._passValues.setMatrix( names._lightViewProj, shadow._lightViewProj );
        context._passValues.setFloat4( names._shadowParams, shadow._shadowParams );
        context._passValues.setFloat4( names._shadowCascadeLayout, shadow._cascadeLayout );
        for ( uint32 cascadeIndex = 0; cascadeIndex < shaderslot::kShadowCascadeMax; ++cascadeIndex )
        {
            context._passValues.setFloat4( names._arrShadowCascade[cascadeIndex], shadow._arrCascade[cascadeIndex] );
        }
    }

    void FrameRenderer::updatePassConstants( FramePassContext& context )
    {
        // **프레임당 한 번** 프레임 시드(_frameContext)에만 채운다. 패스 컨텍스트는 이 시드를 복사해
        // 가므로(onGraphPassExecute) 패스마다 다시 계산할 필요가 없고, 드로우마다는 더더욱 없다.
        //
        // 뷰 행렬은 씬이 없으면(렌더 스레드 패킷 경로) 폴백으로 세운다. 패킷이 자기 뷰 행렬을 갖고 있으면 executePacket 이
        // 그 위에 덮어쓴다. 그림자는 `applyFrameShadow` 가 마련한 주 시점의 것이다(주광이 없으면 키 라이트 폴백).
        applyViewShadowConstants( context, _mainView._shadow );
        view( RenderViewType::Shadow ).setViewProjection( _mainView._shadow._cullViewProj );

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
        // 패스 플래그 — 비트는 bindingslots.hlsli 의 SW_PASS_FLAG_*(C++ 는 shaderslot::kPassFlag*)가 원본이다. 후처리는 뷰마다 끌 수 있다(CCTV).
        uint32 flags = ( _pDevice != nullptr && _pDevice->supportsNativeBindlessSampling() ) ? shaderslot::kPassFlagNativeBindless : 0u;
        if ( _pActiveView->_settings.usesPostProcess() == false )
            flags |= shaderslot::kPassFlagSkipPost;
        if ( _pActiveView->_settings.usesTonemap() == false )
            flags |= shaderslot::kPassFlagSkipTonemap;
        if ( _pActiveView->_settings._upscaleZoom > 0 )
            flags |= shaderslot::kPassFlagPointUpscale;
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
