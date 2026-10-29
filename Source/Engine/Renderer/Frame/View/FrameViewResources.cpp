/**
 * @file FrameViewResources.cpp
 * @brief 뷰 하나의 GPU 자원 수명(트랜지언트 첨부 · 히스토리 · 가림 타일)과 프레임마다의 패스 고르기입니다.
 */
#include "pch.h"

#include "Engine/Renderer/Frame/View/FrameViewResources.h"

#include "Core/Container/VectorUtil.h"
#include "Core/Math/MathUtil.h"

#include "Engine/Graphics/RHI/IRHIDevice.h"
#include "Engine/Graphics/RHI/IRHIResourceFactory.h"
#include "Engine/Renderer/Frame/FrameRendererUtil.h"
#include "Engine/Renderer/Frame/PostProcess/FramePostProcess.h"
#include "Engine/Renderer/Frame/View/FrameViewTarget.h"
#include "Engine/Renderer/Pipeline/RenderPassInputSignature.h"
#include "Engine/Renderer/Pipeline/RenderPassTypeInfo.h"
#include "Engine/Renderer/Pipeline/RenderPipelineAsset.h"
#include "Engine/UserSettings/UserSettingsVariables.h"

namespace sw
{
    SW_LOG_CALLER( "FrameViewResources" );

    namespace
    {
        struct FrameViewResourcesInternal
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

    FrameViewResources::FrameViewResources( IRHIDevice* const& pDevice, const RenderPipelineAsset& pipeline, FramePostProcess& postProcess )
        : _pDevice{ pDevice }
        , _pipeline{ pipeline }
        , _postProcess{ postProcess }
        , _listScratchNeededAttachment{}
    {
    }

    uint32 FrameViewResources::getShadowMapResolution()
    {
        // 값의 출처는 설정 스키마 하나다. 여기서는 텍스처를 만들 수 있는 범위만 지킨다(1 미만 · 밉 상한을 넘는 한 변은 첨부를 만들지 못한다).
        constexpr int32 kMaxResolution = static_cast<int32>( 1u << ( constant::kMaxTextureMipCount - 1 ) );
        return static_cast<uint32>( MathUtil::clamp( static_cast<int32>( gv_shadowMapResolution ), 1, kMaxResolution ) );
    }

    bool FrameViewResources::updateViewTransients( FrameViewTarget& view, uint32 width, uint32 height )
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
        for ( const RenderPassAttachment& attachment : _pipeline.getDesc()._listAttachment )
        {
            if ( FrameViewResourcesInternal::isMadeOnFirstUse( _pipeline.getGraphPass(), hashed_string( attachment._name.c_str() ) ) )
                continue;
            const RHIFormat format = FrameRendererUtil::parseAttachmentFormat( attachment._format );
            const bool      bDepth = FrameRendererUtil::isDepthFormat( format );
            if ( resolveRenderPassInputRole( attachment._name, bDepth, attachment._role ) == RenderPassInputRole::ShadowMap )
                (void)pool.allocateSized( _pDevice, attachment._name, format, bDepth, attachment._clearColor, shadowResolution, shadowResolution );
            else
                pool.allocate( _pDevice, attachment._name, format, bDepth, attachment._clearColor, attachment._resolutionDivisor );
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

    void FrameViewResources::ensureDepthHistory( FrameViewTarget& view )
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

    void FrameViewResources::ensureTAAHistory( FrameViewTarget& view )
    {
        if ( _pDevice == nullptr || view._taaHistory != 0 )
            return;

        // 파이프라인에 TAA 패스가 없으면 히스토리도 필요 없다.
        const RenderGraphPassDesc* pTAAPass = nullptr;
        for ( const RenderGraphPassDesc& pass : _pipeline.getGraphPass() )
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
            SW_LOG_ERROR( "Pipeline '%#': the TAA pass has no declared output attachment - no TAA history", _pipeline.getDesc()._name.c_str() );
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

    void FrameViewResources::ensureOcclusionTiles( FrameViewTarget& view )
    {
        const bool bNotNeeded = _pDevice == nullptr || view._depthHistory == 0 || view._occlusionTiles.isValid();
        if ( bNotNeeded )
            return;
        const TransientAttachmentPool::Attachment depth = view._transientPool.find( attachmentNames()._sceneDepth.view() );
        if ( depth._texture == 0 )
            return;
        view._occlusionColumns = FrameRendererUtil::divideRoundUp( depth._width, FrameRendererUtil::kOcclusionTileSize );
        view._occlusionRows    = FrameRendererUtil::divideRoundUp( depth._height, FrameRendererUtil::kOcclusionTileSize );
        const bool bCreated    = view._occlusionTiles.ensureCapacity( _pDevice, sizeof( float32 ), view._occlusionColumns * view._occlusionRows,
                                                                      RHIBufferUsage::Structured | RHIBufferUsage::ShaderResource | RHIBufferUsage::UnorderedAccess, true, true,
                                                                      nullptr );
        if ( bCreated == false )
        {
            SW_LOG_ERROR( "Failed to create the occlusion tiles (%#x%# floats) - this view culls without occlusion", view._occlusionColumns, view._occlusionRows );
            view._occlusionTiles.release( _pDevice );
        }
    }

    void FrameViewResources::releaseViewTransients( FrameViewTarget& view )
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

    FrameViewResources::PassSelectionResult FrameViewResources::applyViewPassSelection( FrameViewTarget& view, const float4x4& viewProj )
    {
        using Internal = FrameViewResourcesInternal;
        // 카메라 모션 블러의 속도는 지난 뷰-투영과의 차이뿐이다. 같거나 기록을 버리는 프레임(지난 것 = 이번 것)이면 모든 픽셀이 원본이다(motionblur.hlsl).
        const bool                          bCameraStill   = view._bHistoryReset == SW_TRUE || Memory::compare( &view._prevViewProj, &viewProj, sizeof( float4x4 ) ) == 0;
        const vector<RenderGraphPassDesc>&  listPass       = _pipeline.getGraphPass();
        const vector<RenderPassAttachment>& listAttachment = _pipeline.getDesc()._listAttachment;
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
                pool.allocate( _pDevice, attachment._name, format, FrameRendererUtil::isDepthFormat( format ), attachment._clearColor, attachment._resolutionDivisor );
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
        // 화면에 나가는 첨부 표시(`_bPresented`)가 가리키는 곳을 따라가므로 가리키는 곳이 바뀌어도 렌더러가 목록을 다시 싣는다. 놓은 타깃도 목록에서 뺀다(죽은 핸들을 남기지 않는다).
        PassSelectionResult result{};
        result._bTargetsRepointed = bForwardSet ? SW_TRUE : SW_FALSE;
        result._bTargetsChanged   = ( bAllocated || bReleased ) ? SW_TRUE : SW_FALSE;
        return result;
    }
} // namespace sw
