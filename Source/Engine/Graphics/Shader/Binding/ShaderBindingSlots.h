/**
 * @file ShaderBindingSlots.h
 * @brief 셰이더 바인딩 계약의 C++ 쪽입니다. `Content/engine/shaders/bindingslots.hlsli` 를 **그대로 include** 합니다.
 * @details 번호는 이 파일에 없다. HLSL 과 C++ 가 같은 파일을 읽으므로 "수동 동기" 가 없다.
 *          백엔드 4개는 여기 constexpr 로만 바인딩 위치를 정하고, ShaderBindingValidator 가 쿠킹된 바이너리의
 *          리플렉션을 이 값과 대조한다(런타임 로드 시와 테스트).
 *
 *          모델은 언리얼 GPUScene 방식이다. 셰이더 선언은 네 백엔드에서 같고(register b#/t#/u#), 드로우별 데이터는
 *          바인딩이 아니라 버퍼(인스턴스 t4, 머티리얼 t5)에서 인덱스로 읽는다. 백엔드는 리플렉션이 준 슬롯을
 *          각자의 방식으로 바인딩한다. DX12 는 루트 CBV 와 디스크립터 테이블, Vulkan 은 세트 0 의 시프트된 binding,
 *          DX11 과 GL 은 슬롯이다. 텍스처만 DX12 와 Vulkan 이 무제한 배열(`bindless::`)이고 DX11 과 GL 은 고정 슬롯이다.
 */
#pragma once
#include "Core/Foundation/Types.h"

#include "bindingslots.hlsli"

namespace sw
{
    namespace shaderslot
    {
        // ------------------------------------------------------------------------------
        // 1) 상수 버퍼(b#, space0)
        // ------------------------------------------------------------------------------
        inline constexpr uint32 kPassConstantBuffer      = SW_SLOT_PASS_CB;
        inline constexpr uint32 kMaterialConstantBuffer  = SW_SLOT_MATERIAL_CB;
        inline constexpr uint32 kComputeConstantBuffer   = SW_SLOT_COMPUTE_CB;
        inline constexpr uint32 kConstantBufferSlotCount = SW_CB_SLOT_COUNT;
        /// @brief setComputeRootConstants 전용 루트/푸시 상수입니다. DX12 는 b0 space2, Vulkan 은 푸시 상수, DX11 과 GL 은 UBO 로 에뮬레이션합니다.
        inline constexpr uint32 kRootConstantRegister = SW_SLOT_ROOT_CB;
        inline constexpr uint32 kRootConstantSpace    = SW_SPACE_ROOT_CB;
        inline constexpr uint32 kRootConstantDwords   = SW_ROOT_DWORD_COUNT;
        /// @brief DX11 과 GL 이 루트 상수를 담는 상수 버퍼 슬롯입니다(b#, space0). setComputeRootConstants 가 여기에 바인딩합니다.
        inline constexpr uint32 kRootConstantEmulationSlot = SW_SLOT_ROOT_CB_EMUL;

