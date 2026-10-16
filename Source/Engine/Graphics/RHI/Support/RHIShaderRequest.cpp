#include "pch.h"

#include "Engine/Graphics/RHI/Support/RHIShaderRequest.h"

#include "Engine/Common/EngineServices.h"
#include "Engine/Graphics/RHI/RHITypes.h"
#include "Engine/Graphics/Shader/Compile/ShaderCache.h"
#include "Engine/Graphics/Shader/Reflection/ShaderReflection.h"

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

        // 뎁스만 쓰는 파이프라인(RT 0 개)도 PS 경로가 있으면 픽셀 스테이지를 둔다 — 알파 컷오프 머티리얼이 그림자 · 깊이 프리패스에서
        // 같은 모양으로 오리려면 픽셀에서 버려야 한다(`FrameRendererUtil::hasMaterialDepthPixelStage`). 컬러 출력이 없는 패스 PSO 는
        // FrameRenderer 가 PS 경로를 비운다(createPSOForPassType). 어느 쪽이든 서술체의 경로가 답이다.
        request._bDepthOnly      = ( desc._numRenderTargets == 0 && desc._bEnableDepthTest != 0 ) ? SW_TRUE : SW_FALSE;
        request._bHasPixelShader = desc._pixelShaderPath.empty() == false ? SW_TRUE : SW_FALSE;
        request._numRenderTargets =
            request._bDepthOnly != SW_FALSE ? 0u : ( desc._numRenderTargets > 0 ? desc._numRenderTargets : 1u );

        request._vertex._filePath     = desc._vertexShaderPath;
        request._vertex._entryPoint   = string( resolveEntryPoint( desc._vertexEntryPoint, ShaderStage::Vertex ) );
        request._vertex._stage        = ShaderStage::Vertex;
        request._vertex._targetFormat = targetFormat;

        if ( request._bHasPixelShader != SW_FALSE )
        {
            request._pixel._filePath     = desc._pixelShaderPath;
            request._pixel._entryPoint   = string( resolveEntryPoint( desc._pixelEntryPoint, ShaderStage::Pixel ) );
            request._pixel._stage        = ShaderStage::Pixel;
            request._pixel._targetFormat = targetFormat;
        }

        // define 은 두 스테이지에 같이 붙는다. 퍼뮤테이션은 파이프라인 단위다.
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
            return engine::getSubsystem<ShaderCache>().getOrCompile( desc );
        return ShaderCompiler::compileHlsl( desc );
    }

    bool RHIShaderRequest::compileGraphics( const RHIGraphicsShaderRequest& request, ShaderCompileResult& outVertex, ShaderCompileResult& outPixel )
    {
        outVertex = compile( request._vertex );
        outPixel  = ShaderCompileResult{};
        if ( request._bHasPixelShader != SW_FALSE )
            outPixel = compile( request._pixel );
        return outVertex._bSuccess && ( request._bHasPixelShader == SW_FALSE || outPixel._bSuccess );
    }

    uint32 RHIShaderRequest::computeConsumedVertexAttributeMask( const vector<uint8>& vertexBytecode, ShaderTargetFormat targetFormat )
    {
        static_assert( constant::kVertexAttributeCount <= 32u, "정점 속성 마스크가 uint32 를 넘는다" );
        const ShaderReflectionData reflection = ShaderReflection::reflect( vertexBytecode, targetFormat );
        uint32                     mask{ 0 };
        for ( uint32 attributeIndex = 0; attributeIndex < constant::kVertexAttributeCount; ++attributeIndex )
        {
            for ( const ShaderVertexInputInfo& input : reflection._listVertexInput )
            {
                if ( input._location != constant::arrVertexAttribute[attributeIndex]._location )
                    continue;
                mask |= ( 1u << attributeIndex );
                break;
            }
        }
        return mask;
    }
} // namespace sw
