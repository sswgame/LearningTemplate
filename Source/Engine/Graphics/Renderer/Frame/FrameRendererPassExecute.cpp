#include "pch.h"

#include "Core/GlobalVariable/GlobalVariableManager.h"

#include "Engine/Common/EngineServices.h"
#include "Engine/Graphics/Material/Material.h"
#include "Engine/Graphics/RHI/IRHICommandList.h"
#include "Engine/Graphics/RHI/IRHIDevice.h"
#include "Engine/Graphics/RHI/IRHIResourceFactory.h"
#include "Engine/Graphics/Renderer/Frame/FrameRenderer.h"
#include "Engine/Graphics/Renderer/Frame/FrameRendererUtil.h"
#include "Engine/Graphics/Renderer/Pipeline/RenderPassTypeInfo.h"
#include "Engine/Graphics/Texture/Texture2D.h"
#include "Engine/Utility/Profiling/FrameProfiler.h"

namespace sw
{
    SW_LOG_CALLER( "FrameRenderer" );

    /**
     * @brief `-gv_dumpRenderGraph=1`: 파이프라인을 묶을 때마다(시작 · 파이프라인 교체) 컴파일된 레벨 순서를 로그로 남깁니다.
     * @details 레벨 · 패스 · 읽고 쓰는 자원 · 컬링된 패스(`RenderGraph::describeCompiledOrder`). "왜 이 패스가 저것보다 먼저 도나 · 왜 안 도나" 의 답이다.
     */
    SW_TEST_GLOBAL_VARIABLE( bool, gv_dumpRenderGraph, false, "렌더 그래프를 컴파일할 때마다 레벨 · 패스 · 읽고 쓰는 자원을 로그로 남긴다" );

    void FrameRenderer::bindPassCallbacks()
    {
        const vector<RenderGraphPassDesc>& listPass = _pipelineResource.getGraphPass();
        _graph.clear();
        _mapPassNameToIndex.clear();
        // Swapchain 을 쓰는 마지막 패스 — 그래프는 같은 출력의 쓰기를 선언 순서로 잇는다(쓰기 사슬). 스크린샷 캡처 → 백버퍼 복사는 그 패스 끝에서 한다.
        _pLastSwapchainWriter = nullptr;
        for ( const RenderGraphPassDesc& pass : listPass )
        {
            if ( std::find( pass._listOutput.begin(), pass._listOutput.end(), string( kSwapchainOutputName ) ) != pass._listOutput.end() )
                _pLastSwapchainWriter = &pass;
        }

        for ( uint32 index = 0; index < static_cast<uint32>( listPass.size() ); ++index )
        {
            const RenderGraphPassDesc& pass = listPass[index];
            const hashed_string        nameHash( pass._name.c_str() );
            // 이름이 겹치면 앞의 것이 이긴다 — 그래프도 뒤의 선언을 버린다(`RenderGraph::addPass`). 파이프라인 검사가 먼저 알린다.
            if ( _mapPassNameToIndex.find( nameHash ) == _mapPassNameToIndex.end() )
                _mapPassNameToIndex[nameHash] = index;

            vector<hashed_string> listInput;
            vector<hashed_string> listOutput;
            for ( const string& in : pass._listInput )
            {
                listInput.emplace_back( in.c_str() );
            }
            for ( const string& out : pass._listOutput )
            {
                listOutput.emplace_back( out.c_str() );
            }
            // 깊이 첨부도 그래프가 아는 접근이다. 깊이를 쓰지 않는 패스(투명)도 그것을 DSV 로 거므로 "첨부" 상태가 필요하다. 출력에 없으면
            // 여기서 더한다 — 그래야 레벨 프롤로그가 첨부 전이(prepareTextureForRenderTarget)를 미리 내고, 앞 패스가 SRV 로 걸어 둔 깊이를
            // D3D11 이 떼며(OMSetRenderTargets 해저드), 뒤에서 깊이를 읽는 패스가 다시 읽기 전이를 받는다(언리얼 RDG 의 깊이 첨부 접근).
            // 일정은 쓰기 사슬을 따른다: 이 패스보다 **먼저 선언한** 읽는 패스는 앞 판을 읽고 이 패스 앞에 온다.
            if ( pass._resolvedDepthAttachment.empty() == false &&
                 std::find( listOutput.begin(), listOutput.end(), pass._resolvedDepthAttachment ) == listOutput.end() )
                listOutput.push_back( pass._resolvedDepthAttachment );

            RenderGraphPassExecuteFn execute =
                SW_DELEGATE_METHOD( RenderGraphPassExecuteFn, &FrameRenderer::onGraphPassExecute, this );

            _graph.addPass( nameHash, std::move( listInput ), std::move( listOutput ), std::move( execute ) );
        }

        _graph.setLevelPrologue( SW_DELEGATE_METHOD( RenderGraphLevelPrologueFn, &FrameRenderer::onGraphLevelPrologue, this ) );

        if ( _graph.compile() == false )
        {
            SW_LOG_ERROR( "Callback bind compile failed" );
        }
        else
        {
            _bCallbacksBound = SW_TRUE;
            if ( gv_dumpRenderGraph )
                SW_LOG_INFO( "[gv_dumpRenderGraph]\n%#", _graph.describeCompiledOrder().c_str() );
        }
    }

    void FrameRenderer::onGraphLevelPrologue( const RenderGraphLevelContext& levelCtx )
    {
        if ( _pDevice == nullptr || levelCtx._pCmdList == nullptr )
            return;

        if ( levelCtx._pListBarrier == nullptr )
            return;

        // 그래프가 **실제로 바뀌는 전이만** 추려서 준다. 여기서는 이름을 텍스처로 풀어 그대로 건다.
        // 같은 자원을 여러 패스가 읽어도, 이미 맞는 상태여도 다시 걸지 않는다.
        for ( const RenderGraphBarrier& barrier : *levelCtx._pListBarrier )
        {
            if ( barrier._after == RenderGraphResourceState::Write )
            {
                // 스왑체인은 전용 경로가 있다(핸들 0). 이름으로는 트랜지언트에 없다.
                if ( barrier._resource == attachmentNames()._swapchain )
                {
                    levelCtx._pCmdList->prepareTextureForRenderTarget( 0 );
                    continue;
                }
                const RHITextureHandle texture = findTransient( barrier._resource.c_str() );
                if ( texture != 0 )
                    levelCtx._pCmdList->prepareTextureForRenderTarget( texture );
            }
            else if ( barrier._after == RenderGraphResourceState::Read )
            {
                const RHITextureHandle texture = findTransient( barrier._resource.c_str() );
                if ( texture != 0 )
                    levelCtx._pCmdList->prepareTextureForShaderRead( texture );
            }
        }
    }

