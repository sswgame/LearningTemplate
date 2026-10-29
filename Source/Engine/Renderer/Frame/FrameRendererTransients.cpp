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

    namespace
    {
        struct FrameRendererTransientsInternal
        {
            /** @brief @p name 을 쓰는(출력으로 선언한) 패스 수입니다. */
            static uint32 countWriters( const vector<RenderGraphPassDesc>& listPass, const hashed_string& name )
            {
                uint32 count = 0;
                for ( const RenderGraphPassDesc& pass : listPass )
                {
                    count += VectorUtil::contains( pass._listResolvedOutput, name ) ? 1u : 0u;
                }
                return count;
            }

            /**
             * @brief @p name 이 쓰는 패스가 있고 모두 일반 풀스크린 패스면 true 입니다 — 풀을 만들 때는 그 타깃을 만들지 않고, 그 패스가 이 뷰에서 처음 돌 때 만든다(`applyViewPassSelection`).
             * @details 지오메트리 · TAA 처럼 늘 도는 패스의 출력과, 셋업이 미리 읽는 첨부(TAA 출력 · 장면 깊이)는 여기 들지 않는다.
             */
            static bool isMadeOnFirstUse( const vector<RenderGraphPassDesc>& listPass, const hashed_string& name )
            {
                bool bWritten = false;
                for ( const RenderGraphPassDesc& pass : listPass )
                {
                    if ( VectorUtil::contains( pass._listResolvedOutput, name ) == false )
                        continue;
                    if ( getRenderPassTypeInfo( pass._resolvedType ).hasFlag( RenderPassTraitFlag::kGenericFullscreen ) == false )
                        return false;
                    bWritten = true;
                }
                return bWritten;
            }

            /**
             * @brief 이 뷰에서 빠질 수 있는 패스면 true 입니다 — 일반 풀스크린 패스이거나 꺼지면 원본을 가리키는 패스(TAA)이고, 컬러 출력을 모두 혼자 쓴다.
             * @details 출력을 혼자 써야 그 이름을 원본에 잇거나 비워도 다른 쓰기와 섞이지 않는다(꺼질 수 있는 효과는 로드 검증 3-6 · 3-7 이 그것을 요구한다).
             */
            static bool isSelectable( const vector<RenderGraphPassDesc>& listPass, const RenderGraphPassDesc& pass )
            {
                const RenderPassTypeInfo& info            = getRenderPassTypeInfo( pass._resolvedType );
                const bool                bSelectableType = info.hasFlag( RenderPassTraitFlag::kGenericFullscreen ) || info.hasFlag( RenderPassTraitFlag::kBypassesToSource );
                if ( bSelectableType == false || pass._listResolvedColorOutput.empty() )
                    return false;
                return std::all_of( pass._listResolvedColorOutput.begin(), pass._listResolvedColorOutput.end(),
                                    [&listPass]( const RenderGraphPassDesc::ResolvedAttachment& output )
                { return countWriters( listPass, output._attachment ) == 1; } );
            }

            /** @brief 파이프라인에 이 패스의 컬러 출력을 읽는 다른 패스가 있으면 true 입니다. 없으면(디버그 · 스크린샷용 출력) 읽는 쪽이 없다고 빼지 않는다. */
            static bool isConsumed( const vector<RenderGraphPassDesc>& listPass, const RenderGraphPassDesc& pass )
            {
                for ( const RenderGraphPassDesc::ResolvedAttachment& output : pass._listResolvedColorOutput )
                {
                    for ( const RenderGraphPassDesc& other : listPass )
                    {
                        const bool bReads = std::any_of( other._listResolvedInput.begin(), other._listResolvedInput.end(),
                                                         [&output]( const RenderGraphPassDesc::ResolvedAttachment& input )
                        { return input._attachment == output._attachment; } );
                        if ( &other != &pass && bReads )
                            return true;
                    }
                }
                return false;
            }

            /** @brief 파이프라인이 선언한 첨부 가운데 @p name 의 자리입니다. 선언되지 않은 이름(Swapchain)이면 첨부 수입니다. */
            static size_t findAttachmentIndex( const vector<RenderPassAttachment>& listAttachment, const hashed_string& name )
            {
                size_t index = 0;
                while ( index < listAttachment.size() && string_view( listAttachment[index]._name ) != name.view() )
                {
                    ++index;
                }
                return index;
            }

            /** @brief @p name 을 이번 프레임에 "읽힌다" 로 적습니다. 플래그는 선언한 첨부 자리마다 하나다(선언 밖 이름은 적지 않는다). */
            static void markNeeded( const vector<RenderPassAttachment>& listAttachment, const hashed_string& name, vector<uint8>& outListNeeded )
            {
                const size_t index = findAttachmentIndex( listAttachment, name );
                if ( index < outListNeeded.size() )
                    outListNeeded[index] = SW_TRUE;
            }

            /** @brief @p name 이 이번 프레임에 "읽힌다" 로 적혀 있으면 true 입니다. */
            static bool isNeeded( const vector<RenderPassAttachment>& listAttachment, const hashed_string& name, const vector<uint8>& listNeeded )
            {
                const size_t index = findAttachmentIndex( listAttachment, name );
                return index < listNeeded.size() && listNeeded[index] == SW_TRUE;
            }

            /** @brief 도는 패스가 읽는 첨부를 "읽힌다" 로 적습니다. @p bIncludeOutputs 면 쓰는 첨부도 적는다(늘 도는 패스는 앞 판을 불러 이어 그릴 수 있다). */
            static void markReads( const vector<RenderPassAttachment>& listAttachment, const RenderGraphPassDesc& pass, bool bIncludeOutputs, vector<uint8>& outListNeeded )
            {
                for ( const RenderGraphPassDesc::ResolvedAttachment& input : pass._listResolvedInput )
                {
                    markNeeded( listAttachment, input._attachment, outListNeeded );
                }
                if ( pass._resolvedDepthAttachment.empty() == false )
                    markNeeded( listAttachment, pass._resolvedDepthAttachment, outListNeeded );
                if ( bIncludeOutputs == false )
                    return;
                for ( const hashed_string& output : pass._listResolvedOutput )
                {
                    markNeeded( listAttachment, output, outListNeeded );
                }
            }

            /** @brief 패스의 원본(SourceColor 입력)입니다. 없으면 nullptr 입니다(꺼지면 원본을 가리키는 타입은 로드가 막는다). */
            static const hashed_string* findSource( const RenderGraphPassDesc& pass )
            {
                for ( const RenderGraphPassDesc::ResolvedAttachment& input : pass._listResolvedInput )
                {
                    if ( static_cast<RenderPassInputRole>( input._role ) == RenderPassInputRole::SourceColor )
                        return &input._attachment;
                }
                return nullptr;
            }

            /**
             * @brief 이 뷰에서 이번 프레임에 그 효과(`kSkipsWithoutPostProcess`)가 꺼졌으면 true 입니다. 꺼진 효과는 돌려도 원본(SSAO 는 가림 없음)과 같은 그림이다.
             * @param bCameraStill 이번 뷰-투영이 지난번 것과 같거나 기록을 버리는 프레임이다. 모션 블러는 카메라 움직임만 흐리므로(motionblur.hlsl — 물체 속도 버퍼가 없다) 속도가 모두 0 이다.
             */
            static bool isEffectOff( const RenderGraphPassDesc& pass, const RenderViewSettings& settings, bool bCameraStill )
            {
                if ( settings.usesPostProcess() == false || pass._minPostQuality > static_cast<int32>( gv_postQuality ) )
                    return true;
                if ( pass._resolvedType == RenderPassType::DepthOfField )
                    return settings._depthOfField.hasDepthOfField() == false || settings._projectionScaleY <= 0.0f; // 핀홀(조리개 0) · 직교
                if ( pass._resolvedType == RenderPassType::MotionBlur )
                    return static_cast<bool>( gv_motionBlur ) == false || bCameraStill;
                return false;
            }
        };
    } // namespace
} // namespace sw

