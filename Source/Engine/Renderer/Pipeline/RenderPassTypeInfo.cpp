#include "pch.h"

#include "Engine/Renderer/Pipeline/RenderPassTypeInfo.h"

#include "Core/Algorithm/VectorUtil.h"

#include "Engine/Graphics/Debug/GPUMessage.h"
#include "Engine/Graphics/Debug/GPUNanCheck.h"
#include "Engine/Renderer/Pipeline/GBufferLayout.h"

namespace sw
{
    namespace
    {
        /// G버퍼 MRT 의 고정 포맷입니다. 명세 한 벌(`GBufferAttachmentSpec` — 알베도 · 노멀 · 머티리얼, 판 번호 있음)에서 온다 — 포맷을 여기 따로 적지 않는다.
        constexpr const RHIFormat* kArrGBufferColorFormat = GBufferLayout::kArrColorFormat;

        /// 깊이 히스토리의 고정 포맷입니다(깊이 하나 — 컴퓨트가 RW 텍스처로 읽는다. R32 실수는 네 백엔드 모두 형식 있는 UAV 읽기가 된다).
        constexpr RHIFormat kArrDepthHistoryFormat[] = { RHIFormat::R32_FLOAT };

        /// 자동 노출 EV 의 고정 포맷입니다(값 하나 — 음수 EV 가 있어 UNORM 은 안 된다).
        constexpr RHIFormat kArrAutoExposureFormat[] = { RHIFormat::R32_FLOAT };

