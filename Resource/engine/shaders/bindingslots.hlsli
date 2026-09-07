/**
 * bindingslots.hlsli — 셰이더 바인딩 계약의 **유일한 정본**.
 *
 * 이 파일은 순수 전처리기 정의만 담는다(#define 정수 + 주석). 그래서 HLSL 과 C++ 가 **같은 파일을
 * include** 한다 — C++ 쪽은 Source/Engine/Graphics/Shader/ShaderBindingSlots.h 가 이 파일을 include 해
 * 같은 값을 constexpr 로 노출하고, 4개 백엔드(DX11/DX12/Vulkan/GL)는 그 상수로만 바인딩한다.
 * ShaderBindingContract::validate 가 구운 바이너리의 리플렉션을 이 표와 대조한다.
 *
 * 규칙: 이 파일에는 #define 과 주석만 둔다 (C++ 컴파일러가 그대로 읽는다). 산술식·함수형 매크로 금지.
 *
 * 모델 (언리얼 GPUScene 식): 셰이더 선언은 네 백엔드에서 **같다** — register(b#/t#/u#) 하나로 쓰고, 백엔드는
 * 그 번호를 각자의 방식으로 건다. 드로우마다 바뀌는 데이터는 바인딩을 갈아 끼우지 않고 **큰 버퍼에서 인덱스로 읽는다**
 * (인스턴스 g_SwInstances[t4], 머티리얼 g_SwMaterials[t9]) — 그래서 드로우 사이에 바뀌는 바인딩이 없고 슬롯 번호는
 * 리플렉션이 준다. 텍스처만 백엔드가 갈린다: DX12/Vulkan 은 무제한 배열(g_SwBindlessTex2D[]), DX11/GL 은 고정 슬롯.
 *
 *   레지스터 → 백엔드 바인딩 자리
 *   ┌────────────────────┬──────────────┬─────────────────────┬─────────────────────┬──────────────────┐
 *   │ 논리 리소스        │ HLSL         │ DX12                │ Vulkan (set 0)      │ OpenGL           │
 *   ├────────────────────┼──────────────┼─────────────────────┼─────────────────────┼──────────────────┤
 *   │ PassCB / 컴퓨트 CB │ b0           │ 루트 CBV            │ UBO  binding 0      │ UBO 0            │
 *   │ MaterialCB (픽스처)│ b1           │ 루트 CBV            │ UBO  binding 1      │ UBO 1            │
 *   │ 엔진 텍스처 슬롯   │ t0..t3 (에뮬)│ (없음)              │ (없음)              │ 텍스처 유닛 0..3 │
 *   │ 인스턴스 구조버퍼  │ t4           │ t 테이블            │ SSBO binding 16+4   │ SSBO 4           │
 *   │ 머티리얼 텍스처    │ t5..t8 (에뮬)│ (없음)              │ (없음)              │ 텍스처 유닛 5..8 │
 *   │ 머티리얼 데이터    │ t9           │ t 테이블            │ SSBO binding 16+9   │ SSBO 9           │
 *   │ 컴퓨트 읽기 버퍼   │ t0..t3       │ t 테이블            │ SSBO binding 16+#   │ SSBO #           │
 *   │ 컴퓨트 쓰기 버퍼   │ u0..u3       │ u 테이블            │ SSBO binding 32+#   │ SSBO 16+#        │
 *   │ bindless 텍스처    │ t0 space1    │ 테이블 (힙 전체)    │ set 1 binding 0     │ (없음)           │
 *   │ 정적 샘플러        │ s0           │ 정적 샘플러         │ set 1 binding 1     │ (결합 샘플러)    │  DX11: s9..s15 샘플러 상태
 *   │ 루트 상수          │ b0 space2    │ 32비트 루트 상수    │ 푸시 상수           │ (UBO 에뮬)       │
 *   └────────────────────┴──────────────┴─────────────────────┴─────────────────────┴──────────────────┘
 *   - DX12: b# 는 루트 CBV(GPU 주소), t#/u# 슬롯은 디스크립터 테이블(오프라인 힙의 뷰를 드로우 직전 온라인 블록에 복사 — 언리얼
 *     FD3D12DescriptorCache), 텍스처 배열은 힙 시작을 가리키는 테이블. 루트 예산은 3*2 + 3*1 + 16 = 25/64 dword (ShaderBindingSlots.h dx12).
 *   - Vulkan: set 0 은 "슬롯 세트" — 레지스터 종류별 시프트(DXC -fvk-b/t/u-shift)로 binding 이 정해진다. 드로우/디스패치
 *     직전에 바인딩 상태가 바뀌었으면 세트를 새로 할당해 쓴다(언리얼 Vulkan RHI 와 같은 방식). set 1 은 텍스처 배열.
 *   - OpenGL: DescriptorSet 을 무시하고 binding 만 본다 — b# 가 곧 UBO #, t# 가 곧 텍스처 유닛/SSBO #, u# 는 SSBO 16+#.
 *   - DX11(SM5.0): space 를 모르고 리소스 배열 동적 인덱싱이 없다 — 텍스처는 고정 슬롯이다.
 */

