#include "pch.h"

#include "Engine/Renderer/Frame/Resource/FramePSOManager.h"

#include "Core/Algorithm/VectorUtil.h"
#include "Core/Encoding/FourCCUtil.h"
#include "Core/Hash/HashUtil.h"
#include "Core/String/StringUtil.h"
#include "Core/Task/TaskManager.h"

#include "Engine/Config/EngineConfig.h"
#include "Engine/Config/EngineDefaultAssets.h"
#include "Engine/Foundation/DeterministicRun.h"
#include "Engine/Foundation/EngineParallel.h"
#include "Engine/Foundation/EngineServices.h"
#include "Engine/Graphics/Material/Material.h"
#include "Engine/Graphics/Material/MaterialCache.h"
#include "Engine/Graphics/Material/MaterialInstance.h"
#include "Engine/Graphics/Material/MaterialPacking.h"
#include "Engine/Graphics/Material/MaterialPermutation.h"
#include "Engine/Graphics/Mesh/Mesh.h"
#include "Engine/Graphics/RHI/IRHIDevice.h"
#include "Engine/Graphics/RHI/IRHIResourceFactory.h"
#include "Engine/Graphics/RHI/RHI.h"
#include "Engine/Graphics/RHI/RHIPipelineStateDesc.h"
#include "Engine/Graphics/Shader/Binding/ShaderBindingLayout.h"
#include "Engine/Graphics/Shader/Compile/RHIShaderRequest.h"
#include "Engine/Graphics/Shader/Compile/ShaderCache.h"
#include "Engine/Graphics/Shader/Compile/ShaderCompiler.h"
#include "Engine/Profiling/FrameProfiler.h"
#include "Engine/Reflection/EnumName.h"
#include "Engine/Renderer/Extension/RenderExtension.h"
#include "Engine/Renderer/Frame/FrameRendererUtil.h"
#include "Engine/Renderer/Frame/Packet/RenderFrameSettings.h"
#include "Engine/Renderer/Pipeline/RenderPassInputSignature.h"
#include "Engine/Renderer/Pipeline/RenderPassKindInfo.h"
#include "Engine/Renderer/Pipeline/RenderPipelineAsset.h"
#include "Engine/Renderer/Scene/GPUMaterialRegistry.h"
#include "Engine/Renderer/Scene/GPUScene.h"

namespace sw
{

    namespace
    {
        /**
         * @brief 뷰 모드를 PSO 디스크립터에 적용합니다. 바꾼 것이 있으면 true 입니다.
         * @details Lit 는 아무것도 하지 않는다. 그것이 패스가 이미 만들어 둔 상태다.
         */
        [[nodiscard]] bool applyViewModeToDesc( RHIPipelineStateDesc& desc, RenderViewMode viewMode )
        {
            bool                      bChanged{ false };
            const RenderViewModeInfo& info = RenderViewModeUtil::getInfo( viewMode );
            if ( info._bWireframe )
            {
                desc._renderState._fillMode = FillMode::Wireframe;
                // 와이어프레임은 뒷면도 보여야 형태를 읽을 수 있다. 컬링을 남기면 뒤쪽 선이 사라져
                // 상자가 열린 것처럼 보인다. 에디터의 와이어프레임은 관례적으로 양면이다.
                desc._renderState._cullMode = CullMode::None;
                bChanged                    = true;
            }
            if ( info._bOverlay )
            {
                // 겹침(ShadedWireframe)은 Lit 으로 그린 같은 삼각형 위에 선을 그린다. 선의 래스터 깊이는 면과 조금씩 달라 LessEqual 만으로는
                // z-fighting 한다 — 카메라 쪽으로 미는 바이어스를 걸고, 선이 면의 깊이를 바꾸지 않게 깊이 쓰기를 끈다.
                static constexpr int32   kOverlayDepthBias            = -4;
                static constexpr float32 kOverlaySlopeScaledDepthBias = -1.0f;
                desc._renderState._depthBias                          = kOverlayDepthBias;
                desc._renderState._slopeScaledDepthBias               = kOverlaySlopeScaledDepthBias;
                desc._renderState._bEnableDepthWrite                  = SW_FALSE;
                bChanged                                              = true;
            }
            if ( info._bAdditiveNoDepth )
            {
                // 겹쳐 그린 수를 세려면 가려진 것도 그려야 한다. 깊이를 쓰지 않고 단색을 더한다(유니티 Overdraw 와 같다).
                // 깊이 테스트는 남긴다. 패스가 깊이를 클리어(1)하고 아무도 쓰지 않으니 모든 면이 통과한다. 테스트를 끄면 백엔드가 깊이 첨부 없는
                // PSO 로 만들어(Vulkan 렌더 패스 호환) 깊이를 바인딩한 패스에서 검증 오류가 난다. 깊이 프리패스가 있는 파이프라인에서는 보이는 면만 센다.
                // 프리멀티플라이(색 One / InvSrcAlpha)에 셰이더가 알파 0을 내면 그것이 더하기다.
                desc._renderState.setAllBlendTargets( BlendTargetState::makePremultipliedAlpha() );
                desc._renderState._bEnableDepthWrite = SW_FALSE;
                desc._renderState._cullMode          = CullMode::None;
                bChanged                             = true;
            }

            // 셰이딩을 바꾸는 모드(Unlit, Normals, Depth, Overdraw)는 조명 항을 셰이더에서 **컴파일 아웃**한다. 런타임 분기가 아니라 퍼뮤테이션이다.
            // define 은 쿠커와 같은 원본(findViewModeDefine)에서 얻는다. 쿠커가 쿠킹하지 않은 define 은 Shipping 에서 PSO 를 만들 수 없다.
            const utf8* pViewModeDefine = RenderViewModeUtil::getInfo( viewMode )._pShaderDefine;
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
            if ( desc._renderState._cullMode == CullMode::Back )
            {
                desc._renderState._cullMode = CullMode::Front;
                return true;
            }
            if ( desc._renderState._cullMode == CullMode::Front )
            {
                desc._renderState._cullMode = CullMode::Back;
                return true;
            }
            return false;
        }

        /** @brief 파이프라인에 없는 내부 슬롯(`pPassDesc` 없음)의 깊이 · 블렌드 · 컬을 표에서 정합니다. 파이프라인 패스는 XML 값이 그대로 들어간다(`applyPassRenderState`). */
        void applyInternalPassRenderState( RHIPipelineStateDesc& desc, const RenderPassKindInfo& info, RenderPassKind passType )
        {
            RenderState& state       = desc._renderState;
            state                    = RenderState{};
            state._bEnableDepthTest  = info.hasFlag( RenderPassTraitFlag::kDepthTest ) ? SW_TRUE : SW_FALSE;
            state._bEnableDepthWrite = ( info.hasFlag( RenderPassTraitFlag::kDepthTest ) && info.hasFlag( RenderPassTraitFlag::kDepthWrite ) ) ? SW_TRUE : SW_FALSE;
            switch ( info._blend )
            {
                case RenderPassBlend::None:
                    break;
                case RenderPassBlend::Straight:
                    state.setAllBlendTargets( BlendTargetState::makeStraightAlpha() );
                    break;
                case RenderPassBlend::Premultiplied:
                    state.setAllBlendTargets( BlendTargetState::makePremultipliedAlpha() );
                    break;
            }
            // 풀스크린 삼각형의 와인딩에는 앞뒤 의미가 없다. 컬링하면 화면이 빈다.
            if ( getRenderPassKindInfo( passType ).shouldDrawSceneMeshes() == false )
                state._cullMode = CullMode::None;
            else
                state._cullMode = info.hasFlag( RenderPassTraitFlag::kCullFront ) ? CullMode::Front : CullMode::Back;
        }

        /**
         * @brief 깊이 테스트에 맞춰 깊이 형식을 정합니다.
         * @details 뎁스를 쓰지 않는 패스(풀스크린)는 렌더 패스에 DSV 를 붙이지 않는데, desc 의 기본값이 D24 라서
         *          PSO 는 "뎁스 있음" 으로 만들어졌다. DX12 는 null DSV 를 PSO 뎁스 포맷이 UNKNOWN 일 때만 허용하므로 그런 패스의 드로우마다 검증 오류가 났다.
         */
        void finishPassRenderState( RHIPipelineStateDesc& desc )
        {
            desc._depthStencilFormat = ( desc._renderState._bEnableDepthTest != SW_FALSE ) ? constant::render::kDepthStencilFormat : RHIFormat::Unknown;
        }

        /** @brief 셋업의 셰이더 미리 데우기 잡입니다(`FramePSOManager::warmShaders`). 워커는 요청 배열을 읽기만 하고, 캐시는 자기 잠금으로 같은 요청을 한 번만 컴파일한다. */
        struct ShaderWarmJob
        {
            const ShaderCompileDesc* _pShader{ nullptr };
            ShaderCache*             _pShaders{ nullptr };

            void run( uint32 start, uint32 end )
            {
                for ( uint32 index = start; index < end; ++index )
                {
                    // 결과는 버린다 — 실패는 뒤의 PSO 생성이 같은 요청으로 다시 받아 경로와 함께 알린다.
                    (void)_pShaders->getOrCompile( _pShader[index] );
                }
            }
        };

        /** @brief 입자 PSO 의 desc 입니다 — 투명 패스 PSO 의 렌더 상태(@p transparentDesc)를 물려받고 셰이더만 입자 셰이더로 바꾼다(`ensureParticlePSO` · 셋업의 셰이더 데우기). */
        RHIPipelineStateDesc makeParticlePSODesc( const RHIPipelineStateDesc& transparentDesc, const string& shaderPath )
        {
            RHIPipelineStateDesc desc = transparentDesc;
            desc._vertexShaderPath    = shaderPath;
            desc._pixelShaderPath     = shaderPath;
            desc._vertexEntryPoint    = string( resolveEntryPoint( string_view{}, ShaderStage::Vertex ) );
            desc._pixelEntryPoint     = string( resolveEntryPoint( string_view{}, ShaderStage::Pixel ) );
            desc._listShaderDefine.clear();
            desc._renderState._cullMode = CullMode::None;
            desc._renderState._fillMode = FillMode::Solid;
            desc._renderState.setAllBlendTargets( BlendTargetState::makePremultipliedAlpha() );
            desc._renderState._bEnableDepthWrite = SW_FALSE;
            return desc;
        }