namespace sw
{
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
        if ( updateViewTransients( _mainView, poolWidth, poolHeight ) )
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

    bool FrameRenderer::updateViewTransients( FrameViewTarget& view, uint32 width, uint32 height )
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
        // 일반 풀스크린 패스만 쓰는 첨부(후처리 효과 · 톤맵의 출력)는 여기서 만들지 않는다 — 그 패스가 이 뷰에서 처음 돌 때 만든다(`applyViewPassSelection`).
        for ( const RenderPassAttachment& attachment : _pipelineResource.getDesc()._listAttachment )
        {
            if ( FrameRendererTransientsInternal::isMadeOnFirstUse( _pipelineResource.getGraphPass(), hashed_string( attachment._name.c_str() ) ) )
                continue;
            const RHIFormat format = FrameRendererUtil::parseAttachmentFormat( attachment._format );
            const bool      bDepth = FrameRendererUtil::isDepthFormat( format );
            if ( resolveRenderPassInputRole( attachment._name, bDepth, attachment._role ) == RenderPassInputRole::ShadowMap )
                (void)pool.allocateSized( _pDevice, attachment._name, format, bDepth, attachment._clearColor, shadowResolution, shadowResolution );
            else
                allocateTransient( pool, attachment._name, format, bDepth, attachment._clearColor, attachment._resolutionDivisor );
        }

