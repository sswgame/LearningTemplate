#include "pch.h"

#include "Core/Container/VectorUtil.h"
#include "Core/GlobalVariable/GlobalVariableManager.h"

#include "Engine/Common/EngineServices.h"
#include "Engine/Graphics/Material/Material.h"
#include "Engine/Graphics/RHI/IRHICommandList.h"
#include "Engine/Graphics/RHI/IRHIDevice.h"
#include "Engine/Graphics/RHI/IRHIResourceFactory.h"
#include "Engine/Graphics/RHI/IRHITimestampProvider.h"
#include "Engine/Graphics/Shader/Binding/ShaderBindingSlots.h"
#include "Engine/Graphics/Texture/Texture2D.h"
#include "Engine/Profiling/FrameProfiler.h"
#include "Engine/Renderer/Frame/FrameRenderer.h"
#include "Engine/Renderer/Frame/FrameRendererUtil.h"
#include "Engine/Renderer/Module/ModuleRenderPassRegistry.h"
#include "Engine/Renderer/Pipeline/RenderPassTypeInfo.h"

namespace sw
{
    SW_LOG_CALLER( "FrameRenderer" );

    /**
     * @brief `-gv_dumpRenderGraph=1` 이면 파이프라인을 연결할 때마다(시작, 파이프라인 교체) 컴파일된 레벨 순서를 로그로 남깁니다.
     * @details 레벨, 패스, 읽고 쓰는 자원, 컬링된 패스를 보여 준다(`RenderGraph::describeCompiledOrder`). "왜 이 패스가 저것보다 먼저 도나", "왜 안 도나" 에 답한다.
     */
    SW_TEST_GLOBAL_VARIABLE( bool, gv_dumpRenderGraph, false, "렌더 그래프를 컴파일할 때마다 레벨 · 패스 · 읽고 쓰는 자원을 로그로 남긴다" );

