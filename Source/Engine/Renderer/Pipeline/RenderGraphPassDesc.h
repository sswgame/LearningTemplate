/**
 * @file RenderGraphPassDesc.h
 * @brief 파이프라인 XML 과 렌더 패스 XML 이 함께 쓰는 패스 종류 · 첨부 · 패스 노드 서술을 정의합니다.
 */
#pragma once
#include "Core/Container/string.h"
#include "Core/Container/vector.h"
#include "Core/Math/VectorMath.h"

#include "Engine/Foundation/Foundation.h"
#include "Engine/Graphics/RHI/RHIDescriptors.h"
#include "Engine/Reflection/ReflectionMacros.h"
#include "Engine/Renderer/Graph/RenderGraph.h"

namespace sw
{
    /**
     * @enum RenderPassType
     * @brief 파이프라인 XML 의 `_type` 이 가리키는 패스 종류입니다.
     * @details 디스패치와 PSO 생성이 같은 값을 본다. 주의: 두 곳이 각자 문자열을 비교하면 받아 주는 표기가 갈려,
     *          디스패치는 되는데 PSO 는 desc 를 찾지 못하고 기본 포맷으로 만들어진다. 리플렉션이
     *          문자열과 값의 변환을 제공하므로 파서도 검증도 이 테이블만 보면 된다.
     * @note 이름은 널리 쓰이는 표기 하나로 통일한다. 아직 개발 중이라 여러 표기를
     *       받아 줄 이유가 없고, 표기가 갈리는 순간 "어느 쪽으로 적었나" 를 매번 확인해야 한다.
     *       XML 에 적히는 철자가 곧 열거자 이름이고, 여기에 없는 표기는 읽기 오류다. 적지 않으면 Invalid 로 남아 RenderPipelineAsset::validate 가 잡는다.
     * @note 이름을 바꾸면 XML 을 새 이름으로 다시 쓴다. `ENUM( ValueAlias = "Old:New" )` 는 실제 게임 데이터가 생겨
     *       다시 쓸 수 없게 된 뒤에 쓰는 수단이다.
     */
    ENUM()
    enum class RenderPassType : uint32
    {
        Invalid = 0, ///< 알 수 없는 표기이며 검증에서 오류로 보고한다.

