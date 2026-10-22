/**
 * materialshader.hlsli — 머티리얼 셰이더 뼈대의 1단: 훅이 주고받는 형식과 훅 선택 검사.
 *
 * 엔진이 정점 · 픽셀 진입점(VSMain / PSMain)을 가지고(materialshadermain.hlsli), 머티리얼 셰이더 파일은 **훅만 정의한다**.
 * 위치를 만드는 코드는 뼈대 한 곳뿐이다 — 훅이 클립 위치를 직접 만들 수 없으므로 깊이 프리패스 · 그림자 · 본 패스의 깊이가 같은 비트로 남는다.
 *
 * 셰이더 파일의 순서(순서가 계약이다)
 *   1. `#include "lighting.hlsli"`
 *   2. `SW_MATERIAL_BEGIN { … } SW_MATERIAL_END`        머티리얼 구조체(.material 의 프로퍼티와 이름 · 순서가 같다)
 *   3. 선택 매크로                                       아래 "훅 선택" 표
 *   4. 사용자 보간 칸 구조체 `MaterialVaryings`          SW_MATERIAL_CUSTOM_VARYINGS 일 때만
 *   5. `#include "materialshader.hlsli"`                 이 파일 — 훅의 입출력 형식
 *   6. 훅 정의
 *   7. `#include "materialshadermain.hlsli"`            진입점 뼈대
 *
 * 훅 다섯 — 있으면 쓰고 없으면 기본이라는 폴백은 없다. 훅마다 정의하거나 기본을 명시해 고른다.
 *   표면      `SwMaterialSurface computeMaterialSurface( SwPixelContext context )`         기본 없음 — 머티리얼마다 정의한다
 *   조명 모델 `float4 computeMaterialLighting( SwPixelContext context, SwMaterialSurface surface )`
 *             rgb 는 조명을 받은 색, a 는 알파다(알파를 쓰는지는 뼈대가 MATERIAL_BLEND_TRANSLUCENT 로 정한다).
 *             SW_MATERIAL_CUSTOM_LIGHTING(훅 정의) · SW_MATERIAL_DEFAULT_LIGHTING(PBR) · SW_MATERIAL_UNLIT(표면 색 그대로) 중 하나.
 *   월드 위치 오프셋 `float3 computeWorldPositionOffset( SwVertexContext context )`        MATERIAL_VERTEX_DEFORM 퍼뮤테이션에서만 부른다
 *   사용자 보간 칸  `MaterialVaryings computeMaterialVaryings( SwVertexContext context )`
 *             SW_MATERIAL_CUSTOM_VARYINGS(구조체 + 훅 정의) · SW_MATERIAL_DEFAULT_VARYINGS(칸 없음) 중 하나. 시맨틱은 TEXCOORD8 ~ TEXCOORD14.
 *   외곽선    `SwOutlineOffset computeMaterialOutline( SwVertexContext context )`  메시 외곽선 패스(SW_PASS_MESH_OUTLINE)에서만 부른다.
 * 선택이 없거나 겹치면 이 파일의 `#error` 가 멈춘다. 훅 정의가 없는데 선택했으면 컴파일러가 미선언 식별자로 멈춘다.
 *
 * 머티리얼은 **한 단계에서만 읽는다**(GL 은 구조버퍼를 정점 · 픽셀 두 단계가 읽으면 링크를 거절한다 — binding.hlsli 9 절).
 * 읽는 단계는 형식이 정한다. 기본은 픽셀이고, `SW_MATERIAL_READ_IN_VERTEX` 를 정의하면 정점이다. 안 읽는 단계의 문맥에는 `material` 멤버가 없어서
 * 거기서 머티리얼을 읽으려는 코드는 컴파일이 안 된다. 정점에서 읽는 셰이더는 픽셀이 쓸 값을 사용자 보간 칸으로 넘긴다.
 *
 * 셰이더 안의 기능 스위치(외부 define 이 아니다 — 쿠킹 퍼뮤테이션 수가 늘지 않는다)
 *   SW_MATERIAL_READ_IN_VERTEX   머티리얼을 정점에서 읽는다
 *   SW_MATERIAL_USES_TANGENT     정점 탄젠트를 월드로 옮겨 `SwPixelContext.tangent` 로 넘긴다(노멀 맵)
 *
 * 훅 안에서 이른 반환(중간 `return`)을 쓰지 않는다. 뼈대는 출구가 하나이고 훅이 인라인되어도 그래야 한다 — Vfx V5 에서 드라이버가 이른 반환 모양을 틀리게
 * 컴파일했을 가능성이 있다(docs/plans/Vfx.md V4 남은 확인). 형식으로 막지 못하는 틈이라 리뷰가 본다.
 */