        // ------------------------------------------------------------------------------
        // 2) SRV(t#, space0): 네 백엔드 공통 슬롯
        // ------------------------------------------------------------------------------
        inline constexpr uint32 kEngineTexture0     = SW_SLOT_ENGINE_TEX0;
        inline constexpr uint32 kEngineTextureCount = SW_ENGINE_TEXTURE_SLOT_COUNT;
        /**
         * @brief 엔진 텍스처 슬롯 t0..t3(풀스크린 입력, G버퍼, 깊이, 그림자 맵)을 읽는 샘플러(SW_SAMPLER_*)입니다. 선형 필터와 클램프 주소를 씁니다.
         * @details 네이티브 bindless(DX12, Vulkan)는 `Bindless::sampleIndex` 가 이 샘플러로 읽는다. 슬롯 결합 샘플러를 쓰는 DX11 과 GL 은 엔진이 t0..t3 에
         *          같은 샘플러를 바인딩한다(언리얼 후처리 입력의 Bilinear, Clamp 정적 샘플러). 랩이면 화면 가장자리의 탭이 반대편을 읽는다.
         */
        inline constexpr uint32 kEngineTextureSampler = SW_ENGINE_TEXTURE_SAMPLER;
        inline constexpr uint32 kInstanceBuffer       = SW_SLOT_INSTANCE_SRV;
        /**
         * @brief 네이티브 bindless 가 없는 백엔드(DX11, GL)의 머티리얼 텍스처 슬롯 t16..t31(16 칸)입니다. 머티리얼 하나가 받는 텍스처 수의 상한이기도 합니다(네 백엔드 공통).
         * @details 그 두 백엔드는 머티리얼이 준 전역 인덱스를 셰이더에서 해석할 수 없다. DX11 은 SM5.0 이라
         *          리소스 배열 동적 인덱싱이 없고(그건 SM5.1, 곧 D3D12), GL 은 SPIR-V 로 먹이므로
         *          ARB_bindless_texture 를 쓸 수 없다. 그래서 엔진이 머티리얼 텍스처를 이 고정 슬롯에
         *          바인딩하고 머티리얼 데이터에는 서수를 넣는다. 16 은 언리얼 머티리얼 샘플러 한도와 같다.
         *          슬롯은 Vulkan t 밴드(`vk::kRangeSize`) 전체의 **뒤**에 있다 — DX12 테이블과 Vulkan t 밴드는 이 슬롯을 모르고(선언도 에뮬 백엔드뿐이다),
         *          SRV 띠(`kSRVSlotCount`)가 밴드 끝까지 자라도 이 슬롯은 움직이지 않는다.
         */
        inline constexpr uint32 kMaterialTexture0     = SW_SLOT_MATERIAL_TEX0;
        inline constexpr uint32 kMaterialTextureCount = SW_MATERIAL_TEXTURE_SLOT_COUNT;
        /** @brief 캔버스(UI) 배치 하나가 쓰는 텍스처 수입니다. 머티리얼 텍스처 슬롯의 앞 칸을 빌려 쓰고, 루트 상수 예산 때문에 머티리얼 칸 수와 따로 둡니다. */
        inline constexpr uint32 kCanvasTextureCount = SW_CANVAS_TEXTURE_COUNT;
        /**
         * @brief 머티리얼 텍스처를 읽는 샘플러(bindingslots.hlsli 4 의 SW_SAMPLER_*)입니다. 선형 필터와 랩 주소를 씁니다.
         * @details 네이티브 bindless(DX12, Vulkan)는 `MaterialTexture::sampleIndex` 가 이 샘플러로 읽는다. 슬롯 결합 샘플러를 쓰는
         *          GL 은 셰이더가 고르지 못하므로 엔진이 머티리얼 유닛 16..31 에 같은 샘플러를 바인딩하고, DX11 은 셰이더가 정적 세트의 이 번호로 읽는다
         *          (샘플러 슬롯이 16 개뿐이라 머티리얼 슬롯마다 짝을 둘 수 없다). 언리얼 머티리얼 텍스처의 기본 Wrap 샘플러다.
         */
        inline constexpr uint32 kMaterialTextureSampler = SW_MATERIAL_TEXTURE_SAMPLER;
        /// @brief GPUScene 머티리얼 데이터 구조 버퍼(g_SwMaterials)입니다. 인스턴스의 materialIndex 로 읽습니다.
        inline constexpr uint32 kMaterialBuffer = SW_SLOT_MATERIAL_BUFFER;
        /**
         * @brief GPU 컬링이 만든 가시 인스턴스 ID 목록(g_SwVisibleInstanceIDs)입니다.
         * @details 컬링 컴퓨트가 살아남은 인스턴스 번호를 배치 구간에 압축해 넣고, 정점 셰이더가 이 순서로
         *          읽는다. 언리얼 FInstanceCullingContext 의 InstanceIDBuffer 에 해당한다.
         */
        inline constexpr uint32 kVisibleInstanceBuffer = SW_SLOT_VISIBLE_INSTANCE_SRV;
        inline constexpr uint32 kMorphVertexBuffer     = SW_SLOT_MORPH_VERTEX_SRV;
        /**
         * @brief 씬의 라이트 목록(g_SwLights)입니다. 방향광과 점광이 한 버퍼에 섞여 있습니다.
         * @details 포워드와 디퍼드가 **같은 버퍼를 같은 루프로** 읽는다. 상수 버퍼가 아닌 이유는
         *          인스턴스와 머티리얼 때와 같다. 개수가 씬마다 다르고, 패스당 한 번 바인딩하면 드로우 사이에
         *          바인딩이 바뀌지 않는다.
         */
        inline constexpr uint32 kLightBuffer = SW_SLOT_LIGHT_SRV;
        /**
         * @brief 씬 배치 테이블(g_SwBatches)입니다. 배치마다 인스턴스 시작, 모프 풀 시작, 정점 풀 시작을 담습니다.
         * @details 정점 셰이더가 자기 배치 번호로 읽는다. 배치마다 바뀌는 값이 모두 여기 있어 루트 상수를 배치마다
         *          다시 바인딩할 필요가 없고, 그래서 같은 PSO 의 배치들이 멀티 드로우 하나로 나간다. 컬링 컴퓨트의 t1 과 같은 버퍼다.
         */
        inline constexpr uint32 kBatchBuffer = SW_SLOT_BATCH_SRV;
        /**
         * @brief 정점 애니메이션(VAT) 테이블(g_SwVertexAnimation)입니다. 먼 군중의 메시마다 첫 원소와 프레임 × 정점 데이터를 담습니다.
         * @details 정점 셰이더가 배치 테이블의 VAT 시작과 인스턴스의 시간 오프셋으로 읽는다(binding.hlsli `InstanceData::loadAnimatedVertex`).
         */
        inline constexpr uint32 kVertexAnimationBuffer = SW_SLOT_VERTEX_ANIMATION_SRV;
        /**
         * @brief 패스 전용 구조 버퍼 슬롯입니다. 캔버스 패스는 사각형(g_SwCanvasQuads)을, 투명 패스의 입자 그리기는 입자(g_SwParticles)를 겁니다.
         * @details 정점 셰이더가 원소 하나를 인스턴스 하나로 읽고 `SV_VertexID` 로 모서리를 만든다(동적 정점 버퍼는 없다). 두 그리기는 같은 패스에서 겹치지 않으므로
         *          슬롯을 나눠 쓴다.
         */
        inline constexpr uint32 kPassBuffer = SW_SLOT_PASS_SRV;
        /**
         * @brief 지금 뷰의 라이트 그리드(g_SwLightGrid, `uint` 단어 배열)입니다. 뷰마다의 그리드가 이어 붙어 있고 PassCB `g_SwLightGridBase` 가 첫 단어입니다.
         * @details CPU 생산자(`LightGrid`)는 동적 버퍼로, GPU 생산자(lightgrid.hlsl)는 기본 힙 UAV 버퍼로 같은 슬롯에 바인딩한다. 셰이더는 생산자를 모른다.
         */
        inline constexpr uint32 kLightGridBuffer = SW_SLOT_LIGHT_GRID_SRV;
        /**
         * @brief 라이트 프로브의 SH9(g_SwLightProbes, float4 배열)입니다. 프로브 하나가 `kLightProbeFloat4Count` 원소입니다.
         * @details 볼륨은 라이트 목록의 `kLightTypeProbeVolume` 원소이고 그 원소가 이 버퍼의 첫 프로브 번호를 싣는다(bindingslots.hlsli 8절).
         */
        inline constexpr uint32 kLightProbeBuffer      = SW_SLOT_LIGHT_PROBE_SRV;
        inline constexpr uint32 kLightProbeFloat4Count = SW_LIGHT_PROBE_FLOAT4_COUNT;
        inline constexpr uint32 kSRVSlotCount          = SW_SRV_SLOT_COUNT;
        /**
         * @brief G버퍼 머티리얼 칸(금속도 · 거칠기)의 엔진 텍스처 슬롯 t15 입니다(에뮬 백엔드 DX11 · GL 만). 디퍼드 조명이 t0..t3 의 넷과 함께 다섯째로 읽습니다.
         * @details t0..t3 과 떨어져 있어 `kEngineTextureCount` 에 들지 않는다. DX11 은 s15 가 정적 샘플러 세트라 이 슬롯에 샘플러를 걸지 않고(셰이더가 정적 세트로 읽는다),
         *          GL 은 유닛 15 에 엔진 텍스처 샘플러를 건다. SRV 띠가 이 칸까지 자라면 아래 static_assert 가 막는다.
         */
        inline constexpr uint32 kEngineGBufferMaterialTexture = SW_SLOT_ENGINE_GBUFFER_MATERIAL;

        // ------------------------------------------------------------------------------
        // 2-1) 라이트 원소의 타입 값. bindingslots.hlsli 가 기준이다(셰이더와 같은 파일).
        // ------------------------------------------------------------------------------
        inline constexpr uint32 kLightTypeDirectional = SW_LIGHT_TYPE_DIRECTIONAL;
        inline constexpr uint32 kLightTypePoint       = SW_LIGHT_TYPE_POINT;
        inline constexpr uint32 kLightTypeSpot        = SW_LIGHT_TYPE_SPOT;
        inline constexpr uint32 kLightTypePoint2D     = SW_LIGHT_TYPE_POINT2D;
        inline constexpr uint32 kLightTypeGlobal2D    = SW_LIGHT_TYPE_GLOBAL2D;
        inline constexpr uint32 kLightTypeFreeform2D  = SW_LIGHT_TYPE_FREEFORM2D;
        inline constexpr uint32 kLightTypeCount       = SW_LIGHT_TYPE_COUNT;
        /// @brief 자유 모양 2D 빛의 변 원소 종류입니다. 빛 종류가 아니라서 `kLightTypeCount` 밖입니다.
        inline constexpr uint32 kLightTypeFreeform2DEdge = SW_LIGHT_TYPE_FREEFORM2D_EDGE;
        /// @brief 자유 모양 2D 빛 하나의 변 수 상한입니다(셰이더 루프의 끝).
        inline constexpr uint32 kLight2DMaxFreeformEdges = SW_LIGHT2D_MAX_FREEFORM_EDGES;
        /// @brief 2D 빛의 섞는 방식 값입니다(GPU `extra.y`).
        inline constexpr uint32 kLight2DBlendAdditive    = SW_LIGHT2D_BLEND_ADDITIVE;
        inline constexpr uint32 kLight2DBlendMultiply    = SW_LIGHT2D_BLEND_MULTIPLY;
        inline constexpr uint32 kLight2DBlendSubtractive = SW_LIGHT2D_BLEND_SUBTRACTIVE;
        /// @brief 2D 빛 쿠키 아틀라스의 한 변(텍셀)과 한 줄의 칸 수, 칸 수 전체, 쿠키 없음 값(빛 원소 `_extra.w`)입니다.
        inline constexpr uint32 kLight2DCookieAtlasSize   = SW_LIGHT2D_COOKIE_ATLAS_SIZE;
        inline constexpr uint32 kLight2DCookieCellsPerRow = SW_LIGHT2D_COOKIE_CELLS_PER_ROW;
        inline constexpr uint32 kLight2DCookieCellCount   = SW_LIGHT2D_COOKIE_CELLS_PER_ROW * SW_LIGHT2D_COOKIE_CELLS_PER_ROW;
        inline constexpr int32  kLight2DNoCookieCell      = SW_LIGHT2D_NO_COOKIE_CELL;

