/**
 * bindingslots.hlsli — 셰이더 바인딩 계약의 **유일한 정본**.
 *
 * 이 파일은 순수 전처리기 정의만 담는다(#define 정수 + 주석). 그래서 HLSL 과 C++ 가 **같은 파일을
 * include** 한다 — C++ 쪽은 Source/Engine/Graphics/Shader/Binding/ShaderBindingSlots.h 가 이 파일을 include 해
 * 같은 값을 constexpr 로 노출하고, 4개 백엔드(DX11/DX12/Vulkan/GL)는 그 상수로만 바인딩한다.
 * ShaderBindingValidator::validate 가 쿠킹된 바이너리의 리플렉션을 이 표와 대조한다.
 *
 * 규칙: 이 파일에는 #define 과 주석만 둔다 (C++ 컴파일러가 그대로 읽는다). 산술식·함수형 매크로 금지. 값은 숫자 리터럴(정수 · `u` · `f` 접미)만.
 *
 * 번호 지도 (그래픽스 셰이더, space0 — 절 번호는 이 파일의 1~10 절):
 *   b0 PassCB · b1 MaterialCB(픽스처) · b2 루트 상수 에뮬(DX11/GL) | b0 space2 루트 상수(DX12 · Vulkan 푸시 상수) — 1 절
 *   t0..t3 엔진 텍스처(에뮬) · t4 인스턴스 · t5 머티리얼 데이터 · t6 가시 인스턴스 ID · t7 모프 정점 · t8 라이트 ·
 *   t9 배치 테이블 · t10 VAT · t11 패스 전용(캔버스 사각형 · 입자) · t12 라이트 그리드 · t13 라이트 프로브 | t14 빈 자리(띠 안) | t15 G버퍼 머티리얼(에뮬) |
 *   t16..t31 머티리얼 텍스처(에뮬 — DX11 · GL 만, 띠 밖) — 2 절
 *   컴퓨트는 따로: b0 · t0..t3 읽기 버퍼 · u0..u3 쓰기 버퍼 · u4..u7 RW 텍스처(에뮬) — 3 절
 *   s0..s6 정적 샘플러 · s7 그림자 비교(DX12 · Vulkan) | DX11 은 s0..s3 엔진 슬롯 결합 샘플러 + s9..s15 정적 세트 — 4 절
 *   t0 space1 / u0 space1 bindless 텍스처 배열(DX12), Vulkan 은 set 1 binding 0..3 — 5 절
 *   그 밖: Vulkan set 0 시프트(6) · GL SSBO · 이미지 유닛(7) · 라이트 타입(8) · 패스 플래그(9) · 버퍼 배치 상수(10).
 *   에뮬 슬롯(t0..t3 · t16..t31)이 bindless 를 어떻게 흉내 내는지는 binding.hlsli 0 절(bindless 입문).
 *
 * 모델 (언리얼 GPUScene 식): 셰이더 선언은 네 백엔드에서 **같다** — register(b#/t#/u#) 하나로 쓰고, 백엔드는
 * 그 번호를 각자의 방식으로 건다. 드로우마다 바뀌는 데이터는 바인딩을 갈아 끼우지 않고 **큰 버퍼에서 인덱스로 읽는다**
 * (인스턴스 g_SwInstances[t4], 머티리얼 g_SwMaterials[t5]) — 그래서 드로우 사이에 바뀌는 바인딩이 없고 슬롯 번호는
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
 *   │ 머티리얼 데이터    │ t5           │ t 테이블            │ SSBO binding 16+5   │ SSBO 5           │
 *   │ 머티리얼 텍스처    │ t16..t31(에뮬)│ (없음)             │ (없음)              │ 텍스처 유닛 16..31│
 *   │ 컴퓨트 읽기 버퍼   │ t0..t3       │ t 테이블            │ SSBO binding 16+#   │ SSBO #           │
 *   │ 컴퓨트 쓰기 버퍼   │ u0..u3       │ u 테이블            │ SSBO binding 32+#   │ SSBO 16+#        │
 *   │ bindless 텍스처    │ t0 space1    │ 테이블 (힙 전체)    │ set 1 binding 0     │ (없음)           │
 *   │ 정적 샘플러        │ s0           │ 정적 샘플러         │ set 1 binding 1     │ (결합 샘플러)    │  DX11: s9..s15 샘플러 상태(머티리얼 텍스처도 이것으로 읽는다)
 *   │ 루트 상수          │ b0 space2    │ 32비트 루트 상수    │ 푸시 상수           │ (UBO 에뮬)       │
 *   └────────────────────┴──────────────┴─────────────────────┴─────────────────────┴──────────────────┘
 *   - DX12: b# 는 루트 CBV(GPU 주소), t#/u# 슬롯은 디스크립터 테이블(오프라인 힙의 뷰를 드로우 직전 온라인 블록에 복사 — 언리얼
 *     FD3D12DescriptorCache), 텍스처 배열은 힙 시작을 가리키는 테이블. 루트 예산은 3*2 + 3*1 + 16 = 25/64 dword (ShaderBindingSlots.h dx12).
 *   - Vulkan: set 0 은 "슬롯 세트" — 레지스터 종류별 시프트(DXC -fvk-b/t/u-shift)로 binding 이 정해진다. 드로우/디스패치
 *     직전에 바인딩 상태가 바뀌었으면 세트를 새로 할당해 쓴다(언리얼 Vulkan RHI 와 같은 방식). set 1 은 텍스처 배열.
 *   - OpenGL: DescriptorSet 을 무시하고 binding 만 본다 — b# 가 곧 UBO #, t# 가 곧 텍스처 유닛/SSBO #, u# 는 SSBO 16+#.
 *   - DX11(SM5.0): space 를 모르고 리소스 배열 동적 인덱싱이 없다 — 텍스처는 고정 슬롯이다.
 *   - 머티리얼 텍스처 칸은 16 개다(언리얼 머티리얼 샘플러 한도와 같다 — 결정 기록 5-12). 네 백엔드가 같은 수를 받는다: DX12 · Vulkan 은 bindless 라
 *     칸이 머티리얼 데이터의 인덱스일 뿐이고, DX11 · GL 은 t16..t31 고정 슬롯이다. 그 슬롯은 Vulkan t 밴드(t0..t15) 전체의 **뒤**라, SRV 띠가
 *     밴드 끝까지 자라도 머티리얼 슬롯은 움직이지 않는다(결정 기록 5-12 D2-13 — 라이트 그리드 t12 를 넣으며 띠와 머티리얼 슬롯을 갈랐다).
 *     DX11 은 샘플러 슬롯이 16 개뿐이라 머티리얼 슬롯에 샘플러를 짝으로 두지 않고 정적 세트의 머티리얼 샘플러(s9 + 0)로 읽는다.
 *     GL 은 텍스처 유닛 16..31 의 결합 샘플러라 프래그먼트 텍스처 유닛이 32 개 이상이어야 한다(디바이스 초기화가 확인한다 — 데스크톱 드라이버는 32).
 */

