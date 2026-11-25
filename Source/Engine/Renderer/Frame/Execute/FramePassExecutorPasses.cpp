/**
 * @file FramePassExecutorPasses.cpp
 * @brief 패스 라이브러리입니다 — 패스 종류마다의 기록(풀스크린 · 컴퓨트 · 포워드 · G버퍼 · 투명 · 하늘 · 안개 · 왜곡 · SSR · 후처리 머티리얼 · 데칼 · TAA · 출력 · 캔버스)과 모듈 확장 패스.
 * @details `FramePassExecutor` 의 한 부분이다 — 그래프 짓기 · 제출은 `FramePassExecutor.cpp` 에 있다.
 */
#include "pch.h"

#include "Core/Algorithm/VectorUtil.h"
#include "Core/Container/array.h"
#include "Core/Container/frame_vector.h"
#include "Core/Container/string.h"
#include "Core/Encoding/FourCCUtil.h"
#include "Core/Hash/HashUtil.h"
#include "Core/Log/LogOnce.h"
#include "Core/Log/Logger.h"
#include "Core/Math/MathUtil.h"
#include "Core/Math/MatrixMath.h"
#include "Core/String/formatString.h"

#include "Engine/Asset/AssetManager.h"
#include "Engine/Config/EngineDefaultAssets.h"
#include "Engine/Foundation/EngineServices.h"
#include "Engine/Graphics/Debug/GPUBreadcrumbs.h"
#include "Engine/Graphics/Debug/GPUPixelInspector.h"
#include "Engine/Graphics/Material/Material.h"
#include "Engine/Graphics/Material/MaterialInstance.h"
#include "Engine/Graphics/Mesh/Mesh.h"
#include "Engine/Graphics/Mesh/MeshAssetFormat.h"
#include "Engine/Graphics/Mesh/MeshCache.h"
#include "Engine/Graphics/Mesh/MeshRenderData.h"
#include "Engine/Graphics/Mesh/MeshUtil.h"
#include "Engine/Graphics/RHI/Diagnostics/BindlessIndexValidator.h"
#include "Engine/Graphics/RHI/IRHICommandList.h"
#include "Engine/Graphics/RHI/IRHIDevice.h"
#include "Engine/Graphics/RHI/IRHIResourceFactory.h"
#include "Engine/Graphics/RHI/IRHITimestampProvider.h"
#include "Engine/Graphics/RHI/RHIDescriptors.h"
#include "Engine/Graphics/RHI/RHIMeshVertexStreams.h"
#include "Engine/Graphics/RHI/RHIPipelineStateDesc.h"
#include "Engine/Graphics/Shader/Binding/EngineComputeShaders.h"
#include "Engine/Graphics/Shader/Binding/ShaderBindingLayout.h"
#include "Engine/Graphics/Shader/Binding/ShaderBindingSlots.h"
#include "Engine/Graphics/Shader/Compile/ShaderCompiler.h"
#include "Engine/Graphics/Texture/Texture2D.h"
#include "Engine/Graphics/Texture/TextureCache.h"
#include "Engine/Graphics/Texture/TextureLayerBake.h"
#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManager.h"
#include "Engine/Profiling/FrameProfiler.h"
#include "Engine/Reflection/ReflectionCast.h"
#include "Engine/Renderer/Capture/FrameCapture.h"
#include "Engine/Renderer/Extension/FrameExtensionHost.h"
#include "Engine/Renderer/Extension/RenderExtensionRegistry.h"
#include "Engine/Renderer/Frame/Execute/FramePassExecutor.h"
#include "Engine/Renderer/Frame/Execute/ViewGraphResource.h"
#include "Engine/Renderer/Frame/FrameRenderState.h"
#include "Engine/Renderer/Frame/FrameRendererUtil.h"
#include "Engine/Renderer/Frame/PostProcess/FrameAutoExposure.h"
#include "Engine/Renderer/Frame/PostProcess/FrameFog.h"
#include "Engine/Renderer/Frame/PostProcess/FramePostGrade.h"
#include "Engine/Renderer/Frame/PostProcess/FrameUpscale.h"
#include "Engine/Renderer/Frame/Resource/FramePSOManager.h"
#include "Engine/Renderer/Frame/Resource/RenderPSOCache.h"
#include "Engine/Renderer/Frame/Resource/ShaderParameterBinder.h"
#include "Engine/Renderer/Frame/Screen/FrameDisplayEncode.h"
#include "Engine/Renderer/Frame/Screen/FrameScreenRotation.h"
#include "Engine/Renderer/Lighting/ShadowFilter.h"
#include "Engine/Renderer/Pipeline/GBufferLayout.h"
#include "Engine/Renderer/Pipeline/RenderPassKindInfo.h"
#include "Engine/Renderer/Pipeline/RenderPipelineAsset.h"
#include "Engine/Visual/Component/MeshComponent.h"

namespace sw
{
    namespace
    {
        /// 데칼 상자 한 그리기의 정점 수 — 면 여섯 × 삼각형 둘 × 정점 셋(decal.hlsl 의 kDecalBoxCorner 표와 같은 수).
        constexpr uint32 kDecalBoxVertexCount = 36;

        struct FramePassExecutorInternal
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

        /**
         * @brief 패스 상수를 쓰고 소멸에서 되돌립니다. 이름은 엔진 패스 상수 칸(PassCB)의 이름이라 같은 컨텍스트의 뒤 드로우가 같은 칸을 읽습니다 — 쓰기 전의 값을 떠 두었다가
         *        되돌립니다(없던 칸은 지웁니다). 되돌리지 않으면 요소 하나가 빌린 칸의 값이 뒤 요소의 그림을 바꾼다.
         */
        template <typename TContext>
        class ExtensionConstantScope
        {
        public:
            ExtensionConstantScope( TContext& context, LinearAllocator& frameArena )
                : _context{ context }
                , _listSaved{ FrameAllocator<Saved>{ frameArena } }
            {
            }
            ~ExtensionConstantScope()
            {
                for ( const Saved& saved : _listSaved )
                {
                    if ( saved._bExisted )
                        _context._passValues.setFloat4( saved._name, saved._value );
                    else
                        _context._passValues.remove( saved._name );
                }
            }
            ExtensionConstantScope( const ExtensionConstantScope& )            = delete;
            ExtensionConstantScope& operator=( const ExtensionConstantScope& ) = delete;

            void apply( const ExtensionConstant* pConstant, uint32 count )
            {
                for ( uint32 index = 0; index < count; ++index )
                {
                    Saved saved;
                    saved._name = pConstant[index]._name;
                    uint32       valueSize{ 0 };
                    const uint8* pBytes = _context._passValues.find( saved._name, valueSize );
                    if ( pBytes != nullptr && valueSize == sizeof( float4 ) )
                    {
                        saved._bExisted = true;
                        Memory::copy( &saved._value, pBytes, sizeof( float4 ) );
                    }
                    _listSaved.push_back( saved );
                    _context._passValues.setFloat4( saved._name, pConstant[index]._value );
                }
            }

        private:
            struct Saved
            {
                hashed_string _name{};
                float4        _value{};
                bool          _bExisted{ false };
            };
            TContext&           _context;
            frame_vector<Saved> _listSaved; ///< 프레임 아레나(요소마다 만들고 버린다 — 힙을 건드리지 않는다)
        };
    } // namespace

    // 일반 풀스크린 패스는 테이블의 플래그로 고르고, 전용 실행 코드가 있는 패스만 아래 switch 의 case 를 가진다(짧은 디스패처라 나누지 않는다).
    // 열거자를 추가하고 case 를 빠뜨리면 -Wswitch-enum 이 이 함수를 알려 준다.
    void FramePassExecutor::executePass( FramePassContext& context, const RenderGraphPassDesc& passDesc )
    {
        SW_PROFILE_SCOPE( "RT.Pass.execute" );
        // 타입은 로드 시점에 한 번 해석해 둔 값을 쓴다. 여기서 문자열을 다시 비교하면 디스패치와 PSO 생성이 서로 다른 표기를 받아 줄 여지가 생긴다.
        const RenderPassKind passType        = passDesc._type;
        const string_view    passName        = string_view( passDesc._name );
        const hashed_string& depthAttachment = passDesc._resolvedDepthAttachment;

        if ( context._pCmd == nullptr )
        {
            SW_LOG_ERROR_ONCE_ID( "FramePassExecutor.ExecutePassNoActiveIRHICommandList", "executePass: no active IRHICommandList" );
            return;
        }
        // 이번 뷰에서 꺼진 패스는 실행 계획에 없다(`selectGraphPlan`).

        // 이벤트 표지(캡처의 구간 이름) + GPU 브레드크럼 시작 · 끝 표지 — 디바이스 소실 때 끝난 패스와 돌던 패스를 이름으로 싣는다. 어느 길로 돌아가도 짝이 맞는다.
        const GPUPassScope passScope{ *context._pCmd, passName };
        context._resourceRegistry.reset();
        context._passType = passType;
        // 라이트는 패스 종류를 가리지 않는다. 포워드 지오메트리도, 디퍼드 풀스크린 조명도 읽는다.
        // 그래서 인스턴스 버퍼(지오메트리 패스 전용)와 달리 여기서 모든 패스에 바인딩한다.
        registerLightBuffer( context );
        registerSurfaceDeformLayer( context );
        registerRainOcclusion( context );
        // 후처리 텍스처(LUT · 먼지 마스크)도 패스를 가리지 않는다 — 톤맵이 Tonemap 패스에도 후처리 체인(Present)에도 있고, 파이프라인 XML 이 자리를 정한다.
        registerPostProcessTextures( context );

        // 이 패스가 바인딩할 뎁스다. 파이프라인 XML 의 `_depthAttachment` 가 기준이고, 비어 있으면
        // 뎁스 없이 연다. "이 패스는 일부러 뎁스를 쓰지 않는다" 를 선언으로 표현할 수 있어야 한다.
        // 선언한 이름이 이번 프레임에 실제로 없으면(트랜지언트가 할당되지 않았으면) 뎁스 없이 진행한다.
        const hashed_string passDepth = ( depthAttachment.empty() == false && findTransient( depthAttachment ) != 0 )
                                          ? depthAttachment
                                          : hashed_string{};

        // 컬러 타깃은 파이프라인이 선언한 컬러 출력이다(로드 때 해석한 `_listResolvedColorOutput`, 선언 순서). 컬러를 그리는 패스(kDrawsDeclaredColor)는
        // 로드가 선언을 요구하므로 비어 있지 않다. 이름(SceneColor · GBufferAlbedo · TAAColor 등)으로 짐작하지 않는다 — 다른 이름을 쓰는
        // 파이프라인에서 없는 첨부(핸들 0 — 무효)를 열었다. 뎁스 로드 연산은 바인딩한 뎁스의 클리어 기록으로 정한다.
        const vector<RenderGraphPassDesc::ResolvedAttachment>& declaredColor = passDesc._listResolvedColorOutput;

        const RenderPassKindInfo& info = getRenderPassKindInfo( passType );
        // 로드가 막았어야 할 상태다(검증 3-1). 여기서는 그리지 않고 끝낸다 — 백버퍼에 그리는 것보다 낫다.
        const bool bMissingDeclaredColor = info.hasFlag( RenderPassTraitFlag::kDrawsDeclaredColor ) && declaredColor.empty();
        SW_ASSERT( bMissingDeclaredColor == false );
        if ( bMissingDeclaredColor )
            return;
        const PassExecuteInput input{ context, passType, passDesc, declaredColor, passDepth, info.hasFlag( RenderPassTraitFlag::kDrawsTransparentBatch ) };
        if ( info.hasFlag( RenderPassTraitFlag::kGenericFullscreen ) )
        {
            executeGenericFullscreenPass( input );
        }
        else
        {
            switch ( passType )
            {
                case RenderPassKind::Shadow:
                {
                    // 클리어 값은 실제로 바인딩하는 뎁스 첨부의 선언에서 읽는다(이름 ShadowMap 으로 찾지 않는다).
                    // 깊이 첨부의 역할이 그릴 타일을 고른다 — ShadowMap 이면 방향광 캐스케이드, LocalShadowMap 이면 점광 · 스폿 타일(아틀라스 둘, 감사 RN-4).
                    // 그림자 캐시 층은 점광 · 스폿 아틀라스 패스 앞에 다시 그리고(다른 깊이 대상이다) 셰이더 읽기로 돌린다 — 렌더 패스 안에서는 상태를 바꿀 수 없다.
                    const bool bShadows     = _pActiveView->_settings._bShadows == SW_TRUE;
                    const bool bLocalShadow = passDesc._resolvedDepthRole == static_cast<uint8>( RenderPassInputRole::LocalShadowMap );
                    const bool bCacheLayer  = bShadows && bLocalShadow && prepareShadowCacheLayer( context, passDepth, input._bTransparentBatch );
                    beginDepthOnlyPass( context, passDepth, getColorLoadFor( passDepth ) );
                    // 그림자를 끈 뷰는 지우기만 한다. 깊이 1(가장 멀다)이면 모두 빛을 받는다.
                    if ( bShadows && bLocalShadow )
                        drawLocalShadowTiles( context, input._bTransparentBatch, bCacheLayer );
                    else if ( bShadows )
                        drawCascadeShadowTiles( context, input._bTransparentBatch );
                    context._pCmd->endRenderPass();
                    break;
                }
                case RenderPassKind::DepthPrepass:
                {
                    beginDepthOnlyPass( context, passDepth, getColorLoadFor( passDepth ) );
                    drawSceneMeshes( context, _psoManager.getEnginePSO( RenderPassKind::DepthPrepass ), input._bTransparentBatch );
                    context._pCmd->endRenderPass();
                    _bHasExecutedDepthPrepass.store( 1 );
                    break;
                }
                case RenderPassKind::LateOcclusion:
                {
                    // 2 단계 가림의 뒤 단계 — 깊이 프리패스가 그린 깊이로 HZB 를 다시 짓고 앞 단계에서 가려졌던 것만 다시 컬링해 이 뒤 불투명 패스가 함께 그린다.
                    // 자원은 모두 이 패스의 선언이다 — 돌지 않는 뷰 · 프레임은 등록이 없어 아무것도 기록하지 않는다(`FrameGraphExecutor::registerViewGraphResources`).
                    _frameGraphExecutor.recordLateOcclusion( *context._pGraphPass, *_pActiveView );
                    break;
                }
                case RenderPassKind::ForwardOpaque:
                case RenderPassKind::ForwardOnlyOpaque:
                {
                    executeForwardOpaquePass( input );
                    break;
                }
                case RenderPassKind::GBuffer:
                {
                    executeGBufferPass( input );
                    break;
                }
                case RenderPassKind::Transparent:
                {
                    executeTransparentPass( input );
                    break;
                }
                case RenderPassKind::Compute:
                {
                    executeComputePass( input );
                    break;
                }
                case RenderPassKind::SceneColorCopy:
                {
                    executeSceneColorCopyPass( input );
                    break;
                }
                case RenderPassKind::MeshOutline:
                {
                    executeMeshOutlinePass( input );
                    break;
                }
                case RenderPassKind::HighlightOutline:
                case RenderPassKind::HighlightSense:
                {
                    executeHighlightPass( input );
                    break;
                }
                case RenderPassKind::Sky:
                {
                    executeSkyPass( input );
                    break;
                }
                case RenderPassKind::Fog:
                {
                    executeFogPass( input );
                    break;
                }
                case RenderPassKind::ScreenSpaceReflection:
                {
                    executeScreenSpaceReflectionPass( input );
                    break;
                }
                case RenderPassKind::LensFlare:
                {
                    executeLensFlarePass( input );
                    break;
                }
                case RenderPassKind::PostProcessMaterial:
                {
                    executePostProcessMaterialPass( input );
                    break;
                }
                case RenderPassKind::Decal:
                {
                    executeDecalPass( input );
                    break;
                }
                case RenderPassKind::TAA:
                {
                    executeTAAPass( input );
                    break;
                }
                case RenderPassKind::Distortion:
                {
                    executeDistortionPass( input );
                    break;
                }
                case RenderPassKind::DistortionApply:
                {
                    executeDistortionApplyPass( input );
                    break;
                }
                case RenderPassKind::Present:
                {
                    executePresentPass( input );
                    break;
                }
                case RenderPassKind::Canvas:
                {
                    executeCanvasPass( input );
                    break;
                }
                // 실행 코드가 없는 타입이다. 패스 서술이 없는 일반 풀스크린 패스이거나, PSO 슬롯만 있는 엔진 내부 타입이다.
                case RenderPassKind::Invalid:
                case RenderPassKind::Lighting:
                case RenderPassKind::SSAO:
                case RenderPassKind::ScreenSpaceGI:
                case RenderPassKind::ContactShadow:
                case RenderPassKind::Bloom:
                case RenderPassKind::BloomDownsample:
                case RenderPassKind::Outline:
                case RenderPassKind::Tonemap:
                case RenderPassKind::DepthOfField:
                case RenderPassKind::MotionBlur:
                case RenderPassKind::MotionBlurTileMax:
                case RenderPassKind::MotionBlurNeighborMax:
                case RenderPassKind::SceneDepthCopy:
                case RenderPassKind::VolumetricClouds:
                case RenderPassKind::ForwardOpaqueNoDepthWrite:
                case RenderPassKind::GPUCull:
                case RenderPassKind::InstanceAnimation:
                case RenderPassKind::InstanceSort:
                case RenderPassKind::InstanceSortMultiPass:
                case RenderPassKind::MeshMorph:
                case RenderPassKind::MeshSkin:
                case RenderPassKind::LightGrid:
                case RenderPassKind::VFXSimulation:
                case RenderPassKind::VFXMeshInstance:
                case RenderPassKind::VFXParticleSort:
                case RenderPassKind::DepthHistory:
                case RenderPassKind::AutoExposure:
                case RenderPassKind::UpscaleEasu:
                case RenderPassKind::UpscaleRcas:
                case RenderPassKind::HZBBuild:
                case RenderPassKind::VolumetricFog:
                case RenderPassKind::CanvasMarker:
                case RenderPassKind::ShadowCacheRestore:
                case RenderPassKind::NanCheck:
                case RenderPassKind::IndirectArgsReset:
                case RenderPassKind::IndirectArgsCompact:
                case RenderPassKind::ScreenRotation:
                case RenderPassKind::SceneVoxelize:
                case RenderPassKind::ProbeUpdate:
                case RenderPassKind::DeferredTileClassify:
                case RenderPassKind::AutoExposureHistogram:
                case RenderPassKind::DisplayEncodeHDR10:
                case RenderPassKind::DisplayEncodeScRGB:
                case RenderPassKind::DisplayCompositeUI:
                {
                    SW_LOG_WARNING_ID( "FramePassExecutor.UnknownPassType", "Unknown pass type '%#' in '%#'", passType, passName );
                    break;
                }
            }
        }
    }

