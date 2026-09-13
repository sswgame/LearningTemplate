/**
 * @file FrameRendererTransients.cpp
 * @brief 프레임 첨부(트랜지언트 렌더타깃)의 수명과 조회 — 파이프라인이 선언한 것을 창 크기로 만든다.
 * @details 첨부는 **구성이 바뀔 때만** 다시 만들어진다(창 크기 · 파이프라인). 소유는 `TransientAttachmentPool` 이 하고
 *          여기서는 "무엇을 얼마나 만들지" 를 파이프라인 선언에서 읽어 채운다. 읽어 가는 쪽(리드백 · PPM 덤프)은
 *          프레임 경로가 아니라 GPU 를 기다리는 진단이라 `FrameRendererReadback.cpp` 로 갈라 두었다.
 */
#include "pch.h"

#include "Engine/Common/EngineServices.h"
#include "Engine/Graphics/RHI/IRHIDevice.h"
#include "Engine/Graphics/RHI/IRHIResource.h"
#include "Engine/Graphics/Renderer/Debug/RenderTargetRegistry.h"
#include "Engine/Graphics/Renderer/Frame/FrameRenderer.h"
#include "Engine/Graphics/Renderer/Frame/FrameRendererUtil.h"
#include "Engine/Window/IWindow.h"

namespace sw
{
    SW_LOG_CALLER( "FrameRenderer" );

    void FrameRenderer::ensureTransientResources( uint32 overrideWidth, uint32 overrideHeight )
    {
        if ( _pDevice == nullptr )
            return;

        uint32 width  = FrameRendererUtil::kDefaultTransientSize;
        uint32 height = FrameRendererUtil::kDefaultTransientSize;
        if ( overrideWidth > 0 && overrideHeight > 0 )
        {
            width  = overrideWidth;
            height = overrideHeight;
        }
        else
        {
            IWindow* pWindow = IWindow::getActiveWindow();
            if ( pWindow != nullptr )
            {
                if ( pWindow->getWidth() > 0 )
                    width = pWindow->getWidth();
                if ( pWindow->getHeight() > 0 )
                    height = pWindow->getHeight();
            }
        }

        if ( width == _transientPool.getWidth() && height == _transientPool.getHeight() && _transientPool.isEmpty() == false )
            return;

        releaseTransientResources();
        _transientPool.setSize( width, height );

        for ( const RenderPassAttachment& att : _pipelineResource.getDesc()._listAttachment )
        {
            const RHIFormat format = FrameRendererUtil::parseAttachmentFormat( att._format );
            allocTransient( att._name, format, FrameRendererUtil::isDepthFormat( format ), att._clearColor );
        }

        auto ensureNamed = [&]( string_view name )
        {
            if ( _transientPool.contains( name ) || name == FrameRendererUtil::Attachment::kSwapchain )
                return;

            float4     clearColor{};
            const bool bHasClear = tryGetAttachmentClearColor( name, clearColor );

            if ( name == FrameRendererUtil::Attachment::kShadowMap || name == FrameRendererUtil::Attachment::kSceneDepth )
                allocTransient( name, RHIFormat::D24_UNORM_S8_UINT, true, bHasClear ? clearColor : FrameRendererUtil::kDepthClear );
            else if ( name == FrameRendererUtil::Attachment::kGBufferNormal || name == FrameRendererUtil::Attachment::kLitColor || name == FrameRendererUtil::Attachment::kBloomColor || name == FrameRendererUtil::Attachment::kBloomBright )
                allocTransient( name, RHIFormat::R16G16B16A16_FLOAT, false, bHasClear ? clearColor : FrameRendererUtil::kBloomClear );
            else if ( name == FrameRendererUtil::Attachment::kSceneColor )
                allocTransient( name, RHIFormat::R8G8B8A8_UNORM, false, bHasClear ? clearColor : FrameRendererUtil::kSceneClear );
            else
                allocTransient( name, RHIFormat::R8G8B8A8_UNORM, false, bHasClear ? clearColor : FrameRendererUtil::kBlackClear );
        };

        for ( const RenderGraphPassDesc& pass : _pipelineResource.getGraphPass() )
        {
            for ( const string& in : pass._listInput )
            {
                ensureNamed( in );
            }
            for ( const string& out : pass._listOutput )
            {
                ensureNamed( out );
            }
        }

        ensureTaaHistory();
        publishRenderTargets();
    }