    void FrameRenderer::bindPassCallbacks()
    {
        const vector<RenderGraphPassDesc>& listPass = _pipelineResource.getGraphPass();
        _graph.clear();
        _mapPassNameToIndex.clear();
        // Swapchain 을 쓰는 마지막 패스다. 그래프는 같은 출력의 쓰기를 선언 순서로 연결한다(쓰기 사슬). 스크린샷 캡처를 백버퍼로 복사하는 일은 그 패스 끝에서 한다.
        _pLastSwapchainWriter = nullptr;
        for ( const RenderGraphPassDesc& pass : listPass )
        {
            if ( VectorUtil::contains( pass._listOutput, string( kSwapchainOutputName ) ) )
                _pLastSwapchainWriter = &pass;
        }

        for ( uint32 index = 0; index < static_cast<uint32>( listPass.size() ); ++index )
        {
            const RenderGraphPassDesc& pass = listPass[index];
            const hashed_string        nameHash( pass._name.c_str() );
            // 이름이 겹치면 앞의 것이 이긴다. 그래프도 뒤의 선언을 버린다(`RenderGraph::addPass`). 파이프라인 검사가 먼저 알린다.
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
            // 깊이 첨부도 그래프가 아는 접근이다. 깊이를 쓰지 않는 패스(투명)도 그것을 DSV 로 바인딩하므로 "첨부" 상태가 필요하다. 출력에 없으면
            // 여기서 추가한다. 그래야 레벨 프롤로그가 첨부 전이(prepareTextureForRenderTarget)를 미리 내고, 앞 패스가 SRV 로 바인딩해 둔 깊이를
            // D3D11 이 해제하며(OMSetRenderTargets 해저드), 뒤에서 깊이를 읽는 패스가 다시 읽기 전이를 받는다(언리얼 RDG 의 깊이 첨부 접근).
            // 순서는 쓰기 사슬을 따른다. 이 패스보다 **먼저 선언한** 읽는 패스는 앞 버전을 읽고 이 패스 앞에 온다.
            if ( pass._resolvedDepthAttachment.empty() == false &&
                 VectorUtil::contains( listOutput, pass._resolvedDepthAttachment ) == false )
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

    void FrameRenderer::onGraphLevelPrologue( const RenderGraphLevelContext& levelContext )
    {
        if ( _pDevice == nullptr || levelContext._pCmdList == nullptr )
            return;

        if ( levelContext._pListBarrier == nullptr )
            return;

        // 그래프가 **실제로 바뀌는 전이만** 추려서 준다. 여기서는 이름으로 텍스처를 찾아 그대로 바인딩한다.
        // 같은 자원을 여러 패스가 읽어도, 이미 맞는 상태여도 다시 바인딩하지 않는다.
        // 꺼진 효과의 출력 이름은 원본을 가리키지만(`TransientAttachmentPool::forward`) 전이는 그 이름으로 만든 텍스처에만 낸다 — 따라가면 꺼진 패스의 "쓰기" 가
        // 같은 레벨에서 원본을 읽는 패스와 부딪힌다. 원본의 읽기 상태는 원본을 입력으로 선언한 그 효과 패스의 레벨이 이미 냈고, 뒤에서 원본을 쓰는 패스는 로드 검증이 막는다.
        for ( const RenderGraphBarrier& barrier : *levelContext._pListBarrier )
        {
            if ( barrier._after == RenderGraphResourceState::Write )
            {
                // 스왑체인은 전용 경로가 있다(핸들 0). 이름으로는 트랜지언트에서 찾을 수 없다.
                if ( barrier._resource == attachmentNames()._swapchain )
                {
                    levelContext._pCmdList->prepareTextureForRenderTarget( 0 );
                    continue;
                }
                const RHITextureHandle texture = activePool().findOwnTexture( barrier._resource.c_str() );
                if ( texture != 0 )
                    levelContext._pCmdList->prepareTextureForRenderTarget( texture );
            }
            else if ( barrier._after == RenderGraphResourceState::Read )
            {
                const RHITextureHandle texture = activePool().findOwnTexture( barrier._resource.c_str() );
                if ( texture != 0 )
                    levelContext._pCmdList->prepareTextureForShaderRead( texture );
            }
        }
    }

    void FrameRenderer::onGraphPassExecute( const RenderGraphPassContext& graphContext )
    {
        if ( _pDevice == nullptr )
            return;

        // 패스 로컬 상태를 새로 만든다. 주의: 멤버 _pCmd 를 저장하고 복원하는 방식은 "한 번에 한 패스만 돈다" 는
        // 전제라 병렬 기록에서 서로를 덮어쓴다.
        // 프레임 시드에서 복사해 뷰, 조명 등 프레임 공통값을 물려받는다.
        // 패스 슬롯의 컨텍스트에 프레임 시드를 **대입**한다. 복사본을 새로 만들면 값 목록과 레지스트리가 프레임마다 다시
        // 늘어난다. 슬롯 수는 submitGraph 가 기록 전에 맞춰 둔다. 이름을 찾지 못하면 마지막 슬롯을 쓴다(직렬 경로에서만 온다).
        const size_t passCount    = _pipelineResource.getGraphPass().size();
        const auto   passSlotIter = _mapPassNameToIndex.find( graphContext._passName );
        const size_t passSlot     = ( passSlotIter != _mapPassNameToIndex.end() && passSlotIter->second < passCount ) ? passSlotIter->second : passCount;
        if ( _listPassContext.size() <= passSlot )
            _listPassContext.resize( passCount + 1 );
        // 벡터 **자체**는 병렬 기록 중 읽기만 한다(슬롯은 패스마다 다르다). 비-const 인덱싱이나 data() 는 컨테이너 레이스
        // 탐지기가 벡터에 대한 쓰기로 잡아 워커 둘이 동시에 들어오면 울린다. const 로 읽고 슬롯만 고쳐 쓴다.
        FramePassContext& passContext = const_cast<FramePassContext&>( std::as_const( _listPassContext ).data()[passSlot] );
        passContext                   = _frameContext;
        passContext._passName         = graphContext._passName;
        if ( graphContext._pCmdList != nullptr )
            passContext._pCmd = graphContext._pCmdList;
        // 상수 버퍼도 패스마다 따로 잡는다. 커맨드 기록은 지연되는데 상수 쓰기는 즉시 일어나므로,
        // 하나를 공유하면 재생 시점에 마지막 패스 값만 남는다.
        acquirePassCb( passContext );

        const vector<RenderGraphPassDesc>& listPass = _pipelineResource.getGraphPass();
        // 타입은 로드 시점에 한 번 해석해 둔 값을 쓴다. 여기서 문자열을 다시 비교하면 디스패치와
        // PSO 생성이 서로 다른 표기를 받아 줄 여지가 생긴다.
        // 그래프의 패스는 모두 파이프라인 서술에서 만들었다(`_mapPassNameToIndex`). 서술이 없는 패스는 실행할 규칙이 없다 — 이름으로 짐작하지 않는다.
        const auto iter        = _mapPassNameToIndex.find( graphContext._passName );
        const bool bPassListed = iter != _mapPassNameToIndex.end() && iter->second < listPass.size();
        SW_ASSERT( bPassListed );
        if ( bPassListed == false )
            return;
        const RenderGraphPassDesc&    passDesc  = listPass[iter->second];
        [[maybe_unused]] const size_t passIndex = iter->second;

#if SW_PROFILE_COMPILED
        // **GPU 시간은 패스 인덱스로 고정된 슬롯 쌍에 기록한다.** 패스는 병렬로 기록될 수 있어
        // 흐르는 카운터를 쓰면 경쟁이 된다. 인덱스로 고정하면 각 패스가 자기 슬롯 두 개만 건드린다.
        //
        // **계측에는 비용이 든다.** 타임스탬프 하나가 GPU 파이프라인에 표식을 넣고 프레임 끝에
        // resolve 와 읽기가 붙는다. 그래서 (1) Shipping 에서는 이 블록이 통째로 사라지고,
        // (2) Dev 에서도 프로파일러가 켜져 있을 때만 기록한다(`-gv_profileFrames`).
        const uint32 timestampBegin = static_cast<uint32>( passIndex ) * 2u;
        // 패스 슬롯은 패스 번호로 고정이라 주 시점만 기록한다(추가 뷰가 같은 슬롯을 다시 쓰면 주 시점의 시간이 덮인다).
        const IRHITimestampProvider* pTimestamps   = _pDevice != nullptr ? _pDevice->findTimestampProvider() : nullptr;
        const bool                   bWriteGPUTime = pTimestamps != nullptr && passContext._pCmd != nullptr && isRenderingExtraView() == false &&
                                   _gpuPassTimeline.isRecording() &&
                                   static_cast<uint32>( passIndex ) < FrameRendererUtil::kGPUTimedPassCapacity &&
                                   ( timestampBegin + 1u ) < pTimestamps->getTimestampSlotCount();
        if ( bWriteGPUTime )
            passContext._pCmd->writeTimestamp( timestampBegin );
#endif

        executePass( passContext, passDesc );

#if SW_PROFILE_COMPILED
        if ( bWriteGPUTime )
            passContext._pCmd->writeTimestamp( timestampBegin + 1u );
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
            case RenderPassInputRole::BloomBright:
                return names._bloomBright;
            case RenderPassInputRole::GBufferMaterial:
                return names._gbufferMaterial;
            case RenderPassInputRole::Invalid:
            case RenderPassInputRole::Count:
                return names._sourceColor;
        }
    }

    void FrameRenderer::registerDeclaredInputs( FramePassContext& context, const RenderGraphPassDesc& passDesc )
    {
        for ( const RenderGraphPassDesc::ResolvedAttachment& input : passDesc._listResolvedInput )
        {
            const RenderPassInputRole role = static_cast<RenderPassInputRole>( input._role );
            if ( ( _disabledInputRoleMask & ( 1u << input._role ) ) != 0 )
                continue;
            // 이 뷰에서 빠진 패스의 출력(`TransientAttachmentPool::markAbsent`, 꺼진 SSAO)은 걸지 않는다 — 셰이더는 그 역할의 기본값(AO = 1)을 쓴다. 로드 검증 3-7 이 선택 입력만 그렇게 읽게 한다.
            if ( activePool().resolveName( input._attachment.view() ).empty() )
                continue;
            registerPassTexture( context, inputRoleName( role ), input._attachment.view() );
        }
    }

    namespace
    {
        struct FrameRendererPassExecuteInternal
        {
            /**
             * @brief 선언한 컬러 출력에서 역할로 고르고, 그 역할을 받은 출력이 없으면 선언 순서(@p orderIndex)로 고릅니다.
             * @param pTaken 이미 다른 역할로 고른 출력(건너뛴다)입니다.
             */
            static const hashed_string* pickColorOutput( const vector<RenderGraphPassDesc::ResolvedAttachment>& declaredColor, RenderPassInputRole role,
                                                         size_t orderIndex, const hashed_string* pTaken )
            {
                for ( const RenderGraphPassDesc::ResolvedAttachment& output : declaredColor )
                {
                    if ( static_cast<RenderPassInputRole>( output._role ) == role )
                        return &output._attachment;
                }
                if ( orderIndex >= declaredColor.size() || &declaredColor[orderIndex]._attachment == pTaken )
                    return nullptr;
                return &declaredColor[orderIndex]._attachment;
            }

            /** @brief 선언한 입력 중 @p role 인 첫 첨부입니다. 그 역할이 없으면 nullptr 입니다. */
            static const RenderGraphPassDesc::ResolvedAttachment* findInputByRole( const RenderGraphPassDesc& passDesc, RenderPassInputRole role )
            {
                for ( const RenderGraphPassDesc::ResolvedAttachment& input : passDesc._listResolvedInput )
                {
                    if ( static_cast<RenderPassInputRole>( input._role ) == role )
                        return &input;
                }
                return nullptr;
            }
        };
    } // namespace

    // 일반 풀스크린 패스는 테이블의 플래그로 고르고, 전용 실행 코드가 있는 패스만 아래 switch 의 case 를 가진다(짧은 디스패처라 나누지 않는다).
    // 열거자를 추가하고 case 를 빠뜨리면 -Wswitch-enum 이 이 함수를 알려 준다.
    void FrameRenderer::executePass( FramePassContext& context, const RenderGraphPassDesc& passDesc )
    {
        SW_PROFILE_SCOPE( "RT.Pass.execute" );
        // 타입은 로드 시점에 한 번 해석해 둔 값을 쓴다. 여기서 문자열을 다시 비교하면 디스패치와 PSO 생성이 서로 다른 표기를 받아 줄 여지가 생긴다.
        const RenderPassType passType        = passDesc._resolvedType;
        const string_view    passName        = string_view( passDesc._name );
        const hashed_string& depthAttachment = passDesc._resolvedDepthAttachment;

        if ( context._pCmd == nullptr )
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
            context._pCmd->beginEventMarker( arrPassName );
        }
        context._resourceRegistry.reset();
        context._passType = passType;
        // 라이트는 **패스 종류를 가리지 않는다.** 포워드 지오메트리도, 디퍼드 풀스크린 조명도 읽는다.
        // 그래서 인스턴스 버퍼(지오메트리 패스 전용)와 달리 여기서 모든 패스에 바인딩한다.
        registerLightBuffer( context );

        // 이 패스가 바인딩할 뎁스다. 파이프라인 XML 의 `_depthAttachment` 가 기준이고, 비어 있으면
        // 뎁스 없이 연다. "이 패스는 일부러 뎁스를 쓰지 않는다" 를 선언으로 표현할 수 있어야 한다.
        // 선언한 이름이 이번 프레임에 실제로 없으면(트랜지언트가 할당되지 않았으면) 뎁스 없이 진행한다.
        const hashed_string passDepth = ( depthAttachment.empty() == false && findTransient( depthAttachment.view() ) != 0 )
                                          ? depthAttachment
                                          : hashed_string{};

        // 컬러 타깃은 파이프라인이 선언한 컬러 출력이다(로드 때 해석한 `_listResolvedColorOutput`, 선언 순서). 컬러를 그리는 패스(kDrawsDeclaredColor)는
        // 로드가 선언을 요구하므로 비어 있지 않다. 이름(SceneColor · GBufferAlbedo · TaaColor 등)으로 짐작하지 않는다 — 다른 이름을 쓰는
        // 파이프라인에서 없는 첨부(핸들 0 은 백버퍼)를 열었다. 뎁스 로드 연산은 바인딩한 뎁스의 클리어 기록으로 정한다.
        const vector<RenderGraphPassDesc::ResolvedAttachment>& declaredColor = passDesc._listResolvedColorOutput;

        const RenderPassTypeInfo& info = getRenderPassTypeInfo( passType );
        // 로드가 막았어야 할 상태다(검증 3-1). 여기서는 그리지 않고 끝낸다 — 백버퍼에 그리는 것보다 낫다.
        const bool bMissingDeclaredColor = info.hasFlag( RenderPassTraitFlag::kDrawsDeclaredColor ) && declaredColor.empty();
        SW_ASSERT( bMissingDeclaredColor == false );
        if ( bMissingDeclaredColor )
        {
            context._pCmd->endEventMarker(); // 함수 끝과 같은 짝
            return;
        }
        const PassExecuteInput input{ context, passType, passDesc, declaredColor, passDepth, info.hasFlag( RenderPassTraitFlag::kDrawsTransparentBatch ) };
        if ( info.hasFlag( RenderPassTraitFlag::kGenericFullscreen ) )
        {
            executeGenericFullscreenPass( input );
        }
        else
        {
            switch ( passType )
            {
                case RenderPassType::Shadow:
                {
                    // 클리어 값은 실제로 바인딩하는 뎁스 첨부의 선언에서 읽는다(이름 ShadowMap 으로 찾지 않는다).
                    const float4 clearVal = getTargetClearColor( passDepth.view() );
                    beginDepthOnlyPass( context, passDepth.view(), clearVal._x, colorLoadFor( passDepth ) );
                    // 그림자를 끈 뷰는 지우기만 한다. 깊이 1(가장 멀다)이면 모두 빛을 받는다.
                    if ( _pActiveView->_settings._bShadows == SW_TRUE )
                        drawShadowTiles( context, input._bTransparentBatch );
                    context._pCmd->endRenderPass();
                    break;
                }
                case RenderPassType::DepthPrepass:
                {
                    const float4 clearVal = getTargetClearColor( passDepth.view() );
                    beginDepthOnlyPass( context, passDepth.view(), clearVal._x, colorLoadFor( passDepth ) );
                    drawSceneMeshes( context, findPassPSO( RenderPassType::DepthPrepass ), input._bTransparentBatch );
                    context._pCmd->endRenderPass();
                    _bHasExecutedDepthPrepass.store( 1 );
                    break;
                }
                case RenderPassType::ForwardOpaque:
                case RenderPassType::ForwardOnlyOpaque:
                {
                    executeForwardOpaquePass( input );
                    break;
                }
                case RenderPassType::GBuffer:
                {
                    executeGBufferPass( input );
                    break;
                }
                case RenderPassType::Transparent:
                {
                    executeTransparentPass( input );
                    break;
                }
                case RenderPassType::MeshOutline:
                {
                    executeMeshOutlinePass( input );
                    break;
                }
                case RenderPassType::HighlightOutline:
                case RenderPassType::HighlightSense:
                {
                    executeHighlightPass( input );
                    break;
                }
                case RenderPassType::Sky:
                {
                    executeSkyPass( input );
                    break;
                }
                case RenderPassType::Fog:
                {
                    executeFogPass( input );
                    break;
                }
                case RenderPassType::TAA:
                {
                    executeTaaPass( input );
                    break;
                }
                case RenderPassType::Present:
                {
                    executePresentPass( input );
                    break;
                }
                case RenderPassType::Canvas:
                {
                    executeCanvasPass( input );
                    break;
                }
                // 실행 코드가 없는 타입이다. 패스 서술이 없는 일반 풀스크린 패스이거나, PSO 슬롯만 있는 엔진 내부 타입이다.
                case RenderPassType::Invalid:
                case RenderPassType::Lighting:
                case RenderPassType::SSAO:
                case RenderPassType::Bloom:
                case RenderPassType::BloomDownsample:
                case RenderPassType::Outline:
                case RenderPassType::Tonemap:
                case RenderPassType::Module:
                case RenderPassType::DepthOfField:
                case RenderPassType::MotionBlur:
                case RenderPassType::ForwardOpaqueNoDepthWrite:
                case RenderPassType::GPUCull:
                case RenderPassType::InstanceAnimation:
                case RenderPassType::InstanceSort:
                case RenderPassType::MeshMorph:
                case RenderPassType::MeshSkin:
                case RenderPassType::LightGrid:
                case RenderPassType::VfxSimulation:
                case RenderPassType::VfxMeshInstance:
                case RenderPassType::DepthHistory:
                case RenderPassType::AutoExposure:
                case RenderPassType::UpscaleEasu:
                case RenderPassType::UpscaleRcas:
                case RenderPassType::OcclusionTiles:
                case RenderPassType::VolumetricFog:
                case RenderPassType::ProbeUpdate:
                {
                    SW_LOG_WARNING( "Unknown pass type '%#' in '%#'", passType, passName );
                    break;
                }
            }
        }

        context._pCmd->endEventMarker();
    }

