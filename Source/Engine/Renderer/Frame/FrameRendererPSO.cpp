#include "pch.h"

#include "Core/Common/FourCcUtil.h"
#include "Core/Common/HashUtil.h"
#include "Core/Container/StringUtil.h"
#include "Core/Container/VectorUtil.h"
#include "Core/Task/TaskManager.h"

#include "Engine/Common/EngineServices.h"
#include "Engine/Config/EngineDefaultAssets.h"
#include "Engine/Graphics/Material/Material.h"
#include "Engine/Graphics/Material/MaterialCache.h"
#include "Engine/Graphics/Material/MaterialInstance.h"
#include "Engine/Graphics/RHI/IRHIDevice.h"
#include "Engine/Graphics/RHI/IRHIResourceFactory.h"
#include "Engine/Graphics/Shader/Compile/ShaderCompiler.h"
#include "Engine/Profiling/FrameProfiler.h"
#include "Engine/Renderer/Frame/FrameRenderer.h"
#include "Engine/Renderer/Frame/FrameRendererUtil.h"
#include "Engine/Renderer/Pipeline/RenderPassTypeInfo.h"
#include "Engine/Renderer/Scene/GPUMaterialRegistry.h"
#include "Engine/Resource/AssetManager.h"

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
    } // namespace

    RHIPipelineStateHandle FrameRenderer::createPSOForPassType( RenderPassType passType, const RHIFormat* pRtvFormatOverride )
    {
        return createPSOForPass( passType, findPassDescByType( passType ), pRtvFormatOverride );
    }

    void FrameRenderer::createModulePassPSOs()
    {
        // 모듈 패스는 셰이더가 패스마다 다르다(`_shaderPath` 필수). 종류 하나에 PSO 하나인 엔진 PSO 표에 둘 수 없어 모듈 이름으로 둔다.
        // 실패(0)도 기록한다 — 기록 중에는 만들 수 없고, 실행이 "등록됐는데 0" 을 보고 오류를 한 번 남긴다.
        for ( const RenderGraphPassDesc& pass : _pipelineResource.getDesc()._listPass )
        {
            RHIPipelineStateHandle existing{ 0 };
            const bool             bNeedsPSO = pass._resolvedType == RenderPassType::Module && pass._resolvedModulePass.empty() == false &&
                                   _pPSOCache->findModulePassPSO( pass._resolvedModulePass, existing ) == false;
            if ( bNeedsPSO )
                _pPSOCache->setModulePassPSO( pass._resolvedModulePass, createPSOForPass( RenderPassType::Module, &pass, nullptr ) );
        }
    }

    RHIPipelineStateHandle FrameRenderer::createPSOForPass( RenderPassType passType, const RenderGraphPassDesc* pPassDesc, const RHIFormat* pRtvFormatOverride )
    {
        if ( _pDevice == nullptr )
            return 0;

        // 셰이더, define, 기본 렌더 상태는 패스 종류 테이블(RenderPassTypeInfo)이 정하고, 파이프라인 XML 의 패스 서술이 그 위를 조정한다.
        // 셰이더와 define 은 쿠커와 **같은 함수**(selectRenderPassShader)로 정한다. 어긋나면 Shipping 에서 매니페스트 미스가 난다.
        const RenderPassTypeInfo&       info   = getRenderPassTypeInfo( passType );
        const RenderPassShaderSelection shader = selectRenderPassShader( passType, pPassDesc, engine::getSubsystem<EngineDefaultAssets>() );

        RHIPipelineStateDesc desc{};
        desc._vertexShaderPath = shader._shaderPath;
        desc._pixelShaderPath  = desc._vertexShaderPath;
        desc._vertexEntryPoint = string( resolveEntryPoint( pPassDesc != nullptr ? string_view( pPassDesc->_vertexEntryPoint ) : string_view{}, ShaderStage::Vertex ) );
        desc._pixelEntryPoint  = string( resolveEntryPoint( pPassDesc != nullptr ? string_view( pPassDesc->_pixelEntryPoint ) : string_view{}, ShaderStage::Pixel ) );
        applyPassDepthAndBlend( desc, info, pPassDesc );
        applyPassCullMode( desc, info, pPassDesc, passType );
        desc._listShaderDefine = shader._listDefine;
        // 내부 슬롯은 대신 그리는 파이프라인 패스의 출력 선언으로 포맷을 정한다(`_targetSourceType`).
        const RenderGraphPassDesc* pTargetDesc =
            ( pPassDesc == nullptr && info._targetSourceType != RenderPassType::Invalid ) ? findPassDescByType( info._targetSourceType ) : pPassDesc;
        fillPassRenderTargetFormats( desc, pTargetDesc, pRtvFormatOverride != nullptr ? pRtvFormatOverride : info._pColorFormat, info._colorTargetCount );

        // 컬러 출력이 없는 패스(그림자, 뎁스 프리패스)는 픽셀 스테이지가 없다. 셰이더에 PSMain 이 있어도 붙이지
        // 않는다. 주의: 여기서 정하지 않으면 백엔드마다 판단이 갈리고, 그 위에 머티리얼 define 을 적용한 변형
        // (createMaterialPSOVariant 는 이 desc 를 그대로 물려받는다)은 쿠커가 쿠킹하지 않아 Shipping 에서 매니페스트 미스로
        // 실패한다. 쿠커 쪽 같은 규칙은 FrameRendererUtil::hasPixelStage 다.
        if ( desc._numRenderTargets == 0 )
        {
            desc._pixelShaderPath.clear();
            desc._pixelEntryPoint.clear();
        }
        const RHIPipelineStateHandle handle = _pDevice->getResourceFactory()->createPipelineState( desc );
        registerPSOLayout( handle, desc );
        return handle;
    }

    void FrameRenderer::fillPassRenderTargetFormats( RHIPipelineStateDesc& desc, const RenderGraphPassDesc* pPassDesc, const RHIFormat* pRtvFormats,
                                                     uint32 numRenderTargets ) const
    {
        desc._numRenderTargets = numRenderTargets;
        if ( desc._numRenderTargets > kMaxColorAttachments )
            desc._numRenderTargets = kMaxColorAttachments;
        if ( pRtvFormats != nullptr )
        {
            for ( uint32 rtIndex = 0; rtIndex < desc._numRenderTargets; ++rtIndex )
            {
                desc._arrRtvFormat[rtIndex] = pRtvFormats[rtIndex];
            }
        }
        else if ( pPassDesc != nullptr && pPassDesc->_listOutput.empty() == false )
        {
            // 출력 선언에서 컬러 RT 를 센다. 쿠커가 픽셀 스테이지 유무를 판정하는 것과 **같은 함수**다.
            // 컬러를 찾지 못하면 호출하는 쪽이 넘긴 수로 물러난다(찾지 못한 슬롯의 포맷은 desc 기본값 그대로다).
            desc._numRenderTargets = FrameRendererUtil::collectColorOutputFormats(
                *pPassDesc, _pipelineResource.getDesc()._listAttachment, desc._arrRtvFormat, kMaxColorAttachments, numRenderTargets );
        }
        else
        {
            for ( uint32 rtIndex = 0; rtIndex < desc._numRenderTargets; ++rtIndex )
            {
                desc._arrRtvFormat[rtIndex] = RHIFormat::R8G8B8A8_UNORM;
            }
        }
    }

    RenderPSOCache::MaterialPSOEntry FrameRenderer::createMaterialPSOVariant( RHIPipelineStateHandle passPSO, RenderPassType passType,
                                                                              const GPUShaderPermutation* pPermutation,
                                                                              RenderViewMode viewMode, bool bReverseCulling )
    {
        RenderPSOCache::MaterialPSOEntry entry{ passPSO, 0 };
        if ( passPSO == 0 || _pDevice == nullptr )
            return entry;

        // 패스가 정한 렌더 상태를 통째로 물려받는다. 블렌드, 뎁스, RT 포맷은 패스의 사실이지 머티리얼의 것이 아니다.
        RHIPipelineStateDesc desc{};
        if ( _pPSOCache->findDesc( passPSO, desc ) == false )
        {
            // desc 를 모르는 PSO 다 — 변형을 만들 근거가 없다. 패스 PSO 로 다른 그림을 그리지 않고 이 조합은 그리지 않는다.
            SW_LOG_ERROR( "Material PSO variant: pass PSO of pass type %# has no recorded desc - these batches are not drawn", static_cast<uint32>( passType ) );
            return RenderPSOCache::MaterialPSOEntry{ 0, 0 };
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
            return entry;

        const RHIPipelineStateHandle handle = _pDevice->getResourceFactory()->createPipelineState( desc );
        if ( handle == 0 )
        {
            // 컴파일 실패다. 패스 PSO(다른 셰이더 · 다른 보기 모드)로 그리면 틀린 그림이 통과하므로 이 조합은 그리지 않고 오류를 남긴다.
            SW_LOG_ERROR( "Material PSO variant '%#' (view mode %#) failed to build - these batches are not drawn", desc._vertexShaderPath,
                          getRenderViewModeInfo( viewMode )._pName );
            return RenderPSOCache::MaterialPSOEntry{ 0, 0 };
        }
        registerPSOLayout( handle, desc );
        entry._pso    = handle;
        entry._bOwned = 1;
        return entry;
    }

    void FrameRenderer::ensureMaterialPSOs()
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

        const uint32 viewModeMask = collectFrameViewModeMask();
        bool         bCreatedVariant{ false };
        for ( const auto& [passType, passPSO] : _pPSOCache->getEnginePSOs() )
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
        // 다른 크기의 SwMaterialData 를 선언하면 그 stride 의 폴백이 없고, 머티리얼 없는 배치가
        // 그 PSO 로 그려질 때 registerMaterialBuffer 가 t5 를 **비운 채** 드로우를 낸다.
        // Vulkan 이 초기화되지 않은 디스크립터를 읽어 디바이스를 잃는 그 경로다.
        // 여기는 아직 기록 시작 전이라 버퍼를 만들 수 있다. 새 변형을 만든 프레임에만 돈다.
        if ( bCreatedVariant )
            ensureMaterialFallbackBuffers();
    }

    void FrameRenderer::warmPinnedMaterialPSOs()
    {
        // 줄은 하나라 주 렌더러만 꺼낸다 — 미리보기 렌더러는 자기 PSO 캐시를 가져 꺼내 가면 주 렌더러가 만들지 못한다.
        const bool bNoWarm = _pDevice == nullptr || _bScenePreview == SW_TRUE || engine::areEngineServicesBound() == false;
        if ( bNoWarm )
            return;
        vector<shared_ptr<Material>>& listMaterial = _listPSOWarmMaterialScratch;
        engine::getSubsystem<AssetManager>().getAssetCache<MaterialCache>().takePSOWarmMaterials( listMaterial );
        if ( listMaterial.empty() )
            return;
        SW_PROFILE_SCOPE( "RT.PSO.warmPinned" );
        // 씬 배치와 같은 키로 만든다(`GPUMaterialRegistry::makeShaderPermutation`) — 그 머티리얼이 씬에 들어오는 프레임에는 이미 캐시에 있다.
        // 비동기로 읽혀 나타나는 메시는 등장 디더 변형으로 먼저 그려지므로 그 변형도 만든다. 거울 배치(컬 반전)는 드물어 그 프레임에 만든다.
        const uint32 viewModeMask = collectFrameViewModeMask();
        bool         bCreatedVariant{ false };
        for ( const shared_ptr<Material>& pMaterial : listMaterial )
        {
            const bool bTransparentMaterial = pMaterial->getBlendMode() == RHIBlendMode::Transparent;
            for ( const bool bAppearanceDither : { false, true } )
            {
                const GPUShaderPermutation permutation = GPUMaterialRegistry::makeShaderPermutation( *pMaterial, nullptr, bAppearanceDither );
                for ( const auto& [passType, passPSO] : _pPSOCache->getEnginePSOs() )
                {
                    const bool bTransparentPass = getRenderPassTypeInfo( passType ).hasFlag( RenderPassTraitFlag::kDrawsTransparentBatch );
                    const bool bSkippedPass     = passPSO == 0 || FrameRendererUtil::drawsSceneMeshes( passType ) == false || bTransparentPass != bTransparentMaterial;
                    if ( bSkippedPass || FrameRendererUtil::drawsMaterialInPass( passType, &permutation._listDefine ) == false )
                        continue;
                    for ( uint32 modeIndex = 0; modeIndex < static_cast<uint32>( RenderViewMode::Count ); ++modeIndex )
                    {
                        if ( ( viewModeMask & ( 1u << modeIndex ) ) == 0 )
                            continue;
                        if ( ensureMaterialPSOVariant( passPSO, passType, &permutation, permutation._hash, false, static_cast<RenderViewMode>( modeIndex ) ) )
                            bCreatedVariant = true;
                    }
                }
            }
        }
        listMaterial.clear(); // 소유를 오래 쥐지 않는다 — 고정을 놓으면 머티리얼이 사라질 수 있어야 한다
        if ( bCreatedVariant )
            ensureMaterialFallbackBuffers();
    }

    uint32 FrameRenderer::collectFrameViewModeMask() const
    {
        // 이번 프레임에 만들 변형은 (패스, 퍼뮤테이션, 뷰 모드)로 정해진다. 보기 모드는 뷰마다의 설정이라 이번 프레임에 그리는 뷰들의 모드를 모은다.
        // 뷰 설정은 프레임 시작에 정해지고 기록 중에 바뀌지 않으므로, 드로우가 고르는 PSO 는 여기서 준비한 집합 안에 있다.
        // 겹침 모드(ShadedWireframe)는 배치를 Lit 으로 한 번 더 그리므로 Lit 변형도 모은다.
        auto viewModeBits = []( RenderViewMode viewMode ) -> uint32
        {
            const uint32 bit = 1u << static_cast<uint32>( viewMode );
            return getRenderViewModeInfo( viewMode )._bOverlay ? ( bit | ( 1u << static_cast<uint32>( RenderViewMode::Lit ) ) ) : bit;
        };
        uint32 viewModeMask = viewModeBits( _mainView._settings._viewMode );
        for ( const unique_ptr<ViewTarget>& pView : _listExtraView )
        {
            if ( pView->_bRenderThisFrame == SW_TRUE )
                viewModeMask |= viewModeBits( pView->_settings._viewMode );
        }
        return viewModeMask;
    }

    void FrameRenderer::collectMaterialPSORequests( vector_reference<const GPUMeshBatch> listBatch, vector<MaterialPSORequest>& outListRequest )
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

    bool FrameRenderer::ensureRequestMaterialPSOs( RHIPipelineStateHandle passPSO, RenderPassType passType, const MaterialPSORequest& request, uint32 viewModeMask )
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

    bool FrameRenderer::ensureMaterialPSOVariant( RHIPipelineStateHandle passPSO, RenderPassType passType, const GPUShaderPermutation* pPermutation,
                                                  uint64 permutationHash, bool bReverseCulling, RenderViewMode viewMode )
    {
        // 만드는 것은 락 밖에서, 넣는 것만 락 안에서 한다. 셰이더 컴파일이 낄 수 있어 드로우 경로의 조회를 붙잡으면 안 된다.
        const uint64 key = RenderPSOCache::materialPSOKey( passPSO, permutationHash, viewMode, bReverseCulling );
        if ( _pPSOCache->hasMaterialPSO( key ) )
            return false;
        const RenderPSOCache::MaterialPSOEntry entry = createMaterialPSOVariant( passPSO, passType, pPermutation, viewMode, bReverseCulling );
        _pPSOCache->setMaterialPSO( key, entry );
        return true;
    }

    void FrameRenderer::ensureParticlePSO()
    {
        _particlePSOKey                              = 0;
        const RHIPipelineStateHandle transparentPSO  = findPassPSO( RenderPassType::Transparent );
        const bool                   bNoParticlePass = _pDevice == nullptr || transparentPSO == 0 || _gpuScene.getParticleBatches().empty();
        if ( bNoParticlePass )
            return;

        // 머티리얼 변형 캐시의 키 공간을 빌린다 — 퍼뮤테이션 해시 자리에 입자 셰이더 경로와 태그를 섞어 머티리얼 퍼뮤테이션과 겹치지 않게 한다.
        const string& shaderPath = engine::getSubsystem<EngineDefaultAssets>()._shaderParticle.get();
        const uint64  shaderHash = HashUtil::combine( hashed_string( shaderPath ).getHash(), FourCcUtil::make( "VFXP" ) );
        _particlePSOKey          = RenderPSOCache::materialPSOKey( transparentPSO, shaderHash, RenderViewMode::Lit, false );
        if ( _pPSOCache->hasMaterialPSO( _particlePSOKey ) )
            return;

        RHIPipelineStateDesc desc{};
        if ( _pPSOCache->findDesc( transparentPSO, desc ) == false )
        {
            SW_LOG_ERROR( "Transparent pass PSO has no recorded desc - particles are not drawn" );
            _pPSOCache->setMaterialPSO( _particlePSOKey, RenderPSOCache::MaterialPSOEntry{ 0, 0 } );
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
        const RHIPipelineStateHandle handle = _pDevice->getResourceFactory()->createPipelineState( desc );
        if ( handle == 0 )
            SW_LOG_ERROR( "Failed to create the particle PSO from '%#' - particles are not drawn", shaderPath );
        registerPSOLayout( handle, desc );
        _pPSOCache->setMaterialPSO( _particlePSOKey, RenderPSOCache::MaterialPSOEntry{ handle, static_cast<uint8>( handle != 0 ? 1 : 0 ) } );
    }

    RHIPipelineStateHandle FrameRenderer::findParticlePSO() const
    {
        return _particlePSOKey != 0 ? _pPSOCache->findMaterialPSO( _particlePSOKey ) : RHIPipelineStateHandle{ 0 };
    }

    bool FrameRenderer::findPSODesc( RHIPipelineStateHandle pso, RHIPipelineStateDesc& outDesc ) const
    {
        return _pPSOCache->findDesc( pso, outDesc );
    }

    RHIPipelineStateHandle FrameRenderer::psoForBatch( RHIPipelineStateHandle passPSO, const GPUMeshBatch& batch, RenderViewMode viewMode ) const
    {
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
        const bool bReverseCulling = ( batch._bReverseCulling != SW_FALSE );
        const bool bDefaultPassPSO = permutationHash == 0 && viewMode == RenderViewMode::Lit && bReverseCulling == false;
        if ( bDefaultPassPSO )
            return passPSO;

        // 찾지 못하거나 만들지 못한 변형(0)은 그리지 않는다 — 패스 PSO 로 바꿔 그리면 머티리얼 · 보기 모드가 다른 그림이 된다. ensureMaterialPSOs 가 기록 전에
        // 채우므로 정상 경로에서는 항상 있고, 만들지 못한 것은 그때 오류를 남겼다.
        return _pPSOCache->findMaterialPSO( RenderPSOCache::materialPSOKey( passPSO, permutationHash, viewMode, bReverseCulling ) );
    }

    bool FrameRenderer::drawsBatchInPass( RenderPassType passType, const GPUMeshBatch& batch ) const
    {
        if ( FrameRendererUtil::filtersBatchesByMaterial( passType ) == false )
            return true;
        const GPUShaderPermutation* pPermutation =
            ( batch._shaderPermutation == kInvalidShaderPermutation ) ? nullptr : _gpuScene.findShaderPermutation( batch._shaderPermutation );
        return FrameRendererUtil::drawsMaterialInPass( passType, pPermutation != nullptr ? &pPermutation->_listDefine : nullptr );
    }

    bool FrameRenderer::supportsViewMode( RenderViewMode viewMode ) const
    {
        // 와이어프레임 채우기가 없는 디바이스(Vulkan fillModeNonSolid 없음)는 와이어프레임 모드를 받지 않는다 — 솔리드 그림이 와이어프레임으로 통과하지 않게.
        const bool bNeedsWireframe = getRenderViewModeInfo( viewMode )._bWireframe;
        const bool bDrawable       = bNeedsWireframe == false || _pDevice == nullptr || _pDevice->getCapabilities()._bWireframeFill != SW_FALSE;
        if ( bDrawable )
            return true;
        SW_LOG_ERROR( "View mode %# needs wireframe fill, which this device does not support - the mode is not applied", getRenderViewModeInfo( viewMode )._pName );
        return false;
    }

    void FrameRenderer::setViewMode( RenderViewMode viewMode )
    {
        // 범위 밖 값을 Lit 으로 바꾸지 않는다 — 들어오는 번호는 부르는 쪽이 거른다(isRenderViewModeIndex).
        if ( viewMode >= RenderViewMode::Count )
        {
            SW_LOG_ERROR( "setViewMode: %# is not a view mode - the main output keeps its mode", static_cast<uint32>( viewMode ) );
            SW_ASSERT( viewMode < RenderViewMode::Count );
            return;
        }
        if ( supportsViewMode( viewMode ) == false )
            return;
        const uint8 previous = _viewMode.exchange( static_cast<uint8>( viewMode ), std::memory_order_relaxed );
        if ( previous == static_cast<uint8>( viewMode ) )
            return;

        // 바뀔 때만 남긴다. 뷰 모드는 화면 전체를 바꾸는 상태다. 로그가 없으면 "왜 와이어프레임인가" 를 화면만 보고 되짚어야 한다.
        SW_LOG_INFO( "Main output view mode: %#", getRenderViewModeInfo( viewMode )._pName );
    }

    RenderViewMode FrameRenderer::getViewMode() const
    {
        // setViewMode 가 범위 밖 값을 받지 않으므로 그대로 바꾼다.
        return static_cast<RenderViewMode>( _viewMode.load( std::memory_order_relaxed ) );
    }

    void FrameRenderer::setSceneViewMode( RenderViewMode viewMode )
    {
        if ( viewMode >= RenderViewMode::Count )
        {
            SW_LOG_ERROR( "setSceneViewMode: %# is not a view mode - the scene view keeps its mode", static_cast<uint32>( viewMode ) );
            SW_ASSERT( viewMode < RenderViewMode::Count );
            return;
        }
        if ( supportsViewMode( viewMode ) == false )
            return;
        const uint8 previous = _sceneViewMode.exchange( static_cast<uint8>( viewMode ), std::memory_order_relaxed );
        if ( previous == static_cast<uint8>( viewMode ) )
            return;
        SW_LOG_INFO( "Scene view mode: %#", getRenderViewModeInfo( viewMode )._pName );
    }

    RenderViewMode FrameRenderer::getSceneViewMode() const
    {
        return static_cast<RenderViewMode>( _sceneViewMode.load( std::memory_order_relaxed ) );
    }
} // namespace sw