        // 패스 플래그: PassCB `g_Flags` 의 비트(bindingslots.hlsli 9).
        inline constexpr uint32 kPassFlagNativeBindless = SW_PASS_FLAG_NATIVE_BINDLESS;
        inline constexpr uint32 kPassFlagSkipPost       = SW_PASS_FLAG_SKIP_POST;
        inline constexpr uint32 kPassFlagSkipTonemap    = SW_PASS_FLAG_SKIP_TONEMAP;
        inline constexpr uint32 kPassFlagPointUpscale   = SW_PASS_FLAG_POINT_UPSCALE;
        /// @brief 2D 그림자 가림막 토막 원소의 종류입니다. 빛 종류가 아니라서(레지스트리 슬롯이 없다) `kLightTypeCount` 밖에 있습니다.
        inline constexpr uint32 kLightTypeShadow2D = SW_LIGHT_TYPE_SHADOW2D;
        /// @brief 라이트 프로브 볼륨 원소의 종류입니다. 빛 종류가 아니라서 `kLightTypeCount` 밖이고, 라이트 그리드는 경계 구로 클러스터에 넣습니다.
        inline constexpr uint32 kLightTypeProbeVolume = SW_LIGHT_TYPE_PROBE_VOLUME;
        /// @brief 반사 프로브 원소의 종류입니다. 빛 종류가 아니라서 `kLightTypeCount` 밖이고, 텍셀은 라이트 프로브 버퍼(t13)에 SH9 원소와 함께 붙습니다.
        inline constexpr uint32 kLightTypeReflectionProbe = SW_LIGHT_TYPE_REFLECTION_PROBE;
        /// @brief 라이트 그리드가 경계 구로 클러스터에 넣는 타입의 비트(1 << 타입)입니다. CPU · GPU 생산자가 이 하나를 봅니다.
        inline constexpr uint32 kLightGridLocalTypeMask = SW_LIGHT_GRID_LOCAL_TYPE_MASK;
        static_assert( kLightGridLocalTypeMask == ( ( 1u << SW_LIGHT_TYPE_POINT ) | ( 1u << SW_LIGHT_TYPE_SPOT ) | ( 1u << SW_LIGHT_TYPE_REFLECTION_PROBE ) |
                                                    ( 1u << SW_LIGHT_TYPE_PROBE_VOLUME ) ),
                       "라이트 그리드의 지역 타입 비트가 타입 값과 맞지 않는다 (bindingslots.hlsli SW_LIGHT_GRID_LOCAL_TYPE_MASK)" );
        /// @brief 한 프레임에 GPU 로 보내는 라이트 수의 상한입니다. 넘으면 엔진이 잘라서 보내고 경고합니다.
        inline constexpr uint32 kMaxFrameLight = SW_MAX_FRAME_LIGHT;

        // ------------------------------------------------------------------------------
        // 2-2) 버퍼 배치와 식 상수. bindingslots.hlsli 10절이 기준이다(셰이더와 같은 파일).
        // ------------------------------------------------------------------------------
        /// @brief 모프 풀 정점 하나의 원소(float4) 수입니다.
        inline constexpr uint32 kMorphFloat4PerVertex = SW_MORPH_FLOAT4_PER_VERTEX;
        /// @brief 스킨 원본 정점 하나의 가중치 버퍼 원소(float4) 수입니다.
        inline constexpr uint32 kSkinFloat4PerVertex = SW_SKIN_FLOAT4_PER_VERTEX;
        /// @brief 스킨 팔레트 본 하나의 원소(float4) 수입니다.
        inline constexpr uint32 kSkinFloat4PerBone = SW_SKIN_FLOAT4_PER_BONE;
        /// @brief 스킨 인스턴스 표 한 줄의 원소(uint4) 수입니다.
        inline constexpr uint32 kSkinUint4PerInstance = SW_SKIN_UINT4_PER_INSTANCE;
        /// @brief VAT 표에서 메시 하나의 머리 단어(uint32) 수입니다.
        inline constexpr uint32 kVertexAnimationHeaderWords = SW_VERTEX_ANIMATION_HEADER_WORDS;
        /// @brief VAT 표에서 정점 하나의 한 프레임이 차지하는 단어(uint32) 수입니다(8 B).
        inline constexpr uint32 kVertexAnimationWordsPerElement = SW_VERTEX_ANIMATION_WORDS_PER_ELEMENT;
        /// @brief 거스트너 파도의 항 수입니다(진폭이 0인 항은 비어 있는 항입니다).
        inline constexpr uint32 kGerstnerWaveCount = SW_GERSTNER_WAVE_COUNT;
        /// @brief 방향광 캐스케이드의 상한입니다(PassCB `g_ShadowCascade0..3`). 그림자 아틀라스의 네 사분면에 하나씩 듭니다.
        inline constexpr uint32 kShadowCascadeMax = SW_SHADOW_CASCADE_MAX;
        /// @brief 점광 · 스폿 그림자 타일 하나의 라이트 버퍼 원소 수입니다([0] 뷰-투영 행 넷, [1] 칸 사각형 · 바이어스).
        inline constexpr uint32 kShadowTileElementCount = SW_SHADOW_TILE_ELEMENT_COUNT;
        /// @brief 점광 그림자의 면 수입니다(+X −X +Y −Y +Z −Z 순 — 셰이더의 면 고르기와 같은 순서).
        inline constexpr uint32 kPointShadowFaceCount = SW_POINT_SHADOW_FACE_COUNT;