    RHIRenderPassLoadOp FrameRenderer::colorLoadFor( const hashed_string& name )
    {
        // 이름은 **이미 intern 된 것**만 받는다. string_view 를 받으면 패스마다 여기서 다시 intern 하게
        // 된다(FNV + 32-way 샤드 뮤텍스). 타깃 이름은 모두 코드 리터럴이라 attachmentNames() 캐시로 충분하다.
        return markAttachmentCleared( name ) ? RHIRenderPassLoadOp::Clear : RHIRenderPassLoadOp::Load;
    }

    void FrameRenderer::executeFullscreenInto( FramePassContext& context, RHIPipelineStateHandle pso, const hashed_string& targetName, const float4& passClearColor )
    {
        if ( beginColorPass( context, targetName.view(), "", passClearColor, colorLoadFor( targetName ), RHIRenderPassLoadOp::Load ) == false )
            return;
        drawFullscreen( context, pso );
        context._pCmd->endRenderPass();
    }

    void FrameRenderer::executeGenericFullscreenPass( const PassExecuteInput& input )
    {
        // 일반 풀스크린 패스(테이블의 kGenericFullscreen: Lighting, SSAO, Bloom, BloomDownsample, Outline, Tonemap)다. **선언이 곧 바인딩**이다.
        // 입력은 역할 이름으로 모두 바인딩하고, 타깃은 선언한 첫 컬러 출력이다. 계약(RenderPassInputSignature)이
        // 로드 시점에 같은 목록을 검사했다. 새 포스트 패스는 테이블의 한 줄로 여기를 탄다.
        FramePassContext&          context  = input._context;
        const RenderGraphPassDesc& passDesc = input._passDesc;
        const hashed_string*       pTarget  = &input._declaredColor[0]._attachment;
        // 이 뷰에서 빠진 패스는 셋업이 출력 이름을 원본에 잇거나 비웠다(`applyViewPassSelection` — 꺼진 효과, 읽는 패스가 모두 빠진 패스). 아무것도 기록하지 않는다.
        // 후처리를 끈 뷰 · 품질로 꺼지는 효과(kSkipsWithoutPostProcess)도 모두 이 길이다 — 원본을 복사하거나 셰이더가 원본을 고르던 길은 지웠다.
        if ( activePool().isForwarded( pTarget->view() ) )
            return;
        registerDeclaredInputs( context, passDesc );
        recordAutoExposure( context, passDesc );

        // PSO 가 0이면 `drawFullscreen` 이 파이프라인 설정을 건너뛴다. 대체할 PSO 는 테이블이 정한 것만 쓴다(Tonemap 은 Present).
        const RHIPipelineStateHandle pso = findPassPSO( input._passType );

        // 클리어 색은 타깃 첨부의 선언이다(SSAO 의 흰색 = 가림 없음도 AOColor 선언에 있다).
        const float4 targetClear = getTargetClearColor( pTarget->view() );
        if ( input._passType == RenderPassType::Module )
        {
            // 모듈 패스는 PSO 를 종류가 아니라 모듈 이름으로 찾고, 상수를 레지스트리에서 값으로 복사해 온다(executeModulePass).
            executeModulePass( context, passDesc, *pTarget, targetClear );
            return;
        }
        executeFullscreenInto( context, pso, *pTarget, targetClear );
    }

    void FrameRenderer::executeForwardOpaquePass( const PassExecuteInput& input )
    {
        // ForwardOnlyOpaque 는 디퍼드의 G버퍼가 뺀 배치(MATERIAL_FORWARD_ONLY — 툰)만 조명 패스가 쓴 색과 장면 깊이 위에 그린다(거르기는 drawsBatchInPass).
        // 첨부는 앞 패스가 이미 썼으므로 colorLoadFor 가 Load 를 고른다.
        // 그림자 맵은 선언한 입력 중 ShadowMap 역할이다(첨부의 `_role` 또는 원본 이름). 선언하지 않은 파이프라인(2D)은 그림자를 바인딩하지 않는다
        // — 옛 코드는 이름 ShadowMap 으로 찾았고, 그 첨부가 없는 파이프라인에서는 어차피 빈 바인딩이었다.
        using Internal                                              = FrameRendererPassExecuteInternal;
        FramePassContext&                              context      = input._context;
        const hashed_string&                           passDepth    = input._passDepth;
        const RenderGraphPassDesc::ResolvedAttachment* pShadowInput = Internal::findInputByRole( input._passDesc, RenderPassInputRole::ShadowMap );
        if ( pShadowInput != nullptr )
            registerPassTexture( context, attachmentNames()._shadowMap, pShadowInput->_attachment.view() );
        const hashed_string& colorTarget = input._declaredColor[0]._attachment;
        const float4         sceneClear  = getTargetClearColor( colorTarget.view() );
        if ( beginColorPass( context, colorTarget.view(), passDepth.view(), sceneClear, colorLoadFor( colorTarget ), colorLoadFor( passDepth ) ) == false )
            return;
        const bool bDepthAlreadyWritten = input._passType == RenderPassType::ForwardOpaque && _bHasExecutedDepthPrepass.load() != 0 &&
                                          getEnginePSO( RenderPassType::ForwardOpaqueNoDepthWrite ) != 0;
        const RHIPipelineStateHandle psoForward = bDepthAlreadyWritten ? getEnginePSO( RenderPassType::ForwardOpaqueNoDepthWrite ) : getEnginePSO( input._passType );
        drawSceneMeshes( context, psoForward, input._bTransparentBatch );
        context._pCmd->endRenderPass();
    }