        /** @brief 확장 프로그램 PSO 의 desc 입니다(진입점은 스테이지 기본값, define 없음 — 쿠커가 그 한 벌만 굽는다). */
        RHIPipelineStateDesc makeExtensionProgramDesc( const ExtensionProgramDesc& program, RHIFormat colorFormat, bool bDepthTest )
        {
            RHIPipelineStateDesc desc{};
            desc._vertexShaderPath   = program._shaderPath;
            desc._pixelShaderPath    = program._shaderPath;
            desc._vertexEntryPoint   = string( resolveEntryPoint( string_view{}, ShaderStage::Vertex ) );
            desc._pixelEntryPoint    = string( resolveEntryPoint( string_view{}, ShaderStage::Pixel ) );
            desc._numRenderTargets   = 1;
            desc._arrRTVFormat[0]    = colorFormat;
            desc._depthStencilFormat = bDepthTest ? constant::render::kDepthStencilFormat : RHIFormat::Unknown;
            RenderState& state       = desc._renderState;
            state._bEnableDepthTest  = bDepthTest ? SW_TRUE : SW_FALSE;
            state._bEnableDepthWrite = ( bDepthTest && program._bDepthWrite ) ? SW_TRUE : SW_FALSE;
            state._cullMode          = program._bCullBack ? CullMode::Back : CullMode::None;
            if ( program._bBlend )
                state.setAllBlendTargets( BlendTargetState::makePremultipliedAlpha() );
            return desc;
        }

        /** @brief 확장 프로그램 PSO 의 저장소 키입니다 — 셰이더 · 렌더 상태 · 타깃 포맷이 같으면 한 PSO 를 나눈다(`getOrCreateExtensionProgramPSO` · `warmExtensionPrograms`). 상태를 칸마다 글자로 적지 않아 칸이 늘어도 따라온다. */
        hashed_string makeExtensionProgramKey( const ExtensionProgramDesc& program, RHIFormat colorFormat, bool bDepthTest )
        {
            const RHIPipelineStateDesc desc = makeExtensionProgramDesc( program, colorFormat, bDepthTest );
            string                     key  = program._shaderPath;
            key.append( "|" );
            key.append( to_string( desc._renderState.computeHash() ) );
            key.append( "|" );
            key.append( to_string( static_cast<uint32>( colorFormat ) ) );
            return hashed_string{ string_view{ key } };
        }

        /** @brief 2D 빛 쿠키 아틀라스 칸에 옮겨 그리는 PSO 의 desc 입니다 — 전체 화면 블릿 셰이더를 @p atlasFormat 으로(`getOrCreateCookieAtlasPSO` · 셋업의 셰이더 데우기). */
        RHIPipelineStateDesc makeCookieAtlasPSODesc( const string& shaderPath, RHIFormat atlasFormat )
        {
            RHIPipelineStateDesc desc{};
            desc._vertexShaderPath               = shaderPath;
            desc._pixelShaderPath                = shaderPath;
            desc._vertexEntryPoint               = string( resolveEntryPoint( string_view{}, ShaderStage::Vertex ) );
            desc._pixelEntryPoint                = string( resolveEntryPoint( string_view{}, ShaderStage::Pixel ) );
            desc._numRenderTargets               = 1;
            desc._arrRTVFormat[0]                = atlasFormat;
            desc._depthStencilFormat             = RHIFormat::Unknown;
            desc._renderState._bEnableDepthWrite = SW_FALSE;
            return desc;
        }

        /** @brief UV 공간 층 굽기 PSO 의 desc 입니다 — 층 굽기 셰이더, 타깃 둘(알베도 · 표면 덮어쓰기)을 같은 포맷으로(`getOrCreateLayerBakePSO` · 셋업의 셰이더 데우기). */
        RHIPipelineStateDesc makeLayerBakePSODesc( const string& shaderPath, RHIFormat targetFormat )
        {
            RHIPipelineStateDesc desc{};
            desc._vertexShaderPath               = shaderPath;
            desc._pixelShaderPath                = shaderPath;
            desc._vertexEntryPoint               = string( resolveEntryPoint( string_view{}, ShaderStage::Vertex ) );
            desc._pixelEntryPoint                = string( resolveEntryPoint( string_view{}, ShaderStage::Pixel ) );
            desc._numRenderTargets               = 2;
            desc._arrRTVFormat[0]                = targetFormat;
            desc._arrRTVFormat[1]                = targetFormat;
            desc._depthStencilFormat             = RHIFormat::Unknown;
            desc._renderState._bEnableDepthWrite = SW_FALSE;
            return desc;
        }

        /** @brief 디퍼드 조명 타일 종류 PSO 의 저장소 키입니다 — 머티리얼 변형 저장소의 키 공간을 빌린다(쿠키 아틀라스 PSO 와 같은 길 — 저장소가 소유하고 디바이스를 바꾸면 셋업이 다시 만든다). */
        uint64 makeDeferredTilePSOKey( uint32 tileClass )
        {
            return RenderPSOCache::materialPSOKey( 0, HashUtil::combine( static_cast<uint64>( FourCCUtil::make( "DTLC" ) ), static_cast<uint64>( tileClass ) ), RenderViewMode::Lit, false,
                                                   RHIMeshVertexFormat::Packed );
        }

        /** @brief PSO 를 동기로 만듭니다(셰이더 컴파일 · 드라이버 생성을 렌더 스레드가 기다리는 시간을 `RT.PSO.create` 로 잰다). */
        [[nodiscard]] RHIPipelineStateHandle createPipelineStateTimed( IRHIDevice& device, const RHIPipelineStateDesc& desc )
        {
            SW_PROFILE_SCOPE( "RT.PSO.create" );
            return device.getResourceFactory()->createPipelineState( desc );
        }

        /**
         * @brief 패스 셰이더가 실제로 읽는 엔진 역할(`g_<역할>Index`, 리플렉션의 `Used`)이 그 패스 형식의 입력 계약 안인지 봅니다. 밖이면 오류이고 false 입니다.
         * @details 계약에 없는 역할은 그래프가 걸지 않아 무효 인덱스다 — 셰이더의 판정이 조용히 상수가 된다(SSR 이 계약 밖 GBufferAlbedo 를 읽던 545d9cf099).
         *          엔진 PassCB 는 모든 역할의 인덱스 칸을 가지므로 칸이 있는 것이 아니라 읽는 것을 본다. 쓰임을 모르는 리플렉션(SPIR-V · GLSL · 쿠킹 매니페스트)은 건너뛴다 —
         *          DX 백엔드의 개발 실행 · 시나리오가 잡는다(언리얼 RDG 가 패스 파라미터 밖 자원 접근을 검증 단계에서 잡는 자리).
         */
        bool validatePassInputContract( RenderPassKind passType, const ShaderBindingLayout& layout )
        {
            const RenderPassInputSignature* const pContract = findRenderPassInputSignature( passType );
            if ( pContract == nullptr )
                return true;
            bool bInside = true;
            for ( const ShaderBindingSlot& slot : layout.getSlots() )
            {
                for ( const ShaderVariableInfo& member : slot._listCbMember )
                {
                    const string_view name       = member._name;
                    const bool        bRoleIndex = member._usage == ShaderVariableUsage::Used && name.size() > 7 && name.substr( 0, 2 ) == "g_" && name.substr( name.size() - 5 ) == "Index";
                    if ( bRoleIndex == false )
                        continue;
                    RenderPassInputRole role  = RenderPassInputRole::Invalid;
                    const bool          bRole = tryParseRenderPassInputRole( name.substr( 2, name.size() - 7 ), role );
                    if ( bRole == false || pContract->hasRole( role ) || getRenderPassKindInfo( passType )._executionBoundRole == role )
                        continue;
                    SW_LOG_ERROR_ID( "FramePSOManager.PassReadsRoleOutsideContract",
                                     "Pass %# reads engine role '%#' (%#) but its input contract does not have it - the graph binds no texture there and the shader reads an invalid index; add the role to the contract (RenderPassKindInfo) and the pipeline inputs",
                                     getEnumName( passType ), getRenderPassInputRoleName( role ), member._name.c_str() );
                    bInside = false;
                }
            }
            return bInside;
        }
    } // namespace

    void FramePSOManager::applyPassRenderState( RHIPipelineStateDesc& desc, RenderPassKind passType, const RenderGraphPassDesc* pPassDesc )
    {
        if ( pPassDesc != nullptr )
            desc._renderState = pPassDesc->_resolvedRenderState; // 조합은 읽을 때 검사했다(`RenderPipelineAsset::validate`)
        else
            applyInternalPassRenderState( desc, getRenderPassKindInfo( passType ), passType );
        finishPassRenderState( desc );
    }

    FramePSOManager::FramePSOManager( IRHIDevice* const& pDevice, const RenderPipelineAsset& pipeline, const GPUScene& gpuScene, const RenderFrameSettings& frameSettings )
        : _pDevice{ pDevice }
        , _pipeline{ pipeline }
        , _gpuScene{ gpuScene }
        , _frameSettings{ frameSettings }
        , _cache{}
        , _pCache{ &_cache }
    {
    }

    FramePSOManager::~FramePSOManager() = default;

