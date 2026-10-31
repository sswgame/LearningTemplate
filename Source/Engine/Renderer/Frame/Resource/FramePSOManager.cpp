#include "pch.h"

#include "Engine/Renderer/Frame/Resource/FramePSOManager.h"

#include "Core/Container/StringUtil.h"
#include "Core/Container/VectorUtil.h"
#include "Core/GlobalVariable/GlobalVariableManager.h"
#include "Core/Hash/HashUtil.h"
#include "Core/String/FourCcUtil.h"
#include "Core/Task/TaskManager.h"

#include "Engine/Asset/AssetManager.h"
#include "Engine/Config/EngineDefaultAssets.h"
#include "Engine/Foundation/EngineParallel.h"
#include "Engine/Foundation/EngineServices.h"
#include "Engine/Graphics/Material/Material.h"
#include "Engine/Graphics/Material/MaterialCache.h"
#include "Engine/Graphics/Material/MaterialInstance.h"
#include "Engine/Graphics/Material/MaterialUtil.h"
#include "Engine/Graphics/RHI/Backend/RHIShaderRequest.h"
#include "Engine/Graphics/RHI/IRHIDevice.h"
#include "Engine/Graphics/RHI/IRHIResourceFactory.h"
#include "Engine/Graphics/RHI/RHI.h"
#include "Engine/Graphics/Shader/Binding/ShaderBindingLayout.h"
#include "Engine/Graphics/Shader/Compile/ShaderCache.h"
#include "Engine/Graphics/Shader/Compile/ShaderCompiler.h"
#include "Engine/Profiling/FrameProfiler.h"
#include "Engine/Renderer/Extension/RenderExtension.h"
#include "Engine/Renderer/Frame/FrameRendererUtil.h"
#include "Engine/Renderer/Pipeline/RenderPassTypeInfo.h"
#include "Engine/Renderer/Pipeline/RenderPipelineAsset.h"
#include "Engine/Renderer/Scene/GPUMaterialRegistry.h"
#include "Engine/Renderer/Scene/GPUScene.h"

namespace sw
{
    SW_LOG_CALLER( "FramePSOManager" );

    /**
     * @brief `-gv_psoAsync=0` 이면 머티리얼 변형 PSO 를 빌더 스레드가 아니라 렌더 스레드가 셋업 구간에서 모두 만듭니다(옛 동작 - A/B 측정 · 진단용).
     * @details 기본은 빌더 스레드입니다(`MaterialPSOBuilder`). 스레드를 쓸 수 없는 백엔드(GL)는 이 값과 상관없이 렌더 스레드가 프레임당 시간 예산 안에서 만듭니다.
     */
    SW_GLOBAL_VARIABLE( bool, gv_psoAsync, true, "머티리얼 변형 PSO 를 빌더 스레드가 만든다 (false=렌더 스레드가 동기로 만듦, A/B 측정용)" );

    namespace
    {
        /**
         * @brief 뷰 모드를 PSO 디스크립터에 적용합니다. 바꾼 것이 있으면 true 입니다.
         * @details Lit 는 아무것도 하지 않는다. 그것이 패스가 이미 만들어 둔 상태다.
         */
        [[nodiscard]] bool applyViewModeToDesc( RHIPipelineStateDesc& desc, RenderViewMode viewMode )
        {
            bool                      bChanged{ false };
            const RenderViewModeInfo& info = getRenderViewModeInfo( viewMode );
            if ( info._bWireframe )
            {
                desc._fillMode = RHIFillMode::Wireframe;
                // 와이어프레임은 뒷면도 보여야 형태를 읽을 수 있다. 컬링을 남기면 뒤쪽 선이 사라져
                // 상자가 열린 것처럼 보인다. 에디터의 와이어프레임은 관례적으로 양면이다.
                desc._cullMode = RHICullMode::None;
                bChanged       = true;
            }
            if ( info._bOverlay )
            {
                // 겹침(ShadedWireframe)은 Lit 으로 그린 같은 삼각형 위에 선을 그린다. 선의 래스터 깊이는 면과 조금씩 달라 LessEqual 만으로는
                // z-fighting 한다 — 카메라 쪽으로 미는 바이어스를 걸고, 선이 면의 깊이를 바꾸지 않게 깊이 쓰기를 끈다.
                static constexpr int32   kOverlayDepthBias            = -4;
                static constexpr float32 kOverlaySlopeScaledDepthBias = -1.0f;
                desc._depthBias                                       = kOverlayDepthBias;
                desc._slopeScaledDepthBias                            = kOverlaySlopeScaledDepthBias;
                desc._bEnableDepthWrite                               = 0;
                bChanged                                              = true;
            }
            if ( info._bAdditiveNoDepth )
            {
                // 겹쳐 그린 수를 세려면 가려진 것도 그려야 한다. 깊이를 쓰지 않고 단색을 더한다(유니티 Overdraw 와 같다).
                // 깊이 테스트는 남긴다. 패스가 깊이를 클리어(1)하고 아무도 쓰지 않으니 모든 면이 통과한다. 테스트를 끄면 백엔드가 깊이 첨부 없는
                // PSO 로 만들어(Vulkan 렌더 패스 호환) 깊이를 바인딩한 패스에서 검증 오류가 난다. 깊이 프리패스가 있는 파이프라인에서는 보이는 면만 센다.
                // RHI 에 더하기 블렌드가 따로 없다. 프리멀티플라이(색 One / InvSrcAlpha)에 셰이더가 알파 0을 내면 그것이 더하기다.
                desc._bEnableBlend        = 1;
                desc._bPremultipliedAlpha = SW_TRUE;
                desc._bEnableDepthWrite   = 0;
                desc._cullMode            = RHICullMode::None;
                bChanged                  = true;
            }

            // 셰이딩을 바꾸는 모드(Unlit, Normals, Depth, Overdraw)는 조명 항을 셰이더에서 **컴파일 아웃**한다. 런타임 분기가 아니라 퍼뮤테이션이다.
            // define 은 쿠커와 같은 원본(findViewModeDefine)에서 얻는다. 쿠커가 쿠킹하지 않은 define 은 Shipping 에서 PSO 를 만들 수 없다.
            const utf8* pViewModeDefine = FrameRendererUtil::findViewModeDefine( viewMode );
            if ( pViewModeDefine == nullptr )
                return bChanged;
            for ( const string& existing : desc._listShaderDefine )
            {
                if ( existing == pViewModeDefine )
                    return bChanged;
            }
            desc._listShaderDefine.push_back( pViewModeDefine );
            return true;
        }

        /**
         * @brief 컬 모드의 Back 과 Front 를 맞바꿉니다. 바꾼 것이 있으면 true 입니다(None 은 그대로라 false).
         * @details 거울 변환(월드 행렬식 < 0)은 삼각형 감김을 뒤집는다. 언리얼이 `bReverseCulling` 으로 프리미티브의 컬 모드를 뒤집는 것에 해당한다.
         *          셰이더의 `SV_IsFrontFace` 를 뒤집는 것으로는 안 된다. 컬링은 래스터라이저가 픽셀 셰이더보다 먼저 한다.
         */
        bool reverseCullMode( RHIPipelineStateDesc& desc )
        {
            if ( desc._cullMode == RHICullMode::Back )
            {
                desc._cullMode = RHICullMode::Front;
                return true;
            }
            if ( desc._cullMode == RHICullMode::Front )
            {
                desc._cullMode = RHICullMode::Back;
                return true;
            }
            return false;
        }

