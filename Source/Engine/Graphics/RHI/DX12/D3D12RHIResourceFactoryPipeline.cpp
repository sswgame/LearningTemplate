/**
 * @file D3D12RHIResourceFactoryPipeline.cpp
 * @brief DirectX 12 의 파이프라인 상태 객체(PSO, 셰이더 스테이지, 렌더 패스 객체)입니다.
 * @details `D3D12RHIResourceFactory` 의 일부입니다. 리소스(버퍼, 텍스처)를 만드는 것과 파이프라인을 만드는 것은
 *          배우는 내용이 다르고 백엔드별 차이도 가장 크게 드러나는 곳이라 따로 둡니다.
 */
#include "pch.h"

#include "Core/Math/MathUtil.h"

#include "Engine/Common/EnginePlatformHeaders.h"
#include "Engine/Common/EngineServices.h"
#include "Engine/Graphics/RHI/DX/RHIDxgiFormat.h"
#include "Engine/Graphics/RHI/DX12/D3D12RHIDevice.h"
#include "Engine/Graphics/RHI/DX12/D3D12RHIResourceFactory.h"
#include "Engine/Graphics/RHI/Support/RHIShaderRequest.h"
#include "Engine/Graphics/Shader/Compile/ShaderCache.h"
#include "Engine/Graphics/Shader/Compile/ShaderCompiler.h"

#if defined( SW_PLATFORM_WINDOWS )
namespace sw
{
    SW_LOG_CALLER( "D3D12RHIResourceFactory" );