        /**
         * @brief 라이트 그리드(클러스터드 라이팅)의 단어 배치입니다. 원본은 bindingslots.hlsli 10절이고, C++ `LightGrid`, 셰이더 `LightCursor::begin`, 컴퓨트 lightgrid.hlsl 이 같은 값을 읽습니다.
         * @details 그리드는 32 비트 단어 배열(g_SwLightGrid)입니다. 헤더, 클러스터마다 단어 두 개, 라이트 번호 목록 순서로 놓입니다.
         */
        namespace lightgrid
        {
            inline constexpr uint32 kHeaderWordCount = SW_LIGHT_GRID_HEADER_WORD_COUNT;
            inline constexpr uint32 kWordsPerCluster = SW_LIGHT_GRID_WORDS_PER_CLUSTER;
            inline constexpr uint32 kDimX            = SW_LIGHT_GRID_DIM_X;
            inline constexpr uint32 kDimY            = SW_LIGHT_GRID_DIM_Y;
            inline constexpr uint32 kDimZ            = SW_LIGHT_GRID_DIM_Z;
            inline constexpr uint32 kClusterCount    = kDimX * kDimY * kDimZ;
            /// @brief 라이트 번호 목록이 시작하는 단어(그리드 첫 단어 기준)입니다. 헤더와 클러스터 단어 뒤입니다.
            inline constexpr uint32 kIndexBase = kHeaderWordCount + kClusterCount * kWordsPerCluster;
            /// @brief GPU 생산자가 뷰 하나에 담는 목록 항목의 상한과 그 뷰가 차지하는 단어 수입니다.
            inline constexpr uint32 kGPUMaxEntryCount     = SW_LIGHT_GRID_GPU_MAX_ENTRY_COUNT;
            inline constexpr uint32 kGPUWordsPerView      = kIndexBase + kGPUMaxEntryCount;
            inline constexpr uint32 kGPUThreadCount       = SW_LIGHT_GRID_GPU_THREAD_COUNT;
            inline constexpr uint32 kGPUViewFloat4Count   = SW_LIGHT_GRID_VIEW_FLOAT4_COUNT;
            inline constexpr uint32 kWordDimX             = SW_LIGHT_GRID_WORD_DIM_X;
            inline constexpr uint32 kWordDimY             = SW_LIGHT_GRID_WORD_DIM_Y;
            inline constexpr uint32 kWordDimZ             = SW_LIGHT_GRID_WORD_DIM_Z;
            inline constexpr uint32 kWordPerspective      = SW_LIGHT_GRID_WORD_PERSPECTIVE;
            inline constexpr uint32 kWordNearDepth        = SW_LIGHT_GRID_WORD_NEAR_DEPTH;
            inline constexpr uint32 kWordSliceScale       = SW_LIGHT_GRID_WORD_SLICE_SCALE;
            inline constexpr uint32 kWordGlobalCount      = SW_LIGHT_GRID_WORD_GLOBAL_COUNT;
            inline constexpr uint32 kWordClusterBase      = SW_LIGHT_GRID_WORD_CLUSTER_BASE;
            inline constexpr uint32 kWordIndexBase        = SW_LIGHT_GRID_WORD_INDEX_BASE;
            inline constexpr uint32 kWordEntryCount       = SW_LIGHT_GRID_WORD_ENTRY_COUNT;
            inline constexpr uint32 kGroupSharedByteLimit = 32768; ///< DX11 cs_5_0 · DX12 · Vulkan 최소 보장 · GL 최소 보장의 그룹 공유 메모리
            /// @brief 라이트 범위의 여유(NDC, 깊이 조각)와 카메라 면 판정 클립 w 입니다. CPU 생산자 · GPU 생산자가 같은 값을 씁니다.
            inline constexpr float32 kNdcPad         = SW_LIGHT_GRID_NDC_PAD;
            inline constexpr float32 kSlicePad       = SW_LIGHT_GRID_SLICE_PAD;
            inline constexpr float32 kMinCornerClipW = SW_LIGHT_GRID_MIN_CORNER_CLIP_W;
            static_assert( kWordEntryCount < kHeaderWordCount, "헤더 단어 번호가 헤더 안에 있어야 한다" );
            static_assert( kClusterCount * sizeof( uint32 ) + kGPUThreadCount * sizeof( uint32 ) * 2 < kGroupSharedByteLimit,
                           "컴퓨트 생산자가 클러스터마다의 개수와 스레드마다의 합을 그룹 공유 메모리에 둔다 — 칸 수를 줄이거나 생산자를 나눈다" );
            static_assert( kGPUThreadCount * 4 >= SW_MAX_FRAME_LIGHT, "컴퓨트 생산자의 스레드는 라이트를 넷 이하씩 돈다" );
        } // namespace lightgrid

        // ------------------------------------------------------------------------------
        // 3) 컴퓨트와 샘플러
        // ------------------------------------------------------------------------------
        inline constexpr uint32 kComputeSRVSlotCount = SW_COMPUTE_SRV_SLOT_COUNT;
        inline constexpr uint32 kComputeUAVSlotCount = SW_COMPUTE_UAV_SLOT_COUNT;
        /// @brief 컴퓨트 셰이더가 제 쓰기 버퍼로 쓰는 슬롯 수입니다(u0..u3).
        inline constexpr uint32 kComputeShaderUAVSlotCount = SW_COMPUTE_SHADER_UAV_SLOT_COUNT;
        /// @brief 셰이더 assert 링의 쓰기 슬롯(u4)입니다 — 셰이더 소유 슬롯 바로 뒤, RW 텍스처 슬롯 바로 앞이라 어느 컴퓨트와도 겹치지 않습니다.
        inline constexpr uint32 kComputeAssertUAV = SW_SLOT_COMPUTE_ASSERT_UAV;
        /// @brief 컴퓨트 RW 텍스처 슬롯 u5..u7 입니다. DX11 과 GL 만 실제 슬롯이고, DX12 와 Vulkan 은 배열 인덱스라 bindComputeUAV 가 무시합니다.
        inline constexpr uint32 kComputeTextureUAV0         = SW_SLOT_COMPUTE_TEXUAV0;
        inline constexpr uint32 kComputeTextureUAVSlotCount = SW_COMPUTE_TEXUAV_SLOT_COUNT;
        inline constexpr uint32 kStaticSamplerCount         = SW_STATIC_SAMPLER_COUNT;
        inline constexpr uint32 kStaticSamplerArrayCount    = SW_STATIC_SAMPLER_ARRAY_COUNT;
        inline constexpr uint32 kSamplerShadowCmp           = SW_SAMPLER_SHADOW_CMP;

