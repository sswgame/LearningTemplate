#include "pch.h"

#include "Engine/Graphics/Renderer/Pipeline/RenderPassTypeTraits.h"

namespace sw
{
    namespace
    {
        /// G버퍼 MRT 의 고정 포맷입니다: [0] 알베도, [1] 노멀.
        constexpr RHIFormat kArrGBufferColorFormat[] = { RHIFormat::R8G8B8A8_UNORM, RHIFormat::R16G16B16A16_FLOAT };

        /// SSAO 의 기본 클리어(가림 없음)입니다.
        constexpr float4 kNoOcclusionClear{ 1.0f, 1.0f, 1.0f, 1.0f };

        using Role = RenderPassInputRole;
        using Flag = RenderPassTraitFlag;

        /// 씬 메시를 그려 화면 색을 만드는 지오메트리 패스의 공통 플래그입니다.
        constexpr uint32 kSceneColorPassFlags = Flag::kDrawsSceneMeshes | Flag::kUsesMaterialShader | Flag::kAppliesViewMode;

        struct RenderPassTypeTraitsInternal
        {
            /** @brief 필수 · 선택 역할 목록으로 입력 계약을 만듭니다. */
            static constexpr RenderPassInputContract makeContract( std::initializer_list<Role> listRequired, std::initializer_list<Role> listOptional )
            {
                RenderPassInputContract contract{};
                for ( const Role role : listRequired )
                    contract._arrRequired[contract._requiredCount++] = role;
                for ( const Role role : listOptional )
                    contract._arrOptional[contract._optionalCount++] = role;
                return contract;
            }

