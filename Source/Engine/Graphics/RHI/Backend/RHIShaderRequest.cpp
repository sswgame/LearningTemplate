#include "pch.h"

#include "Engine/Graphics/RHI/Backend/RHIShaderRequest.h"

#include "Core/Container/StringUtil.h"

#include "Engine/Foundation/EngineServices.h"
#include "Engine/Graphics/RHI/RHITypes.h"
#include "Engine/Graphics/Shader/Compile/ShaderCache.h"
#include "Engine/Graphics/Shader/Reflection/ShaderReflection.h"

namespace sw
{
    RHIGraphicsShaderRequest::RHIGraphicsShaderRequest() noexcept
        : _vertex{}
        , _pixel{}
        , _geometry{}
        , _hull{}
        , _domain{}
        , _numRenderTargets{ 1 }
        , _bHasPixelShader{ SW_FALSE }
        , _bDepthOnly{ SW_FALSE }
        , _bHasGeometryShader{ SW_FALSE }
        , _bHasTessellation{ SW_FALSE }
        , _reserved{ 0 }
    {
    }

    RHIGraphicsShaderRequest RHIShaderRequest::resolveGraphics( const RHIPipelineStateDesc& desc, ShaderTargetFormat targetFormat )
    {
        RHIGraphicsShaderRequest request;

        // 뎁스만 쓰는 파이프라인(RT 0 개)도 PS 경로가 있으면 픽셀 스테이지를 둔다. 알파 컷오프 머티리얼이 그림자와 깊이 프리패스에서
        // 같은 형태로 오려지려면 픽셀에서 버려야 한다(`FrameRendererUtil::hasMaterialDepthPixelStage`). 컬러 출력이 없는 패스 PSO 는
        // FrameRenderer 가 PS 경로를 비운다(createPSOForPassType). 어느 쪽이든 디스크립터의 경로가 답이다.
        request._bDepthOnly      = ( desc._numRenderTargets == 0 && desc._renderState._bEnableDepthTest != 0 ) ? SW_TRUE : SW_FALSE;
        request._bHasPixelShader = desc._pixelShaderPath.empty() == false ? SW_TRUE : SW_FALSE;
        request._numRenderTargets =
            request._bDepthOnly != SW_FALSE ? 0u : ( desc._numRenderTargets > 0 ? desc._numRenderTargets : 1u );

        request._vertex._source       = ShaderSourceID{ desc._vertexShaderPath };
        request._vertex._entryPoint   = string( resolveEntryPoint( desc._vertexEntryPoint, ShaderStage::Vertex ) );
        request._vertex._stage        = ShaderStage::Vertex;
        request._vertex._targetFormat = targetFormat;

        if ( request._bHasPixelShader != SW_FALSE )
        {
            request._pixel._source       = ShaderSourceID{ desc._pixelShaderPath };
            request._pixel._entryPoint   = string( resolveEntryPoint( desc._pixelEntryPoint, ShaderStage::Pixel ) );
            request._pixel._stage        = ShaderStage::Pixel;
            request._pixel._targetFormat = targetFormat;
        }

        // 선택 스테이지의 소스는 정점 스테이지와 같은 파일이다.
        const auto fillOptional = [&]( ShaderCompileDesc& outStage, const string& entryPoint, ShaderStage stage )
        {
            outStage._source       = request._vertex._source;
            outStage._entryPoint   = entryPoint;
            outStage._stage        = stage;
            outStage._targetFormat = targetFormat;
        };
        request._bHasGeometryShader = desc._geometryEntryPoint.empty() ? SW_FALSE : SW_TRUE;
        request._bHasTessellation   = ( desc._hullEntryPoint.empty() || desc._domainEntryPoint.empty() ) ? SW_FALSE : SW_TRUE;
        if ( request._bHasGeometryShader != SW_FALSE )
            fillOptional( request._geometry, desc._geometryEntryPoint, ShaderStage::Geometry );
        if ( request._bHasTessellation != SW_FALSE )
        {
            fillOptional( request._hull, desc._hullEntryPoint, ShaderStage::Hull );
            fillOptional( request._domain, desc._domainEntryPoint, ShaderStage::Domain );
        }

        // define 은 모든 스테이지에 같이 붙는다. 퍼뮤테이션은 파이프라인 단위다.
        for ( const string& define : desc._listShaderDefine )
        {
            const ShaderMacroDefine parsed = ShaderMacroDefine::parse( define );
            request._vertex._listDefine.push_back( parsed );
            if ( request._bHasPixelShader != SW_FALSE )
                request._pixel._listDefine.push_back( parsed );
            if ( request._bHasGeometryShader != SW_FALSE )
                request._geometry._listDefine.push_back( parsed );
            if ( request._bHasTessellation != SW_FALSE )
            {
                request._hull._listDefine.push_back( parsed );
                request._domain._listDefine.push_back( parsed );
            }
        }
        return request;
    }