        Shadow,
        DepthPrepass,
        ForwardOpaque,
        GBuffer,
        Lighting,
        Transparent,
        SSAO,
        Bloom,
        /// @brief 블룸의 밝은 부분을 반해상도 첨부(`BloomBright`)에 구합니다(post_bloom.hlsl 의 `SW_PASS_BLOOM_DOWNSAMPLE`). Bloom 패스가 그것을 업샘플해 더합니다.
        BloomDownsample,
        Outline,
        TAA,
        Tonemap,
        Present,
        /// @brief 인버티드 헐(inverted hull, 뒤집은 껍질) 외곽선입니다. 씬 메시 중 퍼뮤테이션에 `MATERIAL_OUTLINE` 이 있는 배치만 노멀 방향으로 부풀려 앞면을 컬링해 그립니다(toon.hlsl).
        MeshOutline,
        /**
         * @brief 하늘(`SkyComponent`)입니다. 불투명 패스 뒤 · 반투명 앞에 풀스크린 삼각형을 깊이 1 에 그려, 아무것도 그리지 않은 곳(깊이 클리어 1)만 칠합니다(sky.hlsl).
         *        깊이는 테스트만 하고(LessEqual) 쓰지 않으므로 `_depthAttachment` 가 있어야 합니다(검증). 씬에 하늘이 없으면 그리지 않습니다(배경은 클리어 색).
         */
        Sky,
        /**
         * @brief 화면 2D(UI, 월드 글자)를 주 시점 출력(백버퍼, 게임 뷰 RT, 스크린샷 캡처)에 **기존 내용을 유지한 채(Load)** 그립니다. Present(톤맵) 뒤에 두고,
         *        Swapchain 을 쓰는 마지막 패스여야 합니다(검증). 추가 뷰에서는 그리지 않습니다(UI 는 주 시점에만 있습니다).
         */
        Canvas,
        /**
         * @brief 디퍼드에서 G버퍼로 나타낼 수 없는 머티리얼(퍼뮤테이션에 `MATERIAL_FORWARD_ONLY` — 툰의 계단 · 림 · 맷캡)만 포워드 셰이딩으로 그리는 불투명 패스입니다.
         *        G버퍼 패스는 그 배치를 빼므로 조명 패스 뒤에 두고 같은 장면 깊이를 테스트 · 쓰기 합니다(유니티 URP 디퍼드의 `UniversalForwardOnly` 패스와 같은 자리).
         */
        ForwardOnlyOpaque,
        /**
         * @brief 피사계 심도입니다. HDR 색(`SourceColor`)과 장면 깊이(`SceneDepth`)를 읽어 초점면에서 먼 픽셀을 착란원 반지름만큼 흐린 같은 형식의 색을 냅니다(depth_of_field.hlsl).
         *        렌즈 값은 뷰 카메라의 `CameraDepthOfFieldValues` 이고 패스 상수 `g_CameraLens` 로 받습니다. 조리개 0 · 직교 · 후처리를 끈 뷰는 패스를 기록하지 않고
         *        출력 이름이 원본을 가리킵니다(`RenderPassTraitFlag::kBypassesToSource` — 출력 타깃도 처음 켜질 때 만듭니다). 디퍼드는 TAA 와 Tonemap 사이, 포워드는 후처리 체인(Present) 앞에 둡니다.
         */
        DepthOfField,
        /**
         * @brief 모션 블러입니다. 불투명 지오메트리 패스가 쓴 화면 속도(Velocity 역할 입력 — 물체마다 지난 월드 행렬 · 스킨 위치)를 따라 원본을 섞습니다(motion_blur.hlsl).
         *        그리지 않은 픽셀(하늘)은 깊이로 복원한 월드 위치를 지난 프레임 뷰-투영(`g_PrevViewProj`)으로 다시 투영한 카메라 속도다.
         *        사용자 설정 `graphics.motionBlur`(`gv_motionBlur`)를 끄거나 후처리를 끈 뷰, 컷, 카메라와 물체가 모두 서 있는 프레임(속도가 모두 0)은
         *        피사계 심도와 같이 패스를 기록하지 않고 출력 이름이 원본을 가리킵니다(`kBypassesToSource`).
         *        디퍼드는 피사계 심도와 Tonemap 사이에, 포워드는 피사계 심도와 후처리 체인(Present) 사이에 둡니다.
         */
        MotionBlur,
        /**
         * @brief 인스턴스 강조 외곽선입니다(highlight_outline.hlsl). 강조 방식이 Outline 인 인스턴스(`GPUInstance::_highlight`)만 뒤집은 껍질로 깊이 테스트해 그립니다.
         *        상호작용 대상의 외곽선(언리얼 CustomDepth + 외곽선 후처리, 유니티 Render Objects 의 자리)이며 불투명 패스 뒤에 둡니다.
         */
        HighlightOutline,
        /** @brief 감각 모드 외곽선입니다. 강조 방식이 Sense 인 인스턴스만 깊이 테스트 없이 그려 벽 너머에서도 보입니다(반투명 뒤, 톤맵 앞에 둡니다). */
        HighlightSense,
        /**
         * @brief 높이 · 거리 지수 안개입니다(fog.hlsl). 장면 깊이로 시선 위 광학 두께를 구해 선언한 컬러 출력 **위에 블렌드**(프리멀티플라이: 안개색 × 불투명도 + 장면 × (1 - 불투명도))합니다 —
         *        그래서 입력은 장면 깊이뿐이고 출력은 앞 패스가 칠한 색 첨부입니다(복사 첨부를 따로 두지 않는다). 불투명 · 하늘 · 반투명 뒤, 후처리 앞에 둡니다.
         *        씬에 안개(`FogComponent` · 물속)가 없거나 후처리를 끈 뷰 · 표면 값 보기에서는 그리지 않습니다(전용 실행: `FramePassExecutor::executeFogPass`).
         *        볼륨 안개를 켠 씬은 볼륨(froxel, `RenderPassType::VolumetricFog` 컴퓨트)의 산란과 투과율을 같은 블렌드로 더합니다.
         */
        Fog,
        /**
         * @brief 장면 색 사본입니다. 원본 색(`SourceColor`)을 선언한 컬러 출력(`SceneColorCopy` 역할)으로 그대로 복사합니다 — 셰이더 · PSO 가 없습니다(`FramePassExecutor::executeSceneColorCopyPass`).
         *        반투명 패스는 장면 색에 섞으며 쓰므로 그 색을 읽을 수 없다. 반투명 패스가 이 출력을 같은 역할의 입력으로 선언하면 물의 굴절이 읽습니다(`g_SceneColorCopyIndex`).
         *        하늘 뒤 · 반투명 앞에 둡니다(언리얼의 Scene Color 사본 · 유니티 URP 의 Opaque Texture 와 같은 자리).
         */
        SceneColorCopy,
        /**
         * @brief 장면 깊이 사본입니다. 장면 깊이(`SceneDepth`)를 선언한 R32 실수 컬러 출력(`SceneDepthCopy` 역할)으로 풀스크린 셰이더(scene_depth_copy.hlsl)가 텍셀 그대로 옮깁니다.
         *        반투명 패스는 장면 깊이를 깊이 붙임으로 쥐고 있어 읽을 수 없다(백엔드마다 읽기 전용 깊이 + SRV 상태를 보장하지 못한다). 반투명 패스가 이 출력을 같은 역할의 입력으로 선언하면
         *        물의 화면 공간 두께(`g_SceneDepthCopyIndex`)가 읽습니다. 반투명 앞에 둡니다(언리얼의 Scene Depth 사본 · 유니티 URP 의 Depth Texture 와 같은 자리).
         */
        SceneDepthCopy,
        /**
         * @brief 화면 왜곡 누적입니다. 반투명 배치 중 머티리얼에 `MATERIAL_DISTORTION` 이 있는 것만 머티리얼 셰이더의 왜곡 훅(`computeMaterialDistortion`)으로 그려
         *        화면 UV 오프셋을 선언한 R16G16 실수 출력(`Distortion` 역할, 클리어 0)에 더한다(가산 블렌드 — 겹친 왜곡이 합쳐진다). 장면 깊이로 테스트만 한다(가려진 왜곡은 없다).
         *        씬에 왜곡 배치가 없으면 기록하지 않는다(`GPUScene::hasDistortionBatches` — 쓰지 않을 때 비용이 없다). 언리얼 Distortion 패스 · 유니티 왜곡과 같은 자리다.
         */
        Distortion,
        /**
         * @brief 화면 왜곡 적용입니다. 장면 색 사본(`SceneColorCopy` 역할)을 왜곡 오프셋(`Distortion` 역할)만큼 비껴 읽어 선언한 컬러 출력(장면 색)에 쓴다(distortion_apply.hlsl).
         *        비껴 읽은 자리의 깊이가 이 픽셀보다 가까우면(앞의 물체) 끌어오지 않는다. 왜곡 누적과 같은 조건으로 돌지 않는다 — 출력이 곧 왜곡 없는 장면 색이라 그대로 둔다(안개와 같은 전용 실행).
         */
        DistortionApply,
        /**
         * @brief 화면 공간 반사입니다(screen_space_reflection.hlsl). G버퍼 노멀로 반사한 광선을 장면 깊이에 대고 나아가 맞은 자리의 장면 색 사본(`SceneColorCopy`)을 읽어
         *        선언한 컬러 출력(장면 색) **위에 프리멀티플라이 블렌드**합니다 — 안개와 같은 모양이라 출력이 장면 색 그대로이고 반투명이 그 위에 그린다. 디퍼드에서 하늘 · 사본 뒤, 반투명 앞에 둡니다.
         *        뷰의 후처리 값 `_screenSpaceReflectionIntensity` 가 0 이거나 후처리를 끈 뷰 · 표면 값 보기에서는 그리지 않습니다(전용 실행: `FramePassExecutor::executeScreenSpaceReflectionPass`).
         *        끄는 길이 "원본을 가리킴"(`kBypassesToSource`)이 아닌 까닭: 그 길은 출력을 그 패스만 써야 하는데(로드 검증 3-6), 반투명이 같은 장면 색에 그려야 한다.
         */
        ScreenSpaceReflection,
        /**
         * @brief 접촉 그림자입니다(contact_shadow.hlsl). 장면 깊이에서 복원한 점마다 접촉 그림자를 켠 빛(`LightShadowSettings::_contactShadowLength`, 넷까지) 쪽으로 화면 공간에서 짧게 걸어
         *        깊이 버퍼에 가리는 것이 있으면 그 빛의 채널을 0 으로 쓴다(언리얼 Contact Shadows · 유니티 HDRP Contact Shadows 와 같은 자리). 그림자 맵 텍셀보다 작은 가림(발치 · 틈)을 잡는다.
         *        출력은 RGBA 첨부 하나(채널 = 빛, `FrameShadowData::populateContactShadows`)이고 조명(Lighting) · 포워드 불투명이 선택 입력(`ContactShadow` 역할)으로 읽는다.
         *        켠 빛이 없거나 후처리를 끈 뷰는 패스를 기록하지 않고 출력이 없다(`kOmittedWhenOff` — 읽는 쪽은 1). 장면 깊이를 다 쓴 뒤(디퍼드 G버퍼 · 포워드 깊이 프리패스) 조명 앞에 둔다.
         */
        ContactShadow,
        /**
         * @brief 파이프라인 XML 이 고른 컴퓨트 셰이더(`_shaderPath` · `_computeEntryPoint`)를 돌립니다 — 코드 없이 데이터만으로 효과를 더하는 패스입니다(언리얼 RDG 의 컴퓨트 패스 자리).
         *        선언한 컬러 출력 하나에 쓰고 입력(최대 둘, 깊이 형식 제외)을 읽습니다. 첨부는 RW 텍스처로 묶입니다(`compute_pass.hlsli` 의 루트 상수). 그룹 수는 첨부 크기를 `_threadGroupSize` 로 나눈 값입니다.
         */
        Compute,
        /**
         * @brief 화면 공간 간접 확산광(SSGI)입니다(ssgi.hlsl). 반투명 앞에 떠 둔 장면 색(`SceneColorCopy`)을 깊이 · G버퍼 노멀로 반구에서 걸어 가까운 면이 튕긴 빛을 모으고,
         *        G버퍼 알베도를 곱해 선언한 컬러 출력(조명 첨부)에 **더한다**(XML `_blendPreset="Additive"`). 프로브 GI(먼 빛)는 조명 패스가 이미 그렸고 이 패스는 가까운 튐만 더한다
         *        (언리얼 UE4 의 화면 공간 GI 가 라이트맵 · 스카이 위에 더하는 것과 같은 자리). 세기 · 반경 · 걸음 수는 XML 퍼뮤테이션 값이다(셰이더에 기본값이 없다 — `kRequiresPassDesc`).
         *        출력(조명 첨부)을 여러 패스가 쓰므로 뷰마다 건너뛰는 효과가 아니다 — 켜고 끄는 것은 파이프라인 XML 이다(engine/pipeline/deferredpipeline_gi.xml). SceneColorCopy 뒤 · 반투명 앞에 둔다.
         */
        ScreenSpaceGI,
        /**
         * @brief 빛마다의 렌즈 플레어 스프라이트입니다(lens_flare.hlsl). 플레어를 단 빛(`LightComponent::_lensFlarePath`)을 주 시점에 투영해 데이터(`LensFlareAsset`)의 원소를
         *        빛 → 화면 가운데 축 위에 그리고, 빛 자리 둘레의 장면 깊이로 가림을, 화면 가장자리로 감쇠를 곱한다(`LensFlareSpriteBuffer`). 선언한 HDR 컬러 출력에 가산 블렌드로 더한다
         *        (XML `_blendPreset="Additive"`) — 톤맵 · 블룸 앞에 둔다. 블룸에서 고스트를 뽑는 화면 효과와 별개다. 플레어를 단 빛이 없으면 그리지 않는다(출력 그대로). 주 시점만 그린다.
         */
        LensFlare,

