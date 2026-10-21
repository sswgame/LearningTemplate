#include "pch.h"

#include "Engine/Renderer/Pipeline/RenderPassTypeInfo.h"

#include "Core/Container/VectorUtil.h"

namespace sw
{
    namespace
    {
        /// G버퍼 MRT 의 고정 포맷입니다: [0] 알베도(알파 = 셰이딩 모델), [1] 노멀, [2] 머티리얼(r 금속도, g 거칠기 — binding.hlsli swEncodeGBufferMaterial).
        constexpr RHIFormat kArrGBufferColorFormat[] = { RHIFormat::R8G8B8A8_UNORM, RHIFormat::R16G16B16A16_FLOAT, RHIFormat::R8G8B8A8_UNORM };

        /// 깊이 히스토리의 고정 포맷입니다(깊이 하나 — 컴퓨트가 RW 텍스처로 읽는다. R32 실수는 네 백엔드 모두 형식 있는 UAV 읽기가 된다).
        constexpr RHIFormat kArrDepthHistoryFormat[] = { RHIFormat::R32_FLOAT };

        using Role = RenderPassInputRole;
        using Flag = RenderPassTraitFlag;

        /// 씬 메시를 그려 화면 색을 만드는 지오메트리 패스의 공통 플래그입니다.
        constexpr uint32 kSceneColorPassFlags = Flag::kDrawsSceneMeshes | Flag::kUsesMaterialShader | Flag::kAppliesViewMode | Flag::kDrawsDeclaredColor;

        struct RenderPassTypeInfoInternal
        {
            /** @brief 필수 · 선택 역할 목록으로 입력 계약을 만듭니다. */
            static constexpr RenderPassInputSignature makeContract( std::initializer_list<Role> listRequired, std::initializer_list<Role> listOptional )
            {
                RenderPassInputSignature contract{};
                for ( const Role role : listRequired )
                {
                    contract._arrRequired[contract._requiredCount++] = role;
                }
                for ( const Role role : listOptional )
                {
                    contract._arrOptional[contract._optionalCount++] = role;
                }
                return contract;
            }