    void FrameRenderer::onGraphPassExecute( const RenderGraphPassContext& graphCtx )
    {
        if ( _pDevice == nullptr )
            return;

        // 패스 로컬 상태를 새로 만든다. 주의: 멤버 _pCmd 를 저장 · 복원하는 식은 "한 번에 한 패스만 돈다" 는
        // 전제라 병렬 기록에서 서로를 덮어쓴다.
        // 프레임 시드에서 복사해 뷰 · 조명 등 프레임 공통값을 물려받는다.
        // 패스 슬롯의 컨텍스트에 프레임 시드를 **대입**한다. 복사본을 새로 만들면 값 목록 · 레지스트리가 프레임마다 다시
        // 자란다. 슬롯 수는 submitGraph 가 기록 전에 맞춰 둔다. 이름을 못 찾으면 마지막 칸이다(직렬 경로에서만 온다).
        const size_t passCount    = _pipelineResource.getGraphPass().size();
        const auto   passSlotIter = _mapPassNameToIndex.find( graphCtx._passName );
        const size_t passSlot     = ( passSlotIter != _mapPassNameToIndex.end() && passSlotIter->second < passCount ) ? passSlotIter->second : passCount;
        if ( _listPassContext.size() <= passSlot )
            _listPassContext.resize( passCount + 1 );
        // 벡터 **자체**는 병렬 기록 중 읽기만 한다(칸은 패스마다 다르다). 비-const 인덱싱 · data() 는 컨테이너 레이스
        // 탐지기가 벡터에 대한 쓰기로 잡아 워커 둘이 동시에 들어오면 울린다. const 로 읽고 칸만 고쳐 쓴다.
        FramePassContext& passCtx = const_cast<FramePassContext&>( std::as_const( _listPassContext ).data()[passSlot] );
        passCtx                   = _frameCtx;
        if ( graphCtx._pCmdList != nullptr )
            passCtx._pCmd = graphCtx._pCmdList;
        // 상수 버퍼도 패스마다 따로 잡는다. 커맨드 기록은 지연인데 상수 쓰기는 즉시라,
        // 하나를 공유하면 재생 시점에 마지막 패스 값만 남는다.
        acquirePassCb( passCtx );

        const vector<RenderGraphPassDesc>& listPass = _pipelineResource.getGraphPass();
        // 타입은 로드 시점에 한 번 해석해 둔 값을 쓴다. 여기서 문자열을 다시 비교하면 디스패치와
        // PSO 생성이 서로 다른 표기를 받아 줄 여지가 생긴다.
        RenderPassType             passType  = RenderPassType::Invalid;
        const utf8*                pPassName = graphCtx._passName.c_str() != nullptr ? graphCtx._passName.c_str() : "";
        hashed_string              depthAttachment;
        const RenderGraphPassDesc* pPassDesc{ nullptr };
        const auto                 iter = _mapPassNameToIndex.find( graphCtx._passName );
        [[maybe_unused]] size_t    passIndex{ listPass.size() };
        if ( iter != _mapPassNameToIndex.end() && iter->second < listPass.size() )
        {
            const RenderGraphPassDesc& pass = listPass[iter->second];
            passType                        = pass._resolvedType;
            pPassName                       = pass._name.c_str();
            depthAttachment                 = pass._resolvedDepthAttachment;
            pPassDesc                       = &pass;
            passIndex                       = iter->second;
        }

#if SW_PROFILE_COMPILED
        // **GPU 시간은 패스 인덱스로 고정된 슬롯 쌍에 적는다.** 패스는 병렬로 기록될 수 있어
        // 흐르는 카운터를 쓰면 경쟁이 된다. 인덱스로 고정하면 각 패스가 자기 두 칸만 건드린다.
        //
        // **계측이 공짜는 아니다.** 타임스탬프 하나가 GPU 파이프라인에 표식을 박고 프레임 끝에
        // resolve 와 읽기가 붙는다. 그래서 (1) Shipping 에서는 이 블록이 통째로 사라지고,
        // (2) Dev 에서도 프로파일러가 켜져 있을 때만 찍는다(`-gv_profileFrames`).
        const uint32 timestampBegin = static_cast<uint32>( passIndex ) * 2u;
        // 패스 슬롯은 패스 번호로 고정이라 주 시점만 적는다(추가 뷰가 같은 칸을 다시 쓰면 주 시점의 시간이 덮인다).
        const bool bWriteGpuTime = _pDevice != nullptr && passCtx._pCmd != nullptr && isRenderingExtraView() == false &&
                                   engine::getFrameProfiler().isEnabled() &&
                                   static_cast<uint32>( passIndex ) < FrameRendererUtil::kGpuTimedPassCapacity &&
                                   ( timestampBegin + 1u ) < _pDevice->getTimestampSlotCount();
        if ( bWriteGpuTime )
            passCtx._pCmd->writeTimestamp( timestampBegin );
#endif

        executePass( passCtx, passType, pPassName, depthAttachment, pPassDesc );

#if SW_PROFILE_COMPILED
        if ( bWriteGpuTime )
            passCtx._pCmd->writeTimestamp( timestampBegin + 1u );
#endif
    }

    void FrameRenderer::setInputRoleEnabled( RenderPassInputRole role, bool bEnabled )
    {
        const uint32 bit = 1u << static_cast<uint32>( role );
        if ( bEnabled )
            _disabledInputRoleMask &= ~bit;
        else
            _disabledInputRoleMask |= bit;
    }

    const hashed_string& FrameRenderer::inputRoleName( RenderPassInputRole role ) const
    {
        const AttachmentNames& names = attachmentNames();
        switch ( role )
        {
            case RenderPassInputRole::SourceColor:
                return names._sourceColor;
            case RenderPassInputRole::SceneDepth:
                return names._sceneDepth;
            case RenderPassInputRole::GBufferAlbedo:
                return names._gbufferAlbedo;
            case RenderPassInputRole::GBufferNormal:
                return names._gbufferNormal;
            case RenderPassInputRole::ShadowMap:
                return names._shadowMap;
            case RenderPassInputRole::AmbientOcclusion:
                return names._ambientOcclusion;
            case RenderPassInputRole::Invalid:
            case RenderPassInputRole::Count:
                return names._sourceColor;
        }
    }

    void FrameRenderer::registerDeclaredInputs( FramePassContext& ctx, const RenderGraphPassDesc& passDesc )
    {
        for ( const RenderGraphPassDesc::ResolvedAttachment& input : passDesc._listResolvedInput )
        {
            const RenderPassInputRole role = static_cast<RenderPassInputRole>( input._role );
            if ( ( _disabledInputRoleMask & ( 1u << input._role ) ) != 0 )
                continue;
            registerPassTexture( ctx, inputRoleName( role ), input._attachment.view() );
        }
    }