        // --- 아래는 뷰 패스(`_listPass`)로 쓸 수 없다. 프레임 패스(`isFramePassType` — 깊이 히스토리 · 가림 타일 · 입자 · 애니메이션 · 모프 · 스킨 · 컬링 · 라이트 그리드 ·
        //     볼륨 안개 · 프로브 · 마커)는 `_listFramePass` 에 선언하고, 나머지는 엔진 내부 PSO 슬롯이라 XML 에 나올 수 없다(검증이 거부한다). ---
        ForwardOpaqueNoDepthWrite,
        GPUCull,
        /// @brief GPUScene 인스턴스 애니메이션 컴퓨트(instance_animation.hlsl)입니다. 인스턴스마다 다른 각속도로 회전시킵니다.
        InstanceAnimation,
        /// @brief 배치 안의 가시 인스턴스를 CPU 정렬 순서(인스턴스 번호)로 되돌리는 컴퓨트(instance_sort.hlsl)입니다. 투명 블렌딩 순서를 맞춥니다.
        InstanceSort,
        /// @brief 레스트 정점을 읽어 변형 결과를 쓰는 컴퓨트(mesh_morph.hlsl)입니다. 정점 셰이더가 그 결과를 풀링합니다.
        MeshMorph,
        /// @brief 스킨드 메시의 바인드 포즈 정점을 본 팔레트로 블렌딩해 모프 풀의 스킨 구간에 쓰는 컴퓨트(mesh_skin.hlsl)입니다.
        MeshSkin,
        /// @brief 뷰마다의 라이트 그리드(클러스터드 라이팅의 빛 목록)를 만드는 컴퓨트(light_grid.hlsl)입니다. 그룹 하나가 뷰 하나입니다.
        LightGrid,
        /// @brief GPU 방출기의 입자 시뮬레이션 컴퓨트(vfx_simulate.hlsl)입니다. 그룹 하나가 방출기 하나이고 간접 인자를 씁니다(`VFXGPUSimulation`).
        VFXSimulation,
        /// @brief GPU 메시 입자를 메시 인스턴스 배치의 인스턴스 칸에 쓰는 컴퓨트(vfx_mesh_instance.hlsl)입니다. 인스턴스 애니메이션 · 컬링 앞에 돈다.
        VFXMeshInstance,
        /// @brief 뷰의 지난 프레임 장면 깊이를 R32 실수 RT(깊이 히스토리)로 옮기는 풀스크린(depth_history.hlsl)입니다. 프레임 그래프 리스트에서 그린다.
        DepthHistory,
        /**
         * @brief 자동 노출의 측정 · 적응 풀스크린(auto_exposure.hlsl)입니다. HDR 원본의 평균 로그 휘도를 재어 지난 EV 에서 목표 EV 로 다가간 값을 뷰의 1x1 R32 실수 RT 에 씁니다.
         *        톤맵하는 패스(Tonemap, 후처리 체인 Present)가 자기 그리기 바로 앞에 같은 리스트로 기록한다(`FramePostProcess::recordAutoExposure`).
         */
        AutoExposure,
        /// @brief FSR 1 의 EASU(fsr1.hlsl) — Present 원본(배율 크기)을 뷰포트 크기 중간 RT 로 키웁니다. Present 가 렌더 패스를 열기 전에 기록한다(`FrameRenderer::recordUpscaleEasu`).
        UpscaleEasu,
        /// @brief FSR 1 의 RCAS(fsr1.hlsl 의 `SW_PASS_FSR_RCAS`) — 중간 RT 를 선명하게 해 Present 대상에 그립니다. 대상 포맷마다 PSO 가 다른 출력 패스다(Present 와 같다).
        UpscaleRcas,
        /**
         * @brief 가림 타일 컴퓨트(occlusion_tiles.hlsl)입니다. 뷰의 깊이 히스토리(지난 프레임 장면 깊이)를 16 px 타일마다 최대 깊이 하나로 줄여 구조 버퍼에 씁니다.
         *        컬링(gpu_cull.hlsl)이 경계 구를 그 프레임의 뷰-투영으로 옮겨 덮는 타일보다 멀면 가려졌다고 본다(`FramePassExecutor::recordOcclusionTiles`).
         */
        OcclusionTiles,
        /**
         * @brief 볼륨 안개 컴퓨트(volumetric_fog.hlsl)입니다. 뷰의 froxel 격자(화면 칸 × 깊이 조각)마다 안개 밀도와 라이트 산란(방향광 + 라이트 그리드의 점광 · 스폿)을 구하고
         *        깊이 방향으로 누적해 (산란 rgb, 투과율)을 구조 버퍼에 쓴다. 안개 패스(`Fog`)가 그 버퍼를 읽는다(`FramePostProcess::dispatchVolumetricFog`).
         */
        VolumetricFog,
        /**
         * @brief 실시간 라이트 프로브 갱신 컴퓨트(probe_update.hlsl)입니다. 프로브마다 점유 복셀(`SceneVoxelGrid`)로 광선을 쏘아 SH9 기록을 갱신하고, 소비 셰이더의 프로브 원소로 바꿔
         *        프로브 버퍼(t13)에 쓴다(`RealtimeProbeUpdater`). 라이트 업로드 뒤 · 조명 패스 앞에 한 번 돈다.
         */
        ProbeUpdate,
        /**
         * @brief 화면 마커 투영 컴퓨트(canvas_marker.hlsl)입니다. 마커마다 월드 점을 게임 카메라로 투영해 가시 판정과 함께 캔버스 사각형 버퍼의 앵커 원소(x, y, 배율)에 씁니다(`CanvasRenderer::recordMarkerProjection`).
         *        캔버스 업로드 뒤 · 캔버스 그리기 앞의 프레임 그래프에서 `gv_uiMarkerGpuProjection` 로 올린 마커가 있는 프레임에만 돈다.
         */
        CanvasMarker,
        /**
         * @brief 그림자 캐시 복원 풀스크린(shadow_cache_restore.hlsl)입니다. 점광 · 스폿 타일 칸을 뷰포트로 잡고 캐시 층(정적 메시의 깊이, `ShadowCache`)의 텍셀을
         *        픽셀 깊이(SV_Depth)로 그림자 아틀라스에 옮긴다 — 그 위에 움직이는 메시만 그린다(`FramePassExecutor::drawShadowTiles`). 컬러 출력이 없고 픽셀 스테이지가 있다(`kWritesPixelDepth`).
         */
        ShadowCacheRestore,
        /**
         * @brief 패스 출력의 NaN · Inf 감시 컴퓨트(gpu_nan_check.hlsl)입니다. 부동소수 컬러 출력 텍스처를 텍셀마다 훑어 NaN · Inf 가 든 텍셀 수를 슬롯별 카운터에 더합니다(`GPUNanCheck`, 언리얼 r.CheckNaN 선).
         *        `-gv_nanCheck=1` 일 때만 PSO 가 만들어지고, 그래프가 끝난 뒤 주 시점의 패스 출력마다 한 번 돈다(`FramePassExecutor::scanPassOutputsForNan`). 네이티브 bindless(DX12 · Vulkan)만 쓴다.
         */
        NanCheck,
    };