        // 패스가 쓰는 첨부는 모두 위에서 만들었다 — 파이프라인 로드가 선언하지 않은 입출력을 거절한다(`RenderPipelineAsset::validate`).
        // 그래서 코드가 이름으로 포맷 · 클리어 색을 짐작해 첨부를 더 만드는 길이 없다(Swapchain 은 첨부가 아니라 백버퍼다).
        ensureTAAHistory( view );
        ensureDepthHistory( view );
        ensureOcclusionTiles( view );
        _postProcess.ensureAutoExposureHistory( view );
        // 새 풀의 장면 깊이에는 아직 아무것도 없다 — 이 뷰를 한 번 그릴 때까지 깊이 충돌은 보지 않는다. 시간 누적 기록(TAA · 지난 뷰-투영 · 노출)도 새것이라 첫 프레임은 버린다.
        view._bLastFrameDepthValid = SW_FALSE;
        view._bHasHistory          = SW_FALSE;
        return true;
    }

    void FrameRenderer::ensureDepthHistory( FrameViewTarget& view )
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
            view._depthHistoryUAV = _pDevice->getResourceFactory()->registerBindlessTextureUAV( view._depthHistory );
        if ( view._depthHistoryUAV == kInvalidDescriptorIndex )
            SW_LOG_ERROR( "Failed to create the depth history (%#x%#, R32 float RT + UAV) - GPU particles do not collide with the scene depth", depth._width, depth._height );
    }

    void FrameRenderer::ensureTAAHistory( FrameViewTarget& view )
    {
        if ( _pDevice == nullptr || view._taaHistory != 0 )
            return;

        // 파이프라인에 TAA 패스가 없으면 히스토리도 필요 없다.
        const RenderGraphPassDesc* pTAAPass = nullptr;
        for ( const RenderGraphPassDesc& pass : _pipelineResource.getGraphPass() )
        {
            if ( pass._resolvedType == RenderPassType::TAA )
            {
                pTAAPass = &pass;
                break;
            }
        }
        if ( pTAAPass == nullptr )
            return;

        // 히스토리는 TAA 출력의 복사본이다. CopyResource 는 포맷이 정확히 같아야 하므로 대상 첨부의 포맷(선언에서 만든 것)을 그대로 따라간다.
        // 대상은 TAA 패스가 선언한 출력 중 있는 것이다(실행과 같은 규칙). 이름으로 짐작하지 않는다 — 없으면 히스토리를 만들지 않고 알린다.
        const TransientAttachmentPool&      pool = view._transientPool;
        TransientAttachmentPool::Attachment taaTarget{};
        for ( const hashed_string& output : pTAAPass->_listResolvedOutput )
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
            view._taaHistorySRV = _pDevice->getResourceFactory()->registerBindlessTexture( view._taaHistory );
    }

    void FrameRenderer::releaseTransientResources()
    {
        // 목록을 먼저 비운다. 놓는 도중에 UI 가 죽은 핸들을 집어 가면 안 된다.
        if ( RenderTargetRegistry* pRegistry = engine::findSubsystem<RenderTargetRegistry>(); pRegistry != nullptr )
            pRegistry->clear();

        releaseViewTransients( _mainView );
        for ( unique_ptr<FrameViewTarget>& pView : _listExtraView )
        {
            releaseViewTransients( *pView );
        }
        // 캡처도 트랜지언트와 크기가 같아야 한다. 같이 버리고 다음 ensureTransientResources 가 새 크기로 만든다.
        _capture.releasePresent();
    }

    void FrameRenderer::releaseViewTransients( FrameViewTarget& view )
    {
        _postProcess.releaseAutoExposureHistory( view );
        _postProcess.releaseUpscaleIntermediate( view );
        view._bOcclusionTilesThisFrame = SW_FALSE;
        if ( _pDevice == nullptr )
        {
            view._occlusionTiles = RHIStructuredBufferSlot{};
            view._transientPool.forget();
            view._taaHistory           = 0;
            view._taaHistorySRV        = kInvalidDescriptorIndex;
            view._depthHistory         = 0;
            view._depthHistoryUAV      = kInvalidDescriptorIndex;
            view._bLastFrameDepthValid = SW_FALSE;
            return;
        }

        // 가림 타일은 깊이 히스토리 크기를 따른다.
        view._occlusionTiles.release( _pDevice );
        // 깊이 히스토리도 풀 크기를 따른다. UAV 등록을 먼저 놓고 텍스처를 지운다(버퍼 슬롯과 같은 순서 — 레지스트리에 죽은 핸들을 남기지 않는다).
        if ( view._depthHistoryUAV != kInvalidDescriptorIndex )
            _pDevice->getResourceFactory()->unregisterBindlessUAV( view._depthHistoryUAV );
        if ( view._depthHistory != 0 )
            _pDevice->getResourceFactory()->destroyTexture( view._depthHistory );
        view._depthHistory         = 0;
        view._depthHistoryUAV      = kInvalidDescriptorIndex;
        view._bLastFrameDepthValid = SW_FALSE;

        // 히스토리는 TAA 출력의 복사본이라 크기가 정확히 같아야 한다(CopyResource 제약). 트랜지언트가
        // 새 크기로 다시 잡히면 이것도 같이 버려야 ensureTAAHistory 가 새 크기로 다시 만든다.
        if ( view._taaHistorySRV != kInvalidDescriptorIndex )
        {
            _pDevice->getResourceFactory()->unregisterBindlessTexture( view._taaHistorySRV );
            view._taaHistorySRV = kInvalidDescriptorIndex;
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

    void FrameRenderer::applyViewPassSelection( FrameViewTarget& view, const float4x4& viewProj )
    {
        using Internal = FrameRendererTransientsInternal;
        // 카메라 모션 블러의 속도는 지난 뷰-투영과의 차이뿐이다. 같거나 기록을 버리는 프레임(지난 것 = 이번 것)이면 모든 픽셀이 원본이다(motionblur.hlsl).
        const bool                          bCameraStill   = view._bHistoryReset == SW_TRUE || Memory::compare( &view._prevViewProj, &viewProj, sizeof( float4x4 ) ) == 0;
        const vector<RenderGraphPassDesc>&  listPass       = _pipelineResource.getGraphPass();
        const vector<RenderPassAttachment>& listAttachment = _pipelineResource.getDesc()._listAttachment;
        TransientAttachmentPool&            pool           = view._transientPool;
        // 선언한 첨부 자리마다 "이번 프레임에 읽힌다" 하나 — 크기가 같으면 할당하지 않는다(프레임마다 · 뷰마다 부른다).
        vector<uint8>& listNeeded = _listScratchNeededAttachment;
        listNeeded.assign( listAttachment.size(), SW_FALSE );
        bool bForwardSet = false;
        bool bAllocated  = false;
        bool bReleased   = false;
        // 빠진 패스의 출력 타깃은 이어서 오래(`TransientAttachmentPool::kIdleFramesBeforeRelease`) 빠져 있으면 놓는다. 처음 돌 때 만드는 타깃만이다
        // — 나머지(TAA 출력 · 지오메트리 출력)는 셋업이 늘 있다고 보고 읽는다. 놓은 GPU 자원은 백엔드 해제 큐가 앞 프레임들이 끝난 뒤에 지운다.
        auto ageOutput = [&]( const hashed_string& name, bool bUsedThisFrame )
        {
            if ( Internal::isMadeOnFirstUse( listPass, name ) )
                bReleased = pool.releaseIfIdle( _pDevice, name.view(), bUsedThisFrame ) || bReleased;
        };
        // TAA 가 이번 프레임에 기록을 쓰는지는 이 고르기 하나가 정한다(실행은 같은 결과 — 출력이 이어졌는지 — 를 본다). 다음 프레임의 `beginViewHistory` 가 읽는다.
        view._bTAAHistoryWritten = SW_FALSE;
        auto ensureMade          = [&]( const hashed_string& name )
        {
            if ( pool.contains( name.view() ) )
                return;
            for ( const RenderPassAttachment& attachment : listAttachment )
            {
                if ( string_view( attachment._name ) != name.view() )
                    continue;
                const RHIFormat format = FrameRendererUtil::parseAttachmentFormat( attachment._format );
                allocateTransient( pool, attachment._name, format, FrameRendererUtil::isDepthFormat( format ), attachment._clearColor, attachment._resolutionDivisor );
                bAllocated = true;
            }
        };
        // 뒤에서부터 본다 — 패스가 돌지는 뒤의 패스가 그 출력을 읽는지로 정해진다(읽는 쪽은 늘 쓰는 쪽보다 뒤에 선언한다).
        for ( size_t passIndex = listPass.size(); passIndex-- > 0; )
        {
            const RenderGraphPassDesc& pass = listPass[passIndex];
            if ( Internal::isSelectable( listPass, pass ) == false )
            {
                if ( pass._resolvedType == RenderPassType::TAA )
                    view._bTAAHistoryWritten = SW_TRUE;
                Internal::markReads( listAttachment, pass, true, listNeeded );
                for ( const RenderGraphPassDesc::ResolvedAttachment& output : pass._listResolvedColorOutput )
                {
                    ensureMade( output._attachment );
                }
                continue;
            }
            const RenderPassTypeInfo& info          = getRenderPassTypeInfo( pass._resolvedType );
            const bool                bOff          = info.hasFlag( RenderPassTraitFlag::kSkipsWithoutPostProcess ) && Internal::isEffectOff( pass, view._settings, bCameraStill );
            const bool                bOutputNeeded = std::any_of( pass._listResolvedColorOutput.begin(), pass._listResolvedColorOutput.end(),
                                                                   [&]( const RenderGraphPassDesc::ResolvedAttachment& output )
                           { return Internal::isNeeded( listAttachment, output._attachment, listNeeded ); } );
            const hashed_string*      pSource       = Internal::findSource( pass );
            const bool                bForwards     = bOff && info.hasFlag( RenderPassTraitFlag::kBypassesToSource ) && pSource != nullptr;
            if ( bForwards )
            {
                // 꺼진 효과의 출력은 원본을 가리킨다 — 출력을 읽는 쪽이 원본을 읽는다.
                bForwardSet = pool.forward( pass._listResolvedColorOutput[0]._attachment.view(), pSource->view() ) || bForwardSet;
                ageOutput( pass._listResolvedColorOutput[0]._attachment, false );
                if ( bOutputNeeded )
                    Internal::markNeeded( listAttachment, *pSource, listNeeded );
                continue;
            }
            if ( bOff || ( bOutputNeeded == false && Internal::isConsumed( listPass, pass ) ) )
            {
                // 꺼진 원본 없는 효과(SSAO)와 읽는 패스가 모두 빠진 패스는 출력이 없다 — 읽는 쪽은 그 역할을 바인딩하지 않는다.
                for ( const RenderGraphPassDesc::ResolvedAttachment& output : pass._listResolvedColorOutput )
                {
                    bForwardSet = pool.markAbsent( output._attachment.view() ) || bForwardSet;
                    ageOutput( output._attachment, false );
                }
                continue;
            }
            if ( pass._resolvedType == RenderPassType::TAA )
                view._bTAAHistoryWritten = SW_TRUE;
            for ( const RenderGraphPassDesc::ResolvedAttachment& output : pass._listResolvedColorOutput )
            {
                bForwardSet = pool.stopForward( output._attachment.view() ) || bForwardSet;
                ensureMade( output._attachment );
                ageOutput( output._attachment, true );
            }
            Internal::markReads( listAttachment, pass, false, listNeeded );
        }
        if ( &view != &_mainView )
            return;
        // 화면에 나가는 첨부 표시(`_bPresented`)가 가리키는 곳을 따라가므로 가리키는 곳이 바뀌어도 다시 싣는다. 놓은 타깃도 목록에서 뺀다(죽은 핸들을 남기지 않는다).
        const bool bTargetsChanged = bAllocated || bReleased;
        if ( bForwardSet || bTargetsChanged )
            publishRenderTargets();
        if ( bTargetsChanged )
            reportRenderTargetMemory();
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
