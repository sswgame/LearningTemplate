/**
 * bindingslots.hlsli — 셰이더 바인딩 계약의 **유일한 정본**.
 *
 * 이 파일은 순수 전처리기 정의만 담는다(#define 정수 + 주석). 그래서 HLSL 과 C++ 가 **같은 파일을
 * include** 한다 — C++ 쪽은 Source/Engine/Graphics/Shader/ShaderBindingSlots.h 가 이 파일을 include 해
 * 같은 값을 constexpr 로 노출하고, 4개 백엔드(DX11/DX12/Vulkan/GL)는 그 상수로만 바인딩한다.
 * 예전엔 이 번호들이 HLSL 매크로·C++ 헤더·백엔드 4곳에 손으로 복사돼 있었고, 그 어긋남이
 * "검증 에러 없이 검게 나오는" 형태로 여섯 번 이상 반복됐다. 여기 한 줄을 바꾸면 양쪽이 함께 바뀐다.
 *
 * 규칙: 이 파일에는 #define 과 주석만 둔다 (C++ 컴파일러가 그대로 읽는다). 산술식·함수형 매크로 금지.
 *
 * 레지스터 → 백엔드 바인딩 위치 (같은 줄의 값이 곧 계약이다)
 *  ┌────────────────────┬──────────────┬────────────────┬──────────────────┬─────────────────┐
 *  │ 논리 리소스        │ HLSL         │ Vulkan set/bind│ OpenGL binding   │ DX12 루트 파라미터│
 *  ├────────────────────┼──────────────┼────────────────┼──────────────────┼─────────────────┤
 *  │ PassCB             │ b0 space0    │ set 0 / 0      │ UBO 0            │ CBV 테이블 (b0)  │
 *  │ MaterialCB         │ b1 space0    │ set 10 / 0     │ UBO 1            │ CBV 테이블 (b1)  │
 *  │ 컴퓨트 CB          │ b0 space0    │ set 0 / 0      │ UBO 0            │ CBV 테이블 (b0)  │
 *  │ 엔진 텍스처 슬롯   │ t0..t3       │ (네이티브 — 없음)│ 텍스처 유닛 0..3 │ SRV 테이블 t0..3 │
 *  │ 인스턴스 구조버퍼  │ t4 (VK: t0 space6)│ set 6 / 0 │ SSBO 4           │ SRV 테이블 t4    │
 *  │ 머티리얼 텍스처    │ t5..t8       │ (네이티브 — 없음)│ 텍스처 유닛 5..8 │ SRV 테이블 t5..8 │
 *  │ bindless 텍스처 배열│ t0 space1   │ set 1 / 0      │ (없음)           │ SRV 테이블 space1│
 *  │ 정적 샘플러        │ s0..s7 (VK: space4)│ set 4 / 0..7│ (결합 샘플러)   │ 정적 샘플러      │
 *  │ 컴퓨트 SRV         │ t0..t3 (VK: space6..9)│ set 6+#/0│ SSBO #          │ SRV 테이블 t#    │
 *  │ 컴퓨트 UAV         │ u0..u3 (VK: space7..9)│ set 7+#/0│ SSBO #          │ UAV 테이블 u#    │
 *  │ DX12 루트 상수     │ b0 space1    │ (푸시 상수)    │ (없음)           │ 32비트 루트 상수 │
 *  └────────────────────┴──────────────┴────────────────┴──────────────────┴─────────────────┘
 *
 * 백엔드별 함정 (계약이 이렇게 생긴 이유):
 *  - Vulkan 은 세트 단위 바인딩이라 상수버퍼 슬롯마다 세트가 하나씩 필요하다(b0=set0, b1=set10).
 *  - OpenGL(GL_ARB_gl_spirv)은 DescriptorSet 을 **무시**하고 binding 번호만 본다 — 그래서 GL 용 SPIR-V 는
 *    모든 리소스가 set 0 이어야 하고, b# 가 곧 UBO binding #, t# 가 곧 텍스처 유닛/SSBO # 다.
 *  - DX11(SM5.0)은 space 를 모르고 리소스 배열 동적 인덱싱이 없다 — 그래서 텍스처는 고정 슬롯이다.
 *  - DX12 는 SM6.6 ResourceDescriptorHeap 으로 힙을 직접 인덱싱한다 — 텍스처 슬롯은 쓰지 않는다.
 * 런타임/테스트 검증: ShaderBindingContract::validate 가 구운 바이너리의 리플렉션을 이 표와 대조한다.
 */

#ifndef SW_ENGINE_BINDINGSLOTS_HLSLI
#define SW_ENGINE_BINDINGSLOTS_HLSLI

// ------------------------------------------------------------------------------
// 1) 상수버퍼 (b#, space0)
// ------------------------------------------------------------------------------
#define SW_SLOT_PASS_CB          0
#define SW_SLOT_MATERIAL_CB      1
#define SW_SLOT_COMPUTE_CB       0
#define SW_MAX_CONSTANT_BUFFER   16

// DX12 루트 상수 — b0 space1 (g_BindlessCbIndex). Vulkan 은 같은 자리를 푸시 상수로 받는다.
#define SW_SLOT_BINDLESS_CB      0
#define SW_SPACE_BINDLESS_CB     1