    /**
     * @enum RenderPassExecutionMode
     * @brief 일반 풀스크린 패스의 실행 방식입니다. 파이프라인 XML 의 `_implementation` 이 적는다(블룸 다운샘플 · 피사계 심도 — 표의 `kComputeImplementation`).
     * @details Pixel 은 풀스크린 삼각형, Compute 는 같은 셰이더 파일의 `CSMain`(그룹 공유 타일)을 출력 크기로 디스패치한다. 어느 쪽이 나은지는 측정이 정하고 XML 한 줄로 바꾼다.
     *          Compute 는 네이티브 bindless(DX12 · Vulkan)에서만 돌고, 에뮬 백엔드는 Pixel 이 돈다(능력이 정한 길).
     */
    ENUM()
    enum class RenderPassExecutionMode : uint8
    {
        Pixel,   ///< 풀스크린 삼각형(기본)
        Compute, ///< 컴퓨트 디스패치
    };

    /**
     * @brief 파이프라인 출력에 쓰는 예약어입니다. 디바이스가 주는 백버퍼를 가리킵니다.
     * @details `_attachments` 에 선언되지 않는 유일한 출력이라 검증에서 예외로 다룬다.
     */
    inline constexpr const utf8* kSwapchainOutputName = "Swapchain";

    /** @brief 뷰 그래프(`_listPass`)의 `_type` 으로 쓸 수 있는 값인지 확인합니다. 뷰마다 다시 도는 패스입니다(내부 슬롯 · 프레임 패스 · Invalid 는 제외합니다). */
    constexpr bool isViewPassType( RenderPassType type )
    {
        return type != RenderPassType::Invalid && static_cast<uint32>( type ) <= static_cast<uint32>( RenderPassType::Compute );
    }

