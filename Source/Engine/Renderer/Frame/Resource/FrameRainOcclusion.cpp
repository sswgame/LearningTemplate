#include "pch.h"

#include "Engine/Renderer/Frame/Resource/FrameRainOcclusion.h"

#include "Core/Log/LogOnce.h"

#include "Engine/Graphics/RHI/IRHIDevice.h"
#include "Engine/Graphics/RHI/RHIDescriptors.h"
#include "Engine/Renderer/Frame/FrameRendererUtil.h"
#include "Engine/Renderer/Pipeline/RenderPassKindInfo.h"
#include "Engine/Renderer/VFX/VFXGPUSimulation.h"

namespace sw
{
    FrameRainOcclusion::FrameRainOcclusion()
        : _depth{}
        , _particleDepth{}
        , _cullInput{}
        , _contentViewProj{}
        , _contentEye{}
        , _shaderParams{}
        , _cullSlot{ kMainCullSlot }
        , _bActive{ SW_FALSE }
        , _bDrawn{ SW_FALSE }
        , _bContentValid{ SW_FALSE }
    {
    }

    void FrameRainOcclusion::beginFrame( IRHIDevice* pDevice, const RainOcclusionSettings& settings, const float3& viewPosition, RHIFormat depthFormat, bool bNeeded, bool bParticles )
    {
        // 지난 프레임에 그린 맵이 이번 프레임 강수의 입력이다(그리지 않은 프레임이 끼면 낡은 맵이라 버린다).
        _bContentValid   = _bDrawn;
        _contentViewProj = _cullInput._viewProj;
        _contentEye      = _cullInput._position;
        _bDrawn          = SW_FALSE;
        _bActive         = SW_FALSE;
        _shaderParams    = float4{};
        if ( pDevice == nullptr || settings.isEnabled() == false )
        {
            release( pDevice ); // 씬 · 품질이 껐다 — 메모리도 없다
            return;
        }
        if ( bNeeded == false || depthFormat == RHIFormat::Unknown )
            return;

        if ( _depth._width != settings._resolution || _depth._format != depthFormat )
            _bContentValid = SW_FALSE; // 새 텍스처에는 아직 맵이 없다
        RHITextureDesc depthDesc{};
        depthDesc._width             = settings._resolution;
        depthDesc._height            = settings._resolution;
        depthDesc._format            = depthFormat;
        depthDesc._bIsDepthStencil   = SW_TRUE;
        depthDesc._bIsShaderResource = SW_TRUE;
        depthDesc._clearDepth        = 1.0f;
        const bool bDepthReady       = _depth.ensure( *pDevice, depthDesc, "RainOcclusion" );
        const bool bCullReady        = _cullInput._cullCb.isValid() || _cullInput._cullCb.create( pDevice, sizeof( FrameRendererUtil::GPUCullParams ) );
        if ( bDepthReady == false || bCullReady == false )
        {
            SW_LOG_ERROR_ONCE_ID( "FrameRainOcclusion.FailedCreate", "Failed to create the rain occlusion map (%#x%#) - covered surfaces get wet and GPU rain is not occluded", settings._resolution, settings._resolution );
            return;
        }
        if ( bParticles )
        {
            RHITextureDesc particleDesc{};
            particleDesc._width              = settings._resolution;
            particleDesc._height             = settings._resolution;
            particleDesc._format             = getRenderPassKindInfo( RenderPassKind::DepthHistory )._pColorFormat[0]; // 복사 PSO 와 같은 포맷 한 곳
            particleDesc._bIsRenderTarget    = SW_TRUE;
            particleDesc._bIsShaderResource  = SW_TRUE;
            particleDesc._bIsUnorderedAccess = SW_TRUE;
            if ( _particleDepth.ensure( *pDevice, particleDesc, "RainOcclusionParticles" ) == false )
                SW_LOG_ERROR_ONCE_ID( "FrameRainOcclusion.FailedCreateParticleDepth", "Failed to create the rain occlusion copy (R32 float RT + UAV) - GPU rain is not occluded" );
        }
        else
        {
            _particleDepth.release( pDevice );
        }

        const float3 eye     = settings.computeEye( viewPosition );
        _cullInput._position = eye;
        _cullInput.setViewProjection( settings.computeViewProjection( eye ) );
        _shaderParams = settings.makeShaderParams();
        _bActive      = SW_TRUE;
    }

    void FrameRainOcclusion::release( IRHIDevice* pDevice )
    {
        _depth.release( pDevice );
        _particleDepth.release( pDevice );
        _cullInput._cullCb.release( pDevice );
        _bActive       = SW_FALSE;
        _bDrawn        = SW_FALSE;
        _bContentValid = SW_FALSE;
    }

    VFXDepthHistoryInput FrameRainOcclusion::makeParticleInput() const
    {
        VFXDepthHistoryInput input{};
        input._texture  = _particleDepth._texture;
        input._uav      = _particleDepth._uav;
        input._width    = _particleDepth._width;
        input._height   = _particleDepth._height;
        input._viewProj = _contentViewProj;
        input._eye      = _contentEye;
        return input;
    }
} // namespace sw
