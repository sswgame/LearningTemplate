#include "pch.h"

#include "Engine/Renderer/Frame/PostProcess/FramePostProcessMaterials.h"

#include "Core/Container/vector.h"
#include "Core/Log/LogOnce.h"
#include "Core/Math/MathUtil.h"

#include "Engine/Graphics/Material/MaterialRenderProxy.h"
#include "Engine/Graphics/RHI/IRHIDevice.h"
#include "Engine/Renderer/Extension/RenderExtension.h"
#include "Engine/Renderer/Frame/Resource/FramePSOManager.h"
#include "Engine/Renderer/Frame/View/FrameViewTarget.h"
#include "Engine/Renderer/Pipeline/RenderPipelineAsset.h"

namespace sw
{
    FramePostProcessMaterials::FramePostProcessMaterials( IRHIDevice* const& pDevice, const RenderPipelineAsset& pipeline, FramePSOManager& psoManager )
        : _pDevice{ pDevice }
        , _pipeline{ pipeline }
        , _psoManager{ psoManager }
        , _mapElementBuffer{}
        , _frame{ 1 }
    {
    }

    FramePostProcessMaterials::~FramePostProcessMaterials()
    {
        release();
    }

    const RHIStructuredBuffer* FramePostProcessMaterials::ensureElementBuffer( const hashed_string& materialPath, const shared_ptr<const MaterialRenderData>& data )
    {
        // 원소 stride 는 셰이더 리플렉션의 구조 버퍼 원소 크기다(GPUScene 머티리얼 그룹과 같은 규칙 — 다르면 DX11 이 SRV stride 를 거절한다).
        const uint32   stride  = data->_elementStride != 0 ? data->_elementStride : static_cast<uint32>( data->_bytes.size() );
        ElementBuffer& element = _mapElementBuffer[materialPath];
        element._lastUsedFrame = _frame;
        const bool bFits       = element._buffer.isValid() && element._buffer._elementSize == stride;
        if ( bFits == false && element._buffer.ensureCapacity( _pDevice, stride, 1u, RHIBufferUsage::Structured | RHIBufferUsage::ShaderResource, true, false, nullptr ) == false )
        {
            SW_LOG_ERROR_ONCE_ID( "FramePostProcessMaterials.CouldNotCreateElement", "Could not create the element buffer of post process material '%#' (stride %#) - it is not drawn", materialPath.c_str(), stride );
            return nullptr;
        }
        if ( bFits == false || element._uploaded != data )
        {
            element._buffer.upload( _pDevice, data->_bytes.data(), MathUtil::min( stride, static_cast<uint32>( data->_bytes.size() ) ) );
            element._uploaded = data;
        }
        return &element._buffer;
    }

    void FramePostProcessMaterials::prepareView( FrameViewTarget& view )
    {
        view._listPostProcessMaterialPass.clear();
        if ( _pDevice == nullptr || view._settings._listPostProcessMaterial.empty() )
            return;
        const vector<RenderGraphPassDesc>&  listPass       = _pipeline.getGraphPass();
        const vector<RenderPassAttachment>& listAttachment = _pipeline.getDesc()._listAttachment;
        for ( const PostProcessMaterialDraw& draw : view._settings._listPostProcessMaterial )
        {
            // 그 자리를 맡는 패스의 출력 형식이 PSO 의 타깃 형식이다(자리마다 패스는 하나 — 검증 3-8). 패스가 없는 파이프라인(포워드)은 그 블렌더블을 그리지 않는다.
            RHIFormat outputFormat = RHIFormat::Unknown;
            for ( const RenderGraphPassDesc& pass : listPass )
            {
                if ( pass._type != RenderPassType::PostProcessMaterial || pass._blendableLocation != draw._location || pass._listResolvedColorOutput.empty() )
                    continue;
                for ( const RenderPassAttachment& attachment : listAttachment )
                {
                    if ( string_view( attachment._name ) == pass._listResolvedColorOutput[0]._attachment.view() )
                        outputFormat = attachment._format;
                }
            }
            if ( outputFormat == RHIFormat::Unknown )
                continue;
            ExtensionProgramDesc program;
            program._shaderPath                               = draw._shaderPath;
            program._bBlend                                   = true; // 프리멀티플라이 — 셰이더가 (색 × 무게, 무게)를 낸다
            const RHIPipelineStateHandle             pso      = _psoManager.findOrCreateExtensionProgramPSO( program, outputFormat, false );
            const RHIStructuredBuffer*               pElement = ensureElementBuffer( draw._materialPath, draw._binding._data );
            FrameViewTarget::PostProcessMaterialPass drawPass;
            drawPass._pso         = pso;
            drawPass._buffer      = pElement != nullptr ? pElement->_buffer : 0;
            drawPass._srv         = pElement != nullptr ? pElement->_srv : kInvalidDescriptorIndex;
            drawPass._pTextureSRV = draw._binding._data->_arrTextureSRV;
            drawPass._weight      = draw._weight;
            drawPass._location    = draw._location;
            view._listPostProcessMaterialPass.push_back( drawPass );
        }
    }

    void FramePostProcessMaterials::pruneUnused()
    {
        // 이번 프레임에 어느 뷰도 그리지 않은 머티리얼의 버퍼는 놓는다(볼륨을 나가면 버퍼도 사라진다).
        for ( auto it = _mapElementBuffer.begin(); it != _mapElementBuffer.end(); )
        {
            if ( it->second._lastUsedFrame == _frame )
            {
                ++it;
                continue;
            }
            it->second._buffer.release( _pDevice );
            it = _mapElementBuffer.erase( it );
        }
        ++_frame;
    }

    void FramePostProcessMaterials::release()
    {
        for ( auto& [path, element] : _mapElementBuffer )
        {
            (void)path;
            element._buffer.release( _pDevice );
        }
        _mapElementBuffer.clear();
    }
} // namespace sw
