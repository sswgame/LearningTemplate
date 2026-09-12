/**
 * @file D3D11RHIResourcePipeline.cpp
 * @brief DirectX 11 의 파이프라인 상태 객체 — PSO, 셰이더 스테이지, 렌더패스 객체
 * @details `D3D11RHIResource` 의 일부다. 리소스(버퍼/텍스처)를 만드는 것과 파이프라인을 만드는 것은
 *          배우는 내용이 다르고 백엔드별 차이도 가장 크게 드러나는 곳이라 따로 둔다.
 */
#include "pch.h"

#include "Core/Math/MathUtil.h"

#include "Engine/Common/EngineServices.h"
#include "Engine/Graphics/RHI/DX/RHIDxgiFormat.h"
#include "Engine/Graphics/RHI/DX11/D3D11RHIDevice.h"
#include "Engine/Graphics/RHI/DX11/D3D11RHIResource.h"
#include "Engine/Graphics/RHI/Support/RHIIndexFreeList.h"
#include "Engine/Graphics/RHI/Support/RHIShaderRequest.h"
#include "Engine/Graphics/Shader/Compile/ShaderCache.h"

#if defined( SW_PLATFORM_WINDOWS )
namespace sw
{
    SW_LOG_CALLER( "D3D11" );

    RHIPipelineStateHandle D3D11RHIResource::createPipelineState( const RHIPipelineStateDesc& desc )
    {
        auto fillDefines = [&]( ShaderCompileDesc& compileDesc )
        {
            for ( const string& define : desc._listShaderDefine )
                compileDesc._listDefine.push_back( ShaderMacroDefine::parse( define ) );
        };

        // 서술체 해석(진입점 기본값·define·뎁스 전용 판정)은 RHIShaderRequest 하나가 한다 — 백엔드는 받기만 한다.
        // (컴퓨트 경로는 아래에서 fillDefines 를 그대로 쓴다 — 그래픽스 요청에는 컴퓨트 스테이지가 없다.)
        const RHIGraphicsShaderRequest request = RHIShaderRequest::resolveGraphics( desc, ShaderTargetFormat::DXBC_D3D11 );

        D3D11RHIDevice::D3D11PipelineStateRecord pso{};
        if ( desc._vertexShaderPath.empty() == false )
        {
            ShaderCompileResult res = RHIShaderRequest::compile( request._vertex );
            if ( res._bSuccess )
            {
                _pDevice->_device->CreateVertexShader( res._bytecode.data(), res._bytecode.size(), nullptr, pso._vs.GetAddressOf() );
                D3D11_INPUT_ELEMENT_DESC inputElementDescs[] = {
                    {"POSITION", 0,    DXGI_FORMAT_R32G32B32_FLOAT, 0,  0, D3D11_INPUT_PER_VERTEX_DATA, 0},
                    {   "COLOR", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 12, D3D11_INPUT_PER_VERTEX_DATA, 0}
                };
                _pDevice->_device->CreateInputLayout( inputElementDescs, _countof( inputElementDescs ), res._bytecode.data(), res._bytecode.size(), pso._inputLayout.GetAddressOf() );
            }
        }
        // 뎁스 전용(RT 0 개)이면 경로가 있어도 PS 를 붙이지 않는다 — 다른 세 백엔드와 같은 규칙이다.
        if ( request._bHasPixelShader != SW_FALSE )
        {
            ShaderCompileResult res = RHIShaderRequest::compile( request._pixel );
            if ( res._bSuccess )
                _pDevice->_device->CreatePixelShader( res._bytecode.data(), res._bytecode.size(), nullptr, pso._ps.GetAddressOf() );
        }
        if ( desc._computeShaderPath.empty() == false )
        {
            ShaderCompileDesc csDesc{};
            csDesc._filePath     = desc._computeShaderPath;
            csDesc._entryPoint   = desc._computeEntryPoint.empty() ? "CSMain" : desc._computeEntryPoint;
            csDesc._stage        = ShaderStage::Compute;
            csDesc._targetFormat = ShaderTargetFormat::DXBC_D3D11;
            fillDefines( csDesc );
            ShaderCompileResult res = RHIShaderRequest::compile( csDesc );
            if ( res._bSuccess )
                _pDevice->_device->CreateComputeShader( res._bytecode.data(), res._bytecode.size(), nullptr, pso._cs.GetAddressOf() );
        }

        D3D11_RASTERIZER_DESC rasterizerDesc{};
        rasterizerDesc.FillMode        = ( desc._fillMode == RHIFillMode::Wireframe ) ? D3D11_FILL_WIREFRAME : D3D11_FILL_SOLID;
        rasterizerDesc.CullMode        = ( desc._cullMode == RHICullMode::Front ) ? D3D11_CULL_FRONT : ( ( desc._cullMode == RHICullMode::Back ) ? D3D11_CULL_BACK : D3D11_CULL_NONE );
        rasterizerDesc.DepthClipEnable = TRUE;
        if ( _pDevice != nullptr )
            _pDevice->_device->CreateRasterizerState( &rasterizerDesc, pso._rasterizerState.GetAddressOf() );

        if ( _pDevice != nullptr )
        {
            D3D11_BLEND_DESC blendDesc{};
            blendDesc.RenderTarget[0].RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_ALL;
            if ( desc._bEnableBlend != 0 )
            {
                blendDesc.RenderTarget[0].BlendEnable    = TRUE;
                blendDesc.RenderTarget[0].SrcBlend       = D3D11_BLEND_SRC_ALPHA;
                blendDesc.RenderTarget[0].DestBlend      = D3D11_BLEND_INV_SRC_ALPHA;
                blendDesc.RenderTarget[0].BlendOp        = D3D11_BLEND_OP_ADD;
                blendDesc.RenderTarget[0].SrcBlendAlpha  = D3D11_BLEND_ONE;
                blendDesc.RenderTarget[0].DestBlendAlpha = D3D11_BLEND_INV_SRC_ALPHA;
                blendDesc.RenderTarget[0].BlendOpAlpha   = D3D11_BLEND_OP_ADD;
            }
            _pDevice->_device->CreateBlendState( &blendDesc, pso._blendState.GetAddressOf() );

            D3D11_DEPTH_STENCIL_DESC dsDesc{};
            dsDesc.DepthEnable    = ( desc._bEnableDepthTest != 0 ) ? TRUE : FALSE;
            dsDesc.DepthWriteMask = ( desc._bEnableDepthWrite != 0 ) ? D3D11_DEPTH_WRITE_MASK_ALL : D3D11_DEPTH_WRITE_MASK_ZERO;
            dsDesc.DepthFunc      = D3D11_COMPARISON_LESS_EQUAL;
            _pDevice->_device->CreateDepthStencilState( &dsDesc, pso._depthStencilState.GetAddressOf() );
        }

        return _pDevice->_pipelineStates.insert( std::move( pso ) );
    }