        /** @brief 패스의 깊이 · 블렌드 상태를 정합니다. 테이블이 구조적 사실이고 XML 은 그 안의 조정입니다. */
        void applyPassDepthAndBlend( RHIPipelineStateDesc& desc, const RenderPassTypeInfo& info, const RenderGraphPassDesc* pPassDesc )
        {
            const bool bDepthTest         = info.hasFlag( RenderPassTraitFlag::kDepthTest );
            const bool bDefaultDepthWrite = info.hasFlag( RenderPassTraitFlag::kDepthWrite );
            const bool bDefaultBlend      = info.hasFlag( RenderPassTraitFlag::kBlend );
            // 테이블의 깊이 테스트는 "이 패스가 지오메트리인가 풀스크린인가" 라는 구조적 사실이고 XML 은 그 안에서의 조정이다.
            // 그래서 덮어쓰기가 아니라 AND 다. XML 로 끌 수는 있어도 켤 수는 없다(RenderGraphPassDesc 의 기본값이 true 라
            // 덮어쓰면 풀스크린 패스에 깊이 테스트가 켜지고, DSV 없이 그리는 드로우마다 검증 오류가 난다).
            const bool bPassDepthTest  = ( pPassDesc == nullptr ) || ( pPassDesc->_bEnableDepthTest != 0 );
            const bool bPassDepthWrite = ( pPassDesc == nullptr ) || ( pPassDesc->_bEnableDepthWrite != 0 );
            desc._bEnableDepthTest     = ( bDepthTest && bPassDepthTest ) ? 1 : 0;
            desc._bEnableDepthWrite    = ( bDefaultDepthWrite && bPassDepthWrite ) ? 1 : 0;
            desc._bEnableBlend         = pPassDesc != nullptr ? ( pPassDesc->_bEnableBlend != 0 ? 1 : 0 ) : ( bDefaultBlend ? 1 : 0 );
            // 프리멀티플라이는 셰이더의 출력 규약이라 테이블이 정한다(XML 이 바꿀 수 없다. 곧은 알파로 섞으면 알파가 두 번 곱해진다).
            const bool bPremultiplied = desc._bEnableBlend != 0 && info.hasFlag( RenderPassTraitFlag::kPremultipliedAlpha );
            desc._bPremultipliedAlpha = bPremultiplied ? SW_TRUE : SW_FALSE;

            // 뎁스를 쓰지 않는 패스(풀스크린)는 렌더 패스에 DSV 를 붙이지 않는데, desc 의 기본값이 D24 라서
            // PSO 는 "뎁스 있음" 으로 만들어졌다. DX12 는 null DSV 를 PSO 뎁스 포맷이 UNKNOWN 일 때만
            // 허용하므로 그런 패스의 드로우마다 검증 오류가 났다. 뎁스 테스트가 꺼져 있으면 뎁스 쓰기도
            // 의미가 없으니 같이 끈다.
            desc._depthStencilFormat = ( desc._bEnableDepthTest != 0 ) ? constant::kDepthStencilFormat : RHIFormat::Unknown;
            if ( desc._bEnableDepthTest == 0 )
                desc._bEnableDepthWrite = 0;
        }

        /**
         * @brief 패스의 컬 모드를 정합니다. 뎁스와 같은 구조다. **패스가 무엇을 그리는가**가 먼저고 XML 은 그 안의 조정이다.
         * @details 풀스크린 패스는 SV_VertexID 로 삼각형 하나를 만들어 화면을 덮는다. 그 삼각형의 와인딩은
         *          셰이더가 정한 것이고 "앞/뒤" 라는 의미가 없으므로, 컬링을 걸면 화면이 통째로 비거나 그대로
         *          나오거나 둘 중 하나다. 고를 값이 아니다.
         *
         *          주의: 이 기본값은 XML 에 패스를 적어 둔 경우에도 풀스크린 패스에 적용되어야 한다. XML 의 `Back` 을
         *          그대로 따르면 풀스크린 패스(Shading, SSAO, Bloom, Outline, TAA, Tonemap, Present)가 오류도
         *          경고도 없이 아무것도 그리지 않아 화면이 배경색뿐이다.
         *          인버티드 헐(inverted hull) 외곽선은 앞면을 컬링한다(테이블의 kCullFront). XML 의 "Back" 은 그 기본값을 바꾸지 않는다.
         */
        void applyPassCullMode( RHIPipelineStateDesc& desc, const RenderPassTypeInfo& info, const RenderGraphPassDesc* pPassDesc, RenderPassType passType )
        {
            const bool        bFullscreenPass = ( FrameRendererUtil::drawsSceneMeshes( passType ) == false );
            const RHICullMode meshCullMode    = info.hasFlag( RenderPassTraitFlag::kCullFront ) ? RHICullMode::Front : RHICullMode::Back;
            desc._cullMode                    = bFullscreenPass ? RHICullMode::None : meshCullMode;
            if ( pPassDesc == nullptr || bFullscreenPass )
                return;
            const string& cull = pPassDesc->_cullMode;
            if ( cull == "None" || cull == "none" )
                desc._cullMode = RHICullMode::None;
            else if ( cull == "Front" || cull == "front" )
                desc._cullMode = RHICullMode::Front;
        }

        /** @brief 셋업의 셰이더 미리 데우기 잡입니다(`FramePSOManager::prewarmPassShaders`). 워커는 요청 배열을 읽기만 하고, 캐시는 자기 잠금으로 같은 요청을 한 번만 컴파일한다. */
        struct PassShaderWarmJob
        {
            const RHIGraphicsShaderRequest* _pRequest{ nullptr };
            ShaderCache*                    _pShaders{ nullptr };

            void run( uint32 start, uint32 end )
            {
                for ( uint32 index = start; index < end; ++index )
                {
                    // 결과는 버린다 — 실패는 뒤의 PSO 생성이 같은 요청으로 다시 받아 경로와 함께 알린다.
                    const RHIGraphicsShaderRequest& request = _pRequest[index];
                    (void)_pShaders->getOrCompile( request._vertex );
                    if ( request._bHasPixelShader != SW_FALSE )
                        (void)_pShaders->getOrCompile( request._pixel );
                }
            }
        };

        /** @brief PSO 를 동기로 만듭니다(셰이더 컴파일 · 드라이버 생성을 렌더 스레드가 기다리는 시간을 `RT.PSO.create` 로 잰다). */
        [[nodiscard]] RHIPipelineStateHandle createPipelineStateTimed( IRHIDevice& device, const RHIPipelineStateDesc& desc )
        {
            SW_PROFILE_SCOPE( "RT.PSO.create" );
            return device.getResourceFactory()->createPipelineState( desc );
        }
    } // namespace

    FramePSOManager::FramePSOManager( IRHIDevice* const& pDevice, const RenderPipelineAsset& pipeline, const GPUScene& gpuScene )
        : _pDevice{ pDevice }
        , _pipeline{ pipeline }
        , _gpuScene{ gpuScene }
        , _cache{}
        , _pCache{ &_cache }
    {
    }

    FramePSOManager::~FramePSOManager() = default;