    /**
     * @brief 프레임 그래프(`_listFramePass`)의 `_type` 으로 쓸 수 있는 값인지 확인합니다. 프레임에 한 번 모든 뷰의 몫을 기록하는 패스입니다(`FrameGraphExecutor`).
     * @details 이 목록이 곧 "그래프 앞에 C++ 가 박아 두던 일" 의 전부다. 파이프라인이 선언하지 않은 종류는 PSO 도 없고 돌지 않는다(`FramePSOManager::createPassPSOs`).
     *          InstanceSort · VFXMeshInstance 는 GPUCull · VFXSimulation 패스 안의 두 번째 디스패치라 따로 선언하지 않는다.
     */
    constexpr bool isFramePassType( RenderPassType type )
    {
        switch ( type )
        {
            case RenderPassType::DepthHistory:
            case RenderPassType::OcclusionTiles:
            case RenderPassType::VFXSimulation:
            case RenderPassType::InstanceAnimation:
            case RenderPassType::MeshMorph:
            case RenderPassType::MeshSkin:
            case RenderPassType::GPUCull:
            case RenderPassType::LightGrid:
            case RenderPassType::VolumetricFog:
            case RenderPassType::ProbeUpdate:
            case RenderPassType::CanvasMarker:
                return true;
            default:
                return false;
        }
    }

    /** @brief 파이프라인 XML 의 `_type` 으로 쓸 수 있는 값인지 확인합니다(뷰 패스 또는 프레임 패스 — 내부 슬롯과 Invalid 는 제외합니다). */
    constexpr bool isPipelinePassType( RenderPassType type )
    {
        return isViewPassType( type ) || isFramePassType( type );
    }