        /// @brief 네이티브 bindless 텍스처 배열(DX12, Vulkan)의 위치입니다. 슬롯 리소스와 분리되어 있습니다.
        namespace bindless
        {
            inline constexpr uint32 kTextureSpace           = SW_SPACE_BINDLESS_TEX;        ///< DX12: t0 space1
            inline constexpr uint32 kVkTextureSet           = SW_VK_TEXTURE_SET;            ///< Vulkan set 1
            inline constexpr uint32 kVkTextureBinding       = SW_VK_TEXTURE_BINDING;        ///< set 1 binding 0은 텍스처 배열이다.
            inline constexpr uint32 kVkSamplerBinding       = SW_VK_SAMPLER_BINDING;        ///< set 1 binding 1은 immutable sampler 배열이다.
            inline constexpr uint32 kVkShadowSamplerBinding = SW_VK_SHADOW_SAMPLER_BINDING; ///< set 1 binding 2는 비교 샘플러다.
            inline constexpr uint32 kVkRwTextureBinding     = SW_VK_RWTEXTURE_BINDING;      ///< set 1 binding 3은 STORAGE_IMAGE 배열이다.
            /// @brief 배열 칸 수입니다. 셰이더가 첨자를 (칸 수 - 1) 로 마스크하므로(binding.hlsli Bindless::textureSlot) 2 의 거듭제곱이고, 백엔드는 이 크기로 만든다.
            inline constexpr uint32 kDx12DescriptorHeapSize = SW_DX12_DESCRIPTOR_HEAP_SIZE;       ///< DX12 셰이더 가시 힙(텍스처 · RW 텍스처 공용)
            inline constexpr uint32 kVkTextureCount         = SW_VK_BINDLESS_TEXTURE_COUNT;       ///< Vulkan set 1 binding 0
            inline constexpr uint32 kVkStorageImageCount    = SW_VK_BINDLESS_STORAGE_IMAGE_COUNT; ///< Vulkan set 1 binding 3
            static_assert( ( kDx12DescriptorHeapSize & ( kDx12DescriptorHeapSize - 1 ) ) == 0 && ( kVkTextureCount & ( kVkTextureCount - 1 ) ) == 0 &&
                               ( kVkStorageImageCount & ( kVkStorageImageCount - 1 ) ) == 0,
                           "bindless 배열 칸 수는 2 의 거듭제곱이어야 한다 (셰이더가 첨자를 마스크한다)" );
        } // namespace bindless

        /// @brief Vulkan 슬롯 세트(set 0)입니다. binding 은 종류별 시프트에 레지스터 번호를 더한 값입니다(DXC -fvk-*-shift 와 같은 값).
        namespace vk
        {
            inline constexpr uint32 kBShift           = SW_VK_B_SHIFT;
            inline constexpr uint32 kTShift           = SW_VK_T_SHIFT;
            inline constexpr uint32 kUShift           = SW_VK_U_SHIFT;
            inline constexpr uint32 kRangeSize        = SW_VK_SLOT_RANGE_SIZE;
            inline constexpr uint32 kSlotBindingCount = SW_VK_SLOT_BINDING_COUNT;
        } // namespace vk

        /**
         * @brief DX11 정적 샘플러 세트 s9..s15 입니다. 슬롯 결합 샘플러 뒤에 놓입니다. 디바이스가 초기화할 때 바인딩하고 셰이더가 samplerID 로 고릅니다.
         * @details DX11 이 SW_SAMPLER_* 를 존중하는 유일한 방법이다(SM5.0 은 샘플러 배열 동적 인덱싱이 없어 리터럴 분기를 쓴다). GL 은 없다.
         */
        namespace dx11
        {
            inline constexpr uint32 kStaticSampler0      = SW_DX11_STATIC_SAMPLER0;
            inline constexpr uint32 kMaxSamplerSlotCount = SW_DX11_MAX_SAMPLER_SLOT_COUNT;
            static_assert( SW_DX11_STATIC_SAMPLER1 == SW_DX11_STATIC_SAMPLER0 + 1 && SW_DX11_STATIC_SAMPLER2 == SW_DX11_STATIC_SAMPLER0 + 2 &&
                               SW_DX11_STATIC_SAMPLER3 == SW_DX11_STATIC_SAMPLER0 + 3 && SW_DX11_STATIC_SAMPLER4 == SW_DX11_STATIC_SAMPLER0 + 4 &&
                               SW_DX11_STATIC_SAMPLER5 == SW_DX11_STATIC_SAMPLER0 + 5 && SW_DX11_STATIC_SAMPLER6 == SW_DX11_STATIC_SAMPLER0 + 6,
                           "DX11 정적 샘플러 슬롯은 연속이어야 한다 (PSSetSamplers 한 번에 건다)" );
            static_assert( kStaticSampler0 >= kEngineTexture0 + kEngineTextureCount, "DX11 정적 샘플러가 엔진 슬롯 결합 샘플러(s0..s3)와 겹친다" );
            static_assert( kStaticSampler0 + kStaticSamplerArrayCount <= kMaxSamplerSlotCount, "DX11 정적 샘플러가 샘플러 슬롯 수(16)를 넘는다" );
        } // namespace dx11

        /**
         * @brief DX12 루트 시그니처 예산입니다. 언리얼 FD3D12RootSignature 와 같은 배치로, CB 는 루트 CBV 이고 t/u 슬롯은 디스크립터 테이블입니다.
         * @details 루트 시그니처는 64 dword 다. 루트 디스크립터는 2, 테이블은 1, 루트 상수는 dword 수만큼 든다.
         *          CB 셋(3×2) + 테이블 셋(3×1) + 루트 상수(16) = 25 다. t/u 슬롯 수는 테이블 안에서 늘어나므로 예산에 들지 않는다
         *          (t/u 를 루트 디스크립터로 두면 슬롯 하나가 2 dword 라 슬롯을 늘릴 여지가 없다).
         */
        namespace dx12
        {
            inline constexpr uint32 kRootBudgetDwords     = 64;
            inline constexpr uint32 kRootDescriptorDwords = 2;
            inline constexpr uint32 kRootTableDwords      = 1;
            inline constexpr uint32 kRootCBVCount         = kConstantBufferSlotCount; ///< b0..b(N-1) 루트 CBV
            inline constexpr uint32 kRootTableCount       = 3;                        ///< t 슬롯 테이블, u 슬롯 테이블, 텍스처 배열 테이블
            inline constexpr uint32 kRootSignatureDwords  = kRootCBVCount * kRootDescriptorDwords + kRootTableCount * kRootTableDwords + kRootConstantDwords;
            static_assert( kRootSignatureDwords <= kRootBudgetDwords, "DX12 루트 시그니처가 64 dword 예산을 넘는다" );
        } // namespace dx12

        /// @brief OpenGL SSBO 번호입니다. u# 는 t# 와 겹치지 않게 SW_GL_UAV_BINDING0 부터 셉니다(DXC -fvk-u-shift 값이기도 합니다).
        namespace gl
        {
            inline constexpr uint32 kUAVBinding0 = SW_GL_UAV_BINDING0;
            inline constexpr uint32 kImageUnit0  = SW_GL_IMAGE_UNIT0; ///< 컴퓨트 RW 텍스처 u5..u7이 이미지 유닛 0..2 에 대응한다.
        } // namespace gl

        /// @brief 엔진 예약 CB 이름입니다(리플렉션 매칭 키).
        namespace cbname
        {
            inline constexpr const utf8* kPass      = "PassCB";
            inline constexpr const utf8* kMaterial  = "MaterialCB";
            inline constexpr const utf8* kCull      = "CullParams";
            inline constexpr const utf8* kAnimation = "AnimationParams"; ///< instanceanimation.hlsl 의 컴퓨트 CB(b0)
            inline constexpr const utf8* kSort      = "SortParams";      ///< instancesort.hlsl 의 컴퓨트 CB(b0)
            /// @brief SW_ROOT_CONSTANTS_BEGIN/END 가 선언하는 루트/푸시 상수 블록입니다(DX12 b0 space2, Vulkan 푸시 상수, DX11 과 GL 의 b2).
            inline constexpr const utf8* kRootConstants = "SwRootConstants";
        } // namespace cbname

