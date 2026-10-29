#include "pch.h"

#include "Core/Math/MathUtil.h"
#include "Core/Math/MatrixMath.h"

#include "Engine/Environment/Fog/FogSettings.h"
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
        constexpr float4 kDefaultOutlineColor{ 0.08f, 0.05f, 0.12f, 0.85f };
    } // namespace

    void FrameRenderer::applyFrameSky( bool bHasSky, const SkyGradient& sky )
    {
        // 셰이더 상수 배치는 SkyGradient 머리 주석이 기준이다(sky.hlsl 의 evaluateSky 가 같은 순서로 읽는다).
        _state._bHasFrameSky   = bHasSky ? SW_TRUE : SW_FALSE;
        _state._frameSkySource = bHasSky ? sky : SkyGradient{};
        if ( bHasSky == false )
        {
            _state._frameSky = FrameSkyState{};
            return;
        }
        _state._frameSky._zenithExponent  = float4{ sky._zenithColor._x, sky._zenithColor._y, sky._zenithColor._z, sky._horizonExponent };
        _state._frameSky._horizon         = float4{ sky._horizonColor._x, sky._horizonColor._y, sky._horizonColor._z, 0.0f };
        _state._frameSky._groundIntensity = float4{ sky._groundColor._x, sky._groundColor._y, sky._groundColor._z, sky._intensity };

        // 대기 산란 — 셰이더가 해 방향 · 색을 키 라이트 상수에서 읽으므로 여기에는 대기의 값만 싣는다. 그러데이션 모드는 w = 0 이라 셰이더가 그 길을 고른다.
        const SkyAtmosphere& atmosphere      = sky._atmosphere;
        const float3         rayleigh        = atmosphere.computeRayleighBeta();
        _state._frameSky._atmosphereRayleigh = float4{ rayleigh._x, rayleigh._y, rayleigh._z, sky._mode == SkyMode::Atmosphere ? 1.0f : 0.0f };
        _state._frameSky._atmosphereMie      = float4{ atmosphere.computeMieBeta(), SkyAtmosphere::kMieScaleHeightKm, atmosphere._mieAnisotropy, atmosphere._sunIntensity };
        _state._frameSky._atmosphereGeometry = float4{ atmosphere._planetRadiusKm, atmosphere._atmosphereHeightKm, atmosphere._observerAltitudeKm, atmosphere._sunDiscAngle };

        // 구름 층 — 바람 방향은 라디안을 단위 벡터로 바꿔 싣는다.
        const SkyClouds& clouds      = sky._clouds;
        _state._frameSky._cloudShape = float4{ MathUtil::saturate( clouds._coverage ), clouds._altitudeKm, clouds._scale, MathUtil::saturate( clouds._opacity ) };
        _state._frameSky._cloudWind  = float4{ MathUtil::cos( clouds._windDirection ), MathUtil::sin( clouds._windDirection ), clouds._windSpeedKmPerSecond, clouds._softness };
        _state._frameSky._cloudColor = float4{ clouds._color._x, clouds._color._y, clouds._color._z, clouds._bEnabled == SW_TRUE ? 1.0f : 0.0f };
    }

    void FrameRenderer::applyFrameShadow()
    {
        // 게임 스레드가 배치를 만들지 않은 프레임(씬 없음)이거나 그림자 품질이 그 사이 바뀌었으면 아틀라스 레이아웃을 지금 크기로 다시 만든다.
        const uint32 atlasResolution = getShadowMapResolution();
        if ( _state._frameShadow._layout.getAtlasResolution() != atlasResolution )
            _state._frameShadow._layout = ShadowAtlasLayout::make( atlasResolution, _state._frameShadow._layout.getCascadeCount(), static_cast<uint32>( _state._frameShadow._listLocalTile.size() ) );
        // 게임 스레드가 그림자 배치를 만들지 않은 프레임(씬 없음)은 그림자 없는 뷰다. 빛이 없는데 그림자 볼륨을 지어내지 않는다.
        if ( _state._frameShadow._mainView._bValid == SW_FALSE )
            _state._frameShadow._mainView = ShadowSetupUtil::makeUnshadowedView( _state._frameShadow._layout );
        _state._mainView._shadow = _state._frameShadow._mainView;
    }

    void FrameRenderer::updatePassConstants( FramePassContext& context, const float4x4& mainViewProj )
    {
        // **프레임당 한 번** 프레임 시드(_state._frameContext)에만 채운다. 패스 컨텍스트는 이 시드를 복사해
        // 가므로(onGraphPassExecute) 패스마다 다시 계산할 필요가 없고, 드로우마다는 더더욱 없다.
        //
        // 주 시점 뷰-투영은 호출자가 카메라에서 만들어 넘긴다(씬 직접 경로는 씬의 게임 카메라, 패킷 경로는 패킷의 주 시점).
        // 그림자는 `applyFrameShadow` 가 준비한 주 시점의 것이다(그림자 빛이 없으면 그림자 없는 뷰).
        _passExecutor.applyViewShadowConstants( context, _state._mainView._shadow );
        _state.view( RenderViewType::Shadow ).setViewProjection( _state._mainView._shadow._cullViewProj );

        beginViewHistory( _state._mainView );
        // 지난 뷰-투영과 견주므로 applyViewProjection(지난 것을 이번 것으로 바꾼다) 앞이다.
        // 주 시점의 첨부만 렌더 타깃 목록(에디터 패널)에 싣는다 — 가리키는 곳이 바뀌거나 타깃을 만들고 놓았을 때만 다시 싣는다.
        const FrameViewResources::PassSelectionResult selection = _viewResources.applyViewPassSelection( _state._mainView, mainViewProj );
        if ( selection._bTargetsRepointed == SW_TRUE || selection._bTargetsChanged == SW_TRUE )
            publishRenderTargets();
        if ( selection._bTargetsChanged == SW_TRUE )
            reportRenderTargetMemory();
        _passExecutor.applyViewProjection( context, _state._mainView, mainViewProj );
        context._passValues.setMatrix( passConstantNames()._world, context._world );

        context._passValues.setFloat4( passConstantNames()._keyLightDirIntensity, _state._frameLight._dirIntensity );
        context._passValues.setFloat4( passConstantNames()._keyLightColor, _state._frameLight._colorAmbient );
        context._passValues.setFloat4( passConstantNames()._skyZenith, _state._frameSky._zenithExponent );
        context._passValues.setFloat4( passConstantNames()._skyHorizon, _state._frameSky._horizon );
        context._passValues.setFloat4( passConstantNames()._skyGround, _state._frameSky._groundIntensity );
        context._passValues.setFloat4( passConstantNames()._skyAtmosphereRayleigh, _state._frameSky._atmosphereRayleigh );
        context._passValues.setFloat4( passConstantNames()._skyAtmosphereMie, _state._frameSky._atmosphereMie );
        context._passValues.setFloat4( passConstantNames()._skyAtmosphereGeometry, _state._frameSky._atmosphereGeometry );
        context._passValues.setFloat4( passConstantNames()._skyCloudShape, _state._frameSky._cloudShape );
        context._passValues.setFloat4( passConstantNames()._skyCloudWind, _state._frameSky._cloudWind );
        context._passValues.setFloat4( passConstantNames()._skyCloudColor, _state._frameSky._cloudColor );
        context._passValues.setFloat4( passConstantNames()._fogColorDensity, _postProcess.getFrameFog()._colorDensity );
        context._passValues.setFloat4( passConstantNames()._fogHeightParams, _postProcess.getFrameFog()._heightParams );
        context._passValues.setFloat4( passConstantNames()._fogInscatter, _postProcess.getFrameFog()._inscatter );
        context._passValues.setFloat4( passConstantNames()._fogVolumeParams, _postProcess.getFrameFog()._volumeParams );
        context._passValues.setFloat4( passConstantNames()._outlineColor, kDefaultOutlineColor );
        _passExecutor.applyViewPassConstants( context, _state._mainView );
    }

    void FrameRenderer::beginViewHistory( FrameViewTarget& view )
    {
        view._bHistoryReset = ( view._bHasHistory == SW_FALSE || view._settings._bCut == SW_TRUE ) ? SW_TRUE : SW_FALSE;
        view._bHasHistory   = SW_TRUE;
        // TAA 기록은 TAA 가 도는 프레임만 쓴다. 지난 프레임이 쓰지 않았으면 기록은 그 앞 화면이라 버린다 — 뷰 상태(후처리 켬)가 바뀐 첫 프레임이다.
        // 지난 프레임에 TAA 가 돌았는지는 그 프레임의 패스 고르기(`applyViewPassSelection`)가 남긴 `_bTAAHistoryWritten` 하나다 — 실행도 같은 결과를 본다.
        // 후처리 뒤의 효과(피사계 심도 · 모션 블러)가 켜지고 꺼지는 것은 TAA 뒤라 기록과 무관하다.
        view._bTAAHistoryReset = ( view._bHistoryReset == SW_TRUE || view._bTAAHistoryWritten == SW_FALSE ) ? SW_TRUE : SW_FALSE;
    }

} // namespace sw