    /// @brief 렌더 패스 어태치먼트 하나(이름, 포맷, 첫 사용의 로드 · 저장 동작, 초기값)입니다.

    REFLECT()
    struct RenderPassAttachment
    {
        REFLECT_BODY();
        PROPERTY( InheritKey )
        string _name = "ColorAttachment0";

        PROPERTY()
        RHIFormat _format{ RHIFormat::R8G8B8A8_UNORM };

        PROPERTY()
        float4 _clearColor = { 0.1f, 0.2f, 0.3f, 1.0f };

        PROPERTY()
        float32 _clearDepth{ 1.0f }; ///< 깊이 첨부를 지우는 값

        PROPERTY()
        uint8 _clearStencil{ 0 }; ///< 깊이 · 스텐실 첨부를 지우는 스텐실 값

        /**
         * @brief 프레임에 이 첨부를 처음 여는 패스의 로드 동작입니다. 그 뒤의 패스는 앞 패스가 쓴 것을 읽는다(`Load`).
         * @details `DontCare` 는 패스가 모든 픽셀을 다시 쓸 때, `Load` 는 지난 프레임 내용을 이어 쓸 때 고른다(언리얼 RDG 의 ERenderTargetLoadAction, 유니티 RenderBufferLoadAction).
         */
        PROPERTY()
        RHIRenderPassLoadOp _loadOp{ RHIRenderPassLoadOp::Clear };

        /** @brief 이 첨부를 여는 패스의 저장 동작입니다. `DontCare` 는 이 첨부를 쓰는 패스가 하나이고 읽는 패스가 없을 때만 받습니다(검증). */
        PROPERTY()
        RHIRenderPassStoreOp _storeOp{ RHIRenderPassStoreOp::Store };

        /** @brief 첨부를 만들고 여는 데 쓰는 값들입니다. */
        [[nodiscard]] RHIAttachmentOps getOps() const { return RHIAttachmentOps{ _clearColor, _clearDepth, _clearStencil, _loadOp, _storeOp }; }

        /**
         * @brief 이 첨부가 패스에 바인딩될 때의 역할입니다(`RenderPassInputRole` 이름: GBufferAlbedo, GBufferNormal, GBufferMaterial, ShadowMap, AmbientOcclusion,
         *        BloomBright, SceneDepth, SourceColor). 비어 있으면 원본 이름(GBufferAlbedo 등)과 포맷으로 정합니다.
         * @details 역할을 **이름으로만** 정하면 G버퍼, 그림자 맵, AO 첨부의 이름을 바꿀 때 Lighting 의 입력 계약이 깨진다(역할 없는 컬러는
         *          모두 SourceColor). 언리얼 RDG 는 패스 파라미터 구조체의 멤버로, 유니티 RenderGraph 는 셰이더 프로퍼티 이름으로 텍스처를 바인딩한다.
         *          어느 쪽도 텍스처의 이름이 바인딩을 정하지 않는다. 여기서는 첨부가 자기 역할을 선언하고, 이름은 파이프라인이 자유롭게 짓는다.
         */
        PROPERTY( SkipIfEmpty )
        string _role;

        /**
         * @brief 첨부 크기는 프레임 크기를 이 값으로 나눈 값(올림)입니다. 1(기본), 2, 4 만 받습니다(`RenderPipelineAsset::validate`).
         * @details 반해상도 후처리(블룸, SSAO)처럼 프레임보다 작게 그려도 되는 타깃용이다(언리얼 RDG 에서 텍스처 Extent 를 뷰 크기의
         *          분수로 잡는 것에 해당한다). 한 패스의 출력(컬러, 깊이)은 모두 같은 나눗수여야 한다. 렌더 패스의 타깃은 크기가 같아야 하기 때문이다.
         *          패스는 출력 첨부의 크기로 뷰포트를 잡고, 입력은 UV 로 읽으므로 크기가 달라도 된다(선형 샘플러가 늘리고 줄인다).
         */
        PROPERTY()
        uint32 _resolutionDivisor{ 1 };
    };
} // namespace sw

namespace sw
{
    /**
     * @brief 프레임 그래프의 자원 하나(이름, 수명 종류)입니다. 파이프라인 XML 의 `_listResource` 원소입니다.
     * @details 첨부가 아닌 자원 — 서브시스템이 가진 버퍼(인스턴스 · 라이트 · 컬링 결과)와 프레임을 넘는 히스토리(깊이 히스토리 · 가림 타일 · 입자 상태 · 프로브) — 의 이름이다.
     *          프레임 패스가 이 이름으로 읽기 · 쓰기를 선언하면 그래프가 순서를 정한다. 그래프는 버퍼를 만들지 않는다(주인은 서브시스템이다 — 언리얼 RDG 의 외부 버퍼 등록과 같다).
     *          `_kind` 는 Persistent 또는 External 이다(Transient 는 첨부의 몫이라 검증이 막는다).
     */
    REFLECT()
    struct RenderGraphResourceDesc
    {
        REFLECT_BODY();
        PROPERTY( InheritKey )
        string _name;

        PROPERTY()
        RenderGraphResourceKind _kind{ RenderGraphResourceKind::External };
    };