    void FramePSOManager::createPassPSOs()
    {
        if ( _pDevice == nullptr )
            return;
        // 그래픽스 패스는 desc 를 먼저 모으고(아래 1), 셰이더를 워커가 나눠 데운 뒤(2), PSO 를 이 스레드가 차례로 만든다(3).
        vector<PassPSOJob> listJob;
        // 엔진 PSO 는 패스 종류의 표(RenderPassTypeInfo)를 enum 순서로 훑어 만든다. 셰이더 경로는 파이프라인 XML 패스 설정이 먼저이고
        // 표의 EngineDefaultAssets 경로는 마지막 폴백일 뿐이다. 셰이더 쿠커가 같은 표를 훑는다.
        // 저장소에 이미 있는 패스는 만들지 않는다 — 미리보기 씬 렌더러는 주 렌더러의 저장소를 함께 쓰고(`shareCacheOf`), 주 렌더러가 같은 파이프라인으로 먼저 만들었다.
        const EngineDefaultAssets& engineDefaultAssets = engine::getSubsystem<EngineDefaultAssets>();
        const RHICapabilities      caps                = _pDevice->getCapabilities();
        for ( uint32 typeIndex = 0; typeIndex < kRenderPassTypeCount; ++typeIndex )
        {
            const RenderPassType      passType = static_cast<RenderPassType>( typeIndex );
            const RenderPassTypeInfo& info     = getRenderPassTypeInfo( passType );
            const bool                bMissing = info._pDefaultShader != nullptr && _pCache->findEnginePSO( passType ) == 0;
            if ( bMissing == false )
                continue;
            // 셰이더가 파이프라인의 퍼뮤테이션을 요구하는 패스는 그 파이프라인에 있을 때만 만든다(없으면 실행도 없다).
            if ( info.hasFlag( RenderPassTraitFlag::kRequiresPassDesc ) && findPassDescByType( passType ) == nullptr )
                continue;
            // 진단 패스는 요청한 실행에서만 만든다. 네이티브 bindless 가 필요한 패스는 에뮬 백엔드에서 쓰지 않는다(능력 질의로 가린 것이지 실패가 아니다).
            if ( info.hasFlag( RenderPassTraitFlag::kDiagnostic ) && isDiagnosticPassRequested( passType ) == false )
                continue;
            if ( info.hasFlag( RenderPassTraitFlag::kRequiresNativeBindless ) && _pDevice->supportsNativeBindlessSampling() == false )
                continue;
            // 출력 패스는 엔진 PSO 가 없다 — 대상 포맷마다 아래 collectOutputPSOJobs 가 만든다.
            if ( info.hasFlag( RenderPassTraitFlag::kOutputPass ) )
                continue;
            // 컬러 포맷을 정할 근거(표의 고정 포맷 · 파이프라인의 출력 선언)가 없는 그래픽스 패스는 이 파이프라인에서 실행되지 않는다 — RGBA8 로 짐작해 만들지 않는다.
            if ( info.hasFlag( RenderPassTraitFlag::kCompute ) == false && hasRenderTargetFormatSource( passType ) == false )
                continue;

            if ( info.hasFlag( RenderPassTraitFlag::kCompute ) == false )
            {
                PassPSOJob job;
                job._passType = passType;
                if ( makePassPSODesc( passType, findPassDescByType( passType ), nullptr, job._desc ) )
                    listJob.push_back( std::move( job ) );
                else
                    SW_LOG_ERROR( "Engine PSO for pass %# could not be created (shader '%#') - the pass draws nothing", passType,
                                  ( engineDefaultAssets.*info._pDefaultShader ).c_str() );
                continue;
            }
            // 컬링 · 정렬은 간접 인자 버퍼 능력(_bGPUCulling)을, 애니메이션 · 모프는 구조버퍼 UAV(_bCompute)만 요구한다.
            // DX11 은 한 버퍼에 STRUCTURED 와 DRAWINDIRECT_ARGS 를 같이 못 걸어 _bGPUCulling 이 0 이지만 _bCompute 는 1 이다.
            const bool bCapable = info.hasFlag( RenderPassTraitFlag::kRequiresGPUCulling ) ? caps._bGPUCulling != SW_FALSE : caps._bCompute != SW_FALSE;
            if ( bCapable == false )
                continue; // 능력이 없는 백엔드는 이 디스패치를 쓰지 않는다(능력 질의로 가린 것이지 실패가 아니다)
            // 셰이더 경로와 define 은 쿠커와 같은 함수가 정한다(셰이더 assert 변형이 define 으로 온다).
            // 컴퓨트 셰이더는 백엔드 팩토리가 스스로 요청을 짓는다(그래픽스 요청 해석을 타지 않는다) — 데우지 않고 이 자리에서 만든다.
            const RenderPassShaderSelection computeShader = selectRenderPassShader( passType, nullptr, engineDefaultAssets );
            const RHIPipelineStateHandle    pso           = _pDevice->getResourceFactory()->createComputePipelineState( computeShader._shaderPath, {}, computeShader._listDefine );
            // 다른 패스의 PSO 로 대신하지 않는다 — 이 패스는 그리지 않고 여기서 이유를 한 번 남긴다.
            if ( pso == 0 )
            {
                SW_LOG_ERROR( "Engine PSO for pass %# could not be created (shader '%#') - the pass draws nothing", passType,
                              ( engineDefaultAssets.*info._pDefaultShader ).c_str() );
                continue;
            }
            _pCache->setEnginePSO( passType, pso );
        }

        // 출력 패스(Present · Canvas)의 포맷별 변종도 PSO 등록 단계에서 만든다. 기록 중에는 PSO 를 만들 수 없다(findOutputPSO 주석 참고).
        collectOutputPSOJobs( listJob );
        prewarmPassShaders( listJob );
        for ( const PassPSOJob& job : listJob )
        {
            const RHIPipelineStateHandle pso = createPipelineStateTimed( *_pDevice, job._desc );
            registerLayout( pso, job._desc );
            if ( job._outputFormat != RHIFormat::Unknown )
            {
                // 주 출력에 그리는 PSO 다 — 없으면 화면에 아무것도 낼 수 없고, 다른 PSO · 복사로 대신하지 않으므로 여기서 멈춘다.
                if ( pso == 0 )
                    SW_FATAL( "Output pass %# could not create its PSO for target format %# - nothing can be presented", job._passType, static_cast<uint32>( job._outputFormat ) );
                _pCache->setOutputPSO( job._passType, job._outputFormat, pso );
                continue;
            }
            // 다른 패스의 PSO 로 대신하지 않는다 — 이 패스는 그리지 않고(drawFullscreen 은 PSO 없이 그리지 않는다) 여기서 이유를 한 번 남긴다.
            if ( pso == 0 )
            {
                SW_LOG_ERROR( "Engine PSO for pass %# could not be created (shader '%#') - the pass draws nothing", job._passType, job._desc._vertexShaderPath.c_str() );
                continue;
            }
            _pCache->setEnginePSO( job._passType, pso );
        }
        // 폴백 원소는 PSO 를 모두 등록한 뒤에 만든다. 필요한 stride 를 레이아웃에서 읽어야 하고, 기록 중에는 만들 수 없다.
        ensureMaterialFallbackBuffers();
    }

    void FramePSOManager::release()
    {
        // PSO 는 저장소가 순서대로 놓는다: 변형(소유한 것만) → 패스 → Present → 레이아웃 표.
        // 빌려 쓰는 저장소(미리보기 씬 렌더러)는 소유자인 주 렌더러가 놓는다 — 다른 미리보기 창과 주 시점이 아직 그 PSO 로 그린다.
        if ( ownsCache() )
            _pCache->releaseAll( _pDevice );
        for ( MaterialFallback& fallback : _listMaterialFallback )
        {
            fallback._buffer.release( _pDevice );
        }
        _listMaterialFallback.clear();
        _mapLayoutMaterialFallback.clear();
        _particlePSOKey = 0;
    }

    void FramePSOManager::onShaderRecompiled( string_view shaderPath )
    {
        // 빌려 쓰는 저장소(미리보기 씬 렌더러)는 소유자인 주 렌더러가 이미 무효화하고 다시 채웠다.
        if ( ownsCache() )
            _pCache->invalidateLayoutsByShaderPath( shaderPath );
    }

    RHIPipelineStateHandle FramePSOManager::getEnginePSO( RenderPassType passType ) const
    {
        return _pCache->findEnginePSO( passType );
    }

    RHIPipelineStateHandle FramePSOManager::findOutputPSO( RenderPassType passType, RHIFormat targetFormat ) const
    {
        // **조회만 한다.** 없다고 여기서 만들면 안 된다 — 이 함수는 패스 실행 중 = 태스크 워커에서
        // 불린다. PSO 생성은 RHIHandleTable(락 없음)과 Vulkan 렌더 패스 캐시(락 없음)를 건드리므로, 같은
        // 레벨의 다른 패스가 드로우하며 그 표를 읽는 중이면 레이스다. requireRegistryMutableNow 는 bindless
        // 레지스트리만 감시해서 이 경우를 못 잡는다. 변종은 collectOutputPSOJobs 가 셋업에서 만든다.
        // 셋업에 없는 포맷(장치가 알리지 않은 Unknown 포함)의 대상은 설정 결함이다 — 다른 포맷의 PSO · 복사로 대신 그리지 않고 멈춘다(collectOutputPSOJobs 의 목록에 더한다).
        RHIPipelineStateHandle pso{ 0 };
        const bool             bFound = _pCache->findOutputPSO( passType, targetFormat, pso ) && pso != 0;
        if ( bFound == false )
            SW_FATAL( "Output pass %# has no PSO for target format %# from setup - add the format to collectOutputPSOJobs", passType, static_cast<uint32>( targetFormat ) );
        return pso;
    }

    const ShaderBindingLayout* FramePSOManager::findLayout( RHIPipelineStateHandle pso ) const
    {
        return _pCache->findLayout( pso );
    }

    bool FramePSOManager::findDesc( RHIPipelineStateHandle pso, RHIPipelineStateDesc& outDesc ) const
    {
        return _pCache->findDesc( pso, outDesc );
    }

    const RHIStructuredBufferSlot* FramePSOManager::findMaterialFallback( const ShaderBindingLayout* pLayout ) const
    {
        const auto it = _mapLayoutMaterialFallback.find( pLayout );
        return ( it != _mapLayoutMaterialFallback.end() ) ? &_listMaterialFallback[it->second]._buffer : nullptr;
    }

    void FramePSOManager::registerLayout( RHIPipelineStateHandle pso, const RHIPipelineStateDesc& desc )
    {
        if ( pso == 0 || _pDevice == nullptr )
            return;
        // gv_rhiBackend(전역) 대신 이 관리자가 실제로 물려 있는 디바이스의 백엔드를 넘긴다.
        // 한 프로세스에 여러 IRHIDevice 가 동시에 있으면 전역값이 어긋날 수 있다.
        _pCache->registerLayout( pso, desc, _pDevice->getBackendType() );
    }

    const RenderGraphPassDesc* FramePSOManager::findPassDescByType( RenderPassType passType ) const
    {
        // 표기 흔들림은 로드 시점의 _resolvedType 이 이미 흡수했다. 여기서는 값만 비교하면 된다.
        for ( const RenderGraphPassDesc& pass : _pipeline.getGraphPass() )
        {
            if ( pass._resolvedType == passType )
                return &pass;
        }
        return nullptr;
    }