            /**
             * @brief 패스 종류 하나의 줄입니다. 열거자를 더하고 case 를 빠뜨리면 -Wswitch-enum 이 알립니다(default 는 Invalid 줄이다).
             */
            static constexpr RenderPassTypeTraits makeTraits( RenderPassType type )
            {
                RenderPassTypeTraits traits{};
                traits._type = type;
                switch ( type )
                {
                    case RenderPassType::Invalid:
                        break;
                    case RenderPassType::Shadow:
                    {
                        traits._pDefaultShader   = &EngineData::_shaderShadowDepth;
                        traits._colorTargetCount = 0;
                        traits._flags            = Flag::kDepthTest | Flag::kDepthWrite | Flag::kDepthOnly | Flag::kDrawsSceneMeshes;
                        break;
                    }
                    case RenderPassType::DepthPrepass:
                    {
                        // 그림자와 같은 셰이더 파일을 카메라 행렬로 그린다(패스 define 이 행렬을 고른다).
                        traits._pDefaultShader   = &EngineData::_shaderShadowDepth;
                        traits._pPassDefine      = kPassDepthPrepassDefine;
                        traits._colorTargetCount = 0;
                        traits._psoFallbackType  = RenderPassType::Shadow;
                        traits._flags            = Flag::kDepthTest | Flag::kDepthWrite | Flag::kDepthOnly | Flag::kDrawsSceneMeshes;
                        break;
                    }
                    case RenderPassType::ForwardOpaque:
                    {
                        traits._pDefaultShader = &EngineData::_shaderForwardLit;
                        traits._flags          = Flag::kDepthTest | Flag::kDepthWrite | kSceneColorPassFlags;
                        break;
                    }
                    case RenderPassType::GBuffer:
                    {
                        // define 은 이 PSO 를 물려받는 머티리얼 변형까지 MRT 서명으로 컴파일되게 한다.
                        traits._pDefaultShader   = &EngineData::_shaderGBuffer;
                        traits._pPassDefine      = kPassGBufferDefine;
                        traits._pColorFormat     = kArrGBufferColorFormat;
                        traits._colorTargetCount = 2;
                        traits._flags            = Flag::kDepthTest | Flag::kDepthWrite | kSceneColorPassFlags;
                        break;
                    }
                    case RenderPassType::Lighting:
                    {
                        traits._pDefaultShader = &EngineData::_shaderDeferredLighting;
                        traits._inputContract  = makeContract( { Role::GBufferAlbedo, Role::GBufferNormal, Role::SceneDepth }, { Role::ShadowMap } );
                        traits._flags          = Flag::kGenericFullscreen | Flag::kHasInputContract;
                        break;
                    }
                    case RenderPassType::Transparent:
                    {
                        // 블렌드를 켜고 깊이 쓰기를 끄는 것까지가 패스의 몫이다. 어떤 퍼뮤테이션으로 그릴지는 머티리얼이 정한다.
                        traits._pDefaultShader  = &EngineData::_shaderForwardLit;
                        traits._psoFallbackType = RenderPassType::ForwardOpaque;
                        traits._flags           = Flag::kDepthTest | Flag::kBlend | Flag::kDrawsTransparentBatch | kSceneColorPassFlags;
                        break;
                    }
                    case RenderPassType::SSAO:
                    {
                        traits._pDefaultShader = &EngineData::_shaderSsao;
                        traits._pDefaultClear  = &kNoOcclusionClear;
                        traits._inputContract  = makeContract( { Role::GBufferNormal, Role::SceneDepth }, {} );
                        traits._flags          = Flag::kGenericFullscreen | Flag::kHasInputContract;
                        break;
                    }
                    case RenderPassType::Bloom:
                    {
                        traits._pDefaultShader = &EngineData::_shaderPostBloom;
                        traits._inputContract  = makeContract( { Role::SourceColor }, { Role::AmbientOcclusion } );
                        traits._flags          = Flag::kGenericFullscreen | Flag::kHasInputContract;
                        break;
                    }
                    case RenderPassType::Outline:
                    {
                        traits._pDefaultShader = &EngineData::_shaderPostOutline;
                        traits._inputContract  = makeContract( { Role::SourceColor, Role::SceneDepth }, {} );
                        traits._flags          = Flag::kGenericFullscreen | Flag::kHasInputContract;
                        break;
                    }
                    case RenderPassType::TAA:
                    {
                        traits._pDefaultShader = &EngineData::_shaderTaa;
                        traits._inputContract  = makeContract( { Role::SourceColor }, {} );
                        traits._flags          = Flag::kHasInputContract;
                        break;
                    }
                    case RenderPassType::Tonemap:
                    {
                        // 톤매핑 PSO 가 없어도 그림은 나가야 하므로 Present(단순 블릿)로 대신한다.
                        traits._pDefaultShader  = &EngineData::_shaderTonemap;
                        traits._psoFallbackType = RenderPassType::Present;
                        traits._inputContract   = makeContract( { Role::SourceColor }, {} );
                        traits._flags           = Flag::kGenericFullscreen | Flag::kHasInputContract;
                        break;
                    }
                    case RenderPassType::Present:
                    {
                        // 입력은 모두 선택이다. 선언이 없으면 "가장 나중 컬러" 로 폴백하고(resolvePresentSource),
                        // 후처리 체인(postchain.hlsl)을 겸하면 깊이 · AO 를 읽는다.
                        traits._pDefaultShader = &EngineData::_shaderFullscreenBlit;
                        traits._inputContract  = makeContract( {}, { Role::SourceColor, Role::SceneDepth, Role::AmbientOcclusion } );
                        traits._flags          = Flag::kHasInputContract;
                        break;
                    }
                    case RenderPassType::ForwardOpaqueNoDepthWrite:
                    {
                        // 깊이 프리패스가 돈 프레임의 불투명 패스다. 깊이는 테스트만 한다.
                        traits._pDefaultShader = &EngineData::_shaderForwardLit;
                        traits._flags          = Flag::kDepthTest | kSceneColorPassFlags;
                        break;
                    }
                    case RenderPassType::GpuCull:
                    {
                        traits._pDefaultShader = &EngineData::_shaderGpuCull;
                        traits._flags          = Flag::kCompute | Flag::kRequiresGpuCulling;
                        break;
                    }
                    case RenderPassType::InstanceAnim:
                    {
                        // 인스턴스 버퍼 UAV 하나만 쓰므로 컬링 능력(간접 인자 제약)과 무관하다.
                        traits._pDefaultShader = &EngineData::_shaderInstanceAnim;
                        traits._flags          = Flag::kCompute;
                        break;
                    }
                    case RenderPassType::InstanceSort:
                    {
                        // 컬링이 압축한 가시 목록을 정렬하므로 컬링과 같은 조건이다.
                        traits._pDefaultShader = &EngineData::_shaderInstanceSort;
                        traits._flags          = Flag::kCompute | Flag::kRequiresGpuCulling;
                        break;
                    }
                    case RenderPassType::MeshMorph:
                    {
                        traits._pDefaultShader = &EngineData::_shaderMeshMorph;
                        traits._flags          = Flag::kCompute;
                        break;
                    }
                }
                return traits;
            }

