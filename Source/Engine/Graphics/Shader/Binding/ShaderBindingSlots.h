/**
 * @file ShaderBindingSlots.h
 * @brief 셰이더 바인딩 계약의 C++ 쪽입니다. `Resource/engine/shaders/bindingslots.hlsli` 를 **그대로 include** 합니다.
 * @details 번호는 이 파일에 없다. HLSL 과 C++ 가 같은 파일을 읽으므로 "수동 동기" 가 없다.
 *          백엔드 4개는 여기 constexpr 로만 바인딩 위치를 정하고, ShaderBindingValidator 가 쿠킹된 바이너리의
 *          리플렉션을 이 값과 대조한다(런타임 로드 시와 테스트).
 *
 *          모델은 언리얼 GPUScene 방식이다. 셰이더 선언은 네 백엔드에서 같고(register b#/t#/u#), 드로우별 데이터는
 *          바인딩이 아니라 버퍼(인스턴스 t4, 머티리얼 t9)에서 인덱스로 읽는다. 백엔드는 리플렉션이 준 슬롯을
 *          각자의 방식으로 바인딩한다. DX12 는 루트 CBV 와 디스크립터 테이블, Vulkan 은 세트 0 의 시프트된 binding,
 *          DX11 과 GL 은 슬롯이다. 텍스처만 DX12 와 Vulkan 이 무제한 배열(`bindless::`)이고 DX11 과 GL 은 고정 슬롯이다.
 */
