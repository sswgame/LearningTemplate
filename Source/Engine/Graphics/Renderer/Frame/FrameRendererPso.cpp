#include "pch.h"

#include "Core/String/StringUtil.h"
#include "Core/Task/TaskManager.h"

#include "Engine/Graphics/Material/Material.h"
#include "Engine/Graphics/Material/MaterialInstance.h"
#include "Engine/Graphics/RHI/IRHIDevice.h"
#include "Engine/Graphics/RHI/IRHIResource.h"
#include "Engine/Graphics/Renderer/Frame/FrameRenderer.h"
#include "Engine/Graphics/Renderer/Frame/FrameRendererUtil.h"

namespace sw
{
    RHIPipelineStateHandle FrameRenderer::createEnginePso( string_view shaderPath, bool bDepthTest, uint32 numRenderTargets,
                                                           const RHIFormat* pRtvFormats, bool bBlend, bool bDepthWrite )
    {
        if ( _pDevice == nullptr )
            return 0;
        RHIPipelineStateDesc desc{};
        desc._vertexShaderPath  = shaderPath;
        desc._pixelShaderPath   = shaderPath;
        desc._bEnableDepthTest  = bDepthTest ? 1 : 0;
        desc._bEnableDepthWrite = bDepthWrite ? 1 : 0;
        desc._bEnableBlend      = bBlend ? 1 : 0;
        desc._cullMode          = bDepthTest ? RHICullMode::Back : RHICullMode::None;
        desc._numRenderTargets  = numRenderTargets > 0 ? numRenderTargets : 1;
        if ( desc._numRenderTargets > kMaxColorAttachments )
            desc._numRenderTargets = kMaxColorAttachments;
        for ( uint32 rtIndex = 0; rtIndex < desc._numRenderTargets; ++rtIndex )
        {
            desc._arrRtvFormat[rtIndex] = ( pRtvFormats != nullptr ) ? pRtvFormats[rtIndex] : RHIFormat::R8G8B8A8_UNORM;
        }
        const RHIPipelineStateHandle handle = _pDevice->getResource()->createPipelineState( desc );
        registerPsoLayout( handle, desc );
        return handle;
    }