    RHIRenderPassLoadOp FramePassExecutor::getColorLoadFor( const hashed_string& name )
    {
        // 이름은 이미 intern 된 것만 받는다. string_view 를 받으면 패스마다 여기서 다시 intern 하게
        // 된다(FNV + 32-way 샤드 뮤텍스). 타깃 이름은 모두 코드 리터럴이라 attachmentNames() 캐시로 충분하다.
        // 프레임의 첫 사용만 선언의 로드 동작을 따르고(풀에 없는 이름인 백버퍼는 Clear), 그 뒤의 패스는 앞 패스가 쓴 것을 읽는다.
        return markAttachmentCleared( name ) ? findTransientAttachment( name )._ops._loadOp : RHIRenderPassLoadOp::Load;
    }

    void FramePassExecutor::executeFullscreenInto( FramePassContext& context, RHIPipelineStateHandle pso, const hashed_string& targetName, const float4& passClearColor )
    {
        if ( beginColorPass( context, targetName, hashed_string{}, passClearColor, getColorLoadFor( targetName ), RHIRenderPassLoadOp::Load ) == false )
            return;
        drawFullscreen( context, pso );
        context._pCmd->endRenderPass();
    }

    void FramePassExecutor::executeGenericFullscreenPass( const PassExecuteInput& input )
    {
        // 일반 풀스크린 패스(테이블의 kGenericFullscreen: Lighting, SSAO, Bloom, BloomDownsample, Outline, Tonemap)다. 선언이 곧 바인딩이다.
        // 입력은 역할 이름으로 모두 바인딩하고, 타깃은 선언한 첫 컬러 출력이다. 계약(RenderPassInputSignature)이
        // 로드 시점에 같은 목록을 검사했다. 새 포스트 패스는 테이블의 한 줄로 여기를 탄다.
        FramePassContext&          context  = input._context;
        const RenderGraphPassDesc& passDesc = input._passDesc;
        const hashed_string*       pTarget  = &input._declaredColor[0]._attachment;
        // 이 뷰에서 빠진 패스는 셋업이 출력 이름을 원본에 잇거나 비웠다(`applyViewPassSelection` — 꺼진 효과, 읽는 패스가 모두 빠진 패스). 아무것도 기록하지 않는다.
        // 후처리를 끈 뷰 · 품질로 꺼지는 효과(kSkipsWithoutPostProcess)도 모두 이 길이다 — 원본을 복사하거나 셰이더가 원본을 고르던 길은 지웠다.
        if ( activePool().isForwarded( *pTarget ) )
            return;
        if ( input._passType == RenderPassKind::VolumetricClouds )
        {
            executeVolumetricCloudsPass( input );
            return;
        }
        // 컴퓨트 구현(`_implementation="Compute"`)은 네이티브 bindless 에서만 돈다. 에뮬 백엔드는 아래 픽셀 구현이다 — 능력이 정한 길이고 PSO 도 그 백엔드에는 만들지 않는다.
        if ( passDesc._implementation == RenderPassExecutionMode::Compute && _pDevice->supportsNativeBindlessSampling() )
        {
            executeComputeImplementation( input );
            return;
        }
        registerDeclaredInputs( context, passDesc );
        _autoExposure.recordAutoExposure( context, *_pActiveView, passDesc );
        // 디퍼드 조명은 타일 분류를 쓸 수 있으면 종류마다 타일 사각형만 그린다 — 아니면 아래 풀스크린 한 장(모든 엽)이다(능력이 정한 길, 같은 그림).
        if ( input._passType == RenderPassKind::Lighting && executeTiledLighting( input, *pTarget ) )
            return;

        // PSO 가 0이면 `drawFullscreen` 이 파이프라인 설정을 건너뛴다. 대체할 PSO 는 테이블이 정한 것만 쓴다(Tonemap 은 Present).
        const RHIPipelineStateHandle pso = _psoManager.getEnginePSO( input._passType );

        // 클리어 색은 타깃 첨부의 선언이다(SSAO 의 흰색 = 가림 없음도 AOColor 선언에 있다).
        const float4 targetClear = getTargetClearColor( *pTarget );
        executeFullscreenInto( context, pso, *pTarget, targetClear );
    }

    bool FramePassExecutor::isTiledLightingReady( const FrameViewTarget& view ) const
    {
        bool bReady = view._deferredTileCapacity > 0 && _pDevice->supportsNativeBindlessSampling() && _psoManager.getEnginePSO( RenderPassKind::DeferredTileClassify ) != 0;
        for ( uint32 tileClass = 0; tileClass < kDeferredTileClassCount; ++tileClass )
        {
            bReady = bReady && _psoManager.findDeferredTilePSO( tileClass ) != 0;
        }
        return bReady;
    }