    void FramePSOManager::ensureMaterialFallbackBuffers()
    {
        if ( _pDevice == nullptr || _pDevice->getResourceFactory() == nullptr )
            return;

        // 등록된 PSO 레이아웃마다 머티리얼 폴백 원소를 만든다. stride 와 텍스처 인덱스 필드 위치는 셰이더 타입마다 다를 수 있고, 레이아웃은
        // 이 디바이스의 백엔드로 빌드된 것이다(SPIR-V 도 지금은 `-fvk-use-dx-layout` 으로 DX 규칙을 따른다). 내용이 같은 원소는 버퍼 하나를 나눠 쓴다.
        vector<RenderPSOCache::RegisteredLayout> listLayout;
        _pCache->collectLayouts( listLayout );
        for ( const RenderPSOCache::RegisteredLayout& registered : listLayout )
        {
            const RHIPipelineStateHandle     pso     = registered._pso;
            const ShaderBindingLayout* const pLayout = registered._pLayout;
            if ( pLayout == nullptr || _mapLayoutMaterialFallback.find( pLayout ) != _mapLayoutMaterialFallback.end() )
                continue;
            const ShaderBindingSlot* pSlot = pLayout->find( passConstantNames()._swMaterials );
            if ( pSlot == nullptr )
                continue; // 이 셰이더는 머티리얼 버퍼를 선언하지 않는다(풀스크린 등). 걸 것이 없다.
            if ( pSlot->_elementStride == 0 )
            {
                // 선언은 있는데 원소 레이아웃이 없다 = 리플렉션 공백. 그대로 두면 머티리얼 없는 배치가 슬롯을 비운 채
                // 그리게 되고(Vulkan 은 partially-bound 슬롯을 읽으면 정의되지 않는다) 원인을 찾기 어렵다.
                SW_LOG_ERROR( "The material buffer element stride of PSO %# is 0 - reflection did not provide the g_SwMaterials element layout.", pso );
                continue;
            }

            vector<uint8> bytes         = MaterialUtil::makeFallbackElement( pSlot->_listElementMember, pSlot->_elementStride );
            uint32        fallbackIndex = static_cast<uint32>( _listMaterialFallback.size() );
            for ( uint32 index = 0; index < static_cast<uint32>( _listMaterialFallback.size() ); ++index )
            {
                if ( _listMaterialFallback[index]._bytes == bytes )
                {
                    fallbackIndex = index;
                    break;
                }
            }
            if ( fallbackIndex == _listMaterialFallback.size() )
            {
                const uint32            stride = pSlot->_elementStride;
                RHIStructuredBufferSlot buffer{};
                if ( buffer.ensureCapacity( _pDevice, stride, 1, RHIBufferUsage::Structured | RHIBufferUsage::ShaderResource, true, false, bytes.data() ) == false )
                {
                    SW_LOG_ERROR( "Failed to create the material fallback buffer (stride %#).", stride );
                    continue;
                }
                buffer.upload( _pDevice, bytes.data(), stride );
                MaterialFallback& fallback = _listMaterialFallback.emplace_back();
                fallback._bytes            = std::move( bytes );
                fallback._buffer           = buffer;
            }
            _mapLayoutMaterialFallback.insert_or_assign( pLayout, fallbackIndex );
        }
    }

    void FramePSOManager::prewarmPassShaders( const vector<PassPSOJob>& listJob ) const
    {
        // 셰이더 읽기 · 컴파일(DXC · FXC, 병렬 쿠킹이 이미 워커에서 돌린다)은 디바이스를 쓰지 않아 모든 백엔드에서 워커로 나눈다. PSO 생성은 나누지 않는다 —
        // 드라이버 생성이 직렬이라 병렬 이득이 9 % 로 기각됐고(`MaterialPSOBuilder.h`), GL 은 컨텍스트가 이 스레드에 묶여 워커가 만들 수 없다
        // (언리얼도 셰이더 컴파일은 ShaderCompileWorker 로 나누고 PSO 는 RHI 스레드가 만든다).
        if ( listJob.empty() || engine::areEngineServicesBound() == false )
            return;
        SW_PROFILE_SCOPE( "RT.PSO.prewarmShaders" );
        const ShaderTargetFormat         targetFormat = RHI::getShaderTargetFormat( _pDevice->getBackendType() );
        vector<RHIGraphicsShaderRequest> listRequest;
        listRequest.reserve( listJob.size() );
        for ( const PassPSOJob& job : listJob )
        {
            // 백엔드 팩토리와 같은 해석이다(`RHIShaderRequest::resolveGraphics`) — 캐시 키가 같아 뒤의 생성이 적중한다.
            listRequest.push_back( RHIShaderRequest::resolveGraphics( job._desc, targetFormat ) );
        }
        PassShaderWarmJob warmJob{};
        warmJob._pRequest = listRequest.data();
        warmJob._pShaders = &engine::getSubsystem<ShaderCache>();
        engine::runParallelByCost( static_cast<uint32>( listRequest.size() ), SW_DELEGATE_METHOD( ParallelBlockDelegate, &PassShaderWarmJob::run, &warmJob ) );
    }

    void FramePSOManager::collectOutputPSOJobs( vector<PassPSOJob>& inoutListJob )
    {
        // 주 출력에 그리는 패스(Present · Canvas · Present 를 대신하는 FSR 의 RCAS)의 대상은 백버퍼(디바이스가 실제 채택한 포맷), 오프스크린 렌더 타깃(에디터 씬 뷰 · 게임 뷰 등,
        // 계약값 kOffscreenColorFormat), 스크린샷 캡처 텍스처(계약값 kBackBufferFormat)다. 모두 **셋업에서** 만들어 둔다.
        // 출력 패스는 표의 kOutputPass 가 정한다.
        const RHIFormat arrTargetFormat[] = { _pDevice->getBackBufferFormat(), constant::kOffscreenColorFormat, constant::kBackBufferFormat };
        for ( uint32 typeIndex = 0; typeIndex < kRenderPassTypeCount; ++typeIndex )
        {
            const RenderPassType passType = static_cast<RenderPassType>( typeIndex );
            if ( getRenderPassTypeInfo( passType ).hasFlag( RenderPassTraitFlag::kOutputPass ) == false )
                continue;
            for ( const RHIFormat format : arrTargetFormat )
            {
                RHIPipelineStateHandle existing{ 0 };
                if ( format == RHIFormat::Unknown || _pCache->findOutputPSO( passType, format, existing ) )
                    continue;
                // 백버퍼 포맷과 캡처 포맷이 같을 수 있다 — 같은 변형을 두 번 만들지 않는다.
                bool bQueued{ false };
                for ( const PassPSOJob& queued : inoutListJob )
                {
                    bQueued = bQueued || ( queued._passType == passType && queued._outputFormat == format );
                }
                if ( bQueued )
                    continue;
                PassPSOJob job;
                job._passType                  = passType;
                job._outputFormat              = format;
                const RHIFormat arrRTVFormat[] = { format };
                // 주 출력에 그리는 PSO 다 — 없으면 화면에 아무것도 낼 수 없고, 다른 PSO · 복사로 대신하지 않으므로 여기서 멈춘다.
                if ( makePassPSODesc( passType, findPassDescByType( passType ), arrRTVFormat, job._desc ) == false )
                    SW_FATAL( "Output pass %# could not create its PSO for target format %# - nothing can be presented", passType, static_cast<uint32>( format ) );
                inoutListJob.push_back( std::move( job ) );
            }
        }
    }

    RHIPipelineStateHandle FramePSOManager::findOrCreateCookieAtlasPSO( RHIFormat atlasFormat )
    {
        if ( _pDevice == nullptr )
            return 0;
        // 칸에 옮겨 그리는 PSO — 전체 화면 블릿 셰이더(쿠킹되는 Present 기본 셰이더)를 아틀라스 포맷으로. 머티리얼 변형 저장소의 키 공간을 빌려 저장소가 소유한다
        // (입자 PSO 와 같은 방식 — 디바이스를 바꾸면 저장소가 비고 여기서 다시 만든다).
        const string& shaderPath = engine::getSubsystem<EngineDefaultAssets>()._shaderFullscreenBlit.get();
        const uint64  psoKey     = RenderPSOCache::materialPSOKey( 0, HashUtil::combine( hashed_string( shaderPath ).getHash(), FourCcUtil::make( "CKAT" ) ), RenderViewMode::Lit, false );
        if ( _pCache->hasMaterialPSO( psoKey ) == false )
        {
            RHIPipelineStateDesc desc{};
            desc._vertexShaderPath              = shaderPath;
            desc._pixelShaderPath               = shaderPath;
            desc._vertexEntryPoint              = string( resolveEntryPoint( string_view{}, ShaderStage::Vertex ) );
            desc._pixelEntryPoint               = string( resolveEntryPoint( string_view{}, ShaderStage::Pixel ) );
            desc._numRenderTargets              = 1;
            desc._arrRTVFormat[0]               = atlasFormat;
            desc._depthStencilFormat            = RHIFormat::Unknown;
            desc._bEnableDepthTest              = 0;
            desc._bEnableDepthWrite             = 0;
            desc._cullMode                      = RHICullMode::None;
            const RHIPipelineStateHandle handle = _pDevice->getResourceFactory()->createPipelineState( desc );
            if ( handle == 0 )
                SW_LOG_ERROR( "The 2D light cookie atlas PSO could not be created from '%#' - cookie lights draw without cookies", shaderPath );
            else
                registerLayout( handle, desc );
            _pCache->setMaterialPSO( psoKey, RenderPSOCache::MaterialPSOEntry::ready( handle, true ) );
        }
        return _pCache->findMaterialPSO( psoKey );
    }