#ifndef SW_ENGINE_BINDINGSLOTS_HLSLI
#define SW_ENGINE_BINDINGSLOTS_HLSLI

// ------------------------------------------------------------------------------
// 1) 상수버퍼 (b#, space0)
// ------------------------------------------------------------------------------
#define SW_SLOT_PASS_CB          0
#define SW_SLOT_MATERIAL_CB      1
#define SW_SLOT_COMPUTE_CB       0
#define SW_CB_SLOT_COUNT         3   // b0..b2 — b2 는 DX11/GL 루트 상수 에뮬 자리(SW_SLOT_ROOT_CB_EMUL)
#define SW_MAX_CONSTANT_BUFFER   16

// 루트/푸시 상수 — DX12 b0 space2 (32비트 루트 상수), Vulkan 푸시 상수, DX11/GL 은 UBO 에뮬. setComputeRootConstants 전용.
#define SW_SLOT_ROOT_CB          0
#define SW_SPACE_ROOT_CB         2
#define SW_ROOT_DWORD_COUNT      16
#define SW_SLOT_ROOT_CB_EMUL     2   // DX11/GL: 루트 상수를 담는 상수버퍼 슬롯 (SW_DECLARE_ROOT_CONSTANTS)
// ------------------------------------------------------------------------------
// 2) SRV (t#, space0). t0..t9 총 10개 — 엔진 텍스처 슬롯(에뮬)·인스턴스·머티리얼 텍스처(에뮬)·머티리얼 데이터.
// ------------------------------------------------------------------------------
#define SW_SLOT_ENGINE_TEX0            0
#define SW_SLOT_ENGINE_TEX1            1
#define SW_SLOT_ENGINE_TEX2            2
#define SW_SLOT_ENGINE_TEX3            3
#define SW_ENGINE_TEXTURE_SLOT_COUNT   4
#define SW_FALLBACK_SRV_COUNT          4   // = SW_ENGINE_TEXTURE_SLOT_COUNT (옛 이름)

// GPUScene 인스턴스 구조버퍼 (per-instance world/material). 엔진 텍스처 슬롯 바로 다음. 네 백엔드 공통.
#define SW_SLOT_INSTANCE_SRV           4

// 머티리얼 텍스처 고정 슬롯. DX11/GL 은 bindless 가 없어 머티리얼이 준 전역 인덱스를 풀 수 없으므로
// 엔진이 텍스처를 이 슬롯에 걸고 머티리얼 데이터에는 **서수(0..N-1)** 를 넣는다. DX12/Vulkan 은 전역 인덱스.
#define SW_SLOT_MATERIAL_TEX0          5
#define SW_SLOT_MATERIAL_TEX1          6
#define SW_SLOT_MATERIAL_TEX2          7
#define SW_SLOT_MATERIAL_TEX3          8
#define SW_MATERIAL_TEXTURE_SLOT_COUNT 4