    RHIPipelineStateHandle D3D12RHIResourceFactory::createPipelineState( const RHIPipelineStateDesc& desc )
    {
        Microsoft::WRL::ComPtr<ID3D12PipelineState> pso;

        // 디스크립터 해석(진입점 기본값, define, 뎁스 전용 판정, RT 수)은 RHIShaderRequest 하나가 한다. 백엔드는 받기만 한다.
        const RHIGraphicsShaderRequest request         = RHIShaderRequest::resolveGraphics( desc, ShaderTargetFormat::DXIL_D3D12 );
        const bool                     bHasPixelShader = request._bHasPixelShader != SW_FALSE;
        ShaderCompileResult            vsResult{};
        ShaderCompileResult            psResult{};
        if ( RHIShaderRequest::compileGraphics( request, vsResult, psResult ) )
        {
            // 입력 레이아웃은 **공용 테이블**(constant::arrVertexAttribute)에서 만든다.
            D3D12_INPUT_ELEMENT_DESC arrInputElement[constant::kVertexAttributeCount]{};
            for ( uint32 attributeIndex = 0; attributeIndex < constant::kVertexAttributeCount; ++attributeIndex )
            {
                const RHIVertexAttribute& attribute                  = constant::arrVertexAttribute[attributeIndex];
                arrInputElement[attributeIndex].SemanticName         = attribute._pSemanticName;
                arrInputElement[attributeIndex].Format               = toDxgiVertexFormat( attribute );
                arrInputElement[attributeIndex].AlignedByteOffset    = attribute._byteOffset;
                arrInputElement[attributeIndex].InputSlot            = attribute._inputSlot;
                arrInputElement[attributeIndex].InputSlotClass       = ( attribute._bPerInstance != SW_FALSE ) ? D3D12_INPUT_CLASSIFICATION_PER_INSTANCE_DATA
                                                                                                               : D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA;
                arrInputElement[attributeIndex].InstanceDataStepRate = ( attribute._bPerInstance != SW_FALSE ) ? 1u : 0u;
            }

            D3D12_GRAPHICS_PIPELINE_STATE_DESC psoDesc{};
            psoDesc.InputLayout    = { arrInputElement, constant::kVertexAttributeCount };
            psoDesc.pRootSignature = _pDevice->_rootSignature.Get();
            psoDesc.VS             = { vsResult._bytecode.data(), vsResult._bytecode.size() };
            if ( bHasPixelShader )
                psoDesc.PS = { psResult._bytecode.data(), psResult._bytecode.size() };
            // 다른 백엔드와 같이 desc._fillMode 를 읽는다. SOLID 로 고정하면 Wireframe 요청이 DX12 에서만 조용히 솔리드가 된다.
            psoDesc.RasterizerState.FillMode = ( desc._fillMode == RHIFillMode::Wireframe )
                                                 ? D3D12_FILL_MODE_WIREFRAME
                                                 : D3D12_FILL_MODE_SOLID;
            psoDesc.RasterizerState.CullMode = ( desc._cullMode == RHICullMode::Front )
                                                 ? D3D12_CULL_MODE_FRONT
                                                 : ( ( desc._cullMode == RHICullMode::Back ) ? D3D12_CULL_MODE_BACK : D3D12_CULL_MODE_NONE );
            // 가까운 · 먼 평면에서 자른다 — DX11(DepthClipEnable) · Vulkan(depthClampEnable 끔) · GL(GL_DEPTH_CLAMP 끔)과 같다. 0 으로 두면 DX12 만 평면 밖 기하를 깊이 1 로 눌러 그린다
            // (psoDesc{} 의 기본값이 FALSE 다). 시야 거리 설정(gv_viewDistanceScale)이 먼 평면을 당겨도 DX12 에서만 먼 기하가 남았다(useroptions distance 0.307, 나머지 0.665).
            psoDesc.RasterizerState.DepthClipEnable = TRUE;
            // 깊이 바이어스 — 다른 백엔드와 같은 상수 · 기울기 두 칸이다(RHIPipelineStateDesc::_depthBias).
            psoDesc.RasterizerState.DepthBias            = desc._depthBias;
            psoDesc.RasterizerState.SlopeScaledDepthBias = desc._slopeScaledDepthBias;
            psoDesc.SampleMask                           = MathUtil::kMaxUInt32;
            psoDesc.PrimitiveTopologyType                = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
            // 뎁스 전용은 RT 가 0 개다. 1 로 두고 R8G8B8A8 을 선언하면 실제로는 DSV 만 바인딩되는 것과 어긋난다.
            psoDesc.NumRenderTargets = request._numRenderTargets;
            if ( psoDesc.NumRenderTargets > 8 )
                psoDesc.NumRenderTargets = 8;
            for ( UINT rtvIndex = 0; rtvIndex < psoDesc.NumRenderTargets; ++rtvIndex )
            {
                auto& rtBlend                 = psoDesc.BlendState.RenderTarget[rtvIndex];
                rtBlend.RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;
                psoDesc.RTVFormats[rtvIndex]  = toDxgiFormat( desc._arrRtvFormat[rtvIndex] );
                if ( desc._bEnableBlend != 0 )
                {
                    rtBlend.BlendEnable    = TRUE;
                    rtBlend.SrcBlend       = ( desc._bPremultipliedAlpha != SW_FALSE ) ? D3D12_BLEND_ONE : D3D12_BLEND_SRC_ALPHA;
                    rtBlend.DestBlend      = D3D12_BLEND_INV_SRC_ALPHA;
                    rtBlend.BlendOp        = D3D12_BLEND_OP_ADD;
                    rtBlend.SrcBlendAlpha  = D3D12_BLEND_ONE;
                    rtBlend.DestBlendAlpha = D3D12_BLEND_INV_SRC_ALPHA;
                    rtBlend.BlendOpAlpha   = D3D12_BLEND_OP_ADD;
                }
            }
            if ( desc._bEnableDepthTest != 0 )
            {
                psoDesc.DepthStencilState.DepthEnable    = TRUE;
                psoDesc.DepthStencilState.DepthWriteMask = ( desc._bEnableDepthWrite != 0 )
                                                             ? D3D12_DEPTH_WRITE_MASK_ALL
                                                             : D3D12_DEPTH_WRITE_MASK_ZERO;
                psoDesc.DepthStencilState.DepthFunc      = D3D12_COMPARISON_FUNC_LESS_EQUAL;
                psoDesc.DSVFormat                        = toDxgiFormat( desc._depthStencilFormat );
            }
            else
            {
                psoDesc.DepthStencilState.DepthEnable    = FALSE;
                psoDesc.DepthStencilState.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ZERO;
                psoDesc.DSVFormat                        = DXGI_FORMAT_UNKNOWN;
            }
            psoDesc.SampleDesc.Count = 1;

            // 실패를 조용히 삼키지 않는다. PSO 가 null 이면 드로우가 아무 흔적 없이 사라진다. 루트 시그니처와 셰이더가 맞지 않으면
            // 그렇게 숨는다. 디버그 레이어 메시지를 바로 비워 원인이 같은 줄에 나오게 한다.
            const HRESULT hrPSO = _pDevice->_device->CreateGraphicsPipelineState( &psoDesc, IID_PPV_ARGS( pso.GetAddressOf() ) );
            if ( FAILED( hrPSO ) )
            {
                SW_LOG_ERROR( "CreateGraphicsPipelineState failed (hr=%#): VS '%#' PS '%#'", static_cast<uint32>( hrPSO ), desc._vertexShaderPath.c_str(), desc._pixelShaderPath.c_str() );
                _pDevice->flushDebugMessages( "CreateGraphicsPipelineState" );
            }
        }

        return _pDevice->_pipelineStates.insert( { pso } );
    }

