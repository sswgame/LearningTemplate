/**
 * @file FrameRendererTransients.cpp
 * @brief 프레임 첨부(트랜지언트 렌더 타깃)의 수명과 조회입니다. 파이프라인이 선언한 것을 창 크기로 만듭니다.
 * @details 첨부는 **구성이 바뀔 때만** 다시 만들어집니다(창 크기 · 파이프라인). 소유는 `TransientAttachmentPool` 이 하고
 *          여기서는 "무엇을 얼마나 만들지" 를 파이프라인 선언에서 읽어 채웁니다. 읽어 가는 쪽(리드백 · PPM 덤프)은
 *          프레임 경로가 아니라 GPU 를 기다리는 진단이라 `FrameRendererReadback.cpp` 로 갈라 두었습니다.
 */
#include "pch.h"

#include "Engine/Common/EngineServices.h"
#include "Engine/Graphics/Debug/RenderTargetRegistry.h"
#include "Engine/Graphics/RHI/IRHIDevice.h"
#include "Engine/Graphics/RHI/IRHIResourceFactory.h"
#include "Engine/Renderer/Frame/FrameRenderer.h"
#include "Engine/Renderer/Frame/FrameRendererUtil.h"
#include "Engine/Renderer/Pipeline/RenderPassInputSignature.h"
#include "Engine/UserSettings/UserSettingsVariables.h"

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
            // 창이 아니라 **스왑체인**의 크기다. 렌더러는 OS 창을 모른다(IRHIDevice::resize 참고).
            if ( _pDevice->getBackBufferWidth() > 0 )
                width = _pDevice->getBackBufferWidth();
            if ( _pDevice->getBackBufferHeight() > 0 )
                height = _pDevice->getBackBufferHeight();
        }
        _outputWidth  = width;
        _outputHeight = height;

        // 주 시점의 풀은 출력 × 사각형 × 해상도 배율이다(분할 화면의 한 칸 · 낮춘 해상도). 비율을 지키려고 둘 다 같은 배율로 줄인다.
        const RenderViewSettings& settings   = _mainView._settings;
        const uint32              poolWidth  = MathUtil::max( 1u, static_cast<uint32>( static_cast<float32>( width ) * settings._screenRect._z * settings._resolutionScale + 0.5f ) );
        const uint32              poolHeight = MathUtil::max( 1u, static_cast<uint32>( static_cast<float32>( height ) * settings._screenRect._w * settings._resolutionScale + 0.5f ) );
        if ( ensureViewTransients( _mainView, poolWidth, poolHeight ) )
            publishRenderTargets();
        ensurePresentCapture();
    }

    bool FrameRenderer::ensureViewTransients( ViewTarget& view, uint32 width, uint32 height )
    {
        TransientAttachmentPool& pool             = view._transientPool;
        const uint32             shadowResolution = getShadowMapResolution();
        if ( width == pool.getWidth() && height == pool.getHeight() && pool.isEmpty() == false && view._shadowMapResolution == shadowResolution )
            return false;

        releaseViewTransients( view );
        pool.setSize( width, height );
        view._shadowMapResolution = shadowResolution;

        // 그림자 맵은 화면이 아니라 빛의 볼륨을 담는다 — 프레임 크기 대신 정사각 고정 크기로 만든다. 이름이 아니라 역할로 가린다(이름을 바꾼 그림자 첨부도 같다).
        for ( const RenderPassAttachment& attachment : _pipelineResource.getDesc()._listAttachment )
        {
            const RHIFormat format = FrameRendererUtil::parseAttachmentFormat( attachment._format );
            const bool      bDepth = FrameRendererUtil::isDepthFormat( format );
            if ( resolveRenderPassInputRole( attachment._name, bDepth, attachment._role ) == RenderPassInputRole::ShadowMap )
                (void)pool.allocateSized( _pDevice, attachment._name, format, bDepth, attachment._clearColor, shadowResolution, shadowResolution );
            else
                allocateTransient( pool, attachment._name, format, bDepth, attachment._clearColor, attachment._resolutionDivisor );
        }

        auto ensureNamed = [&]( string_view name )
        {
            if ( pool.contains( name ) || name == FrameRendererUtil::Attachment::kSwapchain )
                return;

            float4     clearColor{};
            const bool bHasClear = tryGetAttachmentClearColor( name, clearColor );

            if ( name == FrameRendererUtil::Attachment::kShadowMap )
                (void)pool.allocateSized( _pDevice, name, RHIFormat::D24_UNORM_S8_UINT, true, bHasClear ? clearColor : FrameRendererUtil::kDepthClear,
                                          shadowResolution, shadowResolution );
            else if ( name == FrameRendererUtil::Attachment::kSceneDepth )
                allocateTransient( pool, name, RHIFormat::D24_UNORM_S8_UINT, true, bHasClear ? clearColor : FrameRendererUtil::kDepthClear );
            else if ( name == FrameRendererUtil::Attachment::kGBufferNormal || name == FrameRendererUtil::Attachment::kLitColor || name == FrameRendererUtil::Attachment::kBloomColor || name == FrameRendererUtil::Attachment::kBloomBright )
                allocateTransient( pool, name, RHIFormat::R16G16B16A16_FLOAT, false, bHasClear ? clearColor : FrameRendererUtil::kBloomClear );
            else if ( name == FrameRendererUtil::Attachment::kSceneColor )
                allocateTransient( pool, name, RHIFormat::R8G8B8A8_UNORM, false, bHasClear ? clearColor : FrameRendererUtil::kSceneClear );
            else
                allocateTransient( pool, name, RHIFormat::R8G8B8A8_UNORM, false, bHasClear ? clearColor : FrameRendererUtil::kBlackClear );
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

        ensureTaaHistory( view );
        return true;
    }

    void FrameRenderer::ensureTaaHistory( ViewTarget& view )
    {
        if ( _pDevice == nullptr || view._taaHistory != 0 )
            return;

        // 파이프라인에 TAA 패스가 없으면 히스토리도 필요 없다.
        const RenderGraphPassDesc* pTaaPass = nullptr;
        for ( const RenderGraphPassDesc& pass : _pipelineResource.getGraphPass() )
        {
            if ( pass._resolvedType == RenderPassType::TAA )
            {
                pTaaPass = &pass;
                break;
            }
        }
        if ( pTaaPass == nullptr )
            return;

        // 히스토리는 TAA 출력의 복사본이다. CopyResource 는 포맷이 정확히 같아야 하므로 대상 첨부의 포맷을 그대로 따라간다.
        // 대상은 TAA 패스가 선언한 출력 중 있는 것이다(실행과 같은 규칙). 아래 이름은 선언이 없을 때의 폴백이다.
        const TransientAttachmentPool& pool      = view._transientPool;
        string_view                    taaTarget = pool.contains( string_view{ "TaaColor" } ) ? string_view{ "TaaColor" }
                                                                                              : string_view{ FrameRendererUtil::Attachment::kSceneColor };
        for ( const hashed_string& output : pTaaPass->_listResolvedOutput )
        {
            if ( pool.contains( output.view() ) )
            {
                taaTarget = output.view();
                break;
            }
        }

        RHITextureDesc historyDesc{};
        historyDesc._width             = pool.getWidth() != 0 ? pool.getWidth() : FrameRendererUtil::kDefaultTransientSize;
        historyDesc._height            = pool.getHeight() != 0 ? pool.getHeight() : FrameRendererUtil::kDefaultTransientSize;
        historyDesc._format            = attachmentFormatOrDefault( taaTarget, constant::kBackBufferFormat );
        historyDesc._bIsRenderTarget   = SW_TRUE;
        historyDesc._bIsShaderResource = SW_TRUE;

        view._taaHistory = _pDevice->getResourceFactory()->createTexture2D( historyDesc );
        if ( view._taaHistory != 0 )
            view._taaHistorySrv = _pDevice->getResourceFactory()->registerBindlessTexture( view._taaHistory );
    }

    void FrameRenderer::setPresentCaptureEnabled( bool bEnabled )
    {
        _bPresentCaptureEnabled = bEnabled ? SW_TRUE : SW_FALSE;
        if ( bEnabled == false )
        {
            if ( _presentCapture != 0 && _pDevice != nullptr )
            {
                _pDevice->getResourceFactory()->destroyTexture( _presentCapture );
                _presentCapture = 0;
            }
            return;
        }
        // **여기서 만들어야 한다.** 트랜지언트 할당은 크기가 그대로면 통째로 건너뛰므로, 켜는
        // 시점이 그 뒤면(커맨드라인은 프레임이 돌기 시작한 뒤에야 반영된다) 영영 안 만들어진다.
        ensurePresentCapture();
    }

    void FrameRenderer::ensurePresentCapture()
    {
        if ( _pDevice == nullptr || _bPresentCaptureEnabled == SW_FALSE )
            return;
        // 캡처는 주 출력과 크기가 같아야 한다(주 시점 · 화면 사각형 뷰가 그 안에 그린다). 출력 크기가 바뀌었으면 다시 만든다.
        const uint32 outputWidth  = _outputWidth != 0 ? _outputWidth : FrameRendererUtil::kDefaultTransientSize;
        const uint32 outputHeight = _outputHeight != 0 ? _outputHeight : FrameRendererUtil::kDefaultTransientSize;
        if ( _presentCapture != 0 )
        {
            if ( _presentCaptureWidth == outputWidth && _presentCaptureHeight == outputHeight )
                return;
            _pDevice->getResourceFactory()->destroyTexture( _presentCapture );
            _presentCapture = 0;
        }

        // 포맷은 **계약값**이다. 백버퍼가 실제로 무엇을 채택했든(Vulkan 은 서피스 협상 결과) 캡처는
        // 늘 같은 포맷이라 PPM 으로 푸는 쪽이 한 가지만 알면 된다. Present PSO 변종에도 이 포맷이 있다.
        RHITextureDesc captureDesc{};
        captureDesc._width             = outputWidth;
        captureDesc._height            = outputHeight;
        captureDesc._format            = constant::kBackBufferFormat;
        captureDesc._bIsRenderTarget   = SW_TRUE;
        captureDesc._bIsShaderResource = SW_TRUE;
        _presentCapture                = _pDevice->getResourceFactory()->createTexture2D( captureDesc );
        _presentCaptureWidth           = outputWidth;
        _presentCaptureHeight          = outputHeight;
    }

    void FrameRenderer::releaseTransientResources()
    {
        // 목록을 먼저 비운다. 놓는 도중에 UI 가 죽은 핸들을 집어 가면 안 된다.
        if ( RenderTargetRegistry* pRegistry = engine::getRenderTargetRegistry(); pRegistry != nullptr )
            pRegistry->clear();

        releaseViewTransients( _mainView );
        for ( unique_ptr<ViewTarget>& pView : _listExtraView )
        {
            releaseViewTransients( *pView );
        }
        // 캡처도 트랜지언트와 크기가 같아야 한다. 같이 버리고 ensurePresentCapture 가 새 크기로 만든다.
        if ( _presentCapture != 0 && _pDevice != nullptr )
            _pDevice->getResourceFactory()->destroyTexture( _presentCapture );
        _presentCapture = 0;
    }

    void FrameRenderer::releaseViewTransients( ViewTarget& view )
    {
        if ( _pDevice == nullptr )
        {
            view._transientPool.forget();
            view._taaHistory    = 0;
            view._taaHistorySrv = kInvalidDescriptorIndex;
            return;
        }

        // 히스토리는 TAA 출력의 복사본이라 크기가 정확히 같아야 한다(CopyResource 제약). 트랜지언트가
        // 새 크기로 다시 잡히면 이것도 같이 버려야 ensureTaaHistory 가 새 크기로 다시 만든다.
        if ( view._taaHistorySrv != kInvalidDescriptorIndex )
        {
            _pDevice->getResourceFactory()->unregisterBindlessTexture( view._taaHistorySrv );
            view._taaHistorySrv = kInvalidDescriptorIndex;
        }
        if ( view._taaHistory != 0 )
        {
            _pDevice->getResourceFactory()->destroyTexture( view._taaHistory );
            view._taaHistory = 0;
        }
        view._transientPool.release( _pDevice );
    }

    void FrameRenderer::allocateTransient( TransientAttachmentPool& pool, string_view name, RHIFormat format, bool bDepth, const float4& clearColor,
                                           uint32 resolutionDivisor )
    {
        pool.allocate( _pDevice, name, format, bDepth, clearColor, resolutionDivisor );
    }

    uint32 FrameRenderer::getShadowMapResolution()
    {
        constexpr uint32 kArrResolution[] = { 1024u, 1536u, 2048u, 4096u };
        const int32      quality          = MathUtil::clamp( static_cast<int32>( gv_shadowQuality ), 0, 3 );
        return kArrResolution[quality];
    }

    bool FrameRenderer::markAttachmentCleared( const hashed_string& key )
    {
        return activePool().markCleared( key );
    }

    void FrameRenderer::resetClearedAttachments()
    {
        activePool().resetCleared();
    }

    bool FrameRenderer::tryGetAttachmentClearColor( string_view attachmentName, float4& outClearColor ) const
    {
        for ( const RenderPassAttachment& attachment : _pipelineResource.getDesc()._listAttachment )
        {
            if ( attachment._name == attachmentName )
            {
                outClearColor = attachment._clearColor;
                return attachment._bClear;
            }
        }
        return false;
    }

    float4 FrameRenderer::getAttachmentClearColorOrDefault( string_view attachmentName, const float4& fallback ) const
    {
        float4 clearColor = fallback;
        (void)tryGetAttachmentClearColor( attachmentName, clearColor ); // 선언이 없으면 fallback
        return clearColor;
    }

    TransientAttachmentPool::Attachment FrameRenderer::findTransientAttachment( string_view name ) const
    {
        return activePool().find( name );
    }

    RHITextureHandle FrameRenderer::findTransient( string_view name ) const
    {
        return activePool().findTexture( name );
    }

    void FrameRenderer::publishRenderTargets() const
    {
        RenderTargetRegistry* pRegistry = engine::getRenderTargetRegistry();
        if ( pRegistry == nullptr )
            return;

        // 화면에 나가는 것이 무엇인지 함께 실어 준다. 패널이 열리자마자 **지금 보이는 그림**을
        // 고를 수 있어야 쓸모가 있다(이름순 첫 번째는 AOColor 라 아무 의미가 없다).
        const string_view presented = getPresentedAttachmentName();

        vector<RenderTargetInfo> listTarget;
        listTarget.reserve( _mainView._transientPool.getAll().size() );
        for ( const auto& [name, attachment] : _mainView._transientPool.getAll() )
        {
            if ( attachment._texture == 0 )
                continue;
            RenderTargetInfo info{};
            info._name       = name;
            info._bPresented = ( presented.empty() == false && name == presented ) ? SW_TRUE : SW_FALSE;
            info._texture    = attachment._texture;
            info._width      = attachment._width;
            info._height     = attachment._height;
            info._format     = attachmentFormatOrDefault( name, RHIFormat::R8G8B8A8_UNORM );
            info._bDepth     = FrameRendererUtil::isDepthFormat( info._format ) ? SW_TRUE : SW_FALSE;
            listTarget.push_back( std::move( info ) );
        }

        // 이름순으로 정렬해 둔다. 해시맵 순서는 실행마다 달라서, 정렬하지 않으면 목록이 프레임마다
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

    string_view FrameRenderer::resolvePresentSource() const
    {
        // 파이프라인이 Present 의 입력을 선언했으면 그것이 기준이다. 다른 풀스크린 패스와 같은 규칙이다.
        for ( const RenderGraphPassDesc& pass : _pipelineResource.getGraphPass() )
        {
            if ( pass._resolvedType != RenderPassType::Present )
                continue;
            for ( const RenderGraphPassDesc::ResolvedAttachment& input : pass._listResolvedInput )
            {
                if ( static_cast<RenderPassInputRole>( input._role ) == RenderPassInputRole::SourceColor && findTransient( input._attachment.view() ) != 0 )
                    return input._attachment.view();
            }
        }
        // 선언이 없을 때의 폴백. 가장 나중에 만들어지는 컬러부터 본다.
        const utf8* pName = FrameRendererUtil::pickFirstExisting(
            activePool().getAll(),
            { "TonemapColor", "OutlineColor", "BloomColor", "TaaColor",
              "TransparentColor", "LitColor", "SceneColor", "GBufferAlbedo" } );
        return pName != nullptr ? string_view{ pName } : string_view{};
    }
} // namespace sw