    void FrameRenderer::executePass( FramePassContext& ctx, RenderPassType passType, string_view passName, const hashed_string& depthAttachment,
                                     const RenderGraphPassDesc* pPassDesc )
    {
        SW_PROFILE_SCOPE( "RT.Pass.execute" );

        if ( ctx._pCmd == nullptr )
        {
            SW_LOG_ERROR( "executePass: no active IRHICommandList" );
            return;
        }

        if ( passName.empty() == false )
        {
            utf8         arrPassName[constant::kMaxBuffer64];
            const size_t copyLen = ( passName.size() < sizeof( arrPassName ) - 1 ) ? passName.size() : ( sizeof( arrPassName ) - 1 );
            Memory::copy( arrPassName, passName.data(), copyLen );
            arrPassName[copyLen] = '\0';
            ctx._pCmd->beginEventMarker( arrPassName );
        }
        ctx._resourceRegistry.reset();
        ctx._passType = passType;
        // 라이트는 **패스 종류를 가리지 않는다.** 포워드 지오메트리도, 디퍼드 풀스크린 조명도 읽는다.
        // 그래서 인스턴스 버퍼(지오메트리 패스 전용)와 달리 여기서 모든 패스에 건다.
        registerLightBuffer( ctx );

        // 이름은 **이미 intern 된 것**만 받는다. string_view 를 받으면 패스마다 여기서 다시 intern 하게
        // 된다(FNV + 32-way 샤드 뮤텍스). 타깃 이름은 모두 코드 리터럴이라 attachmentNames() 캐시로 충분하다.
        auto colorLoadFor = [this]( const hashed_string& name, bool bForceLoad ) -> RHIRenderPassLoadOp
        {
            if ( bForceLoad )
                return RHIRenderPassLoadOp::Load;
            return markAttachmentCleared( name ) ? RHIRenderPassLoadOp::Clear : RHIRenderPassLoadOp::Load;
        };

        // b0 에 들어갈 패스 상수. 머티리얼 CB 로 폴백하면 안 된다. 레이아웃이 다르다.
        // 머티리얼 상수는 드로우마다 메시 · 배치에서 직접 넘긴다.
        const RHIDescriptorIndex passCb = ctx._passCbIndex;

        auto executeFullscreenPass = [&]( RHIPipelineStateHandle pso, const hashed_string& targetName, const float4& passClearColor )
        {
            if ( beginColorPass( ctx, targetName.view(), "", passClearColor, colorLoadFor( targetName, false ), RHIRenderPassLoadOp::Load ) == false )
                return;
            drawFullscreen( ctx, pso, passCb );
            ctx._pCmd->endRenderPass();
        };

        // 이 패스가 바인딩할 뎁스. 파이프라인 XML 의 `_depthAttachment` 가 기준이고, 비어 있으면
        // 뎁스 없이 연다. "이 패스는 일부러 뎁스를 안 쓴다" 를 선언으로 표현할 수 있어야 한다.
        // 선언한 이름이 이번 프레임에 실제로 없으면(트랜지언트 미할당) 뎁스 없이 진행한다.
        const hashed_string passDepth = ( depthAttachment.empty() == false && findTransient( depthAttachment.view() ) != 0 )
                                          ? depthAttachment
                                          : hashed_string{};

        // 지오메트리 패스가 그릴 컬러 타깃: 파이프라인이 선언한 컬러 출력(로드 때 해석한 `_listResolvedColorOutput`, 선언 순서)이다 — 풀스크린
        // 패스와 같은 규칙이다. 뎁스 로드 연산도 바인딩한 뎁스의 클리어 기록으로 정한다. 주의: 이름을 코드에 박으면(SceneColor ·
        // GBufferAlbedo …) 다른 이름을 쓰는 파이프라인에서 없는 첨부(핸들 0 = 백버퍼)를 연다. 선언이 없을 때만(패스 서술 없이
        // 부르거나, 검증이 이미 오류를 낸 파이프라인) 정본 이름으로 간다.
        const vector<RenderGraphPassDesc::ResolvedAttachment>* pDeclaredColor =
            ( pPassDesc != nullptr && pPassDesc->_listResolvedColorOutput.empty() == false ) ? &pPassDesc->_listResolvedColorOutput : nullptr;
        // 선언한 컬러 출력에서 역할로 고르고, 그 역할을 받은 출력이 없으면 선언 순서(fallbackIndex)로 고른다. 이미 다른 역할로 고른 자리는 건너뛴다.
        auto pickColorOutput = [pDeclaredColor]( RenderPassInputRole role, size_t fallbackIndex, const hashed_string* pTaken ) -> const hashed_string*
        {
            if ( pDeclaredColor == nullptr )
                return nullptr;
            for ( const RenderGraphPassDesc::ResolvedAttachment& output : *pDeclaredColor )
            {
                if ( static_cast<RenderPassInputRole>( output._role ) == role )
                    return &output._attachment;
            }
            if ( fallbackIndex >= pDeclaredColor->size() || &( *pDeclaredColor )[fallbackIndex]._attachment == pTaken )
                return nullptr;
            return &( *pDeclaredColor )[fallbackIndex]._attachment;
        };

        // 일반 풀스크린 패스는 표의 플래그로 고르고, 전용 실행 코드가 있는 패스만 아래 switch 의 case 를 갖는다.
        // 열거자를 더하고 case 를 빠뜨리면 -Wswitch-enum 이 이 자리를 알린다.
        const RenderPassTypeInfo& info              = getRenderPassTypeInfo( passType );
        const bool                bTransparentBatch = info.hasFlag( RenderPassTraitFlag::kDrawsTransparentBatch );
        if ( info.hasFlag( RenderPassTraitFlag::kGenericFullscreen ) && pPassDesc != nullptr )
        {
            // 일반 풀스크린 패스(표의 kGenericFullscreen: Lighting · SSAO · Bloom · Outline · Tonemap). **선언이 곧 바인딩**이다:
            // 입력은 역할 이름으로 모두 걸고, 타깃은 선언한 출력 중 첫 번째로 있는 것이다. 계약(RenderPassInputSignature)이
            // 로드 시점에 같은 목록을 검사했다. 새 포스트 패스는 표의 한 줄로 여기를 탄다.
            registerDeclaredInputs( ctx, *pPassDesc );

            const AttachmentNames& names   = attachmentNames();
            const hashed_string*   pTarget = &names._sceneColor;
            for ( const hashed_string& output : pPassDesc->_listResolvedOutput )
            {
                if ( findTransient( output.view() ) != 0 )
                {
                    pTarget = &output;
                    break;
                }
            }

            // PSO 가 0 이면 `drawFullscreen` 이 파이프라인 설정을 건너뛴다. 대신할 PSO 는 표가 정한 것만 쓴다(Tonemap → Present).
            const RHIPipelineStateHandle pso = findPassPso( passType );

            // 기본 클리어는 표의 값(SSAO 는 흰색 = 가림 없음)이고 없으면 렌더러의 클리어 색이다. 첨부가 클리어 색을 선언했으면 그것이 우선이다.
            const float4 defaultClear = info._pDefaultClear != nullptr ? *info._pDefaultClear : _clearColor;
            const float4 targetClear  = getAttachmentClearColorOrDefault( pTarget->view(), defaultClear );
            // 후처리를 끈 뷰(CCTV): 효과 패스(SSAO · 블룸 · 외곽선)는 그리지 않는다 — 원본을 그대로 넘기거나(포맷이 같으면 복사) 지우기만 한다(SSAO 는 흰색 = 가림 없음).
            // 조명 · 톤맵은 효과가 아니라 그림을 만드는 단계라 그대로 돈다.
            const bool bSkipEffect = _pActiveView->_settings._bPostProcess == SW_FALSE &&
                                     ( passType == RenderPassType::SSAO || passType == RenderPassType::Bloom || passType == RenderPassType::Outline );
            if ( bSkipEffect )
            {
                RHITextureHandle source{ 0 };
                for ( const RenderGraphPassDesc::ResolvedAttachment& input : pPassDesc->_listResolvedInput )
                {
                    if ( static_cast<RenderPassInputRole>( input._role ) == RenderPassInputRole::SourceColor )
                        source = findTransient( input._attachment.view() );
                }
                const RHITextureHandle target = findTransient( pTarget->view() );
                const bool             bCopy  = source != 0 && target != 0 && source != target &&
                                   _pDevice->getResourceFactory()->getTextureFormat( source ) == _pDevice->getResourceFactory()->getTextureFormat( target );
                if ( bCopy )
                {
                    ctx._pCmd->blitTexture( source, target );
                    (void)markAttachmentCleared( *pTarget );
                }
                else if ( beginColorPass( ctx, pTarget->view(), "", targetClear, RHIRenderPassLoadOp::Clear, RHIRenderPassLoadOp::Load ) )
                {
                    ctx._pCmd->endRenderPass();
                }
            }
            else
            {
                executeFullscreenPass( pso, *pTarget, targetClear );
            }
        }
        else
        {
            switch ( passType )
            {
                case RenderPassType::Shadow:
                {
                    // 클리어 값은 실제로 거는 뎁스 첨부의 선언에서 읽는다(이름 ShadowMap 으로 찾지 않는다).
                    const float4 clearVal = getAttachmentClearColorOrDefault( passDepth.view(), float4{ 1.0f, 0.0f, 0.0f, 0.0f } );
                    beginDepthOnlyPass( ctx, passDepth.view(), clearVal._x, colorLoadFor( passDepth, false ) );
                    // 그림자를 끈 뷰는 지우기만 한다 — 깊이 1(가장 멀다)이면 모두 빛을 받는다.
                    if ( _pActiveView->_settings._bShadows == SW_TRUE )
                    {
                        // 그림자는 라이트 절두체로 거른 목록을 쓴다(언리얼의 뷰별 인스턴스 컬링과 같은 자리). 끝나면 지금 뷰의 칸으로 돌아간다.
                        const uint32 viewCull = ctx._cullViewIndex;
                        ctx._cullViewIndex    = static_cast<uint32>( RenderViewType::Shadow );
                        drawSceneMeshes( ctx, getEnginePso( RenderPassType::Shadow ), passCb, bTransparentBatch );
                        ctx._cullViewIndex = viewCull;
                    }
                    ctx._pCmd->endRenderPass();
                    break;
                }
                case RenderPassType::DepthPrepass:
                {
                    const float4 clearVal = getAttachmentClearColorOrDefault( passDepth.view(), float4{ 1.0f, 0.0f, 0.0f, 0.0f } );
                    beginDepthOnlyPass( ctx, passDepth.view(), clearVal._x, colorLoadFor( passDepth, false ) );
                    drawSceneMeshes( ctx, findPassPso( RenderPassType::DepthPrepass ), passCb, bTransparentBatch );
                    ctx._pCmd->endRenderPass();
                    _bHasExecutedDepthPrepass.store( 1 );
                    break;
                }
                case RenderPassType::ForwardOpaque:
                {
                    // 그림자 맵은 선언한 입력 중 ShadowMap 역할이다(첨부의 `_role` 또는 정본 이름). 선언이 없을 때만 정본 이름으로 찾는다.
                    const RenderGraphPassDesc::ResolvedAttachment* pShadowInput = nullptr;
                    if ( pPassDesc != nullptr )
                    {
                        for ( const RenderGraphPassDesc::ResolvedAttachment& input : pPassDesc->_listResolvedInput )
                        {
                            if ( static_cast<RenderPassInputRole>( input._role ) == RenderPassInputRole::ShadowMap )
                            {
                                pShadowInput = &input;
                                break;
                            }
                        }
                    }
                    registerPassTexture( ctx, attachmentNames()._shadowMap,
                                         pShadowInput != nullptr ? pShadowInput->_attachment.view() : string_view{ FrameRendererUtil::Attachment::kShadowMap } );
                    const hashed_string& colorTarget = pDeclaredColor != nullptr ? ( *pDeclaredColor )[0]._attachment : attachmentNames()._sceneColor;
                    const float4         sceneClear  = getAttachmentClearColorOrDefault( colorTarget.view(), _clearColor );
                    if ( beginColorPass( ctx, colorTarget.view(), passDepth.view(), sceneClear, colorLoadFor( colorTarget, false ), colorLoadFor( passDepth, false ) ) )
                    {
                        const RHIPipelineStateHandle psoForward = ( _bHasExecutedDepthPrepass.load() != 0 && getEnginePso( RenderPassType::ForwardOpaqueNoDepthWrite ) != 0 )
                                                                    ? getEnginePso( RenderPassType::ForwardOpaqueNoDepthWrite )
                                                                    : getEnginePso( RenderPassType::ForwardOpaque );
                        drawSceneMeshes( ctx, psoForward, passCb, bTransparentBatch );
                        ctx._pCmd->endRenderPass();
                    }
                    break;
                }
                case RenderPassType::GBuffer:
                {
                    // 알베도 · 노멀은 역할로 고른다(첨부의 `_role` 또는 정본 이름). 역할이 없으면 선언 순서([0] 알베도, [1] 노멀)다.
                    // 한 MRT 패스로 둘 다 쓴다(네 백엔드 모두 MRT 를 보장한다). 노멀 출력이 없는 G버퍼는 검증이 거부하므로 여기서는 그리지 않는다.
                    const AttachmentNames& names         = attachmentNames();
                    const hashed_string*   pAlbedoTarget = pickColorOutput( RenderPassInputRole::GBufferAlbedo, 0, nullptr );
                    const hashed_string&   albedoTarget  = pAlbedoTarget != nullptr ? *pAlbedoTarget : names._gbufferAlbedo;
                    const hashed_string*   pNormalTarget = pDeclaredColor != nullptr ? pickColorOutput( RenderPassInputRole::GBufferNormal, 1, pAlbedoTarget )
                                                                                     : &names._gbufferNormal;
                    const bool             bHasNormal    = pNormalTarget != nullptr && findTransient( pNormalTarget->view() ) != 0;
                    if ( bHasNormal == false || getEnginePso( RenderPassType::GBuffer ) == 0 )
                        break;
                    const float4              clearColor   = getAttachmentClearColorOrDefault( albedoTarget.view(), float4{ 0.0f, 0.0f, 0.0f, 1.0f } );
                    const float4              normalClear  = getAttachmentClearColorOrDefault( pNormalTarget->view(), FrameRendererUtil::kNormalClear );
                    const string_view         arrNames[]   = { albedoTarget.view(), pNormalTarget->view() };
                    const float4              arrClears[2] = { clearColor, normalClear };
                    const RHIRenderPassLoadOp arrLoads[]   = { colorLoadFor( albedoTarget, false ), colorLoadFor( *pNormalTarget, false ) };
                    if ( beginColorPassMrt( ctx, arrNames, arrClears, arrLoads, 2, passDepth.view(), colorLoadFor( passDepth, false ) ) )
                    {
                        drawSceneMeshes( ctx, getEnginePso( RenderPassType::GBuffer ), passCb, bTransparentBatch );
                        ctx._pCmd->endRenderPass();
                    }
                    break;
                }
                case RenderPassType::Transparent:
                {
                    // 선언한 컬러 출력이 기준이다. 선언이 없을 때만 정본 이름 후보(TransparentColor → LitColor → SceneColor)로 간다.
                    const AttachmentNames& names       = attachmentNames();
                    const hashed_string&   colorTarget = pDeclaredColor != nullptr                            ? ( *pDeclaredColor )[0]._attachment
                                                       : findTransient( names._transparentColor.view() ) != 0 ? names._transparentColor
                                                       : findTransient( names._litColor.view() ) != 0         ? names._litColor
                                                                                                              : names._sceneColor;

                    if ( colorTarget == names._transparentColor )
                    {
                        const RHITextureHandle litColor = findTransient( FrameRendererUtil::Attachment::kLitColor );
                        const RHITextureHandle src      = litColor != 0 ? litColor : findTransient( FrameRendererUtil::Attachment::kSceneColor );
                        if ( src != 0 )
                            ctx._pCmd->blitTexture( src, findTransient( FrameRendererUtil::Attachment::kTransparentColor ) );
                        markAttachmentCleared( attachmentNames()._transparentColor );
                    }

                    if ( beginColorPass( ctx, colorTarget.view(), passDepth.view(), _clearColor, RHIRenderPassLoadOp::Load, RHIRenderPassLoadOp::Load ) )
                    {
                        drawSceneMeshes( ctx, findPassPso( RenderPassType::Transparent ), passCb, bTransparentBatch );
                        ctx._pCmd->endRenderPass();
                    }
                    break;
                }
                case RenderPassType::MeshOutline:
                {
                    // 뒤집은 껍질 외곽선 — 불투명 패스가 그린 색 · 깊이 위에, 외곽선을 켠 머티리얼의 배치만 앞면을 컬링해 그린다(drawsBatchInPass).
                    // 와이어프레임 보기에서는 빠진다(껍질이 선 위를 덮는다). 타깃은 선언한 컬러 출력이다(불투명 패스와 같은 규칙).
                    if ( getViewMode() == RenderViewMode::Wireframe )
                        break;
                    const hashed_string& colorTarget = pDeclaredColor != nullptr ? ( *pDeclaredColor )[0]._attachment : attachmentNames()._sceneColor;
                    const float4         sceneClear  = getAttachmentClearColorOrDefault( colorTarget.view(), _clearColor );
                    if ( beginColorPass( ctx, colorTarget.view(), passDepth.view(), sceneClear, colorLoadFor( colorTarget, false ), colorLoadFor( passDepth, false ) ) )
                    {
                        drawSceneMeshes( ctx, getEnginePso( RenderPassType::MeshOutline ), passCb, bTransparentBatch );
                        ctx._pCmd->endRenderPass();
                    }
                    break;
                }
                case RenderPassType::TAA:
                {
                    // 타깃은 선언한 출력 중 있는 것이다(풀스크린 패스와 같은 규칙, 히스토리도 같은 규칙으로 만든다 — ensureTaaHistory).
                    // 선언이 없을 때만 이름(TaaColor → SceneColor)으로 짐작한다.
                    const AttachmentNames& names      = attachmentNames();
                    const hashed_string*   pTaaTarget = findTransient( names._taaColor.view() ) != 0 ? &names._taaColor : &names._sceneColor;
                    if ( pPassDesc != nullptr )
                    {
                        for ( const hashed_string& output : pPassDesc->_listResolvedOutput )
                        {
                            if ( findTransient( output.view() ) != 0 )
                            {
                                pTaaTarget = &output;
                                break;
                            }
                        }
                    }
                    const hashed_string& taaTarget = *pTaaTarget;
                    const utf8*          pSrcName{ nullptr };
                    if ( pPassDesc != nullptr )
                    {
                        registerDeclaredInputs( ctx, *pPassDesc );
                        for ( const RenderGraphPassDesc::ResolvedAttachment& input : pPassDesc->_listResolvedInput )
                        {
                            if ( static_cast<RenderPassInputRole>( input._role ) == RenderPassInputRole::SourceColor )
                                pSrcName = input._attachment.c_str();
                        }
                    }
                    // 히스토리 생성 · bindless 등록은 ensureTaaHistory() 가 셋업 단계에서 끝냈다. 이 콜백은
                    // 병렬 기록에서 태스크 스레드가 돌리므로 여기서 레지스트리를 건드리면 안 된다. 히스토리는 뷰마다 하나다.
                    const ViewTarget&      activeView = *_pActiveView;
                    const RHITextureHandle source     = pSrcName != nullptr ? findTransient( pSrcName ) : RHITextureHandle{ 0 };
                    // 후처리를 끈 뷰는 시간 누적 없이 원본을 넘긴다(히스토리도 건드리지 않는다).
                    if ( activeView._settings._bPostProcess == SW_FALSE )
                    {
                        const RHITextureHandle target = findTransient( taaTarget.view() );
                        if ( source != 0 && target != 0 && source != target )
                            ctx._pCmd->blitTexture( source, target );
                        break;
                    }
                    // 컷 프레임(언리얼 `bCameraCut`): 지난 화면의 기록을 버린다 — 이번 프레임은 기록 자리에 **이번 원본**을 걸어 섞어도 이번 그림만 남게
                    // 한다. 원본을 기록 텍스처에 복사하지 않는 것은 포맷이 다를 수 있어서다(원본 R8G8B8A8 · 기록 R16G16B16A16 — D3D 의 복사는 같은
                    // 포맷만 받는다). 패스 끝의 복사(TAA 출력 → 기록)가 기록을 새로 채운다.
                    const bool bCut = activeView._settings._bCut == SW_TRUE && pSrcName != nullptr;
                    if ( bCut )
                        registerPassTexture( ctx, attachmentNames()._gbufferAlbedo, pSrcName );
                    else if ( activeView._taaHistory != 0 )
                        ctx._resourceRegistry.registerTexture( attachmentNames()._gbufferAlbedo, activeView._taaHistory, activeView._taaHistorySrv );
                    if ( beginColorPass( ctx, taaTarget.view(), "", _clearColor, colorLoadFor( taaTarget, false ), RHIRenderPassLoadOp::Load ) )
                    {
                        const RHIPipelineStateHandle taaPso = getEnginePso( RenderPassType::TAA );
                        if ( taaPso != 0 )
                            drawFullscreen( ctx, taaPso, passCb );
                        else if ( pSrcName != nullptr && taaTarget.view() != pSrcName )
                            ctx._pCmd->blitTexture( findTransient( pSrcName ), findTransient( taaTarget.view() ) );
                        ctx._pCmd->endRenderPass();
                    }

                    const RHITextureHandle taaOut = findTransient( taaTarget.view() );
                    if ( taaOut != 0 && activeView._taaHistory != 0 )
                        ctx._pCmd->blitTexture( taaOut, activeView._taaHistory );
                    break;
                }
                case RenderPassType::Present:
                {
                    const string           srcName = resolvePresentSource();
                    const RHITextureHandle src     = srcName.empty() ? 0 : findTransient( srcName );
                    // 출력은 뷰가 정한다(resolvePresentTarget — Canvas 와 같은 판단). 스크린샷 실행이면 백버퍼 대신 캡처 텍스처에 그리고,
                    // Swapchain 을 쓰는 마지막 패스(보통 Canvas) 끝에서 백버퍼로 복사한다 — 여기서 복사하면 뒤의 UI 가 캡처에 없다.
                    const ViewTarget&      activeView     = *_pActiveView;
                    const PresentTarget    target         = resolvePresentTarget();
                    const bool             bOwnOutput     = target._bOwnOutput == SW_TRUE;
                    const bool             bCaptureToBack = target._bCaptureToBack == SW_TRUE && isLastSwapchainWriter( pPassDesc );
                    const RHITextureHandle dstTarget      = target._texture;
                    const uint32           outputWidth    = target._width;
                    const uint32           outputHeight   = target._height;
                    const bool             bFullRect      = bOwnOutput || activeView._settings.isFullRect();
                    // 주 시점이 사각형 하나만 쓰면 바깥은 지운다. 화면 사각형 뷰는 주 시점 위에 겹치므로 남긴다.
                    const RHIRenderPassLoadOp outputLoad = ( isRenderingExtraView() && bOwnOutput == false ) ? RHIRenderPassLoadOp::Load
                                                         : bFullRect                                         ? RHIRenderPassLoadOp::DontCare
                                                                                                             : RHIRenderPassLoadOp::Clear;
                    // PSO 는 대상의 실제 포맷으로 고른다 — 렌더 타깃 포맷은 PSO 의 일부라 대상마다 PSO 가 다르다.
                    const RHIPipelineStateHandle psoBlit = findOutputPso( RenderPassType::Present, target._format );
                    if ( src != 0 && psoBlit != 0 )
                    {
                        registerPassTexture( ctx, attachmentNames()._sourceColor, srcName );
                        // 선언한 입력을 **모두** 건다. Present 가 후처리 체인을 겸하면 깊이(외곽선) · AO 가 필요하고,
                        // 그냥 블릿이면 선언이 컬러 하나뿐이라 위 등록을 덮어쓸 뿐이다.
                        if ( pPassDesc != nullptr )
                            registerDeclaredInputs( ctx, *pPassDesc );
                        RHIRenderPassBeginInfo beginInfo{};
                        beginInfo._bBindColor        = SW_TRUE;
                        beginInfo._arrColorTarget[0] = dstTarget;
                        beginInfo._colorTargetCount  = 1;
                        beginInfo._arrLoadOp[0]      = outputLoad;
                        beginInfo._arrClearColor[0]  = _clearColor;
                        beginInfo._width             = outputWidth;
                        beginInfo._height            = outputHeight;
                        ctx._pCmd->beginRenderPass( beginInfo );
                        // 사각형이면 뷰포트로 그 안에만 그린다 — 전체 화면 삼각형이 사각형을 채우고 원본 전체를 그 안에 늘인다.
                        if ( bFullRect == false )
                        {
                            const float4& rect = activeView._settings._screenRect;
                            RHIViewport   viewport{};
                            viewport._x      = rect._x * static_cast<float32>( outputWidth );
                            viewport._y      = rect._y * static_cast<float32>( outputHeight );
                            viewport._width  = rect._z * static_cast<float32>( outputWidth );
                            viewport._height = rect._w * static_cast<float32>( outputHeight );
                            ctx._pCmd->setViewport( viewport );
                        }
                        drawFullscreen( ctx, psoBlit, passCb );
                        ctx._pCmd->endRenderPass();
                        if ( bCaptureToBack )
                            ctx._pCmd->blitTexture( dstTarget, 0 );
                    }
                    else if ( src != 0 )
                    {
                        ctx._pCmd->blitTexture( src, dstTarget );
                        if ( bCaptureToBack )
                            ctx._pCmd->blitTexture( dstTarget, 0 );
                    }
                    else
                    {
                        RHIRenderPassBeginInfo beginInfo{};
                        beginInfo.setColorTarget( dstTarget, _clearColor, RHIRenderPassLoadOp::Load );
                        beginInfo._bBindColor = SW_TRUE;
                        ctx._pCmd->beginRenderPass( beginInfo );
                        drawFullscreen( ctx, 0, passCb );
                        ctx._pCmd->endRenderPass();
                    }
                    if ( target._bCaptureFromOutput == SW_TRUE && isLastSwapchainWriter( pPassDesc ) )
                        copyOutputToPresentCapture( ctx, target );
                    break;
                }
                case RenderPassType::Canvas:
                {
                    // 화면 2D 를 주 시점 출력(Present 가 그린 것)에 불러온 채(Load) 그린다. 추가 뷰(렌더 텍스처 · 화면 사각형)에는 그리지 않는다 —
                    // UI 는 주 시점에만 있다. 그릴 것이 없으면 렌더 패스를 열지 않는다. 대상 전체가 뷰포트다(화면 사각형 설정과 무관).
                    const PresentTarget target = resolvePresentTarget();
                    const bool          bDraw  = isRenderingExtraView() == false && _canvasFrame._mainOutput.isEmpty() == false && target._width > 0 &&
                                       target._height > 0;
                    const RHIPipelineStateHandle psoCanvas = bDraw ? findOutputPso( RenderPassType::Canvas, target._format ) : RHIPipelineStateHandle{ 0 };
                    if ( psoCanvas != 0 )
                    {
                        RHIRenderPassBeginInfo beginInfo{};
                        beginInfo._bBindColor        = SW_TRUE;
                        beginInfo._arrColorTarget[0] = target._texture;
                        beginInfo._colorTargetCount  = 1;
                        beginInfo._arrLoadOp[0]      = RHIRenderPassLoadOp::Load;
                        beginInfo._width             = target._width;
                        beginInfo._height            = target._height;
                        ctx._pCmd->beginRenderPass( beginInfo );
                        (void)_canvasRenderer.drawList( *ctx._pCmd, _canvasFrame._mainOutput, 0, psoCanvas, target._width, target._height,
                                                        _pDevice->supportsNativeBindlessSampling(), _canvasFrame._colorVisionMode );
                        ctx._pCmd->endRenderPass();
                    }
                    if ( target._bCaptureToBack == SW_TRUE && isLastSwapchainWriter( pPassDesc ) )
                        ctx._pCmd->blitTexture( target._texture, 0 );
                    if ( target._bCaptureFromOutput == SW_TRUE && isLastSwapchainWriter( pPassDesc ) )
                        copyOutputToPresentCapture( ctx, target );
                    break;
                }
                // 실행 코드가 없는 타입: 일반 풀스크린 패스인데 패스 서술이 없거나, PSO 슬롯만 있는 엔진 내부 타입이다.
                case RenderPassType::Invalid:
                case RenderPassType::Lighting:
                case RenderPassType::SSAO:
                case RenderPassType::Bloom:
                case RenderPassType::Outline:
                case RenderPassType::Tonemap:
                case RenderPassType::ForwardOpaqueNoDepthWrite:
                case RenderPassType::GpuCull:
                case RenderPassType::InstanceAnim:
                case RenderPassType::InstanceSort:
                case RenderPassType::MeshMorph:
                case RenderPassType::MeshSkin:
                {
                    SW_LOG_WARNING( "Unknown pass type '%#' in '%#'", passType, passName );
                    break;
                }
            }
        }

        ctx._pCmd->endEventMarker();
    }