#ifndef SW_ENGINE_BINDINGSLOTS_HLSLI
#define SW_ENGINE_BINDINGSLOTS_HLSLI

// ------------------------------------------------------------------------------
// 1) 상수버퍼 (b#, space0)
// ------------------------------------------------------------------------------
#define SW_SLOT_PASS_CB     0
#define SW_SLOT_MATERIAL_CB 1
#define SW_SLOT_COMPUTE_CB  0
#define SW_CB_SLOT_COUNT    3 // b0..b2 — b2 는 DX11/GL 루트 상수 에뮬 자리(SW_SLOT_ROOT_CB_EMUL)

// 루트/푸시 상수 — DX12 b0 space2 (32비트 루트 상수), Vulkan 푸시 상수, DX11/GL 은 UBO 에뮬. setComputeRootConstants 전용.
#define SW_SLOT_ROOT_CB      0
#define SW_SPACE_ROOT_CB     2
#define SW_ROOT_DWORD_COUNT  16
#define SW_SLOT_ROOT_CB_EMUL 2 // DX11/GL: 루트 상수를 담는 상수버퍼 슬롯 (SW_DECLARE_ROOT_CONSTANTS)
// ------------------------------------------------------------------------------
// 2) SRV (t#, space0). t0..t12 — 엔진 텍스처 슬롯(에뮬)·인스턴스·머티리얼 데이터·씬 표들·패스 전용 버퍼·라이트 그리드(네 백엔드의 띠).
//    띠는 Vulkan t 밴드(t0..t15)까지 자랄 수 있다. t16..t31 — 머티리얼 텍스처(에뮬, DX11 · GL 만). 밴드 뒤라 DX12 테이블 · Vulkan t 밴드에 들지 않는다.
// ------------------------------------------------------------------------------
#define SW_SLOT_ENGINE_TEX0          0
#define SW_SLOT_ENGINE_TEX1          1
#define SW_SLOT_ENGINE_TEX2          2
#define SW_SLOT_ENGINE_TEX3          3
#define SW_ENGINE_TEXTURE_SLOT_COUNT 4

// GPUScene 인스턴스 구조버퍼 (per-instance world/material). 엔진 텍스처 슬롯 바로 다음. 네 백엔드 공통.
#define SW_SLOT_INSTANCE_SRV 4

// GPUScene 머티리얼 데이터 구조버퍼 (StructuredBuffer<MaterialData> g_SwMaterials) — 인스턴스의 materialIndex 로 읽는다.
// 머티리얼 셰이더 타입마다 버퍼 하나(원소 = 그 셰이더의 머티리얼 구조체). 네 백엔드 공통.
#define SW_SLOT_MATERIAL_BUFFER 5

// GPU 컬링이 만든 **가시 인스턴스 ID 목록** (StructuredBuffer<uint> g_SwVisibleInstanceIDs).
// 정점 셰이더가 g_SwVisibleInstanceIDs[인스턴스 슬롯] 으로 자기 인스턴스 번호를 찾는다(슬롯은 입력 어셈블러가 주는 SW_INSTANCESLOT —
// 간접 인자의 startInstance + 서수) — 언리얼 FInstanceCullingContext 의 InstanceIdBuffer 와 같은 자리. 안 걸려 있으면(kInvalidIndex)
// 슬롯이 곧 인스턴스 번호다(컬링 없음 경로).
#define SW_SLOT_VISIBLE_INSTANCE_SRV 6

// GPU 가 변형한 정점 (StructuredBuffer<float4> g_SwMorphVertices — 정점당 float4 셋). 정점 셰이더가
// g_SwMorphVertices[(배치.morphVertexBase + 로컬 정점 번호) * 2] 로 **입력 스트림 대신** 위치를 읽는다 —
// 언리얼 GPU Skin Cache 가 컴퓨트 결과를 정점 스트림으로 물리는 자리와 같은 목적이고, 방법만
// 정점 풀링이다(이 엔진은 인스턴스·머티리얼·가시 목록이 이미 전부 구조버퍼라 그 결이 맞고,
// DX11 의 "구조버퍼는 정점 버퍼가 될 수 없다" 제약과 DX12 의 UPLOAD 힙 제약을 아예 비켜 간다).
// 안 걸려 있으면(kInvalidIndex) 입력 스트림을 그대로 쓴다.
#define SW_SLOT_MORPH_VERTEX_SRV 7

