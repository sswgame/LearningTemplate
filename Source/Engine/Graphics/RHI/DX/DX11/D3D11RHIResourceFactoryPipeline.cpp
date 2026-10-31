/**
 * @file D3D11RHIResourceFactoryPipeline.cpp
 * @brief DirectX 11 의 파이프라인 상태 객체(PSO, 셰이더 스테이지, 렌더 패스 객체)입니다.
 * @details `D3D11RHIResourceFactory` 의 일부입니다. 리소스(버퍼, 텍스처)를 만드는 것과 파이프라인을 만드는 것은
 *          배우는 내용이 다르고 백엔드별 차이도 가장 크게 드러나는 곳이라 따로 둡니다.
 */
#include "pch.h"

#include "Core/Math/MathUtil.h"

#include "Engine/Foundation/EngineServices.h"
#include "Engine/Graphics/RHI/Backend/RHIShaderRequest.h"
#include "Engine/Graphics/RHI/DX/DX11/D3D11RHIDevice.h"
#include "Engine/Graphics/RHI/DX/DX11/D3D11RHIResourceFactory.h"
#include "Engine/Graphics/RHI/DX/RHIDxgiFormat.h"
#include "Engine/Graphics/Shader/Compile/ShaderCache.h"
#include "Engine/Graphics/Shader/Compile/ShaderCompiler.h"

#if defined( SW_PLATFORM_WINDOWS )
namespace sw
{
    SW_LOG_CALLER( "D3D11" );

