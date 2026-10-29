/**
 * @file FrameRendererTransients.cpp
 * @brief 프레임 첨부(트랜지언트 렌더 타깃)의 수명과 조회입니다. 파이프라인이 선언한 것을 창 크기로 만듭니다.
 * @details 첨부는 **구성이 바뀔 때만** 다시 만들어집니다(창 크기 · 파이프라인). 소유는 `TransientAttachmentPool` 이 하고
 *          여기서는 "무엇을 얼마나 만들지" 를 파이프라인 선언에서 읽어 채웁니다. 읽어 가는 쪽(리드백 · PPM 덤프)은
 *          프레임 경로가 아니라 GPU 를 기다리는 진단이라 `FrameCapture` 로 갈라 두었습니다.
 */
#include "pch.h"

#include "Engine/Foundation/EngineServices.h"
#include "Engine/Graphics/Debug/RenderTargetRegistry.h"
#include "Engine/Graphics/RHI/IRHIDevice.h"
#include "Engine/Graphics/RHI/IRHIResourceFactory.h"
#include "Engine/Renderer/Frame/FrameRenderer.h"
#include "Engine/Renderer/Frame/FrameRendererUtil.h"
#include "Engine/Renderer/Pipeline/RenderPassInputSignature.h"
#include "Engine/Renderer/Pipeline/RenderPassTypeInfo.h"
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
        if ( _viewResources.updateViewTransients( _mainView, poolWidth, poolHeight ) )
        {
            publishRenderTargets();
            reportRenderTargetMemory();
        }
        // Present 뷰포트(출력 × 사각형) — FSR 1 로 키우면 중간 RT 가 이 크기다(executePresentPass 의 뷰포트와 같은 식).
        const uint32 viewportWidth  = static_cast<uint32>( static_cast<float32>( width ) * settings._screenRect._z + 0.5f );
        const uint32 viewportHeight = static_cast<uint32>( static_cast<float32>( height ) * settings._screenRect._w + 0.5f );
        _postProcess.prepareUpscale( _mainView, viewportWidth, viewportHeight );
        _capture.ensurePresent( _outputWidth, _outputHeight );
    }

    void FrameRenderer::releaseTransientResources()
    {
        // 목록을 먼저 비운다. 놓는 도중에 UI 가 죽은 핸들을 집어 가면 안 된다.
        if ( RenderTargetRegistry* pRegistry = engine::findSubsystem<RenderTargetRegistry>(); pRegistry != nullptr )
            pRegistry->clear();

        _viewResources.releaseViewTransients( _mainView );
        for ( unique_ptr<FrameViewTarget>& pView : _listExtraView )
        {
            _viewResources.releaseViewTransients( *pView );
        }
        // 캡처도 트랜지언트와 크기가 같아야 한다. 같이 버리고 다음 ensureTransientResources 가 새 크기로 만든다.
        _capture.releasePresent();
    }

    void FrameRenderer::reportRenderTargetMemory() const
    {
#if SW_LOG_LEVEL_COMPILED( SW_LOG_VERBOSITY_INFO )
        // 주 시점의 첨부마다 크기를 남긴다(렌더 타깃 패널 `RenderTargetRegistry` 와 같은 목록). `[Profile] GPU memory by kind` 가 늘면 어느 첨부가 늘었는지 이 줄로 나눈다(측정 기준을 다시 뜰 때 함께 본다).
        // 풀을 다시 만들 때와 꺼져 있던 효과의 타깃을 처음 만들 때만 부른다.
        uint64 totalBytes = 0;
        for ( const auto& [name, attachment] : _mainView._transientPool.getAll() )
        {
            totalBytes += static_cast<uint64>( attachment._width ) * attachment._height * getRHIFormatBytesPerPixel( attachment._format );
        }
        const uint64 totalKilobytes = totalBytes / 1024u;
        SW_LOG_INFO( "[RenderTargets] main view %#x%#: %# KB in %# attachments", _mainView._transientPool.getWidth(), _mainView._transientPool.getHeight(),
                     totalKilobytes, _mainView._transientPool.getAll().size() );
        for ( const auto& [name, attachment] : _mainView._transientPool.getAll() )
        {
            const uint64 kilobytes = static_cast<uint64>( attachment._width ) * attachment._height * getRHIFormatBytesPerPixel( attachment._format ) / 1024u;
            SW_LOG_INFO( "[RenderTargets]   %#  %#x%#  %# KB", name, attachment._width, attachment._height, kilobytes );
        }
#endif
    }

    uint32 FrameRenderer::getShadowMapResolution()
    {
        return FrameViewResources::getShadowMapResolution();
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
        // 꺼진 효과의 출력이면 그것이 가리키는 원본이 화면에 나간다(`TransientAttachmentPool::forward`).
        const string_view presented = _mainView._transientPool.resolveName( getPresentedAttachmentName() );

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