        /// FSR 1 중간 RT 의 고정 포맷입니다(EASU 출력 — 톤맵한 [0,1] 색이라 AMD 권고대로 32 bpp).
        constexpr RHIFormat kArrUpscaleIntermediateFormat[] = { RHIFormat::R8G8B8A8_UNORM };

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
                        info._flags            = Flag::kDepthTest | Flag::kDepthWrite | Flag::kDepthOnly | Flag::kDrawsSceneMeshes;
                        break;
                    }
                    case RenderPassType::ForwardOpaque:
                    {
                        // 입력은 그림자 맵과 접촉 그림자(깊이 프리패스 뒤의 ContactShadow 패스)뿐이고 둘 다 선택이다 — 접촉 그림자는 꺼지면 출력이 없다(3-7).
                        info._pDefaultShader = &EngineDefaultAssets::_shaderForwardLit;
                        info._inputContract  = makeContract( {}, { Role::ShadowMap, Role::ContactShadow } );
                        info._flags          = Flag::kDepthTest | Flag::kDepthWrite | Flag::kMaterialFixedState | Flag::kMaterialBlend | Flag::kWritesVelocity | Flag::kHasInputContract | kSceneColorPassFlags;
                        break;
                    }
                    case RenderPassType::GBuffer:
                    {
                        // define 은 이 PSO 를 물려받는 머티리얼 변형까지 MRT 서명으로 컴파일되게 한다.
                        info._pDefaultShader          = &EngineDefaultAssets::_shaderGBuffer;
                        info._pPassDefine             = kPassGBufferDefine;
                        info._pExcludedMaterialDefine = kMaterialForwardOnlyDefine;
                        info._pColorFormat            = kArrGBufferColorFormat;
                        info._colorTargetCount        = GBufferLayout::kAttachmentCount;
                        info._flags                   = Flag::kDepthTest | Flag::kDepthWrite | Flag::kMaterialFixedState | Flag::kWritesVelocity | kSceneColorPassFlags;
                        break;
                    }
                    case RenderPassType::Lighting:
                    {
                        info._pDefaultShader = &EngineDefaultAssets::_shaderDeferredLighting;
                        info._inputContract  = makeContract( { Role::GBufferAlbedo, Role::GBufferNormal, Role::GBufferMaterial, Role::GBufferCustomData, Role::SceneDepth }, { Role::ShadowMap, Role::ContactShadow } );
                        info._flags          = Flag::kGenericFullscreen | Flag::kDrawsDeclaredColor | Flag::kHasInputContract;
                        break;
                    }
                    case RenderPassType::Transparent:
                    {
                        // 블렌드를 켜고 깊이 쓰기를 끄는 것까지가 패스의 몫이다. 어떤 퍼뮤테이션으로 그릴지는 머티리얼이 정한다.
                        info._pDefaultShader = &EngineDefaultAssets::_shaderForwardLit;
                        info._flags          = Flag::kDepthTest | Flag::kBlend | Flag::kDrawsTransparentBatch | Flag::kMaterialFixedState | Flag::kMaterialBlend | kSceneColorPassFlags;
                        break;
                    }
                    case RenderPassType::SSAO:
                    {
                        // 꺼지면(후처리를 끈 뷰 · 품질) 기록하지 않고 출력이 없다 — 읽는 쪽은 AO 역할을 바인딩하지 않아 가림 없음(1)이다(kOmittedWhenOff).
                        info._pDefaultShader = &EngineDefaultAssets::_shaderSSAO;
                        info._inputContract  = makeContract( { Role::GBufferNormal, Role::SceneDepth }, {} );
                        info._flags          = Flag::kGenericFullscreen | Flag::kDrawsDeclaredColor | Flag::kHasInputContract | Flag::kSkipsWithoutPostProcess | Flag::kOmittedWhenOff;
                        break;
                    }
                    case RenderPassType::ContactShadow:
                    {
                        // 켠 빛이 없거나 후처리를 끈 뷰는 기록하지 않고 출력이 없다 — 읽는 쪽은 접촉 그림자 역할을 바인딩하지 않아 1 이다(kOmittedWhenOff, FrameViewResources::applyViewPassSelection).
                        info._pDefaultShader = &EngineDefaultAssets::_shaderContactShadow;
                        info._inputContract  = makeContract( { Role::SceneDepth }, {} );
                        info._flags          = Flag::kGenericFullscreen | Flag::kDrawsDeclaredColor | Flag::kHasInputContract | Flag::kSkipsWithoutPostProcess | Flag::kOmittedWhenOff;
                        break;
                    }
                    case RenderPassType::Bloom:
                    {
                        // BloomBright(반해상도 밝은 부분)를 선언하면 그것을 업샘플해 더하고, 없으면 원본에서 밝은 부분을 직접 구한다.
                        // 꺼지면 출력이 원본을 가리킨다(AO 적용도 함께 빠진다). 그때 BloomBright · AO 를 읽는 패스가 없으면 그 패스들도 돌지 않는다(applyViewPassSelection).
                        info._pDefaultShader = &EngineDefaultAssets::_shaderPostBloom;
                        info._inputContract  = makeContract( { Role::SourceColor }, { Role::AmbientOcclusion, Role::BloomBright } );
                        info._flags          = Flag::kGenericFullscreen | Flag::kDrawsDeclaredColor | Flag::kHasInputContract | Flag::kSkipsWithoutPostProcess | Flag::kBypassesToSource;
                        break;
                    }
                    case RenderPassType::BloomDownsample:
                    {
                        // 출력은 반해상도 첨부다(파이프라인이 `_resolutionDivisor` 로 정한다). 읽는 블룸이 꺼진 뷰에서는 읽는 패스가 없어 돌지 않는다(applyViewPassSelection).
                        info._pDefaultShader = &EngineDefaultAssets::_shaderPostBloom;
                        info._pPassDefine    = kPassBloomDownsampleDefine;
                        info._inputContract  = makeContract( { Role::SourceColor }, {} );
                        info._flags          = Flag::kGenericFullscreen | Flag::kDrawsDeclaredColor | Flag::kHasInputContract | Flag::kComputeImplementation;
                        break;
                    }
                    case RenderPassType::Outline:
                    {
                        // 꺼지면(후처리를 끈 뷰 · 품질) 출력이 원본을 가리킨다(kBypassesToSource) — 출력은 원본과 같은 HDR 형식이어야 한다(검증 3-6).
                        info._pDefaultShader = &EngineDefaultAssets::_shaderPostOutline;
                        info._inputContract  = makeContract( { Role::SourceColor, Role::SceneDepth }, {} );
                        info._flags          = Flag::kGenericFullscreen | Flag::kDrawsDeclaredColor | Flag::kHasInputContract | Flag::kSkipsWithoutPostProcess | Flag::kBypassesToSource;
                        break;
                    }
                    case RenderPassType::TAA:
                    {
                        // 기록은 화면 속도(물체 움직임 포함)로 지난 프레임 자리를 찾고 이번 원본의 이웃 범위로 자른다(taa.hlsl). 속도는 3x3 에서 가장 가까운 깊이의 것을 쓴다.
                        // 하늘(깊이 1)은 속도 출력이 없어 깊이 · 지난 뷰-투영으로 카메라 재투영만 한다.
                        // 후처리를 끈 뷰(CCTV)에서는 패스를 기록하지 않고 출력이 원본을 가리킨다(kBypassesToSource, 출력은 원본과 같은 형식 — 검증 3-6).
                        // 일반 풀스크린 패스가 아니라 실행은 executeTAAPass 이고, 출력 타깃은 셋업이 기록 형식으로 읽으므로 늘 만든다(ensureTAAHistory).
                        info._pDefaultShader = &EngineDefaultAssets::_shaderTAA;
                        info._inputContract  = makeContract( { Role::SourceColor, Role::SceneDepth, Role::Velocity }, {} );
                        info._flags          = Flag::kHasInputContract | Flag::kDrawsDeclaredColor | Flag::kSkipsWithoutPostProcess | Flag::kBypassesToSource;
                        break;
                    }
                    case RenderPassType::Tonemap:
                    {
                        info._pDefaultShader = &EngineDefaultAssets::_shaderTonemap;
                        info._inputContract  = makeContract( { Role::SourceColor }, {} );
                        info._flags          = Flag::kGenericFullscreen | Flag::kDrawsDeclaredColor | Flag::kHasInputContract;
                        break;
                    }
                    case RenderPassType::Present:
                    {
                        // 화면에 낼 색(SourceColor)은 필수 입력이다 — 옛 코드는 선언이 없으면 "가장 나중 컬러" 를 이름 후보로 짐작했다(resolvePresentSource).
                        // 후처리 체인(post_chain.hlsl)을 겸하면 깊이 · AO 를 읽는다.
                        info._pDefaultShader = &EngineDefaultAssets::_shaderFullscreenBlit;
                        info._inputContract  = makeContract( { Role::SourceColor }, { Role::SceneDepth, Role::AmbientOcclusion } );
                        info._flags          = Flag::kHasInputContract | Flag::kOutputPass | Flag::kGPUMessageTarget; // fullscreen_blit 의 SW_GPU_PRINT(고른 픽셀의 화면 색)
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
                        info._flags          = Flag::kBlend | Flag::kPremultipliedAlpha | Flag::kHasInputContract | Flag::kOutputPass;
                        break;
                    }
                    case RenderPassType::ForwardOnlyOpaque:
                    {
                        // 디퍼드의 G버퍼가 뺀 배치(MATERIAL_FORWARD_ONLY)만 조명 패스 뒤에 포워드로 그린다. 깊이는 G버퍼가 쓴 장면 깊이에 테스트 · 쓰기 한다.
                        info._pDefaultShader          = &EngineDefaultAssets::_shaderForwardLit;
                        info._pRequiredMaterialDefine = kMaterialForwardOnlyDefine;
                        info._flags                   = Flag::kDepthTest | Flag::kDepthWrite | Flag::kMaterialFixedState | Flag::kMaterialBlend | Flag::kWritesVelocity | kSceneColorPassFlags;
                        break;
                    }
                    case RenderPassType::DepthOfField:
                    {
                        // 효과 패스다 — 렌즈가 핀홀(조리개 0) · 직교 · 후처리를 끈 뷰는 패스를 기록하지 않고 출력이 원본을 가리킨다(kBypassesToSource).
                        info._pDefaultShader = &EngineDefaultAssets::_shaderDepthOfField;
                        info._inputContract  = makeContract( { Role::SourceColor, Role::SceneDepth }, {} );
                        info._flags          = Flag::kGenericFullscreen | Flag::kDrawsDeclaredColor | Flag::kHasInputContract | Flag::kSkipsWithoutPostProcess | Flag::kBypassesToSource |
                                      Flag::kComputeImplementation;
                        break;
                    }
                    case RenderPassType::HighlightOutline:
                    {
                        // 강조 외곽선 — 머티리얼과 무관한 엔진 셰이더로 모든 불투명 배치를 그리고, 강조가 Outline 인 인스턴스만 정점이 남는다.
                        // 깊이는 테스트만 한다(쓰면 뒤의 반투명이 껍질에 가려진다). 앞면을 컬링한 껍질이라 몸 둘레만 보인다. 타깃은 선언한 컬러 출력이다.
                        info._pDefaultShader = &EngineDefaultAssets::_shaderHighlightOutline;
                        info._flags          = Flag::kDepthTest | Flag::kDrawsSceneMeshes | Flag::kCullFront | Flag::kDrawsDeclaredColor | Flag::kRequiresPassDesc;
                        break;
                    }
                    case RenderPassType::HighlightSense:
                    {
                        // 감각 모드 — 같은 셰이더의 Sense 퍼뮤테이션을 깊이 테스트 없이 그려 벽 너머의 대상도 보인다.
                        info._pDefaultShader = &EngineDefaultAssets::_shaderHighlightOutline;
                        info._pPassDefine    = kPassHighlightSenseDefine;
                        info._flags          = Flag::kDrawsSceneMeshes | Flag::kCullFront | Flag::kDrawsDeclaredColor | Flag::kRequiresPassDesc;
                        break;
                    }
                    case RenderPassType::Fog:
                    {
                        // 안개 — 장면 깊이로 시선 위 광학 두께를 구해 선언한 컬러 출력(앞 패스가 칠한 색) 위에 프리멀티플라이 블렌드한다(색 = 안개색 × 불투명도, 알파 = 불투명도).
                        // 색 첨부를 읽지 않으므로 입력은 장면 깊이뿐이다 — 읽으면서 쓰는 첨부가 없다. 전용 실행(안개가 없거나 후처리를 끈 뷰는 그리지 않는다).
                        info._pDefaultShader = &EngineDefaultAssets::_shaderFog;
                        info._inputContract  = makeContract( { Role::SceneDepth }, {} );
                        info._flags          = Flag::kBlend | Flag::kPremultipliedAlpha | Flag::kHasInputContract | Flag::kDrawsDeclaredColor;
                        break;
                    }
                    case RenderPassType::SceneColorCopy:
                    {
                        // 셰이더 · PSO 없이 원본을 선언한 출력으로 복사한다(전용 실행 executeSceneColorCopyPass). 입력은 원본 색 하나, 출력은 같은 형식의 첨부다.
                        info._inputContract = makeContract( { Role::SourceColor }, {} );
                        info._flags         = Flag::kHasInputContract | Flag::kDrawsDeclaredColor;
                        break;
                    }
                    case RenderPassType::SceneDepthCopy:
                    {
                        // 장면 깊이를 선언한 R32 실수 출력으로 옮기는 일반 풀스크린 패스다(scene_depth_copy.hlsl). 꺼지는 조건이 없다 — 반투명이 읽는 입력이라 늘 돈다.
                        info._pDefaultShader = &EngineDefaultAssets::_shaderSceneDepthCopy;
                        info._inputContract  = makeContract( { Role::SceneDepth }, {} );
                        info._flags          = Flag::kGenericFullscreen | Flag::kDrawsDeclaredColor | Flag::kHasInputContract;
                        break;
                    }
                    case RenderPassType::Distortion:
                    {
                        // 왜곡 머티리얼(MATERIAL_DISTORTION)의 반투명 배치만 그 머티리얼 셰이더의 왜곡 퍼뮤테이션으로 오프셋을 더한다(가산 블렌드는 파이프라인 XML 이 적는다).
                        // 깊이는 테스트만 한다. 전용 실행(executeDistortionPass)이 씬에 왜곡 배치가 없으면 기록하지 않는다. 뷰 모드는 받지 않는다(값 보기에서는 왜곡이 없다).
                        info._pDefaultShader          = &EngineDefaultAssets::_shaderDistortion;
                        info._pPassDefine             = kPassDistortionDefine;
                        info._pRequiredMaterialDefine = kMaterialDistortionDefine;
                        info._flags                   = Flag::kDepthTest | Flag::kBlend | Flag::kDrawsSceneMeshes | Flag::kUsesMaterialShader | Flag::kDrawsTransparentBatch | Flag::kDrawsDeclaredColor;
                        break;
                    }
                    case RenderPassType::DistortionApply:
                    {
                        // 장면 색 사본을 왜곡 오프셋만큼 비껴 읽어 장면 색에 쓴다. 깊이로 앞의 물체를 끌어오지 않는다. 전용 실행 — 왜곡이 없으면 출력(장면 색)이 이미 맞아 기록하지 않는다.
                        info._pDefaultShader = &EngineDefaultAssets::_shaderDistortionApply;
                        info._inputContract  = makeContract( { Role::SceneColorCopy, Role::Distortion, Role::SceneDepth }, {} );
                        info._flags          = Flag::kHasInputContract | Flag::kDrawsDeclaredColor;
                        break;
                    }
                    case RenderPassType::ScreenSpaceReflection:
                    {
                        // 화면 공간 반사 — 장면 색 사본 · 깊이 · G버퍼 노멀(· 머티리얼)을 읽어 선언한 컬러 출력(장면 색) 위에 프리멀티플라이 블렌드한다(안개와 같은 모양).
                        // 전용 실행이 세기 0 · 후처리를 끈 뷰 · 표면 값 보기에서 그리지 않는다. 출력이 장면 색이라 원본을 가리키는 끄기(kBypassesToSource)는 쓸 수 없다.
                        info._pDefaultShader = &EngineDefaultAssets::_shaderScreenSpaceReflection;
                        info._inputContract  = makeContract( { Role::SceneColorCopy, Role::SceneDepth, Role::GBufferNormal }, { Role::GBufferMaterial } );
                        info._flags          = Flag::kBlend | Flag::kPremultipliedAlpha | Flag::kHasInputContract | Flag::kDrawsDeclaredColor;
                        break;
                    }
                    case RenderPassType::MotionBlur:
                    {
                        // 원본 색 · 장면 깊이 · 화면 속도를 읽는 효과 패스다. 꺼지면(후처리를 끈 뷰 · 품질 · gv_motionBlur · 컷 · 카메라와 물체가 모두 서 있음) 패스를 기록하지 않고 출력이 원본을 가리킨다(kBypassesToSource).
                        info._pDefaultShader = &EngineDefaultAssets::_shaderMotionBlur;
                        info._inputContract  = makeContract( { Role::SourceColor, Role::SceneDepth, Role::Velocity }, {} );
                        info._flags          = Flag::kGenericFullscreen | Flag::kDrawsDeclaredColor | Flag::kHasInputContract | Flag::kSkipsWithoutPostProcess | Flag::kBypassesToSource;
                        break;
                    }
                    case RenderPassType::ScreenSpaceGI:
                    {
                        // 조명 첨부에 더하는 효과다 — 그 첨부를 읽을 수 없어 장면 색 사본을 읽는다. 알베도는 계약상 필수다(튄 빛을 받는 면의 색).
                        // 건너뛸 수 있는 효과(kSkipsWithoutPostProcess)가 아니다 — 출력(조명 첨부)을 여러 패스가 써서 출력을 원본에 잇거나 뺄 수 없다. 켜고 끄는 것은 파이프라인 XML 이다.
                        info._pDefaultShader = &EngineDefaultAssets::_shaderScreenSpaceGI;
                        info._inputContract  = makeContract( { Role::SceneColorCopy, Role::SceneDepth, Role::GBufferNormal, Role::GBufferAlbedo }, {} );
                        info._flags          = Flag::kGenericFullscreen | Flag::kDrawsDeclaredColor | Flag::kHasInputContract | Flag::kRequiresPassDesc;
                        break;
                    }
                    case RenderPassType::LensFlare:
                    {
                        // 스프라이트 버퍼(패스 버퍼 슬롯)를 읽어 선언한 컬러 출력에 더한다 — 전용 실행(`FramePassExecutor::executeLensFlarePass`). 가림은 장면 깊이를 읽는다.
                        info._pDefaultShader = &EngineDefaultAssets::_shaderLensFlare;
                        info._inputContract  = makeContract( { Role::SceneDepth }, {} );
                        info._flags          = Flag::kBlend | Flag::kHasInputContract | Flag::kDrawsDeclaredColor | Flag::kRequiresPassDesc;
                        break;
                    }
                    case RenderPassType::Compute:
                    {
                        // 셰이더는 XML 이 고른다 — 표의 기본값이 없고, 첨부를 RW 텍스처로 묶는 전용 실행이다(`FramePassExecutor::executeComputePass`).
                        info._flags = Flag::kPipelineCompute | Flag::kDrawsDeclaredColor;
                        break;
                    }
                    case RenderPassType::ForwardOpaqueNoDepthWrite:
                    {
                        // 깊이 프리패스가 돈 프레임의 불투명 패스다. 깊이는 테스트만 하고, 그리는 대상은 ForwardOpaque 패스의 선언이다.
                        info._pDefaultShader   = &EngineDefaultAssets::_shaderForwardLit;
                        info._flags            = Flag::kDepthTest | Flag::kMaterialFixedState | Flag::kMaterialBlend | Flag::kWritesVelocity | kSceneColorPassFlags;
                        info._targetSourceType = RenderPassType::ForwardOpaque;
                        break;
                    }
                    case RenderPassType::GPUCull:
                    {
                        info._pDefaultShader = &EngineDefaultAssets::_shaderGPUCull;
                        info._flags          = Flag::kCompute | Flag::kRequiresGPUCulling | Flag::kGPUMessageTarget;
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
                        info._flags          = Flag::kCompute | Flag::kGPUMessageTarget;
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
                    case RenderPassType::VFXSimulation:
                    {
                        // 입자 상태 · 렌더 입력은 구조 버퍼 UAV, 간접 인자는 따로 둔 버퍼(DX11 은 RAW UAV)라 구조 버퍼와 간접 인자 겸용 능력이 필요 없다(DX11 도 돈다).
                        info._pDefaultShader = &EngineDefaultAssets::_shaderVFXSimulation;
                        info._flags          = Flag::kCompute;
                        break;
                    }
                    case RenderPassType::VFXMeshInstance:
                    {
                        // 인스턴스 버퍼 UAV 에 쓰고 구조 버퍼만 읽는다 — 간접 인자 능력과 무관하다(컬링이 없는 DX11 은 CPU 개수로 모두 그린다).
                        info._pDefaultShader = &EngineDefaultAssets::_shaderVFXMeshInstance;
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
                    case RenderPassType::AutoExposure:
                    {
                        // 1x1 R32 실수 RT 하나에 적응한 EV 를 쓴다. 입력(원본 · 지난 EV)은 C++ 가 루트 상수 · 엔진 슬롯으로 직접 건다(깊이 히스토리와 같은 모양).
                        info._pDefaultShader   = &EngineDefaultAssets::_shaderAutoExposure;
                        info._pColorFormat     = kArrAutoExposureFormat;
                        info._colorTargetCount = 1;
                        break;
                    }
                    case RenderPassType::UpscaleEasu:
                    {
                        // 뷰포트 크기 중간 RT(RGBA8 — 톤맵한 [0,1] 색) 하나에 그린다. 원본은 C++ 가 루트 상수 · 엔진 슬롯으로 건다.
                        info._pDefaultShader   = &EngineDefaultAssets::_shaderUpscale;
                        info._pColorFormat     = kArrUpscaleIntermediateFormat;
                        info._colorTargetCount = 1;
                        break;
                    }
                    case RenderPassType::UpscaleRcas:
                    {
                        // Present 대상에 그리는 출력 패스다 — 포맷마다 PSO 를 만든다(collectOutputPSOJobs). 엔진 PSO 는 쓰지 않는다.
                        info._pDefaultShader   = &EngineDefaultAssets::_shaderUpscale;
                        info._pPassDefine      = kPassFSRRcasDefine;
                        info._colorTargetCount = 1;
                        info._flags            = Flag::kOutputPass;
                        break;
                    }
                    case RenderPassType::OcclusionTiles:
                    {
                        // 컬링이 읽는 타일이라 컬링과 같은 조건이다(간접 인자 능력 — DX11 은 컬링이 없다). 깊이 히스토리는 RW 텍스처로 읽는다.
                        info._pDefaultShader = &EngineDefaultAssets::_shaderOcclusionTiles;
                        info._flags          = Flag::kCompute | Flag::kRequiresGPUCulling;
                        break;
                    }
                    case RenderPassType::VolumetricFog:
                    {
                        // 구조 버퍼 UAV 와 읽기 버퍼만 쓴다 — 간접 인자 능력과 무관하다(DX11 도 돈다).
                        info._pDefaultShader = &EngineDefaultAssets::_shaderVolumetricFog;
                        info._flags          = Flag::kCompute;
                        break;
                    }
                    case RenderPassType::CanvasMarker:
                    {
                        // 구조 버퍼 UAV 와 읽기 버퍼만 쓴다 — 간접 인자 능력과 무관하다(컴퓨트 능력이 없는 백엔드는 렌더러가 마커 입력을 오류로 거절한다).
                        info._pDefaultShader = &EngineDefaultAssets::_shaderCanvasMarker;
                        info._flags          = Flag::kCompute | Flag::kGPUMessageTarget;
                        break;
                    }
                    case RenderPassType::ShadowCacheRestore:
                    {
                        // 그림자 아틀라스(깊이만)에 그리는 풀스크린 — 깊이 테스트(지운 1 위라 늘 통과) · 쓰기를 하고 컬러 RT 는 없다. 입력(캐시 층)은 C++ 가 ShadowMap 역할 이름으로 직접 건다.
                        info._pDefaultShader   = &EngineDefaultAssets::_shaderShadowCacheRestore;
                        info._colorTargetCount = 0;
                        info._flags            = Flag::kDepthTest | Flag::kDepthWrite | Flag::kWritesPixelDepth;
                        break;
                    }
                    case RenderPassType::NanCheck:
                    {
                        // 부동소수 출력 텍스처를 엔진 텍스처(bindless)로 읽는 컴퓨트 — 샘플 텍스처를 컴퓨트에서 읽을 수 없는 에뮬 백엔드(DX11 · GL)에는 만들지 않는다.
                        info._pDefaultShader = &EngineDefaultAssets::_shaderNanCheck;
                        info._flags          = Flag::kCompute | Flag::kDiagnostic | Flag::kRequiresNativeBindless;
                        break;
                    }
                    case RenderPassType::ProbeUpdate:
                    {
                        // 구조 버퍼 UAV 와 읽기 버퍼만 쓴다 — 간접 인자 능력과 무관하다(DX11 도 돈다).
                        info._pDefaultShader = &EngineDefaultAssets::_shaderProbeUpdate;
                        info._flags          = Flag::kCompute;
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
                    const uint32 kMeshOnlyFlags = Flag::kUsesMaterialShader | Flag::kAppliesViewMode | Flag::kDrawsTransparentBatch | Flag::kDepthOnly | Flag::kCullFront | Flag::kMaterialFixedState | Flag::kMaterialBlend |
                                                  Flag::kWritesVelocity;
                    if ( row.hasFlag( kMeshOnlyFlags ) && row.hasFlag( Flag::kDrawsSceneMeshes ) == false )
                        return false;
                    // 머티리얼로 배치를 거르는 것도 씬 메시 패스의 성질이다.
                    const bool bFiltersByMaterial = row._pRequiredMaterialDefine != nullptr || row._pExcludedMaterialDefine != nullptr;
                    if ( bFiltersByMaterial && row.hasFlag( Flag::kDrawsSceneMeshes ) == false )
                        return false;
                    // 일반 풀스크린 실행은 선언한 입력을 계약대로 건다.
                    if ( row.hasFlag( Flag::kGenericFullscreen ) && row.hasFlag( Flag::kHasInputContract ) == false )
                        return false;
                    // 꺼진 패스를 기록하지 않는 것은 실행이 출력 이름의 연결(`isForwarded`)을 볼 때만 된다 — 일반 풀스크린 실행과 TAA 실행(executeTAAPass)이 본다.
                    // TAA 는 출력이 원본을 가리키는 길만 있다(출력이 없으면 셋업이 읽는 기록 형식이 사라진다).
                    const bool bExecutorHonorsSelection =
                        row.hasFlag( Flag::kGenericFullscreen ) || ( row._type == RenderPassType::TAA && row.hasFlag( Flag::kBypassesToSource ) );
                    if ( row.hasFlag( Flag::kSkipsWithoutPostProcess ) && bExecutorHonorsSelection == false )
                        return false;
                    // 건너뛸 수 있는 효과는 빠지는 방법(원본을 가리킴 · 출력 없음)이 정확히 하나다. 그 방법이 있는 패스는 건너뛸 수 있는 효과다(`_minPostQuality` 를 받는다).
                    const bool bBypasses = row.hasFlag( Flag::kBypassesToSource );
                    const bool bOmitted  = row.hasFlag( Flag::kOmittedWhenOff );
                    if ( row.hasFlag( Flag::kSkipsWithoutPostProcess ) != ( bBypasses || bOmitted ) || ( bBypasses && bOmitted ) )
                        return false;
                    // 표의 컴퓨트 패스는 뷰 패스가 아니고(프레임 패스 · 내부 슬롯) 그래픽스 성질을 갖지 않는다.
                    if ( row.hasFlag( Flag::kCompute ) && ( isViewPassType( row._type ) || row.hasFlag( Flag::kDrawsSceneMeshes | Flag::kGenericFullscreen ) ) )
                        return false;
                    // 프레임 패스는 표의 셰이더로 돈다(XML 이 셰이더 칸을 비운다 — 검증). 기본 셰이더가 없으면 PSO 도 실행도 없다.
                    if ( isFramePassType( row._type ) && row._pDefaultShader == nullptr )
                        return false;
                    if ( row.hasFlag( Flag::kRequiresGPUCulling ) && row.hasFlag( Flag::kCompute ) == false )
                        return false;
                    // 화면 속도는 깊이와 같은 픽셀의 값이다 — 깊이를 쓰는 불투명 패스(내부 슬롯은 대신 그리는 패스가 깊이를 쓴다)만 쓴다.
                    const bool bDepthOwner = row.hasFlag( Flag::kDepthWrite ) || row._targetSourceType != RenderPassType::Invalid;
                    if ( row.hasFlag( Flag::kWritesVelocity ) && ( bDepthOwner == false || row.hasFlag( Flag::kDrawsTransparentBatch ) ) )
                        return false;
                    // 컴퓨트 구현은 일반 풀스크린 패스의 다른 실행이다 — 같은 입력 계약 · 같은 셰이더 파일을 쓴다.
                    if ( row.hasFlag( Flag::kComputeImplementation ) && ( row.hasFlag( Flag::kGenericFullscreen ) == false || row._pDefaultShader == nullptr ) )
                        return false;
                }
                return true;
            }
        };