    RHIPipelineStateHandle D3D11RHIResource::createComputePipelineState( string_view shaderPath, string_view entryPoint )
    {
        D3D11RHIDevice::D3D11PipelineStateRecord pso{};
        if ( shaderPath.empty() == false )
        {
            ShaderCompileDesc csDesc{};
            csDesc._filePath        = shaderPath;
            csDesc._entryPoint      = entryPoint;
            csDesc._stage           = ShaderStage::Compute;
            csDesc._targetFormat    = ShaderTargetFormat::DXBC_D3D11;
            ShaderCompileResult res = RHIShaderRequest::compile( csDesc );
            if ( res._bSuccess == false || FAILED( _pDevice->_device->CreateComputeShader( res._bytecode.data(), res._bytecode.size(), nullptr, pso._cs.GetAddressOf() ) ) )
                return 0;
        }
        return _pDevice->_pipelineStates.insert( std::move( pso ) );
    }

    void D3D11RHIResource::destroyPipelineState( RHIPipelineStateHandle pso )
    {
        if ( pso == 0 )
            return;
        _pDevice->_pipelineStates.erase( pso );
        if ( _pDevice->_recordingState._activeGraphicsPso == pso )
            _pDevice->_recordingState._activeGraphicsPso = 0;
    }

    RHIRenderPassHandle D3D11RHIResource::createRenderPass( const RHIRenderPassDesc& desc )
    {
        D3D11RHIDevice::D3D11RenderPassRecord record{};
        record._desc   = desc;
        record._bAlive = SW_TRUE;
        _pDevice->_listRenderPass.push_back( record );
        return _pDevice->_listRenderPass.size();
    }

    void D3D11RHIResource::destroyRenderPass( RHIRenderPassHandle pass )
    {
        if ( pass == 0 || pass > _pDevice->_listRenderPass.size() )
            return;
        _pDevice->_listRenderPass[pass - 1]._bAlive = SW_FALSE;
    }
} // namespace sw
#endif
