#include "pch.h"

#include "Core/Math/MathUtil.h"
#include "Core/Math/MatrixMath.h"

#include "Engine/Environment/Sky/SkyGradient.h"
#include "Engine/Graphics/RHI/IRHIDevice.h"
#include "Engine/Graphics/RHI/IRHIResourceFactory.h"
#include "Engine/Graphics/Shader/Binding/ShaderBindingSlots.h"
#include "Engine/Object/Component/3D/DirectionalLightComponent.h"
#include "Engine/Renderer/Frame/FrameRenderer.h"
#include "Engine/Renderer/Frame/FrameRendererUtil.h"

namespace sw
{
    namespace
    {
        constexpr float4 kDefaultBloomParams{ 0.55f, 0.65f, 0.25f, 0.0f };
        constexpr float4 kDefaultOutlineColor{ 0.08f, 0.05f, 0.12f, 0.85f };
        /// 피사계 심도 착란원 반지름의 상한(px)입니다. depthoffield.hlsl 의 탭 16 개가 이 반지름 안에 퍼지므로, 더 키우면 탭 사이가 벌어져 고리 무늬가 보인다.
        constexpr float32 kMaxDepthOfFieldRadiusPixels = 8.0f;
        /// 센서 높이(mm, `CameraDepthOfFieldValues::_sensorHeight`)를 m 로 바꾸는 비율입니다.
        constexpr float32 kMillimetersPerMeter = 1000.0f;
    } // namespace

    void FrameRenderer::applyFrameSky( bool bHasSky, const SkyGradient& sky )
    {
        // 셰이더 상수 배치는 SkyGradient 머리 주석이 정본이다(sky.hlsl 의 evaluateSky 가 같은 순서로 읽는다).
        _bHasFrameSky = bHasSky ? SW_TRUE : SW_FALSE;
        if ( bHasSky == false )
        {
            _frameSky = FrameSkyState{};
            return;
        }
        _frameSky._zenithExponent  = float4{ sky._zenithColor._x, sky._zenithColor._y, sky._zenithColor._z, sky._horizonExponent };
        _frameSky._horizon         = float4{ sky._horizonColor._x, sky._horizonColor._y, sky._horizonColor._z, 0.0f };
        _frameSky._groundIntensity = float4{ sky._groundColor._x, sky._groundColor._y, sky._groundColor._z, sky._intensity };
    }