    bool FrameRenderer::beginColorPass( FramePassContext& ctx, string_view colorName, string_view depthName, const float4& clearColor,
                                        RHIRenderPassLoadOp colorLoad, RHIRenderPassLoadOp depthLoad )
    {
        const string_view         arrName[]  = { colorName };
        const float4              arrClear[] = { clearColor };
        const RHIRenderPassLoadOp arrLoad[]  = { colorLoad };
        return beginColorPassMrt( ctx, arrName, arrClear, arrLoad, 1, depthName, depthLoad );
    }

    bool FrameRenderer::beginColorPassMrt( FramePassContext& ctx, const string_view* pColorNames, const float4* pTargetClearColor, const RHIRenderPassLoadOp* pColorLoad,
                                           uint32 colorCount, string_view depthName, RHIRenderPassLoadOp depthLoad )
    {
        if ( _pDevice == nullptr || ctx._pCmd == nullptr || pColorNames == nullptr || colorCount == 0 )
            return false;

        RHIRenderPassBeginInfo beginInfo{};
        beginInfo._bBindColor       = SW_TRUE;
        beginInfo._depthLoadOp      = depthLoad;
        beginInfo._clearDepth       = 1.0f;
        beginInfo._depthTarget      = depthName.empty() ? 0 : findTransient( depthName );
        beginInfo._width            = activePool().getWidth();
        beginInfo._height           = activePool().getHeight();
        beginInfo._colorTargetCount = colorCount > kMaxColorAttachments ? kMaxColorAttachments : colorCount;
        for ( uint32 colorTargetIndex = 0; colorTargetIndex < beginInfo._colorTargetCount; ++colorTargetIndex )
        {
            const TransientAttachmentPool::Attachment target = findTransientAttachment( pColorNames[colorTargetIndex] );
            beginInfo._arrColorTarget[colorTargetIndex]      = target._texture;
            // 렌더 패스(뷰포트 · 시저)는 타깃의 크기다 — 나눗수가 있는 첨부는 프레임보다 작다. 한 패스의 타깃은 크기가 같다(검증).
            if ( colorTargetIndex == 0 && target._texture != 0 )
            {
                beginInfo._width  = target._width;
                beginInfo._height = target._height;
            }
            // 핸들 0 은 백버퍼다. 없는 첨부를 그대로 열면 패스가 화면에 그린다 — 열지 않고 알린다(패스 경로라 한 번만).
            if ( beginInfo._arrColorTarget[colorTargetIndex] == 0 )
            {
                if ( _bMissingColorTargetLogged.exchange( 1, std::memory_order_relaxed ) == 0 )
                    SW_LOG_ERROR( "컬러 타깃 '%#' 이 이번 프레임에 없습니다 — 패스를 건너뜁니다(없는 첨부의 핸들 0 은 백버퍼라 그대로 열면 화면에 그립니다). "
                                  "파이프라인의 _listOutput · _listAttachment 를 보십시오.",
                                  pColorNames[colorTargetIndex] );
                return false;
            }
            beginInfo._arrLoadOp[colorTargetIndex] = pColorLoad != nullptr ? pColorLoad[colorTargetIndex] : RHIRenderPassLoadOp::Clear;
            if ( pTargetClearColor != nullptr )
                beginInfo._arrClearColor[colorTargetIndex] = pTargetClearColor[colorTargetIndex];
        }
        ctx._pCmd->beginRenderPass( beginInfo );
        return true;
    }