        /// @brief 엔진 예약 리소스 이름입니다. binding.hlsli 와 gpucull.hlsl 의 선언과 같아야 합니다(계약 검증 키).
        namespace resname
        {
            inline constexpr const utf8* kInstances                    = "g_SwInstances";
            inline constexpr const utf8* kMaterials                    = "g_SwMaterials";
            inline constexpr const utf8* kEngineTexture                = "g_SwSlot";                ///< 뒤에 0..3 이 붙는다.
            inline constexpr const utf8* kMaterialTexture              = "g_SwMaterialTex";         ///< 뒤에 0..15 가 붙는다.
            inline constexpr const utf8* kEngineGBufferMaterialTexture = "g_SwSlotGBufferMaterial"; ///< t15(에뮬). G버퍼 머티리얼 칸
            inline constexpr const utf8* kBindlessTextures             = "g_SwBindlessTex2D";
            inline constexpr const utf8* kBindlessRwTextures           = "g_SwBindlessRWTex2D";
            inline constexpr const utf8* kSamplers                     = "g_SwSamplers";           ///< Vulkan: set 1 binding 1 immutable sampler 배열(s0..s6)
            inline constexpr const utf8* kSamplerSlot                  = "g_SwSampler";            ///< 뒤에 0..6 이 붙는다. DX12 정적 샘플러 s#(배열은 정적 샘플러로 채울 수 없다).
            inline constexpr const utf8* kShadowSampler                = "g_SwSamplerShadowCmp";   ///< s7 비교 샘플러
            inline constexpr const utf8* kGPUAssertRing                = "g_GPUAssertRW";          ///< 셰이더 assert 링(컴퓨트 쓰기 슬롯 u4, gpuassert.hlsli).
            inline constexpr const utf8* kRwTextureSlot                = "g_SwRWSlot";             ///< 뒤에 0..2 가 붙는다(DX11/GL 컴퓨트 RW 텍스처 슬롯).
            inline constexpr const utf8* kVisibleInstances             = "g_SwVisibleInstanceIDs"; ///< t6(그래픽스). 컬링이 만든 가시 목록
            inline constexpr const utf8* kMorphVertices                = "g_SwMorphVertices";      ///< t7(그래픽스). GPU 가 변형한 정점 풀
            inline constexpr const utf8* kLights                       = "g_SwLights";             ///< t8(그래픽스). 씬 라이트 목록
            inline constexpr const utf8* kBatches                      = "g_SwBatches";            ///< t9(그래픽스). 씬 배치 테이블
            inline constexpr const utf8* kVertexAnimation              = "g_SwVertexAnimation";    ///< t10(그래픽스). 정점 애니메이션(VAT) 테이블
            inline constexpr const utf8* kCanvasQuads                  = "g_SwCanvasQuads";        ///< t11(캔버스 패스). 화면 2D 사각형
            inline constexpr const utf8* kParticles                    = "g_SwParticles";          ///< t11(투명 패스의 입자 그리기). 방출기 하나의 입자(particle.hlsl)
            inline constexpr const utf8* kLightGrid                    = "g_SwLightGrid";          ///< t12(그래픽스). 뷰마다의 라이트 그리드 단어
            inline constexpr const utf8* kLightProbes                  = "g_SwLightProbes";        ///< t13(그래픽스). 라이트 프로브의 SH9(프로브 하나가 float4 일곱 개)
            inline constexpr const utf8* kLightGridLights              = "g_LightGridLights";      ///< lightgrid 컴퓨트 t0. 씬 라이트 버퍼(g_SwLights 와 같은 버퍼)
            inline constexpr const utf8* kLightGridViews               = "g_LightGridViews";       ///< lightgrid 컴퓨트 t1. 뷰마다의 뷰-투영과 깊이 조각 상수
            inline constexpr const utf8* kLightGridRw                  = "g_LightGridRW";          ///< lightgrid 컴퓨트 u0. 뷰마다의 그리드를 쓴다
            inline constexpr const utf8* kMorphRestVertices            = "g_RestVertices";         ///< meshmorph t0. 레스트 포즈
            inline constexpr const utf8* kMorphDeformPrograms          = "g_DeformPrograms";       ///< meshmorph t1. 메시 칸 표 + 변형 프로그램(meshmodifier.hlsli)
            inline constexpr const utf8* kMorphVerticesRw              = "g_MorphVerticesRW";      ///< meshmorph 와 meshskin 의 u0. 변형 결과
            inline constexpr const utf8* kSkinWeights                  = "g_SkinWeights";          ///< meshskin t1. 스킨 정점의 가중치와 팔레트 행 번호
            inline constexpr const utf8* kSkinPalette                  = "g_SkinPalette";          ///< meshskin t2. 본 팔레트(본 하나가 float4 세 개)
            inline constexpr const utf8* kSkinInstances                = "g_SkinInstances";        ///< meshskin t3. 스킨 인스턴스 테이블(인스턴스 하나가 uint4 두 개)
            inline constexpr const utf8* kCullInstances                = "g_Instances";
            inline constexpr const utf8* kCullBatchInfo                = "g_BatchInfo"; ///< 컬링 t1. 배치의 인스턴스 구간
            inline constexpr const utf8* kCullIndirectArgs             = "g_IndirectArgs";
            inline constexpr const utf8* kCullVisibleIDs               = "g_VisibleInstanceIDs"; ///< 컬링 u1. 압축해서 쓰는 쪽
            inline constexpr const utf8* kCullLODState                 = "g_LODState";           ///< 컬링 u2. 인스턴스마다의 LOD 전환 상태(크로스페이드)
            inline constexpr const utf8* kAnimationInstancesRw         = "g_InstancesRW";        ///< instanceanimation u0. 월드 행렬을 고쳐 쓴다.
        } // namespace resname