// 씬의 라이트 목록 (StructuredBuffer<LightData> g_SwLights). 방향광·점광이 한 버퍼에 섞여 들어가고,
// 포워드와 디퍼드가 **같은 버퍼를 같은 루프로** 읽는다 — 조명 식이 두 벌이면 둘은 반드시 갈라진다.
// 인스턴스·머티리얼과 같은 이유로 상수버퍼가 아니라 구조버퍼다: 개수가 씬마다 다르고, 패스당 한 번
// 걸면 드로우 사이에 바인딩이 바뀌지 않는다(이 엔진의 규약). 언리얼의 라이트 데이터 버퍼와 같은 자리.
#define SW_SLOT_LIGHT_SRV 8

// 씬 배치 표 (StructuredBuffer<BatchData> g_SwBatches). 배치마다 인스턴스 시작·모프 풀 시작·정점 풀 시작.
// 정점 셰이더가 **자기 배치 번호**로 읽는다 — DX12 는 커맨드 시그니처가 루트 상수로 주입, Vulkan·GL 은 DrawIndex 내장
// 변수 + 그룹 첫 번호, DX11 은 배치마다 루트 상수. 배치마다 바뀌는 값이 전부 이 표에 있으므로 같은 PSO·머티리얼의
// 배치들을 멀티 드로우 하나로 낼 수 있다(언리얼 GPUScene 의 프리미티브/인스턴스 데이터 버퍼와 같은 자리).
// 컬링 컴퓨트도 같은 버퍼를 t1 로 읽는다 — gpucull.hlsl 의 GPUBatchInfo 와 레이아웃이 같다.
#define SW_SLOT_BATCH_SRV 9

// 정점 애니메이션(VAT) 표 (StructuredBuffer<float4> g_SwVertexAnimation). 먼 군중의 메시마다 머리 원소 + 프레임 × 정점.
// 정점 셰이더가 배치 표의 vertexAnimationBase 와 인스턴스의 시각 오프셋으로 읽는다(binding.hlsli InstanceData::loadAnimatedVertex).
#define SW_SLOT_VERTEX_ANIMATION_SRV 10

// 패스 전용 구조버퍼 — 한 그리기가 자기 원소를 (루트 상수의 시작 + SV_InstanceID) 번째로 읽고 SV_VertexID 로 모서리를 만든다(동적 정점 버퍼 없음).
// 패스마다 거는 것이 다르다: 캔버스 패스는 사각형(StructuredBuffer<CanvasQuad> g_SwCanvasQuads, canvas.hlsl),
// 투명 패스의 입자 그리기는 입자(StructuredBuffer<GPUParticle> g_SwParticles, particle.hlsl). 두 그리기는 같은 패스에서 겹치지 않는다.
#define SW_SLOT_PASS_SRV 11

// 지금 뷰의 라이트 그리드 (StructuredBuffer<uint> g_SwLightGrid) — 클러스터드 라이팅의 빛 목록(배치는 10 절 SW_LIGHT_GRID_*).
// 뷰마다 그리드 하나가 이 버퍼에 이어 붙고, PassCB g_SwLightGridBase 가 지금 뷰의 첫 단어다. 만드는 쪽은 둘이다 — CPU(LightGrid, 동적 버퍼)와
// GPU 컴퓨트(lightgrid.hlsl, 기본 힙 UAV). 셰이더는 어느 쪽이 만들었는지 모른다(lighting.hlsli LightGrid::loadWord 하나만 읽는다).
#define SW_SLOT_LIGHT_GRID_SRV 12

// 라이트 프로브의 SH9 (StructuredBuffer<float4> g_SwLightProbes) — 프로브 하나가 float4 SW_LIGHT_PROBE_FLOAT4_COUNT 개(C++ SH9Color::makeGPUElement 배치).
// 볼륨은 라이트 목록(g_SwLights)의 SW_LIGHT_TYPE_PROBE_VOLUME 원소이고 그 원소가 이 버퍼의 첫 프로브를 가리킨다. 읽는 함수는 lighting.hlsli LightProbe::load 하나다.
#define SW_SLOT_LIGHT_PROBE_SRV 13

#define SW_SRV_SLOT_COUNT 14 // t0..t13 — DX12 t 테이블 크기. Vulkan set 0 의 t 밴드 폭(16)까지 늘릴 수 있다 — 머티리얼 슬롯은 밴드 뒤라 밀리지 않는다

// G버퍼 머티리얼 칸(첨부 GBufferMaterial — r 금속도, g 거칠기)의 엔진 텍스처 슬롯(에뮬 — DX11 · GL 만). 디퍼드 조명 패스는 그림자 · 알베도 · 노멀 · 깊이와 함께
// 이것을 다섯째로 읽어 t0..t3 에 자리가 없다. t4 부터는 네 백엔드 공통 띠라 띠의 꼭대기(t15)에 둔다 — 띠는 아래부터 자라고(지금 t13 까지), 이 칸에 닿으면
// ShaderBindingSlots.h 가 컴파일에서 막는다. DX12 · Vulkan 은 bindless 라 이 슬롯을 선언하지 않는다.
// DX11 은 s15 가 정적 샘플러 세트(SW_DX11_STATIC_SAMPLER6)라 짝 샘플러를 두지 않고 셰이더가 정적 세트의 엔진 텍스처 샘플러로 읽는다. GL 은 유닛 15 의 결합 샘플러에 엔진이 엔진 텍스처 샘플러를 건다.
#define SW_SLOT_ENGINE_GBUFFER_MATERIAL 15