#ifndef SW_ENGINE_MATERIALSHADER_HLSLI
#define SW_ENGINE_MATERIALSHADER_HLSLI

#include "lighting.hlsli"

// ------------------------------------------------------------------------------
// 1) 패스 종류와 훅 선택 검사
// ------------------------------------------------------------------------------
#if defined( SW_PASS_SHADOW ) || defined( SW_PASS_DEPTH_PREPASS )
#define SW_MATERIAL_DEPTH_PASS 1
#endif

#if defined( SW_PASS_SHADOW ) && defined( SW_PASS_DEPTH_PREPASS )
#error "SW_PASS_SHADOW and SW_PASS_DEPTH_PREPASS are exclusive pass kinds"
#endif

// 조건식 안의 `defined` 산술은 컴파일러마다 다르므로(FXC) 조합을 풀어 쓴다.
#if !defined( SW_MATERIAL_DEFAULT_LIGHTING ) && !defined( SW_MATERIAL_UNLIT ) && !defined( SW_MATERIAL_CUSTOM_LIGHTING )
#error "pick one lighting hook: SW_MATERIAL_CUSTOM_LIGHTING (define computeMaterialLighting), SW_MATERIAL_DEFAULT_LIGHTING or SW_MATERIAL_UNLIT"
#endif
#if ( defined( SW_MATERIAL_DEFAULT_LIGHTING ) && defined( SW_MATERIAL_UNLIT ) ) || ( defined( SW_MATERIAL_DEFAULT_LIGHTING ) && defined( SW_MATERIAL_CUSTOM_LIGHTING ) ) || \
	( defined( SW_MATERIAL_UNLIT ) && defined( SW_MATERIAL_CUSTOM_LIGHTING ) )
#error "pick exactly one lighting hook: SW_MATERIAL_CUSTOM_LIGHTING, SW_MATERIAL_DEFAULT_LIGHTING and SW_MATERIAL_UNLIT are exclusive"
#endif

#if !defined( SW_MATERIAL_DEFAULT_VARYINGS ) && !defined( SW_MATERIAL_CUSTOM_VARYINGS )
#error "pick one varyings hook: SW_MATERIAL_CUSTOM_VARYINGS (define MaterialVaryings and computeMaterialVaryings) or SW_MATERIAL_DEFAULT_VARYINGS"
#endif
#if defined( SW_MATERIAL_DEFAULT_VARYINGS ) && defined( SW_MATERIAL_CUSTOM_VARYINGS )
#error "pick exactly one varyings hook: SW_MATERIAL_CUSTOM_VARYINGS and SW_MATERIAL_DEFAULT_VARYINGS are exclusive"
#endif

// ------------------------------------------------------------------------------
// 2) 사용자 보간 칸 — 기본이면 빈 구조체다(FXC · DXC DXIL · DXC SPIR-V 가 빈 구조체 · 중첩 구조체 보간 칸을 받는다, 확인함).
// ------------------------------------------------------------------------------
#if defined( SW_MATERIAL_DEFAULT_VARYINGS )
struct SwNoMaterialVaryings
{
};
#define SW_MATERIAL_VARYINGS_TYPE SwNoMaterialVaryings
#else
#define SW_MATERIAL_VARYINGS_TYPE MaterialVaryings
#endif