        constexpr RenderPassTypeInfoInternal::InfoTable s_infoTable = RenderPassTypeInfoInternal::makeTable();
        static_assert( RenderPassTypeInfoInternal::isTableConsistent( s_infoTable ), "RenderPassTypeInfo 표의 줄이 규칙을 어긴다" );
    } // namespace

    const RenderPassTypeInfo&
    getRenderPassTypeInfo( RenderPassType type )
    {
        const uint32 index = static_cast<uint32>( type );
        return index < kRenderPassTypeCount ? s_infoTable._arrRow[index] : s_infoTable._arrRow[0];
    }

    bool passWritesVelocity( RenderPassType type, const RenderGraphPassDesc* pTargetDesc )
    {
        if ( pTargetDesc == nullptr || getRenderPassTypeInfo( type ).hasFlag( RenderPassTraitFlag::kWritesVelocity ) == false )
            return false;
        return VectorUtil::containsIf( pTargetDesc->_listResolvedColorOutput, []( const RenderGraphPassDesc::ResolvedAttachment& output )
        { return static_cast<RenderPassInputRole>( output._role ) == RenderPassInputRole::Velocity; } );
    }

    RenderPassShaderSelection selectRenderPassShader( RenderPassType type, const RenderGraphPassDesc* pPassDesc, const RenderGraphPassDesc* pTargetDesc,
                                                      const EngineDefaultAssets& engineDefaultAssets )
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
        // 화면 속도 — 출력 선언(데이터)이 정한다. 내부 슬롯은 대신 그리는 패스의 선언을 본다(컬러 포맷과 같은 근거).
        if ( passWritesVelocity( type, pTargetDesc ) )
            selection._listDefine.push_back( kPassVelocityDefine );
        // 셰이더 메시지 변형 — 요구되지 않으면(Shipping 은 늘) define 도 PSO 변형도 없다.
        if ( info.hasFlag( RenderPassTraitFlag::kGPUMessageTarget ) && GPUMessageChannel::isRequested() )
            selection._listDefine.push_back( kGPUMessagesDefine );
        return selection;
    }

    RenderPassShaderSelection selectRenderPassComputeShader( const RenderGraphPassDesc& passDesc, const EngineDefaultAssets& engineDefaultAssets )
    {
        RenderPassShaderSelection selection = selectRenderPassShader( passDesc._type, &passDesc, engineDefaultAssets );
        const string              computeDefine{ kPassComputeImplementationDefine };
        if ( VectorUtil::contains( selection._listDefine, computeDefine ) == false )
            selection._listDefine.push_back( computeDefine );
        return selection;
    }

    bool isDiagnosticPassRequested( RenderPassType type )
    {
        const RenderPassTypeInfo& info = getRenderPassTypeInfo( type );
        if ( info.hasFlag( RenderPassTraitFlag::kDiagnostic ) == false )
            return false;
        switch ( type )
        {
            case RenderPassType::NanCheck:
                return GPUNanCheck::isRequested();
            default:
                return false;
        }
    }

    bool passAppliesTonemap( const RenderGraphPassDesc& passDesc )
    {
        if ( passDesc._type == RenderPassType::Tonemap )
            return true;
        if ( passDesc._type != RenderPassType::Present )
            return false;
        const string_view define{ kPostTonemapDefine };
        for ( const string& permutation : passDesc._listPermutation )
        {
            // "이름" 또는 "이름=값" — 이름 부분만 비교한다(post_chain.hlsl 은 `defined()` 로 본다).
            const string_view name = string_view{ permutation }.substr( 0, permutation.find( '=' ) );
            if ( name == define )
                return true;
        }
        return false;
    }
} // namespace sw