// 머티리얼 텍스처 고정 슬롯 t16..t31(16 칸). DX11/GL 은 bindless 가 없어 머티리얼이 준 전역 인덱스를 풀 수 없으므로
// 엔진이 텍스처를 이 슬롯에 걸고 머티리얼 데이터에는 **서수(0..N-1)** 를 넣는다. DX12/Vulkan 은 전역 인덱스라 이 슬롯을 선언하지 않는다.
// 첫 칸은 Vulkan t 밴드 폭(SW_VK_SLOT_RANGE_SIZE)과 같다 — SRV 띠가 밴드 끝까지 자라도 겹치지 않는다(ShaderBindingSlots.h 가 확인한다).
// 레지스터는 리터럴이어야 해서(register( SW_CAT( t, slot ) )) 칸마다 번호를 적는다 — 연속인지는 ShaderBindingSlots.h 가 확인한다.
#define SW_SLOT_MATERIAL_TEX0          16
#define SW_SLOT_MATERIAL_TEX1          17
#define SW_SLOT_MATERIAL_TEX2          18
#define SW_SLOT_MATERIAL_TEX3          19
#define SW_SLOT_MATERIAL_TEX4          20
#define SW_SLOT_MATERIAL_TEX5          21
#define SW_SLOT_MATERIAL_TEX6          22
#define SW_SLOT_MATERIAL_TEX7          23
#define SW_SLOT_MATERIAL_TEX8          24
#define SW_SLOT_MATERIAL_TEX9          25
#define SW_SLOT_MATERIAL_TEX10         26
#define SW_SLOT_MATERIAL_TEX11         27
#define SW_SLOT_MATERIAL_TEX12         28
#define SW_SLOT_MATERIAL_TEX13         29
#define SW_SLOT_MATERIAL_TEX14         30
#define SW_SLOT_MATERIAL_TEX15         31
#define SW_MATERIAL_TEXTURE_SLOT_COUNT 16

// 캔버스(UI) 배치 하나가 쓰는 텍스처 수 — 머티리얼 텍스처 슬롯의 앞 넷을 빌려 쓴다(에뮬 백엔드). 루트 상수 16 dword 안에 들어야 해서
// 머티리얼 칸 수와 따로 둔다(canvas.hlsl 의 SwRootConstants: 시작 · 텍스처 넷 · 대상 너비 · 높이 · 색각 보정).
#define SW_CANVAS_TEXTURE_COUNT 4

// ------------------------------------------------------------------------------
// 3) 컴퓨트 — CB 는 b0, 읽기 버퍼 t0..t3, 쓰기 버퍼 u0..u3 (space0)
// ------------------------------------------------------------------------------
#define SW_COMPUTE_SRV_SLOT_COUNT 4
#define SW_COMPUTE_UAV_SLOT_COUNT 4

// 컴퓨트 RW 텍스처 — DX11/GL 은 고정 슬롯 u4..u7(g_SwRWSlot#), DX12/Vulkan 은 무제한 배열(g_SwBindlessRWTex2D, 아래 5)을
// 인덱스로 고른다. 셰이더는 Bindless::storeRwTexture2D( index, texelPosition, value ) 로만 쓴다 — 에뮬 백엔드에서 index 는 슬롯 서수(0..3).
#define SW_SLOT_COMPUTE_TEXUAV0      4
#define SW_SLOT_COMPUTE_TEXUAV1      5
#define SW_SLOT_COMPUTE_TEXUAV2      6
#define SW_SLOT_COMPUTE_TEXUAV3      7
#define SW_COMPUTE_TEXUAV_SLOT_COUNT 4

// ------------------------------------------------------------------------------
// 4) 정적 샘플러 (s#, space0)
// ------------------------------------------------------------------------------
//    DX12: 루트 시그니처 정적 샘플러 s0..s7. Vulkan: set 1 binding SW_VK_SAMPLER_BINDING 의 immutable sampler 배열(0..6) +
//    binding SW_VK_SHADOW_SAMPLER_BINDING 의 비교 샘플러. 셰이더는 g_SwSamplers[SW_SAMPLER_*] / g_SwSamplerShadowCmp 로 쓴다.
//    비교 샘플러는 HLSL 타입이 달라(SamplerComparisonState) 배열에 못 들어가므로 마지막 번호다.
#define SW_STATIC_SAMPLER_COUNT       8
#define SW_STATIC_SAMPLER_ARRAY_COUNT 7 // g_SwSamplers[0..6] — 비교 샘플러 제외

#define SW_SAMPLER_LINEAR_WRAP   0
#define SW_SAMPLER_LINEAR_CLAMP  1
#define SW_SAMPLER_POINT_WRAP    2
#define SW_SAMPLER_POINT_CLAMP   3
#define SW_SAMPLER_LINEAR_MIRROR 4
#define SW_SAMPLER_ANISO_WRAP    5
#define SW_SAMPLER_POINT_BORDER  6
#define SW_SAMPLER_SHADOW_CMP    7

// 슬롯 종류마다 읽는 샘플러(위 SW_SAMPLER_* 번호) — 네 백엔드가 같은 값을 쓴다. 네이티브 bindless(DX12/Vulkan)는 셰이더가 이 번호로
// 고르고, 슬롯 결합 샘플러(DX11/GL)는 엔진이 그 슬롯에 같은 샘플러를 건다.
//   엔진 텍스처 슬롯 t0..t3 (풀스크린 입력 · G버퍼 · 깊이 · 그림자 맵): 선형 · 클램프(1) — 화면 가장자리의 탭이 반대편을 읽지 않는다.
//   머티리얼 텍스처 t16..t31: 선형 · 랩(0). DX11 은 슬롯에 걸지 않고 셰이더가 정적 세트의 이 번호로 읽는다(샘플러 슬롯이 16 개뿐이다).
#define SW_ENGINE_TEXTURE_SAMPLER   1
#define SW_MATERIAL_TEXTURE_SAMPLER 0