    void RHIShaderRequest::appendDefines( ShaderCompileDesc& inoutDesc, const vector<string>& listDefine )
    {
        for ( const string& define : listDefine )
        {
            inoutDesc._listDefine.push_back( ShaderMacroDefine::parse( define ) );
        }
    }

    ShaderCompileDesc RHIShaderRequest::resolveCompute( string_view shaderPath, string_view entryPoint, const vector<string>& listDefine, ShaderTargetFormat targetFormat )
    {
        ShaderCompileDesc desc{};
        desc._source       = ShaderSourceID{ shaderPath };
        desc._entryPoint   = string( resolveEntryPoint( entryPoint, ShaderStage::Compute ) );
        desc._stage        = ShaderStage::Compute;
        desc._targetFormat = targetFormat;
        appendDefines( desc, listDefine );
        return desc;
    }

    ShaderCompileResult RHIShaderRequest::compile( const ShaderCompileDesc& desc )
    {
        return engine::getSubsystem<ShaderCache>().getOrCompile( desc );
    }

    bool RHIShaderRequest::compileGraphics( const RHIGraphicsShaderRequest& request, ShaderCompileResult& outVertex, ShaderCompileResult& outPixel )
    {
        outVertex = compile( request._vertex );
        outPixel  = ShaderCompileResult{};
        if ( request._bHasPixelShader != SW_FALSE )
            outPixel = compile( request._pixel );
        return outVertex._bSuccess && ( request._bHasPixelShader == SW_FALSE || outPixel._bSuccess );
    }

    bool RHIShaderRequest::compileOptionalStages( const RHIGraphicsShaderRequest& request, ShaderCompileResult& outGeometry, ShaderCompileResult& outHull, ShaderCompileResult& outDomain )
    {
        bool bSuccess = true;
        if ( request._bHasGeometryShader != SW_FALSE )
        {
            outGeometry = compile( request._geometry );
            bSuccess    = bSuccess && outGeometry._bSuccess;
        }
        if ( request._bHasTessellation != SW_FALSE )
        {
            outHull   = compile( request._hull );
            outDomain = compile( request._domain );
            bSuccess  = bSuccess && outHull._bSuccess && outDomain._bSuccess;
        }
        return bSuccess;
    }

    uint32 RHIShaderRequest::computeConsumedVertexAttributeMask( const vector<uint8>& vertexBytecode, ShaderTargetFormat targetFormat )
    {
        static_assert( constant::render::kVertexAttributeCount <= 32u, "정점 속성 마스크가 uint32 를 넘는다" );
        const ShaderReflectionData reflection = ShaderReflection::reflect( vertexBytecode, targetFormat );
        uint32                     mask{ 0 };
        for ( uint32 attributeIndex = 0; attributeIndex < constant::render::kVertexAttributeCount; ++attributeIndex )
        {
            for ( const ShaderVertexInputInfo& input : reflection._listVertexInput )
            {
                // 시맨틱 이름으로 대조한다 — DX 리플렉션의 location(시그니처 레지스터)은 선언 순서라 위치만 받는 셰이더(VertexPositionInput)에서 표의 칸과 다르다.
                // 두 형식 표의 시맨틱은 같다. 시맨틱 인덱스는 표에 하나뿐이라(TEXCOORD0) 보지 않는다.
                if ( StringUtil::equals( input._semantic, constant::render::arrPackedMeshVertexAttribute[attributeIndex]._pSemanticName, true ) == false )
                    continue;
                mask |= ( 1u << attributeIndex );
                break;
            }
        }
        return mask;
    }

    uint32 RHIShaderRequest::toVertexSlotMask( uint32 attributeMask )
    {
        uint32 slotMask{ 0 };
        for ( uint32 attributeIndex = 0; attributeIndex < constant::render::kVertexAttributeCount; ++attributeIndex )
        {
            if ( ( attributeMask & ( 1u << attributeIndex ) ) != 0 )
                slotMask |= 1u << constant::render::arrPackedMeshVertexAttribute[attributeIndex]._inputSlot;
        }
        return slotMask;
    }
} // namespace sw
