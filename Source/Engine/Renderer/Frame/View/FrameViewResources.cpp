/**
 * @file FrameViewResources.cpp
 * @brief 뷰 하나의 GPU 자원 수명(트랜지언트 첨부 · 히스토리 · 가림 타일)과 프레임마다의 패스 고르기입니다.
 */
#include "pch.h"

#include "Engine/Renderer/Frame/View/FrameViewResources.h"

#include "Core/Algorithm/VectorUtil.h"
#include "Core/Log/LogOnce.h"
#include "Core/Math/MathUtil.h"

#include "Engine/Config/EngineConfig.h"
#include "Engine/Foundation/EngineSubsystems.h"
#include "Engine/Graphics/RHI/IRHIDevice.h"
#include "Engine/Graphics/RHI/IRHIResourceFactory.h"
#include "Engine/Graphics/RHI/RHIDescriptors.h"
#include "Engine/Profiling/FrameProfiler.h"
#include "Engine/Renderer/Extension/FrameExtensionHost.h"
#include "Engine/Renderer/Frame/FrameRendererUtil.h"
#include "Engine/Renderer/Frame/Packet/RenderFrameSettings.h"
#include "Engine/Renderer/Frame/PostProcess/FrameAutoExposure.h"
#include "Engine/Renderer/Frame/PostProcess/FrameUpscale.h"
#include "Engine/Renderer/Frame/View/FrameViewTarget.h"
#include "Engine/Renderer/Frame/View/HZBPyramid.h"
#include "Engine/Renderer/Frame/View/RenderStability.h"
#include "Engine/Renderer/Graph/RenderGraphMemoryPlan.h"
#include "Engine/Renderer/Pipeline/RenderPassInputSignature.h"
#include "Engine/Renderer/Pipeline/RenderPassKindInfo.h"
#include "Engine/Renderer/Pipeline/RenderPipelineAsset.h"

namespace sw
{
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
             * @brief 이 뷰에서 빠질 수 있는 패스면 true 입니다 — 일반 풀스크린 패스이거나 꺼지면 원본을 가리키는 패스(TAA)이고, 컬러 출력을 모두 혼자 쓴다.
             * @details 출력을 혼자 써야 그 이름을 원본에 잇거나 비워도 다른 쓰기와 섞이지 않는다(꺼질 수 있는 효과는 로드 검증 3-6 · 3-7 이 그것을 요구한다).
             */
            static bool isSelectable( const vector<RenderGraphPassDesc>& listPass, const RenderGraphPassDesc& pass )
            {
                const RenderPassKindInfo& info            = getRenderPassKindInfo( pass._type );
                const bool                bSelectableType = info.hasFlag( RenderPassTraitFlag::kGenericFullscreen ) || info.hasFlag( RenderPassTraitFlag::kBypassesToSource ) ||
                                             info.hasFlag( RenderPassTraitFlag::kCullWhenUnconsumed );
                if ( bSelectableType == false || pass._listResolvedColorOutput.empty() )
                    return false;
                return VectorUtil::allOf( pass._listResolvedColorOutput, [&listPass]( const RenderGraphPassDesc::ResolvedAttachment& output )
                { return countWriters( listPass, output._attachment ) == 1; } );
            }

            /** @brief 파이프라인에 이 패스의 컬러 출력을 읽는 다른 패스가 있으면 true 입니다. 없으면(디버그 · 스크린샷용 출력) 읽는 쪽이 없다고 빼지 않는다. */
            static bool isConsumed( const vector<RenderGraphPassDesc>& listPass, const RenderGraphPassDesc& pass )
            {
                for ( const RenderGraphPassDesc::ResolvedAttachment& output : pass._listResolvedColorOutput )
                {
                    for ( const RenderGraphPassDesc& other : listPass )
                    {
                        const bool bReads = VectorUtil::containsIf( other._listResolvedInput, [&output]( const RenderGraphPassDesc::ResolvedAttachment& input )
                        { return input._attachment == output._attachment; } );
                        if ( &other != &pass && bReads )
                            return true;
                    }
                }
                return false;
            }

            /** @brief 파이프라인이 선언한 첨부 가운데 @p name 의 자리입니다. 선언되지 않은 이름(Swapchain)이면 비어 있습니다. */
            static optional<size_t> findAttachmentIndex( const vector<RenderPassAttachment>& listAttachment, string_view name )
            {
                for ( size_t index = 0; index < listAttachment.size(); ++index )
                {
                    if ( string_view( listAttachment[index]._name ) == name )
                        return index;
                }
                return nullopt;
            }

            /** @brief @p name 을 이번 프레임에 "읽힌다" 로 적습니다. 플래그는 선언한 첨부 자리마다 하나다(선언 밖 이름은 적지 않는다). */
            static void markNeeded( const vector<RenderPassAttachment>& listAttachment, string_view name, vector<uint8>& outListNeeded )
            {
                const optional<size_t> index = findAttachmentIndex( listAttachment, name );
                if ( index.has_value() && *index < outListNeeded.size() )
                    outListNeeded[*index] = SW_TRUE;
            }

            /** @brief @p name 이 이번 프레임에 "읽힌다" 로 적혀 있으면 true 입니다. */
            static bool isNeeded( const vector<RenderPassAttachment>& listAttachment, const hashed_string& name, const vector<uint8>& listNeeded )
            {
                const optional<size_t> index = findAttachmentIndex( listAttachment, name.view() );
                return index.has_value() && *index < listNeeded.size() && listNeeded[*index] == SW_TRUE;
            }