        // 계약의 내부 일관성을 검사한다. 값을 바꾸면 여기서 먼저 걸린다.
        static_assert( kInstanceBuffer == kEngineTexture0 + kEngineTextureCount, "인스턴스 버퍼는 엔진 텍스처 슬롯 바로 다음이어야 한다" );
        static_assert( kMaterialBuffer == kInstanceBuffer + 1, "머티리얼 데이터 버퍼는 인스턴스 버퍼 바로 다음이어야 한다" );
        static_assert( kMaterialTexture0 == vk::kRangeSize && kSRVSlotCount <= kMaterialTexture0,
                       "머티리얼 텍스처 슬롯(에뮬)은 Vulkan t 밴드 바로 뒤에 둔다 — DX12 테이블 · Vulkan t 밴드 밖이고 SRV 띠가 자라도 겹치지 않는다" );
        static_assert( SW_SLOT_MATERIAL_TEX1 == SW_SLOT_MATERIAL_TEX0 + 1 && SW_SLOT_MATERIAL_TEX2 == SW_SLOT_MATERIAL_TEX0 + 2 &&
                           SW_SLOT_MATERIAL_TEX3 == SW_SLOT_MATERIAL_TEX0 + 3 && SW_SLOT_MATERIAL_TEX4 == SW_SLOT_MATERIAL_TEX0 + 4 &&
                           SW_SLOT_MATERIAL_TEX5 == SW_SLOT_MATERIAL_TEX0 + 5 && SW_SLOT_MATERIAL_TEX6 == SW_SLOT_MATERIAL_TEX0 + 6 &&
                           SW_SLOT_MATERIAL_TEX7 == SW_SLOT_MATERIAL_TEX0 + 7 && SW_SLOT_MATERIAL_TEX8 == SW_SLOT_MATERIAL_TEX0 + 8 &&
                           SW_SLOT_MATERIAL_TEX9 == SW_SLOT_MATERIAL_TEX0 + 9 && SW_SLOT_MATERIAL_TEX10 == SW_SLOT_MATERIAL_TEX0 + 10 &&
                           SW_SLOT_MATERIAL_TEX11 == SW_SLOT_MATERIAL_TEX0 + 11 && SW_SLOT_MATERIAL_TEX12 == SW_SLOT_MATERIAL_TEX0 + 12 &&
                           SW_SLOT_MATERIAL_TEX13 == SW_SLOT_MATERIAL_TEX0 + 13 && SW_SLOT_MATERIAL_TEX14 == SW_SLOT_MATERIAL_TEX0 + 14 &&
                           SW_SLOT_MATERIAL_TEX15 == SW_SLOT_MATERIAL_TEX0 + 15 && kMaterialTextureCount == 16,
                       "머티리얼 텍스처 슬롯은 연속 16 칸이어야 한다 (셰이더가 서수로 고른다 — binding.hlsli 의 선언 16 줄과 같다)" );
        static_assert( kCanvasTextureCount <= kMaterialTextureCount, "캔버스 텍스처는 머티리얼 텍스처 슬롯의 앞 칸을 빌려 쓴다" );
        static_assert( kVisibleInstanceBuffer == kMaterialBuffer + 1, "가시 인스턴스 ID 버퍼는 머티리얼 데이터 다음이어야 한다" );
        static_assert( kMorphVertexBuffer == kVisibleInstanceBuffer + 1, "모프 정점 버퍼는 가시 목록 다음이어야 한다" );
        static_assert( kLightBuffer == kMorphVertexBuffer + 1, "라이트 버퍼는 모프 정점 다음이다" );
        static_assert( kBatchBuffer == kLightBuffer + 1, "배치 표는 라이트 다음이다" );
        static_assert( kVertexAnimationBuffer == kBatchBuffer + 1, "VAT 표는 배치 표 다음이다" );
        static_assert( kPassBuffer == kVertexAnimationBuffer + 1, "패스 전용 버퍼는 VAT 표 다음이다" );
        static_assert( kLightGridBuffer == kPassBuffer + 1, "라이트 그리드는 패스 전용 버퍼 다음이다" );
        static_assert( kLightProbeBuffer == kLightGridBuffer + 1 && kLightProbeBuffer + 1 == kSRVSlotCount, "라이트 프로브는 라이트 그리드 다음이고 SRV 슬롯의 마지막이다" );
        static_assert( kLightTypeDirectional < kLightTypeCount && kLightTypePoint < kLightTypeCount &&
                           kLightTypeSpot < kLightTypeCount && kLightTypePoint2D < kLightTypeCount && kLightTypeGlobal2D < kLightTypeCount &&
                           kLightTypeFreeform2D < kLightTypeCount && kLightTypeShadow2D >= kLightTypeCount && kLightTypeProbeVolume >= kLightTypeCount &&
                           kLightTypeProbeVolume != kLightTypeShadow2D && kLightTypeReflectionProbe >= kLightTypeCount && kLightTypeReflectionProbe != kLightTypeShadow2D &&
                           kLightTypeReflectionProbe != kLightTypeProbeVolume && kLightTypeFreeform2DEdge >= kLightTypeCount &&
                           kLightTypeFreeform2DEdge != kLightTypeShadow2D && kLightTypeFreeform2DEdge != kLightTypeProbeVolume &&
                           kLightTypeFreeform2DEdge != kLightTypeReflectionProbe,
                       "라이트 타입 값이 타입 수 안에 있어야 한다" );
        /**
         * @brief 에뮬 백엔드(DX11 · GL)가 엔진 텍스처를 거는 슬롯 표입니다 — 이 표가 **유일한 원본**입니다.
         * @details 이름은 `g_<이름>Index` 의 `<이름>` 이다(역할 이름 `getRenderPassInputRoleName` 과 코드가 거는 쿠키 · 노출). 한 슬롯을 여러 이름이 나눠 쓴다.
         *          셰이더 컴파일러가 항목마다 `SW_EMU_SLOT_<이름>=<슬롯>` 매크로를 모든 컴파일에 넣고(`ShaderCompiler::compileHLSL`) `binding.hlsli` 의
         *          `Bindless::emulatedSlot` 이 그 매크로를 읽는다 — 손으로 맞춘 둘째 표가 없다. 매크로가 없으면 셰이더가 `#error` 로 멈춘다.
         *          C++ 의 역할 → 슬롯(`getEmulatedTextureSlot`)과 실행의 슬롯 바인딩도 이 표를 따른다.
         */
        namespace emulated
        {
            struct EngineTextureBinding
            {
                const utf8* _pName;
                uint32      _slot;
            };

            inline constexpr const utf8* kSlotMacroPrefix = "SW_EMU_SLOT_";

            inline constexpr EngineTextureBinding kEngineTextureBindings[] = {
                {         "ShadowMap",             SW_SLOT_ENGINE_TEX0},
                {       "SourceColor",             SW_SLOT_ENGINE_TEX0},
                {     "GBufferAlbedo",             SW_SLOT_ENGINE_TEX1},
                {  "AmbientOcclusion",             SW_SLOT_ENGINE_TEX1},
                {    "SceneColorCopy",             SW_SLOT_ENGINE_TEX1},
                {    "SceneDepthCopy",             SW_SLOT_ENGINE_TEX2},
                {"Light2DCookieAtlas",             SW_SLOT_ENGINE_TEX1},
                {     "GBufferNormal",             SW_SLOT_ENGINE_TEX2},
                {       "BloomBright",             SW_SLOT_ENGINE_TEX2},
                {      "AutoExposure",             SW_SLOT_ENGINE_TEX2},
                {        "SceneDepth",             SW_SLOT_ENGINE_TEX3},
                {   "GBufferMaterial", SW_SLOT_ENGINE_GBUFFER_MATERIAL},
            };