// ------------------------------------------------------------------------------
// 3) 문맥 — 훅이 읽는 값. 정점 문맥에는 입력 스트림 · 인스턴스 · 변형이 끝난 위치와 노멀이 있다.
// ------------------------------------------------------------------------------
struct SwVertexContext
{
	SwVertexInput  input;         ///< 입력 스트림 그대로(색 · UV · 탄젠트)
	SwInstanceData instance;
	uint           vertexId;
	float3         localPosition; ///< 모프 · 스킨 · VAT 가 적용된 로컬 위치
	float3         localNormal;
	float3         worldPosition; ///< 뼈대가 만든 월드 위치(월드 위치 오프셋을 더하기 전)
	float3         worldNormal;
#if defined( SW_MATERIAL_READ_IN_VERTEX )
	SwMaterialData material;      ///< 머티리얼을 정점에서 읽는 셰이더에만 있다
#endif
};

#if defined( SW_PASS_MESH_OUTLINE )
/** @brief 외곽선 훅의 결과 — 월드 위치에 더할 오프셋과, 클립 위치를 만든 뒤 xy 에 더할 오프셋(화면 두께). 훅은 위치를 직접 만들지 못하고 오프셋만 준다. */
struct SwOutlineOffset
{
	float3 worldOffset;
	float2 clipOffset;
};
#endif

/** @brief 정점 → 픽셀 보간 칸. 모든 패스가 같은 칸을 쓴다(훅이 패스마다 다른 문맥을 받지 않게). 위치는 뼈대 한 곳이 만든다. */
struct SwMaterialInterpolants
{
	float4 position      : SV_POSITION;
	float4 color         : COLOR;
	float2 uv            : TEXCOORD0;
	float3 normal        : TEXCOORD1;
	float3 worldPosition : TEXCOORD2;
#if defined( SW_MATERIAL_USES_TANGENT )
	float4 tangent       : TEXCOORD3; // xyz 월드 탄젠트 · w 바이탄젠트 부호(정점 탄젠트 그대로)
#endif
#if !defined( SW_MATERIAL_READ_IN_VERTEX )
	nointerpolation uint materialIndex : TEXCOORD4; // GPUScene 인스턴스가 준 머티리얼 원소
#endif
	SW_MATERIAL_VARYINGS_TYPE user; // 사용자 보간 칸(시맨틱은 사용자 구조체가 정한다)
	SW_APPEARANCE_VARYING // 등장 디더 변형만 등장 비율을 넘긴다(binding.hlsli 8-1)
};

/** @brief 픽셀 문맥 — 보간된 값과 머티리얼. 양면 머티리얼(MATERIAL_TWO_SIDED)의 뒷면은 `normal` 이 이미 뒤집혀 있다. */
struct SwPixelContext
{
	float4 position;      ///< SV_POSITION(화면 픽셀 좌표 · 깊이)
	float4 color;         ///< 정점 색
	float2 uv;
	float3 normal;        ///< 정규화한 월드 노멀
	float3 worldPosition;
#if defined( SW_MATERIAL_USES_TANGENT )
	float4 tangent;
#endif
	SW_MATERIAL_VARYINGS_TYPE user;
#if !defined( SW_MATERIAL_READ_IN_VERTEX )
	SwMaterialData material;      ///< 머티리얼을 픽셀에서 읽는 셰이더에만 있다
#endif
};

/**
 * @brief 표면 훅의 결과 — 알베도 · 노멀 · 거칠기 · 금속도(보기 모드 Roughness · Metallic 과 G버퍼 머티리얼 칸이 읽는다).
 * @details 알파 컷오프 머티리얼(MATERIAL_ALPHA_CUTOFF)은 `alphaCutoff` 도 채운다 — 깊이 패스와 본 패스가 같은 값으로 오린다(뼈대가 clip 한다).
 */
struct SwMaterialSurface
{
	float4 albedo;
	float3 normal;
	float  roughness;
	float  metallic;
#if defined( MATERIAL_ALPHA_CUTOFF )
	float  alphaCutoff;
#endif
};

#endif // SW_ENGINE_MATERIALSHADER_HLSLI