            /** @brief 도는 패스가 읽는 첨부를 "읽힌다" 로 적습니다. @p bIncludeOutputs 면 쓰는 첨부도 적는다(늘 도는 패스는 앞 판을 불러 이어 그릴 수 있다). */
            static void markReads( const vector<RenderPassAttachment>& listAttachment, const RenderGraphPassDesc& pass, bool bIncludeOutputs, vector<uint8>& outListNeeded,
                                   bool bSkipSceneCopyInputs = false )
            {
                for ( const RenderGraphPassDesc::ResolvedAttachment& input : pass._listResolvedInput )
                {
                    if ( bSkipSceneCopyInputs && isSceneCopyInput( input ) )
                        continue;
                    markNeeded( listAttachment, input._attachment.view(), outListNeeded );
                }
                if ( pass._resolvedDepthAttachment.empty() == false )
                    markNeeded( listAttachment, pass._resolvedDepthAttachment.view(), outListNeeded );
                if ( bIncludeOutputs == false )
                    return;
                for ( const hashed_string& output : pass._listResolvedOutput )
                {
                    markNeeded( listAttachment, output.view(), outListNeeded );
                }
            }

            /** @brief @p name 을 이번 프레임에 도는 패스가 쓰는 첨부 목록에 한 번 넣습니다. */
            static void addWritten( const hashed_string& name, vector<hashed_string>& outListWritten )
            {
                if ( name.empty() == false && VectorUtil::contains( outListWritten, name ) == false )
                    outListWritten.push_back( name );
            }