            struct TraitsTable
            {
                RenderPassTypeTraits _arrRow[kRenderPassTypeCount];
            };

            static constexpr TraitsTable makeTable()
            {
                TraitsTable table{};
                for ( uint32 index = 0; index < kRenderPassTypeCount; ++index )
                    table._arrRow[index] = makeTraits( static_cast<RenderPassType>( index ) );
                return table;
            }

            /** @brief 줄 사이의 규칙을 검사합니다. 하나라도 어기면 false 입니다(static_assert 가 봅니다). */
            static constexpr bool isTableConsistent( const TraitsTable& table )
            {
                for ( uint32 index = 0; index < kRenderPassTypeCount; ++index )
                {
                    const RenderPassTypeTraits& row = table._arrRow[index];
                    if ( static_cast<uint32>( row._type ) != index )
                        return false;
                    // 깊이만 쓰는 패스는 컬러 RT 가 없다. 고정 포맷은 RT 수만큼 있어야 한다.
                    if ( row.hasFlag( Flag::kDepthOnly ) && row._colorTargetCount != 0 )
                        return false;
                    if ( row._pColorFormat != nullptr && row._colorTargetCount == 0 )
                        return false;
                    // 머티리얼 셰이더 · 뷰 모드 · 반투명 배치는 씬 메시를 그리는 패스의 성질이다.
                    const uint32 kMeshOnlyFlags = Flag::kUsesMaterialShader | Flag::kAppliesViewMode | Flag::kDrawsTransparentBatch | Flag::kDepthOnly;
                    if ( row.hasFlag( kMeshOnlyFlags ) && row.hasFlag( Flag::kDrawsSceneMeshes ) == false )
                        return false;
                    // 일반 풀스크린 실행은 선언한 입력을 계약대로 건다.
                    if ( row.hasFlag( Flag::kGenericFullscreen ) && row.hasFlag( Flag::kHasInputContract ) == false )
                        return false;
                    // 컴퓨트 패스는 파이프라인 XML 에 나오지 않고 그래픽스 성질을 갖지 않는다.
                    if ( row.hasFlag( Flag::kCompute ) && ( isPipelinePassType( row._type ) || row.hasFlag( Flag::kDrawsSceneMeshes | Flag::kGenericFullscreen ) ) )
                        return false;
                    if ( row.hasFlag( Flag::kRequiresGpuCulling ) && row.hasFlag( Flag::kCompute ) == false )
                        return false;
                    // 대신할 PSO 는 대신하지 않는 패스여야 한다(사슬을 따라가지 않는다).
                    if ( row._psoFallbackType != RenderPassType::Invalid &&
                         table._arrRow[static_cast<uint32>( row._psoFallbackType )]._psoFallbackType != RenderPassType::Invalid )
                        return false;
                }
                return true;
            }
        };

        constexpr RenderPassTypeTraitsInternal::TraitsTable s_traitsTable = RenderPassTypeTraitsInternal::makeTable();
        static_assert( RenderPassTypeTraitsInternal::isTableConsistent( s_traitsTable ), "RenderPassTypeTraits 표의 줄이 규칙을 어긴다" );
    } // namespace

    const RenderPassTypeTraits& getRenderPassTypeTraits( RenderPassType type )
    {
        const uint32 index = static_cast<uint32>( type );
        return index < kRenderPassTypeCount ? s_traitsTable._arrRow[index] : s_traitsTable._arrRow[0];
    }

    RenderPassShaderSelection selectRenderPassShader( RenderPassType type, const RenderGraphPassDesc* pPassDesc, const EngineData& engineData )
    {
        const RenderPassTypeTraits& traits = getRenderPassTypeTraits( type );

        RenderPassShaderSelection selection;
        if ( pPassDesc != nullptr && pPassDesc->_shaderPath.empty() == false )
            selection._shaderPath = pPassDesc->_shaderPath;
        else if ( traits._pDefaultShader != nullptr )
            selection._shaderPath = engineData.*traits._pDefaultShader;

        if ( pPassDesc != nullptr )
            selection._listDefine = pPassDesc->_listPermutation;
        if ( traits._pPassDefine != nullptr )
        {
            const string passDefine{ traits._pPassDefine };
            if ( std::find( selection._listDefine.begin(), selection._listDefine.end(), passDefine ) == selection._listDefine.end() )
                selection._listDefine.push_back( passDefine );
        }
        return selection;
    }
} // namespace sw
