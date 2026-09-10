#include "pch.h"

#include "Engine/Graphics/RHI/Support/RHIShaderRequest.h"

#include "Engine/Common/EngineServices.h"
#include "Engine/Graphics/Shader/Compile/ShaderBaker.h"
#include "Engine/Graphics/Shader/Compile/ShaderCache.h"

namespace sw
{
    RHIGraphicsShaderRequest::RHIGraphicsShaderRequest() noexcept
        : _vertex{}
        , _pixel{}
        , _numRenderTargets{ 1 }
        , _bHasPixelShader{ SW_FALSE }
        , _bDepthOnly{ SW_FALSE }
        , _reserved{ 0 }
    {
    }

    RHIGraphicsShaderRequest RHIShaderRequest::resolveGraphics( const RHIPipelineStateDesc& desc, ShaderTargetFormat targetFormat )
    {
        RHIGraphicsShaderRequest request;

        // 뎁스만 쓰는 파이프라인(RT 0 개)은 픽셀 스테이지가 없다. FrameRenderer 는 컬러 출력이 없는 패스의 PS 경로를
        // 이미 비우지만(createPsoForPassType), 서술체를 직접 만드는 호출자(테스트·툴)를 위해 여기서도 같은 판정을 한다 —
        // 예전엔 GL·Vulkan 만 이 판정을 하고 DX12 는 PS 를 붙여 백엔드마다 달랐다.
        request._bDepthOnly      = ( desc._numRenderTargets == 0 && desc._bEnableDepthTest != 0 ) ? SW_TRUE : SW_FALSE;
        request._bHasPixelShader = ( desc._pixelShaderPath.empty() == false && request._bDepthOnly == SW_FALSE ) ? SW_TRUE : SW_FALSE;
        request._numRenderTargets =
            request._bDepthOnly != SW_FALSE ? 0u : ( desc._numRenderTargets > 0 ? desc._numRenderTargets : 1u );

        request._vertex._filePath     = desc._vertexShaderPath;
        request._vertex._entryPoint   = desc._vertexEntryPoint.empty() ? string( ShaderBaker::getDefaultEntryPointForStage( ShaderStage::Vertex ) )
                                                                       : desc._vertexEntryPoint;
        request._vertex._stage        = ShaderStage::Vertex;
        request._vertex._targetFormat = targetFormat;

        if ( request._bHasPixelShader != SW_FALSE )
        {
            request._pixel._filePath     = desc._pixelShaderPath;
            request._pixel._entryPoint   = desc._pixelEntryPoint.empty() ? string( ShaderBaker::getDefaultEntryPointForStage( ShaderStage::Pixel ) )
                                                                         : desc._pixelEntryPoint;
            request._pixel._stage        = ShaderStage::Pixel;
            request._pixel._targetFormat = targetFormat;
        }

        // define 은 두 스테이지에 같이 붙는다 — 퍼뮤테이션은 파이프라인 단위다.
        for ( const string& define : desc._listShaderDefine )
        {
            const ShaderMacroDefine parsed = ShaderMacroDefine::parse( define );
            request._vertex._listDefine.push_back( parsed );
            if ( request._bHasPixelShader != SW_FALSE )
                request._pixel._listDefine.push_back( parsed );
        }
        return request;
    }

    ShaderCompileResult RHIShaderRequest::compile( const ShaderCompileDesc& desc )
    {
        if ( engine::areEngineServicesBound() )
            return engine::getShaderCache().getOrCompile( desc );
        return ShaderCompiler::compileHLSL( desc );
    }
} // namespace sw