    bool FramePassExecutor::executeTiledLighting( const PassExecuteInput& input, const hashed_string& target )
    {
        // 셰이더의 SwRootConstants(deferred_tile_classify.hlsl)와 같은 배치다.
        struct RootConstants
        {
            uint32 _albedoIndex{ kInvalidDescriptorIndex }; ///< G버퍼 알베도의 bindless SRV
            uint32 _width{ 0 };                             ///< 뷰 사각형 크기(px)
            uint32 _height{ 0 };
            uint32 _capacity{ 0 }; ///< 종류 하나의 목록 칸 수
            uint32 _mode{ 0 };     ///< 0 간접 인자 지우기 · 1 분류
        };
        static_assert( sizeof( RootConstants ) / sizeof( uint32 ) <= shaderslot::kRootConstantDwords, "tile classification root constants exceed the root block" );
        constexpr uint32 kModeClear    = 0;
        constexpr uint32 kModeClassify = 1;

        using Internal                                              = FramePassExecutorInternal;
        FramePassContext&                              context      = input._context;
        const RenderGraphPassContext&                  graphPass    = *context._pGraphPass;
        const FrameViewTarget&                         view         = *_pActiveView;
        const RenderGraphPassDesc::ResolvedAttachment* pAlbedoInput = Internal::findInputByRole( input._passDesc, RenderPassInputRole::GBufferAlbedo );
        // 분류를 돌릴 수 있는 뷰만 등록했다(`registerViewResources` · `isTiledLightingReady`) — 판정을 다시 하지 않는다. 둘은 레벨 콜백이 UAV 로 옮겼다(이 패스의 선언).
        if ( graphPass.isRegistered( ViewGraphResource::deferredTileArgs() ) == false || graphPass.isRegistered( ViewGraphResource::deferredTileList() ) == false || pAlbedoInput == nullptr )
            return false;
        const TransientAttachmentPool::Attachment albedo = activePool().find( pAlbedoInput->_attachment );
        RootConstants                             constants{};
        constants._width     = view.scaleToRect( albedo, true );
        constants._height    = view.scaleToRect( albedo, false );
        constants._capacity  = view._deferredTileCapacity;
        const uint32 columns = FrameRendererUtil::divideRoundUp( constants._width, kDeferredTileSize );
        const uint32 rows    = FrameRendererUtil::divideRoundUp( constants._height, kDeferredTileSize );
        // 목록은 풀 크기로 만들었다 — 사각형은 풀 안이라 넘지 않는다. 넘는다면(풀을 다시 만들기 전 한 프레임) 그 프레임은 풀스크린으로 그린다.
        if ( albedo._srv == kInvalidDescriptorIndex || columns * rows > view._deferredTileCapacity || columns == 0 || rows == 0 )
            return false;
        constants._albedoIndex = static_cast<uint32>( albedo._srv );

        // 1) 분류 — 간접 인자를 지우고(그룹 하나) 타일마다 그룹 하나. 렌더 패스 밖이다. G버퍼 알베도는 그래프가 이 패스의 입력으로 셰이더 읽기 상태로 옮겨 두었다.
        IRHIPassCommandList&                                cmd = *context._pCmd;
        ComputeShaderParameters<DeferredTileClassifyShader> parameters;
        parameters.set( DeferredTileClassifyShader::TileArgs, graphPass.getBufferUAV( ViewGraphResource::deferredTileArgs() ) );
        parameters.set( DeferredTileClassifyShader::TileList, graphPass.getBufferUAV( ViewGraphResource::deferredTileList() ) );
        if ( parameters.apply( cmd, _psoManager.getComputePSO( RenderPassKind::DeferredTileClassify ) ) == false )
            return false;
        constants._mode = kModeClear;
        cmd.setComputeRootConstants( sizeof( constants ) / sizeof( uint32 ), &constants );
        cmd.dispatchCompute( 1, 1, 1 );
        graphPass.uavBarrier( ViewGraphResource::deferredTileArgs() );
        constants._mode = kModeClassify;
        cmd.setComputeRootConstants( sizeof( constants ) / sizeof( uint32 ), &constants );
        cmd.dispatchCompute( columns, rows, 1 );
        graphPass.transition( ViewGraphResource::deferredTileArgs(), RenderGraphAccess::IndirectArgs );
        graphPass.transition( ViewGraphResource::deferredTileList(), RenderGraphAccess::ShaderRead );

        // 2) 종류마다 간접 그리기 — 인스턴스 하나가 타일 하나(정점 여섯). 타일 목록은 패스 전용 구조버퍼 칸(t11)에 PSO · 공통 바인딩 뒤에 건다(렌즈 플레어와 같다).
        if ( beginColorPass( context, target, hashed_string{}, getTargetClearColor( target ), getColorLoadFor( target ), RHIRenderPassLoadOp::Load ) == false )
            return true;
        setIdentityWorld( context );
        context._passValues.setUint( passConstantNames()._deferredTileCapacity, view._deferredTileCapacity );
        const RHIBufferSRVIndex tileListSRV = graphPass.getBufferSRV( ViewGraphResource::deferredTileList() );
        const RHIBufferHandle   tileArgs    = graphPass.getBuffer( ViewGraphResource::deferredTileArgs() );
        for ( uint32 tileClass = 0; tileClass < kDeferredTileClassCount; ++tileClass )
        {
            const RHIPipelineStateHandle tilePSO = _psoManager.findDeferredTilePSO( tileClass );
            context.setPipeline( tilePSO );
            if ( bindForDraw( context, tilePSO, kInvalidDescriptorIndex ) == false )
                continue;
            cmd.bindStructuredBuffer( tileListSRV, shaderslot::kPassBuffer );
            cmd.drawIndirect( tileArgs, tileClass * static_cast<uint32>( sizeof( RHIDrawIndirectCommand ) ), 1 );
        }
        cmd.endRenderPass();
        return true;
    }

    void FramePassExecutor::executeComputeImplementation( const PassExecuteInput& input )
    {
        // 셰이더의 SwRootConstants(post_bloom.hlsl · depth_of_field.hlsl 의 SW_PASS_COMPUTE 절)와 같은 배치 — 16 dword 안이다(`shaderslot::kRootConstantDwords`).
        struct RootConstants
        {
            uint32  _outputIndex{ 0 };                       ///< 출력 RW 번호
            uint32  _sourceIndex{ kInvalidDescriptorIndex }; ///< SourceColor 의 bindless 번호
            uint32  _depthIndex{ kInvalidDescriptorIndex };  ///< SceneDepth 의 bindless 번호(읽지 않는 패스는 kInvalidIndex)
            uint32  _width{ 0 };                             ///< 출력 크기
            uint32  _height{ 0 };
            float32 _arrSourceTexel[2]{ 0.0f, 0.0f };                    ///< 원본 텍셀 크기(1 / 원본 크기)
            float32 _arrParam[3]{ 0.0f, 0.0f, 0.0f };                    ///< 블룸 g_BloomParams.xyz · 피사계 심도 g_CameraLens.xyz
            float32 _arrInvViewProjColumn3[4]{ 0.0f, 0.0f, 0.0f, 0.0f }; ///< g_InvViewProj 의 4 열(뷰 깊이 복원 — 동차 w 만 쓴다)
        };
        static_assert( sizeof( RootConstants ) / sizeof( uint32 ) <= shaderslot::kRootConstantDwords, "compute implementation root constants exceed the root block" );

        FramePassContext&                         context  = input._context;
        const RenderGraphPassDesc&                passDesc = input._passDesc;
        const hashed_string&                      target   = input._declaredColor[0]._attachment;
        const ComputePipeline                     pipeline = _psoManager.findPipelineComputePSO( hashed_string( passDesc._name.c_str() ) );
        const TransientAttachmentPool::Attachment output   = activePool().find( target );
        if ( pipeline.isValid() == false || output._uav == kInvalidDescriptorIndex )
        {
            SW_LOG_ERROR_ONCE_ID( "FramePassExecutor.ComputeImplementationNotReady", "Pass '%#' compute implementation has no PSO or no RW output - it does not run", passDesc._name );
            return;
        }
        // 크기는 뷰 사각형이 덮는 텍셀이다(자동 렌더 배율 — 풀은 그대로). 원본 텍셀도 사각형 기준이라 셰이더의 픽셀 → UV · 클램프가 사각형 안에 머문다.
        RootConstants constants{};
        constants._outputIndex = static_cast<uint32>( output._uav );
        constants._width       = _pActiveView->scaleToRect( output, true );
        constants._height      = _pActiveView->scaleToRect( output, false );
        // 입력은 역할로 고른다(로드가 계약을 검사했다). 원본 · 깊이는 SRV 로 읽는다 — 깊이 형식은 RW 로 묶을 수 없다.
        for ( const RenderGraphPassDesc::ResolvedAttachment& resolved : passDesc._listResolvedInput )
        {
            const TransientAttachmentPool::Attachment source = activePool().find( resolved._attachment );
            const RenderPassInputRole                 role   = static_cast<RenderPassInputRole>( resolved._role );
            if ( source._srv == kInvalidDescriptorIndex )
                continue;
            if ( role == RenderPassInputRole::SourceColor )
            {
                constants._sourceIndex       = static_cast<uint32>( source._srv );
                constants._arrSourceTexel[0] = 1.0f / static_cast<float32>( MathUtil::max( _pActiveView->scaleToRect( source, true ), 1u ) );
                constants._arrSourceTexel[1] = 1.0f / static_cast<float32>( MathUtil::max( _pActiveView->scaleToRect( source, false ), 1u ) );
            }
            else if ( role == RenderPassInputRole::SceneDepth )
            {
                constants._depthIndex = static_cast<uint32>( source._srv );
            }
        }
        // 패스 상수(시드가 이미 채운 값)에서 효과의 값을 옮긴다 — 컴퓨트 PSO 에는 PassCB 가 묶이지 않는다.
        uint32       valueSize     = 0;
        const bool   bDepthOfField = passDesc._type == RenderPassKind::DepthOfField;
        const uint8* pParam        = context._passValues.find( bDepthOfField ? passConstantNames()._cameraLens : passConstantNames()._bloomParams, valueSize );
        if ( pParam != nullptr && valueSize >= sizeof( constants._arrParam ) )
            Memory::copy( constants._arrParam, pParam, sizeof( constants._arrParam ) );
        const uint8* pInvViewProj = context._passValues.find( passConstantNames()._invViewProj, valueSize );
        if ( pInvViewProj != nullptr && valueSize == sizeof( float4x4 ) )
        {
            // 셰이더는 mul( 행벡터, M ) 이다 — 동차 w 는 M 의 4 열(_14, _24, _34, _44)과의 내적이다(C++ float4x4 는 행 우선 _ij 그대로 올라간다).
            float4x4 invViewProj{};
            Memory::copy( &invViewProj, pInvViewProj, sizeof( float4x4 ) );
            constants._arrInvViewProjColumn3[0] = invViewProj._14;
            constants._arrInvViewProjColumn3[1] = invViewProj._24;
            constants._arrInvViewProjColumn3[2] = invViewProj._34;
            constants._arrInvViewProjColumn3[3] = invViewProj._44;
        }
        if ( constants._sourceIndex == kInvalidDescriptorIndex )
        {
            SW_LOG_ERROR_ONCE_ID( "FramePassExecutor.ComputeImplementationNoSource", "Pass '%#' compute implementation has no SourceColor view - it does not run", passDesc._name );
            return;
        }
        // 입력은 레벨 콜백이 셰이더 읽기로, 출력은 이 패스의 UAV 선언으로 RW 텍스처 상태로 옮겼다(`bindPassCallbacks`). 뒤 패스의 읽기 · UAV 는 레벨 콜백이 옮기거나 기다린다.
        // 컴퓨트 구현은 네이티브 bindless 에서만 만든다 — 출력도 루트 상수의 RW 번호라 이름으로 거는 칸이 없다(표는 같은 컴퓨트 패스 표다).
        if ( ComputeShaderParameters<ComputePassShader>{}.apply( *context._pCmd, pipeline ) == false )
            return;
        context._pCmd->setComputeRootConstants( sizeof( constants ) / sizeof( uint32 ), &constants );
        context._pCmd->dispatchThreads( constants._width, constants._height, 1 );
        (void)markAttachmentCleared( target );
    }

    void FramePassExecutor::executeComputePass( const PassExecuteInput& input )
    {
        // compute_pass.hlsli 의 SwRootConstants 와 같은 배치다.
        struct RootConstants
        {
            uint32 _outputIndex{ 0 };
            uint32 _arrInputIndex[2]{ kInvalidDescriptorIndex, kInvalidDescriptorIndex };
            uint32 _width{ 0 };
            uint32 _height{ 0 };
        };
        FramePassContext&                         context  = input._context;
        const RenderGraphPassDesc&                passDesc = input._passDesc;
        const hashed_string&                      target   = input._declaredColor[0]._attachment;
        const ComputePipeline                     pipeline = _psoManager.findPipelineComputePSO( hashed_string( passDesc._name.c_str() ) );
        const TransientAttachmentPool::Attachment output   = activePool().find( target );
        if ( pipeline.isValid() == false || output._uav == kInvalidDescriptorIndex )
        {
            SW_LOG_ERROR_ONCE_ID( "FramePassExecutor.ComputePassNotReady", "Compute pass '%#' has no PSO or no RW output - it does not run", passDesc._name );
            return;
        }
        // 네이티브는 RW 텍스처 배열의 번호로, 에뮬은 RW 슬롯 서수로 고른다(Bindless::storeRwTexture2D). 슬롯 0 이 출력, 그 뒤가 입력이다.
        const bool    bNative = _pDevice->supportsNativeBindlessSampling();
        RootConstants constants{};
        constants._outputIndex = bNative ? static_cast<uint32>( output._uav ) : 0u;
        constants._width       = _pActiveView->scaleToRect( output, true ); // 사각형 밖은 디스패치하지 않는다
        constants._height      = _pActiveView->scaleToRect( output, false );
        // 출력은 UAV, 입력은 UAV 읽기로 이 패스가 선언했다 — 레벨 콜백이 RW 텍스처 상태로 옮기고, 앞 컴퓨트의 쓰기 뒤면 텍스처 UAV 배리어로 기다린다(`bindPassCallbacks`).
        // 에뮬은 출력 · 입력이 RW 칸 0 · 1 · 2 다(네이티브는 셰이더가 번호로 읽어 칸이 리플렉션에 없다 — 걸지 않는다).
        constexpr ComputePassShader::Parameter arrInputParameter[] = { ComputePassShader::Input0, ComputePassShader::Input1 };
        static_assert( sizeof( arrInputParameter ) / sizeof( arrInputParameter[0] ) == sizeof( constants._arrInputIndex ) / sizeof( constants._arrInputIndex[0] ), "compute pass inputs and RW slots differ" );
        ComputeShaderParameters<ComputePassShader> parameters;
        parameters.set( ComputePassShader::Output, output._uav );
        for ( uint32 inputIndex = 0; inputIndex < passDesc._listResolvedInput.size(); ++inputIndex )
        {
            const TransientAttachmentPool::Attachment source = activePool().find( passDesc._listResolvedInput[inputIndex]._attachment );
            if ( source._uav == kInvalidDescriptorIndex )
            {
                SW_LOG_ERROR_ONCE_ID( "FramePassExecutor.ComputePassInputNotReady", "Compute pass '%#' input %# has no RW view - it does not run", passDesc._name, inputIndex );
                return;
            }
            constants._arrInputIndex[inputIndex] = bNative ? static_cast<uint32>( source._uav ) : inputIndex + 1u;
            parameters.set( arrInputParameter[inputIndex], source._uav );
        }
        if ( parameters.apply( *context._pCmd, pipeline ) == false )
            return;
        context._pCmd->setComputeRootConstants( sizeof( constants ) / sizeof( uint32 ), &constants );
        context._pCmd->dispatchThreads( constants._width, constants._height, 1 );
        // 뒤 패스가 샘플하면 레벨 콜백이 셰이더 읽기로 옮긴다. 컴퓨트가 칠했으니 이번 프레임 첫 렌더 패스가 지우면 안 된다.
        (void)markAttachmentCleared( target );
    }