    /** @brief 첨부 나눗수(`RenderPassAttachment::_resolutionDivisor`)가 받는 값인지 반환합니다(1, 2, 4). */
    constexpr bool isSupportedResolutionDivisor( uint32 divisor )
    {
        return divisor == 1u || divisor == 2u || divisor == 4u;
    }

    /**
     * @brief 렌더 파이프라인 그래프 안의 패스 노드 하나입니다.
     * @details 그래프 연결(입력과 출력)과 PSO 설정(셰이더, 진입점, 렌더 상태, 퍼뮤테이션)을 담는다. 렌더 상태의 칸은 기반 `RHIFixedRenderState` 이고 머티리얼과 같은 형식이다.
     *          **여기 적힌 값이 곧 실행되는 값이다**(`FramePSOManager::applyPassRenderState`) —
     *          코드를 고치지 않고 데이터로 패스를 바꾼다. 패스 실행이 받아 줄 수 없는 조합(깊이를 붙이지 않는 패스의 깊이 테스트 등)은 무시하지 않고 읽기 오류다(`RenderPipelineAsset::validate`).
     */
    REFLECT()
    struct RenderGraphPassDesc : public RHIFixedRenderState
    {
        REFLECT_BODY();
        PROPERTY( InheritKey )
        string _name = "Pass";

        /** @brief 패스 종류입니다. 디스패치와 PSO 생성이 같은 값을 본다. */
        PROPERTY()
        RenderPassType _type{ RenderPassType::Invalid };

        PROPERTY( SkipIfEmpty )
        vector<string> _listInput;

        PROPERTY( SkipIfEmpty )
        vector<string> _listOutput;

        /**
         * @brief 이 패스가 바인딩할 뎁스 첨부 이름입니다. **비어 있으면 뎁스 없이 엽니다.**
         * @details 패스 타입마다 코드에 박으면("GBuffer 면 SceneDepth") "이 패스는 일부러 뎁스를 쓰지 않는다" 를
         *          표현할 방법이 없어, DepthPrepass 를 넣거나 빼는 구성을 바꾸려면 엔진 코드를 고쳐야 한다.
         *          선언으로 두면 파이프라인 XML 만으로 바뀐다.
         * @note 읽기와 쓰기 여부는 `_listInput` 과 `_listOutput` 이 따로 말해 준다. Transparent 는 SceneDepth 를
         *       **입력으로 읽으면서** 뎁스로 바인딩한다(테스트만 하고 쓰지 않는다). 그래서 출력에서
         *       유추하지 않고 별도 필드로 둔다.
         */
        PROPERTY( SkipIfEmpty )
        string _depthAttachment;

        /**
         * @brief `_depthAttachment` 를 intern 해 둔 값입니다. XML 로드 시 RenderPipelineAsset 가 채웁니다.
         * @details 직렬화 대상이 아니다(원본 철자는 `_depthAttachment` 가 가지고 있다). 이 이름은 패스마다 어태치먼트 클리어 여부를
         *          판정하는 데 쓰이는데, 매 프레임 다시 intern 하면 그때마다 전역 문자열 레지스트리의
         *          샤드 뮤텍스를 잡게 된다.
         */
        hashed_string _resolvedDepthAttachment;

        /**
         * @brief 해석한 첨부 하나(이름, 역할)입니다. 입력 목록과 컬러 출력 목록이 같은 구조를 씁니다.
         * @details 역할 값은 `RenderPassInputRole` 인데 이 헤더가 그 enum 을 모르므로(계약 헤더가 이 헤더를 include 한다) 정수로 둔다.
         *          역할은 첨부의 `_role` 선언, 원본 이름, 포맷 순으로 정한다(`resolveRenderPassInputRole`).
         */
        struct ResolvedAttachment
        {
            hashed_string _attachment;
            uint8         _role{ 0 };
        };

        /**
         * @brief `_listInput` 하나하나의 (첨부 이름, 역할)입니다. XML 로드 시 RenderPipelineAsset 가 채웁니다.
         * @details 직렬화 대상이 아니다. 실행은 이 목록을 그대로 바인딩하고(역할 이름이 곧 셰이더의 `g_<Role>Index` 다),
         *          검증은 패스 타입의 계약(RenderPassInputSignature)과 대조한다. 둘이 같은 해석을 본다.
         */
        vector<ResolvedAttachment> _listResolvedInput;

        /** @brief `_listOutput` 을 intern 해 둔 값입니다. 풀스크린 패스가 "첫 번째로 존재하는 출력" 을 타깃으로 고릅니다. */
        vector<hashed_string> _listResolvedOutput;

        /**
         * @brief `_listOutput` 중 **컬러 첨부**만 선언 순서대로 모은 목록입니다(뎁스 포맷, 스왑체인, 선언되지 않은 이름은 빠집니다). 역할도 함께 가집니다.
         *        XML 로드 시 RenderPipelineAsset 가 채웁니다.
         * @details 지오메트리 패스(ForwardOpaque, GBuffer, Transparent)는 이 목록에서 컬러 타깃을 고른다. GBuffer 는 역할(GBufferAlbedo,
         *          GBufferNormal, GBufferMaterial)로 고르고, 역할이 없으면 선언 순서([0] 알베도, [1] 노멀, [2] 머티리얼)로 고른다. 주의: 첨부 이름(SceneColor, GBufferAlbedo 등)을 코드에
         *          박으면 다른 이름을 쓰는 파이프라인에서 없는 첨부를 열고, 없는 첨부의 핸들 0은 백버퍼라 화면에 그린다.
         */
        vector<ResolvedAttachment> _listResolvedColorOutput;