    void FrameRenderer::beginDepthOnlyPass( FramePassContext& ctx, string_view depthName, float32 clearDepth, RHIRenderPassLoadOp depthLoad )
    {
        if ( ctx._pCmd == nullptr )
            return;
        RHIRenderPassBeginInfo beginInfo{};
        beginInfo._bBindColor                           = SW_FALSE;
        const TransientAttachmentPool::Attachment depth = findTransientAttachment( depthName );
        beginInfo._colorTargetCount                     = 0;
        beginInfo._depthTarget                          = depth._texture;
        beginInfo._depthLoadOp                          = depthLoad;
        beginInfo._clearDepth                           = clearDepth;
        beginInfo._width                                = depth._texture != 0 ? depth._width : activePool().getWidth();
        beginInfo._height                               = depth._texture != 0 ? depth._height : activePool().getHeight();
        ctx._pCmd->beginRenderPass( beginInfo );
    }

    void FrameRenderer::registerPassTexture( FramePassContext& ctx, const hashed_string& canonicalName, string_view attachmentName )
    {
        const TransientAttachmentPool::Attachment attachment = findTransientAttachment( attachmentName );
        if ( attachment._texture != 0 && ctx._pCmd != nullptr )
            ctx._pCmd->prepareTextureForShaderRead( attachment._texture );
        ctx._resourceRegistry.registerTexture( canonicalName, attachment._texture, attachment._srv );
        // 원본을 비켜 읽는 효과(블룸)는 그 원본의 텍셀로 비켜야 한다 — 반해상도 첨부면 프레임 텍셀의 두 배다. ctx 는 패스마다 시드의 사본이라 다음 패스로 새지 않는다.
        if ( canonicalName == attachmentNames()._sourceColor && attachment._width > 0 && attachment._height > 0 )
        {
            const float32 width  = static_cast<float32>( attachment._width );
            const float32 height = static_cast<float32>( attachment._height );
            ctx._passValues.setFloat4( passConstantNames()._sourceTexel, float4{ 1.0f / width, 1.0f / height, width, height } );
        }
    }