    RHIPipelineStateHandle FramePSOManager::createPSOForPassType( RenderPassType passType, const RHIFormat* pRTVFormatOverride )
    {
        return createPSOForPass( passType, findPassDescByType( passType ), pRTVFormatOverride );
    }

    RHIPipelineStateHandle FramePSOManager::findOrCreateExtensionProgramPSO( const ExtensionProgramDesc& program, RHIFormat colorFormat, bool bDepthTest )
    {
        // 모듈 확장 패스의 프로그램(ExtensionRenderPass M3)이다. 셰이더 · 고정 상태 · 타깃 포맷이 같으면 한 PSO 를 나눈다. 기록 중에는 만들 수 없으므로 셋업(`FrameExtensionHost::collectFrame`)만 부른다.
        // 실패(0)도 기록한다 — 매 프레임 같은 컴파일을 되풀이하지 않고, 부르는 쪽이 0 을 보고 오류를 남기고 그리지 않는다(다른 셰이더로 대신 그리지 않는다).
        if ( _pDevice == nullptr || program._shaderPath.empty() )
            return 0;
        string key = program._shaderPath;
        key.append( "|" );
        key.push_back( program._bCullBack ? 'c' : 'n' );
        key.push_back( bDepthTest ? 't' : 'f' );
        key.push_back( program._bDepthWrite ? 'w' : 'r' );
        key.push_back( program._bBlend ? 'b' : 'o' );
        key.append( "|" );
        for ( uint32 formatValue = static_cast<uint32>( colorFormat );; formatValue /= 10u )
        {
            key.push_back( static_cast<char>( '0' + ( formatValue % 10u ) ) );
            if ( formatValue < 10u )
                break;
        }
        const hashed_string    programKey{ string_view{ key } };
        RHIPipelineStateHandle existing{ 0 };
        if ( _pCache->findExtensionProgramPSO( programKey, existing ) )
            return existing;

        RHIPipelineStateDesc desc{};
        desc._vertexShaderPath              = program._shaderPath;
        desc._pixelShaderPath               = program._shaderPath;
        desc._vertexEntryPoint              = string( resolveEntryPoint( string_view{}, ShaderStage::Vertex ) );
        desc._pixelEntryPoint               = string( resolveEntryPoint( string_view{}, ShaderStage::Pixel ) );
        desc._numRenderTargets              = 1;
        desc._arrRTVFormat[0]               = colorFormat;
        desc._bEnableDepthTest              = bDepthTest ? 1 : 0;
        desc._bEnableDepthWrite             = ( bDepthTest && program._bDepthWrite ) ? 1 : 0;
        desc._depthStencilFormat            = bDepthTest ? constant::kDepthStencilFormat : RHIFormat::Unknown;
        desc._bEnableBlend                  = program._bBlend ? 1 : 0;
        desc._bPremultipliedAlpha           = program._bBlend ? SW_TRUE : SW_FALSE;
        desc._cullMode                      = program._bCullBack ? RHICullMode::Back : RHICullMode::None;
        const RHIPipelineStateHandle handle = createPipelineStateTimed( *_pDevice, desc );
        registerLayout( handle, desc );
        if ( handle == 0 )
            SW_LOG_ERROR( "Extension program '%#' could not be created (see the shader error above) - passes that use it are not drawn", program._shaderPath );
        _pCache->setExtensionProgramPSO( programKey, handle );
        return handle;
    }

    RHIPipelineStateHandle FramePSOManager::createPSOForPass( RenderPassType passType, const RenderGraphPassDesc* pPassDesc, const RHIFormat* pRTVFormatOverride )
    {
        if ( _pDevice == nullptr )
            return 0;
        RHIPipelineStateDesc desc{};
        if ( makePassPSODesc( passType, pPassDesc, pRTVFormatOverride, desc ) == false )
            return 0;
        const RHIPipelineStateHandle handle = createPipelineStateTimed( *_pDevice, desc );
        registerLayout( handle, desc );
        return handle;
    }

    bool FramePSOManager::makePassPSODesc( RenderPassType passType, const RenderGraphPassDesc* pPassDesc, const RHIFormat* pRTVFormatOverride, RHIPipelineStateDesc& outDesc ) const
    {
        // 셰이더, define, 기본 렌더 상태는 패스 종류 테이블(RenderPassTypeInfo)이 정하고, 파이프라인 XML 의 패스 서술이 그 위를 조정한다.
        // 셰이더와 define 은 쿠커와 **같은 함수**(selectRenderPassShader)로 정한다. 어긋나면 Shipping 에서 매니페스트 미스가 난다.
        const RenderPassTypeInfo&       info   = getRenderPassTypeInfo( passType );
        const RenderPassShaderSelection shader = selectRenderPassShader( passType, pPassDesc, engine::getSubsystem<EngineDefaultAssets>() );

        RHIPipelineStateDesc& desc = outDesc;
        desc                       = RHIPipelineStateDesc{};
        desc._vertexShaderPath     = shader._shaderPath;
        desc._pixelShaderPath      = desc._vertexShaderPath;
        desc._vertexEntryPoint     = string( resolveEntryPoint( pPassDesc != nullptr ? string_view( pPassDesc->_vertexEntryPoint ) : string_view{}, ShaderStage::Vertex ) );
        desc._pixelEntryPoint      = string( resolveEntryPoint( pPassDesc != nullptr ? string_view( pPassDesc->_pixelEntryPoint ) : string_view{}, ShaderStage::Pixel ) );
        applyPassDepthAndBlend( desc, info, pPassDesc );
        applyPassCullMode( desc, info, pPassDesc, passType );
        desc._listShaderDefine = shader._listDefine;
        // 내부 슬롯은 대신 그리는 파이프라인 패스의 출력 선언으로 포맷을 정한다(`_targetSourceType`).
        const RenderGraphPassDesc* pTargetDesc =
            ( pPassDesc == nullptr && info._targetSourceType != RenderPassType::Invalid ) ? findPassDescByType( info._targetSourceType ) : pPassDesc;
        if ( fillPassRenderTargetFormats( desc, pTargetDesc, pRTVFormatOverride != nullptr ? pRTVFormatOverride : info._pColorFormat, info._colorTargetCount ) == false )
        {
            SW_LOG_ERROR( "Pass %# has no render target format (no fixed format, and no color output declaration with a known format) - no PSO is created", passType );
            return false;
        }

        // 컬러 출력이 없는 패스(그림자, 뎁스 프리패스)는 픽셀 스테이지가 없다. 셰이더에 PSMain 이 있어도 붙이지
        // 않는다. 주의: 여기서 정하지 않으면 백엔드마다 판단이 갈리고, 그 위에 머티리얼 define 을 적용한 변형
        // (createMaterialPSOVariant 는 이 desc 를 그대로 물려받는다)은 쿠커가 쿠킹하지 않아 Shipping 에서 매니페스트 미스로
        // 실패한다. 쿠커 쪽 같은 규칙은 FrameRendererUtil::hasPixelStage 다.
        if ( desc._numRenderTargets == 0 )
        {
            desc._pixelShaderPath.clear();
            desc._pixelEntryPoint.clear();
        }
        return true;
    }