    RHIPipelineStateHandle FrameRenderer::createPsoForPassType( RenderPassType passType, string_view defaultShader,
                                                                bool bDepthTest, uint32 numRenderTargets, const RHIFormat* pRtvFormats,
                                                                bool bDefaultBlend, bool bDefaultDepthWrite,
                                                                const vector<string>* pExtraDefines )
    {
        if ( _pDevice == nullptr )
            return 0;

        const RenderGraphPassDesc* pPassDesc = findPassDescByType( passType );
        RHIPipelineStateDesc       desc{};
        desc._vertexShaderPath = ( pPassDesc != nullptr && pPassDesc->_shaderPath.empty() == false ) ? pPassDesc->_shaderPath : defaultShader;
        desc._pixelShaderPath  = desc._vertexShaderPath;
        desc._vertexEntryPoint = ( pPassDesc != nullptr && pPassDesc->_vertexEntryPoint.empty() == false )
                                   ? pPassDesc->_vertexEntryPoint
                                   : FrameRendererUtil::Entry::kVSMain;
        desc._pixelEntryPoint  = ( pPassDesc != nullptr && pPassDesc->_pixelEntryPoint.empty() == false )
                                   ? pPassDesc->_pixelEntryPoint
                                   : FrameRendererUtil::Entry::kPSMain;
        // 예전엔 pPassDesc 가 있으면 XML 값으로 **덮어썼다**. 그런데 RenderGraphPassDesc 의
        // _bEnableDepthTest/_bEnableDepthWrite 기본값이 true 이고 파이프라인 XML 은 이 항목을
        // 아예 적지 않는다 — 그래서 registerPso 가 풀스크린 패스에 명시적으로 넘긴 bDepthTest=false 가
        // 통째로 무시되고 뎁스 테스트가 켜졌다. 그 PSO 는 뎁스 포맷을 선언하는데 풀스크린 패스는
        // DSV 를 바인딩하지 않으므로 드로우마다 검증 오류가 났다.
        // 호출부의 bDepthTest 는 "이 패스가 지오메트리인가 풀스크린인가" 라는 구조적 사실이고 XML 은
        // 그 안에서의 조정이다. 그래서 덮어쓰기가 아니라 AND 다 — XML 로 끌 수는 있어도 켤 수는 없다.
        const bool bPassDepthTest  = ( pPassDesc == nullptr ) || ( pPassDesc->_bEnableDepthTest != 0 );
        const bool bPassDepthWrite = ( pPassDesc == nullptr ) || ( pPassDesc->_bEnableDepthWrite != 0 );
        desc._bEnableDepthTest     = ( bDepthTest && bPassDepthTest ) ? 1 : 0;
        desc._bEnableDepthWrite    = ( bDefaultDepthWrite && bPassDepthWrite ) ? 1 : 0;
        desc._bEnableBlend         = pPassDesc != nullptr ? ( pPassDesc->_bEnableBlend != 0 ? 1 : 0 ) : ( bDefaultBlend ? 1 : 0 );

        // 뎁스를 안 쓰는 패스(풀스크린)는 렌더패스에 DSV 를 붙이지 않는데, desc 의 기본값이 D24 라서
        // PSO 는 "뎁스 있음" 으로 만들어졌다. DX12 는 null DSV 를 PSO 뎁스 포맷이 UNKNOWN 일 때만
        // 허용하므로 그런 패스의 드로우마다 검증 오류가 났다. 뎁스 테스트가 꺼져 있으면 뎁스 쓰기도
        // 의미가 없으니 같이 끈다.
        desc._depthStencilFormat = ( desc._bEnableDepthTest != 0 ) ? constant::kDepthStencilFormat : RHIFormat::Unknown;
        if ( desc._bEnableDepthTest == 0 )
            desc._bEnableDepthWrite = 0;

        desc._cullMode = RHICullMode::Back;
        if ( pPassDesc != nullptr )
        {
            const string& cull = pPassDesc->_cullMode;
            if ( cull == "None" || cull == "none" )
                desc._cullMode = RHICullMode::None;
            else if ( cull == "Front" || cull == "front" )
                desc._cullMode = RHICullMode::Front;
            if ( pPassDesc->_listPermutation.empty() == false )
                desc._listShaderDefine = pPassDesc->_listPermutation;
        }
        else if ( bDepthTest == false )
            desc._cullMode = RHICullMode::None;

        if ( pExtraDefines != nullptr )
        {
            for ( const string& defineStr : *pExtraDefines )
            {
                bool found{ false };
                for ( const string& existing : desc._listShaderDefine )
                {
                    if ( existing == defineStr )
                    {
                        found = true;
                        break;
                    }
                }
                if ( found == false )
                    desc._listShaderDefine.push_back( defineStr );
            }
        }

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
            uint32 colorCount{ 0 };
            bool   bHasDepthOutput{ false };
            for ( const string& outName : pPassDesc->_listOutput )
            {
                if ( colorCount >= kMaxColorAttachments )
                    break;
                for ( const RenderPassAttachment& att : _pipelineResource.getDesc()._listAttachment )
                {
                    if ( att._name == outName )
                    {
                        const RHIFormat fmt = parseAttachmentFormat( att._format );
                        if ( FrameRendererUtil::isDepthFormat( fmt ) == false )
                        {
                            desc._arrRtvFormat[colorCount++] = fmt;
                        }
                        else
                        {
                            bHasDepthOutput = true;
                        }
                        break;
                    }
                }
            }
            desc._numRenderTargets = ( colorCount > 0 ) ? colorCount : ( bHasDepthOutput ? 0 : 1 );
            if ( desc._numRenderTargets == 1 && colorCount == 0 )
                desc._arrRtvFormat[0] = RHIFormat::R8G8B8A8_UNORM;
        }
        else
        {
            for ( uint32 rtIndex = 0; rtIndex < desc._numRenderTargets; ++rtIndex )
            {
                desc._arrRtvFormat[rtIndex] = RHIFormat::R8G8B8A8_UNORM;
            }
        }
        const RHIPipelineStateHandle handle = _pDevice->getResource()->createPipelineState( desc );
        registerPsoLayout( handle, desc );
        return handle;
    }

    namespace
    {
        /**
         * @brief (패스 PSO, 퍼뮤테이션, 뷰 모드) 캐시 키.
         * @details 뷰 모드가 키의 한 축이다 — 같은 머티리얼이라도 Lit 와 Wireframe 은 다른 PSO 이고,
         *          모드를 되돌리면 이미 만들어 둔 것이 다시 나온다(다시 컴파일하지 않는다).
         */
        uint64 materialPsoKey( RHIPipelineStateHandle passPso, uint64 permutationHash, RenderViewMode viewMode )
        {
            uint64 key = static_cast<uint64>( passPso ) * 0x9e3779b97f4a7c15ull;
            key ^= permutationHash + 0x9e3779b97f4a7c15ull + ( key << 6 ) + ( key >> 2 );
            key ^= ( static_cast<uint64>( viewMode ) + 1 ) * 0xff51afd7ed558ccdull;
            return key;
        }

        /**
         * @brief 뷰 모드를 PSO 디스크립터에 얹습니다. 바꾼 것이 있으면 true.
         * @details Lit 는 아무것도 하지 않는다 — 그것이 패스가 이미 만들어 둔 상태다.
         */
        bool applyViewModeToDesc( RHIPipelineStateDesc& desc, RenderViewMode viewMode )
        {
            if ( viewMode == RenderViewMode::Wireframe )
            {
                desc._fillMode = RHIFillMode::Wireframe;
                // 와이어프레임은 뒷면도 보여야 형태를 읽을 수 있다. 컬링을 남기면 뒤쪽 선이 사라져
                // 상자가 열린 것처럼 보인다 — 에디터의 와이어프레임은 관례적으로 양면이다.
                desc._cullMode = RHICullMode::None;
                return true;
            }
            if ( viewMode == RenderViewMode::Unlit )
            {
                // 조명 항을 셰이더에서 **컴파일 아웃**한다. 런타임 분기가 아니라 퍼뮤테이션이라
                // 그림자 샘플링·림 라이트까지 같이 빠진다.
                for ( const string& existing : desc._listShaderDefine )
                {
                    if ( existing == kViewModeUnlitDefine )
                        return false;
                }
                desc._listShaderDefine.push_back( kViewModeUnlitDefine );
                return true;
            }
            return false;
        }
    } // namespace

    FrameRenderer::MaterialPsoEntry FrameRenderer::createMaterialPsoVariant( RHIPipelineStateHandle passPso, RenderPassType passType,
                                                                             const GpuShaderPermutation* pPermutation,
                                                                             RenderViewMode              viewMode )
    {
        MaterialPsoEntry entry{ passPso, 0 };
        if ( passPso == 0 || _pDevice == nullptr )
            return entry;

        RHIPipelineStateDesc desc{};
        {
            // 패스가 정한 렌더 상태를 통째로 물려받는다 — 블렌드·뎁스·RT 포맷은 패스의 사실이지 머티리얼의 것이 아니다.
            std::scoped_lock<mutex> lock{ _psoLayoutMutex };
            const auto              it = _mapPsoDesc.find( passPso );
            if ( it == _mapPsoDesc.end() )
                return entry; // desc 를 모르는 PSO — 변형을 만들 근거가 없다.
            desc = it->second;
        }

        bool bChanged{ false };
        if ( pPermutation != nullptr )
        {
            // 머티리얼 셰이더를 쓰는 패스만 경로를 갈아탄다(그림자·뎁스는 자기 지오메트리 셰이더가 정본이다).
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

        // 뷰 모드는 머티리얼 퍼뮤테이션 **뒤에** 얹는다 — 순서가 반대면 머티리얼이 셰이더 경로를
        // 갈아탈 때 방금 넣은 define 이 다른 셰이더로 넘어가 의미가 달라진다.
        if ( FrameRendererUtil::appliesViewMode( passType ) && applyViewModeToDesc( desc, viewMode ) )
            bChanged = true;

        // 얹을 게 없다 = 패스 PSO 가 이미 이 머티리얼의 셰이더이고 모드도 Lit 다. 똑같은 PSO 를 하나 더 만들 이유가 없다.
        if ( bChanged == false )
            return entry;

        const RHIPipelineStateHandle handle = _pDevice->getResource()->createPipelineState( desc );
        if ( handle == 0 )
            return entry; // 컴파일 실패 — 패스 PSO 로 그린다(화면이 비는 것보다 낫다).
        registerPsoLayout( handle, desc );
        entry._pso    = handle;
        entry._bOwned = 1;
        return entry;
    }

    void FrameRenderer::ensureMaterialPsos()
    {
        if ( _pDevice == nullptr )
            return;

        // 이번 프레임에 만들 변형은 (패스, 퍼뮤테이션, 뷰 모드) 로 정해진다. 모드는 프레임 시작에 한 번만
        // 읽는다 — 기록 중에 바뀌어도 이 프레임이 고르는 PSO 는 여기서 준비한 집합 안에 있어야 한다.
        const RenderViewMode viewMode = getViewMode();

        // 이번 프레임 배치가 실제로 쓰는 퍼뮤테이션만 본다. 불투명 패스와 반투명 패스는 배치 목록이 다르므로
        // 유리 머티리얼의 변형을 그림자 패스까지 만들어 두는 낭비가 없다.
        // 퍼뮤테이션이 없는 배치(머티리얼을 안 붙인 메시)도 세어 둔다 — Lit 이 아니면 그 배치에도
        // 변형이 필요하다. 그냥 건너뛰면 머티리얼 없는 메시만 뷰 모드가 안 걸려 화면이 섞인다.
        auto collect = []( const vector<GpuMeshBatch>& listBatch, vector<uint32>& outList, bool& outHasPlain )
        {
            for ( const GpuMeshBatch& batch : listBatch )
            {
                if ( batch._shaderPermutation == GpuScene::kInvalidShaderPermutation )
                {
                    outHasPlain = true;
                    continue;
                }
                bool bFound{ false };
                for ( const uint32 existing : outList )
                {
                    if ( existing == batch._shaderPermutation )
                    {
                        bFound = true;
                        break;
                    }
                }
                if ( bFound == false )
                    outList.push_back( batch._shaderPermutation );
            }
        };

        bool           bCreatedVariant{ false };
        vector<uint32> listOpaquePermutation;
        vector<uint32> listTransparentPermutation;
        bool           bOpaqueHasPlain{ false };
        bool           bTransparentHasPlain{ false };
        collect( _gpuScene.getOpaqueBatches(), listOpaquePermutation, bOpaqueHasPlain );
        collect( _gpuScene.getTransparentBatches(), listTransparentPermutation, bTransparentHasPlain );
        if ( listOpaquePermutation.empty() && listTransparentPermutation.empty() && bOpaqueHasPlain == false &&
             bTransparentHasPlain == false )
            return;

        // 만드는 것은 락 밖에서, 넣는 것만 락 안에서 한다 — 셰이더 컴파일이 낄 수 있어 드로우 경로의
        // 조회를 붙잡으면 안 된다. 세 자리가 같은 절차를 반복하던 것을 여기 하나로 모았다.
        auto ensureVariant = [this, viewMode, &bCreatedVariant]( RHIPipelineStateHandle passPso, RenderPassType passType,
                                                                 const GpuShaderPermutation* pPermutation, uint64 permutationHash )
        {
            const uint64 key = materialPsoKey( passPso, permutationHash, viewMode );
            {
                std::scoped_lock<mutex> lock{ _materialPsoMutex };
                if ( _mapMaterialPso.find( key ) != _mapMaterialPso.end() )
                    return;
            }
            const MaterialPsoEntry entry = createMaterialPsoVariant( passPso, passType, pPermutation, viewMode );
            {
                std::scoped_lock<mutex> lock{ _materialPsoMutex };
                _mapMaterialPso.insert_or_assign( key, entry );
            }
            bCreatedVariant = true;
        };

        for ( const auto& [passType, passPso] : _mapEnginePso )
        {
            if ( passPso == 0 || FrameRendererUtil::drawsSceneMeshes( passType ) == false )
                continue;
            const bool            bTransparentPass = ( passType == RenderPassType::Transparent );
            const vector<uint32>& listPermutation  = bTransparentPass ? listTransparentPermutation : listOpaquePermutation;
            const bool            bHasPlain        = bTransparentPass ? bTransparentHasPlain : bOpaqueHasPlain;

            // 퍼뮤테이션이 없는 배치를 위한 변형 — 얹는 것이 뷰 모드뿐이다. Lit 에서는 만들 것이 없다
            // (그때는 패스 PSO 가 그대로 정답이고 psoForBatch 도 캐시를 보지 않는다).
            if ( bHasPlain && viewMode != RenderViewMode::Lit && FrameRendererUtil::appliesViewMode( passType ) )
                ensureVariant( passPso, passType, nullptr, 0 );

            for ( const uint32 permutationIndex : listPermutation )
            {
                const GpuShaderPermutation* pPermutation = _gpuScene.findShaderPermutation( permutationIndex );
                if ( pPermutation == nullptr )
                    continue;
                ensureVariant( passPso, passType, pPermutation, pPermutation->_hash );
            }
        }

        // 방금 만든 변형의 레이아웃은 셋업 때 폴백 stride 를 모으던 시점에는 없었다. 그 셰이더가
        // 다른 크기의 SwMaterialData_t 를 선언하면 그 stride 의 폴백이 없고, 머티리얼 없는 배치가
        // 그 PSO 로 그려질 때 registerMaterialBuffer 가 t9 를 **비운 채** 드로우를 낸다 —
        // Vulkan 이 초기화되지 않은 디스크립터를 읽어 디바이스를 잃는 그 경로다.
        // 여기는 아직 기록 시작 전이라 버퍼를 만들 수 있다. 새 변형을 만든 프레임에만 돈다.
        if ( bCreatedVariant )
            ensureMaterialFallbackBuffers();
    }

    bool FrameRenderer::findPsoDesc( RHIPipelineStateHandle pso, RHIPipelineStateDesc& outDesc ) const
    {
        std::scoped_lock<mutex> lock{ _psoLayoutMutex };
        const auto              it = _mapPsoDesc.find( pso );
        if ( it == _mapPsoDesc.end() )
            return false;
        outDesc = it->second;
        return true;
    }

    RHIPipelineStateHandle FrameRenderer::psoForBatch( RHIPipelineStateHandle passPso, const GpuMeshBatch& batch ) const
    {
        if ( passPso == 0 )
            return passPso;

        const RenderViewMode viewMode = getViewMode();

        uint64 permutationHash{ 0 };
        if ( batch._shaderPermutation != GpuScene::kInvalidShaderPermutation )
        {
            const GpuShaderPermutation* pPermutation = _gpuScene.findShaderPermutation( batch._shaderPermutation );
            if ( pPermutation != nullptr )
                permutationHash = pPermutation->_hash;
        }

        // 얹을 것이 하나도 없는 조합이다 — ensureMaterialPsos 도 이 키를 만들지 않는다.
        // (해시 0 은 퍼뮤테이션 없음을 뜻한다. 실제 퍼뮤테이션 해시가 0 이 되더라도 같은 자리로
        //  떨어지는데, 그 둘이 만드는 디스크립터는 어차피 같으므로 해롭지 않다.)
        if ( permutationHash == 0 && viewMode == RenderViewMode::Lit )
            return passPso;

        std::scoped_lock<mutex> lock{ _materialPsoMutex };
        const auto              it = _mapMaterialPso.find( materialPsoKey( passPso, permutationHash, viewMode ) );
        // 못 찾으면 패스 PSO 로 그린다 — ensureMaterialPsos 가 기록 전에 채우므로 정상 경로에선 늘 있다.
        return ( it != _mapMaterialPso.end() && it->second._pso != 0 ) ? it->second._pso : passPso;
    }

    void FrameRenderer::setViewMode( RenderViewMode viewMode )
    {
        if ( viewMode >= RenderViewMode::Count )
            viewMode = RenderViewMode::Lit;
        const uint8 previous = _viewMode.exchange( static_cast<uint8>( viewMode ), std::memory_order_relaxed );
        if ( previous == static_cast<uint8>( viewMode ) )
            return;

        // 바뀔 때만 남긴다. 뷰 모드는 화면 전체를 바꾸는 상태인데 바꾼 주체가 셋이다(툴바·커맨드라인·코드) —
        // 로그가 없으면 "왜 와이어프레임인가" 를 화면만 보고 되짚어야 한다.
        // 배포본에서는 SW_LOG_INFO 가 사라지므로 이름표까지 함께 컴파일 아웃한다(안 그러면 미사용 경고).
#if SW_LOG_LEVEL_COMPILED( 2 )
        static const utf8* s_arrName[] = { "Lit", "Unlit", "Wireframe" };
        SW_LOG_INFO( "뷰 모드: %#", s_arrName[static_cast<uint8>( viewMode )] );
#endif
    }

    RenderViewMode FrameRenderer::getViewMode() const
    {
        const uint8 raw = _viewMode.load( std::memory_order_relaxed );
        return ( raw < static_cast<uint8>( RenderViewMode::Count ) ) ? static_cast<RenderViewMode>( raw ) : RenderViewMode::Lit;
    }
} // namespace sw