    void FrameRenderer::commitBindlessTextureBindings( FramePassContext& ctx )
    {
        if ( _pDevice == nullptr || ctx._pCmd == nullptr )
            return;

        // 주의: 여기서 updatePassConstants 를 부르지 않는다. 이 함수는 드로우 루프 안에서 불리므로 그러면
        // 드로우마다 라이트 · 뷰 행렬을 다시 만들고(정규화 · 외적 · 4x4 곱 두 번) 카메라를 다시 찾고
        // hashed_string 을 여덟 개씩 intern 한다. 그 값들은 모두 프레임 상수라 execute/executePacket
        // 이 프레임 시드(_frameCtx)에 한 번만 채우면 되고, 패스 컨텍스트는 그 시드를 복사해 간다.
        // 드로우마다 바뀌는 것은 g_World 하나뿐이고 그것은 bindForDraw 가 넣는다.

        // DX11 · GL: PassCB 인덱스를 t0..t3 에 건다(bindless 에뮬레이션).
        // DX12 · VK: 셰이더가 힙 · 배열을 직접 인덱싱한다. 리플렉션 바인더가 g_<Name>Index 를 채운다.
        if ( _pDevice->supportsNativeBindlessSampling() )
            return;

        // 이 함수는 드로우 루프 안에서 불린다. 이름은 attachmentNames() 의 intern 된 것으로 찾는다. 주의: 이름을
        // 문자열로 받으면 **드로우마다** intern 하게 된다(FNV 해시 + 32-way 샤드 뮤텍스 x 4).
        auto srvOf = [&ctx]( const hashed_string& name ) -> RHIDescriptorIndex
        {
            const RegisteredTexture* pTex = ctx._resourceRegistry.findTexture( name );
            return pTex != nullptr ? pTex->_srv : kInvalidDescriptorIndex;
        };

        const AttachmentNames&   names  = attachmentNames();
        const RHIDescriptorIndex shadow = srvOf( names._shadowMap );
        const RHIDescriptorIndex albedo = srvOf( names._gbufferAlbedo );
        const RHIDescriptorIndex normal = srvOf( names._gbufferNormal );
        const RHIDescriptorIndex depth  = srvOf( names._sceneDepth );
        const RHIDescriptorIndex source = srvOf( names._sourceColor );
        const RHIDescriptorIndex ao     = srvOf( names._ambientOcclusion );

        // 슬롯 표는 binding.hlsli 의 swSampleIndex 와 같아야 한다: [shadow|source, albedo|ao, normal, depth|shadow].
        // 한 슬롯을 나눠 쓰는 둘은 같은 패스에 함께 걸리지 않는다(알베도는 Lighting, AO 는 Bloom).
        const RHIDescriptorIndex slot0 = ( shadow != kInvalidDescriptorIndex ) ? shadow : source;
        const RHIDescriptorIndex slot1 = ( albedo != kInvalidDescriptorIndex ) ? albedo : ao;
        const RHIDescriptorIndex slot2 = normal;
        const RHIDescriptorIndex slot3 = ( depth != kInvalidDescriptorIndex ) ? depth : shadow;

        if ( shadow != kInvalidDescriptorIndex || source != kInvalidDescriptorIndex )
            ctx._pCmd->bindShaderResource( slot0, 0 );
        if ( albedo != kInvalidDescriptorIndex || ao != kInvalidDescriptorIndex )
            ctx._pCmd->bindShaderResource( slot1, 1 );
        if ( normal != kInvalidDescriptorIndex )
            ctx._pCmd->bindShaderResource( slot2, 2 );
        if ( depth != kInvalidDescriptorIndex || shadow != kInvalidDescriptorIndex )
            ctx._pCmd->bindShaderResource( slot3, 3 );
    }