    bool FramePSOManager::fillPassRenderTargetFormats( RHIPipelineStateDesc& desc, const RenderGraphPassDesc* pPassDesc, const RHIFormat* pRTVFormats,
                                                       uint32 numRenderTargets ) const
    {
        desc._numRenderTargets = numRenderTargets;
        if ( desc._numRenderTargets > kMaxColorAttachments )
            desc._numRenderTargets = kMaxColorAttachments;
        if ( pRTVFormats != nullptr )
        {
            for ( uint32 rtIndex = 0; rtIndex < desc._numRenderTargets; ++rtIndex )
            {
                desc._arrRTVFormat[rtIndex] = pRTVFormats[rtIndex];
            }
            return true;
        }
        if ( pPassDesc != nullptr && pPassDesc->_listOutput.empty() == false )
        {
            // 출력 선언에서 컬러 RT 를 센다. 쿠커가 픽셀 스테이지 유무를 판정하는 것과 **같은 함수**다. Swapchain 출력은 포맷을 모른다(Unknown) —
            // 그 포맷은 출력 패스의 대상 포맷(pRTVFormats)으로만 정하므로 여기까지 오면 근거가 없다.
            desc._numRenderTargets = FrameRendererUtil::collectColorOutputFormats( *pPassDesc, _pipeline.getDesc()._listAttachment, desc._arrRTVFormat,
                                                                                   kMaxColorAttachments, numRenderTargets );
            for ( uint32 rtIndex = 0; rtIndex < desc._numRenderTargets; ++rtIndex )
            {
                if ( desc._arrRTVFormat[rtIndex] == RHIFormat::Unknown )
                    return false;
            }
            return true;
        }
        // 고정 포맷도 출력 선언도 없다 — 컬러가 없는 패스만 맞다(RGBA8 로 짐작하지 않는다).
        return desc._numRenderTargets == 0;
    }

    bool FramePSOManager::hasRenderTargetFormatSource( RenderPassType passType ) const
    {
        const RenderPassTypeInfo&  info        = getRenderPassTypeInfo( passType );
        const RenderPassType       sourceType  = info._targetSourceType != RenderPassType::Invalid ? info._targetSourceType : passType;
        const RenderGraphPassDesc* pTargetDesc = findPassDescByType( sourceType );
        const bool                 bDeclared   = pTargetDesc != nullptr && pTargetDesc->_listOutput.empty() == false;
        return info._pColorFormat != nullptr || info._colorTargetCount == 0 || bDeclared;
    }

    RenderPSOCache::MaterialPSOEntry FramePSOManager::createMaterialPSOVariant( RHIPipelineStateHandle passPSO, RenderPassType passType,
                                                                                const GPUShaderPermutation* pPermutation,
                                                                                RenderViewMode viewMode, bool bReverseCulling, RHIPipelineStateDesc* pOutDeferredDesc )
    {
        if ( passPSO == 0 || _pDevice == nullptr )
            return RenderPSOCache::MaterialPSOEntry::failed();

        // 패스가 정한 렌더 상태를 통째로 물려받는다. 블렌드, 뎁스, RT 포맷은 패스의 사실이지 머티리얼의 것이 아니다.
        RHIPipelineStateDesc desc{};
        if ( _pCache->findDesc( passPSO, desc ) == false )
        {
            // desc 를 모르는 PSO 다 — 변형을 만들 근거가 없다. 패스 PSO 로 다른 그림을 그리지 않고 이 조합은 그리지 않는다.
            SW_LOG_ERROR( "Material PSO variant: pass PSO of pass type %# has no recorded desc - these batches are not drawn", static_cast<uint32>( passType ) );
            return RenderPSOCache::MaterialPSOEntry::failed();
        }

        bool bChanged{ false };
        if ( pPermutation != nullptr )
        {
            // 양면 머티리얼은 후면 컬링을 끈다(언리얼 머티리얼의 Two Sided). 앞면 컬링 패스(외곽선 껍질)는 그대로 둔다. 껍질은 뒤집어 그려야 외곽선이다.
            const bool bTwoSided = VectorUtil::contains( pPermutation->_listDefine, string( kMaterialTwoSidedDefine ) );
            if ( bTwoSided && desc._cullMode == RHICullMode::Back )
            {
                desc._cullMode = RHICullMode::None;
                bChanged       = true;
            }
            // 머티리얼 셰이더를 쓰는 패스만 경로를 교체한다. 그림자와 뎁스는 자기 지오메트리 셰이더가 기준이고, 모양을 머티리얼 코드가 정하는
            // 배치(알파 컷오프 · 정점 변형)만 머티리얼 셰이더의 깊이 퍼뮤테이션으로 그린다(drawsDepthWithMaterialShader — 쿠커와 같은 판정).
            const bool bMaterialDepthShader = FrameRendererUtil::drawsDepthWithMaterialShader( passType, &pPermutation->_listDefine );
            const bool bReplaceShaderPath   = ( FrameRendererUtil::usesMaterialShader( passType ) || bMaterialDepthShader ) && pPermutation->_shaderPath.empty() == false &&
                                            pPermutation->_shaderPath != desc._vertexShaderPath;
            if ( bReplaceShaderPath )
            {
                desc._vertexShaderPath = pPermutation->_shaderPath;
                desc._pixelShaderPath  = pPermutation->_shaderPath;
                // 깊이 패스는 컬러 출력이 없다 — 픽셀 스테이지는 알파 컷오프의 버리기에만 둔다.
                if ( bMaterialDepthShader && FrameRendererUtil::hasMaterialDepthPixelStage( passType, &pPermutation->_listDefine ) == false )
                    desc._pixelShaderPath.clear();
                bChanged = true;
            }
            // 등장 디더 변형은 패스 셰이더의 깊이 패스에도 픽셀 스테이지가 있어야 한다 — 기본 패스와 같은 픽셀을 버린다(쿠커와 같은 판정).
            if ( FrameRendererUtil::addsAppearanceDitherPixelStage( passType, &pPermutation->_listDefine ) && desc._pixelShaderPath.empty() )
            {
                desc._pixelShaderPath = desc._vertexShaderPath;
                if ( desc._pixelEntryPoint.empty() )
                    desc._pixelEntryPoint = string( resolveEntryPoint( string_view{}, ShaderStage::Pixel ) );
                bChanged = true;
            }
            for ( const string& defineStr : pPermutation->_listDefine )
            {
                bool bFound{ false };
                for ( const string& existing : desc._listShaderDefine )
                {
                    if ( existing == defineStr )
                    {
                        bFound = true;
                        break;
                    }
                }
                if ( bFound == false )
                {
                    desc._listShaderDefine.push_back( defineStr );
                    bChanged = true;
                }
            }
        }

        // 뷰 모드는 머티리얼 퍼뮤테이션 **뒤에** 적용한다. 순서가 반대면 머티리얼이 셰이더 경로를
        // 교체할 때 방금 넣은 define 이 다른 셰이더로 넘어가 의미가 달라진다.
        if ( FrameRendererUtil::appliesViewMode( passType ) && applyViewModeToDesc( desc, viewMode ) )
            bChanged = true;

        // 거울 변환 배치는 감김이 뒤집혀 있다. 패스의 컬 모드를 맞바꿔야 같은 면(바깥 면)이 남는다. 와이어프레임처럼 컬링이 없는
        // 패스(None)는 바꿀 것이 없다. 뷰 모드 뒤에 둔다. 뷰 모드가 컬링을 끄면 뒤집을 것도 없다.
        if ( bReverseCulling && reverseCullMode( desc ) )
            bChanged = true;

        // 적용할 것이 없다는 것은 패스 PSO 가 이미 이 머티리얼의 셰이더이고 모드도 Lit 이고 컬 모드도 그대로라는 뜻이다. 똑같은 PSO 를 하나 더 만들 이유가 없다.
        if ( bChanged == false )
            return RenderPSOCache::MaterialPSOEntry::ready( passPSO, false );

        // 셰이더 컴파일 · 드라이버 생성은 호출자가 desc 를 받아 빌더 줄에 넣는다(`Building` 으로 돌려준다). 합치는 쪽이 같은 규칙으로 `Ready` · `Failed` 를 정한다.
        if ( pOutDeferredDesc != nullptr )
        {
            *pOutDeferredDesc = std::move( desc );
            return RenderPSOCache::MaterialPSOEntry::building();
        }

        const RHIPipelineStateHandle handle = createPipelineStateTimed( *_pDevice, desc );
        if ( handle == 0 )
        {
            // 컴파일 실패다. 패스 PSO(다른 셰이더 · 다른 보기 모드)로 그리면 틀린 그림이 통과하므로 이 조합은 그리지 않고 오류를 남긴다.
            SW_LOG_ERROR( "Material PSO variant '%#' (view mode %#) failed to build - these batches are not drawn", desc._vertexShaderPath,
                          getRenderViewModeInfo( viewMode )._pName );
            return RenderPSOCache::MaterialPSOEntry::failed();
        }
        registerLayout( handle, desc );
        return RenderPSOCache::MaterialPSOEntry::ready( handle, true );
    }

