/**
 * bindingslots.hlsli — 범용 바인딩 슬롯 번호 (C++ ShaderBindingSlots.h 와 수동 동기).
 *
 *  b0  space0 : PassCB   (엔진이 리플렉션으로 채움 — 셰이더가 필요한 필드만 선언)
 *  b1  space0 : MaterialCB (리플렉션 구동) — 네 백엔드 모두 실제 상수 버퍼다.
 *              Vulkan 은 세트 단위 바인딩이라 b0/b1 이 각각 별도 디스크립터 세트를 쓴다
 *              (common.hlsli 의 SW_VK_CB_SET_*, C++ VulkanRHIDevice::k*CbSetIndex 와 같은 값).
 *  b2..b15    : 자유 (범용 루트 시그니처 용량; 현재 엔진 바인더는 b0/b1 만 자동 채움)
 *  b0  space1 : g_BindlessCbIndex (루트 상수 — DX12 SM6.6 ResourceDescriptorHeap 경로)
 *  t0..t3     : 비네이티브 백엔드(DX11/GL) 엔진 텍스처 슬롯 (리플렉션 순서)
 *  t4         : GPUScene 인스턴스 구조버퍼 (비네이티브 백엔드)
 *  s0..s7     : 정적 샘플러 (아래 순서 = C++ shaderslot::StaticSampler)
 */

#ifndef SW_ENGINE_BINDINGSLOTS_HLSLI
#define SW_ENGINE_BINDINGSLOTS_HLSLI

#define SW_SLOT_PASS_CB      0
#define SW_SLOT_MATERIAL_CB  1
#define SW_SLOT_BINDLESS_CB  0   // space1

#define SW_MAX_CONSTANT_BUFFER 16
#define SW_FALLBACK_SRV_COUNT  4
#define SW_STATIC_SAMPLER_COUNT 8

// GPUScene 인스턴스 구조 버퍼 (per-instance world/material). 에뮬 백엔드(DX11/GL) t 레지스터 = 엔진 텍스처 슬롯 t0..t3 다음.
#define SW_SLOT_INSTANCE_SRV   4
// Vulkan 그래픽스 storage buffer descriptor set (파이프라인 레이아웃 set 6..9 = STORAGE_BUFFER).
#define SW_SPACE_INSTANCE_SRV  6

#define SW_SAMPLER_LINEAR_WRAP      0
#define SW_SAMPLER_LINEAR_CLAMP     1
#define SW_SAMPLER_POINT_WRAP       2
#define SW_SAMPLER_POINT_CLAMP      3
#define SW_SAMPLER_LINEAR_MIRROR    4
#define SW_SAMPLER_ANISO_WRAP       5
#define SW_SAMPLER_SHADOW_CMP       6
#define SW_SAMPLER_POINT_BORDER     7

#endif // SW_ENGINE_BINDINGSLOTS_HLSLI