// ------------------------------------------------------------------------------
// 2) SRV (t#, space0) — 에뮬 백엔드(DX11/GL)의 고정 슬롯. t0..t8 총 9개.
// ------------------------------------------------------------------------------
#define SW_SLOT_ENGINE_TEX0            0
#define SW_SLOT_ENGINE_TEX1            1
#define SW_SLOT_ENGINE_TEX2            2
#define SW_SLOT_ENGINE_TEX3            3
#define SW_ENGINE_TEXTURE_SLOT_COUNT   4
#define SW_FALLBACK_SRV_COUNT          4   // = SW_ENGINE_TEXTURE_SLOT_COUNT (옛 이름)

// GPUScene 인스턴스 구조버퍼 (per-instance world/material). 엔진 텍스처 슬롯 바로 다음.
#define SW_SLOT_INSTANCE_SRV           4

// 머티리얼 텍스처 고정 슬롯. DX11/GL 은 bindless 가 없어 머티리얼이 준 전역 인덱스를 풀 수 없으므로
// 엔진이 텍스처를 이 슬롯에 걸고 MaterialCB 에는 **서수(0..N-1)** 를 넣는다. DX12/Vulkan 은 전역 인덱스.
#define SW_SLOT_MATERIAL_TEX0          5
#define SW_SLOT_MATERIAL_TEX1          6
#define SW_SLOT_MATERIAL_TEX2          7
#define SW_SLOT_MATERIAL_TEX3          8
#define SW_MATERIAL_TEXTURE_SLOT_COUNT 4

#define SW_SRV_SLOT_COUNT              9   // t0..t8 — DX12 SRV 루트 테이블 수

// 네이티브 bindless 텍스처 배열 — t0 space1 (DX12) / set 1 (Vulkan)
#define SW_SPACE_BINDLESS_TEX          1

// ------------------------------------------------------------------------------
// 3) 컴퓨트 — CB 는 b0, 읽기 버퍼 t0..t3, 쓰기 버퍼 u0..u3 (space0)
// ------------------------------------------------------------------------------
#define SW_COMPUTE_SRV_SLOT_COUNT      4
#define SW_COMPUTE_UAV_SLOT_COUNT      4

// ------------------------------------------------------------------------------
// 4) 정적 샘플러 (s#; SPIR-V 는 space/set SW_SPACE_STATIC_SAMPLER)
// ------------------------------------------------------------------------------
#define SW_SPACE_STATIC_SAMPLER        4
#define SW_STATIC_SAMPLER_COUNT        8

#define SW_SAMPLER_LINEAR_WRAP      0
#define SW_SAMPLER_LINEAR_CLAMP     1
#define SW_SAMPLER_POINT_WRAP       2
#define SW_SAMPLER_POINT_CLAMP      3
#define SW_SAMPLER_LINEAR_MIRROR    4
#define SW_SAMPLER_ANISO_WRAP       5
#define SW_SAMPLER_SHADOW_CMP       6
#define SW_SAMPLER_POINT_BORDER     7

// ------------------------------------------------------------------------------
// 5) Vulkan 디스크립터 세트 번호 — 파이프라인 레이아웃(VulkanRHIDeviceDescriptor.cpp)과 HLSL 의
//    [[vk::binding(slot, set)]] 이 **이 값 하나**로 맞는다.
// ------------------------------------------------------------------------------
#define SW_VK_SET_PASS_CB              0   // b0 (UNIFORM_BUFFER)
#define SW_VK_SET_BINDLESS_TEX         1   // g_SwBindlessTex2D[] (COMBINED_IMAGE_SAMPLER 배열) = SW_SPACE_BINDLESS_TEX
#define SW_VK_SET_STATIC_SAMPLER       4   // 정적 샘플러 = SW_SPACE_STATIC_SAMPLER
#define SW_VK_SET_STORAGE0             6   // 읽기 구조버퍼 t# → set 6+# (STORAGE_BUFFER), 인스턴스 버퍼 포함
#define SW_VK_SET_STORAGE1             7
#define SW_VK_SET_STORAGE2             8
#define SW_VK_SET_STORAGE3             9
#define SW_VK_STORAGE_SET_COUNT        4   // set 6..9
#define SW_VK_SET_UAV0                 7   // 쓰기 구조버퍼 u# → set 7+# (STORAGE_BUFFER, 읽기 세트와 공유)
#define SW_VK_SET_UAV1                 8
#define SW_VK_SET_UAV2                 9
#define SW_VK_UAV_SET_COUNT            3   // set 7..9 — 한 디스패치에서 t# 와 u# 가 같은 세트를 가리키면 안 된다
#define SW_VK_SET_MATERIAL_CB          10  // b1 (UNIFORM_BUFFER)
#define SW_VK_BOUND_SET_COUNT          11  // 파이프라인 레이아웃이 요구하는 세트 수 (set 2,3,5 는 예약·미사용)

// 인스턴스 구조버퍼의 Vulkan space (= 세트). 옛 이름 호환.
#define SW_SPACE_INSTANCE_SRV          6

// ------------------------------------------------------------------------------
// 6) OpenGL SSBO 번호 — t# 는 binding #, u# 는 binding SW_GL_UAV_BINDING0 + # (DXC -fvk-u-shift 가 이 값이다).
//    둘을 나누지 않으면 gpucull 의 g_Instances(t0) 와 g_IndirectArgs(u0) 가 같은 SSBO 자리를 다툰다.
// ------------------------------------------------------------------------------
#define SW_GL_UAV_BINDING0             16

#endif // SW_ENGINE_BINDINGSLOTS_HLSLI
