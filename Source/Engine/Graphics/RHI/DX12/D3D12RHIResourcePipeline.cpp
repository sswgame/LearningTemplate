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
#include "Engine/Graphics/Shader/ShaderCache.h"

#if defined( SW_PLATFORM_WINDOWS )
namespace sw
{
    namespace
    {
        ShaderCompileResult compileShader( const ShaderCompileDesc& desc )
        {
            if ( engine::areEngineServicesBound() )
                return engine::getShaderCache().getOrCompile( desc );
            return ShaderCompiler::compileHLSL( desc );
        }
    } // namespace
} // namespace sw

namespace sw
{
    SW_LOG_CALLER( "D3D12RHIResource" );

    RHIPipelineStateHandle D3D12RHIResource::createPipelineState( const RHIPipelineStateDesc& desc )
    {
        Microsoft::WRL::ComPtr<ID3D12PipelineState> pso;
        auto                                        fillDefines = [&]( ShaderCompileDesc& cd )
        {
            for ( const string& def : desc._listShaderDefine )
            {
                ShaderMacroDefine m{};
                const size_t      eq = def.find( '=' );
                if ( eq == string::npos )
                {
                    m._name  = def;
                    m._value = "1";
                }
                else
                {
                    m._name  = def.substr( 0, eq );
                    m._value = def.substr( eq + 1 );
                }
                cd._listDefine.push_back( std::move( m ) );
            }
        };

        ShaderCompileDesc vsDesc{};
        vsDesc._filePath     = desc._vertexShaderPath;
        vsDesc._entryPoint   = desc._vertexEntryPoint.empty() ? "VSMain" : desc._vertexEntryPoint;
        vsDesc._stage        = ShaderStage::Vertex;
        vsDesc._targetFormat = ShaderTargetFormat::DXIL_D3D12;
        fillDefines( vsDesc );
        ShaderCompileResult vsResult = compileShader( vsDesc );

        ShaderCompileDesc psDesc{};
        psDesc._filePath     = desc._pixelShaderPath;
        psDesc._entryPoint   = desc._pixelEntryPoint.empty() ? "PSMain" : desc._pixelEntryPoint;
        psDesc._stage        = ShaderStage::Pixel;
        psDesc._targetFormat = ShaderTargetFormat::DXIL_D3D12;
        fillDefines( psDesc );
        ShaderCompileResult psResult = compileShader( psDesc );

        if ( vsResult._bSuccess && psResult._bSuccess )
        {
            D3D12_INPUT_ELEMENT_DESC inputElementDescs[] = {
                {"POSITION", 0,    DXGI_FORMAT_R32G32B32_FLOAT, 0,  0, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
                {   "COLOR", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 12, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0}
            };

            D3D12_GRAPHICS_PIPELINE_STATE_DESC psoDesc{};
            psoDesc.InputLayout              = { inputElementDescs, _countof( inputElementDescs ) };
            psoDesc.pRootSignature           = _pDevice->_rootSignature.Get();
            psoDesc.VS                       = { vsResult._bytecode.data(), vsResult._bytecode.size() };
            psoDesc.PS                       = { psResult._bytecode.data(), psResult._bytecode.size() };
            psoDesc.RasterizerState.FillMode = D3D12_FILL_MODE_SOLID;
            psoDesc.RasterizerState.CullMode = ( desc._cullMode == RHICullMode::Front )
                                                 ? D3D12_CULL_MODE_FRONT
                                                 : ( ( desc._cullMode == RHICullMode::Back ) ? D3D12_CULL_MODE_BACK : D3D12_CULL_MODE_NONE );
            psoDesc.SampleMask               = MathUtil::MaxUInt32;
            psoDesc.PrimitiveTopologyType    = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
            psoDesc.NumRenderTargets         = desc._numRenderTargets > 0 ? desc._numRenderTargets : 1;
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

            _pDevice->_device->CreateGraphicsPipelineState( &psoDesc, IID_PPV_ARGS( pso.GetAddressOf() ) );
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
            ShaderCompileResult res = compileShader( csDesc );
            if ( res._bSuccess )
            {
                D3D12_COMPUTE_PIPELINE_STATE_DESC psoDesc{};
                psoDesc.pRootSignature = _pDevice->_computeRootSignature ? _pDevice->_computeRootSignature.Get() : _pDevice->_rootSignature.Get();
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
        record._bAlive = 1;
        _pDevice->_listRenderPass.push_back( record );
        return _pDevice->_listRenderPass.size();
    }

    void D3D12RHIResource::destroyRenderPass( RHIRenderPassHandle pass )
    {
        if ( pass == 0 || pass > _pDevice->_listRenderPass.size() )
            return;
        _pDevice->_listRenderPass[pass - 1]._bAlive = 0;
    }
} // namespace sw
#endif