    RHIPipelineStateHandle D3D11RHIResourceFactory::createPipelineState( const RHIPipelineStateDesc& desc )
    {
        // 디스크립터 해석(진입점 기본값, define, 뎁스 전용 판정)은 RHIShaderRequest 하나가 한다. 백엔드는 받기만 한다.
        // (그래픽스 요청에는 컴퓨트 스테이지가 없다. 컴퓨트 경로는 아래에서 define 을 직접 푼다.)
        const RHIGraphicsShaderRequest request = RHIShaderRequest::resolveGraphics( desc, ShaderTargetFormat::DXBC_D3D11 );

        D3D11RHIDevice::D3D11PipelineStateRecord pso{};
        if ( desc._vertexShaderPath.empty() == false )
        {
            // 셰이더를 만들지 못하면 PSO 도 만들지 않는다(핸들 0). 다른 세 백엔드와 같은 규칙이다. 주의: 실패를 그릴 때 "PS 가 없으면
            // 건너뛴다" 로 삼키면 **원래 PS 가 없는** 깊이 전용 PSO(그림자, 깊이 프리패스)의 드로우까지 버려진다(bindGraphicsPipelineForDraw
            // 참고). 뎁스 전용(RT 0 개)이면 경로가 있어도 PS 를 붙이지 않는다.
            ShaderCompileResult vsResult{};
            ShaderCompileResult psResult{};
            const bool          bShaderCreationFailed = RHIShaderRequest::compileGraphics( request, vsResult, psResult ) == false ||
                                               isHrSuccess( _pDevice->_device->CreateVertexShader( vsResult._bytecode.data(), vsResult._bytecode.size(), nullptr, pso._vs.GetAddressOf() ), "CreateVertexShader" ) == false ||
                                               ( request._bHasPixelShader != SW_FALSE &&
                                                 isHrSuccess( _pDevice->_device->CreatePixelShader( psResult._bytecode.data(), psResult._bytecode.size(), nullptr, pso._ps.GetAddressOf() ), "CreatePixelShader" ) == false );
            if ( bShaderCreationFailed )
            {
                SW_LOG_WARNING( "createPipelineState: shader compile/create failed (vs=%# ps=%#) %#", vsResult._bSuccess, psResult._bSuccess,
                                desc._vertexShaderPath.c_str() );
                return 0;
            }
            // 입력 레이아웃은 **공용 테이블**(constant::arrVertexAttribute)에서 만든다. DX12, Vulkan, GL 과 같은 테이블이다.
            D3D11_INPUT_ELEMENT_DESC arrInputElement[constant::kVertexAttributeCount]{};
            for ( uint32 attributeIndex = 0; attributeIndex < constant::kVertexAttributeCount; ++attributeIndex )
            {
                const RHIVertexAttribute& attribute                  = constant::arrVertexAttribute[attributeIndex];
                arrInputElement[attributeIndex].SemanticName         = attribute._pSemanticName;
                arrInputElement[attributeIndex].Format               = toDxgiVertexFormat( attribute );
                arrInputElement[attributeIndex].AlignedByteOffset    = attribute._byteOffset;
                arrInputElement[attributeIndex].InputSlot            = attribute._inputSlot;
                arrInputElement[attributeIndex].InputSlotClass       = ( attribute._bPerInstance != SW_FALSE ) ? D3D11_INPUT_PER_INSTANCE_DATA : D3D11_INPUT_PER_VERTEX_DATA;
                arrInputElement[attributeIndex].InstanceDataStepRate = ( attribute._bPerInstance != SW_FALSE ) ? 1u : 0u;
            }
            if ( isHrSuccess( _pDevice->_device->CreateInputLayout( arrInputElement, constant::kVertexAttributeCount, vsResult._bytecode.data(), vsResult._bytecode.size(), pso._inputLayout.GetAddressOf() ), "CreateInputLayout" ) == false )
            {
                SW_LOG_ERROR( "createPipelineState: CreateInputLayout failed '%#'", desc._vertexShaderPath.c_str() );
                return 0;
            }
        }
        if ( desc._computeShaderPath.empty() == false )
        {
            ShaderCompileDesc csDesc{};
            csDesc._source       = ShaderSourceID{ desc._computeShaderPath };
            csDesc._entryPoint   = string( resolveEntryPoint( desc._computeEntryPoint, ShaderStage::Compute ) );
            csDesc._stage        = ShaderStage::Compute;
            csDesc._targetFormat = ShaderTargetFormat::DXBC_D3D11;
            for ( const string& define : desc._listShaderDefine )
            {
                csDesc._listDefine.push_back( ShaderMacroDefine::parse( define ) );
            }
            ShaderCompileResult res = RHIShaderRequest::compile( csDesc );
            if ( res._bSuccess == false || isHrSuccess( _pDevice->_device->CreateComputeShader( res._bytecode.data(), res._bytecode.size(), nullptr, pso._cs.GetAddressOf() ), "CreateComputeShader" ) == false )
            {
                SW_LOG_ERROR( "createPipelineState: compute shader compile/create failed '%#'", desc._computeShaderPath.c_str() );
                return 0;
            }
        }

        D3D11_RASTERIZER_DESC rasterizerDesc{};
        rasterizerDesc.FillMode        = ( desc._fillMode == RHIFillMode::Wireframe ) ? D3D11_FILL_WIREFRAME : D3D11_FILL_SOLID;
        rasterizerDesc.CullMode        = ( desc._cullMode == RHICullMode::Front ) ? D3D11_CULL_FRONT : ( ( desc._cullMode == RHICullMode::Back ) ? D3D11_CULL_BACK : D3D11_CULL_NONE );
        rasterizerDesc.DepthClipEnable = TRUE;
        // 깊이 바이어스 — 다른 백엔드와 같은 상수 · 기울기 두 칸이다(RHIPipelineStateDesc::_depthBias).
        rasterizerDesc.DepthBias            = desc._depthBias;
        rasterizerDesc.SlopeScaledDepthBias = desc._slopeScaledDepthBias;
        // 가위는 항상 켠다. 뷰포트를 바인딩하는 곳(setViewport, beginRenderPass, beginFrame)이 가위도 뷰포트 전체로 함께 바인딩하므로
        // setScissorRect 를 부르지 않은 드로우는 지금과 같다(언리얼 D3D11 래스터라이저 상태와 같은 방식).
        rasterizerDesc.ScissorEnable = TRUE;
        // 상태 객체를 못 만들면 PSO 를 만들지 않는다(핸들 0). 빈 상태로 등록하면 앞 드로우의 래스터 · 블렌드 · 깊이 상태가 남아 그 상태로 그린다.
        if ( isHrSuccess( _pDevice->_device->CreateRasterizerState( &rasterizerDesc, pso._rasterizerState.GetAddressOf() ), "CreateRasterizerState" ) == false )
        {
            SW_LOG_ERROR( "createPipelineState: CreateRasterizerState failed '%#'", desc._vertexShaderPath.c_str() );
            return 0;
        }

        {
            D3D11_BLEND_DESC blendDesc{};
            blendDesc.RenderTarget[0].RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_ALL;
            if ( desc._bEnableBlend != 0 )
            {
                blendDesc.RenderTarget[0].BlendEnable    = TRUE;
                blendDesc.RenderTarget[0].SrcBlend       = ( desc._bPremultipliedAlpha != SW_FALSE ) ? D3D11_BLEND_ONE : D3D11_BLEND_SRC_ALPHA;
                blendDesc.RenderTarget[0].DestBlend      = D3D11_BLEND_INV_SRC_ALPHA;
                blendDesc.RenderTarget[0].BlendOp        = D3D11_BLEND_OP_ADD;
                blendDesc.RenderTarget[0].SrcBlendAlpha  = D3D11_BLEND_ONE;
                blendDesc.RenderTarget[0].DestBlendAlpha = D3D11_BLEND_INV_SRC_ALPHA;
                blendDesc.RenderTarget[0].BlendOpAlpha   = D3D11_BLEND_OP_ADD;
            }
            D3D11_DEPTH_STENCIL_DESC dsDesc{};
            dsDesc.DepthEnable      = ( desc._bEnableDepthTest != 0 ) ? TRUE : FALSE;
            dsDesc.DepthWriteMask   = ( desc._bEnableDepthWrite != 0 ) ? D3D11_DEPTH_WRITE_MASK_ALL : D3D11_DEPTH_WRITE_MASK_ZERO;
            dsDesc.DepthFunc        = D3D11_COMPARISON_LESS_EQUAL;
            const bool bStateFailed = isHrSuccess( _pDevice->_device->CreateBlendState( &blendDesc, pso._blendState.GetAddressOf() ), "CreateBlendState" ) == false ||
                                      isHrSuccess( _pDevice->_device->CreateDepthStencilState( &dsDesc, pso._depthStencilState.GetAddressOf() ), "CreateDepthStencilState" ) == false;
            if ( bStateFailed )
            {
                SW_LOG_ERROR( "createPipelineState: CreateBlendState/CreateDepthStencilState failed '%#'", desc._vertexShaderPath.c_str() );
                return 0;
            }
        }

        return _pDevice->_pipelineStates.insert( std::move( pso ) );
    }