        /** @brief HLSL 경로(engine/... 또는 common/...)입니다. 비면 FrameRenderer 가 패스 타입의 기본 셰이더를 씁니다. */
        PROPERTY( SkipIfEmpty, AssetPath )
        string _shaderPath;

        /** @brief 정점 셰이더 진입점입니다. 비면 스테이지 기본값을 씁니다(`resolveEntryPoint`, getShaderStageInfo 테이블). */
        PROPERTY( SkipIfEmpty )
        string _vertexEntryPoint;

        /** @brief 픽셀 셰이더 진입점입니다. 비면 스테이지 기본값입니다. */
        PROPERTY( SkipIfEmpty )
        string _pixelEntryPoint;

        /** @brief 컴퓨트 진입점입니다(`Compute` 패스만 적는다). 비면 스테이지 기본값입니다. */
        PROPERTY( SkipIfEmpty )
        string _computeEntryPoint;

        /** @brief 지오메트리 진입점입니다. 적으면 이 패스에 그 스테이지가 붙습니다(소스는 패스 셰이더). 머티리얼 셰이더로 그리는 패스 · 컴퓨트 패스는 쓸 수 없습니다(검증). */
        PROPERTY( SkipIfEmpty )
        string _geometryEntryPoint;

        /** @brief 헐 진입점입니다. 도메인과 함께 적고, 제어점 셋의 패치로 그립니다(검증). */
        PROPERTY( SkipIfEmpty )
        string _hullEntryPoint;

        /** @brief 도메인 진입점입니다. */
        PROPERTY( SkipIfEmpty )
        string _domainEntryPoint;

        /** @brief `Compute` 패스의 스레드 그룹 한 변(`numthreads` 와 같아야 한다)입니다. 그룹 수는 출력 크기를 이 값으로 나눠 올림합니다. */
        PROPERTY( Min = 1 )
        uint32 _threadGroupSize{ 8 };

        /** @brief 셰이더 매크로(퍼뮤테이션)입니다. "NAME" 또는 "NAME=VALUE" 형태로 적습니다. */
        PROPERTY( SkipIfEmpty )
        vector<string> _listPermutation;

        /**
         * @brief 모든 렌더 타깃의 블렌드 프리셋 이름입니다(`engine/renderstate/blendpresets.xml`). 비면 불투명입니다. `_listBlendTarget` 과 함께 적을 수 없습니다(검증).
         * @details 셰이더의 출력 규약(알파를 곱했는가)에 맞춰 고른다 — 캔버스 · 안개는 `Premultiplied` 다.
         */
        PROPERTY( SkipIfEmpty )
        string _blendPreset;

        /** @brief 렌더 타깃마다 따로 적는 블렌드입니다. 하나면 모든 타깃에 같은 값이고, 둘 이상이면 컬러 출력 수와 같아야 합니다(검증). */
        PROPERTY( SkipIfEmpty )
        vector<RHIBlendTargetState> _listBlendTarget;

        /** @brief `RHIFixedRenderState` 칸과 블렌드를 합친 PSO 값입니다. XML 로드 시 RenderPipelineAsset 가 채웁니다(직렬화 대상이 아니다). */
        RHIRenderState _resolvedRenderState;

        /**
         * @brief 이 패스가 도는 최소 후처리 품질(`gv_postQuality`, 사용자 설정 graphics.postQuality 의 선택지 순번 0 낮음 ~ 3 최고)입니다. 0 이면 늘 돕니다.
         * @details 품질이 이보다 낮으면 후처리를 끈 뷰와 같이 건너뛴다(원본을 넘기거나 기본 클리어 — SSAO 는 가림 없음). 그래서 건너뛸 수 있는 타입
         *          (`RenderPassTraitFlag::kSkipsWithoutPostProcess`)만 0 보다 큰 값을 받는다(검증이 막는다). 언리얼 sg.PostProcessQuality 에 해당한다.
         */
        PROPERTY()
        int32 _minPostQuality{ 0 };

        /**
         * @brief 이 패스가 도는 큐입니다. `AsyncCompute` 는 프레임 패스 중 컴퓨트 종류만 받는다(검증) — 뷰 패스 · 그래픽스 드로우(깊이 히스토리)는 그래픽스 큐다.
         * @details 능력 `RHICapabilities::_bAsyncCompute` 가 있는 백엔드(DX12 · 큐가 둘인 Vulkan 장치)는 컴퓨트 큐에서 그래픽스와 겹쳐 돈다. 없으면(DX11 · GL · WebGPU)
         *          그래픽스 큐에서 같은 순서로 돈다 — 능력 표가 정한 길이다. 포크 · 조인은 그래프 간선이 정한다(`RenderGraph::getQueueSteps`). 언리얼 `ERDGPassFlags::AsyncCompute` 자리.
         */
        PROPERTY()
        RHIQueueType _queue{ RHIQueueType::Graphics };

        /** @brief 일반 풀스크린 패스의 실행 방식입니다(`RenderPassExecutionMode`). Compute 는 표가 컴퓨트 구현을 가진 종류만 받는다(검증). */
        PROPERTY()
        RenderPassExecutionMode _implementation{ RenderPassExecutionMode::Pixel };
    };
} // namespace sw
