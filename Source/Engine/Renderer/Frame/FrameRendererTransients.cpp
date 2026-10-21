/**
 * @file FrameRendererTransients.cpp
 * @brief 프레임 첨부(트랜지언트 렌더 타깃)의 수명과 조회입니다. 파이프라인이 선언한 것을 창 크기로 만듭니다.
 * @details 첨부는 **구성이 바뀔 때만** 다시 만들어집니다(창 크기 · 파이프라인). 소유는 `TransientAttachmentPool` 이 하고
 *          여기서는 "무엇을 얼마나 만들지" 를 파이프라인 선언에서 읽어 채웁니다. 읽어 가는 쪽(리드백 · PPM 덤프)은
 *          프레임 경로가 아니라 GPU 를 기다리는 진단이라 `FrameCapture` 로 갈라 두었습니다.
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
        // 정수 업스케일이면 사각형 픽셀 / 배율(내림)이다 — Present 가 그 배로 키운다.
        const RenderViewSettings& settings   = _mainView._settings;
        const uint32              poolWidth  = FrameRendererUtil::computeViewPoolExtent( static_cast<float32>( width ) * settings._screenRect._z,
                                                                                         settings._resolutionScale, settings._upscaleZoom );
        const uint32              poolHeight = FrameRendererUtil::computeViewPoolExtent( static_cast<float32>( height ) * settings._screenRect._w,
                                                                                         settings._resolutionScale, settings._upscaleZoom );
        if ( ensureViewTransients( _mainView, poolWidth, poolHeight ) )
            publishRenderTargets();
        _capture.ensurePresent( _outputWidth, _outputHeight );
    }

    bool FrameRenderer::ensureViewTransients( ViewTarget& view, uint32 width, uint32 height )
    {
        TransientAttachmentPool& pool             = view._transientPool;
        const uint32             shadowResolution = getShadowMapResolution();
        const bool               bPoolUnchanged   = width == pool.getWidth() && height == pool.getHeight() && pool.isEmpty() == false && view._shadowMapResolution == shadowResolution;
        if ( bPoolUnchanged )
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

        // 패스가 쓰는 첨부는 모두 위에서 만들었다 — 파이프라인 로드가 선언하지 않은 입출력을 거절한다(`RenderPipelineAsset::validate`).
        // 그래서 코드가 이름으로 포맷 · 클리어 색을 짐작해 첨부를 더 만드는 길이 없다(Swapchain 은 첨부가 아니라 백버퍼다).
        ensureTaaHistory( view );
        ensureDepthHistory( view );
        // 새 풀의 장면 깊이에는 아직 아무것도 없다 — 이 뷰를 한 번 그릴 때까지 깊이 충돌은 보지 않는다.
        view._bLastFrameDepthValid = SW_FALSE;
        return true;
    }

    void FrameRenderer::ensureDepthHistory( ViewTarget& view )
    {
        if ( _pDevice == nullptr || view._depthHistory != 0 )
            return;
        const TransientAttachmentPool::Attachment depth = view._transientPool.find( attachmentNames()._sceneDepth.view() );
        if ( depth._texture == 0 )
            return; // 장면 깊이가 없는 파이프라인(2D) — 옮길 깊이가 없다

        RHITextureDesc historyDesc{};
        historyDesc._width              = depth._width;
        historyDesc._height             = depth._height;
        historyDesc._format             = RHIFormat::R32_FLOAT;
        historyDesc._bIsRenderTarget    = SW_TRUE;
        historyDesc._bIsShaderResource  = SW_TRUE;
        historyDesc._bIsUnorderedAccess = SW_TRUE;
        view._depthHistory              = _pDevice->getResourceFactory()->createTexture2D( historyDesc );
        if ( view._depthHistory != 0 )
            view._depthHistoryUav = _pDevice->getResourceFactory()->registerBindlessTextureUav( view._depthHistory );
        if ( view._depthHistoryUav == kInvalidDescriptorIndex )
            SW_LOG_ERROR( "Failed to create the depth history (%#x%#, R32 float RT + UAV) - GPU particles do not collide with the scene depth", depth._width, depth._height );
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

        // 히스토리는 TAA 출력의 복사본이다. CopyResource 는 포맷이 정확히 같아야 하므로 대상 첨부의 포맷(선언에서 만든 것)을 그대로 따라간다.
        // 대상은 TAA 패스가 선언한 출력 중 있는 것이다(실행과 같은 규칙). 이름으로 짐작하지 않는다 — 없으면 히스토리를 만들지 않고 알린다.
        const TransientAttachmentPool&      pool = view._transientPool;
        TransientAttachmentPool::Attachment taaTarget{};
        for ( const hashed_string& output : pTaaPass->_listResolvedOutput )
        {
            taaTarget = pool.find( output.view() );
            if ( taaTarget._texture != 0 )
                break;
        }
        if ( taaTarget._texture == 0 )
        {
            SW_LOG_ERROR( "Pipeline '%#': the TAA pass has no declared output attachment - no TAA history", _pipelineResource.getDesc()._name.c_str() );
            return;
        }

        RHITextureDesc historyDesc{};
        historyDesc._width             = pool.getWidth() != 0 ? pool.getWidth() : FrameRendererUtil::kDefaultTransientSize;
        historyDesc._height            = pool.getHeight() != 0 ? pool.getHeight() : FrameRendererUtil::kDefaultTransientSize;
        historyDesc._format            = taaTarget._format;
        historyDesc._bIsRenderTarget   = SW_TRUE;
        historyDesc._bIsShaderResource = SW_TRUE;

        view._taaHistory = _pDevice->getResourceFactory()->createTexture2D( historyDesc );
        if ( view._taaHistory != 0 )
            view._taaHistorySrv = _pDevice->getResourceFactory()->registerBindlessTexture( view._taaHistory );
    }

    void FrameRenderer::releaseTransientResources()
    {
        // 목록을 먼저 비운다. 놓는 도중에 UI 가 죽은 핸들을 집어 가면 안 된다.
        if ( RenderTargetRegistry* pRegistry = engine::findSubsystem<RenderTargetRegistry>(); pRegistry != nullptr )
            pRegistry->clear();

        releaseViewTransients( _mainView );
        for ( unique_ptr<ViewTarget>& pView : _listExtraView )
        {
            releaseViewTransients( *pView );
        }
        // 캡처도 트랜지언트와 크기가 같아야 한다. 같이 버리고 다음 ensureTransientResources 가 새 크기로 만든다.
        _capture.releasePresent();
    }

    void FrameRenderer::releaseViewTransients( ViewTarget& view )
    {
        if ( _pDevice == nullptr )
        {
            view._transientPool.forget();
            view._taaHistory           = 0;
            view._taaHistorySrv        = kInvalidDescriptorIndex;
            view._depthHistory         = 0;
            view._depthHistoryUav      = kInvalidDescriptorIndex;
            view._bLastFrameDepthValid = SW_FALSE;
            return;
        }

        // 깊이 히스토리도 풀 크기를 따른다. UAV 등록을 먼저 놓고 텍스처를 지운다(버퍼 슬롯과 같은 순서 — 레지스트리에 죽은 핸들을 남기지 않는다).
        if ( view._depthHistoryUav != kInvalidDescriptorIndex )
            _pDevice->getResourceFactory()->unregisterBindlessUav( view._depthHistoryUav );
        if ( view._depthHistory != 0 )
            _pDevice->getResourceFactory()->destroyTexture( view._depthHistory );
        view._depthHistory         = 0;
        view._depthHistoryUav      = kInvalidDescriptorIndex;
        view._bLastFrameDepthValid = SW_FALSE;

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
        // 값의 출처는 설정 스키마 하나다. 여기서는 텍스처를 만들 수 있는 범위만 지킨다(1 미만 · 밉 상한을 넘는 한 변은 첨부를 만들지 못한다).
        constexpr int32 kMaxResolution = static_cast<int32>( 1u << ( constant::kMaxTextureMipCount - 1 ) );
        return static_cast<uint32>( MathUtil::clamp( static_cast<int32>( gv_shadowMapResolution ), 1, kMaxResolution ) );
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

    float4 FrameRenderer::getTargetClearColor( string_view attachmentName ) const
    {
        // 첨부의 클리어 색은 파이프라인 XML 의 선언 하나가 원본이다(풀이 할당할 때 선언 값을 든다). 패스마다 코드가 따로 적던
        // 기본값(뎁스 1, 노멀 0.5 · 0.5 · 1, SSAO 흰색)은 선언과 갈라질 수 있어 지웠다. 풀에 없는 이름은 백버퍼(핸들 0)이고,
        // 백버퍼의 클리어 색은 렌더러의 클리어 색(SceneColor 선언에서 온다) 하나다.
        const TransientAttachmentPool::Attachment attachment = activePool().find( attachmentName );
        return attachment._texture != 0 ? attachment._clearColor : _clearColor;
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
        RenderTargetRegistry* pRegistry = engine::findSubsystem<RenderTargetRegistry>();
        // 목록은 주 렌더러의 것이다 — 미리보기 씬 렌더러가 공개하면 렌더 타깃 패널이 미리보기 첨부를 보인다.
        if ( pRegistry == nullptr || _bScenePreview == SW_TRUE )
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
            info._format     = attachment._format;
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
        // Present 의 SourceColor 입력이 원본이다(계약의 필수 입력 — 로드가 선언을 요구한다). 이름 후보로 "가장 나중 컬러" 를 짐작하지 않는다.
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
        return string_view{};
    }
} // namespace sw