    void FramePassExecutor::executeForwardOpaquePass( const PassExecuteInput& input )
    {
        // ForwardOnlyOpaque 는 디퍼드의 G버퍼가 뺀 배치(MATERIAL_FORWARD_ONLY — 툰)만 조명 패스가 쓴 색과 장면 깊이 위에 그린다(거르기는 shouldDrawBatchInPass).
        // 첨부는 앞 패스가 이미 썼으므로 getColorLoadFor 가 Load 를 고른다.
        // 그림자 맵은 선언한 입력 중 ShadowMap · LocalShadowMap 역할이다(첨부의 `_role` 또는 원본 이름). 선언하지 않은 파이프라인(2D)은 그림자를 바인딩하지 않는다
        // — 옛 코드는 이름 ShadowMap 으로 찾았고, 그 첨부가 없는 파이프라인에서는 어차피 빈 바인딩이었다.
        using Internal                                              = FramePassExecutorInternal;
        FramePassContext&                              context      = input._context;
        const hashed_string&                           passDepth    = input._passDepth;
        const RenderGraphPassDesc::ResolvedAttachment* pShadowInput = Internal::findInputByRole( input._passDesc, RenderPassInputRole::ShadowMap );
        if ( pShadowInput != nullptr )
            registerPassTexture( context, attachmentNames()._shadowMap, pShadowInput->_attachment );
        const RenderGraphPassDesc::ResolvedAttachment* pLocalShadowInput = Internal::findInputByRole( input._passDesc, RenderPassInputRole::LocalShadowMap );
        if ( pLocalShadowInput != nullptr )
            registerPassTexture( context, attachmentNames()._localShadowMap, pLocalShadowInput->_attachment );
        // 접촉 그림자 마스크는 그 패스가 이 뷰에서 돌았을 때만 건다(꺼지면 출력이 없다 — 셰이더는 1). 나머지 선언 입력(원본 색 · 깊이)은 이 패스가 그리는 첨부라 걸지 않는다.
        const RenderGraphPassDesc::ResolvedAttachment* pContactInput = Internal::findInputByRole( input._passDesc, RenderPassInputRole::ContactShadow );
        if ( pContactInput != nullptr )
            registerPassTexture( context, attachmentNames()._contactShadow, pContactInput->_attachment );
        // DBuffer 셋도 같다. 데칼 패스가 이 뷰에서 돌았을 때만 건다(데칼이 없으면 출력이 없고 셰이더는 "덮지 않음").
        for ( const RenderPassInputRole dbufferRole : { RenderPassInputRole::DBufferA, RenderPassInputRole::DBufferB, RenderPassInputRole::DBufferC } )
        {
            const RenderGraphPassDesc::ResolvedAttachment* pDBufferInput = Internal::findInputByRole( input._passDesc, dbufferRole );
            if ( pDBufferInput != nullptr )
                registerPassTexture( context, inputRoleName( dbufferRole ), pDBufferInput->_attachment );
        }
        // 선언한 컬러 출력을 모두 연다 — 화면 속도를 선언했으면 [색, 속도] 두 타깃이다(검증 3-3, 셰이더 SV_TARGET0 · 1).
        const vector<RenderGraphPassDesc::ResolvedAttachment>& declaredColor = input._declaredColor;
        const uint32                                           colorCount    = static_cast<uint32>( declaredColor.size() ) < kMaxColorAttachments ? static_cast<uint32>( declaredColor.size() ) : kMaxColorAttachments;
        hashed_string                                          arrNames[kMaxColorAttachments]{};
        float4                                                 arrClears[kMaxColorAttachments]{};
        RHIRenderPassLoadOp                                    arrLoads[kMaxColorAttachments]{};
        for ( uint32 colorIndex = 0; colorIndex < colorCount; ++colorIndex )
        {
            arrNames[colorIndex]  = declaredColor[colorIndex]._attachment;
            arrClears[colorIndex] = getTargetClearColor( arrNames[colorIndex] );
            arrLoads[colorIndex]  = getColorLoadFor( arrNames[colorIndex] );
        }
        if ( beginColorPassMrt( context, arrNames, arrClears, arrLoads, colorCount, passDepth, getColorLoadFor( passDepth ) ) == false )
            return;
        const bool bDepthAlreadyWritten = input._passType == RenderPassKind::ForwardOpaque && _bHasExecutedDepthPrepass.load() != 0 &&
                                          _psoManager.getEnginePSO( RenderPassKind::ForwardOpaqueNoDepthWrite ) != 0;
        const RHIPipelineStateHandle psoForward = bDepthAlreadyWritten ? _psoManager.getEnginePSO( RenderPassKind::ForwardOpaqueNoDepthWrite ) : _psoManager.getEnginePSO( input._passType );
        drawSceneMeshes( context, psoForward, input._bTransparentBatch );
        context._pCmd->endRenderPass();
    }

    void FramePassExecutor::executeGBufferPass( const PassExecuteInput& input )
    {
        // 알베도 · 노멀 · 머티리얼 · 속도 · 사용자 칸 · 장면 색은 역할로 고른다(첨부의 `_role` 또는 원본 이름). 역할이 없으면 선언 순서(GBufferLayout 의 MRT 순서)다.
        // 한 MRT 패스로 여섯 다 쓴다(네 백엔드 모두 MRT 8 장을 보장한다). 컬러 출력 수가 표의 값이 아닌 G버퍼는 검증(3-2)이 거부한다. 장면 색에는 자체 발광을 미리 적는다.
        using Internal                       = FramePassExecutorInternal;
        FramePassContext&    context         = input._context;
        const hashed_string& passDepth       = input._passDepth;
        const hashed_string* pAlbedoTarget   = Internal::pickColorOutput( input._declaredColor, RenderPassInputRole::GBufferAlbedo, 0, nullptr );
        const hashed_string* pNormalTarget   = Internal::pickColorOutput( input._declaredColor, RenderPassInputRole::GBufferNormal, 1, pAlbedoTarget );
        const hashed_string* pMaterialTarget = Internal::pickColorOutput( input._declaredColor, RenderPassInputRole::GBufferMaterial, 2, pNormalTarget );
        // 화면 속도는 명세의 넷째 슬롯이다(GBufferLayout::kVelocitySlot — 셰이더 SV_TARGET3). 사용자 칸 · 장면 색(자체 발광)이 뒤를 잇는다(SV_TARGET4 · 5).
        const hashed_string* pVelocityTarget = Internal::pickColorOutput( input._declaredColor, RenderPassInputRole::Velocity, GBufferLayout::kVelocitySlot, pMaterialTarget );
        const hashed_string* pCustomTarget   = Internal::pickColorOutput( input._declaredColor, RenderPassInputRole::GBufferCustomData, GBufferLayout::kVelocitySlot + 1, pVelocityTarget );
        const hashed_string* pSceneTarget    = Internal::pickColorOutput( input._declaredColor, RenderPassInputRole::SourceColor, GBufferLayout::kVelocitySlot + 2, pCustomTarget );
        const bool           bHasTargets     = pAlbedoTarget != nullptr && pNormalTarget != nullptr && findTransient( *pNormalTarget ) != 0 &&
                                 pMaterialTarget != nullptr && findTransient( *pMaterialTarget ) != 0 && pVelocityTarget != nullptr && findTransient( *pVelocityTarget ) != 0 &&
                                 pCustomTarget != nullptr && findTransient( *pCustomTarget ) != 0 && pSceneTarget != nullptr && findTransient( *pSceneTarget ) != 0;
        if ( bHasTargets == false || _psoManager.getEnginePSO( RenderPassKind::GBuffer ) == 0 )
            return;
        const hashed_string&      albedoTarget = *pAlbedoTarget;
        const hashed_string       arrNames[]   = { albedoTarget, *pNormalTarget, *pMaterialTarget, *pVelocityTarget, *pCustomTarget, *pSceneTarget };
        const float4              arrClears[]  = { getTargetClearColor( albedoTarget ), getTargetClearColor( *pNormalTarget ), getTargetClearColor( *pMaterialTarget ),
                                                   getTargetClearColor( *pVelocityTarget ), getTargetClearColor( *pCustomTarget ), getTargetClearColor( *pSceneTarget ) };
        const RHIRenderPassLoadOp arrLoads[]   = { getColorLoadFor( albedoTarget ), getColorLoadFor( *pNormalTarget ), getColorLoadFor( *pMaterialTarget ),
                                                   getColorLoadFor( *pVelocityTarget ), getColorLoadFor( *pCustomTarget ), getColorLoadFor( *pSceneTarget ) };
        static_assert( sizeof( arrNames ) / sizeof( arrNames[0] ) == GBufferLayout::kAttachmentCount, "G버퍼 패스의 MRT 는 GBufferLayout 의 첨부 수다" );
        // 선언 입력은 DBuffer 셋뿐이다(계약) — 이 뷰에 데칼이 없으면 출력이 없어 걸지 않고, 셰이더는 "덮지 않음" 을 읽는다.
        // 패스를 열기 전에 건다 — 입력을 셰이더 읽기로 옮기는 전이는 패스 밖의 일이라 열린 패스를 닫는다(데칼이 켜지면 G버퍼 드로우가 모두 빠져 하늘만 남았다).
        registerDeclaredInputs( context, input._passDesc );
        if ( beginColorPassMrt( context, arrNames, arrClears, arrLoads, GBufferLayout::kAttachmentCount, passDepth, getColorLoadFor( passDepth ) ) == false )
            return;
        drawSceneMeshes( context, _psoManager.getEnginePSO( RenderPassKind::GBuffer ), input._bTransparentBatch );
        context._pCmd->endRenderPass();
    }