// GPUScene 머티리얼 데이터 구조버퍼 (StructuredBuffer<SwMaterialData_t> g_SwMaterials) — 인스턴스의 materialIndex 로 읽는다.
// 머티리얼 셰이더 타입마다 버퍼 하나(원소 = 그 셰이더의 머티리얼 구조체). 네 백엔드 공통.
#define SW_SLOT_MATERIAL_BUFFER        9

#define SW_SRV_SLOT_COUNT              10  // t0..t9 — DX12 t 테이블 크기, Vulkan set 0 의 t 밴드 폭 이내

// ------------------------------------------------------------------------------
// 3) 컴퓨트 — CB 는 b0, 읽기 버퍼 t0..t3, 쓰기 버퍼 u0..u3 (space0)
// ------------------------------------------------------------------------------
#define SW_COMPUTE_SRV_SLOT_COUNT      4
#define SW_COMPUTE_UAV_SLOT_COUNT      4

// 컴퓨트 RW 텍스처 — DX11/GL 은 고정 슬롯 u4..u7(g_SwRWSlot#), DX12/Vulkan 은 무제한 배열(g_SwBindlessRWTex2D, 아래 5)을
// 인덱스로 고른다. 셰이더는 SW_StoreTex2D( index, coord, value ) 로만 쓴다 — 에뮬 백엔드에서 index 는 슬롯 서수(0..3).
#define SW_SLOT_COMPUTE_TEXUAV0        4
#define SW_SLOT_COMPUTE_TEXUAV1        5
#define SW_SLOT_COMPUTE_TEXUAV2        6
#define SW_SLOT_COMPUTE_TEXUAV3        7
#define SW_COMPUTE_TEXUAV_SLOT_COUNT   4

// ------------------------------------------------------------------------------
// 4) 정적 샘플러 (s#, space0)
// ------------------------------------------------------------------------------
//    DX12: 루트 시그니처 정적 샘플러 s0..s7. Vulkan: set 1 binding SW_VK_SAMPLER_BINDING 의 immutable sampler 배열(0..6) +
//    binding SW_VK_SHADOW_SAMPLER_BINDING 의 비교 샘플러. 셰이더는 g_SwSamplers[SW_SAMPLER_*] / g_SwSamplerShadowCmp 로 쓴다.
//    비교 샘플러는 HLSL 타입이 달라(SamplerComparisonState) 배열에 못 들어가므로 마지막 번호다.
#define SW_STATIC_SAMPLER_COUNT        8
#define SW_STATIC_SAMPLER_ARRAY_COUNT  7   // g_SwSamplers[0..6] — 비교 샘플러 제외

#define SW_SAMPLER_LINEAR_WRAP      0
#define SW_SAMPLER_LINEAR_CLAMP     1
#define SW_SAMPLER_POINT_WRAP       2
#define SW_SAMPLER_POINT_CLAMP      3
#define SW_SAMPLER_LINEAR_MIRROR    4
#define SW_SAMPLER_ANISO_WRAP       5
#define SW_SAMPLER_POINT_BORDER     6
#define SW_SAMPLER_SHADOW_CMP       7