    void FrameRenderer::executeGBufferPass( const PassExecuteInput& input )
    {
        // 알베도 · 노멀 · 머티리얼은 역할로 고른다(첨부의 `_role` 또는 원본 이름). 역할이 없으면 선언 순서([0] 알베도, [1] 노멀, [2] 머티리얼)를 따른다.
        // 한 MRT 패스로 셋 다 쓴다(네 백엔드 모두 MRT 를 보장한다). 컬러 출력 수가 표의 값이 아닌 G버퍼는 검증(3-2)이 거부한다.
        using Internal                       = FrameRendererPassExecuteInternal;
        FramePassContext&    context         = input._context;
        const hashed_string& passDepth       = input._passDepth;
        const hashed_string* pAlbedoTarget   = Internal::pickColorOutput( input._declaredColor, RenderPassInputRole::GBufferAlbedo, 0, nullptr );
        const hashed_string* pNormalTarget   = Internal::pickColorOutput( input._declaredColor, RenderPassInputRole::GBufferNormal, 1, pAlbedoTarget );
        const hashed_string* pMaterialTarget = Internal::pickColorOutput( input._declaredColor, RenderPassInputRole::GBufferMaterial, 2, pNormalTarget );
        const bool           bHasTargets     = pAlbedoTarget != nullptr && pNormalTarget != nullptr && findTransient( pNormalTarget->view() ) != 0 &&
                                 pMaterialTarget != nullptr && findTransient( pMaterialTarget->view() ) != 0;
        if ( bHasTargets == false || getEnginePSO( RenderPassType::GBuffer ) == 0 )
            return;
        const hashed_string&      albedoTarget = *pAlbedoTarget;
        const string_view         arrNames[]   = { albedoTarget.view(), pNormalTarget->view(), pMaterialTarget->view() };
        const float4              arrClears[]  = { getTargetClearColor( albedoTarget.view() ), getTargetClearColor( pNormalTarget->view() ),
                                                   getTargetClearColor( pMaterialTarget->view() ) };
        const RHIRenderPassLoadOp arrLoads[]   = { colorLoadFor( albedoTarget ), colorLoadFor( *pNormalTarget ), colorLoadFor( *pMaterialTarget ) };
        if ( beginColorPassMrt( context, arrNames, arrClears, arrLoads, 3, passDepth.view(), colorLoadFor( passDepth ) ) == false )
            return;
        drawSceneMeshes( context, getEnginePSO( RenderPassType::GBuffer ), input._bTransparentBatch );
        context._pCmd->endRenderPass();
    }

    void FrameRenderer::executeTransparentPass( const PassExecuteInput& input )
    {
        // 타깃은 선언한 컬러 출력이다. 원본 색 입력(SourceColor 역할)을 다른 첨부로 선언했으면 그 위에 섞도록 먼저 복사한다
        // — 옛 코드는 타깃 이름이 TransparentColor 일 때 LitColor · SceneColor 이름으로 원본을 짐작했다.
        FramePassContext&    context     = input._context;
        const hashed_string& colorTarget = input._declaredColor[0]._attachment;
        for ( const RenderGraphPassDesc::ResolvedAttachment& sourceInput : input._passDesc._listResolvedInput )
        {
            const bool bSeparateSource = static_cast<RenderPassInputRole>( sourceInput._role ) == RenderPassInputRole::SourceColor && sourceInput._attachment != colorTarget;
            if ( bSeparateSource == false )
                continue;
            const RHITextureHandle src = findTransient( sourceInput._attachment.view() );
            const RHITextureHandle dst = findTransient( colorTarget.view() );
            if ( src != 0 && dst != 0 )
            {
                context._pCmd->blitTexture( src, dst );
                (void)markAttachmentCleared( colorTarget );
            }
            break;
        }

        if ( beginColorPass( context, colorTarget.view(), input._passDepth.view(), _clearColor, RHIRenderPassLoadOp::Load, RHIRenderPassLoadOp::Load ) == false )
            return;
        // 메시 투명 배치와 입자(방출기 하나 = 후보 하나)를 그 뷰의 한 순서로 섞어 그린다(GPUSceneBuilder::populateParticleBatches · computeViewTransparentOrders).
        SW_ASSERT( input._bTransparentBatch );
        drawTransparentQueue( context, findPassPSO( RenderPassType::Transparent ) );
        context._pCmd->endRenderPass();
    }

    void FrameRenderer::executeMeshOutlinePass( const PassExecuteInput& input )
    {
        // 인버티드 헐(inverted hull) 외곽선이다. 불투명 패스가 그린 색과 깊이 위에, 외곽선을 켠 머티리얼의 배치만 앞면을 컬링해 그린다(drawsBatchInPass).
        // 와이어프레임과 표면 값 보기(Normals, Depth, Overdraw)에서는 빠진다(껍질이 선과 값 위를 덮는다). 타깃은 선언한 컬러 출력이다(불투명 패스와 같은 규칙).
        const RenderViewModeInfo& viewModeInfo = getRenderViewModeInfo( _pActiveView->_settings._viewMode );
        if ( ( viewModeInfo._bWireframe && viewModeInfo._bOverlay == false ) || viewModeInfo._bShowsBuffer ) // 겹침(ShadedWireframe)은 Lit 그림이라 그린다
            return;
        FramePassContext&    context     = input._context;
        const hashed_string& passDepth   = input._passDepth;
        const hashed_string& colorTarget = input._declaredColor[0]._attachment;
        const float4         sceneClear  = getTargetClearColor( colorTarget.view() );
        if ( beginColorPass( context, colorTarget.view(), passDepth.view(), sceneClear, colorLoadFor( colorTarget ), colorLoadFor( passDepth ) ) == false )
            return;
        drawSceneMeshes( context, getEnginePSO( RenderPassType::MeshOutline ), input._bTransparentBatch );
        context._pCmd->endRenderPass();
    }

    void FrameRenderer::executeHighlightPass( const PassExecuteInput& input )
    {
        // 인스턴스 강조 외곽선이다(highlightoutline.hlsl). 불투명 배치를 모두 그리되 이 패스의 강조 방식인 인스턴스만 정점이 남는다 —
        // 배치를 CPU 에서 거르지 않으므로 강조를 켜고 꺼도 배치가 다시 나뉘지 않는다. 와이어프레임과 표면 값 보기에서는 빠진다(메시 외곽선과 같다).
        const RenderViewModeInfo& viewModeInfo = getRenderViewModeInfo( _pActiveView->_settings._viewMode );
        if ( ( viewModeInfo._bWireframe && viewModeInfo._bOverlay == false ) || viewModeInfo._bShowsBuffer )
            return;
        FramePassContext&            context     = input._context;
        const hashed_string&         passDepth   = input._passDepth;
        const hashed_string&         colorTarget = input._declaredColor[0]._attachment;
        const float4                 sceneClear  = getTargetClearColor( colorTarget.view() );
        const RHIPipelineStateHandle pso         = getEnginePSO( input._passType );
        // 렌더 패스의 깊이 붙임은 PSO 의 깊이 형식을 따른다 — 깊이 테스트를 끈 감각 패스의 PSO 는 깊이가 없고, 둘이 다르면 Vulkan 은 그리지 않는다.
        RHIPipelineStateDesc psoDesc{};
        const bool           bUsesDepth = findPSODesc( pso, psoDesc ) && psoDesc._depthStencilFormat != RHIFormat::Unknown;
        const string_view    depthName  = bUsesDepth ? passDepth.view() : string_view{};
        if ( beginColorPass( context, colorTarget.view(), depthName, sceneClear, colorLoadFor( colorTarget ), colorLoadFor( passDepth ) ) == false )
            return;
        drawSceneMeshes( context, pso, false );
        context._pCmd->endRenderPass();
    }