// DX11(SM5.0) 정적 샘플러 세트 자리 s9..s15 — 엔진 슬롯 결합 샘플러(s0..s3, t# 와 같은 번호)와 겹치지 않는다. 엔진이 디바이스
// 초기화 때 한 번 걸어 두고 셰이더는 Bindless::sampleIndexWith 의 samplerId 로 고른다(SM5.0 은 샘플러 배열 동적 인덱싱이 없어 리터럴 분기).
// 언리얼 D3D11 RHI 는 슬롯마다 엔진이 고른 샘플러를 걸 뿐 셰이더가 고르는 세트가 없다 — 여기서는 DX12/Vulkan 과 같은
// SW_SAMPLER_* 를 DX11 도 존중하게 한 것이다. GL 은 결합 샘플러뿐(ARB_gl_spirv 는 분리 샘플러를 못 쓴다)이라 세트가 없다.
#define SW_DX11_STATIC_SAMPLER0        9
#define SW_DX11_STATIC_SAMPLER1        10
#define SW_DX11_STATIC_SAMPLER2        11
#define SW_DX11_STATIC_SAMPLER3        12
#define SW_DX11_STATIC_SAMPLER4        13
#define SW_DX11_STATIC_SAMPLER5        14
#define SW_DX11_STATIC_SAMPLER6        15
#define SW_DX11_MAX_SAMPLER_SLOT_COUNT 16 // D3D11_COMMONSHADER_SAMPLER_SLOT_COUNT

// ------------------------------------------------------------------------------
// 5) 네이티브 bindless 텍스처 배열 (DX12/Vulkan) — 슬롯 리소스와 다른 자리에 둔다.
//    DX12: Texture2D g_SwBindlessTex2D[] : register(t0, space1) 와 RWTexture2D g_SwBindlessRWTex2D[] : register(u0, space1)
//          — 루트 시그니처의 유일한 테이블(범위 둘, 둘 다 힙 시작에서 무제한).
//    Vulkan: set 1 binding 0 (COMBINED_IMAGE_SAMPLER[]), binding 1 = immutable sampler 배열, binding 2 = 비교 샘플러,
//            binding 3 = STORAGE_IMAGE[] (RW 텍스처).
// ------------------------------------------------------------------------------
#define SW_SPACE_BINDLESS_TEX        1
#define SW_VK_TEXTURE_SET            1
#define SW_VK_TEXTURE_BINDING        0
#define SW_VK_SAMPLER_BINDING        1
#define SW_VK_SHADOW_SAMPLER_BINDING 2
#define SW_VK_RWTEXTURE_BINDING      3
// 배열의 칸 수 — 모두 2 의 거듭제곱이다. 셰이더는 첨자를 (칸 수 - 1) 로 마스크해 늘 배열 안을 가리킨다(binding.hlsli Bindless::textureSlot).
// DX12 는 셰이더 가시 힙 하나(CBV · SRV · UAV 공용)라 텍스처 · RW 텍스처가 같은 칸 수를 쓴다.
#define SW_DX12_DESCRIPTOR_HEAP_SIZE       524288
#define SW_VK_BINDLESS_TEXTURE_COUNT       4096
#define SW_VK_BINDLESS_STORAGE_IMAGE_COUNT 1024

// ------------------------------------------------------------------------------
// 6) Vulkan 슬롯 세트(set 0) — binding = 레지스터 종류별 시프트 + 번호. DXC 에 -fvk-b-shift/-fvk-t-shift/-fvk-u-shift 로
//    넘기고(ShaderCompiler.cpp), 파이프라인 레이아웃(VulkanRHIDeviceDescriptor.cpp)이 같은 번호에 바인딩을 만든다.
//    b0..b15 → 0..15 (UBO), t0..t15 → 16..31 (SSBO), u0..u15 → 32..47 (SSBO).
// ------------------------------------------------------------------------------
#define SW_VK_B_SHIFT            0
#define SW_VK_T_SHIFT            16
#define SW_VK_U_SHIFT            32
#define SW_VK_SLOT_RANGE_SIZE    16
#define SW_VK_SLOT_BINDING_COUNT 48

// ------------------------------------------------------------------------------
// 7) OpenGL SSBO 번호 — t# 는 binding #, u# 는 binding SW_GL_UAV_BINDING0 + # (DXC -fvk-u-shift 가 이 값이다).
//    둘을 나누지 않으면 gpucull 의 g_Instances(t0) 와 g_IndirectArgs(u0) 가 같은 SSBO 자리를 다툰다.
// ------------------------------------------------------------------------------
#define SW_GL_UAV_BINDING0 16
// OpenGL 이미지 유닛 — 컴퓨트 RW 텍스처 u4..u7 → 이미지 유닛 0..3 (SSBO 와 다른 이름공간, common.hlsli 가 명시 binding 으로 적는다).
#define SW_GL_IMAGE_UNIT0 0
#define SW_GL_IMAGE_UNIT1 1
#define SW_GL_IMAGE_UNIT2 2
#define SW_GL_IMAGE_UNIT3 3