            /**
             * @brief 패스 종류 하나의 줄입니다. 열거자를 더하고 case 를 빠뜨리면 -Wswitch-enum 이 알립니다(default 는 Invalid 줄이다). 경우마다 표 한 줄인 디스패처라 길어도 나누지 않는다.
             */
            static constexpr RenderPassTypeInfo makeInfo( RenderPassType type )
            {
                RenderPassTypeInfo info{};
                info._type = type;
                switch ( type )
                {
                    case RenderPassType::Invalid:
                    {
                        break;
                    }
                    case RenderPassType::Shadow:
                    {
                        // 패스 define 은 머티리얼 셰이더로 그리는 배치(알파 컷오프 · 정점 변형)가 빛 행렬 퍼뮤테이션을 고르는 데 쓴다.
                        info._pDefaultShader   = &EngineDefaultAssets::_shaderShadowDepth;
                        info._pPassDefine      = kPassShadowDefine;
                        info._colorTargetCount = 0;
                        info._flags            = Flag::kDepthTest | Flag::kDepthWrite | Flag::kDepthOnly | Flag::kDrawsSceneMeshes;
                        break;
                    }
                    case RenderPassType::DepthPrepass:
                    {
                        // 그림자와 같은 셰이더 파일을 카메라 행렬로 그린다(패스 define 이 행렬을 고른다).
                        info._pDefaultShader   = &EngineDefaultAssets::_shaderShadowDepth;
                        info._pPassDefine      = kPassDepthPrepassDefine;
                        info._colorTargetCount = 0;
                        info._psoFallbackType  = RenderPassType::Shadow;
                        info._flags            = Flag::kDepthTest | Flag::kDepthWrite | Flag::kDepthOnly | Flag::kDrawsSceneMeshes;
                        break;
                    }
                    case RenderPassType::ForwardOpaque:
                    {
                        info._pDefaultShader = &EngineDefaultAssets::_shaderForwardLit;
                        info._flags          = Flag::kDepthTest | Flag::kDepthWrite | kSceneColorPassFlags;
                        break;
                    }
                    case RenderPassType::GBuffer:
                    {
                        // define 은 이 PSO 를 물려받는 머티리얼 변형까지 MRT 서명으로 컴파일되게 한다.
                        info._pDefaultShader          = &EngineDefaultAssets::_shaderGBuffer;
                        info._pPassDefine             = kPassGBufferDefine;
                        info._pExcludedMaterialDefine = kMaterialForwardOnlyDefine;
                        info._pColorFormat            = kArrGBufferColorFormat;
                        info._colorTargetCount        = static_cast<uint32>( sizeof( kArrGBufferColorFormat ) / sizeof( kArrGBufferColorFormat[0] ) );
                        info._flags                   = Flag::kDepthTest | Flag::kDepthWrite | kSceneColorPassFlags;
                        break;
                    }
                    case RenderPassType::Lighting:
                    {
                        info._pDefaultShader = &EngineDefaultAssets::_shaderDeferredLighting;
                        info._inputContract  = makeContract( { Role::GBufferAlbedo, Role::GBufferNormal, Role::GBufferMaterial, Role::SceneDepth }, { Role::ShadowMap } );
                        info._flags          = Flag::kGenericFullscreen | Flag::kDrawsDeclaredColor | Flag::kHasInputContract;
                        break;
                    }
                    case RenderPassType::Transparent:
                    {
                        // 블렌드를 켜고 깊이 쓰기를 끄는 것까지가 패스의 몫이다. 어떤 퍼뮤테이션으로 그릴지는 머티리얼이 정한다.
                        info._pDefaultShader  = &EngineDefaultAssets::_shaderForwardLit;
                        info._psoFallbackType = RenderPassType::ForwardOpaque;
                        info._flags           = Flag::kDepthTest | Flag::kBlend | Flag::kDrawsTransparentBatch | kSceneColorPassFlags;
                        break;
                    }
                    case RenderPassType::SSAO:
                    {
                        // 클리어(가림 없음 = 흰색)는 AO 첨부의 선언이다.
                        info._pDefaultShader = &EngineDefaultAssets::_shaderSsao;
                        info._inputContract  = makeContract( { Role::GBufferNormal, Role::SceneDepth }, {} );
                        info._flags          = Flag::kGenericFullscreen | Flag::kDrawsDeclaredColor | Flag::kHasInputContract | Flag::kSkipsWithoutPostProcess;
                        break;
                    }
                    case RenderPassType::Bloom:
                    {
                        // BloomBright(반해상도 밝은 부분)를 선언하면 그것을 업샘플해 더하고, 없으면 원본에서 밝은 부분을 직접 구한다.
                        info._pDefaultShader = &EngineDefaultAssets::_shaderPostBloom;
                        info._inputContract  = makeContract( { Role::SourceColor }, { Role::AmbientOcclusion, Role::BloomBright } );
                        info._flags          = Flag::kGenericFullscreen | Flag::kDrawsDeclaredColor | Flag::kHasInputContract | Flag::kSkipsWithoutPostProcess;
                        break;
                    }
                    case RenderPassType::BloomDownsample:
                    {
                        // 출력은 반해상도 첨부다(파이프라인이 `_resolutionDivisor` 로 정한다). 후처리를 끈 뷰에서는 셰이더가 0 을 낸다 —
                        // 원본을 복사하는 kSkipsWithoutPostProcess 는 크기가 다른 출력에 맞지 않는다.
                        info._pDefaultShader = &EngineDefaultAssets::_shaderPostBloom;
                        info._pPassDefine    = kPassBloomDownsampleDefine;
                        info._inputContract  = makeContract( { Role::SourceColor }, {} );
                        info._flags          = Flag::kGenericFullscreen | Flag::kDrawsDeclaredColor | Flag::kHasInputContract;
                        break;
                    }
                    case RenderPassType::Outline:
                    {
                        info._pDefaultShader = &EngineDefaultAssets::_shaderPostOutline;
                        info._inputContract  = makeContract( { Role::SourceColor, Role::SceneDepth }, {} );
                        info._flags          = Flag::kGenericFullscreen | Flag::kDrawsDeclaredColor | Flag::kHasInputContract | Flag::kSkipsWithoutPostProcess;
                        break;
                    }
                    case RenderPassType::TAA:
                    {
                        info._pDefaultShader = &EngineDefaultAssets::_shaderTaa;
                        info._inputContract  = makeContract( { Role::SourceColor }, {} );
                        info._flags          = Flag::kHasInputContract | Flag::kDrawsDeclaredColor;
                        break;
                    }
                    case RenderPassType::Tonemap:
                    {
                        // 톤매핑 PSO 가 없어도 그림은 나가야 하므로 Present(단순 블릿)로 대신한다.
                        info._pDefaultShader  = &EngineDefaultAssets::_shaderTonemap;
                        info._psoFallbackType = RenderPassType::Present;
                        info._inputContract   = makeContract( { Role::SourceColor }, {} );
                        info._flags           = Flag::kGenericFullscreen | Flag::kDrawsDeclaredColor | Flag::kHasInputContract;
                        break;
                    }
                    case RenderPassType::Present:
                    {
                        // 화면에 낼 색(SourceColor)은 필수 입력이다 — 옛 코드는 선언이 없으면 "가장 나중 컬러" 를 이름 후보로 짐작했다(resolvePresentSource).
                        // 후처리 체인(postchain.hlsl)을 겸하면 깊이 · AO 를 읽는다.
                        info._pDefaultShader = &EngineDefaultAssets::_shaderFullscreenBlit;
                        info._inputContract  = makeContract( { Role::SourceColor }, { Role::SceneDepth, Role::AmbientOcclusion } );
                        info._flags          = Flag::kHasInputContract;
                        break;
                    }
                    case RenderPassType::MeshOutline:
                    {
                        // 뒤집은 껍질 — 머티리얼 셰이더(toon.hlsl)의 외곽선 퍼뮤테이션으로, 외곽선을 켠 머티리얼의 배치만 앞면을 컬링해 그린다.
                        // 깊이는 테스트 · 쓰기 모두 한다(뒤에 오는 반투명이 외곽선에 가려진다). 뷰 모드는 받지 않는다(와이어프레임에서는 패스가 빠진다).
                        info._pDefaultShader          = &EngineDefaultAssets::_shaderToon;
                        info._pPassDefine             = kPassMeshOutlineDefine;
                        info._pRequiredMaterialDefine = kMaterialOutlineDefine;
                        info._flags                   = Flag::kDepthTest | Flag::kDepthWrite | Flag::kDrawsSceneMeshes | Flag::kUsesMaterialShader | Flag::kCullFront | Flag::kDrawsDeclaredColor;
                        break;
                    }
                    case RenderPassType::Sky:
                    {
                        // 하늘 — 깊이 1 의 풀스크린 삼각형을 깊이 테스트(LessEqual)로 그려 빈 곳만 칠한다. 깊이는 쓰지 않는다(전용 실행: 하늘이 없거나 표면 값 보기면 건너뛴다).
                        // 하늘 값은 패스 상수(g_Sky*)로 받는다. 입력은 그려 둔 장면 색 · 깊이 뒤에 서게 하는 순서 선언이라 둘 다 선택이다(셰이더는 읽지 않는다).
                        info._pDefaultShader = &EngineDefaultAssets::_shaderSky;
                        info._inputContract  = makeContract( {}, { Role::SourceColor, Role::SceneDepth } );
                        info._flags          = Flag::kDepthTest | Flag::kHasInputContract | Flag::kDrawsDeclaredColor;
                        break;
                    }
                    case RenderPassType::Canvas:
                    {
                        // 화면 2D — 사각형 구조버퍼(t11)를 인스턴스로 그린다(전용 실행: CanvasRenderer). 깊이 없음 · 프리멀티플라이 블렌드.
                        // 입력 계약은 비어 있다 — 첨부를 읽지 않으므로 선언한 입력은 검증 오류다.
                        info._pDefaultShader = &EngineDefaultAssets::_shaderCanvas;
                        info._inputContract  = makeContract( {}, {} );
                        info._flags          = Flag::kBlend | Flag::kPremultipliedAlpha | Flag::kHasInputContract;
                        break;
                    }
                    case RenderPassType::ForwardOnlyOpaque:
                    {
                        // 디퍼드의 G버퍼가 뺀 배치(MATERIAL_FORWARD_ONLY)만 조명 패스 뒤에 포워드로 그린다. 깊이는 G버퍼가 쓴 장면 깊이에 테스트 · 쓰기 한다.
                        info._pDefaultShader          = &EngineDefaultAssets::_shaderForwardLit;
                        info._pRequiredMaterialDefine = kMaterialForwardOnlyDefine;
                        info._flags                   = Flag::kDepthTest | Flag::kDepthWrite | kSceneColorPassFlags;
                        break;
                    }
                    case RenderPassType::Module:
                    {
                        // 모듈이 등록한 풀스크린 패스 — 기본 셰이더가 없다(패스의 `_shaderPath` 가 필수, 검증). PSO 는 패스 이름마다 만든다(createModulePassPSOs).
                        // 입력은 모두 선택이다. 무엇을 읽는지는 모듈의 셰이더가 정하고, 계약은 역할 이름(`g_<Role>Index`)까지만 맡는다.
                        info._inputContract = makeContract( {}, { Role::SourceColor, Role::SceneDepth, Role::GBufferNormal, Role::AmbientOcclusion } );
                        info._flags         = Flag::kGenericFullscreen | Flag::kDrawsDeclaredColor | Flag::kHasInputContract;
                        break;
                    }
                    case RenderPassType::ForwardOpaqueNoDepthWrite:
                    {
                        // 깊이 프리패스가 돈 프레임의 불투명 패스다. 깊이는 테스트만 한다.
                        info._pDefaultShader = &EngineDefaultAssets::_shaderForwardLit;
                        info._flags          = Flag::kDepthTest | kSceneColorPassFlags;
                        break;
                    }
                    case RenderPassType::GPUCull:
                    {
                        info._pDefaultShader = &EngineDefaultAssets::_shaderGPUCull;
                        info._flags          = Flag::kCompute | Flag::kRequiresGPUCulling;
                        break;
                    }
                    case RenderPassType::InstanceAnimation:
                    {
                        // 인스턴스 버퍼 UAV 하나만 쓰므로 컬링 능력(간접 인자 제약)과 무관하다.
                        info._pDefaultShader = &EngineDefaultAssets::_shaderInstanceAnimation;
                        info._flags          = Flag::kCompute;
                        break;
                    }
                    case RenderPassType::InstanceSort:
                    {
                        // 컬링이 압축한 가시 목록을 정렬하므로 컬링과 같은 조건이다.
                        info._pDefaultShader = &EngineDefaultAssets::_shaderInstanceSort;
                        info._flags          = Flag::kCompute | Flag::kRequiresGPUCulling;
                        break;
                    }
                    case RenderPassType::MeshMorph:
                    {
                        info._pDefaultShader = &EngineDefaultAssets::_shaderMeshMorph;
                        info._flags          = Flag::kCompute;
                        break;
                    }
                    case RenderPassType::MeshSkin:
                    {
                        info._pDefaultShader = &EngineDefaultAssets::_shaderMeshSkin;
                        info._flags          = Flag::kCompute;
                        break;
                    }
                    case RenderPassType::LightGrid:
                    {
                        // 구조 버퍼 UAV 와 그룹 공유 메모리 원자 연산만 쓴다 — 간접 인자 능력과 무관하다(DX11 도 돈다).
                        info._pDefaultShader = &EngineDefaultAssets::_shaderLightGrid;
                        info._flags          = Flag::kCompute;
                        break;
                    }
                    case RenderPassType::VfxSimulation:
                    {
                        // 입자 상태 · 렌더 입력은 구조 버퍼 UAV, 간접 인자는 따로 둔 버퍼(DX11 은 RAW UAV)라 구조 버퍼와 간접 인자 겸용 능력이 필요 없다(DX11 도 돈다).
                        info._pDefaultShader = &EngineDefaultAssets::_shaderVfxSimulation;
                        info._flags          = Flag::kCompute;
                        break;
                    }
                    case RenderPassType::VfxMeshInstance:
                    {
                        // 인스턴스 버퍼 UAV 에 쓰고 구조 버퍼만 읽는다 — 간접 인자 능력과 무관하다(컬링이 없는 DX11 은 CPU 개수로 모두 그린다).
                        info._pDefaultShader = &EngineDefaultAssets::_shaderVfxMeshInstance;
                        info._flags          = Flag::kCompute;
                        break;
                    }
                    case RenderPassType::DepthHistory:
                    {
                        // 깊이 없이 R32 실수 RT 하나에 풀스크린 삼각형을 그린다(입력은 C++ 가 직접 건다 — 파이프라인 패스가 아니라 입력 계약이 없다).
                        info._pDefaultShader   = &EngineDefaultAssets::_shaderDepthHistory;
                        info._pColorFormat     = kArrDepthHistoryFormat;
                        info._colorTargetCount = 1;
                        break;
                    }
                }
                return info;
            }