// DX11(SM5.0) 정적 샘플러 세트 자리 s9..s15 — 슬롯 결합 샘플러(s0..s8, t# 와 같은 번호)와 겹치지 않는다. 엔진이 디바이스
// 초기화 때 한 번 걸어 두고 셰이더는 SW_SampleIndexWith 의 samplerId 로 고른다(SM5.0 은 샘플러 배열 동적 인덱싱이 없어 리터럴 분기).
// 언리얼 D3D11 RHI 는 슬롯마다 엔진이 고른 샘플러를 걸 뿐 셰이더가 고르는 세트가 없다 — 여기서는 DX12/Vulkan 과 같은
// SW_SAMPLER_* 를 DX11 도 존중하게 한 것이다. GL 은 결합 샘플러뿐(ARB_gl_spirv 는 분리 샘플러를 못 쓴다)이라 세트가 없다.
#define SW_DX11_STATIC_SAMPLER0        9
#define SW_DX11_STATIC_SAMPLER1        10
#define SW_DX11_STATIC_SAMPLER2        11
#define SW_DX11_STATIC_SAMPLER3        12
#define SW_DX11_STATIC_SAMPLER4        13
#define SW_DX11_STATIC_SAMPLER5        14
#define SW_DX11_STATIC_SAMPLER6        15
#define SW_DX11_MAX_SAMPLER_SLOT_COUNT 16  // D3D11_COMMONSHADER_SAMPLER_SLOT_COUNT

// ------------------------------------------------------------------------------
// 5) 네이티브 bindless 텍스처 배열 (DX12/Vulkan) — 슬롯 리소스와 다른 자리에 둔다.
//    DX12: Texture2D g_SwBindlessTex2D[] : register(t0, space1) 와 RWTexture2D g_SwBindlessRWTex2D[] : register(u0, space1)
//          — 루트 시그니처의 유일한 테이블(범위 둘, 둘 다 힙 시작에서 무제한).
//    Vulkan: set 1 binding 0 (COMBINED_IMAGE_SAMPLER[]), binding 1 = immutable sampler 배열, binding 2 = 비교 샘플러,
//            binding 3 = STORAGE_IMAGE[] (RW 텍스처).
// ------------------------------------------------------------------------------
#define SW_SPACE_BINDLESS_TEX          1
#define SW_VK_TEXTURE_SET              1
#define SW_VK_TEXTURE_BINDING          0
#define SW_VK_SAMPLER_BINDING          1
#define SW_VK_SHADOW_SAMPLER_BINDING   2
#define SW_VK_RWTEXTURE_BINDING        3

// ------------------------------------------------------------------------------
// 6) Vulkan 슬롯 세트(set 0) — binding = 레지스터 종류별 시프트 + 번호. DXC 에 -fvk-b-shift/-fvk-t-shift/-fvk-u-shift 로
//    넘기고(ShaderCompiler.cpp), 파이프라인 레이아웃(VulkanRHIDeviceDescriptor.cpp)이 같은 번호에 바인딩을 만든다.
//    b0..b15 → 0..15 (UBO), t0..t15 → 16..31 (SSBO), u0..u15 → 32..47 (SSBO).
// ------------------------------------------------------------------------------
#define SW_VK_B_SHIFT                  0
#define SW_VK_T_SHIFT                  16
#define SW_VK_U_SHIFT                  32
#define SW_VK_SLOT_BAND_WIDTH          16
#define SW_VK_SLOT_BINDING_COUNT       48

// ------------------------------------------------------------------------------
// 7) OpenGL SSBO 번호 — t# 는 binding #, u# 는 binding SW_GL_UAV_BINDING0 + # (DXC -fvk-u-shift 가 이 값이다).
//    둘을 나누지 않으면 gpucull 의 g_Instances(t0) 와 g_IndirectArgs(u0) 가 같은 SSBO 자리를 다툰다.
// ------------------------------------------------------------------------------
#define SW_GL_UAV_BINDING0             16
// OpenGL 이미지 유닛 — 컴퓨트 RW 텍스처 u4..u7 → 이미지 유닛 0..3 (SSBO 와 다른 이름공간, common.hlsli 가 명시 binding 으로 적는다).
#define SW_GL_IMAGE_UNIT0              0
#define SW_GL_IMAGE_UNIT1              1
#define SW_GL_IMAGE_UNIT2              2
#define SW_GL_IMAGE_UNIT3              3

#endif // SW_ENGINE_BINDINGSLOTS_HLSLI