    FrameRenderer::PresentTarget FrameRenderer::resolvePresentTarget() const
    {
        // 렌더 텍스처 · 호스트 타깃 뷰는 자기 출력 전체, 주 시점 · 화면 사각형 뷰는 주 출력(백버퍼 · 게임 뷰 RT)이다. 스크린샷 실행이면 백버퍼 대신 캡처
        // 텍스처에 그린다 — 백버퍼는 핸들이 없어 읽을 수 없고, 후처리 · UI 가 끝난 최종 화면을 볼 길이 그것뿐이다. 캡처는 주 출력만 받는다.
        const ViewTarget& activeView = *_pActiveView;
        RHITextureHandle  ownOutput{ 0 };
        if ( isRenderingExtraView() && activeView._outputKind == RenderViewOutputKind::RenderTexture && activeView._pOutputTexture != nullptr )
            ownOutput = activeView._pOutputTexture->getHandle();
        else if ( isRenderingExtraView() && activeView._outputKind == RenderViewOutputKind::HostTarget )
            ownOutput = activeView._hostTarget;
        const bool bOwnOutput = ownOutput != 0;
        const bool bCapture   = bOwnOutput == false && ( _outputRenderTarget == 0 ) && isPresentCaptureEnabled();
        // 캡처를 백버퍼로 옮기는 것은 출력이 곧 백버퍼일 때만이다 — 크기를 덮어쓴 출력(초상화 굽기)은 화면에 나가지 않는다(크기가 다르면 복사도 안 된다).
        const bool bCaptureToBack = bCapture && _outputWidth == _pDevice->getBackBufferWidth() && _outputHeight == _pDevice->getBackBufferHeight();
        // 출력이 RT(에디터 게임 뷰)면 그 RT 에 그대로 그리고 끝에 캡처로 복사한다. 화면 사각형 뷰도 같은 RT 에 겹쳐 그리므로 뷰마다 복사하면
        // 마지막 복사가 화면과 같다.
        const bool bCaptureFromOutput = bOwnOutput == false && _outputRenderTarget != 0 && isPresentCaptureEnabled();

        PresentTarget target{};
        target._texture = bOwnOutput ? ownOutput : bCapture ? _presentCapture
                                                            : _outputRenderTarget;
        target._width   = bOwnOutput ? activeView._outputWidth : _outputWidth;
        target._height  = bOwnOutput ? activeView._outputHeight : _outputHeight;
        // 백버퍼는 디바이스가 채택한 포맷(Vulkan 은 서피스 협상 결과), 텍스처는 그 텍스처가 기록한 포맷이다.
        target._format             = ( target._texture == 0 ) ? _pDevice->getBackBufferFormat() : _pDevice->getResourceFactory()->getTextureFormat( target._texture );
        target._bOwnOutput         = bOwnOutput ? SW_TRUE : SW_FALSE;
        target._bCapture           = bCapture ? SW_TRUE : SW_FALSE;
        target._bCaptureToBack     = bCaptureToBack ? SW_TRUE : SW_FALSE;
        target._bCaptureFromOutput = bCaptureFromOutput ? SW_TRUE : SW_FALSE;
        return target;
    }