    RHIPipelineStateHandle D3D12RHIResourceFactory::createComputePipelineState( string_view shaderPath, string_view entryPoint )
    {
        Microsoft::WRL::ComPtr<ID3D12PipelineState> pso;
        if ( shaderPath.empty() == false )
        {
            ShaderCompileDesc csDesc{};
            csDesc._filePath        = shaderPath;
            csDesc._entryPoint      = string( resolveEntryPoint( entryPoint, ShaderStage::Compute ) );
            csDesc._stage           = ShaderStage::Compute;
            csDesc._targetFormat    = ShaderTargetFormat::DXIL_D3D12;
            ShaderCompileResult res = RHIShaderRequest::compile( csDesc );
            if ( res._bSuccess )
            {
                D3D12_COMPUTE_PIPELINE_STATE_DESC psoDesc{};
                psoDesc.pRootSignature = _pDevice->_rootSignature.Get();
                psoDesc.CS             = { res._bytecode.data(), res._bytecode.size() };

                const HRESULT hr = _pDevice->_device->CreateComputePipelineState( &psoDesc, IID_PPV_ARGS( pso.GetAddressOf() ) );
                if ( FAILED( hr ) )
                {
                    SW_LOG_ERROR( "CreateComputePipelineState failed hr=0x%#", static_cast<uint32>( hr ) );
                    _pDevice->flushDebugMessages( "CreateComputePipelineState" );
                    return 0;
                }
            }
            else
                return 0;
        }
        return _pDevice->_pipelineStates.insert( { pso } );
    }

    void D3D12RHIResourceFactory::destroyPipelineState( RHIPipelineStateHandle pso )
    {
        if ( pso == 0 )
            return;
        D3D12RHIDevice::D3D12PipelineStateRecord record{};
        if ( _pDevice->_pipelineStates.take( pso, record ) == false )
            return;
        if ( _pDevice->_frameStreamState._activeGraphicsPSO == pso )
            _pDevice->_frameStreamState._activeGraphicsPSO = 0;
        _pDevice->_releaseQueue.enqueueGPUKeepAlive( record._pso, _pDevice->_fenceValue );
    }

    RHIRenderPassHandle D3D12RHIResourceFactory::createRenderPass( const RHIRenderPassDesc& desc )
    {
        D3D12RHIDevice::D3D12RenderPassRecord record{};
        record._desc   = desc;
        record._bAlive = SW_TRUE;
        _pDevice->_listRenderPass.push_back( record );
        return _pDevice->_listRenderPass.size();
    }

    void D3D12RHIResourceFactory::destroyRenderPass( RHIRenderPassHandle pass )
    {
        if ( pass == 0 || pass > _pDevice->_listRenderPass.size() )
            return;
        _pDevice->_listRenderPass[pass - 1]._bAlive = SW_FALSE;
    }
} // namespace sw
#endif