    void FrameRenderer::executeSkyPass( const PassExecuteInput& input )
    {
        // 하늘이다. 깊이 1 의 풀스크린 삼각형을 깊이 테스트(LessEqual, 쓰기 없음)로 그려 아무것도 그리지 않은 곳만 칠한다(sky.hlsl).
        // 씬에 하늘이 없으면 그리지 않는다 — 배경은 컬러 첨부의 클리어 색이다. 와이어프레임과 표면 값 보기에서도 빠진다(값 위를 덮는다).
        // 뎁스나 선언한 컬러 출력이 없으면 그리지 않는다(장면을 통째로 덮거나 백버퍼에 그린다 — 그런 파이프라인은 검증이 거부한다).
        // 앞의 불투명 패스가 색과 깊이를 이미 썼으므로 둘 다 불러온다(지우지 않는다).
        const RenderViewModeInfo&    viewModeInfo = getRenderViewModeInfo( _pActiveView->_settings._viewMode );
        const RHIPipelineStateHandle skyPSO       = getEnginePSO( RenderPassType::Sky );
        const bool                   bShowsSky    = ( viewModeInfo._bWireframe == false || viewModeInfo._bOverlay ) && viewModeInfo._bShowsBuffer == false;
        const bool                   bHasTargets  = input._passDepth.empty() == false;
        const bool                   bDrawsSky    = _bHasFrameSky == SW_TRUE && bHasTargets && skyPSO != 0 && bShowsSky;
        if ( bDrawsSky == false )
            return;
        FramePassContext&    context     = input._context;
        const hashed_string& colorTarget = input._declaredColor[0]._attachment;
        if ( beginColorPass( context, colorTarget.view(), input._passDepth.view(), _clearColor, RHIRenderPassLoadOp::Load, RHIRenderPassLoadOp::Load ) == false )
            return;
        drawFullscreen( context, skyPSO );
        context._pCmd->endRenderPass();
    }

    void FrameRenderer::executeTaaPass( const PassExecuteInput& input )
    {
        // 타깃은 선언한 첫 컬러 출력이다(풀스크린 패스와 같은 규칙이며 히스토리도 같은 규칙으로 만든다. ensureTaaHistory).
        FramePassContext&      context   = input._context;
        const AttachmentNames& names     = attachmentNames();
        const hashed_string&   taaTarget = input._declaredColor[0]._attachment;
        const utf8*            pSrcName{ nullptr };
        registerDeclaredInputs( context, input._passDesc );
        for ( const RenderGraphPassDesc::ResolvedAttachment& sourceInput : input._passDesc._listResolvedInput )
        {
            if ( static_cast<RenderPassInputRole>( sourceInput._role ) == RenderPassInputRole::SourceColor )
                pSrcName = sourceInput._attachment.c_str();
        }
        // 히스토리 생성과 bindless 등록은 ensureTaaHistory() 가 셋업 단계에서 끝냈다. 이 콜백은
        // 병렬 기록에서 태스크 스레드가 실행하므로 여기서 레지스트리를 건드리면 안 된다. 히스토리는 뷰마다 하나다.
        const ViewTarget&      activeView = *_pActiveView;
        const RHITextureHandle source     = pSrcName != nullptr ? findTransient( pSrcName ) : RHITextureHandle{ 0 };
        // 후처리를 끈 뷰는 시간 누적 없이 원본을 넘긴다(히스토리도 건드리지 않는다). 포맷이 같으면 복사하고, 다르면(원본 R8G8B8A8, 출력
        // R16G16B16A16이며 D3D 의 복사는 같은 포맷만 받는다) 아래 컷 프레임과 같이 히스토리 슬롯에 원본을 바인딩해 TAA 셰이더가 원본을 옮기게 한다.
        const bool bSkipPost = activeView._settings.usesPostProcess() == false;
        if ( bSkipPost )
        {
            const RHITextureHandle target              = findTransient( taaTarget.view() );
            IRHIResourceFactory*   pFactory            = _pDevice->getResourceFactory();
            const bool             bSameFormat         = source != 0 && target != 0 && pFactory->getTextureFormat( source ) == pFactory->getTextureFormat( target );
            const bool             bNoShaderPassNeeded = bSameFormat || source == 0 || target == 0 || source == target;
            if ( bNoShaderPassNeeded )
            {
                if ( bSameFormat && source != target )
                    context._pCmd->blitTexture( source, target );
                return;
            }
        }
        // 기록을 버리는 프레임(처음 그리는 프레임 · 컷 · 후처리를 다시 켠 첫 프레임 — `ViewTarget::_bTaaHistoryReset`)에서는 지난 화면의 히스토리를 버린다. 이번 프레임은 히스토리 슬롯에 **이번 원본**을
        // 바인딩해 섞어도 이번 이미지만 남게 한다. 원본을 히스토리 텍스처에 복사하지 않는 것은 포맷이 다를 수 있어서다(원본 R8G8B8A8, 히스토리 R16G16B16A16이며
        // D3D 의 복사는 같은 포맷만 받는다). 패스 끝의 복사(TAA 출력에서 히스토리로)가 히스토리를 새로 채운다.
        const bool bCut = ( activeView._bTaaHistoryReset == SW_TRUE || bSkipPost ) && pSrcName != nullptr;
        if ( bCut )
            registerPassTexture( context, names._gbufferAlbedo, pSrcName );
        else if ( activeView._taaHistory != 0 )
            context._resourceRegistry.registerTexture( names._gbufferAlbedo, activeView._taaHistory, activeView._taaHistorySrv );
        if ( beginColorPass( context, taaTarget.view(), "", _clearColor, colorLoadFor( taaTarget ), RHIRenderPassLoadOp::Load ) )
        {
            const RHIPipelineStateHandle taaPSO = getEnginePSO( RenderPassType::TAA );
            if ( taaPSO != 0 )
                drawFullscreen( context, taaPSO );
            else if ( pSrcName != nullptr && taaTarget.view() != pSrcName )
                context._pCmd->blitTexture( findTransient( pSrcName ), findTransient( taaTarget.view() ) );
            context._pCmd->endRenderPass();
        }

        const RHITextureHandle taaOut              = findTransient( taaTarget.view() );
        const bool             bTaaHistoryWritable = taaOut != 0 && activeView._taaHistory != 0 && bSkipPost == false;
        if ( bTaaHistoryWritable )
            context._pCmd->blitTexture( taaOut, activeView._taaHistory );
    }