    RHIPipelineStateHandle D3D11RHIResourceFactory::createComputePipelineState( string_view shaderPath, string_view entryPoint, const vector<string>& listDefine )
    {
        D3D11RHIDevice::D3D11PipelineStateRecord pso{};
        if ( shaderPath.empty() ) // 빈 레코드를 등록하지 않는다(다른 백엔드와 같다)
            return 0;
        {
            ShaderCompileDesc csDesc{};
            csDesc._source       = ShaderSourceID{ shaderPath };
            csDesc._entryPoint   = string( resolveEntryPoint( entryPoint, ShaderStage::Compute ) );
            csDesc._stage        = ShaderStage::Compute;
            csDesc._targetFormat = ShaderTargetFormat::DXBC_D3D11;
            RHIShaderRequest::appendDefines( csDesc, listDefine );
            ShaderCompileResult res = RHIShaderRequest::compile( csDesc );
            if ( res._bSuccess == false || isHrSuccess( _pDevice->_device->CreateComputeShader( res._bytecode.data(), res._bytecode.size(), nullptr, pso._cs.GetAddressOf() ), "CreateComputeShader" ) == false )
                return 0;
        }
        return _pDevice->_pipelineStates.insert( std::move( pso ) );
    }

    void D3D11RHIResourceFactory::destroyPipelineState( RHIPipelineStateHandle pso )
    {
        if ( pso == 0 )
            return;
        // 아래 `forgetPipelineStateInRecordingStates` 는 다른 스레드가 기록 중인 리스트의 상태에 쓴다. 병렬 기록 중에는 부르지 않는 것이 계약이다.
        _pDevice->requireRegistryMutableNow( "destroyPipelineState" );
        _pDevice->_pipelineStates.erase( pso );
        _pDevice->forgetPipelineStateInRecordingStates( pso );
    }

    RHIRenderPassHandle D3D11RHIResourceFactory::createRenderPass( const RHIRenderPassDesc& desc )
    {
        return _pDevice->_renderPassLedger.add( desc );
    }

    void D3D11RHIResourceFactory::destroyRenderPass( RHIRenderPassHandle pass )
    {
        _pDevice->_renderPassLedger.retire( pass );
    }
} // namespace sw
#endif