            struct InfoTable
            {
                RenderPassTypeInfo _arrRow[kRenderPassTypeCount];
            };

            static constexpr InfoTable makeTable()
            {
                InfoTable table{};
                for ( uint32 index = 0; index < kRenderPassTypeCount; ++index )
                {
                    table._arrRow[index] = makeInfo( static_cast<RenderPassType>( index ) );
                }
                return table;
            }

            /** @brief 줄 사이의 규칙을 검사합니다. 하나라도 어기면 false 입니다(static_assert 가 봅니다). */
            static constexpr bool isTableConsistent( const InfoTable& table )
            {
                for ( uint32 index = 0; index < kRenderPassTypeCount; ++index )
                {
                    const RenderPassTypeInfo& row = table._arrRow[index];
                    if ( static_cast<uint32>( row._type ) != index )
                        return false;
                    // 깊이만 쓰는 패스는 컬러 RT 가 없다. 고정 포맷은 RT 수만큼 있어야 한다.
                    if ( row.hasFlag( Flag::kDepthOnly ) && row._colorTargetCount != 0 )
                        return false;
                    if ( row._pColorFormat != nullptr && row._colorTargetCount == 0 )
                        return false;
                    // 머티리얼 셰이더 · 뷰 모드 · 반투명 배치는 씬 메시를 그리는 패스의 성질이다.
                    const uint32 kMeshOnlyFlags = Flag::kUsesMaterialShader | Flag::kAppliesViewMode | Flag::kDrawsTransparentBatch | Flag::kDepthOnly | Flag::kCullFront;
                    if ( row.hasFlag( kMeshOnlyFlags ) && row.hasFlag( Flag::kDrawsSceneMeshes ) == false )
                        return false;
                    // 머티리얼로 배치를 거르는 것도 씬 메시 패스의 성질이다.
                    const bool bFiltersByMaterial = row._pRequiredMaterialDefine != nullptr || row._pExcludedMaterialDefine != nullptr;
                    if ( bFiltersByMaterial && row.hasFlag( Flag::kDrawsSceneMeshes ) == false )
                        return false;
                    // 일반 풀스크린 실행은 선언한 입력을 계약대로 건다.
                    if ( row.hasFlag( Flag::kGenericFullscreen ) && row.hasFlag( Flag::kHasInputContract ) == false )
                        return false;
                    // 후처리를 끈 뷰에서 효과를 건너뛰는 일은 일반 풀스크린 실행만 한다.
                    if ( row.hasFlag( Flag::kSkipsWithoutPostProcess ) && row.hasFlag( Flag::kGenericFullscreen ) == false )
                        return false;
                    // 컴퓨트 패스는 파이프라인 XML 에 나오지 않고 그래픽스 성질을 갖지 않는다.
                    if ( row.hasFlag( Flag::kCompute ) && ( isPipelinePassType( row._type ) || row.hasFlag( Flag::kDrawsSceneMeshes | Flag::kGenericFullscreen ) ) )
                        return false;
                    if ( row.hasFlag( Flag::kRequiresGPUCulling ) && row.hasFlag( Flag::kCompute ) == false )
                        return false;
                    // 대신할 PSO 는 대신하지 않는 패스여야 한다(사슬을 따라가지 않는다).
                    if ( row._psoFallbackType != RenderPassType::Invalid &&
                         table._arrRow[static_cast<uint32>( row._psoFallbackType )]._psoFallbackType != RenderPassType::Invalid )
                        return false;
                }
                return true;
            }
        };

