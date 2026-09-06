#include "pch.h"

#include "Core/String/StringUtil.h"
#include "Core/Task/TaskManager.h"

#include "Engine/Graphics/Material/Material.h"
#include "Engine/Graphics/Material/MaterialInstance.h"
#include "Engine/Graphics/RHI/IRHIDevice.h"
#include "Engine/Graphics/RHI/IRHIResource.h"
#include "Engine/Graphics/Renderer/Frame/FrameRenderer.h"
#include "Engine/Graphics/Renderer/Frame/FrameRendererUtil.h"

namespace sw
{
    RHIPipelineStateHandle FrameRenderer::createEnginePso( string_view shaderPath, bool bDepthTest, uint32 numRenderTargets,
                                                           const RHIFormat* pRtvFormats, bool bBlend, bool bDepthWrite )
    {
        if ( _pDevice == nullptr )
            return 0;
        RHIPipelineStateDesc desc{};
        desc._vertexShaderPath  = shaderPath;
        desc._pixelShaderPath   = shaderPath;
        desc._bEnableDepthTest  = bDepthTest ? 1 : 0;
        desc._bEnableDepthWrite = bDepthWrite ? 1 : 0;
        desc._bEnableBlend      = bBlend ? 1 : 0;
        desc._cullMode          = bDepthTest ? RHICullMode::Back : RHICullMode::None;
        desc._numRenderTargets  = numRenderTargets > 0 ? numRenderTargets : 1;
        if ( desc._numRenderTargets > kMaxColorAttachments )
            desc._numRenderTargets = kMaxColorAttachments;
        for ( uint32 rtIndex = 0; rtIndex < desc._numRenderTargets; ++rtIndex )
        {
            desc._arrRtvFormat[rtIndex] = ( pRtvFormats != nullptr ) ? pRtvFormats[rtIndex] : RHIFormat::R8G8B8A8_UNORM;
        }
        const RHIPipelineStateHandle handle = _pDevice->getResource()->createPipelineState( desc );
        registerPsoLayout( handle, desc );
        return handle;
    }

