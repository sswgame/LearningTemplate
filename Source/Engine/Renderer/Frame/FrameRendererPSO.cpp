#include "pch.h"

#include "Core/Container/StringUtil.h"
#include "Core/Task/TaskManager.h"

#include "Engine/Common/EngineServices.h"
#include "Engine/Graphics/Material/Material.h"
#include "Engine/Graphics/Material/MaterialInstance.h"
#include "Engine/Graphics/RHI/IRHIDevice.h"
#include "Engine/Graphics/RHI/IRHIResourceFactory.h"
#include "Engine/Graphics/Shader/Compile/ShaderCompiler.h"
#include "Engine/Profiling/FrameProfiler.h"
#include "Engine/Renderer/Frame/FrameRenderer.h"
#include "Engine/Renderer/Frame/FrameRendererUtil.h"
#include "Engine/Renderer/Pipeline/RenderPassTypeInfo.h"

namespace sw
{
    namespace
    {
        /**
         * @brief 뷰 모드를 PSO 디스크립터에 얹습니다. 바꾼 것이 있으면 true 입니다.
         * @details Lit 는 아무것도 하지 않습니다. 그것이 패스가 이미 만들어 둔 상태입니다.
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
            if ( info._bAdditiveNoDepth )
            {
                // 겹쳐 그린 수를 세려면 가려진 것도 그려야 한다 — 깊이를 쓰지 않고 단색을 더한다(유니티 Overdraw 와 같다).
                // 깊이 테스트는 남긴다: 패스가 깊이를 클리어(1)하고 아무도 쓰지 않으니 모든 면이 통과한다. 테스트를 끄면 백엔드가 깊이 첨부 없는
                // PSO 로 만들어(Vulkan 렌더 패스 호환) 깊이를 건 패스에서 검증 오류가 난다. 깊이 프리패스가 있는 파이프라인에서는 보이는 면만 센다.
                // RHI 에 더하기 블렌드가 따로 없다. 프리멀티플라이(색 One / InvSrcAlpha)에 셰이더가 알파 0 을 내면 그것이 더하기다.
                desc._bEnableBlend        = 1;
                desc._bPremultipliedAlpha = SW_TRUE;
                desc._bEnableDepthWrite   = 0;
                desc._cullMode            = RHICullMode::None;
                bChanged                  = true;
            }

            // 셰이딩을 바꾸는 모드(Unlit · Normals · Depth · Overdraw)는 조명 항을 셰이더에서 **컴파일 아웃**한다. 런타임 분기가 아니라 퍼뮤테이션이다.
            // define 은 쿠커와 같은 정본(findViewModeDefine)에서 얻는다 — 쿠커가 쿠킹하지 않은 define 은 Shipping 에서 PSO 를 못 만든다.
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
         * @details 거울 변환(월드 행렬식 < 0)은 삼각형 감김을 뒤집습니다. 언리얼이 `bReverseCulling` 으로 프리미티브의 컬 모드를 뒤집는 자리입니다.
         *          셰이더의 `SV_IsFrontFace` 를 뒤집는 것으로는 안 됩니다 — 컬링은 래스터라이저가 픽셀 셰이더보다 먼저 합니다.
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
    } // namespace

    RHIPipelineStateHandle FrameRenderer::createPSOForPassType( RenderPassType passType, const RHIFormat* pRtvFormatOverride )
    {
        if ( _pDevice == nullptr )
            return 0;

        // 셰이더 · define · 기본 렌더 상태는 패스 종류의 표(RenderPassTypeInfo)가 정하고, 파이프라인 XML 의 패스 서술이 그 위를 조정한다.
        // 셰이더와 define 은 쿠커와 **같은 함수**(selectRenderPassShader)로 정한다 — 어긋나면 Shipping 에서 매니페스트 미스가 난다.
        const RenderPassTypeInfo&       info               = getRenderPassTypeInfo( passType );
        const RenderGraphPassDesc*      pPassDesc          = findPassDescByType( passType );
        const RenderPassShaderSelection shader             = selectRenderPassShader( passType, pPassDesc, engine::getEngineDefaultAssets() );
        const bool                      bDepthTest         = info.hasFlag( RenderPassTraitFlag::kDepthTest );
        const bool                      bDefaultDepthWrite = info.hasFlag( RenderPassTraitFlag::kDepthWrite );
        const bool                      bDefaultBlend      = info.hasFlag( RenderPassTraitFlag::kBlend );
        const uint32                    numRenderTargets   = info._colorTargetCount;
        const RHIFormat*                pRtvFormats        = pRtvFormatOverride != nullptr ? pRtvFormatOverride : info._pColorFormat;

        RHIPipelineStateDesc desc{};
        desc._vertexShaderPath = shader._shaderPath;
        desc._pixelShaderPath  = desc._vertexShaderPath;
        desc._vertexEntryPoint = string( resolveEntryPoint( pPassDesc != nullptr ? string_view( pPassDesc->_vertexEntryPoint ) : string_view{}, ShaderStage::Vertex ) );
        desc._pixelEntryPoint  = string( resolveEntryPoint( pPassDesc != nullptr ? string_view( pPassDesc->_pixelEntryPoint ) : string_view{}, ShaderStage::Pixel ) );
        // 표의 깊이 테스트는 "이 패스가 지오메트리인가 풀스크린인가" 라는 구조적 사실이고 XML 은 그 안에서의 조정이다.
        // 그래서 덮어쓰기가 아니라 AND 다. XML 로 끌 수는 있어도 켤 수는 없다(RenderGraphPassDesc 의 기본값이 true 라
        // 덮어쓰면 풀스크린 패스에 깊이 테스트가 켜지고, DSV 없이 그리는 드로우마다 검증 오류가 난다).
        const bool bPassDepthTest  = ( pPassDesc == nullptr ) || ( pPassDesc->_bEnableDepthTest != 0 );
        const bool bPassDepthWrite = ( pPassDesc == nullptr ) || ( pPassDesc->_bEnableDepthWrite != 0 );
        desc._bEnableDepthTest     = ( bDepthTest && bPassDepthTest ) ? 1 : 0;
        desc._bEnableDepthWrite    = ( bDefaultDepthWrite && bPassDepthWrite ) ? 1 : 0;
        desc._bEnableBlend         = pPassDesc != nullptr ? ( pPassDesc->_bEnableBlend != 0 ? 1 : 0 ) : ( bDefaultBlend ? 1 : 0 );
        // 프리멀티플라이는 셰이더의 출력 규약이라 표가 정한다(XML 이 바꿀 수 없다 — 곧은 알파로 섞으면 알파가 두 번 곱해진다).
        const bool bPremultiplied = desc._bEnableBlend != 0 && info.hasFlag( RenderPassTraitFlag::kPremultipliedAlpha );
        desc._bPremultipliedAlpha = bPremultiplied ? SW_TRUE : SW_FALSE;

        // 뎁스를 안 쓰는 패스(풀스크린)는 렌더패스에 DSV 를 붙이지 않는데, desc 의 기본값이 D24 라서
        // PSO 는 "뎁스 있음" 으로 만들어졌다. DX12 는 null DSV 를 PSO 뎁스 포맷이 UNKNOWN 일 때만
        // 허용하므로 그런 패스의 드로우마다 검증 오류가 났다. 뎁스 테스트가 꺼져 있으면 뎁스 쓰기도
        // 의미가 없으니 같이 끈다.
        desc._depthStencilFormat = ( desc._bEnableDepthTest != 0 ) ? constant::kDepthStencilFormat : RHIFormat::Unknown;
        if ( desc._bEnableDepthTest == 0 )
            desc._bEnableDepthWrite = 0;

        // 컬 모드도 뎁스와 같은 구조다. **패스가 무엇을 그리는가**가 먼저고 XML 은 그 안의 조정이다.
        // 풀스크린 패스는 SV_VertexID 로 삼각형 하나를 만들어 화면을 덮는다. 그 삼각형의 와인딩은
        // 셰이더가 정한 것이고 "앞/뒤" 라는 뜻이 없으므로, 컬링을 걸면 화면이 통째로 비거나 그대로
        // 나오거나 둘 중 하나다. 고를 값이 아니다.
        //
        // 주의: 이 기본값은 XML 에 패스를 적어 둔 경우에도 풀스크린 패스에 적용돼야 한다. XML 의 `Back` 을
        // 그대로 따르면 풀스크린 패스(Shading · SSAO · Bloom · Outline · TAA · Tonemap · Present)가 오류도
        // 경고도 없이 아무것도 그리지 않아 화면이 배경색뿐이다.
        // 뒤집은 껍질 외곽선은 앞면을 컬링한다(표의 kCullFront) — XML 의 "Back" 은 그 기본값을 바꾸지 않는다.
        const bool        bFullscreenPass = ( FrameRendererUtil::drawsSceneMeshes( passType ) == false );
        const RHICullMode meshCullMode    = info.hasFlag( RenderPassTraitFlag::kCullFront ) ? RHICullMode::Front : RHICullMode::Back;
        desc._cullMode                    = bFullscreenPass ? RHICullMode::None : meshCullMode;
        if ( pPassDesc != nullptr )
        {
            if ( bFullscreenPass == false )
            {
                const string& cull = pPassDesc->_cullMode;
                if ( cull == "None" || cull == "none" )
                    desc._cullMode = RHICullMode::None;
                else if ( cull == "Front" || cull == "front" )
                    desc._cullMode = RHICullMode::Front;
            }
        }
        desc._listShaderDefine = shader._listDefine;

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
            // 컬러를 못 찾으면 부르는 쪽이 넘긴 수로 물러난다(못 찾은 자리의 포맷은 desc 기본값 그대로).
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

        // 컬러 출력이 없는 패스(그림자 · 뎁스 프리패스)는 픽셀 스테이지가 없다. 셰이더에 PSMain 이 있어도 붙이지
        // 않는다. 주의: 여기서 정하지 않으면 백엔드마다 판단이 갈리고, 그 위에 머티리얼 define 을 얹은 변형
        // (createMaterialPSOVariant 는 이 desc 를 그대로 물려받는다)은 쿠커가 쿠킹하지 않아 Shipping 에서 매니페스트 미스로
        // 떨어진다. 쿠커 쪽 같은 규칙은 FrameRendererUtil::hasPixelStage 다.
        if ( desc._numRenderTargets == 0 )
        {
            desc._pixelShaderPath.clear();
            desc._pixelEntryPoint.clear();
        }
        const RHIPipelineStateHandle handle = _pDevice->getResourceFactory()->createPipelineState( desc );
        registerPSOLayout( handle, desc );
        return handle;
    }

    RenderPSOCache::MaterialPSOEntry FrameRenderer::createMaterialPSOVariant( RHIPipelineStateHandle passPSO, RenderPassType passType,
                                                                              const GPUShaderPermutation* pPermutation,
                                                                              RenderViewMode viewMode, bool bReverseCulling )
    {
        RenderPSOCache::MaterialPSOEntry entry{ passPSO, 0 };
        if ( passPSO == 0 || _pDevice == nullptr )
            return entry;

        // 패스가 정한 렌더 상태를 통째로 물려받는다. 블렌드 · 뎁스 · RT 포맷은 패스의 사실이지 머티리얼의 것이 아니다.
        RHIPipelineStateDesc desc{};
        if ( _psoCache.findDesc( passPSO, desc ) == false )
            return entry; // desc 를 모르는 PSO 다. 변형을 만들 근거가 없다.

        bool bChanged{ false };
        if ( pPermutation != nullptr )
        {
            // 양면 머티리얼은 후면 컬링을 끈다(언리얼 머티리얼의 Two Sided). 앞면 컬링 패스(외곽선 껍질)는 그대로다 — 껍질은 뒤집어 그려야 외곽선이다.
            const bool bTwoSided = std::find( pPermutation->_listDefine.begin(), pPermutation->_listDefine.end(), string( kMaterialTwoSidedDefine ) ) !=
                                   pPermutation->_listDefine.end();
            if ( bTwoSided && desc._cullMode == RHICullMode::Back )
            {
                desc._cullMode = RHICullMode::None;
                bChanged       = true;
            }
            // 머티리얼 셰이더를 쓰는 패스만 경로를 갈아탄다(그림자 · 뎁스는 자기 지오메트리 셰이더가 기준이다).
            if ( FrameRendererUtil::usesMaterialShader( passType ) && pPermutation->_shaderPath.empty() == false &&
                 pPermutation->_shaderPath != desc._vertexShaderPath )
            {
                desc._vertexShaderPath = pPermutation->_shaderPath;
                desc._pixelShaderPath  = pPermutation->_shaderPath;
                bChanged               = true;
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

        // 뷰 모드는 머티리얼 퍼뮤테이션 **뒤에** 얹는다. 순서가 반대면 머티리얼이 셰이더 경로를
        // 갈아탈 때 방금 넣은 define 이 다른 셰이더로 넘어가 의미가 달라진다.
        if ( FrameRendererUtil::appliesViewMode( passType ) && applyViewModeToDesc( desc, viewMode ) )
            bChanged = true;

        // 거울 변환 배치는 감김이 뒤집혀 있다. 패스의 컬 모드를 맞바꿔야 같은 면(바깥 면)이 남는다. 와이어프레임처럼 컬링이 없는
        // 패스(None)는 바꿀 것이 없다. 뷰 모드 뒤에 둔다 — 뷰 모드가 컬링을 끄면 뒤집을 것도 없다.
        if ( bReverseCulling && reverseCullMode( desc ) )
            bChanged = true;

        // 얹을 것이 없다 = 패스 PSO 가 이미 이 머티리얼의 셰이더이고 모드도 Lit 이고 컬 모드도 그대로다. 똑같은 PSO 를 하나 더 만들 이유가 없다.
        if ( bChanged == false )
            return entry;

        const RHIPipelineStateHandle handle = _pDevice->getResourceFactory()->createPipelineState( desc );
        if ( handle == 0 )
            return entry; // 컴파일 실패다. 패스 PSO 로 그린다(화면이 비는 것보다 낫다).
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

        // 이번 프레임에 만들 변형은 (패스, 퍼뮤테이션, 뷰 모드)로 정해진다. 보기 모드는 뷰마다의 설정이라 이번 프레임에 그리는 뷰들의 모드를 모은다.
        // 뷰 설정은 프레임 시작에 정해지고 기록 중에 바뀌지 않으므로, 드로우가 고르는 PSO 는 여기서 준비한 집합 안에 있다.
        uint32 viewModeMask = 1u << static_cast<uint32>( _mainView._settings._viewMode );
        for ( const unique_ptr<ViewTarget>& pView : _listExtraView )
        {
            if ( pView->_bRenderThisFrame == SW_TRUE )
                viewModeMask |= 1u << static_cast<uint32>( pView->_settings._viewMode );
        }

        // 이번 프레임 배치가 실제로 쓰는 (퍼뮤테이션, 컬 반전) 조합만 본다. 불투명 패스와 반투명 패스는 배치 목록이 다르므로
        // 유리 머티리얼의 변형을 그림자 패스까지 만들어 두는 낭비가 없다.
        // 퍼뮤테이션이 없는 배치(머티리얼을 안 붙인 메시)도 모은다. Lit 이 아니거나 거울 배치면 그 배치에도
        // 변형이 필요하다. 그냥 건너뛰면 머티리얼 없는 메시만 뷰 모드 · 컬 반전이 안 걸려 화면이 섞인다.
        auto collect = []( const vector<GPUMeshBatch>& listBatch, vector<MaterialPSORequest>& outList )
        {
            for ( const GPUMeshBatch& batch : listBatch )
            {
                bool bFound{ false };
                for ( const MaterialPSORequest& existing : outList )
                {
                    if ( existing._shaderPermutation == batch._shaderPermutation && existing._bReverseCulling == batch._bReverseCulling )
                    {
                        bFound = true;
                        break;
                    }
                }
                if ( bFound == false )
                    outList.push_back( MaterialPSORequest{ batch._shaderPermutation, batch._bReverseCulling } );
            }
        };

        bool                        bCreatedVariant{ false };
        vector<MaterialPSORequest>& listOpaqueRequest      = _listOpaquePSORequestScratch;
        vector<MaterialPSORequest>& listTransparentRequest = _listTransparentPSORequestScratch;
        listOpaqueRequest.clear();
        listTransparentRequest.clear();
        collect( _gpuScene.getOpaqueBatches(), listOpaqueRequest );
        collect( _gpuScene.getTransparentBatches(), listTransparentRequest );
        if ( listOpaqueRequest.empty() && listTransparentRequest.empty() )
            return;

        // 만드는 것은 락 밖에서, 넣는 것만 락 안에서 한다. 셰이더 컴파일이 낄 수 있어 드로우 경로의
        // 조회를 붙잡으면 안 된다. 세 자리가 같은 절차를 반복하던 것을 여기 하나로 모았다.
        auto ensureVariant = [this, &bCreatedVariant]( RHIPipelineStateHandle passPSO, RenderPassType passType, const GPUShaderPermutation* pPermutation,
                                                       uint64 permutationHash, bool bReverseCulling, RenderViewMode viewMode )
        {
            const uint64 key = RenderPSOCache::materialPSOKey( passPSO, permutationHash, viewMode, bReverseCulling );
            if ( _psoCache.hasMaterialPSO( key ) )
                return;
            const RenderPSOCache::MaterialPSOEntry entry = createMaterialPSOVariant( passPSO, passType, pPermutation, viewMode, bReverseCulling );
            _psoCache.setMaterialPSO( key, entry );
            bCreatedVariant = true;
        };

        for ( const auto& [passType, passPSO] : _psoCache.getEnginePSOs() )
        {
            if ( passPSO == 0 || FrameRendererUtil::drawsSceneMeshes( passType ) == false )
                continue;
            const bool                        bTransparentPass = getRenderPassTypeInfo( passType ).hasFlag( RenderPassTraitFlag::kDrawsTransparentBatch );
            const vector<MaterialPSORequest>& listRequest      = bTransparentPass ? listTransparentRequest : listOpaqueRequest;

            for ( const MaterialPSORequest& request : listRequest )
            {
                const bool bReverseCulling = ( request._bReverseCulling != SW_FALSE );
                // 이 패스가 그리지 않는 배치(외곽선을 켜지 않은 머티리얼)의 변형은 만들지 않는다 — 쿠커도 그것을 쿠킹하지 않는다.
                const GPUShaderPermutation* pRequestPermutation =
                    ( request._shaderPermutation == kInvalidShaderPermutation ) ? nullptr : _gpuScene.findShaderPermutation( request._shaderPermutation );
                if ( FrameRendererUtil::drawsMaterialInPass( passType, pRequestPermutation != nullptr ? &pRequestPermutation->_listDefine : nullptr ) == false )
                    continue;
                for ( uint32 modeIndex = 0; modeIndex < static_cast<uint32>( RenderViewMode::Count ); ++modeIndex )
                {
                    if ( ( viewModeMask & ( 1u << modeIndex ) ) == 0 )
                        continue;
                    const RenderViewMode viewMode = static_cast<RenderViewMode>( modeIndex );
                    if ( request._shaderPermutation == kInvalidShaderPermutation )
                    {
                        // 퍼뮤테이션이 없는 배치를 위한 변형. 얹는 것이 뷰 모드 · 컬 반전뿐이다. Lit 이고 거울이 아니면 만들 것이 없다
                        // (그때는 패스 PSO 가 그대로 정답이고 psoForBatch 도 캐시를 보지 않는다).
                        const bool bAppliesViewMode = ( viewMode != RenderViewMode::Lit ) && FrameRendererUtil::appliesViewMode( passType );
                        if ( bAppliesViewMode || bReverseCulling )
                            ensureVariant( passPSO, passType, nullptr, 0, bReverseCulling, viewMode );
                        continue;
                    }
                    if ( pRequestPermutation == nullptr )
                        continue;
                    ensureVariant( passPSO, passType, pRequestPermutation, pRequestPermutation->_hash, bReverseCulling, viewMode );
                }
            }
        }

        // 방금 만든 변형의 레이아웃은 셋업 때 폴백 stride 를 모으던 시점에는 없었다. 그 셰이더가
        // 다른 크기의 SwMaterialData 를 선언하면 그 stride 의 폴백이 없고, 머티리얼 없는 배치가
        // 그 PSO 로 그려질 때 registerMaterialBuffer 가 t9 를 **비운 채** 드로우를 낸다.
        // Vulkan 이 초기화되지 않은 디스크립터를 읽어 디바이스를 잃는 그 경로다.
        // 여기는 아직 기록 시작 전이라 버퍼를 만들 수 있다. 새 변형을 만든 프레임에만 돈다.
        if ( bCreatedVariant )
            ensureMaterialFallbackBuffers();
    }

    bool FrameRenderer::findPSODesc( RHIPipelineStateHandle pso, RHIPipelineStateDesc& outDesc ) const
    {
        return _psoCache.findDesc( pso, outDesc );
    }

    RHIPipelineStateHandle FrameRenderer::psoForBatch( RHIPipelineStateHandle passPSO, const GPUMeshBatch& batch ) const
    {
        if ( passPSO == 0 )
            return passPSO;

        // 보기 모드는 지금 그리는 뷰의 설정이다(씬 뷰만 Normals 이고 게임 뷰는 Lit 일 수 있다).
        const RenderViewMode viewMode = _pActiveView->_settings._viewMode;

        uint64 permutationHash{ 0 };
        if ( batch._shaderPermutation != kInvalidShaderPermutation )
        {
            const GPUShaderPermutation* pPermutation = _gpuScene.findShaderPermutation( batch._shaderPermutation );
            if ( pPermutation != nullptr )
                permutationHash = pPermutation->_hash;
        }

        // 얹을 것이 하나도 없는 조합이다. ensureMaterialPSOs 도 이 키를 만들지 않는다.
        // (해시 0 은 퍼뮤테이션 없음을 뜻한다. 실제 퍼뮤테이션 해시가 0 이 되더라도 같은 자리로
        //  떨어지는데, 그 둘이 만드는 디스크립터는 어차피 같으므로 해롭지 않다.)
        const bool bReverseCulling = ( batch._bReverseCulling != SW_FALSE );
        if ( permutationHash == 0 && viewMode == RenderViewMode::Lit && bReverseCulling == false )
            return passPSO;

        // 못 찾으면 패스 PSO 로 그린다. ensureMaterialPSOs 가 기록 전에 채우므로 정상 경로에서는 늘 있다.
        const RHIPipelineStateHandle variant =
            _psoCache.findMaterialPSO( RenderPSOCache::materialPSOKey( passPSO, permutationHash, viewMode, bReverseCulling ) );
        return ( variant != 0 ) ? variant : passPSO;
    }

    bool FrameRenderer::drawsBatchInPass( RenderPassType passType, const GPUMeshBatch& batch ) const
    {
        if ( getRenderPassTypeInfo( passType )._pRequiredMaterialDefine == nullptr )
            return true;
        const GPUShaderPermutation* pPermutation =
            ( batch._shaderPermutation == kInvalidShaderPermutation ) ? nullptr : _gpuScene.findShaderPermutation( batch._shaderPermutation );
        return FrameRendererUtil::drawsMaterialInPass( passType, pPermutation != nullptr ? &pPermutation->_listDefine : nullptr );
    }

    void FrameRenderer::setViewMode( RenderViewMode viewMode )
    {
        if ( viewMode >= RenderViewMode::Count )
            viewMode = RenderViewMode::Lit;
        const uint8 previous = _viewMode.exchange( static_cast<uint8>( viewMode ), std::memory_order_relaxed );
        if ( previous == static_cast<uint8>( viewMode ) )
            return;

        // 바뀔 때만 남긴다. 뷰 모드는 화면 전체를 바꾸는 상태다 — 로그가 없으면 "왜 와이어프레임인가" 를 화면만 보고 되짚어야 한다.
        SW_LOG_INFO( "Main output view mode: %#", getRenderViewModeInfo( viewMode )._pName );
    }

    RenderViewMode FrameRenderer::getViewMode() const
    {
        const uint8 raw = _viewMode.load( std::memory_order_relaxed );
        return ( raw < static_cast<uint8>( RenderViewMode::Count ) ) ? static_cast<RenderViewMode>( raw ) : RenderViewMode::Lit;
    }

    void FrameRenderer::setSceneViewMode( RenderViewMode viewMode )
    {
        if ( viewMode >= RenderViewMode::Count )
            viewMode = RenderViewMode::Lit;
        const uint8 previous = _sceneViewMode.exchange( static_cast<uint8>( viewMode ), std::memory_order_relaxed );
        if ( previous == static_cast<uint8>( viewMode ) )
            return;
        SW_LOG_INFO( "Scene view mode: %#", getRenderViewModeInfo( viewMode )._pName );
    }

    RenderViewMode FrameRenderer::getSceneViewMode() const
    {
        const uint8 raw = _sceneViewMode.load( std::memory_order_relaxed );
        return ( raw < static_cast<uint8>( RenderViewMode::Count ) ) ? static_cast<RenderViewMode>( raw ) : RenderViewMode::Lit;
    }
} // namespace sw