    void FrameRenderer::applyFrameShadow()
    {
        // 게임 스레드가 배치를 만들지 않은 프레임(씬 없음)이거나 그림자 품질이 그 사이 바뀌었으면 아틀라스 레이아웃을 지금 크기로 다시 만든다.
        const uint32 atlasResolution = getShadowMapResolution();
        if ( _frameShadow._layout.getAtlasResolution() != atlasResolution )
            _frameShadow._layout = ShadowAtlasLayout::make( atlasResolution, _frameShadow._layout.getCascadeCount(), static_cast<uint32>( _frameShadow._listLocalTile.size() ) );
        // 게임 스레드가 그림자 배치를 만들지 않은 프레임(씬 없음)은 그림자 없는 뷰다. 빛이 없는데 그림자 볼륨을 지어내지 않는다.
        if ( _frameShadow._mainView._bValid == SW_FALSE )
            _frameShadow._mainView = ShadowSetupUtil::makeUnshadowedView( _frameShadow._layout );
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

    void FrameRenderer::updatePassConstants( FramePassContext& context, const float4x4& mainViewProj )
    {
        // **프레임당 한 번** 프레임 시드(_frameContext)에만 채운다. 패스 컨텍스트는 이 시드를 복사해
        // 가므로(onGraphPassExecute) 패스마다 다시 계산할 필요가 없고, 드로우마다는 더더욱 없다.
        //
        // 주 시점 뷰-투영은 호출자가 카메라에서 만들어 넘긴다(씬 직접 경로는 씬의 게임 카메라, 패킷 경로는 패킷의 주 시점).
        // 그림자는 `applyFrameShadow` 가 준비한 주 시점의 것이다(그림자 빛이 없으면 그림자 없는 뷰).
        applyViewShadowConstants( context, _mainView._shadow );
        view( RenderViewType::Shadow ).setViewProjection( _mainView._shadow._cullViewProj );

        applyViewProjection( context, mainViewProj );
        context._passValues.setMatrix( passConstantNames()._world, context._world );

        context._passValues.setFloat4( passConstantNames()._keyLightDirIntensity, _frameLight._dirIntensity );
        context._passValues.setFloat4( passConstantNames()._keyLightColor, _frameLight._colorAmbient );
        context._passValues.setFloat4( passConstantNames()._skyZenith, _frameSky._zenithExponent );
        context._passValues.setFloat4( passConstantNames()._skyHorizon, _frameSky._horizon );
        context._passValues.setFloat4( passConstantNames()._skyGround, _frameSky._groundIntensity );
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
        // 원본 텍셀의 기본값은 프레임 텍셀이다. 원본 역할 입력을 바인딩하는 패스가 그 첨부의 실제 크기로 덮는다(registerPassTexture).
        context._passValues.setFloat4( passConstantNames()._sourceTexel,
                                       float4{ outlineY, outlineZ, static_cast<float32>( pool.getWidth() ), static_cast<float32>( pool.getHeight() ) } );
        // 카메라별 후처리 값(CameraPostProcessValues)도 뷰마다다. 중립 값(노출 0, 배율 1, 비네트 · 레터박스 0)이면 셰이더 결과가 전역 값과 같다.
        const CameraPostProcessValues& post = _pActiveView->_settings._postProcess;
        float4                         bloomParams{ kDefaultBloomParams };
        bloomParams._y *= MathUtil::max( 0.0f, post._bloomScale );
        context._passValues.setFloat4( passConstantNames()._bloomParams, bloomParams );
        context._passValues.setFloat4( passConstantNames()._cameraGrade, float4{ MathUtil::pow( 2.0f, post._exposureCompensation ), MathUtil::max( 0.0f, post._saturation ),
                                                                                 MathUtil::max( 0.0f, post._contrast ), MathUtil::saturate( post._vignetteIntensity ) } );
        const float32 viewAspect = pool.getHeight() > 0 ? static_cast<float32>( pool.getWidth() ) / static_cast<float32>( pool.getHeight() ) : 0.0f;
        // 색각 보정은 화면 · 호스트 출력 뷰만 건다. 렌더 텍스처는 주 화면에 그려진 뒤 그 뷰의 톤맵에서 보정된다(두 번 걸지 않는다).
        const bool    bScreenOutput   = _pActiveView->_pOutputTexture == nullptr;
        const float32 colorVisionMode = bScreenOutput ? static_cast<float32>( _canvasFrame._colorVisionMode ) : 0.0f;
        context._passValues.setFloat4( passConstantNames()._cameraFrame, float4{ MathUtil::max( 0.0f, post._letterboxAspectRatio ), viewAspect, colorVisionMode, 0.0f } );
        // 피사계 심도(depthoffield.hlsl) — 얇은 렌즈의 착란원. 지름 / 센서 높이 = 센서 높이 · s² / ( 4 · N · 초점 거리 ) · |1 - 초점 거리 / 깊이|
        // (s = 1 / tan( fovY / 2 ) 라 초점 길이 = 센서 높이 · s / 2, 구경 = 초점 길이 / N, 초점 길이가 초점 거리보다 훨씬 짧다고 본다). y 는 그 반지름을 화면 높이 px 로 바꾼 배율이다.
        // 조리개 0(핀홀) · 직교 · 후처리를 끈 뷰는 끈다(w = 0) — 셰이더가 원본을 고른다.
        const CameraDepthOfFieldValues& lens          = _pActiveView->_settings._depthOfField;
        const float32                   scaleY        = _pActiveView->_settings._projectionScaleY;
        const bool                      bDepthOfField = _pActiveView->_settings.usesPostProcess() && lens.hasDepthOfField() && scaleY > 0.0f;
        float32                         cocScale      = 0.0f;
        if ( bDepthOfField )
        {
            const float32 sensorHeightMeters = lens._sensorHeight / kMillimetersPerMeter;
            cocScale                         = 0.5f * static_cast<float32>( pool.getHeight() ) * sensorHeightMeters * scaleY * scaleY / ( 4.0f * lens._aperture * lens._focusDistance );
        }
        context._passValues.setFloat4( passConstantNames()._cameraLens,
                                       float4{ lens._focusDistance, cocScale, kMaxDepthOfFieldRadiusPixels, bDepthOfField ? 1.0f : 0.0f } );
        // 패스 플래그: 비트는 bindingslots.hlsli 의 SW_PASS_FLAG_*(C++ 는 shaderslot::kPassFlag*)가 기준이다. 후처리는 뷰마다 끌 수 있다(CCTV).
        uint32 flags = ( _pDevice != nullptr && _pDevice->supportsNativeBindlessSampling() ) ? shaderslot::kPassFlagNativeBindless : 0u;
        if ( _pActiveView->_settings.usesPostProcess() == false )
            flags |= shaderslot::kPassFlagSkipPost;
        if ( _pActiveView->_settings.usesTonemap() == false )
            flags |= shaderslot::kPassFlagSkipTonemap;
        if ( _pActiveView->_settings._upscaleZoom > 0 )
            flags |= shaderslot::kPassFlagPointUpscale;
        context._passValues.setUint( passConstantNames()._flags, flags );
    }

    void FrameRenderer::applyViewProjection( FramePassContext& context, const float4x4& viewProj )
    {
        context._passValues.setMatrix( passConstantNames()._viewProj, viewProj );
        // 역행렬은 **여기서만** 만든다. 디퍼드 조명이 깊이에서 월드 위치를 복원하는 데 쓰는데,
        // 뷰와 따로 채우면 언젠가 한쪽만 갱신되고, 그 증상은 "빛이 한 프레임 늦게 따라온다"이다.
        context._passValues.setMatrix( passConstantNames()._invViewProj, viewProj.invert() );
        // 컬링은 기록 시작 전에 도는데 그때는 상수 버퍼에서 도로 꺼낼 수 없다. 지금 그리는 뷰에 같은 값을 남긴다.
        activeCullInput().setViewProjection( viewProj );

        // 모션 블러의 지난 뷰-투영 — 뷰마다 프레임에 한 번 이 함수를 지난다. 처음 그리는 뷰 · 다른 카메라가 쓰던 칸 · 컷 프레임은 이번 것으로 둬 속도가 0 이다.
        ViewTarget& view          = *_pActiveView;
        const bool  bFreshHistory = view._bHasPrevViewProj == SW_FALSE || view._prevViewID != view._viewID || view._settings._bCut == SW_TRUE;
        context._passValues.setMatrix( passConstantNames()._prevViewProj, bFreshHistory ? viewProj : view._prevViewProj );
        view._prevViewProj     = viewProj;
        view._prevViewID       = view._viewID;
        view._bHasPrevViewProj = SW_TRUE;
    }

    void FrameRenderer::setIdentityWorld( FramePassContext& context )
    {
        context._world = float4x4::Identity;
    }
} // namespace sw