    void FramePSOManager::createPassPSOs()
    {
        if ( _pDevice == nullptr )
            return;
        // 그래픽스 · 컴퓨트 패스는 desc 를 먼저 모으고(아래 1), 셰이더를 워커가 나눠 데운 뒤(2), PSO 를 이 스레드가 차례로 만든다(3).
        vector<PassPSOJob>    listJob;
        vector<ComputePSOJob> listComputeJob;
        // 엔진 PSO 는 패스 종류의 표(RenderPassKindInfo)를 enum 순서로 훑어 만든다. 셰이더 경로는 파이프라인 XML 패스 설정이 먼저이고
        // 표의 EngineDefaultAssets 경로는 마지막 폴백일 뿐이다. 셰이더 쿠커가 같은 표를 훑는다.
        // 저장소에 이미 있는 패스는 만들지 않는다 — 미리보기 씬 렌더러는 주 렌더러의 저장소를 함께 쓰고(`shareCache`), 주 렌더러가 같은 파이프라인으로 먼저 만들었다.
        const EngineDefaultAssets& engineDefaultAssets = engine::getSubsystem<EngineDefaultAssets>();
        const RHICapabilities      caps                = _pDevice->getCapabilities();
        for ( uint32 typeIndex = 0; typeIndex < kRenderPassKindCount; ++typeIndex )
        {
            const RenderPassKind      passType = static_cast<RenderPassKind>( typeIndex );
            const RenderPassKindInfo& info     = getRenderPassKindInfo( passType );
            const bool                bMissing = info._pDefaultShader != nullptr && _pCache->findEnginePSO( passType ) == 0;
            if ( bMissing == false )
                continue;
            // 셰이더가 파이프라인의 퍼뮤테이션을 요구하는 패스는 그 파이프라인에 있을 때만 만든다(없으면 실행도 없다).
            if ( info.hasFlag( RenderPassTraitFlag::kRequiresPassDesc ) && findPassDescByType( passType ) == nullptr )
                continue;
            // 프레임 패스(컬링 · 그리드 · 스키닝 등)는 파이프라인이 `_listFramePass` 에 선언할 때만 돈다 — 선언이 없으면 PSO 도 만들지 않는다.
            // 실행 쪽은 PSO 가 아니라 선언으로 가른다(`RenderPipelineAsset::hasFramePass`) — 미리보기 렌더러는 주 렌더러의 저장소를 함께 써 PSO 가 있어도 선언이 없을 수 있다.
            if ( isFramePassType( passType ) && _pipeline.hasFramePass( passType ) == false )
                continue;
            // 진단 패스는 요청한 실행에서만 만든다. 네이티브 bindless 가 필요한 패스는 에뮬 백엔드에서 쓰지 않는다(능력 질의로 가린 것이지 실패가 아니다).
            if ( info.hasFlag( RenderPassTraitFlag::kDiagnostic ) && isDiagnosticPassRequested( passType ) == false )
                continue;
            if ( info.hasFlag( RenderPassTraitFlag::kRequiresNativeBindless ) && _pDevice->supportsNativeBindlessSampling() == false )
                continue;
            // HDR 화면 출력 패스는 모니터가 HDR 을 받을 때만 만든다(능력 표가 정한 길 — HDR 이 없는 기계는 PSO 도 패스도 없다). 실행 중 HDR 을 켜도 셋업의 이 PSO 를 쓴다.
            if ( isDisplayOutputPassType( passType ) && caps.hasHDRDisplayOutput() == false )
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
                    SW_LOG_ERROR_ID( "FramePSOManager.EnginePSOPassCould", "Engine PSO for pass %# could not be created (shader '%#') - the pass draws nothing", passType,
                                     ( engineDefaultAssets.*info._pDefaultShader ).c_str() );
                continue;
            }
            // 컬링 · 정렬은 간접 인자 버퍼 능력(_bGPUCulling)을, 애니메이션 · 모프는 구조버퍼 UAV(_bCompute)만 요구한다.
            // DX11 은 한 버퍼에 STRUCTURED 와 DRAWINDIRECT_ARGS 를 같이 못 걸어 _bGPUCulling 이 0 이지만 _bCompute 는 1 이다.
            const bool bCapable = info.hasFlag( RenderPassTraitFlag::kRequiresGPUCulling ) ? caps._bGPUCulling != SW_FALSE : caps._bCompute != SW_FALSE;
            if ( bCapable == false )
                continue; // 능력이 없는 백엔드는 이 디스패치를 쓰지 않는다(능력 질의로 가린 것이지 실패가 아니다)
            // 셰이더 경로와 define 은 쿠커와 같은 함수가 정한다(셰이더 assert 변형이 define 으로 온다).
            RenderPassShaderSelection computeShader = selectRenderPassShader( passType, nullptr, nullptr, engineDefaultAssets );
            ComputePSOJob&            computeJob    = listComputeJob.emplace_back();
            computeJob._shaderPath                  = std::move( computeShader._shaderPath );
            computeJob._listDefine                  = std::move( computeShader._listDefine );
            computeJob._passType                    = passType;
        }

        // 파이프라인 컴퓨트 패스는 셰이더가 패스마다 달라 표가 아니라 그래프를 돈다. 컴퓨트 능력이 없는 백엔드는 이 패스를 돌리지 못한다.
        // 이미 있는 패스는 만들지 않는다 — 다시 부르는 자리(백버퍼 형식 변경 · 셰이더 핫 리로드)에서 덮어쓰면 옛 PSO 가 파괴되지 않고 남는다.
        for ( const RenderGraphPassDesc& pass : _pipeline.getGraphPass() )
        {
            RHIPipelineStateHandle existingCompute{ 0 };
            if ( _pCache->findExtensionProgramPSO( hashed_string( pass._name.c_str() ), existingCompute ) )
                continue;
            // 일반 풀스크린 패스의 컴퓨트 구현 — 네이티브 bindless 에서만 만든다(에뮬 백엔드는 픽셀 구현이 돈다 — 그 PSO 는 위의 표 단계가 늘 만든다). 키는 패스 이름이다.
            if ( pass._implementation == RenderPassExecutionMode::Compute && _pDevice->supportsNativeBindlessSampling() )
            {
                RenderPassShaderSelection computeShader = selectRenderPassComputeShader( pass, engineDefaultAssets );
                ComputePSOJob&            computeJob    = listComputeJob.emplace_back();
                computeJob._shaderPath                  = std::move( computeShader._shaderPath );
                computeJob._listDefine                  = std::move( computeShader._listDefine );
                computeJob._pipelinePassName            = hashed_string( pass._name.c_str() );
                computeJob._passType                    = pass._type;
                continue;
            }
            if ( pass._type != RenderPassKind::Compute )
                continue;
            RenderPassShaderSelection computeShader = selectRenderPassShader( RenderPassKind::Compute, &pass, &pass, engineDefaultAssets );
            ComputePSOJob&            computeJob    = listComputeJob.emplace_back();
            computeJob._shaderPath                  = std::move( computeShader._shaderPath );
            computeJob._entryPoint                  = pass._computeEntryPoint;
            computeJob._listDefine                  = std::move( computeShader._listDefine );
            computeJob._pipelinePassName            = hashed_string( pass._name.c_str() );
            computeJob._passType                    = RenderPassKind::Compute;
        }

        // 출력 패스(Present · Canvas)의 포맷별 변종도 PSO 등록 단계에서 만든다. 기록 중에는 PSO 를 만들 수 없다(findOutputPSO 주석 참고).
        collectOutputPSOJobs( listJob );

        // 디퍼드 조명 타일 종류 변형 — 조명 패스 서술 그대로에 종류 define 과 타일 정점 진입점(deferred_lighting.hlsl VSTile). 분류 컴퓨트와 같은 조건(네이티브 bindless · 간접 인자)에서만 만든다.
        struct DeferredTilePSOJob
        {
            uint64               _key{ 0 };
            RHIPipelineStateDesc _desc{};
        };
        vector<DeferredTilePSOJob> listTileJob;
        const RenderGraphPassDesc* pLightingDesc = findPassDescByType( RenderPassKind::Lighting );
        const bool                 bTileCapable  = pLightingDesc != nullptr && _pDevice->supportsNativeBindlessSampling() && caps._bGPUCulling != SW_FALSE;
        for ( uint32 tileClass = 0; bTileCapable && tileClass < kDeferredTileClassCount; ++tileClass )
        {
            DeferredTilePSOJob tileJob;
            tileJob._key = makeDeferredTilePSOKey( tileClass );
            if ( _pCache->hasMaterialPSO( tileJob._key ) || makePassPSODesc( RenderPassKind::Lighting, pLightingDesc, nullptr, tileJob._desc ) == false )
                continue;
            tileJob._desc._listShaderDefine = selectDeferredTileLightingShader( *pLightingDesc, tileClass, engineDefaultAssets )._listDefine;
            tileJob._desc._vertexEntryPoint = kDeferredTileVertexEntryPoint;
            listTileJob.push_back( std::move( tileJob ) );
        }

        // 2) 셰이더 데우기 — 패스 PSO 셋과, 처음 그리는 프레임에 만드는 PSO(입자 · 쿠키 아틀라스)의 셰이더를 한 번에 나눈다.
        //    입자 PSO 는 투명 패스의 desc 를 물려받으므로 이번에 만들 투명 패스 desc 가 있을 때만 넣는다(이미 있으면 처음 셋업이 데웠다).
        vector<ShaderCompileDesc> listShader;
        for ( const DeferredTilePSOJob& tileJob : listTileJob )
        {
            appendShaderWarm( tileJob._desc, listShader );
        }
        for ( const PassPSOJob& job : listJob )
        {
            appendShaderWarm( job._desc, listShader );
            if ( job._passType == RenderPassKind::Transparent && job._outputFormat == RHIFormat::Unknown )
                appendShaderWarm( makeParticlePSODesc( job._desc, engineDefaultAssets._shaderParticle.get() ), listShader );
        }
        if ( listJob.empty() == false )
        {
            // 아틀라스 포맷은 셰이더 바이트코드에 들지 않는다(RT 수만 든다 — `RHIShaderRequest::resolveGraphics`) — 아틀라스와 같은 RGBA8 로 데운다.
            appendShaderWarm( makeCookieAtlasPSODesc( engineDefaultAssets._shaderFullscreenBlit.get(), RHIFormat::R8G8B8A8_UNORM ), listShader );
            appendShaderWarm( makeLayerBakePSODesc( engineDefaultAssets._shaderLayerBake.get(), constant::render::kOffscreenColorFormat ), listShader );
        }
        const ShaderTargetFormat targetFormat = _pDevice->getShaderTargetFormat();
        for ( const ComputePSOJob& computeJob : listComputeJob )
        {
            listShader.push_back( RHIShaderRequest::resolveCompute( computeJob._shaderPath, string_view( computeJob._entryPoint ), computeJob._listDefine, targetFormat ) );
        }
        warmShaders( listShader );