            /** @brief 도는 패스가 쓰는 첨부(모든 출력과 깊이 첨부 — 깊이는 읽기만 해도 렌더 패스에 걸린다)를 넣습니다. */
            static void addWrites( const RenderGraphPassDesc& pass, vector<hashed_string>& outListWritten )
            {
                for ( const hashed_string& output : pass._listResolvedOutput )
                {
                    addWritten( output, outListWritten );
                }
                addWritten( pass._resolvedDepthAttachment, outListWritten );
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

            /** @brief 굴절 머티리얼(물 · 유리 — scene_refraction.hlsli)만 읽는 입력(장면 색 · 깊이 사본)이면 true 입니다. 굴절 배치가 없으면 반투명 패스는 필요로 하지 않는다(입자는 읽지 않는다). */
            static bool isSceneCopyInput( const RenderGraphPassDesc::ResolvedAttachment& input )
            {
                const RenderPassInputRole role = static_cast<RenderPassInputRole>( input._role );
                return role == RenderPassInputRole::SceneColorCopy || role == RenderPassInputRole::SceneDepthCopy;
            }

            /**
             * @brief 패스가 이번 뷰에서 할 일이 있으면 true 입니다 — 판정은 이 하나다(언리얼 `ShouldRender<기능>` · `Is<효과>Enabled(View)` 처럼 긍정형).
             * @details 꺼진 효과(`kSkipsWithoutPostProcess` — 후처리를 끈 뷰 · 품질 · DOF · 모션 블러 · 접촉 그림자 · 후처리 머티리얼)와 그릴 것이 없는 덧그리는 패스(SSR · 왜곡)가 false 다.
             *          결과를 어떻게 쓰는지는 패스 성격이 정한다 — 고를 수 있는 효과는 출력이 원본을 가리키거나(`kBypassesToSource`) 없고, 덧그리는 패스는 출력이 그대로다. 둘 다 실행 입구가 기록하지 않는다.
             *          예전에는 같은 뜻을 `isEffectOff`(꺼짐)와 `shouldRenderPass`(그림)가 반대 극성으로 따로 말했다.
             * @param bMotionStill 기록을 버리는 프레임(컷 — 언리얼도 컷에서 모션 블러를 끈다)이거나, 이번 뷰-투영이 지난번 것과 같고 물체도 움직이지 않았다 — 화면 속도가 모두 0 이다.
             */
            static bool isPassEnabledInView( const RenderGraphPassDesc& pass, const RenderViewSettings& settings, const RenderFrameSettings& frameSettings, const FrameViewResources::ViewPassSceneState& sceneState,
                                             bool bMotionStill )
            {
                const RenderPassKindInfo& info = getRenderPassKindInfo( pass._type );
                if ( info.hasFlag( RenderPassTraitFlag::kSkipsWithoutPostProcess ) && ( settings.usesPostProcess() == false || pass._minPostQuality > frameSettings._postQuality ) )
                    return false;
                const RenderViewModeInfo& viewModeInfo = RenderViewModeUtil::getInfo( settings._viewMode );
                // 값 보기(노멀 · 깊이 · 오버드로)와 와이어프레임은 장면 색을 덮지 않는다(안개와 같은 판정).
                const bool bShowsScene = ( viewModeInfo._bWireframe == false || viewModeInfo._bOverlay ) && viewModeInfo._bShowsBuffer == false;
                switch ( pass._type )
                {
                    case RenderPassKind::DepthOfField:
                        return settings._depthOfField.hasDepthOfField() && settings._projectionScaleY > 0.0f; // 핀홀(조리개 0) · 직교는 꺼짐
                    case RenderPassKind::MotionBlur:
                    case RenderPassKind::MotionBlurTileMax:
                    case RenderPassKind::MotionBlurNeighborMax:
                        return frameSettings._bMotionBlur != SW_FALSE && bMotionStill == false;
                    case RenderPassKind::ContactShadow:
                        return sceneState._bContactShadows == SW_TRUE; // 접촉 그림자를 켠 빛이 없는 프레임 — 마스크가 모두 1 이다
                    case RenderPassKind::Shadow:
                        // 점광 · 스폿 아틀라스 패스는 타일이 없으면 지우지도 않는다 — 그 프레임엔 그림자 칸을 받은 빛(params.x)이 없어 조명이 아틀라스를 읽지 않으니 내용은 상관없다.
                        return pass._resolvedDepthRole != static_cast<uint8>( RenderPassInputRole::LocalShadowMap ) || sceneState._bLocalShadows == SW_TRUE;
                    case RenderPassKind::PostProcessMaterial:
                        return settings.shouldDrawPostProcessMaterial( pass._blendableLocation ); // 이 자리에 읽기를 마친 블렌더블이 없다 — 출력이 원본을 가리킨다
                    case RenderPassKind::ScreenSpaceReflection:
                        return settings.usesPostProcess() && settings._postProcess._screenSpaceReflectionIntensity > 0.0f;
                    case RenderPassKind::Distortion:
                    case RenderPassKind::DistortionApply:
                        return bShowsScene && sceneState._bDistortionBatches == SW_TRUE;
                    case RenderPassKind::DepthPrepass:
                        // 와이어프레임(겹침 아님)은 선만 그린다 — 면 깊이를 먼저 쓰면 뒤 선이 가려지고 앞 선이 면 깊이와 다투어 끊긴다(프리패스 없는 파이프라인과 같은 그림으로)
                        return viewModeInfo._bWireframe == false || viewModeInfo._bOverlay;
                    case RenderPassKind::VolumetricClouds:
                        return bShowsScene && sceneState._bVolumetricClouds == SW_TRUE; // 하늘 패스와 같은 판정 — 값 보기는 하늘을 그리지 않는다
                    case RenderPassKind::Decal:
                        return bShowsScene && sceneState._bDecals == SW_TRUE; // 값 보기는 데칼을 덮지 않는다 — 출력이 없어 베이스 패스가 DBuffer 를 걸지 않는다
                    default:
                        return true;
                }
            }
        };
    } // namespace

    FrameViewResources::FrameViewResources( IRHIDevice* const& pDevice, const RenderPipelineAsset& pipeline, const RenderFrameSettings& frameSettings, FrameAutoExposure& autoExposure,
                                            FrameUpscale& upscale )
        : _pDevice{ pDevice }
        , _pipeline{ pipeline }
        , _frameSettings{ frameSettings }
        , _autoExposure{ autoExposure }
        , _upscale{ upscale }
        , _transientLeases{}
        , _listScratchNeededAttachment{}
        , _listScratchWrittenAttachment{}
        , _listScratchAgedAttachment{}
    {
    }

    bool FrameViewResources::updateViewTransients( FrameViewTarget& view, uint32 width, uint32 height, uint32 aliasGeneration, bool bMainView )
    {
        TransientAttachmentPool& pool             = view._transientPool;
        const uint32             shadowResolution = _frameSettings._shadowMapResolution; // 게임 스레드가 뜬 사본(텍스처를 만들 수 있는 범위로 잘랐다) — 그림자 행렬의 텍셀 스냅과 같은 값
        // 받은 크기는 출력 공간(TAA 뒤)이다. 시간 업스케일이 켜지면 장면(렌더 공간)은 배율만큼 작게 그리고 TAA 가 키운다(언리얼 1 차 화면 배율).
        const float32 renderScale    = getTemporalUpscaleRenderScale();
        const uint32  renderWidth    = TransientAttachmentPool::computeRenderExtent( width, renderScale );
        const uint32  renderHeight   = TransientAttachmentPool::computeRenderExtent( height, renderScale );
        const bool    bPoolUnchanged = renderWidth == pool.getWidth() && renderHeight == pool.getHeight() && width == pool.getOutputWidth() && height == pool.getOutputHeight() &&
                                    view._shadowMapResolution == shadowResolution && view._transientAliasGeneration == aliasGeneration;
        if ( bPoolUnchanged )
            return false;

        releaseViewTransients( view );
        pool.setLeases( bMainView ? nullptr : &_transientLeases );
        pool.setSize( renderWidth, renderHeight, width, height );
        view._shadowMapResolution      = shadowResolution;
        view._transientAliasGeneration = aliasGeneration;
        // 첨부는 여기서 만들지 않는다 — 크기만 정하고, 도는 패스가 쓰는 첨부를 고르기가 처음 쓰일 때 만든다(`applyViewPassSelection` → `makeAttachments`, 언리얼 RDG 트랜지언트 할당기와 같다).
        _autoExposure.ensureAutoExposureHistory( view );
        // 새 풀의 장면 깊이에는 아직 아무것도 없다 — 이 뷰를 한 번 그릴 때까지 깊이 충돌은 보지 않는다. 시간 누적 기록(TAA · 지난 뷰-투영 · 노출)도 새것이라 첫 프레임은 버린다.
        view._bLastFrameDepthValid = SW_FALSE;
        view._bHasHistory          = SW_FALSE;
        return true;
    }

    float32 FrameViewResources::getTemporalUpscaleRenderScale() const
    {
        // 출력 공간 첨부가 없는 파이프라인(TAA 없는 포워드 · 2D)은 두 공간이 늘 같다 — 키울 패스가 없다.
        bool bHasOutputSpace = false;
        for ( const RenderPassAttachment& attachment : _pipeline.getDesc()._listAttachment )
            bHasOutputSpace = bHasOutputSpace || attachment._resolutionSpace == RenderResolutionSpace::Output;
        if ( bHasOutputSpace == false )
            return 1.0f;
        // 폴백 없음 — 설정에 값이 없거나(0) 범위 밖이면 멈춘다(PROPERTY 범위와 같은 값).
        const float32 renderScale = engine::getSubsystem<EngineConfig>()._render._temporalUpscaleRenderScale;
        if ( renderScale < RenderConfig::kMinTemporalUpscaleRenderScale || renderScale > 1.0f )
            reportFatalError( "EngineConfig _render._temporalUpscaleRenderScale %# is missing or out of range (%#..1)", renderScale, RenderConfig::kMinTemporalUpscaleRenderScale );
        return renderScale;
    }

    void FrameViewResources::placeTransients( TransientAttachmentPool& pool, const RenderGraphAliasPlan& aliasPlan )
    {
        // 능력이 있는 백엔드(DX12)만 — 없으면 같은 서술끼리만 나눈다(아래 allocateAlias, 능력 표가 정한 길).
        if ( _pDevice == nullptr || _pDevice->getResourceFactory() == nullptr || _pDevice->getCapabilities()._bPlacedTextures == SW_FALSE || aliasPlan._listPlaceable.size() < 2 )
            return;
        IRHIResourceFactory&                factory = *_pDevice->getResourceFactory();
        vector<RenderGraphMemoryItem>       listItem;
        vector<const RenderPassAttachment*> listAttachment;
        for ( const RenderGraphAliasCandidate& candidate : aliasPlan._listPlaceable )
        {
            // 이번 프레임에 쓰지 않는 첨부(빠진 패스의 출력)는 나중에 그 패스가 처음 돌 때 자기 텍스처로 만든다 — 힙 계획에 넣지 않는다.
            if ( VectorUtil::contains( _listScratchWrittenAttachment, candidate._name ) == false )
                continue;
            const RenderPassAttachment* pAttachment = nullptr;
            for ( const RenderPassAttachment& attachment : _pipeline.getDesc()._listAttachment )
            {
                if ( string_view( attachment._name ) == candidate._name.view() )
                    pAttachment = &attachment;
            }
            if ( pAttachment == nullptr || FrameRendererUtil::isDepthFormat( pAttachment->_format ) )
                continue;
            const RHITextureDesc desc = TransientAttachmentPool::makeTextureDesc(
                pAttachment->_format, false, pAttachment->getOps(), TransientAttachmentPool::computeScaledExtent( pool.getExtent( pAttachment->_resolutionSpace, true ), pAttachment->_resolutionDivisor ),
                TransientAttachmentPool::computeScaledExtent( pool.getExtent( pAttachment->_resolutionSpace, false ), pAttachment->_resolutionDivisor ), false );
            const RHITextureAllocationInfo info = factory.getTextureAllocationInfo( desc );
            if ( info._sizeBytes == 0 )
                continue;
            listItem.push_back( RenderGraphMemoryItem{ candidate._name, info._sizeBytes, info._alignment, candidate._firstLevel, candidate._lastLevel } );
            listAttachment.push_back( pAttachment );
        }
        const RenderGraphMemoryPlan plan = RenderGraphMemoryPlan::make( listItem );
        // 겹친 바이트가 없으면(모두 수명이 겹친다) 힙이 메모리를 아끼지 않는다 — 커밋 텍스처 그대로 둔다.
        if ( plan._heapBytes == 0 || plan._heapBytes >= plan._sumBytes )
            return;
        for ( size_t index = 0; index < listItem.size(); ++index )
        {
            const RenderPassAttachment& attachment = *listAttachment[index];
            (void)pool.allocatePlaced( _pDevice, listItem[index]._name, attachment._format, attachment.getOps(), attachment._resolutionSpace, attachment._resolutionDivisor, plan._heapBytes,
                                       plan._listOffset[index] );
        }
        SW_PROFILE_COUNT( "RT.Frame.transientHeapBytes", plan._heapBytes );
    }

    void FrameViewResources::ensureDepthHistory( FrameViewTarget& view )
    {
        if ( _pDevice == nullptr || view._depthHistory.isValid() )
            return;
        const TransientAttachmentPool::Attachment depth = view._transientPool.find( attachmentNames()._sceneDepth );
        if ( depth._texture == 0 )
            return; // 장면 깊이가 없는 파이프라인(2D) — 옮길 깊이가 없다

        RHITextureDesc historyDesc{};
        historyDesc._width              = depth._width;
        historyDesc._height             = depth._height;
        historyDesc._format             = getRenderPassKindInfo( RenderPassKind::DepthHistory )._pColorFormat[0]; // 복사 PSO 와 같은 포맷 한 곳
        historyDesc._bIsRenderTarget    = SW_TRUE;
        historyDesc._bIsShaderResource  = SW_TRUE;
        historyDesc._bIsUnorderedAccess = SW_TRUE;
        if ( view._depthHistory.ensure( *_pDevice, historyDesc, "DepthHistory" ) == false )
            SW_LOG_ERROR_ONCE_ID( "FrameViewResources.FailedCreateDepthHistory", "Failed to create the depth history (%#x%#, R32 float RT + UAV) - GPU particles do not collide with the scene depth", depth._width, depth._height );
    }

    void FrameViewResources::updateOutputHistory( const TransientAttachmentPool& pool, const hashed_string& outputName, const utf8* pDebugName, FrameViewHistory& history )
    {
        if ( history.isWritten() == false )
        {
            // 오래 쉬어 출력을 놓은 뷰는 기록도 놓는다. 다시 도는 첫 프레임은 기록을 버리므로(`FrameViewHistoryIdleRule::Discard`) 내용은 잃어도 된다.
            if ( history.isValid() && pool.contains( outputName ) == false )
                history.release( _pDevice );
            return;
        }
        if ( _pDevice == nullptr || history.isValid() )
            return;
        // 기록은 출력의 복사본이다 — 크기(시간 업스케일의 TAA 는 출력 공간) · 형식이 같아야 한다(CopyResource).
        const TransientAttachmentPool::Attachment output = pool.find( outputName );
        RHITextureDesc                            historyDesc{};
        historyDesc._width             = output._width;
        historyDesc._height            = output._height;
        historyDesc._format            = output._format;
        historyDesc._bIsRenderTarget   = SW_TRUE;
        historyDesc._bIsShaderResource = SW_TRUE;
        if ( output._texture == 0 || history.ensure( *_pDevice, historyDesc, pDebugName ) == false )
            SW_LOG_ERROR_ONCE_ID( "FrameViewResources.FailedCreateOutputHistory", "Failed to create '%#' (%#x%#, copy of '%#') - that pass does not accumulate on this view", pDebugName, output._width, output._height,
                                  outputName.c_str() );
    }

    void FrameViewResources::ensureHZB( FrameViewTarget& view )
    {
        const bool bNotNeeded = _pDevice == nullptr || view._depthHistory.isValid() == false || ( view._hzb.isValid() && view._occlusionTiles.isValid() );
        if ( bNotNeeded )
            return;
        const TransientAttachmentPool::Attachment depth = view._transientPool.find( attachmentNames()._sceneDepth );
        if ( depth._texture == 0 )
            return;
        // 피라미드는 풀(텍스처) 크기의 배치로 만든다 — 자동 렌더 배율의 사각형은 그보다 작아 늘 들어간다(`HZBOcclusion::record` 가 확인한다).
        constexpr RHIBufferUsage kUsage = RHIBufferUsage::Structured | RHIBufferUsage::ShaderResource | RHIBufferUsage::UnorderedAccess;
        const HZBLayout          layout = HZBLayout::make( depth._width, depth._height );
        view._occlusionColumns          = FrameRendererUtil::divideRoundUp( depth._width, FrameRendererUtil::kOcclusionTileSize );
        view._occlusionRows             = FrameRendererUtil::divideRoundUp( depth._height, FrameRendererUtil::kOcclusionTileSize );
        const bool bCreated             = view._hzb.ensureCapacity( _pDevice, static_cast<uint32>( 2 * sizeof( float32 ) ), layout._texelCount, kUsage, true, true, nullptr ) &&
                              view._occlusionTiles.ensureCapacity( _pDevice, sizeof( float32 ), view._occlusionColumns * view._occlusionRows, kUsage, true, true, nullptr );
        if ( bCreated == false )
        {
            SW_LOG_ERROR_ID( "FrameViewResources.FailedCreateHZB", "Failed to create the HZB (%# texels for %#x%#) and occlusion tiles - this view culls without occlusion and draws no screen space reflection",
                             layout._texelCount, depth._width, depth._height );
            view._hzb.release( _pDevice );
            view._occlusionTiles.release( _pDevice );
        }
    }

    void FrameViewResources::updateDeferredTileCapacity( FrameViewTarget& view )
    {
        // 분류 컴퓨트가 G버퍼를 샘플하고(네이티브 bindless) 간접 인자를 쓴다(간접 인자 능력 — 컬링과 같다). 조명 패스가 없는 파이프라인(포워드)은 쓸 곳이 없다.
        const bool bUsable = _pDevice != nullptr && _pDevice->supportsNativeBindlessSampling() && _pDevice->getCapabilities()._bGPUCulling != SW_FALSE &&
                             VectorUtil::containsIf( _pipeline.getGraphPass(), []( const RenderGraphPassDesc& pass )
        { return pass._type == RenderPassKind::Lighting; } );
        if ( bUsable == false || view._deferredTileCapacity > 0 )
            return;
        const TransientAttachmentPool::Attachment albedo = view._transientPool.find( attachmentNames()._gbufferAlbedo );
        if ( albedo._texture == 0 )
            return;
        view._deferredTileCapacity = FrameRendererUtil::divideRoundUp( albedo._width, kDeferredTileSize ) * FrameRendererUtil::divideRoundUp( albedo._height, kDeferredTileSize );
    }

    void FrameViewResources::releaseViewTransients( FrameViewTarget& view )
    {
        _autoExposure.releaseAutoExposureHistory( view );
        _upscale.releaseUpscaleIntermediate( view );
        view._postProcessChainBeforeTonemap.release( _pDevice );
        view._postProcessChainAfterTonemap.release( _pDevice );
        view._bOcclusionTilesThisFrame = SW_FALSE;
        view._bHZBValid                = SW_FALSE;
        if ( _pDevice == nullptr )
        {
            view._hzb                  = RHIStructuredBuffer{};
            view._occlusionTiles       = RHIStructuredBuffer{};
            view._deferredTileCapacity = 0;
            view._transientPool.clear();
            view._taaHistory.release( nullptr );
            view._skyCloudHistory.release( nullptr );
            view._depthHistory.release( nullptr );
            view._bLastFrameDepthValid = SW_FALSE;
            return;
        }

        // HZB · 가림 타일은 깊이 히스토리 크기를, 디퍼드 조명 타일 수는 G버퍼 크기를 따른다.
        view._hzb.release( _pDevice );
        view._occlusionTiles.release( _pDevice );
        view._deferredTileCapacity = 0;
        // 깊이 히스토리도 풀 크기를 따른다.
        view._depthHistory.release( _pDevice );
        view._bLastFrameDepthValid = SW_FALSE;

        // 히스토리는 TAA 출력의 복사본이라 크기가 정확히 같아야 한다(CopyResource 제약). 트랜지언트가
        // 새 크기로 다시 잡히면 이것도 같이 버려야 updateOutputHistory 가 새 크기로 다시 만든다.
        view._taaHistory.release( _pDevice );
        view._skyCloudHistory.release( _pDevice );
        view._transientPool.release( _pDevice );
    }

    bool FrameViewResources::makeAttachments( FrameViewTarget& view, const RenderGraphAliasPlan& aliasPlan )
    {
        TransientAttachmentPool& pool = view._transientPool;
        // 새 풀의 첫 묶음만 힙에 겹쳐 놓는다 — 힙 하나의 자리는 한 번에 정해진다. 뒤에 처음 도는 패스의 출력은 자기 텍스처다.
        const bool bNewPool = pool.isEmpty();
        if ( bNewPool )
            placeTransients( pool, aliasPlan );
        const vector<RenderPassAttachment>& listAttachment = _pipeline.getDesc()._listAttachment;
        bool                                bAllocated     = bNewPool && pool.isEmpty() == false;
        // 앨리어싱 계획의 별칭은 주인의 텍스처를 함께 쓴다(레벨 수명이 겹치지 않고 서술이 같다) — 주인을 먼저 만든다. 주인이 이번 프레임에 없으면 자기 텍스처로 만든다.
        for ( const bool bAliasTurn : { false, true } )
        {
            for ( const hashed_string& name : _listScratchWrittenAttachment )
            {
                const hashed_string    aliasOwner = aliasPlan.findOwner( name );
                const optional<size_t> index      = FrameViewResourcesInternal::findAttachmentIndex( listAttachment, name.view() );
                // 선언 밖 이름(Swapchain)은 첨부가 아니다. 선언하지 않은 입출력은 파이프라인 로드가 거절한다 — 코드가 이름으로 포맷 · 클리어 색을 짐작해 첨부를 더 만드는 길이 없다.
                const bool bNotThisTurn = index.has_value() == false || aliasOwner.empty() == bAliasTurn || pool.contains( name );
                if ( bNotThisTurn )
                    continue;
                const RenderPassAttachment& attachment = listAttachment[*index];
                if ( bAliasTurn && pool.allocateAlias( name, aliasOwner, attachment.getOps() ) )
                {
                    bAllocated = true;
                    continue;
                }
                const bool                bDepth = FrameRendererUtil::isDepthFormat( attachment._format );
                const RenderPassInputRole role   = resolveRenderPassInputRole( attachment._name, bDepth, attachment._role );
                // 그림자 맵(방향광 · 점광 스폿 아틀라스 둘)은 화면이 아니라 빛의 볼륨을 담는다 — 프레임 크기 대신 정사각 고정 크기로 만든다. 이름이 아니라 역할로 가린다(이름을 바꾼 그림자 첨부도 같다).
                const bool bShadowAtlas = role == RenderPassInputRole::ShadowMap || role == RenderPassInputRole::LocalShadowMap;
                const bool bMade        = bShadowAtlas
                                            ? pool.allocateSized( _pDevice, name, attachment._format, bDepth, attachment.getOps(), view._shadowMapResolution, view._shadowMapResolution, false, true )
                                            : pool.allocate( _pDevice, name, attachment._format, bDepth, attachment.getOps(), attachment._resolutionSpace, attachment._resolutionDivisor,
                                                             FrameRendererUtil::isComputeOutputOrInput( _pipeline.getGraphPass(), attachment._name ),
                                                             name != attachmentNames()._sceneDepth ); // 장면 깊이는 다음 프레임의 깊이 기록이 읽는다
                bAllocated              = bMade || bAllocated;
            }
        }
        if ( bAllocated )
        {
            // 첨부의 크기를 따르는 자원 — 그 첨부가 처음 생긴 프레임에 함께 만든다(이미 있으면 그대로다).
            ensureDepthHistory( view );
            ensureHZB( view );
            updateDeferredTileCapacity( view );
        }
        return bAllocated;
    }

    bool FrameViewResources::ageAttachments( TransientAttachmentPool& pool )
    {
        bool bReleased = false;
        for ( const hashed_string& name : _listScratchWrittenAttachment )
        {
            (void)pool.releaseIfIdle( _pDevice, name, true );
        }
        // 이름마다 프레임에 한 번만 나아가게 한다 — 쓰는 패스가 여럿인 첨부도 한 번이다.
        _listScratchAgedAttachment.clear();
        auto ageIdle = [&]( const hashed_string& name )
        {
            if ( name.empty() || VectorUtil::contains( _listScratchWrittenAttachment, name ) || VectorUtil::contains( _listScratchAgedAttachment, name ) )
                return;
            _listScratchAgedAttachment.push_back( name );
            bReleased = pool.releaseIfIdle( _pDevice, name, false ) || bReleased;
        };
        for ( const RenderGraphPassDesc& pass : _pipeline.getGraphPass() )
        {
            for ( const hashed_string& output : pass._listResolvedOutput )
            {
                ageIdle( output );
            }
            ageIdle( pass._resolvedDepthAttachment );
        }
        return bReleased;
    }

    FrameViewResources::PassSelectionResult FrameViewResources::applyViewPassSelection( FrameViewTarget& view, const float4x4& viewProj, const ViewPassSceneState& sceneState,
                                                                                        const RenderGraphAliasPlan& aliasPlan, vector_reference<const ExtensionFramePass> listExtensionPass )
    {
        using Internal           = FrameViewResourcesInternal;
        const bool bObjectMotion = sceneState._bObjectMotion == SW_TRUE;
        // 모션 블러의 속도는 카메라(지난 뷰-투영과의 차이)와 물체(화면 속도)다. 컷이거나 둘 다 서 있으면 모든 픽셀이 원본이다(motion_blur.hlsl).
        const bool                          bCameraStill   = Memory::compare( &view._prevViewProj, &viewProj, sizeof( float4x4 ) ) == 0;
        const bool                          bMotionStill   = view._bHistoryReset == SW_TRUE || ( bCameraStill && bObjectMotion == false );
        const vector<RenderGraphPassDesc>&  listPass       = _pipeline.getGraphPass();
        const vector<RenderPassAttachment>& listAttachment = _pipeline.getDesc()._listAttachment;
        TransientAttachmentPool&            pool           = view._transientPool;
        // 선언한 첨부 자리마다 "이번 프레임에 읽힌다" 하나 — 크기가 같으면 할당하지 않는다(프레임마다 · 뷰마다 부른다).
        vector<uint8>& listNeeded = _listScratchNeededAttachment;
        listNeeded.assign( listAttachment.size(), SW_FALSE );
        view._listPassDisabled.assign( listPass.size(), SW_FALSE );
        _listScratchWrittenAttachment.clear();
        bool bForwardSet = false;
        // TAA 가 이번 프레임에 기록을 쓰는지는 이 고르기 하나가 정한다(실행은 같은 결과 — 출력이 이어졌는지 — 를 본다). 다음 프레임의 `beginViewHistory` 가 읽는다.
        hashed_string cloudOutput;
        hashed_string taaOutput;
        // 모듈 확장 패스는 늘 돈다 — 읽는 첨부의 쓰는 쪽이 빠지지 않고, 쓰는 첨부 · 깊이 첨부가 있다. 선언에 없는 이름(모듈 임시 텍스처)은 `FrameExtensionHost::allocateTextures` 가 만든다.
        for ( const ExtensionFramePass& extension : listExtensionPass )
        {
            for ( const ExtensionPassRead& read : extension._params._listRead )
            {
                Internal::markNeeded( listAttachment, read._attachment, listNeeded );
            }
            for ( const string& write : extension._params._listWrite )
            {
                Internal::markNeeded( listAttachment, write, listNeeded );
                Internal::addWritten( hashed_string( write.c_str() ), _listScratchWrittenAttachment );
            }
            if ( extension._depthTarget.empty() == false )
                Internal::markNeeded( listAttachment, extension._depthTarget.view(), listNeeded );
            Internal::addWritten( extension._depthTarget, _listScratchWrittenAttachment );
        }
        // 뒤에서부터 본다 — 패스가 돌지는 뒤의 패스가 그 출력을 읽는지로 정해진다(읽는 쪽은 늘 쓰는 쪽보다 뒤에 선언한다).
        for ( size_t passIndex = listPass.size(); passIndex-- > 0; )
        {
            const RenderGraphPassDesc& pass = listPass[passIndex];
            if ( pass._type == RenderPassKind::TAA && pass._listResolvedColorOutput.empty() == false )
                taaOutput = pass._listResolvedColorOutput[0]._attachment; // 실행(executeTAAPass)과 같은 첫 컬러 출력이다
            if ( pass._type == RenderPassKind::VolumetricClouds && pass._listResolvedColorOutput.empty() == false )
                cloudOutput = pass._listResolvedColorOutput[0]._attachment;
            if ( Internal::isSelectable( listPass, pass ) == false )
            {
                // 비활성인 덧그리는 패스는 출력이 그대로라 입력을 필요로 하지 않는다 — 그것만 읽는 사본이 아래에서 빠진다.
                if ( Internal::isPassEnabledInView( pass, view._settings, _frameSettings, sceneState, bMotionStill ) == false )
                {
                    view._listPassDisabled[passIndex] = SW_TRUE;
                    continue;
                }
                if ( pass._type == RenderPassKind::TAA )
                    view._taaHistory.markWritten();
                const bool bSkipSceneCopyInputs = getRenderPassKindInfo( pass._type ).hasFlag( RenderPassTraitFlag::kDrawsTransparentBatch ) && sceneState._bRefractionBatches == SW_FALSE;
                Internal::markReads( listAttachment, pass, true, listNeeded, bSkipSceneCopyInputs );
                Internal::addWrites( pass, _listScratchWrittenAttachment );
                continue;
            }
            const RenderPassKindInfo& info          = getRenderPassKindInfo( pass._type );
            const bool                bOff          = Internal::isPassEnabledInView( pass, view._settings, _frameSettings, sceneState, bMotionStill ) == false;
            const bool                bOutputNeeded = VectorUtil::containsIf( pass._listResolvedColorOutput, [&]( const RenderGraphPassDesc::ResolvedAttachment& output )
                           { return Internal::isNeeded( listAttachment, output._attachment, listNeeded ); } );
            const hashed_string*      pSource       = Internal::findSource( pass );
            const bool                bForwards     = bOff && info.hasFlag( RenderPassTraitFlag::kBypassesToSource ) && pSource != nullptr;
            if ( bForwards )
            {
                // 꺼진 효과의 출력은 원본을 가리킨다 — 출력을 읽는 쪽이 원본을 읽는다.
                bForwardSet                       = pool.forward( pass._listResolvedColorOutput[0]._attachment, *pSource ) || bForwardSet;
                view._listPassDisabled[passIndex] = SW_TRUE;
                if ( bOutputNeeded )
                    Internal::markNeeded( listAttachment, pSource->view(), listNeeded );
                continue;
            }
            if ( bOff || ( bOutputNeeded == false && Internal::isConsumed( listPass, pass ) ) )
            {
                // 꺼진 원본 없는 효과(SSAO)와 읽는 패스가 모두 빠진 패스(읽는 쪽이 모두 비활성인 사본 포함)는 출력이 없다 — 읽는 쪽은 그 역할을 바인딩하지 않는다.
                view._listPassDisabled[passIndex] = SW_TRUE;
                for ( const RenderGraphPassDesc::ResolvedAttachment& output : pass._listResolvedColorOutput )
                {
                    bForwardSet = pool.markAbsent( output._attachment ) || bForwardSet;
                }
                continue;
            }
            if ( pass._type == RenderPassKind::TAA )
                view._taaHistory.markWritten();
            if ( pass._type == RenderPassKind::VolumetricClouds )
                view._skyCloudHistory.markWritten();
            for ( const RenderGraphPassDesc::ResolvedAttachment& output : pass._listResolvedColorOutput )
            {
                bForwardSet = pool.stopForward( output._attachment ) || bForwardSet;
            }
            Internal::markReads( listAttachment, pass, false, listNeeded );
            Internal::addWrites( pass, _listScratchWrittenAttachment );
        }
        // 첨부는 도는 패스가 쓸 때만 있다 — 패스의 종류 · 실행 방식을 가리지 않는다. 아무 패스도 쓰지 않은 첨부는 읽는 쪽이 걸지 않는다(`FramePassExecutor::registerPassTexture`).
        // 빠진 패스의 출력은 이어서 오래(`TransientAttachmentPool::kIdleFramesBeforeRelease`) 빠져 있으면 놓는다. 놓은 GPU 자원은 백엔드 해제 큐가 앞 프레임들이 끝난 뒤에 지운다.
        const bool bAllocated = makeAttachments( view, aliasPlan );
        const bool bReleased  = ageAttachments( pool );
        // 시간 누적 기록 — 도는 뷰만 있고, 오래 쉬어 출력을 놓은 뷰는 기록도 놓는다. 구름의 표본 번호는 기록을 버린 프레임에 처음부터 센다.
        // 셋업에서 만든다 — 패스 콜백(병렬 기록)이 만들고 bindless 에 등록하면 다른 스레드가 같은 레지스트리를 읽는 도중에 레지스트리가 resize 된다.
        updateOutputHistory( pool, taaOutput, "TAAHistory", view._taaHistory );
        updateOutputHistory( pool, cloudOutput, "SkyCloudHistory", view._skyCloudHistory );
        if ( view._skyCloudHistory.isWritten() )
            view._skyCloudSample = view._skyCloudHistory.isReset() ? 0u : view._skyCloudSample + 1u;
        // 사진 앞 정착(조건 `RenderTemporalAccumulation`) — 시간 누적이 쌓인 정지 프레임 수. TAA 를 이번 프레임에 돌리는지는 위 고르기가 정했다(`_taaHistory`) —
        // TAA 가 없는 뷰는 TAA 기록 버림을 보지 않는다(그것만 보면 Forward 뷰의 정지 프레임이 늘 0 이었다).
        // 볼륨 구름의 기록도 같은 규칙이다(Forward 뷰는 TAA 없이 구름만 쌓는다).
        const bool bHistoryReset      = view._bHistoryReset == SW_TRUE;
        const bool bAccumulationReset = RenderStabilityUtil::isAccumulationReset( bHistoryReset, view._taaHistory.isWritten(), view._taaHistory.isReset() ) ||
                                        RenderStabilityUtil::isAccumulationReset( bHistoryReset, view._skyCloudHistory.isWritten(), view._skyCloudHistory.isReset() );
        view._stillFrameCount       = RenderStabilityUtil::advanceStillFrames( view._stillFrameCount, bAccumulationReset, bCameraStill, bObjectMotion );
        view._cameraStillFrameCount = RenderStabilityUtil::advanceStillFrames( view._cameraStillFrameCount, bAccumulationReset, bCameraStill, false );
        // 화면에 나가는 첨부 표시(`_bPresented`)가 가리키는 곳을 따라가므로 가리키는 곳이 바뀌어도 렌더러가 목록을 다시 싣는다. 놓은 타깃도 목록에서 뺀다(죽은 핸들을 남기지 않는다).
        PassSelectionResult result{};
        result._bTargetsRebound = bForwardSet ? SW_TRUE : SW_FALSE;
        result._bTargetsChanged = ( bAllocated || bReleased ) ? SW_TRUE : SW_FALSE;
        return result;
    }
} // namespace sw