            /** @brief 이름의 슬롯을 돌려줍니다. 표에 없으면 false 입니다. */
            [[nodiscard]] constexpr bool tryFindSlot( const utf8* pName, uint32& outSlot )
            {
                for ( const EngineTextureBinding& binding : kEngineTextureBindings )
                {
                    const utf8* pLeft  = binding._pName;
                    const utf8* pRight = pName;
                    while ( *pLeft != '\0' && *pLeft == *pRight )
                    {
                        ++pLeft;
                        ++pRight;
                    }
                    if ( *pLeft == *pRight )
                    {
                        outSlot = binding._slot;
                        return true;
                    }
                }
                return false;
            }
        } // namespace emulated

        static_assert( SW_SLOT_ENGINE_TEX3 == kEngineTexture0 + kEngineTextureCount - 1, "엔진 텍스처 슬롯은 연속이어야 한다" );
        static_assert( kEngineGBufferMaterialTexture >= kSRVSlotCount && kEngineGBufferMaterialTexture < vk::kRangeSize && kEngineGBufferMaterialTexture < kMaterialTexture0,
                       "G버퍼 머티리얼 슬롯(에뮬)은 SRV 띠 위, Vulkan t 밴드 안, 머티리얼 슬롯 앞이어야 한다 — 띠가 자라 닿으면 DX11 · GL 에서 버퍼와 텍스처가 한 레지스터를 다툰다" );
        static_assert( kEngineGBufferMaterialTexture >= dx11::kStaticSampler0 && kEngineGBufferMaterialTexture < dx11::kStaticSampler0 + kStaticSamplerArrayCount,
                       "G버퍼 머티리얼 슬롯의 같은 번호 샘플러가 DX11 정적 세트가 아니면 짝 샘플러를 둘 수 있다 — binding.hlsli 의 DX11 선언 · D3D11 bindShaderResource 를 다시 본다" );
        static_assert( kPassConstantBuffer != kMaterialConstantBuffer && kMaterialConstantBuffer < kConstantBufferSlotCount &&
                           kComputeConstantBuffer < kConstantBufferSlotCount,
                       "예약 CB 슬롯은 백엔드가 마련한 b# 자리 안이어야 한다" );
        static_assert( kSRVSlotCount <= vk::kRangeSize && kComputeUAVSlotCount <= vk::kRangeSize && kConstantBufferSlotCount <= vk::kRangeSize,
                       "슬롯 수가 Vulkan 세트 0 의 종류별 범위 크기을 넘는다" );
        static_assert( vk::kTShift == vk::kBShift + vk::kRangeSize && vk::kUShift == vk::kTShift + vk::kRangeSize &&
                           vk::kSlotBindingCount == vk::kUShift + vk::kRangeSize,
                       "Vulkan 세트 0 범위(b/t/u)는 연속이어야 한다" );
        static_assert( kRootConstantSpace != 0 && kRootConstantSpace != bindless::kTextureSpace, "루트 상수 space 가 슬롯/텍스처 배열과 겹친다" );
        static_assert( kRootConstantEmulationSlot < kConstantBufferSlotCount && kRootConstantEmulationSlot != kPassConstantBuffer && kRootConstantEmulationSlot != kMaterialConstantBuffer,
                       "루트 상수 에뮬 슬롯은 예약 CB 와 겹치지 않는 b# 자리여야 한다" );
        static_assert( kComputeTextureUAV0 == kComputeUAVSlotCount && kComputeTextureUAV0 + kComputeTextureUAVSlotCount <= vk::kRangeSize,
                       "컴퓨트 RW 텍스처 슬롯은 버퍼 UAV 슬롯 바로 다음이어야 한다" );
        static_assert( SW_SLOT_COMPUTE_TEXUAV1 == SW_SLOT_COMPUTE_TEXUAV0 + 1 && SW_SLOT_COMPUTE_TEXUAV2 == SW_SLOT_COMPUTE_TEXUAV0 + 2 &&
                           SW_COMPUTE_TEXUAV_SLOT_COUNT == 3 && SW_GL_IMAGE_UNIT1 == SW_GL_IMAGE_UNIT0 + 1 && SW_GL_IMAGE_UNIT2 == SW_GL_IMAGE_UNIT0 + 2,
                       "RW 텍스처 슬롯/이미지 유닛은 연속이어야 한다 (셰이더가 서수로 고른다)" );
        // 컴퓨트 쓰기 슬롯 지도: 셰이더 소유 u0..u3 | assert 링 u4 | RW 텍스처 u5..u7. 셋이 겹치지 않고 빈틈이 없어야 한다 — assert 를 새 컴퓨트에 쓰려는 사람이 슬롯을 따로 잡을 필요가 없다는 근거다.
        static_assert( kComputeAssertUAV == kComputeShaderUAVSlotCount && kComputeUAVSlotCount == kComputeShaderUAVSlotCount + 1 && kComputeAssertUAV < kComputeUAVSlotCount,
                       "assert 링 슬롯은 셰이더 소유 쓰기 슬롯 바로 뒤이고 쓰기 버퍼 슬롯 수의 마지막이어야 한다" );
        /// @brief 컴퓨트 셰이더가 제 쓰기 버퍼로 쓰는 슬롯의 수(u0 부터 연속)를 셰이더별로 등록합니다. 하나라도 `kComputeShaderUAVSlotCount` 를 넘으면 assert 링(u4)과 겹치므로 컴파일이 막습니다.
        ///        값은 각 .hlsl 의 `SW_DECLARE_RW_STRUCTURED_BUFFER` / `SW_DECLARE_RW_BYTE_ADDRESS_BUFFER` 선언 수다(ShaderBindingValidator 가 쿠킹된 바이너리로도 u4 점유를 검사한다).
        struct ComputeUAVUsage
        {
            const utf8* _pShader;
            uint32      _bufferCount;
        };
        inline constexpr ComputeUAVUsage kArrComputeUAVUsage[] = {
            {          "gpucull", 3},
            {"instanceanimation", 1},
            {     "instancesort", 2},
            {        "lightgrid", 1},
            {        "meshmorph", 1},
            {         "meshskin", 1},
            {   "occlusiontiles", 1},
            {      "probeupdate", 2},
            {     "canvasmarker", 1},
            {  "vfxmeshinstance", 1},
            {      "vfxsimulate", 4},
            {    "volumetricfog", 1},
        };
        constexpr bool allComputeUAVUsageFits()
        {
            for ( const ComputeUAVUsage& usage : kArrComputeUAVUsage )
            {
                if ( usage._bufferCount > kComputeShaderUAVSlotCount )
                    return false;
            }
            return true;
        }
        static_assert( allComputeUAVUsageFits(), "컴퓨트 셰이더가 셰이더 소유 쓰기 슬롯(u0..u3)을 넘어 assert 링 슬롯(u4)을 침범한다" );
        static_assert( kStaticSamplerArrayCount + 1 == kStaticSamplerCount && kSamplerShadowCmp == kStaticSamplerArrayCount,
                       "비교 샘플러는 샘플러 배열 바로 다음 번호(마지막)여야 한다" );
        static_assert( gl::kUAVBinding0 >= kSRVSlotCount && gl::kUAVBinding0 >= kComputeSRVSlotCount, "GL UAV SSBO 번호가 SRV 번호와 겹친다" );
    } // namespace shaderslot
} // namespace sw