    void FrameRenderer::ensureTaaHistory()
    {
        if ( _pDevice == nullptr || _taaHistory != 0 )
            return;

        // 파이프라인에 TAA 패스가 없으면 히스토리도 필요 없다.
        bool bHasTaaPass = false;
        for ( const RenderGraphPassDesc& pass : _pipelineResource.getGraphPass() )
        {
            if ( pass._resolvedType == RenderPassType::TAA )
            {
                bHasTaaPass = true;
                break;
            }
        }
        if ( bHasTaaPass == false )
            return;

        // 히스토리는 TAA 출력의 복사본이다 — CopyResource 는 포맷이 정확히 같아야 하므로 대상
        // 첨부의 포맷을 그대로 따라간다.
        const bool        bHasTaaColor = _transientPool.contains( string_view{ "TaaColor" } );
        const string_view taaTarget    = bHasTaaColor ? string_view{ "TaaColor" }
                                                      : string_view{ FrameRendererUtil::Attachment::kSceneColor };

        RHITextureDesc histDesc{};
        histDesc._width             = _transientPool.getWidth() != 0 ? _transientPool.getWidth() : FrameRendererUtil::kDefaultTransientSize;
        histDesc._height            = _transientPool.getHeight() != 0 ? _transientPool.getHeight() : FrameRendererUtil::kDefaultTransientSize;
        histDesc._format            = attachmentFormatOrDefault( taaTarget, constant::kBackBufferFormat );
        histDesc._bIsRenderTarget   = SW_TRUE;
        histDesc._bIsShaderResource = SW_TRUE;

        _taaHistory = _pDevice->getResource()->createTexture2D( histDesc );
        if ( _taaHistory != 0 )
            _taaHistorySrv = _pDevice->getResource()->registerBindlessTexture( _taaHistory );
    }

    void FrameRenderer::releaseTransientResources()
    {
        // 목록을 먼저 비운다 — 놓는 도중에 UI 가 죽은 핸들을 집어 가면 안 된다.
        if ( RenderTargetRegistry* pRegistry = engine::getRenderTargetRegistry(); pRegistry != nullptr )
            pRegistry->clear();

        if ( _pDevice == nullptr )
        {
            _transientPool.forget();
            _taaHistory    = 0;
            _taaHistorySrv = kInvalidDescriptorIndex;
            return;
        }

        // 히스토리는 TAA 출력의 복사본이라 크기가 정확히 같아야 한다(CopyResource 제약) — 트랜지언트가
        // 새 크기로 다시 잡히면 이것도 같이 버려야 ensureTaaHistory 가 새 크기로 다시 만든다.
        if ( _taaHistorySrv != kInvalidDescriptorIndex )
        {
            _pDevice->getResource()->unregisterBindlessTexture( _taaHistorySrv );
            _taaHistorySrv = kInvalidDescriptorIndex;
        }
        if ( _taaHistory != 0 )
        {
            _pDevice->getResource()->destroyTexture( _taaHistory );
            _taaHistory = 0;
        }

        _transientPool.release( _pDevice );
    }

    void FrameRenderer::allocTransient( string_view name, RHIFormat format, bool bDepth, const float4& clearColor )
    {
        _transientPool.alloc( _pDevice, name, format, bDepth, clearColor );
    }

    bool FrameRenderer::markAttachmentCleared( const hashed_string& key )
    {
        return _transientPool.markCleared( key );
    }

    void FrameRenderer::resetClearedAttachments()
    {
        _transientPool.resetCleared();
    }

    bool FrameRenderer::tryGetAttachmentClearColor( string_view attachmentName, float4& outClearColor ) const
    {
        for ( const RenderPassAttachment& att : _pipelineResource.getDesc()._listAttachment )
        {
            if ( att._name == attachmentName )
            {
                outClearColor = att._clearColor;
                return att._bClear;
            }
        }
        return false;
    }