    void FramePSOManager::ensureMaterialPSOs( uint32 viewModeMask )
    {
        SW_PROFILE_SCOPE( "RT.PSO.ensureMaterial" );
        if ( _pDevice == nullptr )
            return;

        // 이번 프레임 배치가 실제로 쓰는 (퍼뮤테이션, 컬 반전) 조합만 본다. 불투명 패스와 반투명 패스는 배치 목록이 다르므로
        // 유리 머티리얼의 변형을 그림자 패스까지 만들어 두는 낭비가 없다.
        vector<MaterialPSORequest>& listOpaqueRequest      = _listOpaquePSORequestScratch;
        vector<MaterialPSORequest>& listTransparentRequest = _listTransparentPSORequestScratch;
        collectMaterialPSORequests( _gpuScene.getOpaqueBatches(), listOpaqueRequest );
        collectMaterialPSORequests( _gpuScene.getTransparentBatches(), listTransparentRequest );
        if ( listOpaqueRequest.empty() && listTransparentRequest.empty() )
            return;

        bool bCreatedVariant{ false };
        for ( const auto& [passType, passPSO] : _pCache->getEnginePSOs() )
        {
            if ( passPSO == 0 || FrameRendererUtil::drawsSceneMeshes( passType ) == false )
                continue;
            const bool                        bTransparentPass = getRenderPassTypeInfo( passType ).hasFlag( RenderPassTraitFlag::kDrawsTransparentBatch );
            const vector<MaterialPSORequest>& listRequest      = bTransparentPass ? listTransparentRequest : listOpaqueRequest;
            for ( const MaterialPSORequest& request : listRequest )
            {
                if ( ensureRequestMaterialPSOs( passPSO, passType, request, viewModeMask ) )
                    bCreatedVariant = true;
            }
        }

        // 방금 만든 변형의 레이아웃은 셋업 때 폴백 stride 를 모으던 시점에는 없었다. 그 셰이더가
        // 다른 크기의 MaterialData 를 선언하면 그 stride 의 폴백이 없고, 머티리얼 없는 배치가
        // 그 PSO 로 그려질 때 registerMaterialBuffer 가 t5 를 **비운 채** 드로우를 낸다.
        // Vulkan 이 초기화되지 않은 디스크립터를 읽어 디바이스를 잃는 그 경로다.
        // 여기는 아직 기록 시작 전이라 버퍼를 만들 수 있다. 새 변형을 만든 프레임에만 돈다.
        if ( bCreatedVariant )
            ensureMaterialFallbackBuffers();
    }

    void FramePSOManager::warmPinnedMaterialPSOs( uint32 viewModeMask )
    {
        // 줄은 하나라 주 렌더러만 부른다(`FrameRenderer::uploadSceneAndSubmitFrame`) — 미리보기 렌더러가 꺼내 가면 주 렌더러가 만들지 못한다.
        const bool bNoWarm = _pDevice == nullptr || engine::areEngineServicesBound() == false;
        if ( bNoWarm )
            return;
        vector<shared_ptr<Material>>& listMaterial = _listPSOWarmMaterialScratch;
        engine::getSubsystem<AssetManager>().getAssetCache<MaterialCache>().takePSOWarmMaterials( listMaterial );
        if ( listMaterial.empty() )
            return;
        SW_PROFILE_SCOPE( "RT.PSO.warmPinned" );
        // 씬 배치와 같은 키로 만든다(`GPUMaterialRegistry::makeShaderPermutation`) — 그 머티리얼이 씬에 들어오는 프레임에는 이미 캐시에 있다.
        // 비동기로 읽혀 나타나는 메시는 등장 디더 변형으로 먼저 그려지므로 그 변형도 만든다. 거울 배치(컬 반전)는 드물어 그 프레임에 만든다.
        bool bCreatedVariant{ false };
        for ( const shared_ptr<Material>& pMaterial : listMaterial )
        {
            const bool bTransparentMaterial = pMaterial->getBlendMode() == RHIBlendMode::Transparent;
            for ( const bool bAppearanceDither : { false, true } )
            {
                const GPUShaderPermutation permutation = GPUMaterialRegistry::makeShaderPermutation( *pMaterial, nullptr, bAppearanceDither );
                for ( const auto& [passType, passPSO] : _pCache->getEnginePSOs() )
                {
                    const bool bTransparentPass = getRenderPassTypeInfo( passType ).hasFlag( RenderPassTraitFlag::kDrawsTransparentBatch );
                    const bool bSkippedPass     = passPSO == 0 || FrameRendererUtil::drawsSceneMeshes( passType ) == false || bTransparentPass != bTransparentMaterial;
                    if ( bSkippedPass || FrameRendererUtil::drawsMaterialInPass( passType, &permutation._listDefine ) == false )
                        continue;
                    for ( uint32 modeIndex = 0; modeIndex < static_cast<uint32>( RenderViewMode::Count ); ++modeIndex )
                    {
                        if ( ( viewModeMask & ( 1u << modeIndex ) ) == 0 )
                            continue;
                        if ( ensureMaterialPSOVariant( passPSO, passType, &permutation, permutation._hash, false, static_cast<RenderViewMode>( modeIndex ), false ) )
                            bCreatedVariant = true;
                    }
                }
            }
        }
        listMaterial.clear(); // 소유를 오래 쥐지 않는다 — 고정을 놓으면 머티리얼이 사라질 수 있어야 한다
        if ( bCreatedVariant )
            ensureMaterialFallbackBuffers();
    }

    void FramePSOManager::collectMaterialPSORequests( vector_reference<const GPUMeshBatch> listBatch, vector<MaterialPSORequest>& outListRequest )
    {
        // 퍼뮤테이션이 없는 배치(머티리얼을 붙이지 않은 메시)도 모은다. Lit 이 아니거나 거울 배치면 그 배치에도
        // 변형이 필요하다. 그냥 건너뛰면 머티리얼 없는 메시만 뷰 모드와 컬 반전이 적용되지 않아 화면이 섞인다.
        outListRequest.clear();
        for ( const GPUMeshBatch& batch : listBatch )
        {
            bool bFound{ false };
            for ( const MaterialPSORequest& existing : outListRequest )
            {
                if ( existing._shaderPermutation == batch._shaderPermutation && existing._bReverseCulling == batch._bReverseCulling )
                {
                    bFound = true;
                    break;
                }
            }
            if ( bFound == false )
                outListRequest.push_back( MaterialPSORequest{ batch._shaderPermutation, batch._bReverseCulling } );
        }
    }

    bool FramePSOManager::ensureRequestMaterialPSOs( RHIPipelineStateHandle passPSO, RenderPassType passType, const MaterialPSORequest& request, uint32 viewModeMask )
    {
        const bool bReverseCulling = ( request._bReverseCulling != SW_FALSE );
        // 이 패스가 그리지 않는 배치(외곽선을 켜지 않은 머티리얼)의 변형은 만들지 않는다. 쿠커도 그것을 쿠킹하지 않는다.
        const GPUShaderPermutation* pRequestPermutation =
            ( request._shaderPermutation == kInvalidShaderPermutation ) ? nullptr : _gpuScene.findShaderPermutation( request._shaderPermutation );
        if ( FrameRendererUtil::drawsMaterialInPass( passType, pRequestPermutation != nullptr ? &pRequestPermutation->_listDefine : nullptr ) == false )
            return false;
        bool bCreatedVariant{ false };
        for ( uint32 modeIndex = 0; modeIndex < static_cast<uint32>( RenderViewMode::Count ); ++modeIndex )
        {
            if ( ( viewModeMask & ( 1u << modeIndex ) ) == 0 )
                continue;
            const RenderViewMode viewMode = static_cast<RenderViewMode>( modeIndex );
            if ( request._shaderPermutation == kInvalidShaderPermutation )
            {
                // 퍼뮤테이션이 없는 배치를 위한 변형이다. 적용하는 것이 뷰 모드와 컬 반전뿐이다. Lit 이고 거울이 아니면 만들 것이 없다
                // (그때는 패스 PSO 가 그대로 정답이고 psoForBatch 도 캐시를 보지 않는다).
                const bool bAppliesViewMode = ( viewMode != RenderViewMode::Lit ) && FrameRendererUtil::appliesViewMode( passType );
                const bool bNeedsVariant    = bAppliesViewMode || bReverseCulling;
                if ( bNeedsVariant && ensureMaterialPSOVariant( passPSO, passType, nullptr, 0, bReverseCulling, viewMode ) )
                    bCreatedVariant = true;
                continue;
            }
            if ( pRequestPermutation == nullptr )
                continue;
            if ( ensureMaterialPSOVariant( passPSO, passType, pRequestPermutation, pRequestPermutation->_hash, bReverseCulling, viewMode ) )
                bCreatedVariant = true;
        }
        return bCreatedVariant;
    }