    void FramePassExecutor::executeTransparentPass( const PassExecuteInput& input )
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
            const RHITextureHandle src = findTransient( sourceInput._attachment );
            const RHITextureHandle dst = findTransient( colorTarget );
            if ( src != 0 && dst != 0 )
            {
                context._pCmd->blitTexture( src, dst );
                (void)markAttachmentCleared( colorTarget );
            }
            break;
        }

        // 장면 색 사본(SceneColorCopy 패스가 반투명 앞에 떠 둔 것)을 선언했으면 역할로 건다 — 물의 굴절이 읽는다. 선언하지 않은 파이프라인은 걸지 않고 셰이더는 kInvalidIndex 로 안다.
        using Internal                   = FramePassExecutorInternal;
        const auto* pSceneColorCopyInput = Internal::findInputByRole( input._passDesc, RenderPassInputRole::SceneColorCopy );
        if ( pSceneColorCopyInput != nullptr )
            registerPassTexture( context, attachmentNames()._sceneColorCopy, pSceneColorCopyInput->_attachment );
        // 장면 깊이 사본(SceneDepthCopy 패스 출력)도 같다 — 물의 화면 공간 두께가 읽는다. 반투명은 장면 깊이를 깊이 붙임으로 쥐고 있어 그 깊이를 샘플할 수 없다.
        const auto* pSceneDepthCopyInput = Internal::findInputByRole( input._passDesc, RenderPassInputRole::SceneDepthCopy );
        if ( pSceneDepthCopyInput != nullptr )
            registerPassTexture( context, attachmentNames()._sceneDepthCopy, pSceneDepthCopyInput->_attachment );

        if ( beginColorPass( context, colorTarget, input._passDepth, _state._clearColor, RHIRenderPassLoadOp::Load, RHIRenderPassLoadOp::Load ) == false )
            return;
        // 메시 투명 배치와 입자(방출기 하나 = 후보 하나)를 그 뷰의 한 순서로 섞어 그린다(GPUSceneBuilder::populateParticleBatches · computeViewTransparentOrders).
        SW_ASSERT( input._bTransparentBatch );
        drawTransparentQueue( context, _psoManager.getEnginePSO( RenderPassKind::Transparent ) );
        context._pCmd->endRenderPass();
    }

    void FramePassExecutor::executeSceneColorCopyPass( const PassExecuteInput& input )
    {
        // 원본 색(SourceColor 역할 입력)을 선언한 컬러 출력으로 그대로 복사한다 — 셰이더도 PSO 도 없다. 반투명 패스는 장면 색에 섞으며 쓰므로 그 색을 읽을 수 없다(물의 굴절).
        // 둘은 같은 크기 · 형식이어야 한다(파이프라인이 같은 형식으로 선언한다). 계약(`RenderPassKindInfo`)이 SourceColor 를 필수로 검사했다.
        // 읽는 패스가 이번 뷰에 모두 비활성이면 뷰 패스 고르기가 출력을 빼고 이 패스를 비활성으로 적는다(`kCullWhenUnconsumed`) — 실행 입구가 거르지만 출력이 없는 경우도 지킨다(풀 해상도 HDR 복사 한 번이 매 프레임 0.27 ms 였다).
        const hashed_string& copyTarget = input._declaredColor[0]._attachment;
        if ( activePool().isForwarded( copyTarget ) )
            return;
        using Internal                                              = FramePassExecutorInternal;
        const RenderGraphPassDesc::ResolvedAttachment* pSourceInput = Internal::findInputByRole( input._passDesc, RenderPassInputRole::SourceColor );
        SW_ASSERT( pSourceInput != nullptr );
        if ( pSourceInput == nullptr )
            return;
        const RHITextureHandle src = findTransient( pSourceInput->_attachment );
        const RHITextureHandle dst = findTransient( copyTarget );
        if ( src == 0 || dst == 0 )
            return;
        input._context._pCmd->blitTexture( src, dst );
        (void)markAttachmentCleared( copyTarget );
    }

    void FramePassExecutor::executeMeshOutlinePass( const PassExecuteInput& input )
    {
        // 인버티드 헐(inverted hull) 외곽선이다. 불투명 패스가 그린 색과 깊이 위에, 외곽선을 켠 머티리얼의 배치만 앞면을 컬링해 그린다(shouldDrawBatchInPass).
        // 와이어프레임과 표면 값 보기(Normals, Depth, Overdraw)에서는 빠진다(껍질이 선과 값 위를 덮는다). 타깃은 선언한 컬러 출력이다(불투명 패스와 같은 규칙).
        const RenderViewModeInfo& viewModeInfo = RenderViewModeUtil::getInfo( _pActiveView->_settings._viewMode );
        if ( ( viewModeInfo._bWireframe && viewModeInfo._bOverlay == false ) || viewModeInfo._bShowsBuffer ) // 겹침(ShadedWireframe)은 Lit 그림이라 그린다
            return;
        FramePassContext&    context     = input._context;
        const hashed_string& passDepth   = input._passDepth;
        const hashed_string& colorTarget = input._declaredColor[0]._attachment;
        const float4         sceneClear  = getTargetClearColor( colorTarget );
        if ( beginColorPass( context, colorTarget, passDepth, sceneClear, getColorLoadFor( colorTarget ), getColorLoadFor( passDepth ) ) == false )
            return;
        drawSceneMeshes( context, _psoManager.getEnginePSO( RenderPassKind::MeshOutline ), input._bTransparentBatch );
        context._pCmd->endRenderPass();
    }

    void FramePassExecutor::executeHighlightPass( const PassExecuteInput& input )
    {
        // 인스턴스 강조 외곽선이다(highlight_outline.hlsl). 불투명 배치를 모두 그리되 이 패스의 강조 방식인 인스턴스만 정점이 남는다 —
        // 배치를 CPU 에서 거르지 않으므로 강조를 켜고 꺼도 배치가 다시 나뉘지 않는다. 와이어프레임과 표면 값 보기에서는 빠진다(메시 외곽선과 같다).
        const RenderViewModeInfo& viewModeInfo = RenderViewModeUtil::getInfo( _pActiveView->_settings._viewMode );
        if ( ( viewModeInfo._bWireframe && viewModeInfo._bOverlay == false ) || viewModeInfo._bShowsBuffer )
            return;
        // 이 패스의 방식으로 강조된 인스턴스가 없으면 그리지 않는다 — 그려도 정점 셰이더가 모두 버린다(씬의 모든 정점을 헛돈다).
        const InstanceHighlightStyle style = input._passType == RenderPassKind::HighlightSense ? InstanceHighlightStyle::Sense : InstanceHighlightStyle::Outline;
        if ( _state._gpuScene.hasHighlightStyle( style ) == false )
            return;
        FramePassContext&            context     = input._context;
        const hashed_string&         passDepth   = input._passDepth;
        const hashed_string&         colorTarget = input._declaredColor[0]._attachment;
        const float4                 sceneClear  = getTargetClearColor( colorTarget );
        const RHIPipelineStateHandle pso         = _psoManager.getEnginePSO( input._passType );
        // 렌더 패스의 깊이 붙임은 PSO 의 깊이 형식을 따른다 — 깊이 테스트를 끈 감각 패스의 PSO 는 깊이가 없고, 둘이 다르면 Vulkan 은 그리지 않는다.
        const bool          bUsesDepth = _psoManager.findDepthStencilFormat( pso ) != RHIFormat::Unknown;
        const hashed_string depthName  = bUsesDepth ? passDepth : hashed_string{};
        if ( beginColorPass( context, colorTarget, depthName, sceneClear, getColorLoadFor( colorTarget ), getColorLoadFor( passDepth ) ) == false )
            return;
        drawSceneMeshes( context, pso, false );
        context._pCmd->endRenderPass();
    }

    void FramePassExecutor::executeSkyPass( const PassExecuteInput& input )
    {
        // 하늘이다. 깊이 1 의 풀스크린 삼각형을 깊이 테스트(LessEqual, 쓰기 없음)로 그려 아무것도 그리지 않은 곳만 칠한다(sky.hlsl).
        // 씬에 하늘이 없으면 그리지 않는다 — 배경은 컬러 첨부의 클리어 색이다. 와이어프레임과 표면 값 보기에서도 빠진다(값 위를 덮는다).
        // 뎁스나 선언한 컬러 출력이 없으면 그리지 않는다(장면을 통째로 덮거나 백버퍼에 그린다 — 그런 파이프라인은 검증이 거부한다).
        // 앞의 불투명 패스가 색과 깊이를 이미 썼으므로 둘 다 불러온다(지우지 않는다).
        const RenderViewModeInfo&    viewModeInfo = RenderViewModeUtil::getInfo( _pActiveView->_settings._viewMode );
        const RHIPipelineStateHandle skyPSO       = _psoManager.getEnginePSO( RenderPassKind::Sky );
        const bool                   bShowsSky    = ( viewModeInfo._bWireframe == false || viewModeInfo._bOverlay ) && viewModeInfo._bShowsBuffer == false;
        const bool                   bHasTargets  = input._passDepth.empty() == false;
        const bool                   bDrawsSky    = _state._bHasFrameSky == SW_TRUE && bHasTargets && skyPSO != 0 && bShowsSky;
        if ( bDrawsSky == false )
            return;
        FramePassContext&    context     = input._context;
        const hashed_string& colorTarget = input._declaredColor[0]._attachment;
        // HDRI 하늘은 텍스처를 원본(SourceColor) 역할로 건다 — 쿠키 블릿과 같은 길이라 네 백엔드에 칸을 더하지 않는다. 다 읽기 전이면 걸지 않고 셰이더가 검게 칠한다.
        const Texture2D* pHdri = _state._skyHdriLoad.isReady() ? _state._skyHdriLoad.getOperation()->getTexture() : nullptr;
        if ( pHdri != nullptr && pHdri->isRHIValid() )
            context._resourceRegistry.registerTexture( attachmentNames()._sourceColor, pHdri->getHandle(), pHdri->getSRV() );
        // 볼륨 구름은 그 패스가 이 뷰에서 돌았을 때만 건다(없으면 셰이더는 구름을 얹지 않는다). 나머지 선언 입력(장면 색 · 깊이)은 이 패스가 그리는 첨부라 걸지 않는다.
        const RenderGraphPassDesc::ResolvedAttachment* pCloudInput = FramePassExecutorInternal::findInputByRole( input._passDesc, RenderPassInputRole::SkyClouds );
        if ( pCloudInput != nullptr )
            registerPassTexture( context, attachmentNames()._skyClouds, pCloudInput->_attachment );
        if ( beginColorPass( context, colorTarget, input._passDepth, _state._clearColor, RHIRenderPassLoadOp::Load, RHIRenderPassLoadOp::Load ) == false )
            return;
        drawFullscreen( context, skyPSO );
        context._pCmd->endRenderPass();
    }

    void FramePassExecutor::executeVolumetricCloudsPass( const PassExecuteInput& input )
    {
        FramePassContext&      context = input._context;
        const AttachmentNames& names   = attachmentNames();
        const hashed_string&   target  = input._declaredColor[0]._attachment;
        const FrameViewTarget& view    = *_pActiveView;
        registerDeclaredInputs( context, input._passDesc );
        // 기록은 이 패스가 복사 대상으로 선언한 뷰 자원이다 — 읽기 앞에 셰이더 읽기로 옮긴다. 버리는 프레임(처음 · 컷 · 지난 프레임에 구름이 없음)은 읽지 않고 새로 채우기만 한다.
        const bool bHistory  = context._pGraphPass->isRegistered( ViewGraphResource::skyCloudHistory() );
        const bool bReadable = bHistory && view._skyCloudHistory.isReset() == false;
        if ( bReadable )
        {
            context._pGraphPass->transition( ViewGraphResource::skyCloudHistory(), RenderGraphAccess::ShaderRead );
            context._resourceRegistry.registerTexture( names._gbufferAlbedo, view._skyCloudHistory.getPrevious()._texture, view._skyCloudHistory.getPrevious()._srv );
        }
        float4 volume = _state._frameSky._cloudVolume;
        volume._z     = bReadable ? volume._z : 0.0f;
        // 텍셀 안 자리는 TAA 지터와 같은 Halton 한 바퀴, 걸음 시작은 황금비 수열이다 — 프레임마다 다른 자리를 기록이 모은다.
        constexpr float32                         kGoldenRatioFraction = 0.618034f;
        const TransientAttachmentPool::Attachment output               = findTransientAttachment( target );
        const float2                              offset               = TemporalJitterSequence::getPixelOffset( view._skyCloudSample % TemporalJitterSequence::kSampleCount );
        const float32                             stepPhase            = static_cast<float32>( view._skyCloudSample % TemporalJitterSequence::kMaxSampleCount ) * kGoldenRatioFraction;
        const float32                             divisor              = output._width > 0 ? static_cast<float32>( view._transientPool.getWidth() ) / static_cast<float32>( output._width ) : 1.0f;
        context._passValues.setFloat4( passConstantNames()._skyCloudVolume, volume );
        context._passValues.setFloat4( passConstantNames()._skyCloudSample, float4{ offset._x, offset._y, stepPhase - MathUtil::floor( stepPhase ), divisor } );
        executeFullscreenInto( context, _psoManager.getEnginePSO( RenderPassKind::VolumetricClouds ), target, getTargetClearColor( target ) );
        if ( output._texture == 0 || bHistory == false )
            return;
        context._pCmd->blitTexture( output._texture, view._skyCloudHistory.getCurrent()._texture );
        context._pGraphPass->transition( ViewGraphResource::skyCloudHistory(), RenderGraphAccess::ShaderRead ); // 복사는 기록을 렌더 타깃 상태로 남긴다(TAA 와 같다)
    }

    bool FramePassExecutor::isPassDisabledInView( uint32 passIndex ) const
    {
        // 파이프라인 패스 목록 밖(모듈 확장 패스)은 고르기가 보지 않는다 — 고르기 결과 밖이면 돈다.
        const vector<uint8>& listPassDisabled = _pActiveView->_listPassDisabled;
        return passIndex < listPassDisabled.size() && listPassDisabled[passIndex] == SW_TRUE;
    }

    bool FramePassExecutor::isPassKindEnabledInView( RenderPassKind type ) const
    {
        const vector<RenderGraphPassDesc>& listPass = _pipelineResource.getGraphPass();
        for ( uint32 passIndex = 0; passIndex < static_cast<uint32>( listPass.size() ); ++passIndex )
        {
            if ( listPass[passIndex]._type == type && isPassDisabledInView( passIndex ) == false )
                return true;
        }
        return false;
    }

    void FramePassExecutor::executeDistortionPass( const PassExecuteInput& input )
    {
        // 화면 왜곡 누적 — 왜곡 머티리얼(MATERIAL_DISTORTION)의 반투명 배치가 화면 UV 오프셋을 선언한 출력에 더한다(가산 블렌드, 깊이는 테스트만).
        // 왜곡 배치가 없으면 기록하지 않는다 — 출력은 지워지지도 않지만 읽는 적용 패스도 같은 판정으로 돌지 않는다(쓰지 않을 때 비용이 없다).
        const RHIPipelineStateHandle pso = _psoManager.getEnginePSO( RenderPassKind::Distortion );
        if ( pso == 0 )
            return;
        FramePassContext&    context = input._context;
        const hashed_string& target  = input._declaredColor[0]._attachment;
        if ( beginColorPass( context, target, input._passDepth, getTargetClearColor( target ), getColorLoadFor( target ), RHIRenderPassLoadOp::Load ) == false )
            return;
        drawSceneMeshes( context, pso, input._bTransparentBatch );
        context._pCmd->endRenderPass();
    }

    void FramePassExecutor::executeDistortionApplyPass( const PassExecuteInput& input )
    {
        // 화면 왜곡 적용 — 장면 색 사본을 왜곡 오프셋만큼 비껴 읽어 장면 색에 쓴다(distortion_apply.hlsl). 왜곡이 없으면 출력(장면 색)이 이미 맞아 아무것도 하지 않는다
        // — 일반 풀스크린 패스의 복사 길을 타면 왜곡 없는 프레임에도 전 화면을 한 번 읽고 쓴다.
        const RHIPipelineStateHandle pso = _psoManager.getEnginePSO( RenderPassKind::DistortionApply );
        if ( pso == 0 )
            return;
        FramePassContext&    context = input._context;
        const hashed_string& target  = input._declaredColor[0]._attachment;
        registerDeclaredInputs( context, input._passDesc );
        if ( beginColorPass( context, target, hashed_string{}, _state._clearColor, RHIRenderPassLoadOp::Load, RHIRenderPassLoadOp::Load ) == false )
            return;
        drawFullscreen( context, pso );
        context._pCmd->endRenderPass();
    }

    void FramePassExecutor::executeFogPass( const PassExecuteInput& input )
    {
        // 안개다. 장면 깊이로 광학 두께를 구해 선언한 컬러 출력(앞 패스가 칠한 색) 위에 프리멀티플라이 블렌드한다(fog.hlsl).
        // 씬에 안개가 없거나 표면 값 보기(Normals, Depth, Overdraw)와 와이어프레임에서는 그리지 않는다 — 값 위를 덮는다. 후처리를 끈 뷰(CCTV)도 안개는 그린다(장면의 일이다).
        // 출력이 곧 입력의 위(블렌드)라 "그리지 않음" 은 아무것도 하지 않는 것이다 — 일반 풀스크린 패스의 복사 · 지우기 길을 타면 장면이 지워진다.
        const RenderViewModeInfo&    viewModeInfo = RenderViewModeUtil::getInfo( _pActiveView->_settings._viewMode );
        const RHIPipelineStateHandle pso          = _psoManager.getEnginePSO( RenderPassKind::Fog );
        const bool                   bShowsScene  = ( viewModeInfo._bWireframe == false || viewModeInfo._bOverlay ) && viewModeInfo._bShowsBuffer == false;
        const bool                   bDraws       = _fog.hasFrameFog() && pso != 0 && bShowsScene;
        if ( bDraws == false )
            return;

        FramePassContext&    context = input._context;
        const hashed_string& target  = input._declaredColor[0]._attachment;
        registerDeclaredInputs( context, input._passDesc );

        // 볼륨은 컴퓨트가 돈 프레임의 주 시점에서만 읽는다(추가 뷰는 볼륨을 계산하지 않는다). 상수는 이 패스의 복사본에만 쓴다.
        const bool bVolume      = _fog.isFogVolumeActive() && _pActiveView == &_state.getMainView() && _fog.getFogVolumeBuffer()._srv != kInvalidDescriptorIndex;
        float4     volumeParams = _fog.getFrameFog()._volumeParams;
        volumeParams._x         = bVolume ? 1.0f : 0.0f;
        context._passValues.setFloat4( passConstantNames()._fogVolumeParams, volumeParams );
        context._passValues.setFloat4( passConstantNames()._fogDebugParams, FrameFog::makeDebugParams( bVolume, _state._settings._diagnostics ) );

        if ( beginColorPass( context, target, hashed_string{}, getTargetClearColor( target ), getColorLoadFor( target ), RHIRenderPassLoadOp::Load ) == false )
            return;
        // drawFullscreen 과 같은 순서다. 볼륨 버퍼는 PSO 와 공통 바인딩(bindForDraw) 뒤에 건다 — setPipelineState 가 슬롯 상태를 비운다.
        setIdentityWorld( context );
        context.setPipeline( pso );
        if ( bindForDraw( context, pso, kInvalidDescriptorIndex ) )
        {
            if ( bVolume )
                context._pCmd->bindStructuredBuffer( _fog.getFogVolumeBuffer()._srv, shaderslot::kPassBuffer );
            context._pCmd->draw( 3, 0 );
        }
        context._pCmd->endRenderPass();
    }

    void FramePassExecutor::executeScreenSpaceReflectionPass( const PassExecuteInput& input )
    {
        // 화면 공간 반사다. 맞은 자리의 장면 색 사본을 선언한 컬러 출력(장면 색) 위에 프리멀티플라이 블렌드한다(screen_space_reflection.hlsl).
        // 세기 0 · 후처리를 끈 뷰(CCTV) · 표면 값 보기(usesPostProcess 가 거른다)에서는 그리지 않는다 — 출력이 입력의 위(블렌드)라 "그리지 않음" 은 아무것도 하지 않는 것이다(안개와 같다).
        const RHIPipelineStateHandle pso = _psoManager.getEnginePSO( RenderPassKind::ScreenSpaceReflection );
        if ( pso == 0 )
            return;

        FramePassContext&    context = input._context;
        const hashed_string& target  = input._declaredColor[0]._attachment;
        // hi-z trace 가 읽는 HZB 를 이번 장면 깊이(불투명을 다 그린 것)로 짓는다 — 렌더 패스를 열기 전이다. 못 지으면 그리지 않는다(선형 탐색으로 대신하지 않는다).
        FrameViewTarget& view = *_pActiveView;
        if ( _frameGraphExecutor.recordSceneDepthHZB( *context._pGraphPass, view ) == false )
        {
            SW_LOG_ERROR_ONCE_ID( "FramePassExecutor.SSRWithoutHZB", "Screen space reflection is on but this view has no HZB (depth history, HZBBuild PSO or buffer missing) - reflections are not drawn" );
            return;
        }
        context._passValues.setFloat4( passConstantNames()._ssrHZBParams, float4{ static_cast<float32>( view._hzbLayout._depthWidth ), static_cast<float32>( view._hzbLayout._depthHeight ),
                                                                                  static_cast<float32>( view._hzbLayout._mipCount ), 0.0f } );
        registerDeclaredInputs( context, input._passDesc );
        if ( beginColorPass( context, target, hashed_string{}, getTargetClearColor( target ), getColorLoadFor( target ), RHIRenderPassLoadOp::Load ) == false )
            return;
        // drawFullscreen 과 같은 순서다. HZB 버퍼는 PSO 와 공통 바인딩(bindForDraw) 뒤에 건다 — setPipelineState 가 슬롯 상태를 비운다(안개의 볼륨 버퍼와 같다).
        setIdentityWorld( context );
        context.setPipeline( pso );
        if ( bindForDraw( context, pso, kInvalidDescriptorIndex ) )
        {
            context._pCmd->bindStructuredBuffer( view._hzb._srv, shaderslot::kPassBuffer );
            context._pCmd->draw( 3, 0 );
        }
        context._pCmd->endRenderPass();
    }

    void FramePassExecutor::executePostProcessMaterialPass( const PassExecuteInput& input )
    {
        // 후처리 머티리얼이다(언리얼 Post Process Material). 셋업(`FramePostProcessMaterials::prepareView`)이 뷰마다 PSO · 원소 버퍼를 정해 두었다 — 여기서는 그리기만 한다.
        const PostProcessBlendableLocation location  = input._passDesc._blendableLocation;
        const bool                         bReplaces = location == PostProcessBlendableLocation::ReplacingTonemap;
        const hashed_string&               target    = input._declaredColor[0]._attachment;
        // 톤맵 앞 · 뒤 자리에 그릴 것이 없는 뷰는 셋업이 출력 이름을 원본에 이었다(`applyViewPassSelection`) — 기록하지 않는다.
        if ( bReplaces == false && activePool().isForwarded( target ) )
            return;
        const vector<FrameViewTarget::PostProcessMaterialPass>& listDraw = _pActiveView->_listPostProcessMaterialPass;
        const bool                                              bHasDraw = VectorUtil::containsIf( listDraw, [location]( const FrameViewTarget::PostProcessMaterialPass& draw )
                                                     { return draw._location == location && draw._pso != 0; } );
        if ( bReplaces && ( bHasDraw == false || _pActiveView->_settings.usesPostProcess() == false ) )
            return; // 톤맵 출력이 이미 맞다
        FramePassContext&                              context      = input._context;
        const RenderGraphPassDesc::ResolvedAttachment* pSourceInput = FramePassExecutorInternal::findInputByRole( input._passDesc, RenderPassInputRole::SourceColor );
        SW_ASSERT( pSourceInput != nullptr ); // 입력 계약의 필수 역할이다(로드가 검사했다)
        if ( pSourceInput == nullptr )
            return;
        if ( bReplaces == false )
        {
            // 출력을 원본으로 채운다 — 머티리얼은 그 위에 무게만큼 섞는다(무게 0.5 면 반만 바뀐다). 둘은 형식 · 크기가 같다(검증 3-6).
            const RHITextureHandle src = findTransient( pSourceInput->_attachment );
            const RHITextureHandle dst = findTransient( target );
            if ( src == 0 || dst == 0 )
                return;
            context._pCmd->blitTexture( src, dst );
            (void)markAttachmentCleared( target );
        }
        registerDeclaredInputs( context, input._passDesc );
        if ( beginColorPass( context, target, hashed_string{}, getTargetClearColor( target ), RHIRenderPassLoadOp::Load, RHIRenderPassLoadOp::Load ) == false )
            return;
        setIdentityWorld( context );
        const RHITextureHandle targetTexture = findTransient( target );
        for ( const FrameViewTarget::PostProcessMaterialPass& draw : listDraw )
        {
            if ( draw._location != location || draw._pso == 0 || draw._buffer == 0 || draw._srv == kInvalidDescriptorIndex )
                continue;
            // 둘째부터는 앞 결과를 읽는다 — 출력을 뷰의 사본으로 옮기고 원본 역할을 그 사본으로 바꾼다(그리는 동안 같은 텍스처를 읽을 수 없다).
            const bool bChained = draw._pChainSource != nullptr && draw._pChainSource->isValid() && targetTexture != 0;
            if ( bChained )
            {
                context._pCmd->endRenderPass();
                context._pCmd->blitTexture( targetTexture, draw._pChainSource->_texture );
                // 사본은 이 패스가 복사 대상으로 선언했다(`ViewGraphResource`, 이어 그리는 머티리얼이 있는 뷰만 등록). 다음 머티리얼이 읽도록 셰이더 읽기로 옮긴다.
                context._pGraphPass->transition( location == PostProcessBlendableLocation::BeforeTonemap ? ViewGraphResource::postProcessChainBeforeTonemap()
                                                                                                         : ViewGraphResource::postProcessChainAfterTonemap(),
                                                 RenderGraphAccess::ShaderRead );
                context._resourceRegistry.registerTexture( attachmentNames()._sourceColor, draw._pChainSource->_texture, draw._pChainSource->_srv );
                if ( beginColorPass( context, target, hashed_string{}, getTargetClearColor( target ), RHIRenderPassLoadOp::Load, RHIRenderPassLoadOp::Load ) == false )
                    return;
            }
            // 무게는 그리기마다의 패스 상수다(post_process_material_main.hlsli 의 g_PostProcessMaterial.x). 머티리얼은 원소 하나짜리 버퍼의 0 번이다.
            context._passValues.setFloat4( passConstantNames()._postProcessMaterial, float4{ draw._weight, 0.0f, 0.0f, 0.0f } );
            context._resourceRegistry.registerBuffer( passConstantNames()._swMaterials, draw._buffer, draw._srv );
            context._drawMaterialCount = 1u;
            context.setPipeline( draw._pso );
            if ( bindForDraw( context, draw._pso, kInvalidDescriptorIndex, draw._pTextureSRV ) )
                context._pCmd->draw( 3, 0 );
        }
        context._pCmd->endRenderPass();
    }

    void FramePassExecutor::executeDecalPass( const PassExecuteInput& input )
    {
        // DBuffer 데칼이다(언리얼 DBuffer). 깊이 프리패스 뒤 · 베이스 패스 앞에 DBufferA · B · C 를 지우고(클리어 (0, 0, 0, 1) = 덮지 않음) 데칼마다 상자 뒷면을 그려 프리멀티플라이로 쌓는다.
        // 데칼이 없는 뷰 · 표면 값 보기는 뷰 패스 고르기가 이 패스를 빼고 출력도 없다(베이스 패스가 걸지 않는다). 여기 온 뒤에는 PSO 가 없어도 지우기는 한다 —
        // 출력이 있는데 지우지 않으면 베이스 패스가 지난 프레임 값을 섞는다.
        const RHIPipelineStateHandle                    pso       = _psoManager.getEnginePSO( RenderPassKind::Decal );
        const vector<FrameRenderState::FrameDecalDraw>& listDecal = _state._listFrameDecalDraw;
        FramePassContext&                               context   = input._context;
        registerDeclaredInputs( context, input._passDesc );
        // 선언한 컬러 출력 셋이 MRT 다(검증 3-9: DBuffer 역할 셋 — 이 차례). 깊이 붙임은 없다(장면 깊이를 입력으로 읽는다).
        hashed_string       arrColor[3];
        float4              arrClear[3];
        RHIRenderPassLoadOp arrLoad[3];
        const uint32        colorCount = static_cast<uint32>( MathUtil::min( input._declaredColor.size(), static_cast<size_t>( 3 ) ) );
        for ( uint32 colorIndex = 0; colorIndex < colorCount; ++colorIndex )
        {
            arrColor[colorIndex] = input._declaredColor[colorIndex]._attachment;
            arrClear[colorIndex] = getTargetClearColor( arrColor[colorIndex] );
            arrLoad[colorIndex]  = getColorLoadFor( arrColor[colorIndex] );
        }
        if ( beginColorPassMrt( context, arrColor, arrClear, arrLoad, colorCount, hashed_string{}, RHIRenderPassLoadOp::Load ) == false )
            return;
        // 데칼 원소는 셋업이 구조 버퍼 하나에 올렸다(`FrameRenderer::uploadFrameDecals`) — 묶음마다 인스턴스 그리기 하나다. 인스턴스는 차례대로 래스터되므로
        // 프리멀티플라이 쌓기의 순서가 데칼마다 그리던 때와 같다. 에뮬 백엔드는 텍스처를 머티리얼 칸에 걸어야 해서 텍스처가 같은 이웃끼리만 묶는다(네이티브 bindless 는 모두 하나).
        const uint32 decalCount   = static_cast<uint32>( listDecal.size() );
        const bool   bDecalBuffer = _state._decalBuffer._buffer != 0 && _state._decalBuffer._srv != kInvalidDescriptorIndex;
        if ( pso != 0 && bDecalBuffer && decalCount > 0 )
        {
            const bool bNativeBindless = _pDevice->supportsNativeBindlessSampling();
            context._resourceRegistry.registerBuffer( passConstantNames()._swDecals, _state._decalBuffer );
            context._passValues.setUint( passConstantNames()._decalCount, decalCount );
            uint32 first = 0;
            while ( first < decalCount )
            {
                uint32 end = first + 1u;
                while ( end < decalCount && ( bNativeBindless || Memory::compare( listDecal[first]._arrTextureSRV, listDecal[end]._arrTextureSRV, sizeof( listDecal[first]._arrTextureSRV ) ) == 0 ) )
                {
                    ++end;
                }
                context._passValues.setUint( passConstantNames()._decalFirst, first );
                context.setPipeline( pso );
                // 상자 하나 = 삼각형 12 개(정점 버퍼 없이 SV_VertexID — decal.hlsl VSMain), 인스턴스 하나 = 데칼 하나. 앞면은 XML 의 컬(Front)이 버린다(거울 상자는 VSMain 이 감김을 되돌린다).
                if ( bindForDraw( context, pso, kInvalidDescriptorIndex, listDecal[first]._arrTextureSRV ) )
                {
                    context._pCmd->drawInstanced( kDecalBoxVertexCount, end - first, 0, 0 );
                    SW_PROFILE_COUNT( "RT.Draw.decalDraws", 1 ); // 데칼 인스턴스 그리기 묶음 수(네이티브 bindless 는 패스에 하나)
                }
                first = end;
            }
        }
        context._pCmd->endRenderPass();
        setIdentityWorld( context );
    }

    void FramePassExecutor::executeLensFlarePass( const PassExecuteInput& input )
    {
        // 스프라이트는 주 시점의 투영으로 펼쳤다 — 추가 뷰 · 표면 값 보기에서는 그리지 않는다(값 위를 덮는다). 출력 위에 더하므로 "그리지 않음" 은 아무것도 하지 않는 것이다.
        const RenderViewModeInfo&    viewModeInfo = RenderViewModeUtil::getInfo( _pActiveView->_settings._viewMode );
        const RHIPipelineStateHandle pso          = _psoManager.getEnginePSO( RenderPassKind::LensFlare );
        const LensFlareSpriteBuffer& sprites      = _state._lensFlare;
        const bool                   bDraws       = pso != 0 && sprites.getSpriteCount() > 0 && _pActiveView == &_state.getMainView() && viewModeInfo._bShowsBuffer == false && viewModeInfo._bWireframe == false;
        if ( bDraws == false )
            return;

        FramePassContext&    context = input._context;
        const hashed_string& target  = input._declaredColor[0]._attachment;
        registerDeclaredInputs( context, input._passDesc );
        if ( beginColorPass( context, target, hashed_string{}, getTargetClearColor( target ), getColorLoadFor( target ), RHIRenderPassLoadOp::Load ) == false )
            return;
        // drawFullscreen 과 같은 순서다. 스프라이트 버퍼는 PSO 와 공통 바인딩(bindForDraw) 뒤에 건다 — setPipelineState 가 슬롯 상태를 비운다(안개 패스와 같다).
        setIdentityWorld( context );
        // 구간마다 그리기 하나 — Image 구간은 텍스처를 머티리얼 칸 0 으로 빌려 건다(데칼과 같다). 아직 읽는 중인 텍스처의 구간은 이번 프레임에 뺀다(흰 사각형으로 대신하지 않는다).
        for ( const LensFlareDrawRange& range : sprites.getDrawRanges() )
        {
            RHITextureSRVIndex arrTextureSRV[shaderslot::kMaterialTextureCount];
            for ( RHITextureSRVIndex& srv : arrTextureSRV )
            {
                srv = kInvalidDescriptorIndex;
            }
            uint32 textureIndex = kInvalidDescriptorIndex;
            if ( range._pTexturePath != nullptr )
            {
                const Texture2D* pTexture = _state._pathTextures.requestReady( *range._pTexturePath, "lens flare sprite" );
                if ( pTexture == nullptr )
                    continue;
                arrTextureSRV[0] = pTexture->getSRV();
                textureIndex     = _pDevice->getMaterialTextureIndex( pTexture->getSRV(), 0 );
            }
            context._passValues.setUint( passConstantNames()._lensFlareTexture, textureIndex );
            context.setPipeline( pso );
            if ( bindForDraw( context, pso, kInvalidDescriptorIndex, arrTextureSRV ) )
            {
                context._pCmd->bindStructuredBuffer( sprites.getBuffer()._srv, shaderslot::kPassBuffer );
                // 스프라이트 하나 = 모서리 여섯(정점 버퍼 없이 SV_VertexID 로 만든다 — lens_flare.hlsl). 시작 정점이 SV_VertexID 에 더해져 구간의 스프라이트를 읽는다.
                context._pCmd->draw( range._spriteCount * 6u, range._firstSprite * 6u );
            }
        }
        context._pCmd->endRenderPass();
    }

    void FramePassExecutor::executeTAAPass( const PassExecuteInput& input )
    {
        // 타깃은 선언한 첫 컬러 출력이다(풀스크린 패스와 같은 규칙이며 히스토리도 같은 규칙으로 만든다. updateOutputHistory).
        FramePassContext&      context   = input._context;
        const AttachmentNames& names     = attachmentNames();
        const hashed_string&   taaTarget = input._declaredColor[0]._attachment;
        const hashed_string*   pSrcName{ nullptr };
        // 후처리를 끈 뷰는 셋업이 출력 이름을 원본에 이었다(`applyViewPassSelection`, kBypassesToSource). 기록하지 않고 히스토리도 건드리지 않는다
        // — 원본을 TAAColor 로 복사하거나 셰이더로 옮기던 길은 지웠다. 다시 켠 첫 프레임은 기록을 버린다(`FrameViewHistoryIdleRule::Discard`).
        if ( activePool().isForwarded( taaTarget ) )
            return;
        registerDeclaredInputs( context, input._passDesc );
        for ( const RenderGraphPassDesc::ResolvedAttachment& sourceInput : input._passDesc._listResolvedInput )
        {
            if ( static_cast<RenderPassInputRole>( sourceInput._role ) == RenderPassInputRole::SourceColor )
                pSrcName = &sourceInput._attachment;
        }
        // 히스토리 생성과 bindless 등록은 updateOutputHistory() 가 셋업 단계에서 끝냈다. 이 콜백은
        // 병렬 기록에서 태스크 스레드가 실행하므로 여기서 레지스트리를 건드리면 안 된다. 히스토리는 뷰마다 하나다.
        const FrameViewTarget& activeView = *_pActiveView;
        // 기록을 버리는 프레임(처음 그리는 프레임 · 컷 · 후처리를 다시 켠 첫 프레임 — `FrameViewHistory::isReset`)에서는 지난 화면의 히스토리를 버린다. 이번 프레임은 히스토리 슬롯에 이번 원본을
        // 바인딩해 섞어도 이번 이미지만 남게 한다. 원본을 히스토리 텍스처에 복사하지 않는 것은 포맷이 다를 수 있어서다(원본 R8G8B8A8, 히스토리 R16G16B16A16이며
        // D3D 의 복사는 같은 포맷만 받는다). 패스 끝의 복사(TAA 출력에서 히스토리로)가 히스토리를 새로 채운다.
        const bool bCut = activeView._taaHistory.isReset() && pSrcName != nullptr;
        // 기록은 이 패스가 복사 대상으로 선언한 뷰 자원이다(`ViewGraphResource::taaHistory`) — 읽기 앞에 셰이더 읽기로 옮긴다(지난 프레임이 그 상태로 남겼고 새로 만든 텍스처만 실제로 옮겨진다).
        const bool bHistory = context._pGraphPass->isRegistered( ViewGraphResource::taaHistory() );
        if ( bCut )
        {
            registerPassTexture( context, names._gbufferAlbedo, *pSrcName );
        }
        else if ( bHistory )
        {
            context._pGraphPass->transition( ViewGraphResource::taaHistory(), RenderGraphAccess::ShaderRead );
            context._resourceRegistry.registerTexture( names._gbufferAlbedo, activeView._taaHistory.getPrevious()._texture, activeView._taaHistory.getPrevious()._srv );
        }
        if ( beginColorPass( context, taaTarget, hashed_string{}, _state._clearColor, getColorLoadFor( taaTarget ), RHIRenderPassLoadOp::Load ) )
        {
            // TAA PSO 가 없으면(셋업이 알렸다) 원본을 복사해 TAA 없이 넘기지 않는다 — drawFullscreen 이 그리지 않는다.
            drawFullscreen( context, _psoManager.getEnginePSO( RenderPassKind::TAA ) );
            context._pCmd->endRenderPass();
        }

        const RHITextureHandle taaOut = findTransient( taaTarget );
        if ( taaOut == 0 || bHistory == false )
            return;
        context._pCmd->blitTexture( taaOut, activeView._taaHistory.getCurrent()._texture );
        context._pGraphPass->transition( ViewGraphResource::taaHistory(), RenderGraphAccess::ShaderRead ); // 복사는 기록을 렌더 타깃 상태로 남긴다 — 다음 프레임이 그대로 샘플하지 않게
    }

    void FramePassExecutor::executePresentPass( const PassExecuteInput& input )
    {
        FramePassContext&          context  = input._context;
        const RenderGraphPassDesc& passDesc = input._passDesc;
        const hashed_string        srcName  = resolvePresentSource();
        const RHITextureHandle     src      = srcName.empty() ? 0 : findTransient( srcName );
        // 출력은 뷰가 정한다(resolvePresentTarget. Canvas 와 같은 판단). 스크린샷 실행이면 백버퍼 대신 캡처 텍스처에 그리고,
        // Swapchain 을 쓰는 마지막 패스(보통 Canvas) 끝에서 백버퍼로 복사한다. 여기서 복사하면 뒤의 UI 가 캡처에 없다.
        const FrameViewTarget& activeView     = *_pActiveView;
        const PresentTarget    target         = resolvePresentTarget( true );
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
        const float4&             outputClear = upscaleZoom > 0 ? FrameRendererUtil::kBlackClear : _state._clearColor;
        // PSO 는 대상의 실제 포맷으로 고른다. 렌더 타깃 포맷은 PSO 의 일부라 대상마다 PSO 가 다르다. 셋업에 없으면 findOutputPSO 가 멈춘다(복사로 대신하지 않는다).
        const RHIPipelineStateHandle psoBlit = _psoManager.findOutputPSO( RenderPassKind::Present, target._format );
        if ( src != 0 )
        {
            registerPassTexture( context, attachmentNames()._sourceColor, srcName );
            // 선언한 입력을 모두 바인딩한다. Present 가 후처리 체인을 겸하면 깊이(외곽선)와 AO 가 필요하고,
            // 그냥 블릿이면 선언이 컬러 하나뿐이라 위 등록을 덮어쓸 뿐이다.
            registerDeclaredInputs( context, passDesc );
            // 개발 보기 `gpu.show <첨부>` — 주 시점이 화면에 낼 원본을 그 첨부로 바꾼다(언리얼 Buffer Visualization). 선언한 입력 등록 뒤라 그것을 덮는다.
            registerVisualizedAttachment( context );
            // 후처리 체인이 톤맵까지 하면 자동 노출을 이 그리기 앞에서 측정한다(Tonemap 패스와 같은 자리).
            _autoExposure.recordAutoExposure( context, *_pActiveView, passDesc );
            // FSR 1 로 키우는 뷰는 렌더 패스를 열기 전에 EASU 를 중간 RT 에 그리고, 아래에서 블릿 대신 RCAS 로 그린다(prepareUpscale 이 셋업에서 정했다).
            const bool bUpscale = _upscale.recordUpscaleEasu( context, *_pActiveView, srcName );
            context.beginRenderPass( RHIRenderPassBeginInfo::color( dstTarget, outputLoad, outputClear ).setExtent( outputWidth, outputHeight ) );
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
                    // Present 원본은 출력 공간이다(시간 업스케일이 없으면 렌더 공간과 같은 크기).
                    const TransientAttachmentPool& pool = activePool();
                    viewport._x                         = static_cast<float32>( FrameRendererUtil::computeUpscaleOffset(
                        static_cast<uint32>( viewport._x ), static_cast<uint32>( viewport._width ), pool.getOutputWidth(), upscaleZoom ) );
                    viewport._y                         = static_cast<float32>( FrameRendererUtil::computeUpscaleOffset(
                        static_cast<uint32>( viewport._y ), static_cast<uint32>( viewport._height ), pool.getOutputHeight(), upscaleZoom ) );
                    viewport._width                     = static_cast<float32>( pool.getOutputWidth() * upscaleZoom );
                    viewport._height                    = static_cast<float32>( pool.getOutputHeight() * upscaleZoom );
                }
                context._pCmd->setViewport( viewport );
            }
            if ( bUpscale )
                _upscale.drawUpscaleRcas( context, *_pActiveView, target._format );
            else
            {
                drawFullscreen( context, psoBlit );
            }
            context._pCmd->endRenderPass();
            if ( bCaptureToBack )
                context._pCmd->blitTexture( dstTarget, getScreenTarget() );
        }
        else
        {
            // 원본이 없으면 PSO 없이(백엔드 기본 파이프라인으로) 대신 그리지 않는다. 출력 대상의 상태는 옮기지 않는다 — 뒤의 복사 · 겹치기 · 렌더 스레드 UI 패스가 스스로 맞춘다
            // (렌더 패스 · 블릿이 대상 상태를 옮긴다). 패스 기록 면에는 텍스처 전이가 없다.
            SW_LOG_ERROR_ONCE_ID( "FramePassExecutor.PresentSourceColorNo", "Present source color has no transient (its allocation failed) - the output is not drawn" );
        }
        if ( target._bCaptureFromOutput == SW_TRUE && isLastSwapchainWriter( passDesc ) )
            _capture.copyPresentFromOutput( *context._pCmd, target._texture, target._width, target._height, target._format );
        drawDevOverlay( context, passDesc, target ); // Canvas 패스가 없는 파이프라인은 Present 가 마지막 쓰기다
    }

    void FramePassExecutor::executeCanvasPass( const PassExecuteInput& input )
    {
        // 화면 2D 를 주 출력(Present 가 그린 것)에 기존 내용을 유지한 채(Load) 그린다. 주 출력에 마지막으로 그리는 뷰만 그린다
        // (`_pMainOutputCanvasView` — 화면 사각형 뷰가 있으면 그 마지막 것, 없으면 주 시점). 그래야 PiP · 분할 화면이 UI 를 덮지 않는다.
        // 렌더 텍스처 · 호스트 타깃 뷰에는 그리지 않는다. 그릴 것이 없으면 렌더 패스를 열지 않는다. 대상 전체가 뷰포트다(화면 사각형 설정과 무관).
        FramePassContext&   context = input._context;
        const PresentTarget target  = resolvePresentTarget( false );
        const bool          bDraw   = _pActiveView == _pMainOutputCanvasView && _state._canvasFrame.getMainOutput().isEmpty() == false && target._width > 0 && target._height > 0;
        if ( bDraw )
        {
            const RHIPipelineStateHandle psoCanvas = _psoManager.findOutputPSO( RenderPassKind::Canvas, target._format ); // 셋업에 없으면 멈춘다(다른 포맷의 PSO 로 대신하지 않는다)
            context.beginRenderPass( RHIRenderPassBeginInfo::color( target._texture, RHIRenderPassLoadOp::Load ).setExtent( target._width, target._height ) );
            (void)_state._canvasRenderer.drawList( *context._pCmd, _state._canvasFrame.getMainOutput(), _state._canvasRenderer.getMainQuadBase(), psoCanvas, target._width, target._height,
                                                   _state._canvasFrame._colorVisionMode );
            context._pCmd->endRenderPass();
        }
        const bool bLastWriter = isLastSwapchainWriter( input._passDesc );
        if ( target._bCaptureToBack == SW_TRUE && bLastWriter )
            context._pCmd->blitTexture( target._texture, getScreenTarget() );
        if ( target._bCaptureFromOutput == SW_TRUE && bLastWriter )
            _capture.copyPresentFromOutput( *context._pCmd, target._texture, target._width, target._height, target._format );
        drawDevOverlay( context, input._passDesc, target );
    }

    void FramePassExecutor::drawDevOverlay( FramePassContext& context, const RenderGraphPassDesc& passDesc, const PresentTarget& target )
    {
        // 순서가 계약이다: 사진 캡처(캡처 텍스처에 그리기 · 출력에서 복사)가 끝난 뒤, 화면에 나가는 대상에만 그린다 — 사진 · 골든에 개발 겹치기가 들지 않는다.
        const CanvasDrawList&  list        = _state._canvasFrame.getDevOverlay();
        const bool             bOnScreen   = target._bCapture == SW_FALSE || target._bCaptureToBack == SW_TRUE; // 크기를 덮어쓴 캡처는 화면에 나가지 않는다
        const RHITextureHandle screen      = getScreenTarget();                                                 // `kRHIBackBufferTexture` = 스왑체인, 회전한 화면이면 대기 텍스처, HDR 출력이면 UI RT
        const RHITextureHandle sceneTarget = _displayEncode.getSceneTarget();
        // 호스트 타깃(에디터 게임 뷰)에는 그리지 않는다. HDR 출력의 Present 는 장면 출력 RT 에 그렸어도 화면에 나가는 대상이다(겹치기는 UI RT 에 그린다).
        const bool bBackBuffer  = target._bCaptureToBack == SW_TRUE || target._texture == screen || ( sceneTarget != 0 && target._texture == sceneTarget );
        const bool bDrawOverlay = list.isEmpty() == false && isLastSwapchainWriter( passDesc ) && _pActiveView == _pMainOutputCanvasView && target._bOwnOutput == SW_FALSE &&
                                  bOnScreen && bBackBuffer && target._width > 0 && target._height > 0;
        if ( bDrawOverlay == false )
            return;
        // 화면 대상(스왑체인 · 회전 대기 텍스처 · HDR UI RT)은 모두 UI 대상 포맷이다 — 셋업이 그 포맷의 캔버스 PSO 를 만든다.
        const RHIFormat              format = _pDevice->getUITargetFormat();
        const RHIPipelineStateHandle pso    = _psoManager.findOutputPSO( RenderPassKind::Canvas, format ); // 셋업에 없으면 멈춘다
        // 화면 — 캡처를 옮긴 뒤이거나 처음부터 화면에 그린 프레임이다
        context.beginRenderPass( RHIRenderPassBeginInfo::color( screen, RHIRenderPassLoadOp::Load ).setExtent( target._width, target._height ) );
        (void)_state._canvasRenderer.drawList( *context._pCmd, list, _state._canvasRenderer.getDevOverlayQuadBase(), pso, target._width, target._height );
        context._pCmd->endRenderPass();
    }

    bool ExtensionPassContext::drawFullscreen( vector_reference<const ExtensionConstant> constants )
    {
        return _pExecutor->drawModuleFullscreen( *static_cast<FramePassContext*>( _pPassContext ), *static_cast<const ExtensionFramePass*>( _pPass ), constants );
    }

    bool ExtensionPassContext::drawProxyElements( vector_reference<const ExtensionConstant> constants )
    {
        return _pExecutor->drawExtensionElements( *static_cast<FramePassContext*>( _pPassContext ), *static_cast<const ExtensionFramePass*>( _pPass ), constants );
    }

    void FramePassExecutor::executeExtensionPass( FramePassContext& context, const ExtensionFramePass& pass )
    {
        if ( context._pCmd == nullptr )
            return;
        const GPUPassScope passScope{ *context._pCmd, pass._graphName.view() };
        context._resourceRegistry.reset();
        context._passType = RenderPassKind::Invalid;
        registerLightBuffer( context );
        registerSurfaceDeformLayer( context );
        registerRainOcclusion( context );
        ExtensionViewInfo view{};
        view._bMainView = _pActiveView == &_state.getMainView();
        view._width     = activePool().getWidth();
        view._height    = activePool().getHeight();
        ExtensionPassContext moduleContext( *this, &context, &pass, view );
        pass._execute( moduleContext );
    }

    bool FramePassExecutor::drawModuleFullscreen( FramePassContext& context, const ExtensionFramePass& pass, vector_reference<const ExtensionConstant> listConstant )
    {
        if ( pass._pso == 0 || context._pCmd == nullptr )
        {
            StringBuilder<constant::buffer::kMax256> failureKey;
            failureKey.appendFormat( "nodraw:%#", pass._graphName.view() );
            FrameExtensionHost::logFailureOnce( failureKey.view(), "pass has no program - nothing is drawn", pass._graphName.view() );
            return false;
        }
        if ( listConstant.size() > kExtensionMaxConstantCount )
        {
            StringBuilder<constant::buffer::kMax256> failureKey;
            failureKey.appendFormat( "constants:%#", pass._graphName.view() );
            FrameExtensionHost::logFailureOnce( failureKey.view(), "pass has too many constants - nothing is drawn", pass._graphName.view() );
            return false;
        }
        for ( const ExtensionPassRead& read : pass._params._listRead )
        {
            registerPassTexture( context, inputRoleName( read._role ), hashed_string( read._attachment.c_str() ) );
        }
        ExtensionConstantScope<FramePassContext> constantScope( context, _frameArena );
        constantScope.apply( listConstant.data(), static_cast<uint32>( listConstant.size() ) );
        const float4              targetClear = getTargetClearColor( pass._colorTarget );
        const RHIRenderPassLoadOp depthLoad   = pass._depthTarget.empty() ? RHIRenderPassLoadOp::Load : getColorLoadFor( pass._depthTarget );
        if ( beginColorPass( context, pass._colorTarget, pass._depthTarget, targetClear, getColorLoadFor( pass._colorTarget ), depthLoad ) == false )
            return false;
        drawFullscreen( context, pass._pso );
        context._pCmd->endRenderPass();
        return true;
    }

    bool FramePassExecutor::drawExtensionElements( FramePassContext& context, const ExtensionFramePass& pass, vector_reference<const ExtensionConstant> listConstant )
    {
        if ( pass._pso == 0 || context._pCmd == nullptr )
        {
            StringBuilder<constant::buffer::kMax256> failureKey;
            failureKey.appendFormat( "nodraw:%#", pass._graphName.view() );
            FrameExtensionHost::logFailureOnce( failureKey.view(), "pass has no program - nothing is drawn", pass._graphName.view() );
            return false;
        }
        if ( listConstant.size() > kExtensionMaxConstantCount )
        {
            StringBuilder<constant::buffer::kMax256> failureKey;
            failureKey.appendFormat( "constants:%#", pass._graphName.view() );
            FrameExtensionHost::logFailureOnce( failureKey.view(), "pass has too many constants - nothing is drawn", pass._graphName.view() );
            return false;
        }
        if ( _extensionHost.getDrawElements().empty() )
            return true; // 그릴 것이 없으면 렌더 패스를 열지 않는다(열면 타깃의 첫 접근이 클리어로 소비된다)
        for ( const ExtensionPassRead& read : pass._params._listRead )
        {
            registerPassTexture( context, inputRoleName( read._role ), hashed_string( read._attachment.c_str() ) );
        }
        ExtensionConstantScope<FramePassContext> passScope( context, _frameArena );
        passScope.apply( listConstant.data(), static_cast<uint32>( listConstant.size() ) );
        const float4              targetClear = getTargetClearColor( pass._colorTarget );
        const RHIRenderPassLoadOp depthLoad   = pass._depthTarget.empty() ? RHIRenderPassLoadOp::Load : getColorLoadFor( pass._depthTarget );
        if ( beginColorPass( context, pass._colorTarget, pass._depthTarget, targetClear, getColorLoadFor( pass._colorTarget ), depthLoad ) == false )
            return false;
        bool bAllDrawn{ true };
        for ( const ExtensionDrawElement& element : _extensionHost.getDrawElements() )
        {
            // 메시 표는 셋업(`FrameExtensionHost::collectFrame`)이 채웠다 — 기록 중에는 읽기만 한다. 없으면 셋업이 이유를 남겼다.
            const Mesh* pMesh = _extensionHost.findMesh( element._mesh );
            if ( pMesh == nullptr || pMesh->getRenderData().isRHIValid() == false )
            {
                bAllDrawn = false;
                continue;
            }
            const Mesh&  mesh       = *pMesh;
            const uint32 indexCount = mesh.getIndexCount(); // LOD 0 전체
            if ( indexCount == 0 )
                continue;
            // 요소마다 상수가 다르다 — 새 패스 상수 슬롯을 잡는다(앞 요소의 슬롯을 덮으면 GPU 가 나중에 읽는 백엔드에서 모두 마지막 값을 본다). 링이 바닥나면 이 요소부터 그리지 않는다.
            if ( acquirePassCb( context ) == false )
            {
                bAllDrawn = false;
                break;
            }
            ExtensionConstantScope<FramePassContext> elementScope( context, _frameArena );
            elementScope.apply( element._arrConstant, element._constantCount );
            context._world = element._world;
            context.setPipeline( pass._pso ); // 정점 슬롯을 비운다 — 메시 스트림은 그 뒤에 건다
            RHIMeshVertexStreams::bind( *context._pCmd, mesh.getRenderData().getVertexBuffer(), mesh.getAttributeStreamOffset(), mesh.getVertexFormat() );
            context._pCmd->setIndexBuffer( mesh.getRenderData().getIndexBuffer(), mesh.getRenderData().getIndexStride(), 0 );
            if ( bindForDraw( context, pass._pso, kInvalidDescriptorIndex ) )
            {
                context._pCmd->drawIndexedInstanced( indexCount, element._instanceCount );
                SW_PROFILE_COUNT( "RT.Draw.extensionDraws", 1 ); // CPU 직접 그리기(요소마다 PSO · 스트림 · 상수 슬롯)
            }
        }
        setIdentityWorld( context );
        context._pCmd->endRenderPass();
        return bAllDrawn;
    }
} // namespace sw