    void FrameRenderer::executePresentPass( const PassExecuteInput& input )
    {
        FramePassContext&          context  = input._context;
        const RenderGraphPassDesc& passDesc = input._passDesc;
        const string_view          srcName  = resolvePresentSource();
        const RHITextureHandle     src      = srcName.empty() ? 0 : findTransient( srcName );
        // 출력은 뷰가 정한다(resolvePresentTarget. Canvas 와 같은 판단). 스크린샷 실행이면 백버퍼 대신 캡처 텍스처에 그리고,
        // Swapchain 을 쓰는 마지막 패스(보통 Canvas) 끝에서 백버퍼로 복사한다. 여기서 복사하면 뒤의 UI 가 캡처에 없다.
        const ViewTarget&      activeView     = *_pActiveView;
        const PresentTarget    target         = resolvePresentTarget();
        const bool             bOwnOutput     = target._bOwnOutput == SW_TRUE;
        const bool             bCaptureToBack = target._bCaptureToBack == SW_TRUE && isLastSwapchainWriter( passDesc );
        const RHITextureHandle dstTarget      = target._texture;
        const uint32           outputWidth    = target._width;
        const uint32           outputHeight   = target._height;
        // 정수 업스케일(픽셀 퍼펙트)은 주 출력에만 있다 — 원본(사각형 픽셀 / 배율)을 배율만큼 키워 사각형 가운데에 놓으므로 사각형 전체가 아니다.
        const uint32 upscaleZoom = bOwnOutput ? 0u : static_cast<uint32>( activeView._settings._upscaleZoom );
        const bool   bFullRect   = ( bOwnOutput || activeView._settings.isFullRect() ) && upscaleZoom == 0;
        // 주 시점이 사각형 하나만 쓰면 바깥은 지운다(업스케일의 남는 띠는 검게). 화면 사각형 뷰는 주 시점 위에 겹치므로 남긴다.
        const RHIRenderPassLoadOp outputLoad  = ( isRenderingExtraView() && bOwnOutput == false ) ? RHIRenderPassLoadOp::Load
                                              : bFullRect                                         ? RHIRenderPassLoadOp::DontCare
                                                                                                  : RHIRenderPassLoadOp::Clear;
        const float4&             outputClear = upscaleZoom > 0 ? FrameRendererUtil::kBlackClear : _clearColor;
        // PSO 는 대상의 실제 포맷으로 고른다. 렌더 타깃 포맷은 PSO 의 일부라 대상마다 PSO 가 다르다.
        const RHIPipelineStateHandle psoBlit = findOutputPSO( RenderPassType::Present, target._format );
        if ( src != 0 && psoBlit != 0 )
        {
            registerPassTexture( context, attachmentNames()._sourceColor, srcName );
            // 선언한 입력을 **모두** 바인딩한다. Present 가 후처리 체인을 겸하면 깊이(외곽선)와 AO 가 필요하고,
            // 그냥 블릿이면 선언이 컬러 하나뿐이라 위 등록을 덮어쓸 뿐이다.
            registerDeclaredInputs( context, passDesc );
            // 후처리 체인이 톤맵까지 하면 자동 노출을 이 그리기 앞에서 측정한다(Tonemap 패스와 같은 자리).
            recordAutoExposure( context, passDesc );
            // FSR 1 로 키우는 뷰는 렌더 패스를 열기 전에 EASU 를 중간 RT 에 그리고, 아래에서 블릿 대신 RCAS 로 그린다(prepareUpscale 이 셋업에서 정했다).
            const bool             bUpscale = recordUpscaleEasu( context, srcName );
            RHIRenderPassBeginInfo beginInfo{};
            beginInfo._bBindColor        = SW_TRUE;
            beginInfo._arrColorTarget[0] = dstTarget;
            beginInfo._colorTargetCount  = 1;
            beginInfo._arrLoadOp[0]      = outputLoad;
            beginInfo._arrClearColor[0]  = outputClear;
            beginInfo._width             = outputWidth;
            beginInfo._height            = outputHeight;
            context._pCmd->beginRenderPass( beginInfo );
            // 사각형이면 뷰포트로 그 안에만 그린다. 전체 화면 삼각형이 사각형을 채우고 원본 전체를 그 안에 늘인다.
            // 정수 업스케일이면 원본 크기 × 배율을 사각형 가운데에 놓는다 — 셰이더가 점 샘플(SW_PASS_FLAG_POINT_UPSCALE)이라 원본 픽셀이 배율 × 배율 칸이다.
            if ( bFullRect == false )
            {
                const float4& rect = activeView._settings._screenRect;
                RHIViewport   viewport{};
                viewport._x      = rect._x * static_cast<float32>( outputWidth );
                viewport._y      = rect._y * static_cast<float32>( outputHeight );
                viewport._width  = rect._z * static_cast<float32>( outputWidth );
                viewport._height = rect._w * static_cast<float32>( outputHeight );
                if ( upscaleZoom > 0 )
                {
                    const TransientAttachmentPool& pool = activePool();
                    viewport._x                         = static_cast<float32>( FrameRendererUtil::computeUpscaleOffset(
                        static_cast<uint32>( viewport._x ), static_cast<uint32>( viewport._width ), pool.getWidth(), upscaleZoom ) );
                    viewport._y                         = static_cast<float32>( FrameRendererUtil::computeUpscaleOffset(
                        static_cast<uint32>( viewport._y ), static_cast<uint32>( viewport._height ), pool.getHeight(), upscaleZoom ) );
                    viewport._width                     = static_cast<float32>( pool.getWidth() * upscaleZoom );
                    viewport._height                    = static_cast<float32>( pool.getHeight() * upscaleZoom );
                }
                context._pCmd->setViewport( viewport );
            }
            if ( bUpscale )
            {
                const float4& rect      = activeView._settings._screenRect;
                const float32 viewportX = bFullRect ? 0.0f : rect._x * static_cast<float32>( outputWidth );
                const float32 viewportY = bFullRect ? 0.0f : rect._y * static_cast<float32>( outputHeight );
                drawUpscaleRcas( context, target._format, viewportX, viewportY );
            }
            else
            {
                drawFullscreen( context, psoBlit );
            }
            context._pCmd->endRenderPass();
            if ( bCaptureToBack )
                context._pCmd->blitTexture( dstTarget, 0 );
        }
        else if ( src != 0 )
        {
            context._pCmd->blitTexture( src, dstTarget );
            if ( bCaptureToBack )
                context._pCmd->blitTexture( dstTarget, 0 );
        }
        else
        {
            RHIRenderPassBeginInfo beginInfo{};
            beginInfo.setColorTarget( dstTarget, _clearColor, RHIRenderPassLoadOp::Load );
            beginInfo._bBindColor = SW_TRUE;
            context._pCmd->beginRenderPass( beginInfo );
            drawFullscreen( context, 0 );
            context._pCmd->endRenderPass();
        }
        if ( target._bCaptureFromOutput == SW_TRUE && isLastSwapchainWriter( passDesc ) )
            _capture.copyPresentFromOutput( *context._pCmd, target._texture, target._width, target._height, target._format );
    }

    void FrameRenderer::executeCanvasPass( const PassExecuteInput& input )
    {
        // 화면 2D 를 주 출력(Present 가 그린 것)에 기존 내용을 유지한 채(Load) 그린다. 주 출력에 **마지막으로 그리는 뷰**만 그린다
        // (`_pMainOutputCanvasView` — 화면 사각형 뷰가 있으면 그 마지막 것, 없으면 주 시점). 그래야 PiP · 분할 화면이 UI 를 덮지 않는다.
        // 렌더 텍스처 · 호스트 타깃 뷰에는 그리지 않는다. 그릴 것이 없으면 렌더 패스를 열지 않는다. 대상 전체가 뷰포트다(화면 사각형 설정과 무관).
        FramePassContext&            context   = input._context;
        const PresentTarget          target    = resolvePresentTarget();
        const bool                   bDraw     = _pActiveView == _pMainOutputCanvasView && _canvasFrame._mainOutput.isEmpty() == false && target._width > 0 && target._height > 0;
        const RHIPipelineStateHandle psoCanvas = bDraw ? findOutputPSO( RenderPassType::Canvas, target._format ) : RHIPipelineStateHandle{ 0 };
        if ( psoCanvas != 0 )
        {
            RHIRenderPassBeginInfo beginInfo{};
            beginInfo._bBindColor        = SW_TRUE;
            beginInfo._arrColorTarget[0] = target._texture;
            beginInfo._colorTargetCount  = 1;
            beginInfo._arrLoadOp[0]      = RHIRenderPassLoadOp::Load;
            beginInfo._width             = target._width;
            beginInfo._height            = target._height;
            context._pCmd->beginRenderPass( beginInfo );
            (void)_canvasRenderer.drawList( *context._pCmd, _canvasFrame._mainOutput, _canvasRenderer.getMainQuadBase(), psoCanvas, target._width, target._height,
                                            _pDevice->supportsNativeBindlessSampling(), _canvasFrame._colorVisionMode );
            context._pCmd->endRenderPass();
        }
        const bool bLastWriter = isLastSwapchainWriter( input._passDesc );
        if ( target._bCaptureToBack == SW_TRUE && bLastWriter )
            context._pCmd->blitTexture( target._texture, 0 );
        if ( target._bCaptureFromOutput == SW_TRUE && bLastWriter )
            _capture.copyPresentFromOutput( *context._pCmd, target._texture, target._width, target._height, target._format );
    }

    void FrameRenderer::executeModulePass( FramePassContext& context, const RenderGraphPassDesc& passDesc, const hashed_string& target, const float4& targetClear )
    {
        // 렌더 스레드(병렬 기록 워커)에서 모듈 코드를 부르지 않는다. 레지스트리는 락 안에서 상수를 **값으로** 복사해 줄 뿐이다 — 모듈이 언로드되어도(핫 리로드)
        // 이 위치에서 그 모듈의 함수 · vtable 을 밟을 일이 없다. 상수 이름은 모듈 셰이더의 패스 상수 필드 이름이다(바인더가 리플렉션 이름으로 맞춘다).
        RHIPipelineStateHandle pso{ 0 };
        const bool             bHasPSO = _pPSOCache->findModulePassPSO( passDesc._resolvedModulePass, pso ) && pso != 0;
        // 클라이언트 엔진은 늘 서비스 범위에 넣는다 — 목록의 선택 행인 것은 렌더러가 없는 Server 타깃 때문이다.
        ModuleRenderPassRegistry* const pRegistry = engine::findSubsystem<ModuleRenderPassRegistry>();
        if ( pRegistry == nullptr ) // 단언은 Release 에서 사라져 아래가 널 역참조가 된다 — 레지스트리가 없으면 이 패스를 그리지 않고 이유를 남긴다
        {
            SW_LOG_ERROR( "Module pass '%#' is not drawn: the ModuleRenderPassRegistry service is not bound", passDesc._resolvedModulePass.c_str() );
            return;
        }
        vector<ModuleRenderPassRegistry::Constant> listConstant;
        const bool                                 bRegistered = pRegistry->copyConstants( passDesc._resolvedModulePass, listConstant );
        if ( bHasPSO == false || bRegistered == false )
        {
            if ( pRegistry->noteMissingOnce( passDesc._resolvedModulePass ) )
                SW_LOG_ERROR( "Module pass '%#' (pipeline pass '%#') is not drawn: %#", passDesc._resolvedModulePass.c_str(), passDesc._name,
                              bRegistered == false ? "no module registered it (ModuleRenderPassRegistry)" : "its PSO could not be created (see the shader error above)" );
            return;
        }
        // 모듈 상수는 이 패스 안에서만 산다. 이름은 엔진 패스 상수 칸(PassCB)의 이름이라 뒤 패스(Present 의 외곽선 등)가 같은 칸을 읽는다 — 쓰기 전의 값을 떠 두었다가
        // 그린 뒤 되돌린다(없던 칸은 지운다). 되돌리지 않으면 모듈이 빌린 칸의 값이 뒤 패스의 그림을 바꾼다.
        struct SavedConstant
        {
            hashed_string _name{};
            float4        _value{};
            bool          _bExisted{ false };
        };
        vector<SavedConstant> listSaved;
        listSaved.reserve( listConstant.size() );
        for ( const ModuleRenderPassRegistry::Constant& constant : listConstant )
        {
            SavedConstant saved;
            saved._name = constant._name;
            uint32       valueSize{ 0 };
            const uint8* pBytes = context._passValues.find( constant._name, valueSize );
            if ( pBytes != nullptr && valueSize == sizeof( float4 ) )
            {
                saved._bExisted = true;
                Memory::copy( &saved._value, pBytes, sizeof( float4 ) );
            }
            listSaved.push_back( saved );
            context._passValues.setFloat4( constant._name, constant._value );
        }
        // 지울지는 실제로 그릴 때 정한다 — 그리지 않은 패스가 "지웠다" 고 적으면 뒤 패스가 지우지 않은 첨부를 불러 온다.
        if ( beginColorPass( context, target.view(), "", targetClear, colorLoadFor( target ), RHIRenderPassLoadOp::Load ) )
        {
            drawFullscreen( context, pso );
            context._pCmd->endRenderPass();
        }
        for ( const SavedConstant& saved : listSaved )
        {
            if ( saved._bExisted )
                context._passValues.setFloat4( saved._name, saved._value );
            else
                context._passValues.remove( saved._name );
        }
    }