// ------------------------------------------------------------------------------
// 8) 라이트 타입 — g_SwLights 원소의 `directionType.w` 에 들어가는 값.
//    슬롯 번호는 아니지만 **C++ 와 셰이더가 같아야 하는 숫자**라 정본을 여기 둔다(이 파일이 양쪽에서 include 된다).
//    값을 두 군데 두면 언젠가 갈라진다 — 그때 증상은 "빛 하나가 조용히 엉뚱하게 계산된다" 다.
// ------------------------------------------------------------------------------
#define SW_LIGHT_TYPE_DIRECTIONAL 0
#define SW_LIGHT_TYPE_POINT       1
#define SW_LIGHT_TYPE_SPOT        2
// 2D 빛 — 빛 받는 스프라이트(sprite2dlit.hlsl)만 읽는다. 3D 조명 식(Lighting::shadeLights)은 건너뛴다.
#define SW_LIGHT_TYPE_POINT2D    3 // 점 · 스폿(원뿔) 2D 빛 — 안 · 바깥 반경, 감쇠 지수, 노멀 맵 높이
#define SW_LIGHT_TYPE_GLOBAL2D   4 // 전역 2D 빛 — 빛 받는 스프라이트의 바탕 밝기
#define SW_LIGHT_TYPE_FREEFORM2D 5 // 자유 모양 2D 빛 — 다각형 안은 1, 밖은 감쇠 거리까지 줄어든다. 변은 바로 뒤의 FREEFORM2D_EDGE 원소들이다
#define SW_LIGHT_TYPE_COUNT      6
// 빛이 아니라 자유 모양 2D 빛의 변 하나(그 빛 원소 바로 뒤에 extra.z 개가 붙는다). 등록부의 빛 종류가 아니라 COUNT 밖이다.
#define SW_LIGHT_TYPE_FREEFORM2D_EDGE 12
// 자유 모양 2D 빛 하나의 변 수 상한 — 셰이더 루프의 끝이다. C++ FreeformLight2DComponent::setPoints 가 넘는 다각형을 받지 않는다.
#define SW_LIGHT2D_MAX_FREEFORM_EDGES 64
// 2D 빛의 섞는 방식(extra.y) — 유니티 Light 2D 의 Blend Style 자리. 더하기 · 빼기를 모두 더한 뒤 곱하기를 곱하므로 빛의 순서와 무관하다.
#define SW_LIGHT2D_BLEND_ADDITIVE    0
#define SW_LIGHT2D_BLEND_MULTIPLY    1
#define SW_LIGHT2D_BLEND_SUBTRACTIVE 2
// 2D 빛 쿠키 아틀라스(패스 역할 텍스처 `Light2DCookieAtlas`, PassCB g_Light2DCookieAtlasIndex) — 한 변 SW_LIGHT2D_COOKIE_ATLAS_SIZE 텍셀, 한 줄에 SW_LIGHT2D_COOKIE_CELLS_PER_ROW 칸.
// 빛 원소의 extra.w 가 칸 번호이고 SW_LIGHT2D_NO_COOKIE_CELL 이면 쿠키가 없다. C++ Light2DCookieAtlas 가 칸을 나눠 그린다.
#define SW_LIGHT2D_COOKIE_ATLAS_SIZE    1024
#define SW_LIGHT2D_COOKIE_CELLS_PER_ROW 4
#define SW_LIGHT2D_NO_COOKIE_CELL       -1
// 부드러운 2D 그림자의 표본 수 — 빛 원천(반경 = extra.x)을 가로지르는 점마다 가림막을 한 번 더 본다.
#define SW_LIGHT2D_SOFT_SHADOW_SAMPLES 5
// 빛이 아니라 2D 그림자 가림막 토막 하나(빛 목록 뒤에 붙는다). 등록부의 빛 종류가 아니라 COUNT 밖이다.
#define SW_LIGHT_TYPE_SHADOW2D 15
// 빛이 아니라 라이트 프로브 볼륨 하나(IndirectLighting.md P3). 3D 라이트 뒤, 2D 그림자 토막 앞에 붙고 라이트 그리드가 경계 구로 클러스터에 넣는다.
// 원소: positionRadius = (상자 중심 xyz, 상자 대각선 반) · colorIntensity = (첫 프로브 위치 xyz, 0) · directionType = (축마다 간격 xyz, 이 타입) ·
//       params = (축마다 프로브 수 xyz, g_SwLightProbes 의 첫 프로브 번호) — 수와 번호는 정확한 정수 실수(2^24 안)다.
#define SW_LIGHT_TYPE_PROBE_VOLUME 14
// 프로브 하나의 g_SwLightProbes 원소 수(SH9 RGB 를 조도 / π 로 접은 float4 일곱 개).
#define SW_LIGHT_PROBE_FLOAT4_COUNT 7u
// 빛이 아니라 반사 프로브 하나(IndirectLighting.md P4). 프로브 볼륨처럼 경계 구로 그리드에 들고, 옥타헤드럴 텍셀(선형 RGB, 텍셀 하나가 float4 하나)은
// g_SwLightProbes(t13)에 SH9 원소와 함께 이어 붙는다 — 텍스처 슬롯을 쓰지 않아 네 백엔드가 같은 길로 읽는다. 읽는 함수는 lighting.hlsli LightData::sampleReflectionProbe 하나다.
// 원소: positionRadius = (상자 중심 xyz, 상자 대각선 반) · colorIntensity = (상자 반 크기 xyz, 경계에서 섞는 폭 m) · directionType = (찍은 위치 xyz, 이 타입) ·
//       params = (옥타헤드럴 한 변 텍셀 수, 거칠기 단계 수, g_SwLightProbes 의 첫 원소 번호, 0) — 수와 번호는 정확한 정수 실수(2^24 안)다.
//       텍셀 순서는 단계 → 행 → 열(단계 l 의 (x, y) 는 첫 원소 + (l × 변 + y) × 변 + x). 단계 l 의 거칠기는 l / (단계 수 − 1) 이다.
#define SW_LIGHT_TYPE_REFLECTION_PROBE 13
// 라이트 그리드가 경계 구로 클러스터에 넣는 타입의 비트(1 << 타입) — 점광 1 · 스폿 2 · 반사 프로브 13 · 프로브 볼륨 14. 생산자 둘(C++ LightGrid · lightgrid.hlsl)이
// 이 하나를 본다(타입을 더할 때 두 곳을 고치지 않는다). 값이 위 타입과 맞는지는 ShaderBindingSlots.h 가 확인한다.
#define SW_LIGHT_GRID_LOCAL_TYPE_MASK 0x6006u