#pragma once
#include "Core/Common/Types.h"

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
         * @details 네이티브 bindless(DX12, Vulkan)는 `swSampleIndex` 가 이 샘플러로 읽는다. 슬롯 결합 샘플러를 쓰는 DX11 과 GL 은 엔진이 t0..t3 에
         *          같은 샘플러를 바인딩한다(언리얼 후처리 입력의 Bilinear, Clamp 정적 샘플러). 랩이면 화면 가장자리의 탭이 반대편을 읽는다.
         */
        inline constexpr uint32 kEngineTextureSampler = SW_ENGINE_TEXTURE_SAMPLER;
        inline constexpr uint32 kInstanceBuffer       = SW_SLOT_INSTANCE_SRV;
        /**
         * @brief 네이티브 bindless 가 없는 백엔드(DX11, GL)의 머티리얼 텍스처 슬롯 t5..t8 입니다.
         * @details 그 두 백엔드는 머티리얼이 준 전역 인덱스를 셰이더에서 해석할 수 없다. DX11 은 SM5.0 이라
         *          리소스 배열 동적 인덱싱이 없고(그건 SM5.1, 곧 D3D12), GL 은 SPIR-V 로 먹이므로
         *          ARB_bindless_texture 를 쓸 수 없다. 그래서 엔진이 머티리얼 텍스처를 이 고정 슬롯에
         *          바인딩하고 머티리얼 데이터에는 서수를 넣는다.
         */
        inline constexpr uint32 kMaterialTexture0     = SW_SLOT_MATERIAL_TEX0;
        inline constexpr uint32 kMaterialTextureCount = SW_MATERIAL_TEXTURE_SLOT_COUNT;
        /**
         * @brief 머티리얼 텍스처를 읽는 샘플러(bindingslots.hlsli 4 의 SW_SAMPLER_*)입니다. 선형 필터와 랩 주소를 씁니다.
         * @details 네이티브 bindless(DX12, Vulkan)는 `swSampleMaterialTexture` 가 이 샘플러로 읽는다. 슬롯 결합 샘플러를 쓰는
         *          DX11 과 GL 은 셰이더가 고르지 못하므로 엔진이 머티리얼 슬롯 t5..t8 에 같은 샘플러를 바인딩한다(언리얼 머티리얼 텍스처의 기본 Wrap 샘플러).
         */
        inline constexpr uint32 kMaterialTextureSampler = SW_MATERIAL_TEXTURE_SAMPLER;
        /// @brief GPUScene 머티리얼 데이터 구조 버퍼(g_SwMaterials)입니다. 인스턴스의 materialIndex 로 읽습니다.
        inline constexpr uint32 kMaterialBuffer = SW_SLOT_MATERIAL_BUFFER;
        /**
         * @brief GPU 컬링이 만든 가시 인스턴스 ID 목록(g_SwVisibleInstanceIDs)입니다.
         * @details 컬링 컴퓨트가 살아남은 인스턴스 번호를 배치 구간에 압축해 넣고, 정점 셰이더가 이 순서로
         *          읽는다. 언리얼 FInstanceCullingContext 의 InstanceIdBuffer 에 해당한다.
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
         * @details 정점 셰이더가 배치 테이블의 VAT 시작과 인스턴스의 시간 오프셋으로 읽는다(binding.hlsli `swLoadAnimatedVertex`).
         */
        inline constexpr uint32 kVertexAnimationBuffer = SW_SLOT_VERTEX_ANIMATION_SRV;
        /**
         * @brief 캔버스(화면 2D, 곧 UI와 월드 글자) 사각형 구조 버퍼(g_SwCanvasQuads)입니다. 캔버스 패스만 바인딩합니다.
         * @details 정점 셰이더가 사각형 하나를 인스턴스 하나로 읽고 `SV_VertexID` 로 모서리를 만든다(동적 정점 버퍼는 없다).
         */
        inline constexpr uint32 kCanvasQuadBuffer = SW_SLOT_CANVAS_QUAD_SRV;
        inline constexpr uint32 kSrvSlotCount     = SW_SRV_SLOT_COUNT;

        // ------------------------------------------------------------------------------
        // 2-1) 라이트 원소의 타입 값. bindingslots.hlsli 가 기준이다(셰이더와 같은 파일).
        // ------------------------------------------------------------------------------
        inline constexpr uint32 kLightTypeDirectional = SW_LIGHT_TYPE_DIRECTIONAL;
        inline constexpr uint32 kLightTypePoint       = SW_LIGHT_TYPE_POINT;
        inline constexpr uint32 kLightTypeSpot        = SW_LIGHT_TYPE_SPOT;
        inline constexpr uint32 kLightTypePoint2D     = SW_LIGHT_TYPE_POINT2D;
        inline constexpr uint32 kLightTypeGlobal2D    = SW_LIGHT_TYPE_GLOBAL2D;
        inline constexpr uint32 kLightTypeCount       = SW_LIGHT_TYPE_COUNT;

        // 패스 플래그: PassCB `g_Flags` 의 비트(bindingslots.hlsli 9).
        inline constexpr uint32 kPassFlagNativeBindless = SW_PASS_FLAG_NATIVE_BINDLESS;
        inline constexpr uint32 kPassFlagSkipPost       = SW_PASS_FLAG_SKIP_POST;
        inline constexpr uint32 kPassFlagSkipTonemap    = SW_PASS_FLAG_SKIP_TONEMAP;
        /// @brief 2D 그림자 가림막 토막 원소의 종류입니다. 빛 종류가 아니라서(레지스트리 슬롯이 없다) `kLightTypeCount` 밖에 있습니다.
        inline constexpr uint32 kLightTypeShadow2D = SW_LIGHT_TYPE_SHADOW2D;
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
        /// @brief VAT 노멀 팔면체 한 칸의 해상도입니다.
        inline constexpr uint32 kVertexAnimationNormalSteps = SW_VERTEX_ANIMATION_NORMAL_STEPS;
        /// @brief 거스트너 파도의 항 수입니다(진폭이 0인 항은 비어 있는 항입니다).
        inline constexpr uint32 kGerstnerWaveCount = SW_GERSTNER_WAVE_COUNT;

        // ------------------------------------------------------------------------------
        // 3) 컴퓨트와 샘플러
        // ------------------------------------------------------------------------------
        inline constexpr uint32 kComputeSrvSlotCount = SW_COMPUTE_SRV_SLOT_COUNT;
        inline constexpr uint32 kComputeUavSlotCount = SW_COMPUTE_UAV_SLOT_COUNT;
        /// @brief 컴퓨트 RW 텍스처 슬롯 u4..u7 입니다. DX11 과 GL 만 실제 슬롯이고, DX12 와 Vulkan 은 배열 인덱스라 bindComputeUav 가 무시합니다.
        inline constexpr uint32 kComputeTextureUav0         = SW_SLOT_COMPUTE_TEXUAV0;
        inline constexpr uint32 kComputeTextureUavSlotCount = SW_COMPUTE_TEXUAV_SLOT_COUNT;
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
            static_assert( kStaticSampler0 >= kMaterialTexture0 + kMaterialTextureCount, "DX11 정적 샘플러가 슬롯 결합 샘플러(s0..s8)와 겹친다" );
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
            inline constexpr uint32 kRootCbvCount         = kConstantBufferSlotCount; ///< b0..b(N-1) 루트 CBV
            inline constexpr uint32 kRootTableCount       = 3;                        ///< t 슬롯 테이블, u 슬롯 테이블, 텍스처 배열 테이블
            inline constexpr uint32 kRootSignatureDwords  = kRootCbvCount * kRootDescriptorDwords + kRootTableCount * kRootTableDwords + kRootConstantDwords;
            static_assert( kRootSignatureDwords <= kRootBudgetDwords, "DX12 루트 시그니처가 64 dword 예산을 넘는다" );
        } // namespace dx12

        /// @brief OpenGL SSBO 번호입니다. u# 는 t# 와 겹치지 않게 SW_GL_UAV_BINDING0 부터 셉니다(DXC -fvk-u-shift 값이기도 합니다).
        namespace gl
        {
            inline constexpr uint32 kUavBinding0 = SW_GL_UAV_BINDING0;
            inline constexpr uint32 kImageUnit0  = SW_GL_IMAGE_UNIT0; ///< 컴퓨트 RW 텍스처 u4..u7이 이미지 유닛 0..3 에 대응한다.
        } // namespace gl

        /// @brief 엔진 예약 CB 이름입니다(리플렉션 매칭 키).
        namespace cbname
        {
            inline constexpr const utf8* kPass      = "PassCB";
            inline constexpr const utf8* kMaterial  = "MaterialCB";
            inline constexpr const utf8* kCull      = "CullParams";
            inline constexpr const utf8* kAnimation = "AnimParams"; ///< instanceanim.hlsl 의 컴퓨트 CB(b0)
            inline constexpr const utf8* kSort      = "SortParams"; ///< instancesort.hlsl 의 컴퓨트 CB(b0)
            /// @brief SW_ROOT_CONSTANTS_BEGIN/END 가 선언하는 루트/푸시 상수 블록입니다(DX12 b0 space2, Vulkan 푸시 상수, DX11 과 GL 의 b2).
            inline constexpr const utf8* kRootConstants = "SwRootConstants";
        } // namespace cbname

        /// @brief 엔진 예약 리소스 이름입니다. binding.hlsli 와 gpucull.hlsl 의 선언과 같아야 합니다(계약 검증 키).
        namespace resname
        {
            inline constexpr const utf8* kInstances            = "g_SwInstances";
            inline constexpr const utf8* kMaterials            = "g_SwMaterials";
            inline constexpr const utf8* kEngineTexture        = "g_SwSlot";        ///< 뒤에 0..3 이 붙는다.
            inline constexpr const utf8* kMaterialTexture      = "g_SwMaterialTex"; ///< 뒤에 0..3 이 붙는다.
            inline constexpr const utf8* kBindlessTextures     = "g_SwBindlessTex2D";
            inline constexpr const utf8* kBindlessRwTextures   = "g_SwBindlessRWTex2D";
            inline constexpr const utf8* kSamplers             = "g_SwSamplers";           ///< Vulkan: set 1 binding 1 immutable sampler 배열(s0..s6)
            inline constexpr const utf8* kSamplerSlot          = "g_SwSampler";            ///< 뒤에 0..6 이 붙는다. DX12 정적 샘플러 s#(배열은 정적 샘플러로 채울 수 없다).
            inline constexpr const utf8* kShadowSampler        = "g_SwSamplerShadowCmp";   ///< s7 비교 샘플러
            inline constexpr const utf8* kRwTextureSlot        = "g_SwRWSlot";             ///< 뒤에 0..3 이 붙는다(DX11/GL 컴퓨트 RW 텍스처 슬롯).
            inline constexpr const utf8* kVisibleInstances     = "g_SwVisibleInstanceIDs"; ///< t10(그래픽스). 컬링이 만든 가시 목록
            inline constexpr const utf8* kMorphVertices        = "g_SwMorphVertices";      ///< t11(그래픽스). GPU 가 변형한 정점 풀
            inline constexpr const utf8* kLights               = "g_SwLights";             ///< t12(그래픽스). 씬 라이트 목록
            inline constexpr const utf8* kBatches              = "g_SwBatches";            ///< t13(그래픽스). 씬 배치 테이블
            inline constexpr const utf8* kVertexAnimation      = "g_SwVertexAnimation";    ///< t14(그래픽스). 정점 애니메이션(VAT) 테이블
            inline constexpr const utf8* kCanvasQuads          = "g_SwCanvasQuads";        ///< t15(캔버스 패스). 화면 2D 사각형
            inline constexpr const utf8* kMorphRestVertices    = "g_RestVertices";         ///< meshmorph t0. 레스트 포즈
            inline constexpr const utf8* kMorphVerticesRw      = "g_MorphVerticesRW";      ///< meshmorph 와 meshskin 의 u0. 변형 결과
            inline constexpr const utf8* kSkinWeights          = "g_SkinWeights";          ///< meshskin t1. 스킨 정점의 가중치와 팔레트 행 번호
            inline constexpr const utf8* kSkinPalette          = "g_SkinPalette";          ///< meshskin t2. 본 팔레트(본 하나가 float4 세 개)
            inline constexpr const utf8* kSkinInstances        = "g_SkinInstances";        ///< meshskin t3. 스킨 인스턴스 테이블(인스턴스 하나가 uint4 두 개)
            inline constexpr const utf8* kCullInstances        = "g_Instances";
            inline constexpr const utf8* kCullBatchInfo        = "g_BatchInfo"; ///< 컬링 t1. 배치의 인스턴스 구간
            inline constexpr const utf8* kCullIndirectArgs     = "g_IndirectArgs";
            inline constexpr const utf8* kCullVisibleIDs       = "g_VisibleInstanceIDs"; ///< 컬링 u1. 압축해서 쓰는 쪽
            inline constexpr const utf8* kAnimationInstancesRw = "g_InstancesRW";        ///< instanceanim u0. 월드 행렬을 고쳐 쓴다.
        } // namespace resname

        // 계약의 내부 일관성을 검사한다. 값을 바꾸면 여기서 먼저 걸린다.
        static_assert( kInstanceBuffer == kEngineTexture0 + kEngineTextureCount, "인스턴스 버퍼는 엔진 텍스처 슬롯 바로 다음이어야 한다" );
        static_assert( kMaterialTexture0 == kInstanceBuffer + 1, "머티리얼 텍스처 슬롯은 인스턴스 버퍼 바로 다음이어야 한다" );
        static_assert( SW_SLOT_MATERIAL_TEX1 == SW_SLOT_MATERIAL_TEX0 + 1 && SW_SLOT_MATERIAL_TEX2 == SW_SLOT_MATERIAL_TEX0 + 2 &&
                           SW_SLOT_MATERIAL_TEX3 == SW_SLOT_MATERIAL_TEX0 + 3,
                       "머티리얼 텍스처 슬롯은 연속이어야 한다 (셰이더가 서수로 고른다)" );
        static_assert( kMaterialBuffer == kMaterialTexture0 + kMaterialTextureCount, "머티리얼 데이터 버퍼는 머티리얼 텍스처 다음이어야 한다" );
        static_assert( kVisibleInstanceBuffer == kMaterialBuffer + 1, "가시 인스턴스 ID 버퍼는 머티리얼 데이터 다음이어야 한다" );
        static_assert( kMorphVertexBuffer == kVisibleInstanceBuffer + 1, "모프 정점 버퍼는 가시 목록 다음이어야 한다" );
        static_assert( kLightBuffer == kMorphVertexBuffer + 1, "라이트 버퍼는 모프 정점 다음이다" );
        static_assert( kBatchBuffer == kLightBuffer + 1, "배치 표는 라이트 다음이다" );
        static_assert( kVertexAnimationBuffer == kBatchBuffer + 1, "VAT 표는 배치 표 다음이다" );
        static_assert( kCanvasQuadBuffer == kVertexAnimationBuffer + 1 && kCanvasQuadBuffer + 1 == kSrvSlotCount, "캔버스 사각형은 VAT 표 다음이고 SRV 슬롯의 마지막이다" );
        static_assert( kLightTypeDirectional < kLightTypeCount && kLightTypePoint < kLightTypeCount &&
                           kLightTypeSpot < kLightTypeCount && kLightTypePoint2D < kLightTypeCount && kLightTypeGlobal2D < kLightTypeCount &&
                           kLightTypeShadow2D >= kLightTypeCount,
                       "라이트 타입 값이 타입 수 안에 있어야 한다" );
        static_assert( SW_SLOT_ENGINE_TEX3 == kEngineTexture0 + kEngineTextureCount - 1, "엔진 텍스처 슬롯은 연속이어야 한다" );
        static_assert( kPassConstantBuffer != kMaterialConstantBuffer && kMaterialConstantBuffer < kConstantBufferSlotCount &&
                           kComputeConstantBuffer < kConstantBufferSlotCount,
                       "예약 CB 슬롯은 백엔드가 마련한 b# 자리 안이어야 한다" );
        static_assert( kSrvSlotCount <= vk::kRangeSize && kComputeUavSlotCount <= vk::kRangeSize && kConstantBufferSlotCount <= vk::kRangeSize,
                       "슬롯 수가 Vulkan 세트 0 의 종류별 범위 크기을 넘는다" );
        static_assert( vk::kTShift == vk::kBShift + vk::kRangeSize && vk::kUShift == vk::kTShift + vk::kRangeSize &&
                           vk::kSlotBindingCount == vk::kUShift + vk::kRangeSize,
                       "Vulkan 세트 0 범위(b/t/u)는 연속이어야 한다" );
        static_assert( kRootConstantSpace != 0 && kRootConstantSpace != bindless::kTextureSpace, "루트 상수 space 가 슬롯/텍스처 배열과 겹친다" );
        static_assert( kRootConstantEmulationSlot < kConstantBufferSlotCount && kRootConstantEmulationSlot != kPassConstantBuffer && kRootConstantEmulationSlot != kMaterialConstantBuffer,
                       "루트 상수 에뮬 슬롯은 예약 CB 와 겹치지 않는 b# 자리여야 한다" );
        static_assert( kComputeTextureUav0 == kComputeUavSlotCount && kComputeTextureUav0 + kComputeTextureUavSlotCount <= vk::kRangeSize,
                       "컴퓨트 RW 텍스처 슬롯은 버퍼 UAV 슬롯 바로 다음이어야 한다" );
        static_assert( SW_SLOT_COMPUTE_TEXUAV1 == SW_SLOT_COMPUTE_TEXUAV0 + 1 && SW_SLOT_COMPUTE_TEXUAV2 == SW_SLOT_COMPUTE_TEXUAV0 + 2 &&
                           SW_SLOT_COMPUTE_TEXUAV3 == SW_SLOT_COMPUTE_TEXUAV0 + 3 && SW_GL_IMAGE_UNIT1 == SW_GL_IMAGE_UNIT0 + 1 &&
                           SW_GL_IMAGE_UNIT2 == SW_GL_IMAGE_UNIT0 + 2 && SW_GL_IMAGE_UNIT3 == SW_GL_IMAGE_UNIT0 + 3,
                       "RW 텍스처 슬롯/이미지 유닛은 연속이어야 한다 (셰이더가 서수로 고른다)" );
        static_assert( kStaticSamplerArrayCount + 1 == kStaticSamplerCount && kSamplerShadowCmp == kStaticSamplerArrayCount,
                       "비교 샘플러는 샘플러 배열 바로 다음 번호(마지막)여야 한다" );
        static_assert( gl::kUavBinding0 >= kSrvSlotCount && gl::kUavBinding0 >= kComputeSrvSlotCount, "GL UAV SSBO 번호가 SRV 번호와 겹친다" );
    } // namespace shaderslot
} // namespace sw