    float4 FrameRenderer::getAttachmentClearColorOrDefault( string_view attachmentName, const float4& fallback ) const
    {
        float4 clearColor = fallback;
        tryGetAttachmentClearColor( attachmentName, clearColor );
        return clearColor;
    }

    TransientAttachmentPool::Attachment FrameRenderer::findTransientAttachment( string_view name ) const
    {
        return _transientPool.find( name );
    }

    RHITextureHandle FrameRenderer::findTransient( string_view name ) const
    {
        return _transientPool.findTexture( name );
    }

    void FrameRenderer::publishRenderTargets() const
    {
        RenderTargetRegistry* pRegistry = engine::getRenderTargetRegistry();
        if ( pRegistry == nullptr )
            return;

        // 화면에 나가는 것이 무엇인지 함께 실어 준다 — 패널이 열리자마자 **지금 보이는 그림**을
        // 고를 수 있어야 쓸모가 있다(이름순 첫 번째는 AOColor 라 아무 의미가 없다).
        const string_view presented = getPresentedAttachmentName();

        vector<RenderTargetInfo> listTarget;
        listTarget.reserve( _transientPool.getAll().size() );
        for ( const auto& [name, attachment] : _transientPool.getAll() )
        {
            if ( attachment._texture == 0 )
                continue;
            RenderTargetInfo info{};
            info._name       = name;
            info._bPresented = ( presented.empty() == false && name == presented ) ? SW_TRUE : SW_FALSE;
            info._texture    = attachment._texture;
            info._width      = _transientPool.getWidth();
            info._height     = _transientPool.getHeight();
            info._format     = attachmentFormatOrDefault( name, RHIFormat::R8G8B8A8_UNORM );
            info._bDepth     = FrameRendererUtil::isDepthFormat( info._format ) ? SW_TRUE : SW_FALSE;
            listTarget.push_back( std::move( info ) );
        }

        // 이름순으로 정렬해 둔다 — 해시맵 순서는 실행마다 달라서, 정렬하지 않으면 목록이 프레임마다
        // 흔들리는 것처럼 보이고 "어디 있었더라" 를 매번 다시 찾게 된다.
        std::sort( listTarget.begin(), listTarget.end(),
                   []( const RenderTargetInfo& lhs, const RenderTargetInfo& rhs )
        { return lhs._name < rhs._name; } );

        pRegistry->publish( std::move( listTarget ) );
    }

    string_view FrameRenderer::getPresentedAttachmentName() const
    {
        for ( const RenderGraphPassDesc& pass : _pipelineResource.getDesc()._listPass )
        {
            if ( pass._resolvedType == RenderPassType::Present && pass._listInput.empty() == false )
                return string_view{ pass._listInput.front() };
        }
        return string_view{};
    }

    string FrameRenderer::resolvePresentSource() const
    {
        // 파이프라인이 Present 의 입력을 선언했으면 그것이 정본이다 — 다른 풀스크린 패스와 같은 규칙.
        for ( const RenderGraphPassDesc& pass : _pipelineResource.getGraphPass() )
        {
            if ( pass._resolvedType != RenderPassType::Present )
                continue;
            for ( const RenderGraphPassDesc::ResolvedInput& input : pass._listResolvedInput )
            {
                if ( static_cast<RenderPassInputRole>( input._role ) == RenderPassInputRole::SourceColor && findTransient( input._attachment.view() ) != 0 )
                    return string( input._attachment.view() );
            }
        }
        // 선언이 없을 때의 폴백 — 가장 나중에 만들어지는 컬러부터.
        const utf8* pName = FrameRendererUtil::pickFirstExisting(
            _transientPool.getAll(),
            { "TonemapColor", "OutlineColor", "BloomColor", "TaaColor",
              "TransparentColor", "LitColor", "SceneColor", "GBufferAlbedo" } );
        return pName != nullptr ? string( pName ) : string{};
    }
} // namespace sw