        constexpr RenderPassTypeInfoInternal::InfoTable s_infoTable = RenderPassTypeInfoInternal::makeTable();
        static_assert( RenderPassTypeInfoInternal::isTableConsistent( s_infoTable ), "RenderPassTypeInfo 표의 줄이 규칙을 어긴다" );
    } // namespace

    const RenderPassTypeInfo& getRenderPassTypeInfo( RenderPassType type )
    {
        const uint32 index = static_cast<uint32>( type );
        return index < kRenderPassTypeCount ? s_infoTable._arrRow[index] : s_infoTable._arrRow[0];
    }

    RenderPassShaderSelection selectRenderPassShader( RenderPassType type, const RenderGraphPassDesc* pPassDesc, const EngineDefaultAssets& engineDefaultAssets )
    {
        const RenderPassTypeInfo& info = getRenderPassTypeInfo( type );

        RenderPassShaderSelection selection;
        if ( pPassDesc != nullptr && pPassDesc->_shaderPath.empty() == false )
            selection._shaderPath = pPassDesc->_shaderPath;
        else if ( info._pDefaultShader != nullptr )
            selection._shaderPath = ( engineDefaultAssets.*info._pDefaultShader ).get();

        if ( pPassDesc != nullptr )
            selection._listDefine = pPassDesc->_listPermutation;
        if ( info._pPassDefine != nullptr )
        {
            const string passDefine{ info._pPassDefine };
            if ( VectorUtil::contains( selection._listDefine, passDefine ) == false )
                selection._listDefine.push_back( passDefine );
        }
        return selection;
    }
} // namespace sw
