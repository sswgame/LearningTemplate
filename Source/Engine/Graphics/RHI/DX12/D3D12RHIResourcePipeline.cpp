/**
 * @file D3D12RHIResourcePipeline.cpp
 * @brief DirectX 12 의 파이프라인 상태 객체 — PSO, 셰이더 스테이지, 렌더패스 객체
 * @details `D3D12RHIResource` 의 일부다. 리소스(버퍼/텍스처)를 만드는 것과 파이프라인을 만드는 것은
 *          배우는 내용이 다르고 백엔드별 차이도 가장 크게 드러나는 곳이라 따로 둔다.
 */
#include "pch.h"

#include "Core/Math/MathUtil.h"

#include "Engine/Common/EnginePlatformHeaders.h"
#include "Engine/Common/EngineServices.h"
#include "Engine/Graphics/RHI/DX/RHIDxgiFormat.h"
#include "Engine/Graphics/RHI/DX12/D3D12RHIDevice.h"
#include "Engine/Graphics/RHI/DX12/D3D12RHIResource.h"
#include "Engine/Graphics/RHI/Support/RHIShaderRequest.h"
#include "Engine/Graphics/Shader/Compile/ShaderCache.h"

#if defined( SW_PLATFORM_WINDOWS )
namespace sw
{
    SW_LOG_CALLER( "D3D12RHIResource" );

    RHIPipelineStateHandle D3D12RHIResource::createPipelineState( const RHIPipelineStateDesc& desc )
    {
        Microsoft::WRL::ComPtr<ID3D12PipelineState> pso;

        // 서술체 해석(진입점 기본값·define·뎁스 전용 판정·RT 수)은 RHIShaderRequest 하나가 한다 — 백엔드는 받기만 한다.
        // 예전엔 여기서 직접 읽으면서 뎁스 전용 판정만 빠져, 그림자 패스에 머티리얼 define 을 얹은 변형이 DX12 에서만
        // PS 리플렉션을 요구했다.
        const RHIGraphicsShaderRequest request         = RHIShaderRequest::resolveGraphics( desc, ShaderTargetFormat::DXIL_D3D12 );
        const bool                     bHasPixelShader = request._bHasPixelShader != SW_FALSE;
        ShaderCompileResult            vsResult        = RHIShaderRequest::compile( request._vertex );
        ShaderCompileResult            psResult{};
        if ( bHasPixelShader )
            psResult = RHIShaderRequest::compile( request._pixel );

        if ( vsResult._bSuccess && ( bHasPixelShader == false || psResult._bSuccess ) )
        {
            D3D12_INPUT_ELEMENT_DESC inputElementDescs[] = {
                {"POSITION", 0,    DXGI_FORMAT_R32G32B32_FLOAT, 0,  0, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
                {   "COLOR", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 12, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0}
            };

            D3D12_GRAPHICS_PIPELINE_STATE_DESC psoDesc{};
            psoDesc.InputLayout    = { inputElementDescs, _countof( inputElementDescs ) };
            psoDesc.pRootSignature = _pDevice->_rootSignature.Get();
            psoDesc.VS             = { vsResult._bytecode.data(), vsResult._bytecode.size() };
            if ( bHasPixelShader )
                psoDesc.PS = { psResult._bytecode.data(), psResult._bytecode.size() };
            // DX11·Vulkan·GL 은 desc._fillMode 를 읽는데 여기만 SOLID 로 못박혀 있었다 —
            // Wireframe 을 요청한 파이프라인이 DX12 에서만 조용히 솔리드로 그려졌다.
            psoDesc.RasterizerState.FillMode = ( desc._fillMode == RHIFillMode::Wireframe )
                                                 ? D3D12_FILL_MODE_WIREFRAME
                                                 : D3D12_FILL_MODE_SOLID;
            psoDesc.RasterizerState.CullMode = ( desc._cullMode == RHICullMode::Front )
                                                 ? D3D12_CULL_MODE_FRONT
                                                 : ( ( desc._cullMode == RHICullMode::Back ) ? D3D12_CULL_MODE_BACK : D3D12_CULL_MODE_NONE );
            psoDesc.SampleMask               = MathUtil::MaxUInt32;
            psoDesc.PrimitiveTopologyType    = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
            // 뎁스 전용은 RT 0 개다 — 예전엔 1 로 올려 R8G8B8A8 을 선언했는데 실제로는 DSV 만 바인딩된다.
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
                    rtBlend.SrcBlend       = D3D12_BLEND_SRC_ALPHA;
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

            // 실패를 조용히 삼키지 않는다 — PSO 가 null 이면 드로우가 아무 흔적 없이 사라진다(루트 시그니처와 셰이더 불일치가
            // 그렇게 숨어 있었다). 디버그 레이어 메시지를 바로 비워 원인이 같은 줄에 나오게 한다.
            const HRESULT hrPso = _pDevice->_device->CreateGraphicsPipelineState( &psoDesc, IID_PPV_ARGS( pso.GetAddressOf() ) );
            if ( FAILED( hrPso ) )
            {
                SW_LOG_ERROR( "CreateGraphicsPipelineState 실패 (hr=%#): VS '%#' PS '%#'", static_cast<uint32>( hrPso ), desc._vertexShaderPath.c_str(), desc._pixelShaderPath.c_str() );
                _pDevice->flushDebugMessages( "CreateGraphicsPipelineState" );
            }
        }

        return _pDevice->_pipelineStates.insert( { pso } );
    }

    RHIPipelineStateHandle D3D12RHIResource::createComputePipelineState( string_view shaderPath, string_view entryPoint )
    {
        Microsoft::WRL::ComPtr<ID3D12PipelineState> pso;
        if ( shaderPath.empty() == false )
        {
            ShaderCompileDesc csDesc{};
            csDesc._filePath        = shaderPath;
            csDesc._entryPoint      = entryPoint;
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

    void D3D12RHIResource::destroyPipelineState( RHIPipelineStateHandle pso )
    {
        if ( pso == 0 )
            return;
        D3D12RHIDevice::D3D12PipelineStateRecord record{};
        if ( _pDevice->_pipelineStates.take( pso, record ) == false )
            return;
        if ( _pDevice->_frameStreamState._activeGraphicsPso == pso )
            _pDevice->_frameStreamState._activeGraphicsPso = 0;
        Microsoft::WRL::ComPtr<ID3D12PipelineState> owned     = record._pso;
        auto                                        releaseCb = [owned]()
        { (void)owned.Get(); };
        _pDevice->_releaseQueue.enqueueGpuRelease( SW_DELEGATE_LAMBDA( RHIResourceReleaseDelegate, releaseCb ), _pDevice->_fenceValue );
    }

    RHIRenderPassHandle D3D12RHIResource::createRenderPass( const RHIRenderPassDesc& desc )
    {
        D3D12RHIDevice::D3D12RenderPassRecord record{};
        record._desc   = desc;
        record._bAlive = SW_TRUE;
        _pDevice->_listRenderPass.push_back( record );
        return _pDevice->_listRenderPass.size();
    }

    void D3D12RHIResource::destroyRenderPass( RHIRenderPassHandle pass )
    {
        if ( pass == 0 || pass > _pDevice->_listRenderPass.size() )
            return;
        _pDevice->_listRenderPass[pass - 1]._bAlive = SW_FALSE;
    }
} // namespace sw
#endif