// 한 프레임에 GPU 로 보내는 라이트 수 상한(2D 그림자 토막 포함). 넘으면 엔진이 잘라 보내고 경고한다 —
// 셰이더 루프가 이 수를 넘게 도는 일은 없다(g_SwLightCount 가 이 값으로 클램프된다).
#define SW_MAX_FRAME_LIGHT 1024

// ------------------------------------------------------------------------------
// 9) 패스 플래그 — PassCB 의 `g_Flags` 비트. 뷰마다 다른 값이라(다중 뷰: CCTV 는 후처리를 끈다) 셰이더 퍼뮤테이션이 아니라 상수로 싣는다.
//    C++(FrameRenderer::updatePassConstants)와 셰이더가 같은 비트를 본다.
// ------------------------------------------------------------------------------
#define SW_PASS_FLAG_NATIVE_BINDLESS 1 // 네이티브 bindless(DX12 · Vulkan) — 텍스처 배열을 인덱스로 읽는다
#define SW_PASS_FLAG_SKIP_POST       2 // 이 뷰는 후처리를 끈다 — postchain 이 블룸 · 외곽선 · 톤맵을 건너뛰고 원본을 낸다
#define SW_PASS_FLAG_SKIP_TONEMAP    4 // 이 뷰는 표면 값을 보인다(보기 모드 Normals · Depth · Overdraw) — 톤맵 패스가 색을 바꾸지 않는다
#define SW_PASS_FLAG_POINT_UPSCALE   8 // 이 뷰는 정수 업스케일(픽셀 퍼펙트)이다 — Present 블릿이 원본을 점 샘플로 읽어 픽셀을 섞지 않는다