    RHIPipelineStateHandle FrameRenderer::createPsoForPassType( RenderPassType passType, string_view defaultShader,
                                                                bool bDepthTest, uint32 numRenderTargets, const RHIFormat* pRtvFormats,
                                                                bool bDefaultBlend, bool bDefaultDepthWrite,
                                                                const vector<string>* pExtraDefines )
    {
        if ( _pDevice == nullptr )
            return 0;

        const RenderGraphPassDesc* pPassDesc = findPassDescByType( passType );
        RHIPipelineStateDesc       desc{};
        desc._vertexShaderPath = ( pPassDesc != nullptr && pPassDesc->_shaderPath.empty() == false ) ? pPassDesc->_shaderPath : defaultShader;
        desc._pixelShaderPath  = desc._vertexShaderPath;
        desc._vertexEntryPoint = ( pPassDesc != nullptr && pPassDesc->_vertexEntryPoint.empty() == false )
                                   ? pPassDesc->_vertexEntryPoint
                                   : FrameRendererUtil::Entry::kVSMain;
        desc._pixelEntryPoint  = ( pPassDesc != nullptr && pPassDesc->_pixelEntryPoint.empty() == false )
                                   ? pPassDesc->_pixelEntryPoint
                                   : FrameRendererUtil::Entry::kPSMain;
        // 예전엔 pPassDesc 가 있으면 XML 값으로 **덮어썼다**. 그런데 RenderGraphPassDesc 의
        // _bEnableDepthTest/_bEnableDepthWrite 기본값이 true 이고 파이프라인 XML 은 이 항목을
        // 아예 적지 않는다 — 그래서 registerPso 가 풀스크린 패스에 명시적으로 넘긴 bDepthTest=false 가
        // 통째로 무시되고 뎁스 테스트가 켜졌다. 그 PSO 는 뎁스 포맷을 선언하는데 풀스크린 패스는
        // DSV 를 바인딩하지 않으므로 드로우마다 검증 오류가 났다.
        // 호출부의 bDepthTest 는 "이 패스가 지오메트리인가 풀스크린인가" 라는 구조적 사실이고 XML 은
        // 그 안에서의 조정이다. 그래서 덮어쓰기가 아니라 AND 다 — XML 로 끌 수는 있어도 켤 수는 없다.
        const bool bPassDepthTest  = ( pPassDesc == nullptr ) || ( pPassDesc->_bEnableDepthTest != 0 );
        const bool bPassDepthWrite = ( pPassDesc == nullptr ) || ( pPassDesc->_bEnableDepthWrite != 0 );
        desc._bEnableDepthTest     = ( bDepthTest && bPassDepthTest ) ? 1 : 0;
        desc._bEnableDepthWrite    = ( bDefaultDepthWrite && bPassDepthWrite ) ? 1 : 0;
        desc._bEnableBlend         = pPassDesc != nullptr ? ( pPassDesc->_bEnableBlend != 0 ? 1 : 0 ) : ( bDefaultBlend ? 1 : 0 );

        // 뎁스를 안 쓰는 패스(풀스크린)는 렌더패스에 DSV 를 붙이지 않는데, desc 의 기본값이 D24 라서
        // PSO 는 "뎁스 있음" 으로 만들어졌다. DX12 는 null DSV 를 PSO 뎁스 포맷이 UNKNOWN 일 때만
        // 허용하므로 그런 패스의 드로우마다 검증 오류가 났다. 뎁스 테스트가 꺼져 있으면 뎁스 쓰기도
        // 의미가 없으니 같이 끈다.
        desc._depthStencilFormat = ( desc._bEnableDepthTest != 0 ) ? constant::kDepthStencilFormat : RHIFormat::Unknown;
        if ( desc._bEnableDepthTest == 0 )
            desc._bEnableDepthWrite = 0;

        desc._cullMode = RHICullMode::Back;
        if ( pPassDesc != nullptr )
        {
            const string& cull = pPassDesc->_cullMode;
            if ( cull == "None" || cull == "none" )
                desc._cullMode = RHICullMode::None;
            else if ( cull == "Front" || cull == "front" )
                desc._cullMode = RHICullMode::Front;
            if ( pPassDesc->_listPermutation.empty() == false )
                desc._listShaderDefine = pPassDesc->_listPermutation;
        }
        else if ( bDepthTest == false )
            desc._cullMode = RHICullMode::None;

        if ( pExtraDefines != nullptr )
        {
            for ( const string& defineStr : *pExtraDefines )
            {
                bool found{ false };
                for ( const string& existing : desc._listShaderDefine )
                {
                    if ( existing == defineStr )
                    {
                        found = true;
                        break;
                    }
                }
                if ( found == false )
                    desc._listShaderDefine.push_back( defineStr );
            }
        }

        desc._numRenderTargets = numRenderTargets;
        if ( desc._numRenderTargets > kMaxColorAttachments )
            desc._numRenderTargets = kMaxColorAttachments;
        if ( pRtvFormats != nullptr )
        {
            for ( uint32 rtIndex = 0; rtIndex < desc._numRenderTargets; ++rtIndex )
            {
                desc._arrRtvFormat[rtIndex] = pRtvFormats[rtIndex];
            }
        }
        else if ( pPassDesc != nullptr && pPassDesc->_listOutput.empty() == false )
        {
            uint32 colorCount{ 0 };
            bool   bHasDepthOutput{ false };
            for ( const string& outName : pPassDesc->_listOutput )
            {
                if ( colorCount >= kMaxColorAttachments )
                    break;
                for ( const RenderPassAttachment& att : _pipelineResource.getDesc()._listAttachment )
                {
                    if ( att._name == outName )
                    {
                        const RHIFormat fmt = parseAttachmentFormat( att._format );
                        if ( FrameRendererUtil::isDepthFormat( fmt ) == false )
                        {
                            desc._arrRtvFormat[colorCount++] = fmt;
                        }
                        else
                        {
                            bHasDepthOutput = true;
                        }
                        break;
                    }
                }
            }
            desc._numRenderTargets = ( colorCount > 0 ) ? colorCount : ( bHasDepthOutput ? 0 : 1 );
            if ( desc._numRenderTargets == 1 && colorCount == 0 )
                desc._arrRtvFormat[0] = RHIFormat::R8G8B8A8_UNORM;
        }
        else
        {
            for ( uint32 rtIndex = 0; rtIndex < desc._numRenderTargets; ++rtIndex )
            {
                desc._arrRtvFormat[rtIndex] = RHIFormat::R8G8B8A8_UNORM;
            }
        }
        const RHIPipelineStateHandle handle = _pDevice->getResource()->createPipelineState( desc );
        registerPsoLayout( handle, desc );
        return handle;
    }
} // namespace sw
