/**
 * @file FrameRendererNanCheck.cpp
 * @brief 패스 출력의 NaN · Inf 감시(`-gv_nanCheck`)를 기록하는 쪽입니다 — 그래프가 끝난 뒤 주 시점의 부동소수 컬러 출력마다 컴퓨트를 한 번 돌립니다(`GpuNanCheck`).
 * @details 그래프의 패스는 병렬로 기록될 수 있어 패스 안에서 컴퓨트를 끼우면 한 버퍼를 여러 리스트가 만진다. 그래서 감시는 그래프가 끝난 뒤 한 스레드에서 자기 리스트에 기록한다.
 *          그만큼 본 값은 패스 직후가 아니라 그래프 끝 시점의 내용이다(같은 프레임에서 첨부를 다시 쓰는 패스가 있으면 마지막 쓴 패스의 출력).
 */
#include "pch.h"

#include "Core/Container/VectorUtil.h"
#include "Core/Container/formatString.h"
#include "Core/Log/LogOnce.h"

#include "Engine/Graphics/RHI/IRHICommandList.h"
#include "Engine/Graphics/RHI/IRHIDevice.h"
#include "Engine/Renderer/Frame/FrameRenderer.h"
#include "Engine/Renderer/Pipeline/RenderPassTypeInfo.h"

namespace sw
{
    SW_LOG_CALLER( "FrameRenderer" );

    void FrameRenderer::scanPassOutputsForNan( IRHIDevice* pDevice )
    {
        if ( _nanCheck.isActive() == false || pDevice == nullptr )
            return;
        const RHIPipelineStateHandle pso = getEnginePSO( RenderPassType::NanCheck );
        if ( pso == 0 )
        {
            SW_LOG_ERROR_ONCE( "gv_nanCheck is on but the NaN check PSO is missing (shader not cooked?) - no output is scanned" );
            return;
        }
        if ( _nanCheckCommandList == nullptr )
            _nanCheckCommandList = pDevice->createCommandList();
        IRHICommandList* pCmd = _nanCheckCommandList.get();
        if ( pCmd == nullptr )
            return;

        pCmd->beginCommandList();
        vector<RHITextureHandle> listScanned; // 같은 텍스처를 가리키는 출력은 한 번만 훑는다
        for ( const RenderGraphPassDesc& pass : _pipelineResource.getGraphPass() )
        {
            for ( const RenderGraphPassDesc::ResolvedAttachment& output : pass._listResolvedColorOutput )
            {
                const TransientAttachmentPool::Attachment attachment = _mainView._transientPool.find( output._attachment.view() );
                const bool                                bScannable = attachment._texture != 0 && attachment._srv != kInvalidDescriptorIndex && GpuNanCheck::isScannableFormat( attachment._format );
                if ( bScannable == false || VectorUtil::contains( listScanned, attachment._texture ) )
                    continue;
                listScanned.push_back( attachment._texture );

                utf8 arrName[constant::kMaxBuffer64];
                formatstring( arrName, sizeof( arrName ), "%#:%#", pass._name, output._attachment.view() );
                // 컴퓨트가 엔진 텍스처로 읽도록 셰이더 읽기 상태로 옮기고, 다음 프레임의 패스가 쓰도록 렌더 타깃 상태로 돌려 둔다.
                pCmd->prepareTextureForShaderRead( attachment._texture );
                _nanCheck.scan( *pCmd, pso, arrName, attachment._srv, attachment._width, attachment._height );
                pCmd->prepareTextureForRenderTarget( attachment._texture );
            }
        }
        _nanCheck.requestReadback( *pCmd );
        pCmd->endCommandList();
        pDevice->executeCommandList( pCmd );
    }
} // namespace sw