        // 3) PSO 생성 — 이 스레드가 차례로(드라이버 생성은 직렬이고 GL 은 컨텍스트가 이 스레드에 묶인다).
        for ( const ComputePSOJob& computeJob : listComputeJob )
        {
            const RHIPipelineStateHandle pso = _pDevice->getResourceFactory()->createComputePipelineState( computeJob._shaderPath, string_view( computeJob._entryPoint ), computeJob._listDefine );
            // 다른 패스의 PSO 로 대신하지 않는다 — 이 패스는 그리지 않고 여기서 이유를 한 번 남긴다.
            if ( pso == 0 )
            {
                SW_LOG_ERROR_ID( "FramePSOManager.EnginePSOPassCould", "Engine PSO for pass %# could not be created (shader '%#') - the pass draws nothing", computeJob._passType, computeJob._shaderPath.c_str() );
                continue;
            }
            // 어느 셰이더로 만들었는지 적는다 — 셰이더 핫 리로드가 그 셰이더의 PSO 만 놓는다(`RenderPSOCache::releasePSOsUsingShader`).
            RHIPipelineStateDesc computeDesc{};
            computeDesc._computeShaderPath = computeJob._shaderPath;
            computeDesc._computeEntryPoint = computeJob._entryPoint;
            computeDesc._listShaderDefine  = computeJob._listDefine;
            _pCache->registerDesc( pso, computeDesc );
            if ( computeJob._pipelinePassName.empty() == false )
                _pCache->setExtensionProgramPSO( _pDevice, computeJob._pipelinePassName, pso ); // 패스 이름이 키다 — 확장 프로그램과 같은 저장소(같이 해제한다)
            else
                _pCache->setEnginePSO( _pDevice, computeJob._passType, pso );
        }
        for ( const PassPSOJob& job : listJob )
        {
            const RHIPipelineStateHandle pso = createPipelineStateTimed( *_pDevice, job._desc );
            registerLayout( pso, job._desc );
            const ShaderBindingLayout* const pLayout = pso != 0 ? findLayout( pso ) : nullptr;
            if ( pLayout != nullptr )
                (void)validatePassInputContract( job._passType, *pLayout ); // 오류는 남겼다 — 그래도 그린다(다른 패스로 대신하지 않는다)
            if ( job._outputFormat != RHIFormat::Unknown )
            {
                // 주 출력에 그리는 PSO 다 — 없으면 화면에 아무것도 낼 수 없고, 다른 PSO · 복사로 대신하지 않으므로 여기서 멈춘다.
                if ( pso == 0 )
                    SW_FATAL( "Output pass %# could not create its PSO for target format %# - nothing can be presented", job._passType, static_cast<uint32>( job._outputFormat ) );
                _pCache->setOutputPSO( _pDevice, job._passType, job._outputFormat, pso );
                continue;
            }
            // 다른 패스의 PSO 로 대신하지 않는다 — 이 패스는 그리지 않고(drawFullscreen 은 PSO 없이 그리지 않는다) 여기서 이유를 한 번 남긴다.
            if ( pso == 0 )
            {
                SW_LOG_ERROR_ID( "FramePSOManager.EnginePSOPassCould", "Engine PSO for pass %# could not be created (shader '%#') - the pass draws nothing", job._passType, job._desc._vertexShaderPath.c_str() );
                continue;
            }
            _pCache->setEnginePSO( _pDevice, job._passType, pso );
        }
        for ( const DeferredTilePSOJob& tileJob : listTileJob )
        {
            // 못 만들면 그 이유를 남기고 조명은 풀스크린 한 장(모든 엽 — 같은 그림)으로 그린다(`FramePassExecutor::executeTiledLighting` 이 종류 하나라도 없으면 타일을 쓰지 않는다).
            const RHIPipelineStateHandle pso = createPipelineStateTimed( *_pDevice, tileJob._desc );
            if ( pso == 0 )
                SW_LOG_ERROR_ID( "FramePSOManager.DeferredTilePSOCould", "Deferred lighting tile PSO could not be created (shader '%#') - lighting draws one fullscreen triangle with every lobe",
                                 tileJob._desc._vertexShaderPath.c_str() );
            else
                registerLayout( pso, tileJob._desc );
            _pCache->setMaterialPSO( tileJob._key, RenderPSOCache::MaterialPSOEntry::ready( pso, true ) );
        }
        // 폴백 원소는 PSO 를 모두 등록한 뒤에 만든다. 필요한 stride 를 레이아웃에서 읽어야 하고, 기록 중에는 만들 수 없다.
        ensureMaterialFallbackBuffers();
    }

    RHIPipelineStateHandle FramePSOManager::findDeferredTilePSO( uint32 tileClass ) const
    {
        return _pCache->findMaterialPSO( makeDeferredTilePSOKey( tileClass ) );
    }

    void FramePSOManager::release()
    {
        // PSO 는 저장소가 순서대로 놓는다: 변형(소유한 것만) → 패스 → Present → 레이아웃 표.
        // 빌려 쓰는 저장소(미리보기 씬 렌더러)는 소유자인 주 렌더러가 놓는다 — 다른 미리보기 창과 주 시점이 아직 그 PSO 로 그린다.
        if ( hasOwnCache() )
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
        // 빌려 쓰는 저장소(미리보기 씬 렌더러)는 소유자인 주 렌더러가 이미 놓았다 — 빌린 쪽은 자기 파이프라인에서 빠진 것만 채운다(`createPassPSOs`).
        if ( hasOwnCache() )
            (void)_pCache->releasePSOsUsingShader( _pDevice, shaderPath ); // 놓은 수는 쓰지 않는다 — 빈 자리는 createPassPSOs · 지연 생성이 채운다
    }

    RHIPipelineStateHandle FramePSOManager::findPipelineComputePSO( const hashed_string& passName ) const
    {
        RHIPipelineStateHandle pso{ 0 };
        return _pCache->findExtensionProgramPSO( passName, pso ) ? pso : RHIPipelineStateHandle{ 0 };
    }

    RHIPipelineStateHandle FramePSOManager::getEnginePSO( RenderPassKind passType ) const
    {
        // 프레임 패스는 이 파이프라인이 선언했을 때만 있다 — 함께 쓰는 저장소(미리보기 렌더러)에 주 렌더러가 만든 PSO 가 있어도 0 이다.
        // 그래서 "PSO 가 있으면 준비한다" 고 묻는 모든 곳(간접 인자를 컬링에 맡김 · GPU 라이트 그리드 · 볼륨 안개 · 프로브 · 입자 · 깊이 히스토리)이 선언을 따른다.
        if ( isFramePassType( passType ) && _pipeline.hasFramePass( passType ) == false )
            return 0;
        return _pCache->findEnginePSO( passType );
    }

    RHIPipelineStateHandle FramePSOManager::findOutputPSO( RenderPassKind passType, RHIFormat targetFormat ) const
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

    const RHIStructuredBuffer* FramePSOManager::findMaterialFallback( const ShaderBindingLayout* pLayout ) const
    {
        if ( pLayout == nullptr )
            return nullptr;
        const auto it = _mapLayoutMaterialFallback.find( pLayout->getIdentity() );
        return ( it != _mapLayoutMaterialFallback.end() ) ? &_listMaterialFallback[it->second]._buffer : nullptr;
    }

    void FramePSOManager::registerLayout( RHIPipelineStateHandle pso, const RHIPipelineStateDesc& desc )
    {
        if ( pso == 0 || _pDevice == nullptr )
            return;
        // gv_rhiBackend(전역) 대신 이 관리자가 실제로 물려 있는 디바이스의 백엔드를 넘긴다.
        // 한 프로세스에 여러 IRHIDevice 가 동시에 있으면 전역값이 어긋날 수 있다.
        _pCache->registerLayout( pso, desc, _pDevice->getShaderTargetFormat() );
    }

    const RenderGraphPassDesc* FramePSOManager::findPassDescByType( RenderPassKind passType ) const
    {
        // 값만 비교한다 — 표기는 읽을 때 enum 으로 검사했다.
        for ( const RenderGraphPassDesc& pass : _pipeline.getGraphPass() )
        {
            if ( pass._type == passType )
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
            if ( pLayout == nullptr || _mapLayoutMaterialFallback.find( pLayout->getIdentity() ) != _mapLayoutMaterialFallback.end() )
                continue;
            const ShaderBindingSlot* pSlot = pLayout->find( passConstantNames()._swMaterials );
            if ( pSlot == nullptr )
                continue; // 이 셰이더는 머티리얼 버퍼를 선언하지 않는다(풀스크린 등). 걸 것이 없다.
            if ( pSlot->_elementStride == 0 )
            {
                // 선언은 있는데 원소 레이아웃이 없다 = 리플렉션 공백. 그대로 두면 머티리얼 없는 배치가 슬롯을 비운 채
                // 그리게 되고(Vulkan 은 partially-bound 슬롯을 읽으면 정의되지 않는다) 원인을 찾기 어렵다.
                SW_LOG_ERROR_ID( "FramePSOManager.MaterialBufferElementStride", "The material buffer element stride of PSO %# is 0 - reflection did not provide the g_SwMaterials element layout.", pso.getPacked() );
                continue;
            }

            vector<uint8> bytes         = MaterialPacking::makeFallbackElement( pSlot->_listElementMember, pSlot->_elementStride );
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
                const uint32        stride = pSlot->_elementStride;
                RHIStructuredBuffer buffer{};
                if ( buffer.ensureCapacity( _pDevice, stride, 1, RHIBufferUsage::Structured | RHIBufferUsage::ShaderResource, true, false, bytes.data() ) == false )
                {
                    SW_LOG_ERROR_ID( "FramePSOManager.FailedCreateMaterialFallback", "Failed to create the material fallback buffer (stride %#).", stride );
                    continue;
                }
                buffer.upload( _pDevice, bytes.data(), stride );
                MaterialFallback& fallback = _listMaterialFallback.emplace_back();
                fallback._bytes            = std::move( bytes );
                fallback._buffer           = buffer;
            }
            _mapLayoutMaterialFallback.insert_or_assign( pLayout->getIdentity(), fallbackIndex );
        }
    }

    void FramePSOManager::appendShaderWarm( const RHIPipelineStateDesc& desc, vector<ShaderCompileDesc>& inoutListShader ) const
    {
        // 백엔드 팩토리와 같은 해석이다(`RHIShaderRequest::resolveGraphics`) — 캐시 키가 같아 뒤의 생성이 적중한다.
        RHIGraphicsShaderRequest request = RHIShaderRequest::resolveGraphics( desc, _pDevice->getShaderTargetFormat() );
        inoutListShader.push_back( std::move( request._vertex ) );
        if ( request._bHasPixelShader != SW_FALSE )
            inoutListShader.push_back( std::move( request._pixel ) );
        if ( request._bHasTessellation != SW_FALSE )
        {
            inoutListShader.push_back( std::move( request._hull ) );
            inoutListShader.push_back( std::move( request._domain ) );
        }
    }

    void FramePSOManager::warmShaders( const vector<ShaderCompileDesc>& listShader ) const
    {
        // 셰이더 읽기 · 컴파일(DXC · FXC, 병렬 쿠킹이 이미 워커에서 돌린다)은 디바이스를 쓰지 않아 모든 백엔드에서 워커로 나눈다. PSO 생성은 나누지 않는다 —
        // 드라이버 생성이 직렬이라 병렬 이득이 9 % 로 기각됐고(`MaterialPSOBuilder.h`), GL 은 컨텍스트가 이 스레드에 묶여 워커가 만들 수 없다
        // (언리얼도 셰이더 컴파일은 ShaderCompileWorker 로 나누고 PSO 는 RHI 스레드가 만든다).
        if ( listShader.empty() || engine::areEngineServicesBound() == false )
            return;
        SW_PROFILE_SCOPE( "RT.PSO.prewarmShaders" );
        ShaderWarmJob warmJob{};
        warmJob._pShader  = listShader.data();
        warmJob._pShaders = &engine::getSubsystem<ShaderCache>();
        engine::runParallelByCost( static_cast<uint32>( listShader.size() ), SW_DELEGATE_METHOD( ParallelBlockDelegate, &ShaderWarmJob::run, &warmJob ) );
    }

    void FramePSOManager::collectOutputPSOJobs( vector<PassPSOJob>& inoutListJob )
    {
        // 주 출력에 그리는 패스(Present · Canvas · Present 를 대신하는 FSR 의 RCAS)의 대상은 백버퍼(디바이스가 실제 채택한 포맷), 오프스크린 렌더 타깃(에디터 씬 뷰 · 게임 뷰 등,
        // 계약값 kOffscreenColorFormat), 스크린샷 캡처 텍스처(계약값 kBackBufferFormat)다. 모두 **셋업에서** 만들어 둔다.
        // 출력 패스는 표의 kOutputPass 가 정한다.
        // 화면 회전 출력(kSurfaceRotation)은 스왑체인 이미지에만 그려 백버퍼 포맷 하나이고, 서피스를 돌릴 수 있는 장치에서만 만든다(데스크톱은 없다).
        // HDR 을 받는 모니터면 장면 출력 RT(`FrameDisplayEncode` — Present · RCAS 가 그린다)의 포맷도 더한다. 실행 중 HDR 을 켜도 셋업의 이 PSO 를 쓴다. 받지 않으면 Unknown(건너뛴다).
        const RHICapabilities caps        = _pDevice->getCapabilities();
        const bool            bDisplayHDR = caps.hasHDRDisplayOutput();
        // 첫 칸은 화면 UI 대상 포맷(SDR 이면 백버퍼 포맷)이다 — HDR 이면 백버퍼(R10G10B10A2 · R16F)에 그리는 출력 패스는 인코딩 하나뿐이라 그 포맷의 변형을 만들지 않는다.
        const RHIFormat arrTargetFormat[]   = { _pDevice->getUITargetFormat(), constant::render::kOffscreenColorFormat, constant::render::kBackBufferFormat,
                                              bDisplayHDR ? RHIFormat::R16G16B16A16_FLOAT : RHIFormat::Unknown };
        const bool      bSurfacePreRotation = caps._bSurfacePreRotation != SW_FALSE;
        for ( uint32 typeIndex = 0; typeIndex < kRenderPassKindCount; ++typeIndex )
        {
            const RenderPassKind      passType = static_cast<RenderPassKind>( typeIndex );
            const RenderPassKindInfo& info     = getRenderPassKindInfo( passType );
            if ( info.hasFlag( RenderPassTraitFlag::kOutputPass ) == false )
                continue;
            const bool bSurfaceRotation = info.hasFlag( RenderPassTraitFlag::kSurfaceRotation );
            if ( bSurfaceRotation && bSurfacePreRotation == false )
                continue;
            // 화면 회전 출력은 스왑체인 이미지(백버퍼 포맷) 하나에만 그린다.
            const RHIFormat arrRotationFormat[] = { _pDevice->getBackBufferFormat() };
            const uint32    formatCount         = bSurfaceRotation ? 1u : static_cast<uint32>( SW_COUNT_OF( arrTargetFormat ) );
            for ( uint32 formatIndex = 0; formatIndex < formatCount; ++formatIndex )
            {
                const RHIFormat        format = bSurfaceRotation ? arrRotationFormat[formatIndex] : arrTargetFormat[formatIndex];
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

    RHIPipelineStateHandle FramePSOManager::getOrCreateCookieAtlasPSO( RHIFormat atlasFormat )
    {
        if ( _pDevice == nullptr )
            return 0;
        // 칸에 옮겨 그리는 PSO — 전체 화면 블릿 셰이더(쿠킹되는 Present 기본 셰이더)를 아틀라스 포맷으로. 머티리얼 변형 저장소의 키 공간을 빌려 저장소가 소유한다
        // (입자 PSO 와 같은 방식 — 디바이스를 바꾸면 저장소가 비고 여기서 다시 만든다).
        const string& shaderPath = engine::getSubsystem<EngineDefaultAssets>()._shaderFullscreenBlit.get();
        const uint64  psoKey     = RenderPSOCache::materialPSOKey( 0, HashUtil::combine( HashUtil::computeStringHash64( shaderPath, false ), FourCCUtil::make( "CKAT" ) ), RenderViewMode::Lit, false,
                                                                   RHIMeshVertexFormat::Packed );
        if ( _pCache->hasMaterialPSO( psoKey ) == false )
        {
            // 셰이더는 셋업이 데웠다(`createPassPSOS`) — 여기는 드라이버 생성만 남는다.
            const RHIPipelineStateDesc   desc   = makeCookieAtlasPSODesc( shaderPath, atlasFormat );
            const RHIPipelineStateHandle handle = _pDevice->getResourceFactory()->createPipelineState( desc );
            if ( handle == 0 )
                SW_LOG_ERROR_ID( "FramePSOManager.DLightCookieAtlas", "The 2D light cookie atlas PSO could not be created from '%#' - cookie lights draw without cookies", shaderPath );
            else
                registerLayout( handle, desc );
            _pCache->setMaterialPSO( psoKey, RenderPSOCache::MaterialPSOEntry::ready( handle, true ) );
        }
        return _pCache->findMaterialPSO( psoKey );
    }

    RHIPipelineStateHandle FramePSOManager::getOrCreateLayerBakePSO( RHIFormat targetFormat )
    {
        if ( _pDevice == nullptr )
            return 0;
        // 쿠키 아틀라스 PSO 와 같은 길 — 머티리얼 변형 저장소의 키 공간을 빌려 저장소가 소유한다(디바이스를 바꾸면 저장소가 비고 여기서 다시 만든다).
        const string& shaderPath = engine::getSubsystem<EngineDefaultAssets>()._shaderLayerBake.get();
        const uint64  psoKey     = RenderPSOCache::materialPSOKey( 0, HashUtil::combine( HashUtil::computeStringHash64( shaderPath, false ), FourCCUtil::make( "LBAK" ) + static_cast<uint32>( targetFormat ) ),
                                                                   RenderViewMode::Lit, false, RHIMeshVertexFormat::Packed );
        if ( _pCache->hasMaterialPSO( psoKey ) == false )
        {
            const RHIPipelineStateDesc   desc   = makeLayerBakePSODesc( shaderPath, targetFormat );
            const RHIPipelineStateHandle handle = _pDevice->getResourceFactory()->createPipelineState( desc );
            if ( handle == 0 )
                SW_LOG_ERROR_ID( "FramePSOManager.LayerBakePSO", "The UV layer bake PSO could not be created from '%#' - skin layers are not baked", shaderPath );
            else
                registerLayout( handle, desc );
            _pCache->setMaterialPSO( psoKey, RenderPSOCache::MaterialPSOEntry::ready( handle, true ) );
        }
        return _pCache->findMaterialPSO( psoKey );
    }

    RHIPipelineStateHandle FramePSOManager::createPSOForPassType( RenderPassKind passType, const RHIFormat* pRTVFormatOverride )
    {
        return createPSOForPass( passType, findPassDescByType( passType ), pRTVFormatOverride );
    }

    void FramePSOManager::warmExtensionPrograms( const vector<ExtensionProgramRequest>& listRequest ) const
    {
        if ( _pDevice == nullptr )
            return;
        vector<ShaderCompileDesc> listShader;
        for ( const ExtensionProgramRequest& request : listRequest )
        {
            RHIPipelineStateHandle existing{ 0 };
            const bool             bNoProgram = request._pProgram == nullptr || request._pProgram->_shaderPath.empty();
            if ( bNoProgram || _pCache->findExtensionProgramPSO( makeExtensionProgramKey( *request._pProgram, request._colorFormat, request._bDepthTest ), existing ) )
                continue;
            appendShaderWarm( makeExtensionProgramDesc( *request._pProgram, request._colorFormat, request._bDepthTest ), listShader );
        }
        warmShaders( listShader );
    }

    RHIPipelineStateHandle FramePSOManager::getOrCreateExtensionProgramPSO( const ExtensionProgramDesc& program, RHIFormat colorFormat, bool bDepthTest )
    {
        // 모듈 확장 패스의 프로그램(ExtensionRenderPass M3)이다. 셰이더 · 고정 상태 · 타깃 포맷이 같으면 한 PSO 를 나눈다. 기록 중에는 만들 수 없으므로 셋업(`FrameExtensionHost::collectFrame`)만 부른다.
        // 실패(0)도 기록한다 — 매 프레임 같은 컴파일을 되풀이하지 않고, 부르는 쪽이 0 을 보고 오류를 남기고 그리지 않는다(다른 셰이더로 대신 그리지 않는다).
        if ( _pDevice == nullptr || program._shaderPath.empty() )
            return 0;
        const hashed_string    programKey = makeExtensionProgramKey( program, colorFormat, bDepthTest );
        RHIPipelineStateHandle existing{ 0 };
        if ( _pCache->findExtensionProgramPSO( programKey, existing ) )
            return existing;

        // 셰이더는 부르는 쪽이 이번 프레임의 프로그램과 함께 데웠다(`warmExtensionPrograms`) — 여기는 드라이버 생성만 남는다.
        const RHIPipelineStateDesc   desc   = makeExtensionProgramDesc( program, colorFormat, bDepthTest );
        const RHIPipelineStateHandle handle = createPipelineStateTimed( *_pDevice, desc );
        registerLayout( handle, desc );
        if ( handle == 0 )
            SW_LOG_ERROR_ID( "FramePSOManager.ExtensionProgramCouldNot", "Extension program '%#' could not be created (see the shader error above) - passes that use it are not drawn", program._shaderPath );
        _pCache->setExtensionProgramPSO( _pDevice, programKey, handle );
        return handle;
    }

    RHIPipelineStateHandle FramePSOManager::createPSOForPass( RenderPassKind passType, const RenderGraphPassDesc* pPassDesc, const RHIFormat* pRTVFormatOverride )
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

    bool FramePSOManager::makePassPSODesc( RenderPassKind passType, const RenderGraphPassDesc* pPassDesc, const RHIFormat* pRTVFormatOverride, RHIPipelineStateDesc& outDesc ) const
    {
        // 파이프라인 패스는 XML 의 값(셰이더 · 진입점 · 렌더 상태 · 퍼뮤테이션 · 타깃)이 그대로 들어간다. 표는 XML 이 비운 셰이더의 기본값, 패스 define, XML 에 없는 내부 슬롯의 값이다.
        // 셰이더와 define 은 쿠커와 **같은 함수**(selectRenderPassShader)로 정한다. 어긋나면 Shipping 에서 매니페스트 미스가 난다.
        const RenderPassKindInfo& info = getRenderPassKindInfo( passType );
        // 내부 슬롯은 대신 그리는 파이프라인 패스의 출력 선언으로 포맷 · 화면 속도 쓰기를 정한다(`_targetSourceType`).
        const RenderGraphPassDesc* pTargetDesc =
            ( pPassDesc == nullptr && info._targetSourceType != RenderPassKind::Invalid ) ? findPassDescByType( info._targetSourceType ) : pPassDesc;
        const RenderPassShaderSelection shader = selectRenderPassShader( passType, pPassDesc, pTargetDesc, engine::getSubsystem<EngineDefaultAssets>() );

        RHIPipelineStateDesc& desc = outDesc;
        desc                       = RHIPipelineStateDesc{};
        desc._vertexShaderPath     = shader._shaderPath;
        desc._pixelShaderPath      = desc._vertexShaderPath;
        desc._vertexEntryPoint     = string( resolveEntryPoint( pPassDesc != nullptr ? string_view( pPassDesc->_vertexEntryPoint ) : string_view{}, ShaderStage::Vertex ) );
        desc._pixelEntryPoint      = string( resolveEntryPoint( pPassDesc != nullptr ? string_view( pPassDesc->_pixelEntryPoint ) : string_view{}, ShaderStage::Pixel ) );
        applyPassRenderState( desc, passType, pPassDesc );
        if ( pPassDesc != nullptr )
        {
            desc._geometryEntryPoint = pPassDesc->_geometryEntryPoint;
            desc._hullEntryPoint     = pPassDesc->_hullEntryPoint;
            desc._domainEntryPoint   = pPassDesc->_domainEntryPoint;
            if ( desc._hullEntryPoint.empty() == false )
                desc._topology = RHIPrimitiveTopology::PatchList3;
        }
        desc._listShaderDefine = shader._listDefine;
        if ( fillPassRenderTargetFormats( desc, pTargetDesc, pRTVFormatOverride != nullptr ? pRTVFormatOverride : info._pColorFormat, info._colorTargetCount ) == false )
        {
            SW_LOG_ERROR_ID( "FramePSOManager.PassNoRenderTarget", "Pass %# has no render target format (no fixed format, and no color output declaration with a known format) - no PSO is created", passType );
            return false;
        }

        // 컬러 출력이 없는 패스(그림자, 뎁스 프리패스)는 픽셀 스테이지가 없다. 셰이더에 PSMain 이 있어도 붙이지
        // 않는다. 주의: 여기서 정하지 않으면 백엔드마다 판단이 갈리고, 그 위에 머티리얼 define 을 적용한 변형
        // (createMaterialPSOVariant 는 이 desc 를 그대로 물려받는다)은 쿠커가 쿠킹하지 않아 Shipping 에서 매니페스트 미스로
        // 실패한다. 쿠커 쪽 같은 규칙은 FrameRendererUtil::hasPixelStage 다.
        // 그림자 캐시 복원(kWritesPixelDepth)만 컬러 없이 픽셀 깊이를 쓰므로 픽셀 스테이지를 남긴다.
        if ( desc._numRenderTargets == 0 && info.hasFlag( RenderPassTraitFlag::kWritesPixelDepth ) == false )
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

    bool FramePSOManager::hasRenderTargetFormatSource( RenderPassKind passType ) const
    {
        const RenderPassKindInfo&  info        = getRenderPassKindInfo( passType );
        const RenderPassKind       sourceType  = info._targetSourceType != RenderPassKind::Invalid ? info._targetSourceType : passType;
        const RenderGraphPassDesc* pTargetDesc = findPassDescByType( sourceType );
        const bool                 bDeclared   = pTargetDesc != nullptr && pTargetDesc->_listOutput.empty() == false;
        return info._pColorFormat != nullptr || info._colorTargetCount == 0 || bDeclared;
    }

    RenderPSOCache::MaterialPSOEntry FramePSOManager::createMaterialPSOVariant( RHIPipelineStateHandle passPSO, RenderPassKind passType,
                                                                                const GPUShaderPermutation* pPermutation, RenderViewMode viewMode, bool bReverseCulling,
                                                                                RHIMeshVertexFormat vertexFormat, RHIPipelineStateDesc* pOutDeferredDesc )
    {
        if ( passPSO == 0 || _pDevice == nullptr )
            return RenderPSOCache::MaterialPSOEntry::failed();

        // 패스가 정한 렌더 상태를 통째로 물려받는다. 블렌드, 뎁스, RT 포맷은 패스의 사실이지 머티리얼의 것이 아니다.
        RHIPipelineStateDesc desc{};
        if ( _pCache->findDesc( passPSO, desc ) == false )
        {
            // desc 를 모르는 PSO 다 — 변형을 만들 근거가 없다. 패스 PSO 로 다른 그림을 그리지 않고 이 조합은 그리지 않는다.
            SW_LOG_ERROR_ID( "FramePSOManager.MaterialPSOVariantPass", "Material PSO variant: pass PSO of pass type %# has no recorded desc - these batches are not drawn", static_cast<uint32>( passType ) );
            return RenderPSOCache::MaterialPSOEntry::failed();
        }

        const RenderPassKindInfo& passInfo = getRenderPassKindInfo( passType );
        bool                      bChanged{ false };
        if ( pPermutation != nullptr )
        {
            // 패스 → 머티리얼 순서로 합친다. 무엇을 덮는지는 패스 종류의 표가 정한다(`RenderStateOverride::Scope`). 값이 같으면 PSO 를 더 만들지 않는다.
            const RenderState merged = pPermutation->_renderStateOverride.applyTo( desc._renderState, passInfo.getMaterialStateScope() );
            if ( merged != desc._renderState )
            {
                desc._renderState = merged;
                bChanged          = true;
            }
            // 머티리얼 셰이더를 쓰는 패스만 경로를 교체한다. 그림자와 뎁스는 자기 지오메트리 셰이더가 기준이고, 모양을 머티리얼 코드가 정하는
            // 배치(알파 컷오프 · 정점 변형)만 머티리얼 셰이더의 깊이 퍼뮤테이션으로 그린다(usesMaterialShaderForDepth — 쿠커와 같은 판정).
            const bool bMaterialDepthShader = passInfo.usesMaterialShaderForDepth( &pPermutation->_listDefine );
            const bool bReplaceShaderPath   = ( passInfo.usesMaterialShader() || bMaterialDepthShader ) && pPermutation->_shaderPath.empty() == false &&
                                            pPermutation->_shaderPath != desc._vertexShaderPath;
            if ( bReplaceShaderPath )
            {
                desc._vertexShaderPath = pPermutation->_shaderPath;
                desc._pixelShaderPath  = pPermutation->_shaderPath;
                // 깊이 패스는 컬러 출력이 없다 — 픽셀 스테이지는 알파 컷오프의 버리기에만 둔다.
                if ( bMaterialDepthShader && passInfo.hasMaterialDepthPixelStage( &pPermutation->_listDefine ) == false )
                    desc._pixelShaderPath.clear();
                bChanged = true;
            }
            // 변위 머티리얼은 머티리얼 셰이더의 헐 · 도메인을 붙인다(쿠커와 같은 판정). 테셀레이션이 없는 백엔드에서는 평면으로 대신 그리지 않고 이 조합을 그리지 않는다.
            if ( passInfo.isMaterialTessellated( &pPermutation->_listDefine ) )
            {
                if ( _pDevice->getCapabilities()._bMaterialTessellation == SW_FALSE )
                {
                    SW_LOG_ERROR_ID( "FramePSOManager.DisplacementUnsupported",
                                     "Material '%#' uses MATERIAL_DISPLACEMENT but backend %# has no material tessellation (RHICapabilities::_bMaterialTessellation) - these batches are not drawn",
                                     pPermutation->_shaderPath, static_cast<uint32>( _pDevice->getBackendType() ) );
                    return RenderPSOCache::MaterialPSOEntry::failed();
                }
                desc._hullEntryPoint   = string( getShaderStageInfo( ShaderStage::Hull )._pEntryPoint );
                desc._domainEntryPoint = string( getShaderStageInfo( ShaderStage::Domain )._pEntryPoint );
                desc._topology         = RHIPrimitiveTopology::PatchList3;
                bChanged               = true;
            }
            // 등장 디더 변형은 패스 셰이더의 깊이 패스에도 픽셀 스테이지가 있어야 한다 — 기본 패스와 같은 픽셀을 버린다(쿠커와 같은 판정).
            if ( passInfo.needsAppearanceDitherPixelStage( &pPermutation->_listDefine ) && desc._pixelShaderPath.empty() )
            {
                desc._pixelShaderPath = desc._vertexShaderPath;
                if ( desc._pixelEntryPoint.empty() )
                    desc._pixelEntryPoint = string( resolveEntryPoint( string_view{}, ShaderStage::Pixel ) );
                bChanged = true;
            }
            // 같은 이름은 머티리얼 값으로 바꾼다(패스의 MATERIAL_TEXTURE_COUNT=0 → 머티리얼 칸 수) — 쿠커와 같은 함수(MaterialPermutation::mergeDefine).
            for ( const string& defineStr : pPermutation->_listDefine )
            {
                if ( VectorUtil::contains( desc._listShaderDefine, defineStr ) )
                    continue;
                MaterialPermutation::mergeDefine( desc._listShaderDefine, defineStr );
                bChanged = true;
            }
        }

        // 메시 정점 형식이 다르면 입력 레이아웃만 다른 PSO 다(지형의 Full 형식).
        if ( desc._meshVertexFormat != vertexFormat )
        {
            desc._meshVertexFormat = vertexFormat;
            bChanged               = true;
        }

        // 뷰 모드는 머티리얼 퍼뮤테이션 **뒤에** 적용한다. 순서가 반대면 머티리얼이 셰이더 경로를
        // 교체할 때 방금 넣은 define 이 다른 셰이더로 넘어가 의미가 달라진다.
        if ( passInfo.shouldApplyViewMode() && applyViewModeToDesc( desc, viewMode ) )
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
            SW_LOG_ERROR_ID( "FramePSOManager.MaterialPSOVariantView", "Material PSO variant '%#' (view mode %#) failed to build - these batches are not drawn", desc._vertexShaderPath,
                             RenderViewModeUtil::getInfo( viewMode )._pName );
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
        collectMaterialPSORequests( _gpuScene.getOpaqueBatches(), listOpaqueRequest, _listOpaqueVariant );
        collectMaterialPSORequests( _gpuScene.getTransparentBatches(), listTransparentRequest, _listTransparentVariant );
        if ( listOpaqueRequest.empty() && listTransparentRequest.empty() )
            return;

        bool bCreatedVariant{ false };
        for ( const auto& [passType, passPSO] : _pCache->getEnginePSOs() )
        {
            if ( passPSO == 0 || getRenderPassKindInfo( passType ).shouldDrawSceneMeshes() == false )
                continue;
            const bool                        bTransparentPass = getRenderPassKindInfo( passType ).hasFlag( RenderPassTraitFlag::kDrawsTransparentBatch );
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

    void FramePSOManager::warmPinnedMaterialPSOs( MaterialCache& pinnedMaterials, uint32 viewModeMask )
    {
        // 줄은 하나라 호스트가 건 렌더러만 부른다(`FrameRendererHost::_pPinnedMaterials`) — 화면 밖 렌더러가 꺼내 가면 주 렌더러가 만들지 못한다.
        if ( _pDevice == nullptr )
            return;
        vector<MaterialPSOWarmEntry>& listEntry = _listPSOWarmEntryScratch;
        pinnedMaterials.takePSOWarmEntries( listEntry );
        if ( listEntry.empty() )
            return;
        SW_PROFILE_SCOPE( "RT.PSO.warmPinned" );
        // 씬 배치와 같은 키로 만든다(`GPUMaterialRegistry::makeShaderPermutation`) — 그 머티리얼이 씬에 들어오는 프레임에는 이미 캐시에 있다.
        // 비동기로 읽혀 나타나는 메시는 등장 디더 변형으로 먼저 그려지므로 그 변형도 만든다. 거울 배치(컬 반전)는 드물어 그 프레임에 만든다.
        bool bCreatedVariant{ false };
        for ( const MaterialPSOWarmEntry& entry : listEntry )
        {
            const bool bTransparentMaterial = entry._bTransparent;
            for ( const bool bAppearanceDither : { false, true } )
            {
                const GPUShaderPermutation permutation = GPUMaterialRegistry::makeShaderPermutation( entry, bAppearanceDither );
                for ( const auto& [passType, passPSO] : _pCache->getEnginePSOs() )
                {
                    const bool bTransparentPass = getRenderPassKindInfo( passType ).hasFlag( RenderPassTraitFlag::kDrawsTransparentBatch );
                    const bool bSkippedPass     = passPSO == 0 || getRenderPassKindInfo( passType ).shouldDrawSceneMeshes() == false || bTransparentPass != bTransparentMaterial;
                    if ( bSkippedPass || getRenderPassKindInfo( passType ).shouldDrawMaterial( &permutation._listDefine ) == false )
                        continue;
                    const uint32 passModeMask = getPassViewModeMask( getRenderPassKindInfo( passType ), viewModeMask );
                    for ( uint32 modeIndex = 0; modeIndex < static_cast<uint32>( RenderViewMode::Count ); ++modeIndex )
                    {
                        if ( ( passModeMask & ( 1u << modeIndex ) ) == 0 )
                            continue;
                        if ( ensureMaterialPSOVariant( passPSO, passType, &permutation, permutation._hash, false, static_cast<RenderViewMode>( modeIndex ), RHIMeshVertexFormat::Packed, false ) )
                            bCreatedVariant = true;
                    }
                }
            }
        }
        listEntry.clear();
        if ( bCreatedVariant )
            ensureMaterialFallbackBuffers();
    }

    void FramePSOManager::collectMaterialPSORequests( vector_reference<const GPUMeshBatch> listBatch, vector<MaterialPSORequest>& outListRequest, vector<uint32>& outListVariant )
    {
        // 퍼뮤테이션이 없는 배치(머티리얼을 붙이지 않은 메시)도 모은다. Lit 이 아니거나 거울 배치면 그 배치에도
        // 변형이 필요하다. 그냥 건너뛰면 머티리얼 없는 메시만 뷰 모드와 컬 반전이 적용되지 않아 화면이 섞인다.
        outListRequest.clear();
        outListVariant.clear();
        outListVariant.reserve( listBatch.size() );
        _mapRequestIndexScratch.clear();
        for ( const GPUMeshBatch& batch : listBatch )
        {
            const RHIMeshVertexFormat batchFormat = batch._mesh != nullptr ? batch._mesh->getVertexFormat() : RHIMeshVertexFormat::Packed;
            const uint64              packed      = ( static_cast<uint64>( batch._shaderPermutation ) << 16 ) | ( static_cast<uint64>( batchFormat ) << 8 ) | ( batch._bReverseCulling != SW_FALSE ? 1u : 0u );
            const auto                found       = _mapRequestIndexScratch.try_emplace( packed, static_cast<uint32>( outListRequest.size() ) );
            if ( found.second )
                outListRequest.push_back( MaterialPSORequest{ batch._shaderPermutation, batch._bReverseCulling, batchFormat } );
            outListVariant.push_back( found.first->second );
        }
    }

    bool FramePSOManager::ensureRequestMaterialPSOs( RHIPipelineStateHandle passPSO, RenderPassKind passType, const MaterialPSORequest& request, uint32 viewModeMask )
    {
        const bool bReverseCulling = ( request._bReverseCulling != SW_FALSE );
        // 이 패스가 그리지 않는 배치(외곽선을 켜지 않은 머티리얼)의 변형은 만들지 않는다. 쿠커도 그것을 쿠킹하지 않는다.
        const GPUShaderPermutation* pRequestPermutation =
            ( request._shaderPermutation == kInvalidShaderPermutation ) ? nullptr : _gpuScene.findShaderPermutation( request._shaderPermutation );
        if ( getRenderPassKindInfo( passType ).shouldDrawMaterial( pRequestPermutation != nullptr ? &pRequestPermutation->_listDefine : nullptr ) == false )
            return false;
        bool         bCreatedVariant{ false };
        const uint32 passModeMask = getPassViewModeMask( getRenderPassKindInfo( passType ), viewModeMask );
        for ( uint32 modeIndex = 0; modeIndex < static_cast<uint32>( RenderViewMode::Count ); ++modeIndex )
        {
            if ( ( passModeMask & ( 1u << modeIndex ) ) == 0 )
                continue;
            const RenderViewMode viewMode = static_cast<RenderViewMode>( modeIndex );
            if ( request._shaderPermutation == kInvalidShaderPermutation )
            {
                // 퍼뮤테이션이 없는 배치를 위한 변형이다. 적용하는 것이 뷰 모드와 컬 반전뿐이다. Lit 이고 거울이 아니면 만들 것이 없다
                // (그때는 패스 PSO 가 그대로 정답이고 resolveBatchPSO 도 캐시를 보지 않는다).
                const bool bNeedsVariant = viewMode != RenderViewMode::Lit || bReverseCulling || request._vertexFormat != RHIMeshVertexFormat::Packed;
                if ( bNeedsVariant && ensureMaterialPSOVariant( passPSO, passType, nullptr, 0, bReverseCulling, viewMode, request._vertexFormat ) )
                    bCreatedVariant = true;
                continue;
            }
            if ( pRequestPermutation == nullptr )
                continue;
            if ( ensureMaterialPSOVariant( passPSO, passType, pRequestPermutation, pRequestPermutation->_hash, bReverseCulling, viewMode, request._vertexFormat ) )
                bCreatedVariant = true;
        }
        return bCreatedVariant;
    }

    bool FramePSOManager::ensureMaterialPSOVariant( RHIPipelineStateHandle passPSO, RenderPassKind passType, const GPUShaderPermutation* pPermutation,
                                                    uint64 permutationHash, bool bReverseCulling, RenderViewMode viewMode, RHIMeshVertexFormat vertexFormat, bool bUrgent )
    {
        // 키를 `Building` 으로 올린 쪽만 만든다(`beginMaterialPSO`). 셰이더 컴파일이 낄 수 있는 변형은 desc 만 정해 빌더 줄에 넣고(렌더 스레드의 임계 경로에서 뺀다),
        // 결과는 `pollMaterialPSOBuilds` 가 기록 전에 합친다. 드로우 경로는 `Building` 배치를 이번 프레임만 건너뛴다(`RT.PSO.pendingBatches`).
        const MaterialPSOKey key{ passPSO, permutationHash, viewMode, vertexFormat, static_cast<uint8>( bReverseCulling ? SW_TRUE : SW_FALSE ) };
        if ( _pCache->beginMaterialPSO( key ) == false )
            return false;
        RHIPipelineStateDesc                   deferredDesc{};
        const RenderPSOCache::MaterialPSOEntry entry = createMaterialPSOVariant( passPSO, passType, pPermutation, viewMode, bReverseCulling, vertexFormat, &deferredDesc );
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
        // 결정적 실행(`DeterministicRunPolicy::_bFinishAssetJobsEachFrame`)은 빌더 스레드를 쓰지 않는다 — 변형이 언제 준비되느냐가 컴파일 시간(셰이더 캐시가 비었는가)에 달린다.
        return _frameSettings._bPSOAsync != SW_FALSE && engine::getSubsystem<DeterministicRun>().getPolicy()._bFinishAssetJobsEachFrame == SW_FALSE && _pDevice != nullptr && _pDevice->getCapabilities()._bThreadSafeResourceCreation != SW_FALSE;
    }

    void FramePSOManager::pollMaterialPSOBuilds()
    {
        if ( _pDevice == nullptr )
            return;
        SW_PROFILE_SCOPE( "RT.PSO.pumpBuilds" );
        // 빌더 스레드를 쓰지 않을 때: `-gv_psoAsync=0` 은 전부(옛 동작, A/B 측정), 그 밖(GL)은 프레임당 시간 예산(줄이 있으면 최소 하나)이다.
        // 예산은 `EngineConfig::_render`(Config/Engine/EngineConfig.json)가 정본이다 — 0 이면 로드의 범위 검사(Min 1)가 막는다.
        const bool   bBuildAll          = _frameSettings._bPSOAsync == SW_FALSE || engine::getSubsystem<DeterministicRun>().getPolicy()._bFinishAssetJobsEachFrame == SW_TRUE; // 결정적 실행은 시간 예산 없이 이 프레임에 모두 만든다
        const uint32 budgetMicroseconds = bBuildAll ? 0u : engine::getSubsystem<EngineConfig>()._render._psoBuildBudgetMicroseconds;
        const uint32 readyCount         = _pCache->pollMaterialPSOBuilds( _pDevice, usesAsyncMaterialPSOBuilder(), budgetMicroseconds );
        SW_PROFILE_COUNT( "RT.PSO.pendingBuilds", _pCache->getPendingMaterialPSOBuilds() );
        // 방금 합친 변형의 레이아웃은 폴백 stride 를 모으던 시점에는 없었다(`ensureMaterialPSOs` 의 같은 이유).
        if ( readyCount > 0 )
            ensureMaterialFallbackBuffers();
    }

    void FramePSOManager::ensureParticlePSO()
    {
        _particlePSOKey                              = MaterialPSOKey{};
        const RHIPipelineStateHandle transparentPSO  = getEnginePSO( RenderPassKind::Transparent );
        const bool                   bNoParticlePass = _pDevice == nullptr || transparentPSO == 0 || _gpuScene.getParticleBatches().empty();
        if ( bNoParticlePass )
            return;

        // 머티리얼 변형 캐시의 키 공간을 빌린다 — 퍼뮤테이션 해시 자리에 입자 셰이더 경로와 태그를 섞어 머티리얼 퍼뮤테이션과 겹치지 않게 한다.
        const string& shaderPath = engine::getSubsystem<EngineDefaultAssets>()._shaderParticle.get();
        const uint64  shaderHash = HashUtil::combine( HashUtil::computeStringHash64( shaderPath, false ), FourCCUtil::make( "VFXP" ) );
        _particlePSOKey          = MaterialPSOKey{ transparentPSO, shaderHash, RenderViewMode::Lit, RHIMeshVertexFormat::Packed, SW_FALSE };
        if ( _pCache->hasMaterialPSO( _particlePSOKey ) )
            return;

        RHIPipelineStateDesc transparentDesc{};
        if ( _pCache->findDesc( transparentPSO, transparentDesc ) == false )
        {
            SW_LOG_ERROR_ID( "FramePSOManager.TransparentPassPSONo", "Transparent pass PSO has no recorded desc - particles are not drawn" );
            _pCache->setMaterialPSO( _particlePSOKey, RenderPSOCache::MaterialPSOEntry::failed() );
            return;
        }
        // 셰이더는 셋업이 투명 패스와 함께 데웠다(`createPassPSOS`) — 여기는 드라이버 생성만 남는다.
        const RHIPipelineStateDesc   desc   = makeParticlePSODesc( transparentDesc, shaderPath );
        const RHIPipelineStateHandle handle = createPipelineStateTimed( *_pDevice, desc );
        if ( handle == 0 )
            SW_LOG_ERROR_ID( "FramePSOManager.FailedCreateParticlePSO", "Failed to create the particle PSO from '%#' - particles are not drawn", shaderPath );
        registerLayout( handle, desc );
        _pCache->setMaterialPSO( _particlePSOKey, RenderPSOCache::MaterialPSOEntry::ready( handle, true ) );
    }

    RHIPipelineStateHandle FramePSOManager::findParticlePSO() const
    {
        return _particlePSOKey._passPSO != 0 ? _pCache->findMaterialPSO( _particlePSOKey ) : RHIPipelineStateHandle{ 0 };
    }

    GPUMeshBatchPSO FramePSOManager::resolveBatchPSO( RHIPipelineStateHandle passPSO, const MaterialPSORequest& request, RenderViewMode viewMode ) const
    {
        const auto makeBatchPSO = [this]( RHIPipelineStateHandle pso, RenderPSOCache::MaterialPSOState state )
        {
            return GPUMeshBatchPSO{ pso, pso != 0 ? findLayout( pso ) : nullptr, state };
        };
        uint64                      permutationHash{ 0 };
        uint64                      fallbackHash{ 0 };
        const GPUShaderPermutation* pPermutation = ( request._shaderPermutation != kInvalidShaderPermutation ) ? _gpuScene.findShaderPermutation( request._shaderPermutation ) : nullptr;
        if ( pPermutation != nullptr )
        {
            permutationHash = pPermutation->_hash;
            fallbackHash    = pPermutation->_fallbackHash;
        }

        // 적용할 것이 하나도 없는 조합이다. ensureMaterialPSOs 도 이 키를 만들지 않는다.
        // (해시 0은 퍼뮤테이션 없음을 뜻한다. 실제 퍼뮤테이션 해시가 0이 되더라도 같은 키로
        //  떨어지는데, 그 둘이 만드는 디스크립터는 어차피 같으므로 해롭지 않다.)
        const bool bDefaultPassPSO = permutationHash == 0 && viewMode == RenderViewMode::Lit && request._bReverseCulling == SW_FALSE && request._vertexFormat == RHIMeshVertexFormat::Packed;
        if ( bDefaultPassPSO )
            return makeBatchPSO( passPSO, RenderPSOCache::MaterialPSOState::Ready );

        // `Ready` 가 아닌 변형(만드는 중 · 실패 · 없음)은 그리지 않는다 — 패스 PSO 로 바꿔 그리면 머티리얼 · 보기 모드가 다른 그림이 된다. ensureMaterialPSOs 가 기록 전에
        // 채우므로 동기 생성에서는 항상 `Ready` 나 `Failed` 이고(`Failed` 는 그때 오류를 남겼다), `Building` 은 빌더 스레드(U12)가 아직 못 끝낸 배치다.
        MaterialPSOKey                         key{ passPSO, permutationHash, viewMode, request._vertexFormat, request._bReverseCulling };
        const RenderPSOCache::MaterialPSOState state = _pCache->findMaterialPSOState( key );
        if ( state != RenderPSOCache::MaterialPSOState::Building )
            return makeBatchPSO( _pCache->findMaterialPSO( key ), state );
        // 런타임 스위치로 새 변형이 막 생긴 배치는 빌더가 끝낼 때까지 직전 변형으로 그린다(사라짐 대신 늦은 전환). 직전 변형도 `Ready` 가 아니면 그리지 않는다.
        key._permutationHash                     = fallbackHash;
        const RHIPipelineStateHandle fallbackPSO = fallbackHash != 0 ? _pCache->findMaterialPSO( key ) : RHIPipelineStateHandle{ 0 };
        if ( fallbackPSO == 0 )
            return makeBatchPSO( 0, RenderPSOCache::MaterialPSOState::Building );
        SW_PROFILE_COUNT( "RT.PSO.fallbackVariants", 1 );
        return makeBatchPSO( fallbackPSO, RenderPSOCache::MaterialPSOState::Ready );
    }

    void FramePSOManager::buildBatchPSOTable( uint32 viewModeMask )
    {
        SW_PROFILE_SCOPE( "RT.PSO.batchTable" );
        _listBatchPSO.clear();
        _listBatchPSORow.clear();
        if ( _pDevice == nullptr )
            return;
        for ( const auto& [passType, passPSO] : _pCache->getEnginePSOs() )
        {
            const RenderPassKindInfo& passInfo = getRenderPassKindInfo( passType );
            if ( passPSO == 0 || passInfo.shouldDrawSceneMeshes() == false )
                continue;
            const bool                        bTransparentPass = passInfo.hasFlag( RenderPassTraitFlag::kDrawsTransparentBatch );
            const vector<MaterialPSORequest>& listRequest      = bTransparentPass ? _listTransparentPSORequestScratch : _listOpaquePSORequestScratch;
            const uint32                      passModeMask     = getPassViewModeMask( passInfo, viewModeMask );
            for ( uint32 modeIndex = 0; modeIndex < static_cast<uint32>( RenderViewMode::Count ); ++modeIndex )
            {
                if ( ( passModeMask & ( 1u << modeIndex ) ) == 0 )
                    continue;
                BatchPSORowRange row{};
                row._passPSO           = passPSO;
                row._first             = static_cast<uint32>( _listBatchPSO.size() );
                row._viewMode          = static_cast<RenderViewMode>( modeIndex );
                row._bAppliesViewMode  = passInfo.shouldApplyViewMode() ? SW_TRUE : SW_FALSE;
                row._bTransparentBatch = bTransparentPass ? SW_TRUE : SW_FALSE;
                _listBatchPSORow.push_back( row );
                for ( const MaterialPSORequest& request : listRequest )
                {
                    _listBatchPSO.push_back( resolveBatchPSO( passPSO, request, row._viewMode ) );
                }
            }
        }
    }

    bool FramePSOManager::findBatchPSORow( RHIPipelineStateHandle passPSO, bool bTransparentBatch, RenderViewMode viewMode, const GPUMeshBatchPSO*& outPSO,
                                           span<const uint32>& outListVariant ) const
    {
        const RenderViewMode pipelineMode = RenderViewModeUtil::getPipelineMode( viewMode ); // Lit 과 같은 PSO 인 모드(MarkerSlots)는 Lit 변형을 그대로 쓴다
        for ( const BatchPSORowRange& row : _listBatchPSORow )
        {
            if ( row._passPSO != passPSO || ( row._bTransparentBatch != SW_FALSE ) != bTransparentBatch )
                continue;
            if ( row._bAppliesViewMode != SW_FALSE && row._viewMode != pipelineMode )
                continue;
            const vector<uint32>& listVariant = bTransparentBatch ? _listTransparentVariant : _listOpaqueVariant;
            outPSO                            = _listBatchPSO.data() + row._first;
            outListVariant                    = span<const uint32>( listVariant.data(), listVariant.size() );
            return true;
        }
        return false;
    }

    uint32 FramePSOManager::getPassViewModeMask( const RenderPassKindInfo& passInfo, uint32 viewModeMask )
    {
        // 보기 모드를 적용하지 않는 패스(그림자 · 깊이)는 모드마다 같은 PSO 다 — Lit 한 벌만 둔다(모드를 키에 섞으면 같은 PSO 를 또 만들고, 머티리얼 없는 배치는 찾는 키가 없어 그려지지 않았다).
        return passInfo.shouldApplyViewMode() ? viewModeMask : ( 1u << static_cast<uint32>( RenderViewMode::Lit ) );
    }

} // namespace sw