    bool FrameRenderer::beginColorPass( FramePassContext& context, string_view colorName, string_view depthName, const float4& clearColor,
                                        RHIRenderPassLoadOp colorLoad, RHIRenderPassLoadOp depthLoad )
    {
        const string_view         arrName[]  = { colorName };
        const float4              arrClear[] = { clearColor };
        const RHIRenderPassLoadOp arrLoad[]  = { colorLoad };
        return beginColorPassMrt( context, arrName, arrClear, arrLoad, 1, depthName, depthLoad );
    }

    bool FrameRenderer::beginColorPassMrt( FramePassContext& context, const string_view* pColorNames, const float4* pTargetClearColor, const RHIRenderPassLoadOp* pColorLoad,
                                           uint32 colorCount, string_view depthName, RHIRenderPassLoadOp depthLoad )
    {
        const bool bInvalidPassTargets = _pDevice == nullptr || context._pCmd == nullptr || pColorNames == nullptr || colorCount == 0;
        if ( bInvalidPassTargets )
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
            // 렌더 패스(뷰포트, 시저)는 타깃의 크기를 따른다. 나눗수가 있는 첨부는 프레임보다 작다. 한 패스의 타깃은 크기가 같다(검증).
            if ( colorTargetIndex == 0 && target._texture != 0 )
            {
                beginInfo._width  = target._width;
                beginInfo._height = target._height;
            }
            // 핸들 0은 백버퍼다. 없는 첨부를 그대로 열면 패스가 화면에 그린다. 열지 않고 알린다(패스 경로라 한 번만).
            if ( beginInfo._arrColorTarget[colorTargetIndex] == 0 )
            {
                if ( _bMissingColorTargetLogged.exchange( 1, std::memory_order_relaxed ) == 0 )
                    SW_LOG_ERROR( "Color target '%#' does not exist this frame - skipping the pass (handle 0 of a missing attachment is the back buffer, so opening it as is would draw to the screen). "
                                  "Check _listOutput and _listAttachment of the pipeline.",
                                  pColorNames[colorTargetIndex] );
                return false;
            }
            beginInfo._arrLoadOp[colorTargetIndex] = pColorLoad != nullptr ? pColorLoad[colorTargetIndex] : RHIRenderPassLoadOp::Clear;
            if ( pTargetClearColor != nullptr )
                beginInfo._arrClearColor[colorTargetIndex] = pTargetClearColor[colorTargetIndex];
        }
        context._pCmd->beginRenderPass( beginInfo );
        return true;
    }

    void FrameRenderer::beginDepthOnlyPass( FramePassContext& context, string_view depthName, float32 clearDepth, RHIRenderPassLoadOp depthLoad )
    {
        if ( context._pCmd == nullptr )
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
        context._pCmd->beginRenderPass( beginInfo );
    }

    uint32 FrameRenderer::computeShadowTileCountPerView() const
    {
        return _frameShadow._layout.getCascadeCount() + static_cast<uint32>( _frameShadow._listLocalTile.size() );
    }

    void FrameRenderer::drawShadowTiles( FramePassContext& context, bool bTransparentBatch )
    {
        // 그림자는 모든 타일을 덮는 상자로 거른 목록을 쓴다(언리얼의 뷰별 인스턴스 컬링에 해당한다). 끝나면 지금 뷰의 슬롯으로 돌아간다.
        const ViewShadowData&        shadow   = _pActiveView->_shadow;
        const RHIPipelineStateHandle pso      = getEnginePSO( RenderPassType::Shadow );
        const uint32                 viewCull = context._cullViewIndex;
        context._cullViewIndex                = _pActiveView->_shadowCullSlot;

        // 캐스케이드 하나가 아틀라스 전체이고 점광 · 스폿 타일이 없으면 아틀라스가 생기기 전과 같다 — 뷰포트도 행렬도 바꾸지 않는다.
        const bool bWholeAtlas = shadow._cascadeCount <= 1 && _frameShadow._listLocalTile.empty() &&
                                 shadow._arrCascadeTile[0]._cell._size == _frameShadow._layout.getAtlasResolution();
        if ( bWholeAtlas )
        {
            drawSceneMeshes( context, pso, bTransparentBatch );
        }
        else
        {
            // 타일마다 그 칸을 뷰포트로 잡고(가위도 같이 잡힌다) 그 타일의 행렬로 씬을 그린다. 상수가 바뀌므로 드로우는 새 상수 버퍼 슬롯을 받는다.
            const PassConstantNames& names     = passConstantNames();
            const uint32             tileCount = shadow._cascadeCount + static_cast<uint32>( _frameShadow._listLocalTile.size() );
            for ( uint32 tileIndex = 0; tileIndex < tileCount; ++tileIndex )
            {
                const ShadowTileDraw& tile = tileIndex < shadow._cascadeCount ? shadow._arrCascadeTile[tileIndex] : _frameShadow._listLocalTile[tileIndex - shadow._cascadeCount];
                RHIViewport           viewport{};
                viewport._x      = static_cast<float32>( tile._cell._x );
                viewport._y      = static_cast<float32>( tile._cell._y );
                viewport._width  = static_cast<float32>( tile._cell._size );
                viewport._height = static_cast<float32>( tile._cell._size );
                context._pCmd->setViewport( viewport );
                // 타일마다 새 상수 슬롯을 잡는다. 슬롯은 패스당 하나라서 같은 슬롯에 행렬만 다시 올리면, GPU 가 나중에 읽는 DX12 · Vulkan 에서는
                // 모든 타일이 마지막에 쓴 행렬을 본다(DX11 · GL 은 업데이트가 명령 순서대로 들어가 가려진다).
                acquirePassCb( context );
                context._passValues.setMatrix( names._lightViewProj, tile._viewProj );
                drawSceneMeshes( context, pso, bTransparentBatch );
            }
            // 이 패스 컨텍스트는 패스마다 시드의 사본이지만, 기준 행렬로 되돌려 두면 뒤에 무엇이 붙어도 셰이딩과 같은 행렬을 본다.
            context._passValues.setMatrix( names._lightViewProj, shadow._lightViewProj );
        }
        context._cullViewIndex = viewCull;
    }

    void FrameRenderer::registerPassTexture( FramePassContext& context, const hashed_string& canonicalName, string_view attachmentName )
    {
        const TransientAttachmentPool::Attachment attachment = findTransientAttachment( attachmentName );
        if ( attachment._texture != 0 && context._pCmd != nullptr )
            context._pCmd->prepareTextureForShaderRead( attachment._texture );
        context._resourceRegistry.registerTexture( canonicalName, attachment._texture, attachment._srv );
        // 원본을 비켜 읽는 효과(블룸)는 그 원본의 텍셀 크기로 비켜야 한다. 반해상도 첨부면 프레임 텍셀의 두 배다. context 는 패스마다 시드의 사본이라 다음 패스로 새지 않는다.
        const bool bSourceColorAttachment = canonicalName == attachmentNames()._sourceColor && attachment._width > 0 && attachment._height > 0;
        if ( bSourceColorAttachment )
        {
            const float32 width  = static_cast<float32>( attachment._width );
            const float32 height = static_cast<float32>( attachment._height );
            context._passValues.setFloat4( passConstantNames()._sourceTexel, float4{ 1.0f / width, 1.0f / height, width, height } );
        }
    }

    void FrameRenderer::commitBindlessTextureBindings( FramePassContext& context )
    {
        if ( _pDevice == nullptr || context._pCmd == nullptr )
            return;

        // 주의: 여기서 updatePassConstants 를 부르지 않는다. 이 함수는 드로우 루프 안에서 불리므로 그러면
        // 드로우마다 라이트와 뷰 행렬을 다시 만들고(정규화, 외적, 4x4 곱 두 번) 카메라를 다시 찾고
        // hashed_string 을 여덟 개씩 intern 한다. 그 값들은 모두 프레임 상수라 execute/executePacket
        // 이 프레임 시드(_frameContext)에 한 번만 채우면 되고, 패스 컨텍스트는 그 시드를 복사해 간다.
        // 드로우마다 바뀌는 것은 g_World 하나뿐이고 그것은 bindForDraw 가 넣는다.

        // DX11 과 GL 은 PassCB 인덱스를 t0..t3 에 바인딩한다(bindless 에뮬레이션).
        // DX12 와 VK 는 셰이더가 힙이나 배열을 직접 인덱싱한다. 리플렉션 바인더가 g_<Name>Index 를 채운다.
        if ( _pDevice->supportsNativeBindlessSampling() )
            return;

        // 이 함수는 드로우 루프 안에서 불린다. 이름은 attachmentNames() 의 intern 된 것으로 찾는다. 주의: 이름을
        // 문자열로 받으면 **드로우마다** intern 하게 된다(FNV 해시 + 32-way 샤드 뮤텍스 × 4).
        auto srvOf = [&context]( const hashed_string& name ) -> RHIDescriptorIndex
        {
            const RegisteredTexture* pTex = context._resourceRegistry.findTexture( name );
            return pTex != nullptr ? pTex->_srv : kInvalidDescriptorIndex;
        };

        const AttachmentNames&   names  = attachmentNames();
        const RHIDescriptorIndex shadow = srvOf( names._shadowMap );
        const RHIDescriptorIndex albedo = srvOf( names._gbufferAlbedo );
        const RHIDescriptorIndex normal = srvOf( names._gbufferNormal );
        const RHIDescriptorIndex depth  = srvOf( names._sceneDepth );
        const RHIDescriptorIndex source = srvOf( names._sourceColor );
        const RHIDescriptorIndex ao     = srvOf( names._ambientOcclusion );
        const RHIDescriptorIndex bloom  = srvOf( names._bloomBright );
        const RHIDescriptorIndex cookie = srvOf( names._light2DCookieAtlas );
        const RHIDescriptorIndex expo   = srvOf( names._autoExposure );
        // G버퍼 머티리얼 칸은 t0..t3 밖의 자기 슬롯이다(조명 패스가 다섯째로 읽는다 — shaderslot::kEngineGBufferMaterialTexture).
        const RHIDescriptorIndex material = srvOf( names._gbufferMaterial );
        if ( material != kInvalidDescriptorIndex )
            context._pCmd->bindShaderResource( material, shaderslot::kEngineGBufferMaterialTexture );

        // 슬롯 테이블은 binding.hlsli 의 Bindless::sampleIndex 와 같아야 한다: [shadow|source, albedo|ao|light2dCookie, normal|bloom|autoExposure, depth|shadow].
        // 한 슬롯을 나눠 쓰는 것들은 같은 패스에서 함께 읽히지 않는다(알베도 · 노멀은 Lighting · SSAO, AO · 반해상도 블룸은 Bloom, 쿠키는 빛 받는 스프라이트 — 2D 패스엔 알베도 · AO 가 없다,
        // 자동 노출은 톤맵하는 패스 — Tonemap · 후처리 체인 Present 는 노멀 · 반해상도 블룸을 읽지 않는다).
        const RHIDescriptorIndex slot0 = ( shadow != kInvalidDescriptorIndex ) ? shadow : source;
        const RHIDescriptorIndex slot1 = ( albedo != kInvalidDescriptorIndex ) ? albedo : ( ao != kInvalidDescriptorIndex ? ao : cookie );
        const RHIDescriptorIndex slot2 = ( normal != kInvalidDescriptorIndex ) ? normal : ( bloom != kInvalidDescriptorIndex ? bloom : expo );
        const RHIDescriptorIndex slot3 = ( depth != kInvalidDescriptorIndex ) ? depth : shadow;

        if ( shadow != kInvalidDescriptorIndex || source != kInvalidDescriptorIndex )
            context._pCmd->bindShaderResource( slot0, 0 );
        if ( slot1 != kInvalidDescriptorIndex )
            context._pCmd->bindShaderResource( slot1, 1 );
        if ( slot2 != kInvalidDescriptorIndex )
            context._pCmd->bindShaderResource( slot2, 2 );
        if ( depth != kInvalidDescriptorIndex || shadow != kInvalidDescriptorIndex )
            context._pCmd->bindShaderResource( slot3, 3 );
    }

    FrameRenderer::PresentTarget FrameRenderer::resolvePresentTarget() const
    {
        // 렌더 텍스처와 호스트 타깃 뷰는 자기 출력 전체, 주 시점과 화면 사각형 뷰는 주 출력(백버퍼, 게임 뷰 RT)이다. 스크린샷 실행이면 백버퍼 대신 캡처
        // 텍스처에 그린다. 백버퍼는 핸들이 없어 읽을 수 없고, 후처리와 UI 가 끝난 최종 화면을 볼 방법이 그것뿐이다. 캡처는 주 출력만 받는다.
        const ViewTarget& activeView = *_pActiveView;
        RHITextureHandle  ownOutput{ 0 };
        const bool        bExtraViewOwnOutput = isRenderingExtraView() && activeView._outputKind == RenderViewOutputKind::RenderTexture && activeView._pOutputTexture != nullptr;
        if ( bExtraViewOwnOutput )
            ownOutput = activeView._pOutputTexture->getHandle();
        else if ( isRenderingExtraView() && activeView._outputKind == RenderViewOutputKind::HostTarget )
            ownOutput = activeView._hostTarget;
        const bool bOwnOutput = ownOutput != 0;
        const bool bCapture   = bOwnOutput == false && ( _outputRenderTarget == 0 ) && _capture.isPresentActive();
        // 캡처를 백버퍼로 옮기는 것은 출력이 곧 백버퍼일 때만이다. 크기를 덮어쓴 출력(초상화 베이크)은 화면에 나가지 않는다(크기가 다르면 복사도 안 된다).
        const bool bCaptureToBack = bCapture && _outputWidth == _pDevice->getBackBufferWidth() && _outputHeight == _pDevice->getBackBufferHeight();
        // 출력이 RT(에디터 게임 뷰)면 그 RT 에 그대로 그리고 끝에 캡처로 복사한다. 화면 사각형 뷰도 같은 RT 에 겹쳐 그리므로 뷰마다 복사하면
        // 마지막 복사가 화면과 같다.
        const bool bCaptureFromOutput = bOwnOutput == false && _outputRenderTarget != 0 && _capture.isPresentActive();

        PresentTarget target{};
        target._texture = bOwnOutput ? ownOutput : bCapture ? _capture.getPresentTexture()
                                                            : _outputRenderTarget;
        target._width   = bOwnOutput ? activeView._outputWidth : _outputWidth;
        target._height  = bOwnOutput ? activeView._outputHeight : _outputHeight;
        // 백버퍼는 디바이스가 채택한 포맷(Vulkan 은 서피스 협상 결과)이고, 텍스처는 그 텍스처가 기록한 포맷이다.
        target._format             = ( target._texture == 0 ) ? _pDevice->getBackBufferFormat() : _pDevice->getResourceFactory()->getTextureFormat( target._texture );
        target._bOwnOutput         = bOwnOutput ? SW_TRUE : SW_FALSE;
        target._bCapture           = bCapture ? SW_TRUE : SW_FALSE;
        target._bCaptureToBack     = bCaptureToBack ? SW_TRUE : SW_FALSE;
        target._bCaptureFromOutput = bCaptureFromOutput ? SW_TRUE : SW_FALSE;
        return target;
    }

    bool FrameRenderer::isLastSwapchainWriter( const RenderGraphPassDesc& passDesc ) const
    {
        return _pLastSwapchainWriter == nullptr || &passDesc == _pLastSwapchainWriter;
    }

    const RenderGraphPassDesc* FrameRenderer::findTonemapPass() const
    {
        for ( const RenderGraphPassDesc& pass : _pipelineResource.getGraphPass() )
        {
            if ( passAppliesTonemap( pass ) )
                return &pass;
        }
        return nullptr;
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