    void FrameRenderer::copyOutputToPresentCapture( const FramePassContext& ctx, const PresentTarget& target )
    {
        // 캡처는 계약 포맷(kBackBufferFormat)으로 출력 크기에 맞춰 만든다(ensurePresentCapture) — 게임 뷰 RT 도 같은 포맷이라 그대로 복사된다.
        const bool bSameSize   = _presentCaptureWidth == target._width && _presentCaptureHeight == target._height;
        const bool bSameFormat = target._format == constant::kBackBufferFormat;
        if ( bSameSize == false || bSameFormat == false )
        {
            if ( _bCaptureMismatchLogged.exchange( 1 ) == 0 )
                SW_LOG_WARNING( "Present capture skipped - the output render target (%#x%#, format %#) does not match the capture (%#x%#, format %#)",
                                target._width, target._height, static_cast<uint32>( target._format ), _presentCaptureWidth, _presentCaptureHeight,
                                static_cast<uint32>( constant::kBackBufferFormat ) );
            return;
        }
        ctx._pCmd->blitTexture( target._texture, _presentCapture );
    }

    bool FrameRenderer::isLastSwapchainWriter( const RenderGraphPassDesc* pPassDesc ) const
    {
        // 패스 서술 없이 불린 Present(검증이 이미 오류를 낸 파이프라인)는 마지막으로 친다 — 캡처가 화면에 안 나가는 것보다 낫다.
        return _pLastSwapchainWriter == nullptr || pPassDesc == nullptr || pPassDesc == _pLastSwapchainWriter;
    }

    RHIFormat FrameRenderer::attachmentFormatOrDefault( string_view attachmentName, RHIFormat fallback ) const
    {
        for ( const RenderPassAttachment& attachment : _pipelineResource.getDesc()._listAttachment )
        {
            if ( attachment._name == attachmentName )
                return FrameRendererUtil::parseAttachmentFormat( attachment._format );
        }
        return fallback;
    }

    const RenderGraphPassDesc* FrameRenderer::findPassDescByType( RenderPassType passType ) const
    {
        // 표기 흔들림은 로드 시점의 _resolvedType 이 이미 흡수했다. 여기서는 값만 비교하면 된다.
        for ( const RenderGraphPassDesc& pass : _pipelineResource.getGraphPass() )
        {
            if ( pass._resolvedType == passType )
                return &pass;
        }
        return nullptr;
    }
} // namespace sw