// ------------------------------------------------------------------------------
// 10) 버퍼 배치 · 식 상수 — 슬롯 번호는 아니지만 C++ 와 셰이더가 같아야 하는 수. 정본은 여기 하나다(이 파일이 양쪽에서 include 된다).
//     숫자 리터럴만 둔다(정수 · `u` 접미) — C++ 컴파일러도 읽는다.
// ------------------------------------------------------------------------------
// 모프 결과 · 레스트 버퍼 — 정점 하나 = float4 셋([3i] 위치, [3i+1] 노멀, [3i+2] 탄젠트 xyz · w 바이탄젠트 부호). 레스트 버퍼 뒤쪽(g_SkinDeltaBase 부터)은 모프 차이 하나 = float4 셋
// ([0] 위치 차이 + w 에 타깃 번호, [1] 노멀 차이, [2] 비어 있음 — 탄젠트 차이는 없고 컴퓨트가 변형된 노멀에 다시 직교화한다). 구조체가 아니라 평면 배열인 이유는 binding.hlsli 의 g_SwMorphVertices 주석(GL 이 멤버를 옆 원소에서 읽는다).
#define SW_MORPH_FLOAT4_PER_VERTEX 3u
// 스킨 가중치 버퍼 — 원본 정점 하나 = float4 셋([3i] 가중치 넷, [3i+1] 원본 스켈레톤 본 번호 넷, [3i+2] 모프 차이 구간(시작 · 수)).
#define SW_SKIN_FLOAT4_PER_VERTEX 3u
// 스킨 팔레트 — 본 하나 = float4 셋(행벡터 규약 4x4 행렬의 0 · 1 · 2 열). 위치 = dot( float4( p, 1 ), 열 ). 본 행 뒤에 모프 가중치가 float4 로 이어진다.
#define SW_SKIN_FLOAT4_PER_BONE 3u
// 스킨 인스턴스 표 — 인스턴스 하나 = uint4 둘([2i] 결과 시작 · 원본 시작 · 정점 수 · 팔레트 시작, [2i+1] 모프 가중치 시작 · 타깃 수).
#define SW_SKIN_UINT4_PER_INSTANCE 2u
// 정점 애니메이션(VAT) 노멀 — 팔면체 한 칸의 해상도(12 비트). 굽는 쪽(MeshVertexAnimation)과 읽는 쪽(binding.hlsli)이 같아야 한다.
#define SW_VERTEX_ANIMATION_NORMAL_STEPS 4096u
// 거스트너 파도 칸 수. 부력 · 수면 질의(WaterWaveMath)가 CPU 에서 같은 식을 다시 계산한다(RenderPassGPUTest.WaterWaveShaderMatchesCPU).
#define SW_GERSTNER_WAVE_COUNT 4
// 방향광 캐스케이드 상한 — PassCB 의 g_ShadowCascade0..3 넷이다. 그림자 아틀라스의 네 사분면에 하나씩 든다(DirectionalShadowProjection).
#define SW_SHADOW_CASCADE_MAX 4u
// 점광 · 스폿 그림자 타일 하나가 라이트 버퍼에서 차지하는 원소 수 — [0] 아틀라스 자리를 접은 뷰-투영(행 넷), [1] 칸 사각형 · 바이어스.
// 타일 원소는 g_SwLights 의 빛 뒤(g_SwLightCount 부터)에 붙고, 빛 원소의 `params.w` 가 첫 타일의 상대 번호다. 점광은 면 여섯(+X −X +Y −Y +Z −Z 순)이다.
#define SW_SHADOW_TILE_ELEMENT_COUNT 2u
#define SW_POINT_SHADOW_FACE_COUNT   6u
// 라이트 그리드(클러스터드 라이팅, lighting.hlsli LightCursor::begin · C++ LightGrid · 컴퓨트 lightgrid.hlsl) — 32 비트 단어의 배열이다(g_SwLightGrid, t12).
// 그리드는 뷰마다 하나씩 그 버퍼에 이어 붙고, PassCB g_SwLightGridBase 가 지금 뷰의 첫 단어다.
// 배치: 헤더 단어 16 개 → 클러스터마다 단어 두 개(목록 안의 시작, 개수) → 라이트 번호 목록(전역 구간이 맨 앞). 클러스터 번호는 (조각 × Y + y) × X + x 다.
#define SW_LIGHT_GRID_HEADER_WORD_COUNT 16u
#define SW_LIGHT_GRID_WORDS_PER_CLUSTER 2u
// 클러스터 칸 수 — 뷰의 NDC 를 X · Y 칸으로, 깊이를 조각으로 나눈다(결정 기록 5-12 D2-13 의 시작값).
// 컴퓨트 생산자는 클러스터마다의 개수를 그룹 공유 메모리에 두므로 칸 수 × 4 바이트가 그 한도(32KB) 안이어야 한다(ShaderBindingSlots.h 가 확인한다).
#define SW_LIGHT_GRID_DIM_X 16u
#define SW_LIGHT_GRID_DIM_Y 9u
#define SW_LIGHT_GRID_DIM_Z 24u
// GPU 생산자의 뷰 하나에 담는 라이트 번호 목록의 상한(항목 수). 버퍼를 프레임 전에 잡아야 해서(GPU 가 센 수를 CPU 가 모른다) 정한다.
// 넘친 항목은 버린다 — 넘친 클러스터부터 뒤는 개수가 잘린다. CPU 생산자는 상한이 없다(실제 수만큼 잡는다).
#define SW_LIGHT_GRID_GPU_MAX_ENTRY_COUNT 131072u
// GPU 생산자의 스레드 그룹 크기 — 뷰 하나가 그룹 하나다(라이트 상한 SW_MAX_FRAME_LIGHT 를 그룹 하나가 돈다).
#define SW_LIGHT_GRID_GPU_THREAD_COUNT 256u
// 라이트 범위의 여유 — CPU 와 GPU 생산자, 읽는 셰이더의 부동소수 차이로 경계의 픽셀이 이웃 클러스터에 떨어져도 라이트가 빠지지 않게 한다.
// 모서리 클립 w 가 SW_LIGHT_GRID_MIN_CORNER_CLIP_W 보다 작으면 경계 구가 카메라 면을 넘은 것이라 XY 범위를 화면 전체로 둔다.
#define SW_LIGHT_GRID_NDC_PAD           0.001f
#define SW_LIGHT_GRID_SLICE_PAD         0.01f
#define SW_LIGHT_GRID_MIN_CORNER_CLIP_W 0.0001f
// 헤더 단어의 번호. 실수 값은 비트 그대로 담는다(셰이더는 asfloat 로 읽는다).
#define SW_LIGHT_GRID_WORD_DIM_X        0u // NDC x 방향 칸 수
#define SW_LIGHT_GRID_WORD_DIM_Y        1u // NDC y 방향 칸 수
#define SW_LIGHT_GRID_WORD_DIM_Z        2u // 깊이 조각 수
#define SW_LIGHT_GRID_WORD_PERSPECTIVE  3u // 1 = 원근(조각은 클립 w 의 로그), 0 = 직교(조각은 NDC z 의 선형)
#define SW_LIGHT_GRID_WORD_NEAR_DEPTH   4u // 실수 — 원근의 가까운 면 클립 w
#define SW_LIGHT_GRID_WORD_SLICE_SCALE  5u // 실수 — 원근은 조각 수 / log2(먼 면 w / 가까운 면 w), 직교는 조각 수
#define SW_LIGHT_GRID_WORD_GLOBAL_COUNT 6u // 전역 구간(방향광)의 라이트 수
#define SW_LIGHT_GRID_WORD_CLUSTER_BASE 7u // 클러스터 단어의 시작(그리드 첫 단어 기준)
#define SW_LIGHT_GRID_WORD_INDEX_BASE   8u // 라이트 번호 목록의 시작(그리드 첫 단어 기준)
#define SW_LIGHT_GRID_WORD_ENTRY_COUNT  9u // 목록이 담아야 했던 항목 수(전역 구간 포함). GPU 생산자에서 SW_LIGHT_GRID_GPU_MAX_ENTRY_COUNT 를 넘으면 넘친 것이다
// GPU 생산자의 뷰 정보(StructuredBuffer<float4> g_LightGridViews) — 뷰 하나가 float4 여섯 개다.
// [0..3] 뷰-투영 행 넷(행벡터 규약, 셰이더의 g_ViewProj 와 같다) · [4] (가까운 면 w, 조각 배율, 원근 1/직교 0, 0) · [5] asuint (라이트 수, 그리드 첫 단어, 0, 0).
#define SW_LIGHT_GRID_VIEW_FLOAT4_COUNT 6u

// ------------------------------------------------------------------------------
// 11) 볼륨 안개 — 주 시점의 froxel 격자(안개 패스 fog.hlsl 이 읽고 컴퓨트 volumetricfog.hlsl 이 쓴다). 화면 칸 X × Y, 시선 거리의 로그 조각 Z 개다.
//     칸 번호는 (조각 × Y + y) × X + x, 원소는 float4(누적 산란 rgb, 누적 투과율). 조각 경계 거리는 fogcommon.hlsli 가 정의한다(가까운 거리 SW_FOG_VOLUME_NEAR 에서 먼 거리까지).
//     컴퓨트 입력(StructuredBuffer<float4> g_FogParams)은 float4 SW_FOG_VOLUME_PARAM_FLOAT4_COUNT 개이고 행 배치는 volumetricfog.hlsl 머리 주석이다.
// ------------------------------------------------------------------------------
#define SW_FOG_VOLUME_DIM_X              80u
#define SW_FOG_VOLUME_DIM_Y              45u
#define SW_FOG_VOLUME_DIM_Z              32u
#define SW_FOG_VOLUME_NEAR               0.3f
#define SW_FOG_VOLUME_PARAM_FLOAT4_COUNT 15u
#define SW_FOG_VOLUME_GROUP_SIZE         8u

#endif // SW_ENGINE_BINDINGSLOTS_HLSLI