    bool FramePSOManager::ensureMaterialPSOVariant( RHIPipelineStateHandle passPSO, RenderPassType passType, const GPUShaderPermutation* pPermutation,
                                                    uint64 permutationHash, bool bReverseCulling, RenderViewMode viewMode, bool bUrgent )
    {
        // 키를 `Building` 으로 올린 쪽만 만든다(`beginMaterialPSO`). 셰이더 컴파일이 낄 수 있는 변형은 desc 만 정해 빌더 줄에 넣고(렌더 스레드의 임계 경로에서 뺀다),
        // 결과는 `pumpMaterialPSOBuilds` 가 기록 전에 합친다. 드로우 경로는 `Building` 배치를 이번 프레임만 건너뛴다(`RT.PSO.pendingBatches`).
        const uint64 key = RenderPSOCache::materialPSOKey( passPSO, permutationHash, viewMode, bReverseCulling );
        if ( _pCache->beginMaterialPSO( key ) == false )
            return false;
        RHIPipelineStateDesc                   deferredDesc{};
        const RenderPSOCache::MaterialPSOEntry entry = createMaterialPSOVariant( passPSO, passType, pPermutation, viewMode, bReverseCulling, &deferredDesc );
        if ( entry._state == RenderPSOCache::MaterialPSOState::Building )
        {
            _pCache->requestMaterialPSO( _pDevice, key, std::move( deferredDesc ), bUrgent, usesAsyncMaterialPSOBuilder() );
            return false;
        }
        _pCache->setMaterialPSO( key, entry );
        return true;
    }

    bool FramePSOManager::usesAsyncMaterialPSOBuilder() const
    {
        // GL 은 컨텍스트가 스레드에 묶여 빌더 스레드에서 만들 수 없다 — 렌더 스레드가 같은 줄을 예산 안에서 만든다.
        // 결정적 실행(`AssetManager::isFinishingAssetWorkEachFrame`)은 빌더 스레드를 쓰지 않는다 — 변형이 언제 준비되느냐가 컴파일 시간(셰이더 캐시가 비었는가)에 달린다.
        return gv_psoAsync && AssetManager::isFinishingAssetWorkEachFrame() == false && _pDevice != nullptr && _pDevice->getCapabilities()._bThreadSafeResourceCreation != SW_FALSE;
    }

    void FramePSOManager::pumpMaterialPSOBuilds()
    {
        if ( _pDevice == nullptr )
            return;
        SW_PROFILE_SCOPE( "RT.PSO.pumpBuilds" );
        // 빌더 스레드를 쓰지 않을 때: `-gv_psoAsync=0` 은 전부(옛 동작, A/B 측정), 그 밖(GL)은 프레임당 시간 예산(줄이 있으면 최소 하나)이다.
        constexpr uint32 kFrameBudgetMicroseconds = 2000;
        const bool       bBuildAll                = gv_psoAsync == false || AssetManager::isFinishingAssetWorkEachFrame(); // 결정적 실행은 시간 예산 없이 이 프레임에 모두 만든다
        const uint32     budgetMicroseconds       = bBuildAll ? 0u : kFrameBudgetMicroseconds;
        const uint32     readyCount               = _pCache->pumpMaterialPSOBuilds( _pDevice, usesAsyncMaterialPSOBuilder(), budgetMicroseconds );
        SW_PROFILE_COUNT( "RT.PSO.pendingBuilds", _pCache->getPendingMaterialPSOBuilds() );
        // 방금 합친 변형의 레이아웃은 폴백 stride 를 모으던 시점에는 없었다(`ensureMaterialPSOs` 의 같은 이유).
        if ( readyCount > 0 )
            ensureMaterialFallbackBuffers();
    }

    void FramePSOManager::ensureParticlePSO()
    {
        _particlePSOKey                              = 0;
        const RHIPipelineStateHandle transparentPSO  = getEnginePSO( RenderPassType::Transparent );
        const bool                   bNoParticlePass = _pDevice == nullptr || transparentPSO == 0 || _gpuScene.getParticleBatches().empty();
        if ( bNoParticlePass )
            return;

        // 머티리얼 변형 캐시의 키 공간을 빌린다 — 퍼뮤테이션 해시 자리에 입자 셰이더 경로와 태그를 섞어 머티리얼 퍼뮤테이션과 겹치지 않게 한다.
        const string& shaderPath = engine::getSubsystem<EngineDefaultAssets>()._shaderParticle.get();
        const uint64  shaderHash = HashUtil::combine( hashed_string( shaderPath ).getHash(), FourCcUtil::make( "VFXP" ) );
        _particlePSOKey          = RenderPSOCache::materialPSOKey( transparentPSO, shaderHash, RenderViewMode::Lit, false );
        if ( _pCache->hasMaterialPSO( _particlePSOKey ) )
            return;

        RHIPipelineStateDesc desc{};
        if ( _pCache->findDesc( transparentPSO, desc ) == false )
        {
            SW_LOG_ERROR( "Transparent pass PSO has no recorded desc - particles are not drawn" );
            _pCache->setMaterialPSO( _particlePSOKey, RenderPSOCache::MaterialPSOEntry::failed() );
            return;
        }
        desc._vertexShaderPath = shaderPath;
        desc._pixelShaderPath  = shaderPath;
        desc._vertexEntryPoint = string( resolveEntryPoint( string_view{}, ShaderStage::Vertex ) );
        desc._pixelEntryPoint  = string( resolveEntryPoint( string_view{}, ShaderStage::Pixel ) );
        desc._listShaderDefine.clear();
        desc._cullMode                      = RHICullMode::None;
        desc._fillMode                      = RHIFillMode::Solid;
        desc._bEnableBlend                  = 1;
        desc._bPremultipliedAlpha           = SW_TRUE;
        desc._bEnableDepthWrite             = 0;
        const RHIPipelineStateHandle handle = createPipelineStateTimed( *_pDevice, desc );
        if ( handle == 0 )
            SW_LOG_ERROR( "Failed to create the particle PSO from '%#' - particles are not drawn", shaderPath );
        registerLayout( handle, desc );
        _pCache->setMaterialPSO( _particlePSOKey, RenderPSOCache::MaterialPSOEntry::ready( handle, true ) );
    }

    RHIPipelineStateHandle FramePSOManager::findParticlePSO() const
    {
        return _particlePSOKey != 0 ? _pCache->findMaterialPSO( _particlePSOKey ) : RHIPipelineStateHandle{ 0 };
    }

    RHIPipelineStateHandle FramePSOManager::psoForBatch( RHIPipelineStateHandle passPSO, const GPUMeshBatch& batch, RenderViewMode viewMode,
                                                         RenderPSOCache::MaterialPSOState* pOutState ) const
    {
        if ( pOutState != nullptr )
            *pOutState = RenderPSOCache::MaterialPSOState::Ready;
        if ( passPSO == 0 )
            return passPSO;

        uint64 permutationHash{ 0 };
        if ( batch._shaderPermutation != kInvalidShaderPermutation )
        {
            const GPUShaderPermutation* pPermutation = _gpuScene.findShaderPermutation( batch._shaderPermutation );
            if ( pPermutation != nullptr )
                permutationHash = pPermutation->_hash;
        }

        // 적용할 것이 하나도 없는 조합이다. ensureMaterialPSOs 도 이 키를 만들지 않는다.
        // (해시 0은 퍼뮤테이션 없음을 뜻한다. 실제 퍼뮤테이션 해시가 0이 되더라도 같은 키로
        //  떨어지는데, 그 둘이 만드는 디스크립터는 어차피 같으므로 해롭지 않다.)
        viewMode                   = getRenderViewModePipelineMode( viewMode ); // Lit 과 같은 PSO 인 모드(MarkerSlots)는 Lit 변형을 그대로 쓴다
        const bool bReverseCulling = ( batch._bReverseCulling != SW_FALSE );
        const bool bDefaultPassPSO = permutationHash == 0 && viewMode == RenderViewMode::Lit && bReverseCulling == false;
        if ( bDefaultPassPSO )
            return passPSO;

        // `Ready` 가 아닌 변형(만드는 중 · 실패 · 없음)은 그리지 않는다 — 패스 PSO 로 바꿔 그리면 머티리얼 · 보기 모드가 다른 그림이 된다. ensureMaterialPSOs 가 기록 전에
        // 채우므로 동기 생성에서는 항상 `Ready` 나 `Failed` 이고(`Failed` 는 그때 오류를 남겼다), `Building` 은 빌더 스레드(U12)가 아직 못 끝낸 배치다.
        const uint64 key = RenderPSOCache::materialPSOKey( passPSO, permutationHash, viewMode, bReverseCulling );
        if ( pOutState != nullptr )
            *pOutState = _pCache->findMaterialPSOState( key );
        return _pCache->findMaterialPSO( key );
    }

} // namespace sw
